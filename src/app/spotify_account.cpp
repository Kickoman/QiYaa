#include "app/spotify_account.h"

#include "spotify/client/credentials_file.h"
#include "spotify/client/session.h"
#include "spotify/client/web.h"
#include "spotify/protocol/access_point.h"
#include "ui/spotify_login_dialog.h"

#include <QDialog>

#include <optional>
#include <utility>

namespace App {

SpotifyAccount::SpotifyAccount(const Parts& accountParts, QString credentialsFile, QObject* parent)
    : QObject(parent)
    , parts(accountParts)
    , file(std::move(credentialsFile)) {
    connect(parts.session, &SpotifyClient::Session::loggedInChanged, this, &SpotifyAccount::onLoggedInChanged);
    connect(parts.session, &SpotifyClient::Session::loginRefused, this, &SpotifyAccount::onRefused);
}

void SpotifyAccount::start() {
    const std::optional<SpotifyProtocol::Credentials> credentials = SpotifyClient::LoadCredentials(file);
    if (!credentials) {
        return;
    }
    savedData = credentials->data;
    greeted = false;
    parts.session->login(*credentials);
}

void SpotifyAccount::login() {
    Ui::SpotifyLoginDialog dialog(parts.network, parts.endpoints, parts.dialogParent);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    loginWithToken(dialog.token());
}

void SpotifyAccount::loginWithToken(const QString& accessToken) {
    savedData.clear();
    greeted = false;
    Q_EMIT statusText(tr("Connecting to Spotify…"));
    parts.session->login(SpotifyProtocol::Credentials::FromAccessToken(accessToken.toStdString()));
}

void SpotifyAccount::logout() {
    parts.session->logout();
    parts.web->forgetTokens();
    SpotifyClient::ForgetCredentials(file);
    savedData.clear();
    greeted = false;
    Q_EMIT statusText(tr("You have logged out of Spotify"));
}

bool SpotifyAccount::loggedIn() const {
    return parts.session->loggedIn();
}

QString SpotifyAccount::username() const {
    return parts.session->username();
}

void SpotifyAccount::onLoggedInChanged(bool loggedIn) {
    if (!loggedIn) {
        return;
    }
    const SpotifyProtocol::Credentials& credentials = parts.session->credentials();
    if (credentials.data != savedData) {
        SpotifyClient::SaveCredentials(file, credentials);
        savedData = credentials.data;
    }
    if (greeted) {
        return;
    }
    greeted = true;
    Q_EMIT statusText(tr("Logged in to Spotify as %1").arg(username()));
    Q_EMIT connected(username());
}

void SpotifyAccount::onRefused(int code, const QString& description) {
    switch (static_cast<SpotifyProtocol::LoginError>(code)) {
        case SpotifyProtocol::LoginError::BadCredentials:
            SpotifyClient::ForgetCredentials(file);
            savedData.clear();
            Q_EMIT statusText(tr("The Spotify login has expired: log in again"));
            return;
        case SpotifyProtocol::LoginError::PremiumAccountRequired:
            Q_EMIT statusText(tr("Spotify Premium is needed to play"));
            return;
        default: break;
    }
    const QString reason = description.isEmpty() ? QString::number(code) : description;
    Q_EMIT statusText(tr("Spotify refused the login: %1").arg(reason));
}

}  // namespace App
