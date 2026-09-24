// Window snapping helpers.
// Logic ported from webamp's snapUtils.ts (MIT, (c) Jordan Eldredge), see THIRD_PARTY.md.
#pragma once

#include <QList>
#include <QPoint>
#include <QRect>

namespace qiyaa::snap {

// Distance (in device-independent pixels) at which windows snap together.
inline constexpr int kSnapDistance = 15;

// Returns the position `moving` should have so that it sticks to the edges of any
// of `others` that are within kSnapDistance. Axes that don't snap keep their value.
QPoint snapToOthers(const QRect& moving, const QList<QRect>& others,
                    int distance = kSnapDistance);

// Snaps `moving` to the inner edges of `screen` (available geometry) when near.
QPoint snapWithin(const QRect& moving, const QRect& screen, int distance = kSnapDistance);

// Picks the screen `rect` belongs to: the one with the largest intersection, or
// the nearest one if it doesn't intersect any. Returns an empty rect if `screens` is empty.
QRect pickScreen(const QRect& rect, const QList<QRect>& screens);

// Moves `rect` so that it lies fully inside `screen` (if it fits; otherwise
// top-left is kept visible). This is what guarantees windows never get lost off-screen.
QPoint clampInside(const QRect& rect, const QRect& screen);

// True if two windows share an edge (distance 0) and overlap along it —
// Winamp's definition of "docked".
bool touching(const QRect& a, const QRect& b);

// Indices of all rects connected to rects[start] through a chain of touching
// rects (not including `start` itself).
QList<int> connectedGroup(int start, const QList<QRect>& rects);

// Full pipeline used while dragging: snap to other windows, then to the screen
// edges, then clamp inside the screen the window is on.
QPoint resolveDragPosition(const QRect& proposed, const QList<QRect>& others,
                           const QList<QRect>& screens, int distance = kSnapDistance);

}  // namespace qiyaa::snap
