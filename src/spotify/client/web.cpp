#include "spotify/client/web.h"

#include "spotify/client/identity.h"
#include "spotify/client/session.h"
#include "spotify/protocol/login.h"

#include <QDateTime>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QTimer>
#include <QUrl>

#include <utility>
#include <variant>

namespace SpotifyClient {

namespace {

constexpr int kMaxChallengeRounds = 3;
constexpr qint64 kRefreshEarlyMs = 60'000;
const QByteArray kProtobuf = "application/x-protobuf";

SpotifyProtocol::ByteView View(const QByteArray& bytes) {
    return {reinterpret_cast<const std::uint8_t*>(bytes.constData()), static_cast<std::size_t>(bytes.size())};
}

QByteArray Bytes(const SpotifyProtocol::TBytes& bytes) {
    return QByteArray(reinterpret_cast<const char*>(bytes.data()), static_cast<qsizetype>(bytes.size()));
}

qint64 NowMs() {
    return QDateTime::currentMSecsSinceEpoch();
}

WebReply Failure(const QString& text) {
    WebReply reply;
    reply.error = text;
    return reply;
}

}  // namespace

Web::Web(
    QNetworkAccessManager* networkAccessManager,
    Endpoints addresses,
    Session* spotifySession,
    QString deviceId,
    QString appVersion,
    QObject* parent
)
    : QObject(parent)
    , networkManager(networkAccessManager)
    , endpoints(std::move(addresses))
    , session(spotifySession)
    , device(std::move(deviceId))
    , agent(UserAgent(appVersion)) { }

void Web::forgetTokens() {
    clientToken.clear();
    clientTokenUntilMs = 0;
    bearer.clear();
    bearerUntilMs = 0;
}

WebReply Web::ReplyOf(QNetworkReply* reply) {
    WebReply result;
    result.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    result.body = reply->readAll();
    result.network = result.status == 0;
    if (reply->error() != QNetworkReply::NoError) {
        result.error = reply->errorString();
    }
    return result;
}

QNetworkReply* Web::post(const QNetworkRequest& request, const QByteArray& body) {
    return networkManager->post(request, body);
}

void Web::withTokens(TReady ready) {
    const qint64 now = NowMs();
    if (!clientToken.isEmpty() && now < clientTokenUntilMs && !bearer.isEmpty() && now < bearerUntilMs) {
        ready(nullptr);
        return;
    }
    waiting << std::move(ready);
    if (fetching) {
        return;
    }
    fetching = true;
    if (clientToken.isEmpty() || now >= clientTokenUntilMs) {
        fetchClientToken(Bytes(SpotifyProtocol::ClientTokenRequest(ClientTokenParams(device))), 0);
    } else {
        fetchBearer({}, 0);
    }
}

void Web::finishTokens(const WebReply* failure) {
    fetching = false;
    const QList<TReady> ready = std::exchange(waiting, {});
    for (const TReady& next : ready) {
        next(failure);
    }
}

void Web::fetchClientToken(const QByteArray& body, int round) {
    QNetworkRequest request(QUrl(endpoints.clientToken + QStringLiteral("/v1/clienttoken")));
    request.setRawHeader("Accept", kProtobuf);
    request.setHeader(QNetworkRequest::ContentTypeHeader, kProtobuf);
    request.setRawHeader("User-Agent", agent);
    request.setTransferTimeout(kTimeoutMs);
    QNetworkReply* reply = post(request, body);
    connect(reply, &QNetworkReply::finished, this, [this, reply, round] {
        reply->deleteLater();
        const WebReply result = ReplyOf(reply);
        if (!result.ok()) {
            reportFailed(result);
            finishTokens(&result);
            return;
        }
        const SpotifyProtocol::TClientTokenReply parsed = SpotifyProtocol::ParseClientToken(View(result.body));
        if (const auto* granted = std::get_if<SpotifyProtocol::ClientTokenGranted>(&parsed)) {
            clientToken = QString::fromStdString(granted->token);
            clientTokenUntilMs = NowMs() + granted->refreshAfterSeconds * 1000 - kRefreshEarlyMs;
            fetchBearer({}, 0);
            return;
        }
        const auto* challenge = std::get_if<SpotifyProtocol::ClientTokenChallenge>(&parsed);
        std::optional<SpotifyProtocol::HashcashSolution> solution;
        if (challenge && challenge->hashcash && round < kMaxChallengeRounds) {
            solution = SpotifyProtocol::SolveHashcash({}, *challenge->hashcash);
        }
        if (!solution) {
            const WebReply failure = Failure(tr("Spotify did not give a client token"));
            report(WebProblem::Content);
            finishTokens(&failure);
            return;
        }
        fetchClientToken(Bytes(SpotifyProtocol::ClientTokenAnswer(challenge->state, *solution)), round + 1);
    });
}

void Web::fetchBearer(const QByteArray& requestBody, int round) {
    const SpotifyProtocol::Credentials& credentials = session->credentials();
    if (credentials.type != SpotifyProtocol::AuthenticationType::StoredCredentials || credentials.data.empty()) {
        WebReply failure = Failure(tr("not logged in to Spotify"));
        failure.status = 401;
        finishTokens(&failure);
        return;
    }
    QByteArray body = requestBody;
    if (body.isEmpty()) {
        body = Bytes(SpotifyProtocol::Login5Request(kClientId.toStdString(), device.toStdString(), credentials, {}, {})
        );
    }
    QNetworkRequest request(QUrl(endpoints.login5 + QStringLiteral("/v3/login")));
    request.setRawHeader("Accept", kProtobuf);
    request.setHeader(QNetworkRequest::ContentTypeHeader, kProtobuf);
    request.setRawHeader("client-token", clientToken.toLatin1());
    request.setRawHeader("User-Agent", agent);
    request.setTransferTimeout(kTimeoutMs);
    QNetworkReply* reply = post(request, body);
    connect(reply, &QNetworkReply::finished, this, [this, reply, round] {
        reply->deleteLater();
        const WebReply result = ReplyOf(reply);
        if (!result.ok()) {
            reportFailed(result);
            finishTokens(&result);
            return;
        }
        const SpotifyProtocol::TLogin5Reply parsed = SpotifyProtocol::ParseLogin5(View(result.body));
        if (const auto* granted = std::get_if<SpotifyProtocol::Login5Ok>(&parsed)) {
            bearer = QString::fromStdString(granted->accessToken);
            bearerUntilMs = NowMs() + granted->expiresInSeconds * 1000 - kRefreshEarlyMs;
            finishTokens(nullptr);
            return;
        }
        const auto* challenge = std::get_if<SpotifyProtocol::Login5Challenge>(&parsed);
        std::optional<SpotifyProtocol::HashcashSolution> solution;
        if (challenge && round < kMaxChallengeRounds) {
            solution = SpotifyProtocol::SolveHashcash(challenge->context, challenge->hashcash);
        }
        if (!solution) {
            WebReply failure = Failure(tr("Spotify did not give an access token"));
            if (std::holds_alternative<SpotifyProtocol::Login5Error>(parsed)) {
                failure.status = 401;
            }
            reportFailed(failure);
            finishTokens(&failure);
            return;
        }
        const SpotifyProtocol::TBytes answer = SpotifyProtocol::Login5Request(
            kClientId.toStdString(), device.toStdString(), session->credentials(), challenge->context, solution
        );
        fetchBearer(Bytes(answer), round + 1);
    });
}

void Web::spclient(const WebRequest& request, TReply done) {
    send(request, false, std::move(done), false);
}

void Web::pathfinder(const QByteArray& query, TReply done) {
    const WebRequest request{
        .method = "POST",
        .path = QStringLiteral("/pathfinder/v2/query"),
        .body = query,
        .contentType = "application/json;charset=UTF-8",
        .accept = "application/json",
        .accountQuery = false,
    };
    send(request, true, std::move(done), false);
}

void Web::send(const WebRequest& request, bool onPathfinder, TReply done, bool retried) {
    withTokens([=, this](const WebReply* failure) {
        if (failure) {
            done(*failure);
            return;
        }
        const QStringList hosts = session->spclients();
        if (!onPathfinder && hosts.isEmpty()) {
            done(Failure(tr("Spotify gave no server to ask")));
            return;
        }
        QString address = onPathfinder
            ? endpoints.pathfinder + request.path
            : QStringLiteral("%1://%2%3").arg(endpoints.spclientScheme, hosts.value(0), request.path);
        if (request.accountQuery) {
            const QChar separator = request.path.contains(u'?') ? u'&' : u'?';
            const QString salt = QString::number(QRandomGenerator::global()->generate());
            address += QStringLiteral("%1product=0&country=%2&salt=%3").arg(separator).arg(session->country(), salt);
        }
        QNetworkRequest message{QUrl(address)};
        message.setRawHeader("Authorization", "Bearer " + bearer.toLatin1());
        message.setRawHeader("client-token", clientToken.toLatin1());
        message.setRawHeader("User-Agent", agent);
        message.setRawHeader("Accept", request.accept);
        if (!request.contentType.isEmpty()) {
            message.setHeader(QNetworkRequest::ContentTypeHeader, request.contentType);
        }
        message.setTransferTimeout(kTimeoutMs);
        QNetworkReply* reply = networkManager->sendCustomRequest(message, request.method, request.body);
        connect(reply, &QNetworkReply::finished, this, [=, this] {
            reply->deleteLater();
            const WebReply result = ReplyOf(reply);
            if (!retried && result.status == 401) {
                bearer.clear();
                send(request, onPathfinder, done, true);
                return;
            }
            const int retryAfterMs = reply->rawHeader("Retry-After").toInt() * 1000;
            if (!retried && result.status == 429 && retryAfterMs <= kLongestRetryAfterMs) {
                QTimer::singleShot(retryAfterMs, this, [=, this] { send(request, onPathfinder, done, true); });
                return;
            }
            reportFailed(result);
            done(result);
        });
    });
}

void Web::report(WebProblem problem, int httpStatus) {
    Q_EMIT this->problem(problem, httpStatus);
}

void Web::reportFailed(const WebReply& reply) {
    if (reply.network) {
        return;
    }
    if (reply.status > 0 && !reply.ok()) {
        report(WebProblem::Http, reply.status);
    } else if (reply.status == 0) {
        report(WebProblem::Content);
    }
}

}  // namespace SpotifyClient
