#include "ui/playlist_window.h"

#include "core/player.h"
#include "skins/skin.h"
#include "skins/sprites.h"

#include <QApplication>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMenu>
#include <QPainter>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace Ui {

using Audio::AudioEngine;
using TSheet = Skins::Skin::Sheet;
using Skins::PlaylistSprites;

namespace {

constexpr int kListPadTop = 3;

bool RectContains(const QRect& rect, QPoint point) {
    return point.x() >= rect.x() && point.y() >= rect.y() && point.x() < rect.x() + rect.width()
        && point.y() < rect.y() + rect.height();
}

QString FormatTime(qint64 seconds) {
    seconds = std::max<qint64>(0, seconds);
    if (seconds >= 3600) {
        return QStringLiteral("%1:%2:%3")
            .arg(seconds / 3600)
            .arg(seconds / 60 % 60, 2, 10, QLatin1Char('0'))
            .arg(seconds % 60, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

constexpr int kMiniX[] = {3, 11, 20, 29, 37, 45};
constexpr int kMiniWidth = 8;
constexpr int kMiniY = 22;
constexpr int kMiniHeight = 10;

constexpr int kBtnAdd = 0, kBtnRem = 1, kBtnSel = 2, kBtnMisc = 3, kBtnList = 4, kBtnMini0 = 10;

}  // namespace

PlaylistWindow::PlaylistWindow(Core::Player* player, const Skins::Skin* skin, QWidget* parent)
    : SkinnedWindow(
          skin,
          QSize(
              Skins::PlaylistSprites::kMinSize.width(),
              Skins::PlaylistSprites::kMinSize.height() + 4 * Skins::PlaylistSprites::kStepHeight
          ),
          parent
      )
    , corePlayer(player) {
    setWindowTitle(QStringLiteral("QiYaa Playlist"));
    setFocusPolicy(Qt::StrongFocus);
    connect(corePlayer, &Core::Player::queueReplaced, this, [this] {
        selectedRows.clear();
        anchor = cursorRow = -1;
        scrollRow = 0;
        update();
    });
    connect(corePlayer, &Core::Player::playlistChanged, this, [this] {
        const int n = int(corePlayer->playlist().size());
        QSet<int> valid;
        for (int i : selectedRows) {
            if (i < n) {
                valid.insert(i);
            }
        }
        selectedRows = valid;
        if (anchor >= n) {
            anchor = n - 1;
        }
        if (cursorRow >= n) {
            cursorRow = n - 1;
        }
        setScrollOffset(scrollRow);
        update();
    });
    connect(corePlayer, &Core::Player::positionTick, this, [this] {
        const int sec = int(corePlayer->engine()->positionSeconds());
        if (sec == shownSecond) {
            return;
        }
        shownSecond = sec;
        const QSize size = skinSize();
        updateSkinRect(QRect(
            size.width() - 150 + 66, size.height() - Skins::PlaylistSprites::kBottomHeight + 23, 25,
            6
        ));
    });
    connect(corePlayer, &Core::Player::currentTrackChanged, this, [this] {
        ensureRowVisible(corePlayer->currentIndex());
        update();
    });
    connect(corePlayer->engine(), &Audio::AudioEngine::stateChanged, this, [this] { update(); });
}

void PlaylistWindow::setSizeSteps(QSize steps) {
    steps = steps.expandedTo(QSize(0, 0)).boundedTo(QSize(40, 40));
    if (steps == resizeSteps) {
        return;
    }
    resizeSteps = steps;
    const QSize full = fullSkinSize();
    setSkinSize(isShaded() ? QSize(full.width(), 14) : full);
    setScrollOffset(scrollRow);
    Q_EMIT sizeStepsChanged(steps);
}

QRect PlaylistWindow::listRect() const {
    const QSize size = skinSize();
    return {
        Skins::PlaylistSprites::kLeftWidth, Skins::PlaylistSprites::kTopHeight,
        size.width() - Skins::PlaylistSprites::kLeftWidth - Skins::PlaylistSprites::kRightWidth,
        size.height() - Skins::PlaylistSprites::kTopHeight - Skins::PlaylistSprites::kBottomHeight
    };
}

int PlaylistWindow::visibleRows() const {
    return std::max(1, listRect().height() / Skins::PlaylistSprites::kRowHeight);
}

int PlaylistWindow::maxScroll() const {
    return std::max(0, int(corePlayer->playlist().size()) - visibleRows());
}

void PlaylistWindow::setScrollOffset(int row) {
    row = std::clamp(row, 0, maxScroll());
    if (row == scrollRow) {
        return;
    }
    scrollRow = row;
    update();
}

void PlaylistWindow::ensureRowVisible(int row) {
    if (row < 0) {
        return;
    }
    if (row < scrollRow) {
        setScrollOffset(row);
    } else if (row >= scrollRow + visibleRows()) {
        setScrollOffset(row - visibleRows() + 1);
    }
}

int PlaylistWindow::rowAt(QPoint point) const {
    const QRect rect = listRect();
    if (!RectContains(rect, point)) {
        return -1;
    }
    const int y = point.y() - rect.y() - kListPadTop;
    if (y < 0) {
        return -1;
    }
    const int visible = y / Skins::PlaylistSprites::kRowHeight;
    if (visible >= visibleRows()) {
        return -1;
    }
    const int row = scrollRow + visible;
    return row < corePlayer->playlist().size() ? row : -1;
}

QRect PlaylistWindow::scrollHandleRect() const {
    const QRect list = listRect();
    const int travel = std::max(0, list.height() - Skins::PlaylistSprites::kScrollHandle.height());
    const int max = maxScroll();
    const int y = list.y() + (max > 0 ? int(std::lround(double(scrollRow) / max * travel)) : 0);
    return {
        skinSize().width() - 15, y, Skins::PlaylistSprites::kScrollHandle.width(),
        Skins::PlaylistSprites::kScrollHandle.height()
    };
}

void PlaylistWindow::drawTiles(QPainter& painter) const {
    const Skins::Skin& activeSkin = skin();
    const int window = skinSize().width(), h = skinSize().height();
    const bool active = isActiveWindow();

    for (int x = 25; x < window - 25; x += 25) {
        activeSkin.draw(
            painter, TSheet::PlEdit,
            active ? Skins::PlaylistSprites::kTopTileSelected : Skins::PlaylistSprites::kTopTile,
            {x, 0}
        );
    }
    activeSkin.draw(
        painter, TSheet::PlEdit,
        active ? Skins::PlaylistSprites::kTopLeftSelected : Skins::PlaylistSprites::kTopLeft, {0, 0}
    );
    activeSkin.draw(
        painter, TSheet::PlEdit,
        active ? Skins::PlaylistSprites::kTitleSelected : Skins::PlaylistSprites::kTitle,
        {(window - 100) / 2, 0}
    );
    activeSkin.draw(
        painter, TSheet::PlEdit,
        active ? Skins::PlaylistSprites::kTopRightSelected : Skins::PlaylistSprites::kTopRight,
        {window - 25, 0}
    );

    for (int y = Skins::PlaylistSprites::kTopHeight; y < h - Skins::PlaylistSprites::kBottomHeight;
         y += 29) {
        const int tileH = std::min(29, h - Skins::PlaylistSprites::kBottomHeight - y);
        activeSkin.draw(
            painter, TSheet::PlEdit,
            Skins::PlaylistSprites::kLeftTile.adjusted(0, 0, 0, tileH - 29), {0, y}
        );
        activeSkin.draw(
            painter, TSheet::PlEdit,
            Skins::PlaylistSprites::kRightTile.adjusted(0, 0, 0, tileH - 29),
            {window - Skins::PlaylistSprites::kRightWidth, y}
        );
    }

    for (int x = 125; x < window - 150; x += 25) {
        activeSkin.draw(
            painter, TSheet::PlEdit, Skins::PlaylistSprites::kBottomTile,
            {x, h - Skins::PlaylistSprites::kBottomHeight}
        );
    }
    activeSkin.draw(
        painter, TSheet::PlEdit, Skins::PlaylistSprites::kBottomLeft,
        {0, h - Skins::PlaylistSprites::kBottomHeight}
    );
    activeSkin.draw(
        painter, TSheet::PlEdit, Skins::PlaylistSprites::kBottomRight,
        {window - 150, h - Skins::PlaylistSprites::kBottomHeight}
    );

    activeSkin.draw(
        painter, TSheet::PlEdit,
        activeDrag == Drag::Scroll ? Skins::PlaylistSprites::kScrollHandleSelected
                                   : Skins::PlaylistSprites::kScrollHandle,
        scrollHandleRect().topLeft()
    );
    if (activeDrag == Drag::Close) {
        activeSkin.draw(
            painter, TSheet::PlEdit, Skins::PlaylistSprites::kCloseSelected, {window - 11, 3}
        );
    }
    if (activeDrag == Drag::Shade) {
        activeSkin.draw(
            painter, TSheet::PlEdit, Skins::PlaylistSprites::kCollapseSelected, {window - 21, 3}
        );
    }
}

void PlaylistWindow::drawRows(QPainter& painter) const {
    const Skins::Skin::PlaylistStyle& style = skin().playlistStyle();
    const QRect list = listRect();
    painter.fillRect(list, style.normalBg);

    QFont font(style.font);
    font.setPixelSize(9);
    font.setLetterSpacing(QFont::AbsoluteSpacing, 0.5);
    painter.setFont(font);
    const QFontMetrics fm(font);

    const auto& tracks = corePlayer->playlist();
    const int current = corePlayer->currentIndex();
    painter.save();
    painter.setClipRect(list);
    for (int i = 0; i < visibleRows(); ++i) {
        const int row = scrollRow + i;
        if (row >= tracks.size()) {
            break;
        }
        const QRect rect(
            list.x(), list.y() + kListPadTop + i * Skins::PlaylistSprites::kRowHeight, list.width(),
            Skins::PlaylistSprites::kRowHeight
        );
        if (selectedRows.contains(row)) {
            painter.fillRect(rect, style.selectedBg);
        }
        painter.setPen(row == current ? style.current : style.normal);
        const QString duration = FormatTime(tracks[row].durationMs / 1000);
        const int durW = fm.horizontalAdvance(duration) + 3;
        const QRect titleRect = rect.adjusted(2, 0, -durW - 4, 0);
        const QString title = QStringLiteral("%1. %2").arg(row + 1).arg(tracks[row].displayTitle());
        painter.drawText(
            titleRect, Qt::AlignLeft | Qt::AlignVCenter,
            fm.elidedText(title, Qt::ElideRight, titleRect.width())
        );
        painter.drawText(rect.adjusted(0, 0, -3, 0), Qt::AlignRight | Qt::AlignVCenter, duration);
    }
    painter.restore();

    if (tracks.isEmpty()) {
        painter.setPen(style.normal);
        painter.drawText(
            list.adjusted(4, kListPadTop, -4, 0), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
            QStringLiteral("Плейлист пуст.\nПравый клик или кнопка ADD — выбрать, что слушать.")
        );
    }
}

void PlaylistWindow::drawBottomInfo(QPainter& painter) const {
    const int window = skinSize().width(), h = skinSize().height();
    const QPoint base(window - 150, h - Skins::PlaylistSprites::kBottomHeight);

    qint64 total = 0, selected = 0;
    const auto& tracks = corePlayer->playlist();
    for (int i = 0; i < tracks.size(); ++i) {
        total += tracks[i].durationMs / 1000;
        if (selectedRows.contains(i)) {
            selected += tracks[i].durationMs / 1000;
        }
    }
    painter.save();
    painter.setClipRect(QRect(base + QPoint(7, 10), QSize(80, 6)));
    skin().drawText(painter, base + QPoint(7, 10), FormatTime(selected) + u'/' + FormatTime(total));
    painter.restore();

    const auto st = corePlayer->engine()->state();
    if (st != Audio::AudioEngine::State::Stopped) {
        painter.save();
        painter.setClipRect(QRect(base + QPoint(66, 23), QSize(25, 6)));
        skin().drawText(
            painter, base + QPoint(66, 23),
            FormatTime(qint64(corePlayer->engine()->positionSeconds())).rightJustified(5, u' ')
        );
        painter.restore();
    }
}

QSize PlaylistWindow::fullSkinSize() const {
    return {
        Skins::PlaylistSprites::kMinSize.width()
            + resizeSteps.width() * Skins::PlaylistSprites::kStepWidth,
        Skins::PlaylistSprites::kMinSize.height()
            + resizeSteps.height() * Skins::PlaylistSprites::kStepHeight
    };
}

void PlaylistWindow::setShaded(bool shaded) {
    if (shaded == isShaded()) {
        return;
    }
    const QSize full = fullSkinSize();
    applyShade(shaded, shaded ? QSize(full.width(), 14) : full);
    setScrollOffset(scrollRow);
    update();
}

void PlaylistWindow::paintShaded(QPainter& painter) {
    const Skins::Skin& activeSkin = skin();
    const int window = skinSize().width();
    for (int x = 25; x < window - 50; x += 25) {
        activeSkin.draw(painter, TSheet::PlEdit, Skins::PlaylistSprites::kShadeTile, {x, 0});
    }
    activeSkin.draw(painter, TSheet::PlEdit, Skins::PlaylistSprites::kShadeLeft, {0, 0});
    activeSkin.draw(
        painter, TSheet::PlEdit,
        isActiveWindow() ? Skins::PlaylistSprites::kShadeRightSelected
                         : Skins::PlaylistSprites::kShadeRight,
        {window - 50, 0}
    );

    if (const auto* t = corePlayer->currentTrack()) {
        const QString time = FormatTime(t->durationMs / 1000);
        const int timeW = Skins::Skin::TextWidth(time);
        const int timeX = window - 30 - timeW;
        activeSkin.drawText(painter, {timeX, 4}, time);
        painter.save();
        painter.setClipRect(QRect(5, 4, timeX - 5 - 5, 6));
        activeSkin.drawText(
            painter, {5, 4},
            QStringLiteral("%1. %2").arg(corePlayer->currentIndex() + 1).arg(t->displayTitle()),
            timeX - 10
        );
        painter.restore();
    }
    if (activeDrag == Drag::Close) {
        activeSkin.draw(
            painter, TSheet::PlEdit, Skins::PlaylistSprites::kCloseSelected, {window - 11, 3}
        );
    }
    if (activeDrag == Drag::Shade) {
        activeSkin.draw(
            painter, TSheet::PlEdit, Skins::PlaylistSprites::kCollapseSelected, {window - 21, 3}
        );
    }
    if (activeDrag == Drag::Shade) {
        activeSkin.draw(
            painter, TSheet::PlEdit, Skins::PlaylistSprites::kExpandSelected, {window - 21, 3}
        );
    }
}

void PlaylistWindow::paintSkin(QPainter& painter) {
    if (isShaded()) {
        return paintShaded(painter);
    }
    drawRows(painter);
    drawTiles(painter);
    drawBottomInfo(painter);
}

int PlaylistWindow::miniButtonAt(QPoint point) const {
    const int window = skinSize().width(), h = skinSize().height();
    const QPoint base(window - 150, h - Skins::PlaylistSprites::kBottomHeight);
    for (int i = 0; i < 6; ++i) {
        if (RectContains(
                QRect(base + QPoint(kMiniX[i], kMiniY), QSize(kMiniWidth, kMiniHeight)), point
            )) {
            return kBtnMini0 + i;
        }
    }
    const int y = h - 30;
    const int xs[] = {14, 43, 72, 101};
    for (int i = 0; i < 4; ++i) {
        if (RectContains(QRect(xs[i], y, 22, 18), point)) {
            return i;
        }
    }
    if (RectContains(QRect(window - 44, y, 22, 18), point)) {
        return kBtnList;
    }
    return -1;
}

bool PlaylistWindow::isDragArea(QPoint point) const {
    return point.y() < Skins::PlaylistSprites::kTopHeight;
}

void PlaylistWindow::selectRow(int row, Qt::KeyboardModifiers mods) {
    if (row < 0) {
        return;
    }
    if (mods & Qt::ShiftModifier && anchor >= 0) {
        selectedRows.clear();
        for (int i = std::min(anchor, row); i <= std::max(anchor, row); ++i) {
            selectedRows.insert(i);
        }
    } else if (mods & Qt::ControlModifier) {
        if (!selectedRows.remove(row)) {
            selectedRows.insert(row);
        }
        anchor = row;
    } else {
        selectedRows = {row};
        anchor = row;
    }
    cursorRow = row;
    update();
}

bool PlaylistWindow::skinMousePress(QPoint pos, Qt::MouseButton button) {
    if (button != Qt::LeftButton) {
        return false;
    }
    const int window = skinSize().width(), h = skinSize().height();
    dragStart = pos;

    if (RectContains(QRect(window - 11, 3, 9, 9), pos)) {
        activeDrag = Drag::Close;
        update();
        return true;
    }
    if (RectContains(QRect(window - 21, 3, 9, 9), pos)) {
        activeDrag = Drag::Shade;
        update();
        return true;
    }
    if (isShaded()) {
        return false;
    }
    if (RectContains(QRect(window - 20, h - 20, 20, 20), pos)) {
        activeDrag = Drag::Resize;
        dragStartSteps = resizeSteps;
        return true;
    }
    if (RectContains(
            QRect(
                window - Skins::PlaylistSprites::kRightWidth, Skins::PlaylistSprites::kTopHeight,
                Skins::PlaylistSprites::kRightWidth,
                h - Skins::PlaylistSprites::kTopHeight - Skins::PlaylistSprites::kBottomHeight
            ),
            pos
        )) {
        activeDrag = Drag::Scroll;
        dragStartScroll = scrollRow;
        const QRect handle = scrollHandleRect();
        if (pos.y() < handle.y() || pos.y() >= handle.y() + handle.height()) {
            const QRect list = listRect();
            const double frac = double(pos.y() - list.y() - handle.height() / 2)
                / std::max(1, list.height() - handle.height());
            setScrollOffset(int(std::lround(std::clamp(frac, 0.0, 1.0) * maxScroll())));
            dragStartScroll = scrollRow;
        }
        update();
        return true;
    }
    if (const int b = miniButtonAt(pos); b >= 0) {
        activeDrag = Drag::Button;
        pressedButton = b;
        return true;
    }
    if (const int row = rowAt(pos); row >= 0) {
        selectRow(row, QApplication::keyboardModifiers());
        return true;
    }
    return false;
}

void PlaylistWindow::skinMouseMove(QPoint pos) {
    switch (activeDrag) {
        case Drag::Resize: {
            const QPoint d = pos - dragStart;
            setSizeSteps(QSize(
                dragStartSteps.width()
                    + int(std::lround(double(d.x()) / Skins::PlaylistSprites::kStepWidth)),
                dragStartSteps.height()
                    + int(std::lround(double(d.y()) / Skins::PlaylistSprites::kStepHeight))
            ));
            break;
        }
        case Drag::Scroll: {
            const int travel =
                std::max(1, listRect().height() - Skins::PlaylistSprites::kScrollHandle.height());
            const int dy = pos.y() - dragStart.y();
            setScrollOffset(dragStartScroll + int(std::lround(double(dy) / travel * maxScroll())));
            break;
        }
        default: break;
    }
}

void PlaylistWindow::skinMouseRelease(QPoint pos, Qt::MouseButton button) {
    if (button != Qt::LeftButton) {
        return;
    }
    const Drag drag = activeDrag;
    activeDrag = Drag::None;
    const int window = skinSize().width();
    if (drag == Drag::Close && RectContains(QRect(window - 11, 3, 9, 9), pos)) {
        Q_EMIT closeRequested();
    }
    if (drag == Drag::Shade && RectContains(QRect(window - 21, 3, 9, 9), pos)) {
        setShaded(!isShaded());
    }
    if (drag == Drag::Button && miniButtonAt(pos) == pressedButton) {
        switch (pressedButton) {
            case kBtnAdd:
                Q_EMIT sourcesMenuRequested(mapToGlobal(
                    QPoint(qRound(14 * scale()), qRound((skinSize().height() - 30) * scale()))
                ));
                break;
            case kBtnRem: {
                auto* menu = new QMenu(this);
                menu->addAction(
                        QStringLiteral("Удалить выбранные"), this,
                        [this] {
                            corePlayer->removeTracks(selectedRows.values());
                            selectedRows.clear();
                        }
                )->setEnabled(!selectedRows.isEmpty());
                menu->addAction(QStringLiteral("Очистить плейлист"), this, [this] {
                    corePlayer->clearQueue();
                });
                popupAt(menu, {43, skinSize().height() - 30});
                break;
            }
            case kBtnSel: {
                auto* menu = new QMenu(this);
                menu->addAction(QStringLiteral("Выбрать все"), this, [this] {
                    selectedRows.clear();
                    for (int i = 0; i < corePlayer->playlist().size(); ++i) {
                        selectedRows.insert(i);
                    }
                    update();
                });
                menu->addAction(QStringLiteral("Снять выбор"), this, [this] {
                    selectedRows.clear();
                    update();
                });
                popupAt(menu, {72, skinSize().height() - 30});
                break;
            }
            case kBtnMisc: break;
            case kBtnMini0 + 0: corePlayer->previous(); break;
            case kBtnMini0 + 1: corePlayer->play(); break;
            case kBtnMini0 + 2: corePlayer->pause(); break;
            case kBtnMini0 + 3: corePlayer->stop(); break;
            case kBtnMini0 + 4: corePlayer->next(); break;
            default: break;
        }
    }
    pressedButton = -1;
    update();
}

bool PlaylistWindow::skinMouseDoubleClick(QPoint pos, Qt::MouseButton button) {
    if (button == Qt::LeftButton && (isShaded() || pos.y() < Skins::PlaylistSprites::kTopHeight)
        && pos.x() < skinSize().width() - 21) {
        setShaded(!isShaded());
        return true;
    }
    const int row = rowAt(pos);
    if (button != Qt::LeftButton || row < 0) {
        return false;
    }
    corePlayer->playIndex(row);
    return true;
}

void PlaylistWindow::wheelEvent(QWheelEvent* event) {
    const int steps = wheelSteps(event);
    if (steps != 0) {
        setScrollOffset(scrollRow - steps * 3);
    }
}

void PlaylistWindow::keyPressEvent(QKeyEvent* event) {
    const int count = int(corePlayer->playlist().size());
    if (count == 0) {
        return QWidget::keyPressEvent(event);
    }
    int cur =
        cursorRow >= 0 ? std::min(cursorRow, count - 1) : std::max(0, corePlayer->currentIndex());
    switch (event->key()) {
        case Qt::Key_Up: cur = std::max(0, cur - 1); break;
        case Qt::Key_Down: cur = std::min(count - 1, cur + 1); break;
        case Qt::Key_PageUp: cur = std::max(0, cur - visibleRows()); break;
        case Qt::Key_PageDown: cur = std::min(count - 1, cur + visibleRows()); break;
        case Qt::Key_Home: cur = 0; break;
        case Qt::Key_End: cur = count - 1; break;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            if (cursorRow >= 0) {
                corePlayer->playIndex(cursorRow);
            }
            return;
        case Qt::Key_Delete:
            corePlayer->removeTracks(selectedRows.values());
            selectedRows.clear();
            return;
        default: return QWidget::keyPressEvent(event);
    }
    selectRow(cur, event->modifiers() & Qt::ShiftModifier);
    ensureRowVisible(cur);
}

void PlaylistWindow::popupAt(QMenu* menu, QPoint skinPos) {
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->popup(mapToGlobal(QPoint(qRound(skinPos.x() * scale()), qRound(skinPos.y() * scale()))));
}

void PlaylistWindow::contextMenuEvent(QContextMenuEvent* event) {
    const int row = rowAt(toSkin(event->pos()));
    if (row >= 0 && !selectedRows.contains(row)) {
        selectRow(row, {});
    }
    Q_EMIT sourcesMenuRequested(event->globalPos());
}

void PlaylistWindow::closeEvent(QCloseEvent* event) {
    event->ignore();
    Q_EMIT closeRequested();
}

}  // namespace Ui
