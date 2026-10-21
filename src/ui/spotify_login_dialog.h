#pragma once

#include <QDialog>
#include <QString>
#include <QUrl>
#include <QWidget>

class QLabel;
class QNetworkAccessManager;
class QPushButton;

namespace SpotifyClient {
class BrowserLogin;
struct Endpoints;
class PairLogin;
}  // namespace SpotifyClient

namespace Ui {

class SpotifyLoginDialog : public QDialog {
    Q_OBJECT
public:
    SpotifyLoginDialog(
        QNetworkAccessManager* networkManager,
        const SpotifyClient::Endpoints& endpoints,
        QWidget* parent = nullptr
    );

    QString token() const { return accessToken; }

private:
    void finishWith(const QString& token);
    void fitToContents();

    SpotifyClient::PairLogin* pairLogin;
    SpotifyClient::BrowserLogin* browserLogin;
    QLabel* codeLabel;
    QLabel* pairStatus;
    QPushButton* openPair;
    QLabel* browserStatus;
    QUrl pairUrl;
    QString accessToken;
};

}  // namespace Ui
