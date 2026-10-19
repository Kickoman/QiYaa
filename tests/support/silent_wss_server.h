#pragma once

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QSslConfiguration>
#include <QSslServer>
#include <QUrl>

class QSslSocket;

namespace Tests {

class SilentWssServer : public QObject {
public:
    enum class Answers { Nothing, TheUpgradeOnly };

    explicit SilentWssServer(Answers answers);
    ~SilentWssServer() override;

    QUrl url() const;
    int connections() const { return connectionCount; }
    int upgrades() const { return upgradeCount; }

private:
    void readRequest(QSslSocket* socket);

    Answers answers;
    QSslConfiguration previousDefault;
    QHash<QSslSocket*, QByteArray> requests;
    int connectionCount = 0;
    int upgradeCount = 0;
    QSslServer server;
};

}  // namespace Tests
