#pragma once

#include "jam/protocol.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>

#include <functional>
#include <vector>

class QWebSocket;

namespace Jam {

enum class Status { Idle, Connecting, Online, Offline, Stopped };

struct ClientOptions {
    QString appVersion;
    std::vector<int> reconnectDelaysMs = {1'000, 2'000, 4'000, 8'000, 16'000, 30'000};
    int pingIntervalMs = 20'000;
    std::function<qint64()> clock;
};

class Client : public QObject {
    Q_OBJECT
public:
    explicit Client(ClientOptions options, QObject* parent = nullptr);
    ~Client() override;

    Status status() const { return currentStatus; }
    qint64 clockOffsetMs() const { return offsetMs; }
    qint64 serverNow() const;
    const QStringList& outbox() const { return pendingStarted; }

    void start(const QUrl& url);
    void stop();
    bool send(const ClientMessage& message);
    void started(const QString& itemId, bool sendNow = true);
    void restoreOutbox(const QStringList& itemIds);
    void clearOutbox();
    void networkBack();

    static QUrl SocketUrl(const QString& serverUrl);

Q_SIGNALS:
    void statusChanged(Jam::Status status);
    void welcomed();
    void messageReceived(const Jam::ServerMessage& message);
    void outboxChanged(const QStringList& outbox);

private:
    void connectSocket();
    void dropSocket();
    void socketClosed(QWebSocket* closed);
    void receive(const QString& text);
    void ping();
    void setStatus(Status status);
    void setOutbox(QStringList itemIds);

    ClientOptions options;
    QUrl serverUrl;
    QWebSocket* socket = nullptr;
    Status currentStatus = Status::Idle;
    qint64 offsetMs = 0;
    int attempt = 0;
    bool awaitingPong = false;
    QStringList pendingStarted;
    QTimer retryTimer;
    QTimer pingTimer;
};

}  // namespace Jam
