#include "ui/PlaylistWindow.h"

#include <algorithm>
#include <cmath>

#include <QApplication>
#include <QContextMenuEvent>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QCloseEvent>
#include <QMenu>
#include <QPainter>
#include <QWheelEvent>

#include "core/Player.h"
#include "skin/Skin.h"
#include "skin/SkinSprites.h"

namespace qiyaa {

using audio::AudioEngine;
using Sheet = Skin::Sheet;
namespace P = sprites::pl;

namespace {

constexpr int kListPadTop = 3;

bool contains(const QRect& r, QPoint p) {
    return p.x() >= r.x() && p.y() >= r.y() && p.x() < r.x() + r.width() && p.y() < r.y() + r.height();
}

QString formatTime(qint64 seconds) {
    seconds = std::max<qint64>(0, seconds);
    if (seconds >= 3600)
        return QStringLiteral("%1:%2:%3").arg(seconds / 3600).arg(seconds / 60 % 60, 2, 10, QLatin1Char('0')).arg(seconds % 60, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

// Mini transport buttons in the bottom-right corner: prev, play, pause, stop, next, eject.
constexpr int kMiniX[] = {3, 11, 20, 29, 37, 45};
constexpr int kMiniW = 8;
constexpr int kMiniY = 22;
constexpr int kMiniH = 10;

// Bottom-left menu buttons (22x18): ADD, REM, SEL, MISC; LIST is at the right.
constexpr int kBtnAdd = 0, kBtnRem = 1, kBtnSel = 2, kBtnMisc = 3, kBtnList = 4, kBtnMini0 = 10;

}  // namespace

PlaylistWindow::PlaylistWindow(Player* player, const Skin* skin, QWidget* parent)
    : SkinnedWindow(skin, QSize(P::kMinSize.width(), P::kMinSize.height() + 4 * P::kStepH), parent), m_player(player) {
    setWindowTitle(QStringLiteral("QiYaa Playlist"));
    setFocusPolicy(Qt::StrongFocus);
    connect(m_player, &Player::queueReplaced, this, [this] {
        // A different list: old row numbers mean nothing any more.
        m_selected.clear();
        m_anchor = m_cursor = -1;
        m_scroll = 0;
        update();
    });
    connect(m_player, &Player::playlistChanged, this, [this] {
        const int n = int(m_player->playlist().size());
        QSet<int> valid;
        for (int i : m_selected)
            if (i < n) valid.insert(i);
        m_selected = valid;
        if (m_anchor >= n) m_anchor = n - 1;
        if (m_cursor >= n) m_cursor = n - 1;
        setScrollOffset(m_scroll);
        update();
    });
    connect(m_player, &Player::positionTick, this, [this] {
        // Only the mini time changes; repaint it once a second.
        const int sec = int(m_player->engine()->positionSeconds());
        if (sec == m_shownSecond) return;
        m_shownSecond = sec;
        const QSize s = skinSize();
        updateSkinRect(QRect(s.width() - 150 + 66, s.height() - P::kBottomH + 23, 25, 6));
    });
    connect(m_player, &Player::currentTrackChanged, this, [this] {
        ensureRowVisible(m_player->currentIndex());
        update();
    });
    connect(m_player->engine(), &AudioEngine::stateChanged, this, [this] { update(); });
}

void PlaylistWindow::setSizeSteps(QSize steps) {
    steps = steps.expandedTo(QSize(0, 0)).boundedTo(QSize(40, 40));
    if (steps == m_steps) return;
    m_steps = steps;
    setSkinSize(QSize(P::kMinSize.width() + steps.width() * P::kStepW, P::kMinSize.height() + steps.height() * P::kStepH));
    setScrollOffset(m_scroll);
    Q_EMIT sizeStepsChanged(steps);
}

QRect PlaylistWindow::listRect() const {
    const QSize s = skinSize();
    return {P::kLeftW, P::kTopH, s.width() - P::kLeftW - P::kRightW, s.height() - P::kTopH - P::kBottomH};
}

int PlaylistWindow::visibleRows() const {
    return std::max(1, listRect().height() / P::kRowH);
}

int PlaylistWindow::maxScroll() const {
    return std::max(0, int(m_player->playlist().size()) - visibleRows());
}

void PlaylistWindow::setScrollOffset(int row) {
    row = std::clamp(row, 0, maxScroll());
    if (row == m_scroll) return;
    m_scroll = row;
    update();
}

void PlaylistWindow::ensureRowVisible(int row) {
    if (row < 0) return;
    if (row < m_scroll) setScrollOffset(row);
    else if (row >= m_scroll + visibleRows()) setScrollOffset(row - visibleRows() + 1);
}

int PlaylistWindow::rowAt(QPoint p) const {
    const QRect r = listRect();
    if (!contains(r, p)) return -1;
    const int y = p.y() - r.y() - kListPadTop;
    if (y < 0) return -1;
    const int visible = y / P::kRowH;
    if (visible >= visibleRows()) return -1;
    const int row = m_scroll + visible;
    return row < m_player->playlist().size() ? row : -1;
}

QRect PlaylistWindow::scrollHandleRect() const {
    const QRect list = listRect();
    const int travel = std::max(0, list.height() - P::kScrollHandle.height());
    const int max = maxScroll();
    const int y = list.y() + (max > 0 ? int(std::lround(double(m_scroll) / max * travel)) : 0);
    return {skinSize().width() - 15, y, P::kScrollHandle.width(), P::kScrollHandle.height()};
}

// ------------------------------------------------------------------ painting

void PlaylistWindow::drawTiles(QPainter& p) const {
    const Skin& sk = skin();
    const int w = skinSize().width(), h = skinSize().height();
    const bool active = isActiveWindow();

    // Top: corners, tiles, centred title.
    for (int x = 25; x < w - 25; x += 25) sk.draw(p, Sheet::PlEdit, active ? P::kTopTileSelected : P::kTopTile, {x, 0});
    sk.draw(p, Sheet::PlEdit, active ? P::kTopLeftSelected : P::kTopLeft, {0, 0});
    sk.draw(p, Sheet::PlEdit, active ? P::kTitleSelected : P::kTitle, {(w - 100) / 2, 0});
    sk.draw(p, Sheet::PlEdit, active ? P::kTopRightSelected : P::kTopRight, {w - 25, 0});

    // Sides.
    for (int y = P::kTopH; y < h - P::kBottomH; y += 29) {
        const int tileH = std::min(29, h - P::kBottomH - y);
        sk.draw(p, Sheet::PlEdit, P::kLeftTile.adjusted(0, 0, 0, tileH - 29), {0, y});
        sk.draw(p, Sheet::PlEdit, P::kRightTile.adjusted(0, 0, 0, tileH - 29), {w - P::kRightW, y});
    }

    // Bottom.
    for (int x = 125; x < w - 150; x += 25) sk.draw(p, Sheet::PlEdit, P::kBottomTile, {x, h - P::kBottomH});
    sk.draw(p, Sheet::PlEdit, P::kBottomLeft, {0, h - P::kBottomH});
    sk.draw(p, Sheet::PlEdit, P::kBottomRight, {w - 150, h - P::kBottomH});

    sk.draw(p, Sheet::PlEdit, m_drag == Drag::Scroll ? P::kScrollHandleSelected : P::kScrollHandle, scrollHandleRect().topLeft());
    if (m_drag == Drag::Close) sk.draw(p, Sheet::PlEdit, P::kCloseSelected, {w - 11, 3});
}

void PlaylistWindow::drawRows(QPainter& p) const {
    const Skin::PlaylistStyle& st = skin().playlistStyle();
    const QRect list = listRect();
    p.fillRect(list, st.normalBg);

    QFont font(st.font);
    font.setPixelSize(9);
    font.setLetterSpacing(QFont::AbsoluteSpacing, 0.5);
    p.setFont(font);
    const QFontMetrics fm(font);

    const auto& tracks = m_player->playlist();
    const int current = m_player->currentIndex();
    p.save();
    p.setClipRect(list);
    for (int i = 0; i < visibleRows(); ++i) {
        const int row = m_scroll + i;
        if (row >= tracks.size()) break;
        const QRect r(list.x(), list.y() + kListPadTop + i * P::kRowH, list.width(), P::kRowH);
        if (m_selected.contains(row)) p.fillRect(r, st.selectedBg);
        p.setPen(row == current ? st.current : st.normal);
        const QString duration = formatTime(tracks[row].durationMs / 1000);
        const int durW = fm.horizontalAdvance(duration) + 3;
        const QRect titleRect = r.adjusted(2, 0, -durW - 4, 0);
        const QString title = QStringLiteral("%1. %2").arg(row + 1).arg(tracks[row].displayTitle());
        p.drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter, fm.elidedText(title, Qt::ElideRight, titleRect.width()));
        p.drawText(r.adjusted(0, 0, -3, 0), Qt::AlignRight | Qt::AlignVCenter, duration);
    }
    p.restore();

    if (tracks.isEmpty()) {
        p.setPen(st.normal);
        p.drawText(list.adjusted(4, kListPadTop, -4, 0), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
                   QStringLiteral("Плейлист пуст.\nПравый клик или кнопка ADD — выбрать, что слушать."));
    }
}

void PlaylistWindow::drawBottomInfo(QPainter& p) const {
    const int w = skinSize().width(), h = skinSize().height();
    const QPoint base(w - 150, h - P::kBottomH);

    // Running time: "selected/total" like Winamp.
    qint64 total = 0, selected = 0;
    const auto& tracks = m_player->playlist();
    for (int i = 0; i < tracks.size(); ++i) {
        total += tracks[i].durationMs / 1000;
        if (m_selected.contains(i)) selected += tracks[i].durationMs / 1000;
    }
    p.save();
    p.setClipRect(QRect(base + QPoint(7, 10), QSize(80, 6)));
    skin().drawText(p, base + QPoint(7, 10), formatTime(selected) + u'/' + formatTime(total));
    p.restore();

    // Mini time: elapsed.
    const auto st = m_player->engine()->state();
    if (st != AudioEngine::State::Stopped) {
        p.save();
        p.setClipRect(QRect(base + QPoint(66, 23), QSize(25, 6)));
        skin().drawText(p, base + QPoint(66, 23), formatTime(qint64(m_player->engine()->positionSeconds())).rightJustified(5, u' '));
        p.restore();
    }
}

void PlaylistWindow::paintSkin(QPainter& p) {
    drawRows(p);
    drawTiles(p);
    drawBottomInfo(p);
}

// ------------------------------------------------------------------ input

int PlaylistWindow::miniButtonAt(QPoint p) const {
    const int w = skinSize().width(), h = skinSize().height();
    const QPoint base(w - 150, h - P::kBottomH);
    for (int i = 0; i < 6; ++i)
        if (contains(QRect(base + QPoint(kMiniX[i], kMiniY), QSize(kMiniW, kMiniH)), p)) return kBtnMini0 + i;
    const int y = h - 30;
    const int xs[] = {14, 43, 72, 101};
    for (int i = 0; i < 4; ++i)
        if (contains(QRect(xs[i], y, 22, 18), p)) return i;
    if (contains(QRect(w - 44, y, 22, 18), p)) return kBtnList;
    return -1;
}

bool PlaylistWindow::isDragArea(QPoint p) const {
    return p.y() < P::kTopH;
}

void PlaylistWindow::selectRow(int row, Qt::KeyboardModifiers mods) {
    if (row < 0) return;
    if (mods & Qt::ShiftModifier && m_anchor >= 0) {
        m_selected.clear();
        for (int i = std::min(m_anchor, row); i <= std::max(m_anchor, row); ++i) m_selected.insert(i);
    } else if (mods & Qt::ControlModifier) {
        if (!m_selected.remove(row)) m_selected.insert(row);
        m_anchor = row;
    } else {
        m_selected = {row};
        m_anchor = row;
    }
    m_cursor = row;
    update();
}

bool PlaylistWindow::skinMousePress(QPoint pos, Qt::MouseButton button) {
    if (button != Qt::LeftButton) return false;
    const int w = skinSize().width(), h = skinSize().height();
    m_dragStart = pos;

    if (contains(QRect(w - 11, 3, 9, 9), pos)) {
        m_drag = Drag::Close;
        update();
        return true;
    }
    if (contains(QRect(w - 20, h - 20, 20, 20), pos)) {
        m_drag = Drag::Resize;
        m_dragStartSteps = m_steps;
        return true;
    }
    if (contains(QRect(w - P::kRightW, P::kTopH, P::kRightW, h - P::kTopH - P::kBottomH), pos)) {
        m_drag = Drag::Scroll;
        m_dragStartScroll = m_scroll;
        const QRect handle = scrollHandleRect();
        if (pos.y() < handle.y() || pos.y() >= handle.y() + handle.height()) {
            // Click on the track: jump there.
            const QRect list = listRect();
            const double frac = double(pos.y() - list.y() - handle.height() / 2) / std::max(1, list.height() - handle.height());
            setScrollOffset(int(std::lround(std::clamp(frac, 0.0, 1.0) * maxScroll())));
            m_dragStartScroll = m_scroll;
        }
        update();
        return true;
    }
    if (const int b = miniButtonAt(pos); b >= 0) {
        m_drag = Drag::Button;
        m_pressedButton = b;
        return true;
    }
    if (const int row = rowAt(pos); row >= 0) {
        selectRow(row, QApplication::keyboardModifiers());
        return true;
    }
    return false;
}

void PlaylistWindow::skinMouseMove(QPoint pos) {
    switch (m_drag) {
    case Drag::Resize: {
        const QPoint d = pos - m_dragStart;
        setSizeSteps(QSize(m_dragStartSteps.width() + int(std::lround(double(d.x()) / P::kStepW)),
                           m_dragStartSteps.height() + int(std::lround(double(d.y()) / P::kStepH))));
        break;
    }
    case Drag::Scroll: {
        const int travel = std::max(1, listRect().height() - P::kScrollHandle.height());
        const int dy = pos.y() - m_dragStart.y();
        setScrollOffset(m_dragStartScroll + int(std::lround(double(dy) / travel * maxScroll())));
        break;
    }
    default: break;
    }
}

void PlaylistWindow::skinMouseRelease(QPoint pos, Qt::MouseButton button) {
    if (button != Qt::LeftButton) return;
    const Drag drag = m_drag;
    m_drag = Drag::None;
    const int w = skinSize().width();
    if (drag == Drag::Close && contains(QRect(w - 11, 3, 9, 9), pos)) Q_EMIT closeRequested();
    if (drag == Drag::Button && miniButtonAt(pos) == m_pressedButton) {
        switch (m_pressedButton) {
        case kBtnAdd: Q_EMIT sourcesMenuRequested(mapToGlobal(QPoint(qRound(14 * scale()), qRound((skinSize().height() - 30) * scale())))); break;
        case kBtnRem: {
            auto* menu = new QMenu(this);
            menu->addAction(QStringLiteral("Удалить выбранные"), this, [this] {
                m_player->removeTracks(m_selected.values());
                m_selected.clear();
            })->setEnabled(!m_selected.isEmpty());
            menu->addAction(QStringLiteral("Очистить плейлист"), this, [this] { m_player->clearQueue(); });
            popupAt(menu, {43, skinSize().height() - 30});
            break;
        }
        case kBtnSel: {
            auto* menu = new QMenu(this);
            menu->addAction(QStringLiteral("Выбрать все"), this, [this] {
                m_selected.clear();
                for (int i = 0; i < m_player->playlist().size(); ++i) m_selected.insert(i);
                update();
            });
            menu->addAction(QStringLiteral("Снять выбор"), this, [this] {
                m_selected.clear();
                update();
            });
            popupAt(menu, {72, skinSize().height() - 30});
            break;
        }
        case kBtnMini0 + 0: m_player->previous(); break;
        case kBtnMini0 + 1: m_player->play(); break;
        case kBtnMini0 + 2: m_player->pause(); break;
        case kBtnMini0 + 3: m_player->stop(); break;
        case kBtnMini0 + 4: m_player->next(); break;
        default: break;
        }
    }
    m_pressedButton = -1;
    update();
}

bool PlaylistWindow::skinMouseDoubleClick(QPoint pos, Qt::MouseButton button) {
    const int row = rowAt(pos);
    if (button != Qt::LeftButton || row < 0) return false;
    m_player->playIndex(row);
    return true;
}

void PlaylistWindow::wheelEvent(QWheelEvent* e) {
    const int steps = wheelSteps(e);
    if (steps != 0) setScrollOffset(m_scroll - steps * 3);
}

void PlaylistWindow::keyPressEvent(QKeyEvent* e) {
    const int count = int(m_player->playlist().size());
    if (count == 0) return QWidget::keyPressEvent(e);
    int cur = m_cursor >= 0 ? std::min(m_cursor, count - 1) : std::max(0, m_player->currentIndex());
    switch (e->key()) {
    case Qt::Key_Up: cur = std::max(0, cur - 1); break;
    case Qt::Key_Down: cur = std::min(count - 1, cur + 1); break;
    case Qt::Key_PageUp: cur = std::max(0, cur - visibleRows()); break;
    case Qt::Key_PageDown: cur = std::min(count - 1, cur + visibleRows()); break;
    case Qt::Key_Home: cur = 0; break;
    case Qt::Key_End: cur = count - 1; break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (m_cursor >= 0) m_player->playIndex(m_cursor);
        return;
    case Qt::Key_Delete:
        m_player->removeTracks(m_selected.values());
        m_selected.clear();
        return;
    default: return QWidget::keyPressEvent(e);
    }
    // Shift extends from the fixed anchor; plain arrows move both.
    selectRow(cur, e->modifiers() & Qt::ShiftModifier);
    ensureRowVisible(cur);
}

void PlaylistWindow::popupAt(QMenu* menu, QPoint skinPos) {
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->popup(mapToGlobal(QPoint(qRound(skinPos.x() * scale()), qRound(skinPos.y() * scale()))));
}

void PlaylistWindow::contextMenuEvent(QContextMenuEvent* e) {
    const int row = rowAt(toSkin(e->pos()));
    if (row >= 0 && !m_selected.contains(row)) selectRow(row, {});
    Q_EMIT sourcesMenuRequested(e->globalPos());
}

void PlaylistWindow::closeEvent(QCloseEvent* e) {
    // Window manager close: just hide (the owner keeps the EQ/PL buttons in sync).
    e->ignore();
    Q_EMIT closeRequested();
}

}  // namespace qiyaa
