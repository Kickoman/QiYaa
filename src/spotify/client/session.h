#pragma once

#include "spotify/client/endpoints.h"
#include "spotify/protocol/access_point.h"

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTcpSocket>
#include <QTimer>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

class QNetworkAccessManager;
class QNetworkReply;

namespace SpotifyClient {

enum class KeyFailure { Refused, Failed, Network };

struct KeyResult {
    std::optional<SpotifyProtocol::TAesKey> key;
    KeyFailure failure = KeyFailure::Failed;
};

class Session : public QObject {
    Q_OBJECT
public:
    using TKeyDone = std::function<void(const KeyResult& result)>;

    static constexpr int kKeyTimeoutMs = 5000;
    static constexpr int kKeyRetryMs = 1000;
    static constexpr int kQueuedKeyTimeoutMs = 20'000;

    Session(
        QNetworkAccessManager* networkAccessManager,
        Endpoints endpoints,
        SpotifyProtocol::ClientIdentity identity,
        QObject* parent = nullptr
    );
    ~Session() override;

    void login(const SpotifyProtocol::Credentials& credentials);
    void logout();

    bool loggedIn() const;
    bool hasAccount() const { return active; }
    const SpotifyProtocol::Credentials& credentials() const { return loginCredentials; }
    QString username() const;
    QString country() const { return accountCountry; }
    QString product() const { return accountProduct; }
    QStringList spclients() const { return spclientHosts; }

    void audioKey(const SpotifyProtocol::TGid& track, const SpotifyProtocol::TFileId& file, TKeyDone done);

    void setReconnectDelaysMs(QList<int> delays) { reconnectDelays = std::move(delays); }
    void setServerKey(SpotifyProtocol::ServerKey key) { serverKey = std::move(key); }

Q_SIGNALS:
    void loggedInChanged(bool loggedIn);
    void loginRefused(int code, const QString& description);
    void accountChanged();
    void keyFailed(bool refused);
    void accessPointFailed();

private:
    struct KeyRequest {
        SpotifyProtocol::TGid track{};
        SpotifyProtocol::TFileId file{};
        TKeyDone done;
        int attempts = 0;
        std::optional<std::uint32_t> sequence;
        std::shared_ptr<QTimer> timer;
    };

    void resolve();
    void connectNext();
    void onConnected();
    void onReadyRead();
    void onSocketLost();
    void process();
    void flush();
    void dropConnection();
    void scheduleReconnect();
    void sendQueuedKeys();
    void sendKey(KeyRequest& request);
    void retryOrFail(std::uint32_t sequence, KeyFailure failure);
    void finishKey(std::size_t index, const KeyResult& result);
    void failAllKeys(KeyFailure failure);
    std::int64_t now() const;

    QNetworkAccessManager* networkManager;
    Endpoints endpoints;
    SpotifyProtocol::ClientIdentity identity;
    SpotifyProtocol::Credentials loginCredentials;
    SpotifyProtocol::ServerKey serverKey = SpotifyProtocol::SpotifyServerKey();
    QTcpSocket socket;
    std::unique_ptr<SpotifyProtocol::AccessPoint> accessPoint;
    QPointer<QNetworkReply> resolveReply;
    QStringList accessPoints;
    QStringList spclientHosts;
    int nextAccessPoint = 0;
    bool active = false;
    bool connectionLive = false;
    bool loggedInNow = false;
    QString accountCountry;
    QString accountProduct;
    QTimer keepAliveTimer;
    QTimer reconnectTimer;
    QList<int> reconnectDelays = {2000, 4000, 8000, 16'000, 32'000, 60'000};
    int reconnectAttempt = 0;
    QElapsedTimer clock;
    std::vector<KeyRequest> keys;
};

}  // namespace SpotifyClient
