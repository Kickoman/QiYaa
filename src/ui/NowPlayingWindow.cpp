#include "ui/NowPlayingWindow.h"

#include <algorithm>

#include <QDesktopServices>
#include <QFontMetrics>
#include <QPainter>

#include "core/CoverCache.h"
#include "core/Player.h"
#include "skin/Skin.h"

namespace qiyaa {

namespace {
constexpr int kPad = 4;
constexpr int kCoverPx = 400;  // requested size; drawn scaled
}

NowPlayingWindow::NowPlayingWindow(Player* player, CoverCache* covers, const Skin* skin, QWidget* parent)
    : GenWindow(skin, QStringLiteral("NOW PLAYING"), parent), m_player(player), m_covers(covers) {
    setWindowTitle(QStringLiteral("QiYaa: сейчас играет"));
    connect(m_player, &Player::currentTrackChanged, this, [this] { update(); });
    connect(m_player->library(), &yandex::Library::likesChanged, this, [this] { update(); });
    connect(m_covers, &CoverCache::ready, this, [this](const QUrl& url) {
        if (const auto* t = m_player->currentTrack(); t && t->coverUrl(kCoverPx) == url) update();
    });
}

QRect NowPlayingWindow::coverRect() const {
    const QRect a = contentRect().adjusted(kPad, kPad, -kPad, -kPad);
    const int side = std::max(0, std::min(a.height(), a.width() / 2));
    return {a.x(), a.y(), side, side};
}

void NowPlayingWindow::paintContent(QPainter& p, const QRect& area) {
    const Skin::PlaylistStyle& st = skin().playlistStyle();
    p.fillRect(area, st.normalBg);
    const yandex::Track* t = m_player->currentTrack();

    const QRect cover = coverRect();
    const QImage img = t ? m_covers->get(t->coverUrl(kCoverPx)) : QImage();
    if (!img.isNull()) {
        p.save();
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);  // a photo, not pixel art
        p.drawImage(cover, img);
        p.restore();
    } else {
        p.setPen(st.normal);
        p.drawRect(cover.adjusted(0, 0, -1, -1));
    }

    const QRect text(cover.right() + 1 + kPad * 2, cover.y(), area.right() - cover.right() - kPad * 3, cover.height());
    QFont font(st.font);
    font.setPixelSize(9);
    QFont bold = font;
    bold.setBold(true);
    bold.setPixelSize(11);
    int y = text.y();
    auto line = [&](const QFont& f, const QColor& c, const QString& s) {
        if (s.isEmpty()) return;
        const QFontMetrics fm(f);
        if (y + fm.height() > text.bottom() + 1) return;
        p.setFont(f);
        p.setPen(c);
        p.drawText(QRect(text.x(), y, text.width(), fm.height()), Qt::AlignLeft | Qt::AlignVCenter,
                   fm.elidedText(s, Qt::ElideRight, text.width()));
        y += fm.height() + 1;
    };
    if (!t) {
        line(font, st.normal, QStringLiteral("Ничего не играет"));
        return;
    }
    line(bold, st.current, t->title);
    line(font, st.normal, t->artists.join(QStringLiteral(", ")));
    y += 3;
    QString album = t->albumTitle;
    if (t->year > 0) album += album.isEmpty() ? QString::number(t->year) : QStringLiteral(" (%1)").arg(t->year);
    line(font, st.normal, album);
    const int secs = int(t->durationMs / 1000);
    line(font, st.normal, QStringLiteral("%1:%2").arg(secs / 60).arg(secs % 60, 2, 10, QLatin1Char('0')));
    if (m_player->library()->isLiked(t->id)) line(font, st.current, QStringLiteral("♥ В «Мне нравится»"));
}

bool NowPlayingWindow::contentMousePress(QPoint pos, Qt::MouseButton button) {
    const yandex::Track* t = m_player->currentTrack();
    if (button != Qt::LeftButton || !t || !coverRect().contains(pos)) return false;
    QDesktopServices::openUrl(t->webUrl());
    return true;
}

}  // namespace qiyaa
