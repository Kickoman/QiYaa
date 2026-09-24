// "Log in to Yandex Music": device code (preferred) or paste a token/link.
#pragma once

#include <QDialog>
#include <QUrl>

class QLabel;
class QLineEdit;
class QNetworkAccessManager;
class QPushButton;

namespace qiyaa {

namespace yandex {
class DeviceLogin;
}

class LoginDialog : public QDialog {
    Q_OBJECT
public:
    // `oauthBase` overrides https://oauth.yandex.ru (tests).
    explicit LoginDialog(QNetworkAccessManager* nam, QWidget* parent = nullptr, const QString& oauthBase = {});

    QString token() const { return m_token; }

private:
    void finishWith(const QString& token);
    // Word-wrapped labels need their height computed for the actual width;
    // Qt's default size hint doesn't, which squeezes them. Call after text changes.
    void fitToContents();
    void tryPasted();

    yandex::DeviceLogin* m_device;
    QLabel* m_code;
    QLabel* m_deviceStatus;
    QPushButton* m_openDevice;
    QLineEdit* m_paste;
    QLabel* m_pasteError;
    QUrl m_verifyUrl;
    QString m_token;
};

}  // namespace qiyaa
