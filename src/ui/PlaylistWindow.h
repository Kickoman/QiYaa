// The Winamp playlist editor: resizable in 25x29 px steps, skinned by
// PLEDIT.BMP and PLEDIT.TXT, shows the player's queue.
#pragma once

#include <QSet>

#include "ui/SkinnedWindow.h"

namespace qiyaa {

class Player;

class PlaylistWindow : public SkinnedWindow {
    Q_OBJECT
public:
    PlaylistWindow(Player* player, const Skin* skin, QWidget* parent = nullptr);

    // Size in resize steps beyond the minimum 275x116 (Winamp's playlist "segments").
    QSize sizeSteps() const { return m_steps; }
    void setSizeSteps(QSize steps);

    int visibleRows() const;
    int scrollOffset() const { return m_scroll; }
    void setScrollOffset(int row);
    void ensureRowVisible(int row);
    const QSet<int>& selection() const { return m_selected; }
    // Row under a point in skin coordinates, or -1.
    int rowAt(QPoint skinPos) const;

Q_SIGNALS:
    void closeRequested();
    void sizeStepsChanged(QSize steps);
    // Bottom "ADD" button / context menu: the owner shows the sources menu at `globalPos`.
    void sourcesMenuRequested(QPoint globalPos);

protected:
    void closeEvent(QCloseEvent* e) override;
    void paintSkin(QPainter& p) override;
    bool isDragArea(QPoint pos) const override;
    bool skinMousePress(QPoint pos, Qt::MouseButton button) override;
    void skinMouseMove(QPoint pos) override;
    void skinMouseRelease(QPoint pos, Qt::MouseButton button) override;
    bool skinMouseDoubleClick(QPoint pos, Qt::MouseButton button) override;
    void wheelEvent(QWheelEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void contextMenuEvent(QContextMenuEvent* e) override;

private:
    enum class Drag { None, Scroll, Resize, Close, Button };
    QRect listRect() const;
    QRect scrollHandleRect() const;
    int maxScroll() const;
    void drawTiles(QPainter& p) const;
    void drawRows(QPainter& p) const;
    void drawBottomInfo(QPainter& p) const;
    void popupAt(QMenu* menu, QPoint skinPos);
    int miniButtonAt(QPoint p) const;
    void selectRow(int row, Qt::KeyboardModifiers mods);

    Player* m_player;
    QSize m_steps{0, 4};
    int m_scroll = 0;
    QSet<int> m_selected;
    int m_anchor = -1;   // fixed end of a Shift range
    int m_cursor = -1;   // row the keyboard is on
    int m_shownSecond = -1;

    Drag m_drag = Drag::None;
    QPoint m_dragStart;
    QSize m_dragStartSteps;
    int m_dragStartScroll = 0;
    int m_pressedButton = -1;
};

}  // namespace qiyaa
