#pragma once

#include <QDialog>
#include <QString>
#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace Ui {

// "Настройки сервера…" of the jam menu: the jam server's address and whether the jam wave
// learns from skips (HOST-16).
class JamServerDialog : public QDialog {
    Q_OBJECT
public:
    JamServerDialog(
        const QString& server,
        bool waveFeedback,
        const QString& defaultServer,
        QWidget* parent = nullptr
    );

    // The address as typed, or the default for an empty field.
    QString server() const;
    bool waveFeedback() const;

    // http(s) with a host: what the app can turn into the server's socket address.
    static bool IsServerAddress(const QString& text);

private:
    void check();

    QString fallback;
    QLineEdit* serverField;
    QLabel* serverProblem;
    QCheckBox* feedbackBox;
    QPushButton* saveButton;
};

}  // namespace Ui
