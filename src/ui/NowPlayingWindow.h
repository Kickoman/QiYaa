// "Now playing": cover, title, artists, album and year of the current track,
// in a GEN.BMP frame. Click the cover to open the track on music.yandex.ru.
#pragma once

#include "ui/GenWindow.h"

namespace qiyaa {

class CoverCache;
class Player;

class NowPlayingWindow : public GenWindow {
    Q_OBJECT
public:
    NowPlayingWindow(Player* player, CoverCache* covers, const Skin* skin, QWidget* parent = nullptr);

    // Where the cover is drawn (skin coordinates), for tests.
    QRect coverRect() const;

protected:
    void paintContent(QPainter& p, const QRect& area) override;
    bool contentMousePress(QPoint pos, Qt::MouseButton button) override;

private:
    Player* m_player;
    CoverCache* m_covers;
};

}  // namespace qiyaa
