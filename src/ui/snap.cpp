#include "ui/snap.h"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <optional>

namespace Ui {
namespace {

// Exclusive edges as in webamp; QRect::right()/bottom() are inclusive (x + w - 1).
int LeftEdge(const QRect& rect) {
    return rect.x();
}
int TopEdge(const QRect& rect) {
    return rect.y();
}
int RightEdge(const QRect& rect) {
    return rect.x() + rect.width();
}
int BottomEdge(const QRect& rect) {
    return rect.y() + rect.height();
}

bool WithinDistance(int a, int value, int d) {
    return std::abs(a - value) < d;
}

bool OverlapX(const QRect& a, const QRect& b, int d) {
    return LeftEdge(a) <= RightEdge(b) + d && LeftEdge(b) <= RightEdge(a) + d;
}
bool OverlapY(const QRect& a, const QRect& b, int d) {
    return TopEdge(a) <= BottomEdge(b) + d && TopEdge(b) <= BottomEdge(a) + d;
}

struct Snapped {
    std::optional<int> x;
    std::optional<int> y;
};

Snapped SnapOne(const QRect& a, const QRect& b, int d) {
    Snapped s;
    if (OverlapY(a, b, d)) {
        if (WithinDistance(LeftEdge(a), RightEdge(b), d)) {
            s.x = RightEdge(b);
        } else if (WithinDistance(RightEdge(a), LeftEdge(b), d)) {
            s.x = LeftEdge(b) - a.width();
        } else if (WithinDistance(LeftEdge(a), LeftEdge(b), d)) {
            s.x = LeftEdge(b);
        } else if (WithinDistance(RightEdge(a), RightEdge(b), d)) {
            s.x = RightEdge(b) - a.width();
        }
    }
    if (OverlapX(a, b, d)) {
        if (WithinDistance(TopEdge(a), BottomEdge(b), d)) {
            s.y = BottomEdge(b);
        } else if (WithinDistance(BottomEdge(a), TopEdge(b), d)) {
            s.y = TopEdge(b) - a.height();
        } else if (WithinDistance(TopEdge(a), TopEdge(b), d)) {
            s.y = TopEdge(b);
        } else if (WithinDistance(BottomEdge(a), BottomEdge(b), d)) {
            s.y = BottomEdge(b) - a.height();
        }
    }
    return s;
}

long long DistanceSquared(const QRect& a, const QRect& b) {
    const long long dx = std::max({0, LeftEdge(b) - RightEdge(a), LeftEdge(a) - RightEdge(b)});
    const long long dy = std::max({0, TopEdge(b) - BottomEdge(a), TopEdge(a) - BottomEdge(b)});
    return dx * dx + dy * dy;
}

}  // namespace

QPoint SnapToOthers(const QRect& moving, const QList<QRect>& others, int distance) {
    QPoint point = moving.topLeft();
    for (const QRect& other : others) {
        const Snapped s = SnapOne(moving, other, distance);
        if (s.x) {
            point.setX(*s.x);
        }
        if (s.y) {
            point.setY(*s.y);
        }
    }
    return point;
}

QPoint SnapWithin(const QRect& moving, const QRect& screen, int distance) {
    QPoint point = moving.topLeft();
    if (LeftEdge(moving) - distance < LeftEdge(screen)) {
        point.setX(LeftEdge(screen));
    } else if (RightEdge(moving) + distance > RightEdge(screen)) {
        point.setX(RightEdge(screen) - moving.width());
    }

    if (TopEdge(moving) - distance < TopEdge(screen)) {
        point.setY(TopEdge(screen));
    } else if (BottomEdge(moving) + distance > BottomEdge(screen)) {
        point.setY(BottomEdge(screen) - moving.height());
    }
    return point;
}

QRect PickScreen(const QRect& rect, const QList<QRect>& screens) {
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
    if (bestArea > 0 || screens.isEmpty()) {
        return best;
    }

    long long bestDist = std::numeric_limits<long long>::max();
    for (const QRect& s : screens) {
        const long long dist = DistanceSquared(rect, s);
        if (dist < bestDist) {
            bestDist = dist;
            best = s;
        }
    }
    return best;
}

QPoint ClampInside(const QRect& rect, const QRect& screen) {
    if (screen.isEmpty()) {
        return rect.topLeft();
    }
    int x = rect.x();
    int y = rect.y();
    if (RightEdge(rect) > RightEdge(screen)) {
        x = RightEdge(screen) - rect.width();
    }
    if (BottomEdge(rect) > BottomEdge(screen)) {
        y = BottomEdge(screen) - rect.height();
    }
    if (x < LeftEdge(screen)) {
        x = LeftEdge(screen);
    }
    if (y < TopEdge(screen)) {
        y = TopEdge(screen);
    }
    return {x, y};
}

bool Touching(const QRect& a, const QRect& b) {
    const bool yOverlap = TopEdge(a) < BottomEdge(b) && TopEdge(b) < BottomEdge(a);
    const bool xOverlap = LeftEdge(a) < RightEdge(b) && LeftEdge(b) < RightEdge(a);
    if (yOverlap && (RightEdge(a) == LeftEdge(b) || RightEdge(b) == LeftEdge(a))) {
        return true;
    }
    if (xOverlap && (BottomEdge(a) == TopEdge(b) || BottomEdge(b) == TopEdge(a))) {
        return true;
    }
    return false;
}

QList<int> ConnectedGroup(int start, const QList<QRect>& rects) {
    QList<int> group;
    QList<int> queue{start};
    QList<bool> seen(rects.size(), false);
    if (start < 0 || start >= rects.size()) {
        return group;
    }
    seen[start] = true;
    while (!queue.isEmpty()) {
        const int cur = queue.takeFirst();
        for (int i = 0; i < rects.size(); ++i) {
            if (seen[i] || !Touching(rects[cur], rects[i])) {
                continue;
            }
            seen[i] = true;
            group << i;
            queue << i;
        }
    }
    return group;
}

QList<int> StackBelow(int self, const QList<QRect>& rects, int dy, const QList<bool>& solid) {
    if (self < 0 || self >= rects.size() || dy == 0) {
        return {};
    }
    const auto isSolid = [&](int i) { return solid.isEmpty() || solid.value(i, true); };
    const QRect old = rects[self];
    const QRect grown = old.adjusted(0, 0, 0, dy);
    const auto hangsFrom = [](const QRect& upper, const QRect& lower) {
        return BottomEdge(upper) == TopEdge(lower) && LeftEdge(upper) < RightEdge(lower)
            && LeftEdge(lower) < RightEdge(upper);
    };
    const int n = int(rects.size());
    QList<bool> blocked(n, false);
    for (;;) {
        QList<bool> moving(n, false);
        for (bool changed = true; changed;) {
            changed = false;
            for (int i = 0; i < n; ++i) {
                if (i == self || moving[i] || blocked[i] || TopEdge(rects[i]) < BottomEdge(old)) {
                    continue;
                }
                bool follows = hangsFrom(old, rects[i]);
                for (int j = 0; j < n && !follows; ++j) {
                    follows = moving[j] && hangsFrom(rects[j], rects[i]);
                }
                if (dy > 0) {
                    follows = follows || grown.intersects(rects[i]);
                    for (int j = 0; j < n && !follows; ++j) {
                        follows = moving[j] && rects[j].translated(0, dy).intersects(rects[i]);
                    }
                } else if (follows) {
                    for (int j = 0; j < n; ++j) {
                        if (j != self && j != i && !moving[j] && isSolid(j)
                            && hangsFrom(rects[j], rects[i])) {
                            follows = false;
                        }
                    }
                }
                if (follows) {
                    moving[i] = changed = true;
                }
            }
        }
        bool conflict = false;
        if (dy < 0) {
            for (int i = 0; i < n; ++i) {
                if (!moving[i]) {
                    continue;
                }
                const QRect moved = rects[i].translated(0, dy);
                for (int j = 0; j < n; ++j) {
                    if (j != self && j != i && !moving[j] && isSolid(j)
                        && moved.intersects(rects[j])) {
                        blocked[i] = conflict = true;
                    }
                }
            }
        }
        if (!conflict) {
            QList<int> out;
            for (int i = 0; i < n; ++i) {
                if (moving[i]) {
                    out << i;
                }
            }
            return out;
        }
    }
}

QPoint ResolveDragPosition(
    const QRect& proposed,
    const QList<QRect>& others,
    const QList<QRect>& screens,
    int distance
) {
    QRect rect = proposed;
    rect.moveTopLeft(SnapToOthers(rect, others, distance));
    const QRect screen = PickScreen(rect, screens);
    if (screen.isEmpty()) {
        return rect.topLeft();
    }
    rect.moveTopLeft(SnapWithin(rect, screen, distance));
    return ClampInside(rect, screen);
}

}  // namespace Ui
