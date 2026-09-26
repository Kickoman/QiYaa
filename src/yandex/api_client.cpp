#include "yandex/api_client.h"

#include "yandex/track_url.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>

#include <optional>
#include <utility>

namespace Yandex {

namespace {
constexpr int kTimeoutMs = 20'000;

QNetworkRequest MakeRequest(const QUrl& url, const QString& token) {
    QNetworkRequest request(url);
    if (!token.isEmpty()) {
        request.setRawHeader("Authorization", "OAuth " + token.toUtf8());
    }
    request.setRawHeader("Accept-Language", "ru");
    request.setTransferTimeout(kTimeoutMs);
    request.setAttribute(
        QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy
    );
    return request;
}
}  // namespace

QString Track::displayTitle() const {
    return artists.isEmpty() ? title
                             : artists.join(QStringLiteral(", ")) + QStringLiteral(" - ") + title;
}

QUrl Track::coverUrl(int size) const {
    if (coverUri.isEmpty()) {
        return {};
    }
    QString uri = coverUri;
    uri.replace(QStringLiteral("%%"), QStringLiteral("%1x%1").arg(size));
    return QUrl(uri.startsWith(QLatin1String("http")) ? uri : QStringLiteral("https://") + uri);
}

QUrl Track::webUrl() const {
    if (albumId.isEmpty()) {
        return QUrl(QStringLiteral("https://music.yandex.ru/track/%1").arg(id));
    }
    return QUrl(QStringLiteral("https://music.yandex.ru/album/%1/track/%2").arg(albumId, id));
}

ApiClient::ApiClient(QNetworkAccessManager* networkAccessManager, QObject* parent)
    : QObject(parent)
    , networkManager(networkAccessManager)
    , baseUrl(QStringLiteral("https://api.music.yandex.net")) { }

QString ApiClient::IdString(const QJsonValue& value) {
    if (value.isDouble()) {
        return QString::number(qint64(value.toDouble()));
    }
    return value.toString();
}

void ApiClient::getJson(const QString& path, const QUrlQuery& query, TJsonCallback callback) {
    QUrl url(baseUrl + path);
    if (!query.isEmpty()) {
        url.setQuery(query);
    }
    handleJson(networkManager->get(MakeRequest(url, accessToken)), std::move(callback));
}

void ApiClient::postForm(const QString& path, const TForm& form, TJsonCallback callback) {
    QByteArray body;
    for (const auto& [key, value] : form) {
        if (!body.isEmpty()) {
            body += '&';
        }
        body += QUrl::toPercentEncoding(key) + '=' + QUrl::toPercentEncoding(value);
    }
    QNetworkRequest request = MakeRequest(QUrl(baseUrl + path), accessToken);
    request.setHeader(
        QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded")
    );
    QNetworkReply* reply = networkManager->post(request, body);
    handleJson(reply, std::move(callback));
    trackPost(reply);
}

void ApiClient::postJson(const QString& path, const QJsonObject& body, TJsonCallback callback) {
    QNetworkRequest request = MakeRequest(QUrl(baseUrl + path), accessToken);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QNetworkReply* reply =
        networkManager->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    handleJson(reply, std::move(callback));
    trackPost(reply);
}

void ApiClient::trackPost(QNetworkReply* reply) {
    ++pendingPostCount;
    // Connected after handleJson's slot, so this runs after the callback.
    connect(reply, &QNetworkReply::finished, this, [this] {
        if (--pendingPostCount == 0) {
            Q_EMIT postsSettled();
        }
    });
}

void ApiClient::handleJson(QNetworkReply* reply, TJsonCallback callback) {
    connect(reply, &QNetworkReply::finished, this, [reply, callback = std::move(callback)] {
        reply->deleteLater();
        const QByteArray body = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonDocument document = QJsonDocument::fromJson(body);
        const QJsonObject object = document.object();

        if (reply->error() != QNetworkReply::NoError || status >= 400) {
            QString message = object.value(QStringLiteral("error"))
                                  .toObject()
                                  .value(QStringLiteral("message"))
                                  .toString();
            if (message.isEmpty()) {
                message = object.value(QStringLiteral("error")).toString();
            }
            if (message.isEmpty()) {
                message = reply->errorString();
            }
            callback(
                {},
                QStringLiteral("HTTP %1 from %2: %3").arg(status).arg(reply->url().path(), message)
            );
            return;
        }
        if (!document.isObject()) {
            callback(
                {},
                QStringLiteral("%1: the %2-byte reply is not a JSON object")
                    .arg(reply->url().path())
                    .arg(body.size())
            );
            return;
        }
        callback(
            object.contains(QStringLiteral("result")) ? object.value(QStringLiteral("result"))
                                                      : QJsonValue(object),
            {}
        );
    });
}

void ApiClient::accountStatus(TCallback<Account> callback) {
    getJson(
        QStringLiteral("/account/status"), {},
        [callback](const QJsonValue& result, const QString& error) {
            if (!error.isEmpty()) {
                return callback({}, error);
            }
            const QJsonObject accountObject =
                result.toObject().value(QStringLiteral("account")).toObject();
            Account account;
            account.uid = IdString(accountObject.value(QStringLiteral("uid")));
            account.login = accountObject.value(QStringLiteral("login")).toString();
            account.displayName = accountObject.value(QStringLiteral("displayName")).toString();
            if (account.displayName.isEmpty()) {
                account.displayName = account.login;
            }
            if (account.uid.isEmpty()) {
                return callback({}, QStringLiteral("not authorized (no uid) — token expired?"));
            }
            callback(account, {});
        }
    );
}

Track ApiClient::ParseTrack(const QJsonValue& value) {
    const QJsonObject object = value.toObject();
    Track track;
    track.id = IdString(object.value(QStringLiteral("id")));
    track.title = object.value(QStringLiteral("title")).toString();
    const QString version = object.value(QStringLiteral("version")).toString();
    if (!version.isEmpty()) {
        track.title += QStringLiteral(" (%1)").arg(version);
    }
    for (const QJsonValue& artist : object.value(QStringLiteral("artists")).toArray()) {
        track.artists << artist.toObject().value(QStringLiteral("name")).toString();
    }
    const QJsonArray albums = object.value(QStringLiteral("albums")).toArray();
    if (!albums.isEmpty()) {
        const QJsonObject album = albums.first().toObject();
        track.albumId = IdString(album.value(QStringLiteral("id")));
        track.albumTitle = album.value(QStringLiteral("title")).toString();
        track.year = album.value(QStringLiteral("year")).toInt();
        track.genre = album.value(QStringLiteral("genre")).toString();
        track.coverUri = album.value(QStringLiteral("coverUri")).toString();
    }
    if (track.coverUri.isEmpty()) {
        track.coverUri = object.value(QStringLiteral("coverUri")).toString();
    }
    if (track.coverUri.isEmpty()) {
        track.coverUri = object.value(QStringLiteral("ogImage")).toString();
    }
    track.durationMs = qint64(object.value(QStringLiteral("durationMs")).toDouble());
    track.available = object.value(QStringLiteral("available")).toBool(true);
    return track;
}

void ApiClient::tracks(const QStringList& ids, TCallback<QList<Track>> callback) {
    postForm(
        QStringLiteral("/tracks/"),
        {{QStringLiteral("track-ids"), ids.join(u',')},
         {QStringLiteral("with-positions"), QStringLiteral("false")}},
        [callback](const QJsonValue& result, const QString& error) {
            if (!error.isEmpty()) {
                return callback({}, error);
            }
            QList<Track> out;
            for (const QJsonValue& value : result.toArray()) {
                out << ParseTrack(value);
            }
            callback(out, {});
        }
    );
}

void ApiClient::resolveTrackUrl(const QString& trackId, TCallback<ResolvedUrl> callback) {
    const QString id = trackId.section(u':', 0, 0);
    QPointer<ApiClient> self(this);
    getJson(
        QStringLiteral("/tracks/%1/download-info").arg(id), {},
        [self, callback, id](const QJsonValue& result, const QString& error) {
            if (!self) {
                return;
            }
            if (!error.isEmpty()) {
                return callback({}, error);
            }
            const QJsonArray variants = result.toArray();
            const std::optional<DownloadVariant> best =
                PickBestVariant(ParseDownloadVariants(variants));
            if (!best) {
                return callback(
                    {},
                    QStringLiteral("track %1: none of %2 download variants has a usable link")
                        .arg(id)
                        .arg(variants.size())
                );
            }

            QUrl infoUrl = best->downloadInfoUrl;
            QUrlQuery query(infoUrl);
            query.addQueryItem(QStringLiteral("format"), QStringLiteral("json"));
            infoUrl.setQuery(query);

            QNetworkReply* reply =
                self->networkManager->get(MakeRequest(infoUrl, self->accessToken));
            const int bitrate = best->bitrateKbps;
            connect(reply, &QNetworkReply::finished, self, [reply, callback, bitrate] {
                reply->deleteLater();
                if (reply->error() != QNetworkReply::NoError) {
                    return callback({}, QStringLiteral("download-info: ") + reply->errorString());
                }
                const QByteArray body = reply->readAll();
                const std::optional<DownloadInfo> info = ParseDownloadInfo(body);
                if (!info) {
                    return callback(
                        {},
                        QStringLiteral("download-info: no host, path and s in a %1-byte reply")
                            .arg(body.size())
                    );
                }
                callback(ResolvedUrl{BuildTrackUrl(*info), bitrate}, {});
            });
        }
    );
}

void ApiClient::reportPlayStarted(
    const Account& account,
    const Track& track,
    const QString& playId
) {
    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    postForm(
        QStringLiteral("/play-audio"),
        {{QStringLiteral("track-id"), track.id},
         {QStringLiteral("album-id"), track.albumId},
         {QStringLiteral("from"), QStringLiteral("web-own_tracks-track-track-main")},
         {QStringLiteral("play-id"), playId},
         {QStringLiteral("uid"), account.uid},
         {QStringLiteral("timestamp"), now},
         {QStringLiteral("client-now"), now},
         {QStringLiteral("track-length-seconds"), QString::number(track.durationMs / 1000.0)},
         {QStringLiteral("total-played-seconds"), QStringLiteral("0")},
         {QStringLiteral("end-position-seconds"), QStringLiteral("0")}},
        [](const QJsonValue&, const QString& error) {
            if (!error.isEmpty()) {
                qWarning("play-audio failed: %s", qPrintable(error));
            }
        }
    );
}

}  // namespace Yandex
