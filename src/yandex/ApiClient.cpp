#include "yandex/ApiClient.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>

#include "yandex/TrackUrl.h"

namespace qiyaa::yandex {

namespace {
constexpr int kTimeoutMs = 20000;

QNetworkRequest makeRequest(const QUrl& url, const QString& token) {
    QNetworkRequest req(url);
    if (!token.isEmpty()) req.setRawHeader("Authorization", "OAuth " + token.toUtf8());
    req.setRawHeader("Accept-Language", "ru");
    req.setTransferTimeout(kTimeoutMs);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    return req;
}
}  // namespace

QString Track::displayTitle() const {
    return artists.isEmpty() ? title : artists.join(QStringLiteral(", ")) + QStringLiteral(" - ") + title;
}

QUrl Track::webUrl() const {
    if (albumId.isEmpty()) return QUrl(QStringLiteral("https://music.yandex.ru/track/%1").arg(id));
    return QUrl(QStringLiteral("https://music.yandex.ru/album/%1/track/%2").arg(albumId, id));
}

ApiClient::ApiClient(QNetworkAccessManager* nam, QObject* parent)
    : QObject(parent), m_nam(nam), m_base(QStringLiteral("https://api.music.yandex.net")) {}

QString ApiClient::idString(const QJsonValue& v) {
    if (v.isDouble()) return QString::number(qint64(v.toDouble()));
    return v.toString();
}

void ApiClient::getJson(const QString& path, const QUrlQuery& query, JsonCallback cb) {
    QUrl url(m_base + path);
    if (!query.isEmpty()) url.setQuery(query);
    handleJson(m_nam->get(makeRequest(url, m_token)), std::move(cb));
}

void ApiClient::postForm(const QString& path, const Form& form, JsonCallback cb) {
    QByteArray body;
    for (const auto& [k, v] : form) {
        if (!body.isEmpty()) body += '&';
        body += QUrl::toPercentEncoding(k) + '=' + QUrl::toPercentEncoding(v);
    }
    QNetworkRequest req = makeRequest(QUrl(m_base + path), m_token);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    handleJson(m_nam->post(req, body), std::move(cb));
}

void ApiClient::postJson(const QString& path, const QJsonObject& body, JsonCallback cb) {
    QNetworkRequest req = makeRequest(QUrl(m_base + path), m_token);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    handleJson(m_nam->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact)), std::move(cb));
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
            if (msg.isEmpty()) msg = obj.value(QStringLiteral("error")).toString();
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
    getJson(QStringLiteral("/account/status"), {}, [cb](const QJsonValue& r, const QString& err) {
        if (!err.isEmpty()) return cb({}, err);
        const QJsonObject a = r.toObject().value(QStringLiteral("account")).toObject();
        Account acc;
        acc.uid = idString(a.value(QStringLiteral("uid")));
        acc.login = a.value(QStringLiteral("login")).toString();
        acc.displayName = a.value(QStringLiteral("displayName")).toString();
        if (acc.displayName.isEmpty()) acc.displayName = acc.login;
        if (acc.uid.isEmpty()) return cb({}, QStringLiteral("not authorized (no uid) — token expired?"));
        cb(acc, {});
    });
}

Track ApiClient::parseTrack(const QJsonValue& v) {
    const QJsonObject o = v.toObject();
    Track t;
    t.id = idString(o.value(QStringLiteral("id")));
    t.title = o.value(QStringLiteral("title")).toString();
    const QString version = o.value(QStringLiteral("version")).toString();
    if (!version.isEmpty()) t.title += QStringLiteral(" (%1)").arg(version);
    for (const QJsonValue& a : o.value(QStringLiteral("artists")).toArray())
        t.artists << a.toObject().value(QStringLiteral("name")).toString();
    const QJsonArray albums = o.value(QStringLiteral("albums")).toArray();
    if (!albums.isEmpty()) t.albumId = idString(albums.first().toObject().value(QStringLiteral("id")));
    t.durationMs = qint64(o.value(QStringLiteral("durationMs")).toDouble());
    t.available = o.value(QStringLiteral("available")).toBool(true);
    return t;
}

void ApiClient::tracks(const QStringList& ids, Callback<QList<Track>> cb) {
    postForm(QStringLiteral("/tracks/"),
             {{QStringLiteral("track-ids"), ids.join(u',')}, {QStringLiteral("with-positions"), QStringLiteral("false")}},
             [cb](const QJsonValue& r, const QString& err) {
                 if (!err.isEmpty()) return cb({}, err);
                 QList<Track> out;
                 for (const QJsonValue& v : r.toArray()) out << parseTrack(v);
                 cb(out, {});
             });
}

void ApiClient::resolveTrackUrl(const QString& trackId, Callback<ResolvedUrl> cb) {
    const QString id = trackId.section(u':', 0, 0);
    QPointer<ApiClient> self(this);
    getJson(QStringLiteral("/tracks/%1/download-info").arg(id), {}, [self, cb](const QJsonValue& r, const QString& err) {
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
    postForm(QStringLiteral("/play-audio"),
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
             [](const QJsonValue&, const QString& err) {
                 if (!err.isEmpty()) qWarning("play-audio failed: %s", qPrintable(err));
             });
}

}  // namespace qiyaa::yandex
