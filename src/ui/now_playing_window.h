#pragma once

#include "ui/gen_window.h"

namespace Core {
class CoverCache;
class Player;
}  // namespace Core

namespace Ui {

class NowPlayingWindow : public GenWindow {
    Q_OBJECT
public:
    NowPlayingWindow(
        Core::Player* player,
        Core::CoverCache* covers,
        const Skins::Skin* skin,
        QWidget* parent = nullptr
    );

    QRect coverRect() const;

protected:
    void paintContent(QPainter& painter, const QRect& area) override;
    bool contentMousePress(QPoint pos, Qt::MouseButton button) override;

private:
    Core::Player* corePlayer;
    Core::CoverCache* coverCache;
};

}  // namespace Ui
