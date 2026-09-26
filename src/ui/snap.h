// Port of webamp's snapUtils.ts (MIT, (c) Jordan Eldredge), see THIRD_PARTY.md.
#pragma once

#include <QList>
#include <QPoint>
#include <QRect>

namespace Ui {

inline constexpr int kSnapDistance = 15;

QPoint SnapToOthers(const QRect& moving, const QList<QRect>& others, int distance = kSnapDistance);

QPoint SnapWithin(const QRect& moving, const QRect& screen, int distance = kSnapDistance);

QRect PickScreen(const QRect& rect, const QList<QRect>& screens);

QPoint ClampInside(const QRect& rect, const QRect& screen);

bool Touching(const QRect& a, const QRect& b);

QList<int> ConnectedGroup(int start, const QList<QRect>& rects);

QList<int> StackBelow(int self, const QList<QRect>& rects, int dy, const QList<bool>& solid = {});

QPoint ResolveDragPosition(
    const QRect& proposed,
    const QList<QRect>& others,
    const QList<QRect>& screens,
    int distance = kSnapDistance
);

}  // namespace Ui
