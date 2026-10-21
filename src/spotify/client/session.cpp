#include "spotify/client/session.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QSignalBlocker>
#include <QUrl>

#include <algorithm>
#include <utility>
#include <variant>

namespace SpotifyClient {

namespace {

constexpr int kResolveTimeoutMs = 10'000;
constexpr std::uint16_t kKeyRefusedForTheAccount = 1;
const QString kFallbackAccessPoint = QStringLiteral("ap.spotify.com:443");
const QString kFallbackSpclient = QStringLiteral("spclient.wg.spotify.com:443");

void Random(std::uint8_t* data, std::size_t size) {
    for (std::size_t i = 0; i < size; ++i) {
        data[i] = static_cast<std::uint8_t>(QRandomGenerator::system()->bounded(256));
    }
}

QStringList StringsOf(const QJsonValue& value) {
    QStringList strings;
    for (const QJsonValue& item : value.toArray()) {
        if (const QString text = item.toString(); !text.isEmpty()) {
            strings << text;
        }
    }
    return strings;
}

}  // namespace

Session::Session(
    QNetworkAccessManager* networkAccessManager,
    Endpoints addresses,
    SpotifyProtocol::ClientIdentity clientIdentity,
    QObject* parent
)
    : QObject(parent)
    , networkManager(networkAccessManager)
    , endpoints(std::move(addresses))
    , identity(std::move(clientIdentity)) {
    clock.start();
    keepAliveTimer.setSingleShot(true);
    reconnectTimer.setSingleShot(true);
    connect(&socket, &QTcpSocket::connected, this, &Session::onConnected);
    connect(&socket, &QTcpSocket::readyRead, this, &Session::onReadyRead);
    connect(&socket, &QTcpSocket::disconnected, this, &Session::onSocketLost);
    connect(&socket, &QTcpSocket::errorOccurred, this, &Session::onSocketLost);
    connect(&keepAliveTimer, &QTimer::timeout, this, [this] {
        if (accessPoint) {
            accessPoint->advance(now());
            process();
        }
    });
    connect(&reconnectTimer, &QTimer::timeout, this, &Session::connectNext);
}

Session::~Session() {
    active = false;
    socket.disconnect(this);
    socket.abort();
    if (resolveReply) {
        resolveReply->abort();
    }
}

void Session::login(const SpotifyProtocol::Credentials& credentials) {
    logout();
    loginCredentials = credentials;
    active = true;
    reconnectAttempt = 0;
    resolve();
}

void Session::logout() {
    active = false;
    reconnectTimer.stop();
    keepAliveTimer.stop();
    if (resolveReply) {
        resolveReply->abort();
    }
    dropConnection();
    failAllKeys(KeyFailure::Network);
    loginCredentials = {};
    accountCountry.clear();
    accountProduct.clear();
}

bool Session::loggedIn() const {
    return loggedInNow;
}

QString Session::username() const {
    return QString::fromStdString(loginCredentials.username);
}

std::int64_t Session::now() const {
    return clock.elapsed();
}

void Session::resolve() {
    QUrl url(endpoints.apresolve + QStringLiteral("/?type=accesspoint&type=spclient"));
    QNetworkRequest request(url);
    request.setTransferTimeout(kResolveTimeoutMs);
    QNetworkReply* reply = networkManager->get(request);
    resolveReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::OperationCanceledError || !active) {
            return;
        }
        const QJsonObject json = QJsonDocument::fromJson(reply->readAll()).object();
        accessPoints = StringsOf(json.value(QStringLiteral("accesspoint")));
        const QStringList spclients = StringsOf(json.value(QStringLiteral("spclient")));
        if (accessPoints.isEmpty()) {
            accessPoints << kFallbackAccessPoint;
        }
        if (!spclients.isEmpty()) {
            spclientHosts = spclients;
        } else if (spclientHosts.isEmpty()) {
            spclientHosts << kFallbackSpclient;
        }
        nextAccessPoint = 0;
        connectNext();
    });
}

void Session::connectNext() {
    if (!active) {
        return;
    }
    if (accessPoints.isEmpty() || nextAccessPoint >= accessPoints.size()) {
        resolve();
        return;
    }
    dropConnection();
    const QString address = accessPoints.at(nextAccessPoint++);
    const QString host = address.section(u':', 0, -2);
    const quint16 port = address.section(u':', -1).toUShort();
    connectionLive = true;
    socket.connectToHost(host, port == 0 ? 443 : port);
}

void Session::onConnected() {
    accessPoint = std::make_unique<SpotifyProtocol::AccessPoint>(identity, loginCredentials, Random, serverKey);
    accessPoint->start();
    process();
}

void Session::onReadyRead() {
    const QByteArray bytes = socket.readAll();
    if (!accessPoint) {
        return;
    }
    accessPoint->receive(
        {reinterpret_cast<const std::uint8_t*>(bytes.constData()), static_cast<std::size_t>(bytes.size())}, now()
    );
    process();
}

void Session::onSocketLost() {
    if (!active || !connectionLive || socket.state() == QAbstractSocket::ConnectedState) {
        return;
    }
    const bool wasLoggedIn = loggedInNow;
    dropConnection();
    if (!wasLoggedIn && nextAccessPoint < accessPoints.size()) {
        connectNext();
        return;
    }
    scheduleReconnect();
}

void Session::flush() {
    if (!accessPoint) {
        return;
    }
    if (const SpotifyProtocol::TBytes outgoing = accessPoint->takeOutgoing(); !outgoing.empty()) {
        socket.write(reinterpret_cast<const char*>(outgoing.data()), static_cast<qint64>(outgoing.size()));
    }
}

void Session::process() {
    if (!accessPoint) {
        return;
    }
    flush();
    for (SpotifyProtocol::TApEvent& event : accessPoint->takeEvents()) {
        if (auto* welcome = std::get_if<SpotifyProtocol::ApLoggedIn>(&event)) {
            loginCredentials = std::move(welcome->reusable);
            loggedInNow = true;
            reconnectAttempt = 0;
            Q_EMIT loggedInChanged(true);
            sendQueuedKeys();
        } else if (const auto* refused = std::get_if<SpotifyProtocol::ApLoginFailed>(&event)) {
            if (refused->code == SpotifyProtocol::LoginError::TryAnotherAp) {
                dropConnection();
                connectNext();
                return;
            }
            active = false;
            dropConnection();
            failAllKeys(KeyFailure::Failed);
            Q_EMIT loginRefused(static_cast<int>(refused->code), QString::fromStdString(refused->description));
            return;
        } else if (const auto* country = std::get_if<SpotifyProtocol::ApCountry>(&event)) {
            accountCountry = QString::fromStdString(country->code);
            Q_EMIT accountChanged();
        } else if (const auto* product = std::get_if<SpotifyProtocol::ApProduct>(&event)) {
            accountProduct = QString::fromStdString(product->type);
            Q_EMIT accountChanged();
        } else if (const auto* key = std::get_if<SpotifyProtocol::ApAudioKey>(&event)) {
            const auto found = std::find_if(keys.begin(), keys.end(), [&](const KeyRequest& request) {
                return request.sequence == key->sequence;
            });
            if (found != keys.end()) {
                finishKey(static_cast<std::size_t>(found - keys.begin()), KeyResult{key->key, KeyFailure::Failed});
            }
        } else if (const auto* keyRefused = std::get_if<SpotifyProtocol::ApAudioKeyRefused>(&event)) {
            const KeyFailure failure =
                keyRefused->code == kKeyRefusedForTheAccount ? KeyFailure::Refused : KeyFailure::Failed;
            retryOrFail(keyRefused->sequence, failure);
        }
    }
    if (accessPoint && accessPoint->failed()) {
        dropConnection();
        Q_EMIT accessPointFailed();
        scheduleReconnect();
        return;
    }
    if (const std::optional<std::int64_t> deadline = accessPoint ? accessPoint->deadline() : std::nullopt) {
        keepAliveTimer.start(static_cast<int>(std::max<std::int64_t>(0, *deadline - now())));
    }
}

void Session::dropConnection() {
    keepAliveTimer.stop();
    accessPoint.reset();
    connectionLive = false;
    {
        const QSignalBlocker blocker(socket);
        socket.abort();
    }
    for (KeyRequest& request : keys) {
        request.sequence.reset();
        request.timer.reset();
    }
    if (loggedInNow) {
        loggedInNow = false;
        Q_EMIT loggedInChanged(false);
    }
}

void Session::scheduleReconnect() {
    if (!active) {
        return;
    }
    failAllKeys(KeyFailure::Network);
    const int delay =
        reconnectDelays.value(std::min<int>(reconnectAttempt, static_cast<int>(reconnectDelays.size()) - 1), 60'000);
    ++reconnectAttempt;
    nextAccessPoint = 0;
    reconnectTimer.start(delay);
}

void Session::audioKey(const SpotifyProtocol::TGid& track, const SpotifyProtocol::TFileId& file, TKeyDone done) {
    if (!active) {
        done(KeyResult{std::nullopt, KeyFailure::Network});
        return;
    }
    keys.push_back(KeyRequest{track, file, std::move(done), 0, std::nullopt, nullptr});
    KeyRequest& request = keys.back();
    if (loggedInNow) {
        sendKey(request);
        return;
    }
    request.timer = std::make_shared<QTimer>();
    request.timer->setSingleShot(true);
    const SpotifyProtocol::TFileId waiting = file;
    connect(request.timer.get(), &QTimer::timeout, this, [this, waiting] {
        const auto found = std::find_if(keys.begin(), keys.end(), [&](const KeyRequest& queued) {
            return !queued.sequence && queued.file == waiting;
        });
        if (found != keys.end()) {
            finishKey(static_cast<std::size_t>(found - keys.begin()), KeyResult{std::nullopt, KeyFailure::Network});
        }
    });
    request.timer->start(kQueuedKeyTimeoutMs);
}

void Session::sendQueuedKeys() {
    for (KeyRequest& request : keys) {
        if (!request.sequence) {
            sendKey(request);
        }
    }
}

void Session::sendKey(KeyRequest& request) {
    if (!accessPoint) {
        return;
    }
    request.sequence = accessPoint->requestAudioKey(request.track, request.file);
    ++request.attempts;
    request.timer = std::make_shared<QTimer>();
    request.timer->setSingleShot(true);
    if (request.sequence) {
        const std::uint32_t sequence = *request.sequence;
        connect(request.timer.get(), &QTimer::timeout, this, [this, sequence] {
            retryOrFail(sequence, KeyFailure::Failed);
        });
        request.timer->start(kKeyTimeoutMs);
    }
    flush();
}

void Session::retryOrFail(std::uint32_t sequence, KeyFailure failure) {
    const auto found = std::find_if(keys.begin(), keys.end(), [&](const KeyRequest& request) {
        return request.sequence == sequence;
    });
    if (found == keys.end()) {
        return;
    }
    if (failure == KeyFailure::Refused || found->attempts >= 2) {
        finishKey(static_cast<std::size_t>(found - keys.begin()), KeyResult{std::nullopt, failure});
        Q_EMIT keyFailed(failure == KeyFailure::Refused);
        return;
    }
    found->sequence.reset();
    found->timer = std::make_shared<QTimer>();
    found->timer->setSingleShot(true);
    const SpotifyProtocol::TFileId file = found->file;
    connect(found->timer.get(), &QTimer::timeout, this, [this, file] {
        const auto again = std::find_if(keys.begin(), keys.end(), [&](const KeyRequest& request) {
            return !request.sequence && request.file == file && request.attempts > 0;
        });
        if (again != keys.end() && loggedInNow) {
            sendKey(*again);
        }
    });
    found->timer->start(kKeyRetryMs);
}

void Session::finishKey(std::size_t index, const KeyResult& result) {
    TKeyDone done = std::move(keys[index].done);
    keys.erase(keys.begin() + static_cast<std::ptrdiff_t>(index));
    if (done) {
        done(result);
    }
}

void Session::failAllKeys(KeyFailure failure) {
    std::vector<KeyRequest> failing = std::exchange(keys, {});
    for (KeyRequest& request : failing) {
        if (request.done) {
            request.done(KeyResult{std::nullopt, failure});
        }
    }
}

}  // namespace SpotifyClient
