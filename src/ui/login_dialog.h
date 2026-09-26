// "Log in to Yandex Music": device code (preferred) or paste a token/link.
#pragma once

#include <QDialog>
#include <QUrl>

class QLabel;
class QLineEdit;
class QNetworkAccessManager;
class QPushButton;

namespace Yandex {
class DeviceLogin;
}  // namespace Yandex

namespace Ui {

class LoginDialog : public QDialog {
    Q_OBJECT
public:
    // `oauthBase` overrides https://oauth.yandex.ru (tests).
    explicit LoginDialog(
        QNetworkAccessManager* nam,
        QWidget* parent = nullptr,
        const QString& oauthBase = {}
    );

    QString token() const { return accessToken; }

private:
    void finishWith(const QString& token);
    // Word-wrapped labels need their height computed for the actual width;
    // Qt's default size hint doesn't, which squeezes them. Call after text changes.
    void fitToContents();
    void tryPasted();

    Yandex::DeviceLogin* device;
    QLabel* codeLabel;
    QLabel* deviceStatus;
    QPushButton* openDevice;
    QLineEdit* pasteField;
    QLabel* pasteError;
    QUrl verifyUrl;
    QString accessToken;
};

}  // namespace Ui
