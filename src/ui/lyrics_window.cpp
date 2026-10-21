#include "ui/lyrics_window.h"

#include "core/lyrics_finder.h"
#include "core/player.h"
#include "skins/skin.h"
#include "skins/sprites.h"
#include "sources/yandex/tracks.h"

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QStringList>
#include <QWheelEvent>

#include <algorithm>

namespace Ui {

namespace {

constexpr int kPadding = 4;
constexpr int kRowHeight = Skins::PlaylistSprites::kRowHeight;
constexpr int kWheelRows = 3;
constexpr int kFollowPauseMs = 5000;

QFont TextFont(const Skins::Skin::PlaylistStyle& style) {
    QFont font(style.font);
    font.setPixelSize(9);
    font.setLetterSpacing(QFont::AbsoluteSpacing, 0.5);
    return font;
}

QColor Dimmed(const QColor& color, const QColor& background) {
    return QColor(
        (color.red() + background.red()) / 2,
        (color.green() + background.green()) / 2,
        (color.blue() + background.blue()) / 2
    );
}

qsizetype FittingPrefix(const QString& word, const QFontMetrics& metrics, int width) {
    qsizetype low = 1;
    qsizetype high = word.size();
    while (low < high) {
        const qsizetype middle = (low + high + 1) / 2;
        if (metrics.horizontalAdvance(word.left(middle)) <= width) {
            low = middle;
        } else {
            high = middle - 1;
        }
    }
    const bool splitsPair = low > 1 && low < word.size() && word[low - 1].isHighSurrogate();
    return splitsPair ? low - 1 : low;
}

QStringList Wrapped(const QString& text, const QFontMetrics& metrics, int width) {
    QStringList rows;
    QString row;
    for (QString word : text.split(u' ', Qt::SkipEmptyParts)) {
        const QString longer = row.isEmpty() ? word : row + u' ' + word;
        if (metrics.horizontalAdvance(longer) <= width) {
            row = longer;
            continue;
        }
        if (!row.isEmpty()) {
            rows << row;
        }
        while (word.size() > 1 && metrics.horizontalAdvance(word) > width) {
            const qsizetype cut = FittingPrefix(word, metrics, width);
            rows << word.left(cut);
            word = word.mid(cut);
        }
        row = word;
    }
    if (!row.isEmpty() || rows.isEmpty()) {
        rows << row;
    }
    return rows;
}

}  // namespace

LyricsWindow::LyricsWindow(Core::LyricsFinder* finder, Core::Player* player, const Skins::Skin* skin, QWidget* parent)
    : GenWindow(skin, QStringLiteral("LYRICS"), parent)
    , lyricsFinder(finder)
    , corePlayer(player) {
    retranslate();
    followPause.setSingleShot(true);
    followPause.setInterval(kFollowPauseMs);
    connect(&followPause, &QTimer::timeout, this, [this] {
        following = true;
        update();
    });
    connect(lyricsFinder, &Core::LyricsFinder::changed, this, &LyricsWindow::lyricsChanged);
    connect(corePlayer, &Core::Player::positionTick, this, &LyricsWindow::positionMoved);
    connect(corePlayer, &Core::Player::seeked, this, &LyricsWindow::positionMoved);
    connect(corePlayer, &Core::Player::stateChanged, this, &LyricsWindow::positionMoved);
}

int LyricsWindow::currentLine() const {
    if (!isSynced()) {
        return -1;
    }
    const QList<Core::LyricsLine>& lines = lyricsFinder->view().lyrics.lines;
    const qint64 positionMs = static_cast<qint64>(corePlayer->positionSeconds() * 1000);
    const auto after = std::upper_bound(lines.cbegin(), lines.cend(), positionMs, [](qint64 ms, const auto& line) {
        return ms < line.ms;
    });
    return static_cast<int>(after - lines.cbegin()) - 1;
}

int LyricsWindow::lineAt(QPoint skinPos) const {
    const QRect area = textArea();
    if (lyricsFinder->view().phase != Core::LyricsFinder::Phase::Shown || !area.contains(skinPos)) {
        return -1;
    }
    const int shown = (skinPos.y() - area.y()) / kRowHeight;
    if (shown >= visibleRows()) {
        return -1;
    }
    const int row = firstRow() + shown;
    return row < rows().size() ? rows()[row].line : -1;
}

void LyricsWindow::retranslate() {
    setWindowTitle(tr("QiYaa: lyrics"));
    update();
}

void LyricsWindow::paintContent(QPainter& painter, const QRect& area) {
    const Skins::Skin::PlaylistStyle& style = skin().playlistStyle();
    painter.fillRect(area, style.normalBackground);
    painter.setFont(TextFont(style));
    if (lyricsFinder->view().phase != Core::LyricsFinder::Phase::Shown) {
        return paintStatus(painter, area);
    }
    const QRect text = textArea();
    const QList<Row>& all = rows();
    const int first = firstRow();
    const int current = currentLine();
    for (int i = 0; i < visibleRows() && first + i < all.size(); ++i) {
        const Row& row = all[first + i];
        painter.setPen(row.line == current ? style.current : style.normal);
        painter.drawText(
            QRect(text.x(), text.y() + i * kRowHeight, text.width(), kRowHeight), Qt::AlignCenter, row.text
        );
    }
    const QRect footerRect(text.x(), text.bottom() + 1, text.width(), kRowHeight);
    painter.setPen(Dimmed(style.normal, style.normalBackground));
    const QFontMetrics metrics(painter.font());
    painter.drawText(footerRect, Qt::AlignCenter, metrics.elidedText(footer(), Qt::ElideRight, footerRect.width()));
}

bool LyricsWindow::contentMousePress(QPoint pos, Qt::MouseButton button) {
    const Core::Player::State state = corePlayer->state();
    if (button != Qt::LeftButton || !isSynced()
        || (state != Core::Player::State::Playing && state != Core::Player::State::Paused)) {
        return false;
    }
    const int line = lineAt(pos);
    if (line < 0) {
        return false;
    }
    corePlayer->seekTo(static_cast<double>(lyricsFinder->view().lyrics.lines[line].ms) / 1000.0);
    following = true;
    followPause.stop();
    update();
    return true;
}

void LyricsWindow::skinChanged() {
    wrappedWidth = -1;
    update();
}

void LyricsWindow::wheelEvent(QWheelEvent* event) {
    const int steps = wheelSteps(event);
    if (steps == 0 || lyricsFinder->view().phase != Core::LyricsFinder::Phase::Shown) {
        return;
    }
    scrollRow = std::clamp(firstRow() - steps * kWheelRows, 0, lastScroll());
    if (isSynced()) {
        following = false;
        followPause.start();
    }
    update();
}

void LyricsWindow::showEvent(QShowEvent* event) {
    GenWindow::showEvent(event);
    lyricsFinder->setActive(true);
}

void LyricsWindow::hideEvent(QHideEvent* event) {
    GenWindow::hideEvent(event);
    lyricsFinder->setActive(false);
}

void LyricsWindow::lyricsChanged() {
    const Core::LyricsFinder::View& view = lyricsFinder->view();
    std::optional<Core::TrackKey> track;
    if (view.track) {
        track = view.track->key;
    }
    if (view.lyrics != laidOut || track != laidOutTrack) {
        laidOut = view.lyrics;
        laidOutTrack = track;
        wrappedWidth = -1;
        scrollRow = 0;
        following = true;
        followPause.stop();
    }
    shownLine = currentLine();
    update();
}

void LyricsWindow::positionMoved() {
    const int line = currentLine();
    if (line != shownLine) {
        shownLine = line;
        update();
    }
}

QRect LyricsWindow::textArea() const {
    return contentRect().adjusted(kPadding, kPadding, -kPadding, -kPadding - kRowHeight);
}

int LyricsWindow::visibleRows() const {
    return std::max(1, textArea().height() / kRowHeight);
}

int LyricsWindow::firstRow() const {
    if (!isSynced() || !following) {
        return std::clamp(scrollRow, 0, lastScroll());
    }
    const int current = currentLine();
    if (current < 0) {
        return 0;
    }
    const QList<Row>& all = rows();
    const auto first = std::find_if(all.cbegin(), all.cend(), [current](const Row& row) {
        return row.line == current;
    });
    const int row = static_cast<int>(first - all.cbegin());
    return std::clamp(row - visibleRows() / 2, 0, lastScroll());
}

int LyricsWindow::lastScroll() const {
    return std::max(0, static_cast<int>(rows().size()) - visibleRows());
}

bool LyricsWindow::isSynced() const {
    const Core::LyricsFinder::View& view = lyricsFinder->view();
    return view.phase == Core::LyricsFinder::Phase::Shown && view.lyrics.kind == Core::LyricsKind::Synced;
}

const QList<LyricsWindow::Row>& LyricsWindow::rows() const {
    const int width = textArea().width();
    if (width == wrappedWidth) {
        return wrapped;
    }
    wrappedWidth = width;
    wrapped.clear();
    const QFontMetrics metrics(TextFont(skin().playlistStyle()));
    const Core::LyricsFinder::View& view = lyricsFinder->view();
    const bool synced = view.lyrics.kind == Core::LyricsKind::Synced;
    for (int i = 0; i < view.lyrics.lines.size(); ++i) {
        const QString& text = view.lyrics.lines[i].text;
        const QString shown = synced && text.isEmpty() ? QStringLiteral("♪") : text;
        for (const QString& piece : Wrapped(shown, metrics, width)) {
            wrapped << Row{i, piece};
        }
    }
    return wrapped;
}

QString LyricsWindow::footer() const {
    const Core::LyricsFinder::View& view = lyricsFinder->view();
    QString origin;
    switch (view.from) {
        case Core::LyricsFrom::Catalog:
            origin = view.track && view.track->key.source == YandexSource::kSourceName ? tr("Yandex Music") : QString();
            break;
        case Core::LyricsFrom::LrcFile: origin = tr(".lrc file"); break;
        case Core::LyricsFrom::FileTags: origin = tr("the file's tags"); break;
        case Core::LyricsFrom::ThirdParty: origin = QStringLiteral("LRCLIB"); break;
    }
    QStringList parts;
    if (!origin.isEmpty()) {
        parts << tr("Lyrics: %1").arg(origin);
    }
    if (view.lyrics.kind == Core::LyricsKind::Plain) {
        parts << tr("not synced");
    }
    if (view.better && view.lyrics.kind == Core::LyricsKind::Plain) {
        parts << tr("looking for synced lyrics…");
    }
    return parts.join(QStringLiteral(" · "));
}

void LyricsWindow::paintStatus(QPainter& painter, const QRect& area) const {
    QStringList lines;
    switch (lyricsFinder->view().phase) {
        case Core::LyricsFinder::Phase::Idle:
        case Core::LyricsFinder::Phase::Shown: lines << tr("Nothing is playing"); break;
        case Core::LyricsFinder::Phase::Loading: lines << tr("Loading…"); break;
        case Core::LyricsFinder::Phase::Missing:
            lines << tr("No lyrics");
            if (lyricsFinder->hasThirdParty() && !lyricsFinder->thirdPartyEnabled()) {
                lines << tr("Search in LRCLIB is off (menu)");
            }
            break;
        case Core::LyricsFinder::Phase::Instrumental: lines << tr("Instrumental"); break;
        case Core::LyricsFinder::Phase::Failed: lines << tr("Could not load the lyrics"); break;
        case Core::LyricsFinder::Phase::NoAccount: lines << tr("Log in to Yandex Music for its lyrics"); break;
    }
    const Skins::Skin::PlaylistStyle& style = skin().playlistStyle();
    const QFontMetrics metrics(painter.font());
    const QRect inner = area.adjusted(kPadding, 0, -kPadding, 0);
    int y = area.y() + (area.height() - static_cast<int>(lines.size()) * kRowHeight) / 2;
    for (int i = 0; i < lines.size(); ++i) {
        painter.setPen(i == 0 ? style.normal : Dimmed(style.normal, style.normalBackground));
        painter.drawText(
            QRect(inner.x(), y, inner.width(), kRowHeight),
            Qt::AlignCenter,
            metrics.elidedText(lines[i], Qt::ElideRight, inner.width())
        );
        y += kRowHeight;
    }
}

}  // namespace Ui
