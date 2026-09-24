#include "yandex/ApiClient.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QUrlQuery>

#include "yandex/TrackUrl.h"

namespace qiyaa::yandex {

namespace {
const QString kBase = QStringLiteral("https://api.music.yandex.net");
constexpr int kTimeoutMs = 20000;
}  // namespace

QString Track::displayTitle() const {
    return artists.isEmpty() ? title : artists.join(QStringLiteral(", ")) + QStringLiteral(" - ") + title;
}

ApiClient::ApiClient(QNetworkAccessManager* nam, QObject* parent) : QObject(parent), m_nam(nam) {}

static QNetworkRequest makeRequest(const QUrl& url, const QString& token) {
    QNetworkRequest req(url);
    if (!token.isEmpty()) req.setRawHeader("Authorization", "OAuth " + token.toUtf8());
    req.setRawHeader("Accept-Language", "ru");
    req.setTransferTimeout(kTimeoutMs);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    return req;
}

QNetworkReply* ApiClient::get(const QString& path) {
    return m_nam->get(makeRequest(QUrl(kBase + path), m_token));
}

QNetworkReply* ApiClient::postForm(const QString& path, const QList<std::pair<QString, QString>>& form) {
    QUrlQuery q;
    for (const auto& [k, v] : form) q.addQueryItem(k, v);
    QNetworkRequest req = makeRequest(QUrl(kBase + path), m_token);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    return m_nam->post(req, q.query(QUrl::FullyEncoded).toUtf8());
}

void ApiClient::handleJson(QNetworkReply* reply, JsonCallback cb) {
    connect(reply, &QNetworkReply::finished, this, [reply, cb = std::move(cb)] {
        reply->deleteLater();
        const QByteArray body = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonDocument doc = QJsonDocument::fromJson(body);
        const QJsonObject obj = doc.object();

        if (reply->error() != QNetworkReply::NoError || status >= 400) {
            QString msg = obj.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString();
            if (msg.isEmpty()) msg = reply->errorString();
            cb({}, QStringLiteral("HTTP %1: %2").arg(status).arg(msg));
            return;
        }
        if (!doc.isObject() || !obj.contains(QStringLiteral("result"))) {
            cb({}, QStringLiteral("unexpected response"));
            return;
        }
        cb(obj.value(QStringLiteral("result")), {});
    });
}

void ApiClient::accountStatus(Callback<Account> cb) {
    handleJson(get(QStringLiteral("/account/status")), [cb](const QJsonValue& r, const QString& err) {
        if (!err.isEmpty()) return cb({}, err);
        const QJsonObject a = r.toObject().value(QStringLiteral("account")).toObject();
        Account acc;
        const QJsonValue uid = a.value(QStringLiteral("uid"));
        acc.uid = uid.isDouble() ? QString::number(qint64(uid.toDouble())) : uid.toString();
        acc.login = a.value(QStringLiteral("login")).toString();
        acc.displayName = a.value(QStringLiteral("displayName")).toString();
        if (acc.uid.isEmpty()) return cb({}, QStringLiteral("not authorized (no uid) — token expired?"));
        cb(acc, {});
    });
}

void ApiClient::likedTrackIds(const QString& uid, Callback<QStringList> cb) {
    handleJson(get(QStringLiteral("/users/%1/likes/tracks").arg(uid)), [cb](const QJsonValue& r, const QString& err) {
        if (!err.isEmpty()) return cb({}, err);
        QStringList ids;
        const QJsonArray arr = r.toObject().value(QStringLiteral("library")).toObject().value(QStringLiteral("tracks")).toArray();
        for (const QJsonValue& v : arr) {
            const QJsonObject o = v.toObject();
            QString id = o.value(QStringLiteral("id")).toVariant().toString();
            const QString album = o.value(QStringLiteral("albumId")).toVariant().toString();
            if (id.isEmpty()) continue;
            ids << (album.isEmpty() ? id : id + u':' + album);
        }
        cb(ids, {});
    });
}

Track ApiClient::parseTrack(const QJsonValue& v) {
    const QJsonObject o = v.toObject();
    Track t;
    t.id = o.value(QStringLiteral("id")).toVariant().toString();
    t.title = o.value(QStringLiteral("title")).toString();
    const QString version = o.value(QStringLiteral("version")).toString();
    if (!version.isEmpty()) t.title += QStringLiteral(" (%1)").arg(version);
    for (const QJsonValue& a : o.value(QStringLiteral("artists")).toArray())
        t.artists << a.toObject().value(QStringLiteral("name")).toString();
    const QJsonArray albums = o.value(QStringLiteral("albums")).toArray();
    if (!albums.isEmpty()) t.albumId = albums.first().toObject().value(QStringLiteral("id")).toVariant().toString();
    t.durationMs = qint64(o.value(QStringLiteral("durationMs")).toDouble());
    t.available = o.value(QStringLiteral("available")).toBool(true);
    return t;
}

void ApiClient::tracks(const QStringList& ids, Callback<QList<Track>> cb) {
    auto reply = postForm(QStringLiteral("/tracks/"), {{QStringLiteral("track-ids"), ids.join(u',')},
                                                       {QStringLiteral("with-positions"), QStringLiteral("false")}});
    handleJson(reply, [cb](const QJsonValue& r, const QString& err) {
        if (!err.isEmpty()) return cb({}, err);
        QList<Track> out;
        for (const QJsonValue& v : r.toArray()) out << parseTrack(v);
        cb(out, {});
    });
}

void ApiClient::resolveTrackUrl(const QString& trackId, Callback<ResolvedUrl> cb) {
    const QString id = trackId.section(u':', 0, 0);
    QPointer<ApiClient> self(this);
    handleJson(get(QStringLiteral("/tracks/%1/download-info").arg(id)), [self, cb](const QJsonValue& r, const QString& err) {
        if (!self) return;
        if (!err.isEmpty()) return cb({}, err);
        DownloadVariant best;
        if (!pickBestVariant(parseDownloadVariants(r.toArray()), &best))
            return cb({}, QStringLiteral("no download variants"));

        QUrl infoUrl = best.downloadInfoUrl;
        QUrlQuery q(infoUrl);
        q.addQueryItem(QStringLiteral("format"), QStringLiteral("json"));
        infoUrl.setQuery(q);

        QNetworkReply* reply = self->m_nam->get(makeRequest(infoUrl, self->m_token));
        const int bitrate = best.bitrateKbps;
        connect(reply, &QNetworkReply::finished, self, [reply, cb, bitrate] {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError)
                return cb({}, QStringLiteral("download-info: ") + reply->errorString());
            DownloadInfo info;
            if (!parseDownloadInfo(reply->readAll(), &info))
                return cb({}, QStringLiteral("download-info: unexpected response"));
            cb(ResolvedUrl{buildTrackUrl(info), bitrate}, {});
        });
    });
}

void ApiClient::reportPlayStarted(const Account& account, const Track& track, const QString& playId) {
    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    auto reply = postForm(QStringLiteral("/play-audio"),
                          {{QStringLiteral("track-id"), track.id},
                           {QStringLiteral("album-id"), track.albumId},
                           {QStringLiteral("from"), QStringLiteral("web-own_tracks-track-track-main")},
                           {QStringLiteral("play-id"), playId},
                           {QStringLiteral("uid"), account.uid},
                           {QStringLiteral("timestamp"), now},
                           {QStringLiteral("client-now"), now},
                           {QStringLiteral("track-length-seconds"), QString::number(track.durationMs / 1000.0)},
                           {QStringLiteral("total-played-seconds"), QStringLiteral("0")},
                           {QStringLiteral("end-position-seconds"), QStringLiteral("0")}});
    handleJson(reply, [](const QJsonValue&, const QString& err) {
        if (!err.isEmpty()) qWarning("play-audio failed: %s", qPrintable(err));
    });
}

}  // namespace qiyaa::yandex
