#pragma once

#include <QString>

namespace Core {
struct SourceNotice;
}  // namespace Core

namespace Ui {

QString SourceNoticeText(const Core::SourceNotice& notice);

}  // namespace Ui
