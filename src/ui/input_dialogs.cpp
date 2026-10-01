#include "ui/input_dialogs.h"

#include <QCoreApplication>
#include <QDialog>
#include <QInputDialog>

namespace Ui {

namespace {

std::optional<QString> Ask(QInputDialog& dialog, const QString& title, const QString& label) {
    dialog.setWindowTitle(title);
    dialog.setLabelText(label);
    dialog.setOkButtonText(QCoreApplication::translate("Ui::InputDialogs", "OK"));
    dialog.setCancelButtonText(QCoreApplication::translate("Ui::InputDialogs", "Cancel"));
    if (dialog.exec() != QDialog::Accepted) {
        return std::nullopt;
    }
    return dialog.textValue();
}

}  // namespace

std::optional<QString>
AskText(QWidget* parent, const QString& title, const QString& label, const QString& value) {
    QInputDialog dialog(parent);
    dialog.setInputMode(QInputDialog::TextInput);
    dialog.setTextValue(value);
    return Ask(dialog, title, label);
}

std::optional<QString>
AskItem(QWidget* parent, const QString& title, const QString& label, const QStringList& items) {
    QInputDialog dialog(parent);
    dialog.setComboBoxItems(items);
    dialog.setComboBoxEditable(false);
    return Ask(dialog, title, label);
}

}  // namespace Ui
