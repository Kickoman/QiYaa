#pragma once

#include "ui/skinned_window.h"

#include <QPoint>
#include <QRect>
#include <QSet>
#include <QSize>
#include <QWidget>

class QMenu;
class QPainter;

namespace Core {
class Player;
}  // namespace Core

namespace Ui {

class PlaylistWindow : public SkinnedWindow {
    Q_OBJECT
public:
    PlaylistWindow(Core::Player* player, const Skins::Skin* skin, QWidget* parent = nullptr);

    QSize sizeSteps() const { return resizeSteps; }
    void setSizeSteps(QSize steps);
    void setShaded(bool shaded) override;

    int visibleRows() const;
    int scrollOffset() const { return scrollRow; }
    void setScrollOffset(int row);
    void ensureRowVisible(int row);
    const QSet<int>& selection() const { return selectedRows; }
    int rowAt(QPoint skinPos) const;

Q_SIGNALS:
    void closeRequested();
    void sizeStepsChanged(QSize steps);
    void sourcesMenuRequested(QPoint globalPos);

protected:
    void closeEvent(QCloseEvent* event) override;
    void paintSkin(QPainter& painter) override;
    bool isDragArea(QPoint pos) const override;
    bool skinMousePress(QPoint pos, Qt::MouseButton button) override;
    void skinMouseMove(QPoint pos) override;
    void skinMouseRelease(QPoint pos, Qt::MouseButton button) override;
    bool skinMouseDoubleClick(QPoint pos, Qt::MouseButton button) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    enum class Drag { None, Scroll, Resize, Close, Shade, Button };
    QSize fullSkinSize() const;
    void paintShaded(QPainter& painter);
    QRect listRect() const;
    QRect scrollHandleRect() const;
    int maxScroll() const;
    void drawTiles(QPainter& painter) const;
    void drawRows(QPainter& painter) const;
    void drawBottomInfo(QPainter& painter) const;
    void popupAt(QMenu* menu, QPoint skinPos);
    int miniButtonAt(QPoint point) const;
    void selectRow(int row, Qt::KeyboardModifiers mods);

    Core::Player* corePlayer;
    QSize resizeSteps{0, 4};
    int scrollRow = 0;
    QSet<int> selectedRows;
    int anchor = -1;
    int cursorRow = -1;
    int shownSecond = -1;

    Drag activeDrag = Drag::None;
    QPoint dragStart;
    QSize dragStartSteps;
    int dragStartScroll = 0;
    int pressedButton = -1;
};

}  // namespace Ui
