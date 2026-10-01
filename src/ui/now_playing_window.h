#pragma once

#include "ui/gen_window.h"

#include <QPoint>
#include <QRect>
#include <QWidget>

class QPainter;

namespace Skins {
class Skin;
}  // namespace Skins

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
    void retranslate() override;
    void paintContent(QPainter& painter, const QRect& area) override;
    bool contentMousePress(QPoint pos, Qt::MouseButton button) override;

private:
    Core::Player* corePlayer;
    Core::CoverCache* coverCache;
};

}  // namespace Ui
