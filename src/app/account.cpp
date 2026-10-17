#include "app/account.h"

#include "core/player.h"
#include "sources/yandex/catalog.h"
#include "ui/login_dialog.h"
#include "yandex/api_client.h"
#include "yandex/library.h"
#include "yandex/token.h"

#include <QDialog>
#include <QLatin1String>
#include <QTimer>

#include <utility>

namespace App {

Account::Account(const Parts& accountParts, TokenFiles tokenFiles, QObject* parent)
    : QObject(parent)
    , parts(accountParts)
    , files(std::move(tokenFiles))
    , likesOnHold([] { return false; }) { }

void Account::setLikesOnHold(std::function<bool()> onHold) {
    likesOnHold = std::move(onHold);
}

void Account::start() {
    const Yandex::TokenSource token = Yandex::FindToken(files.own, files.imported);
    if (token.token.isEmpty()) {
        Q_EMIT statusText(tr("Log in: right click → Log in"));
        QTimer::singleShot(0, this, &Account::login);
        return;
    }
    qInfo("Using Yandex token from %s", qPrintable(token.origin));
    const bool imported =
        !token.origin.startsWith(files.ownDirectory) && !token.origin.startsWith(QLatin1String("environment"));
    applyToken(token.token, imported);
}

void Account::login() {
    Ui::LoginDialog dialog(parts.network, parts.dialogParent);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    applyToken(dialog.token(), true);
}

void Account::logout() {
    Q_EMIT loggingOut();  // HOST-32: the guests search through this account
    parts.player->clear();
    parts.library->logout();
    Yandex::ForgetToken(files.own);
    Q_EMIT statusText(tr("You have logged out"));
}

void Account::applyToken(const QString& token, bool save) {
    parts.api->setToken(token);
    Q_EMIT statusText(tr("Connecting to Yandex Music…"));
    parts.library->connectAccount([this, token, save](const Yandex::Account& account, const QString& error) {
        if (!error.isEmpty()) {
            Q_EMIT statusText(tr("Login failed: %1").arg(error));
            Q_EMIT failed();
            return;
        }
        if (save) {
            Yandex::SaveToken(files.own, token);
        }
        Q_EMIT statusText(tr("Hello, %1!").arg(account.displayName));
        Q_EMIT connected(account.displayName);
        if (!likesOnHold()) {
            queueLikes();
        }
    });
}

void Account::queueLikes() {
    if (parts.player->entries().isEmpty() && parts.library->isLoggedIn()) {
        parts.catalog->playLikes(false);
    }
}

}  // namespace App
