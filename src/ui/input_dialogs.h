#pragma once

#include <QString>
#include <QStringList>
#include <QWidget>

#include <optional>

namespace Ui {

// QInputDialog's text and list questions with the app's own OK and Cancel: Qt has no Belarusian
// translation of its standard buttons. nullopt: cancelled.
std::optional<QString>
AskText(QWidget* parent, const QString& title, const QString& label, const QString& value = {});
std::optional<QString>
AskItem(QWidget* parent, const QString& title, const QString& label, const QStringList& items);

}  // namespace Ui
