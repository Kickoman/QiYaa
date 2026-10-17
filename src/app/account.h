#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

class QNetworkAccessManager;
class QWidget;

namespace Core {
class Player;
}  // namespace Core

namespace Yandex {
class ApiClient;
class Library;
}  // namespace Yandex

namespace YandexSource {
class Catalog;
}  // namespace YandexSource

namespace App {

class Account : public QObject {
    Q_OBJECT
public:
    struct TokenFiles {
        QString own;
        QStringList imported;
        QString ownDirectory;
    };

    struct Parts {
        Yandex::ApiClient* api = nullptr;
        Yandex::Library* library = nullptr;
        Core::Player* player = nullptr;
        YandexSource::Catalog* catalog = nullptr;
        QNetworkAccessManager* network = nullptr;
        QWidget* dialogParent = nullptr;
    };

    Account(const Parts& parts, TokenFiles files, QObject* parent = nullptr);

    void setLikesOnHold(std::function<bool()> onHold);

    void start();
    void login();
    void logout();
    void applyToken(const QString& token, bool save);
    void queueLikes();

Q_SIGNALS:
    void statusText(const QString& text);
    void connected(const QString& displayName);
    void failed();
    void loggingOut();

private:
    Parts parts;
    TokenFiles files;
    std::function<bool()> likesOnHold;
};

}  // namespace App
