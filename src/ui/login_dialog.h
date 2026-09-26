#pragma once

#include <QDialog>
#include <QString>
#include <QUrl>
#include <QWidget>

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
    explicit LoginDialog(
        QNetworkAccessManager* networkManager,
        QWidget* parent = nullptr,
        const QString& oauthBase = {}
    );

    QString token() const { return accessToken; }

private:
    void finishWith(const QString& token);
    void fitToContents();
    void tryPasted();

    Yandex::DeviceLogin* deviceLogin;
    QLabel* codeLabel;
    QLabel* deviceStatus;
    QPushButton* openDevice;
    QLineEdit* pasteField;
    QLabel* pasteError;
    QUrl verifyUrl;
    QString accessToken;
};

}  // namespace Ui
