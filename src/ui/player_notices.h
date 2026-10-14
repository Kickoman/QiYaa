#pragma once

#include <QString>

namespace Core {
struct PlayerNotice;
}  // namespace Core

namespace Ui {

QString NoticeText(const Core::PlayerNotice& notice);

}  // namespace Ui
