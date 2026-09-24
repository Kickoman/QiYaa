#include "ui/Snap.h"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <optional>

namespace qiyaa::snap {
namespace {

// QRect::right()/bottom() are inclusive (x + w - 1). webamp uses exclusive edges,
// so we use our own helpers everywhere.
int left(const QRect& r) { return r.x(); }
int top(const QRect& r) { return r.y(); }
int right(const QRect& r) { return r.x() + r.width(); }
int bottom(const QRect& r) { return r.y() + r.height(); }

bool near(int a, int b, int d) { return std::abs(a - b) < d; }

bool overlapX(const QRect& a, const QRect& b, int d) {
    return left(a) <= right(b) + d && left(b) <= right(a) + d;
}
bool overlapY(const QRect& a, const QRect& b, int d) {
    return top(a) <= bottom(b) + d && top(b) <= bottom(a) + d;
}

struct Snapped {
    std::optional<int> x;
    std::optional<int> y;
};

Snapped snapOne(const QRect& a, const QRect& b, int d) {
    Snapped s;
    if (overlapY(a, b, d)) {
        if (near(left(a), right(b), d))
            s.x = right(b);
        else if (near(right(a), left(b), d))
            s.x = left(b) - a.width();
        else if (near(left(a), left(b), d))
            s.x = left(b);
        else if (near(right(a), right(b), d))
            s.x = right(b) - a.width();
    }
    if (overlapX(a, b, d)) {
        if (near(top(a), bottom(b), d))
            s.y = bottom(b);
        else if (near(bottom(a), top(b), d))
            s.y = top(b) - a.height();
        else if (near(top(a), top(b), d))
            s.y = top(b);
        else if (near(bottom(a), bottom(b), d))
            s.y = bottom(b) - a.height();
    }
    return s;
}

long long distanceSquared(const QRect& a, const QRect& b) {
    const long long dx = std::max({0, left(b) - right(a), left(a) - right(b)});
    const long long dy = std::max({0, top(b) - bottom(a), top(a) - bottom(b)});
    return dx * dx + dy * dy;
}

}  // namespace

QPoint snapToOthers(const QRect& moving, const QList<QRect>& others, int distance) {
    QPoint p = moving.topLeft();
    for (const QRect& other : others) {
        const Snapped s = snapOne(moving, other, distance);
        if (s.x) p.setX(*s.x);
        if (s.y) p.setY(*s.y);
    }
    return p;
}

QPoint snapWithin(const QRect& moving, const QRect& screen, int distance) {
    QPoint p = moving.topLeft();
    if (left(moving) - distance < left(screen))
        p.setX(left(screen));
    else if (right(moving) + distance > right(screen))
        p.setX(right(screen) - moving.width());

    if (top(moving) - distance < top(screen))
        p.setY(top(screen));
    else if (bottom(moving) + distance > bottom(screen))
        p.setY(bottom(screen) - moving.height());
    return p;
}

QRect pickScreen(const QRect& rect, const QList<QRect>& screens) {
    QRect best;
    long long bestArea = -1;
    for (const QRect& s : screens) {
        const QRect i = s.intersected(rect);
        const long long area = i.isEmpty() ? 0 : 1LL * i.width() * i.height();
        if (area > bestArea) {
            bestArea = area;
            best = s;
        }
    }
    if (bestArea > 0 || screens.isEmpty()) return best;

    long long bestDist = std::numeric_limits<long long>::max();
    for (const QRect& s : screens) {
        const long long dist = distanceSquared(rect, s);
        if (dist < bestDist) {
            bestDist = dist;
            best = s;
        }
    }
    return best;
}

QPoint clampInside(const QRect& rect, const QRect& screen) {
    if (screen.isEmpty()) return rect.topLeft();
    int x = rect.x();
    int y = rect.y();
    if (right(rect) > right(screen)) x = right(screen) - rect.width();
    if (bottom(rect) > bottom(screen)) y = bottom(screen) - rect.height();
    // Top-left wins if the window is bigger than the screen.
    if (x < left(screen)) x = left(screen);
    if (y < top(screen)) y = top(screen);
    return {x, y};
}

bool touching(const QRect& a, const QRect& b) {
    const bool yOverlap = top(a) < bottom(b) && top(b) < bottom(a);
    const bool xOverlap = left(a) < right(b) && left(b) < right(a);
    if (yOverlap && (right(a) == left(b) || right(b) == left(a))) return true;
    if (xOverlap && (bottom(a) == top(b) || bottom(b) == top(a))) return true;
    return false;
}

QList<int> connectedGroup(int start, const QList<QRect>& rects) {
    QList<int> group;
    QList<int> queue{start};
    QList<bool> seen(rects.size(), false);
    if (start < 0 || start >= rects.size()) return group;
    seen[start] = true;
    while (!queue.isEmpty()) {
        const int cur = queue.takeFirst();
        for (int i = 0; i < rects.size(); ++i) {
            if (seen[i] || !touching(rects[cur], rects[i])) continue;
            seen[i] = true;
            group << i;
            queue << i;
        }
    }
    return group;
}

QList<int> stackBelow(int self, const QList<QRect>& rects, int dy, const QList<bool>& solid) {
    if (self < 0 || self >= rects.size() || dy == 0) return {};
    const auto isSolid = [&](int i) { return solid.isEmpty() || solid.value(i, true); };
    const QRect old = rects[self];
    const QRect grown = old.adjusted(0, 0, 0, dy);
    const auto hangsFrom = [](const QRect& upper, const QRect& lower) {
        return bottom(upper) == top(lower) && left(upper) < right(lower) && left(lower) < right(upper);
    };
    const int n = int(rects.size());
    QList<bool> blocked(n, false);
    for (;;) {
        QList<bool> moving(n, false);
        for (bool changed = true; changed;) {
            changed = false;
            for (int i = 0; i < n; ++i) {
                if (i == self || moving[i] || blocked[i] || top(rects[i]) < bottom(old)) continue;
                bool follows = hangsFrom(old, rects[i]);
                for (int j = 0; j < n && !follows; ++j) follows = moving[j] && hangsFrom(rects[j], rects[i]);
                if (dy > 0) {
                    // Growing: anything we'd grow into gets pushed too.
                    follows = follows || grown.intersects(rects[i]);
                    for (int j = 0; j < n && !follows; ++j) follows = moving[j] && rects[j].translated(0, dy).intersects(rects[i]);
                } else if (follows) {
                    // Shrinking: another window that stays still holds it up.
                    for (int j = 0; j < n; ++j)
                        if (j != self && j != i && !moving[j] && isSolid(j) && hangsFrom(rects[j], rects[i])) follows = false;
                }
                if (follows) moving[i] = changed = true;
            }
        }
        // Shrinking must not pull a window onto one that stays.
        bool conflict = false;
        if (dy < 0) {
            for (int i = 0; i < n; ++i) {
                if (!moving[i]) continue;
                const QRect moved = rects[i].translated(0, dy);
                for (int j = 0; j < n; ++j)
                    if (j != self && j != i && !moving[j] && isSolid(j) && moved.intersects(rects[j])) blocked[i] = conflict = true;
            }
        }
        if (!conflict) {
            QList<int> out;
            for (int i = 0; i < n; ++i)
                if (moving[i]) out << i;
            return out;
        }
    }
}

QPoint resolveDragPosition(const QRect& proposed, const QList<QRect>& others,
                           const QList<QRect>& screens, int distance) {
    QRect r = proposed;
    r.moveTopLeft(snapToOthers(r, others, distance));
    const QRect screen = pickScreen(r, screens);
    if (screen.isEmpty()) return r.topLeft();
    r.moveTopLeft(snapWithin(r, screen, distance));
    return clampInside(r, screen);
}

}  // namespace qiyaa::snap
