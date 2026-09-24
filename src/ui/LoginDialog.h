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
    explicit LoginDialog(QNetworkAccessManager* nam, QWidget* parent = nullptr);

    QString token() const { return m_token; }

private:
    void finishWith(const QString& token);
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
