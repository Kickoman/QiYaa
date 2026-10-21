#pragma once

#include "spotify/client/endpoints.h"

#include <QObject>
#include <QString>

#include <cstdint>
#include <optional>
#include <vector>

class QNetworkAccessManager;
class QWidget;

namespace SpotifyClient {
class Session;
class Web;
}  // namespace SpotifyClient

namespace App {

class SpotifyAccount : public QObject {
    Q_OBJECT
public:
    struct Parts {
        SpotifyClient::Session* session = nullptr;
        SpotifyClient::Web* web = nullptr;
        QNetworkAccessManager* network = nullptr;
        QWidget* dialogParent = nullptr;
        SpotifyClient::Endpoints endpoints;
    };

    enum class Way { Code, Browser };
    enum class Refusal { Expired, PremiumRequired, Other };

    SpotifyAccount(const Parts& parts, QString credentialsFile, QObject* parent = nullptr);

    void start();
    void login();
    void loginWithToken(const QString& accessToken, Way way = Way::Code);
    void logout();

    bool loggedIn() const;
    bool hasSavedLogin() const;
    QString username() const;

Q_SIGNALS:
    void statusText(const QString& text);
    void connected(const QString& username);
    void loggedInWith(App::SpotifyAccount::Way way);
    void loggedOut();
    void refused(App::SpotifyAccount::Refusal refusal);

private:
    void onLoggedInChanged(bool loggedIn);
    void onRefused(int code, const QString& description);

    Parts parts;
    QString file;
    std::vector<std::uint8_t> savedData;
    bool greeted = false;
    std::optional<Way> pendingWay;
};

}  // namespace App
