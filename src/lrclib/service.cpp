#include "lrclib/service.h"

#include "lrclib/search.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

#include <utility>

namespace Lrclib {

namespace {

constexpr int kTimeoutMs = 8000;

QString Encoded(const QString& value) {
    return QString::fromLatin1(QUrl::toPercentEncoding(value));
}

Core::LyricsAnswer Nothing(bool serverError = false) {
    return {.from = Core::LyricsFrom::ThirdParty, .serverError = serverError};
}

}  // namespace

Service::Service(QNetworkAccessManager* manager, QString base, const QString& appVersion, QObject* parent)
    : Core::LyricsService(parent)
    , network(manager)
    , baseUrl(std::move(base))
    , userAgent(QStringLiteral("QiYaa/%1 (https://github.com/Kickoman/QiYaa)").arg(appVersion).toUtf8()) { }

Service::~Service() {
    cancel();
}

void Service::lyrics(const Core::Track& track, Core::TLyricsDone done) {
    cancel();
    Lookup next{.done = std::move(done), .artist = track.artists.value(0).trimmed(), .durationMs = track.durationMs};
    if (const QString title = track.title.trimmed(); !title.isEmpty()) {
        next.titles << title;
        if (const QString shorter = TitleWithoutVersion(title); !shorter.isEmpty()) {
            next.titles << shorter;
        }
    }
    const bool askable = !next.artist.isEmpty() && !next.titles.isEmpty();
    lookup = std::move(next);
    if (askable) {
        return search();
    }
    QTimer::singleShot(0, this, [this, current = generation] {
        if (current == generation) {
            answer(Nothing());
        }
    });
}

void Service::cancel() {
    ++generation;
    lookup.reset();
    if (reply) {
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
    reply = nullptr;
}

void Service::search() {
    QUrl url(baseUrl + QStringLiteral("/api/search"));
    const QString query =
        QStringLiteral("track_name=%1&artist_name=%2").arg(Encoded(lookup->titles.front()), Encoded(lookup->artist));
    url.setQuery(query, QUrl::StrictMode);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, userAgent);
    request.setTransferTimeout(kTimeoutMs);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    tooLarge = false;
    QNetworkReply* sent = network->get(request);
    reply = sent;
    connect(sent, &QNetworkReply::downloadProgress, this, [this, sent](qint64 received, qint64 total) {
        if (received > kMaxReplyBytes || total > kMaxReplyBytes) {
            tooLarge = true;
            sent->abort();
        }
    });
    connect(sent, &QNetworkReply::finished, this, [this, sent] { searched(sent); });
}

void Service::searched(QNetworkReply* finished) {
    finished->deleteLater();
    if (finished != reply || !lookup) {
        return;
    }
    reply = nullptr;
    if (tooLarge) {
        return answer({.status = Core::LyricsAnswer::Status::Failed, .from = Core::LyricsFrom::ThirdParty});
    }
    const int status = finished->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool cutOff = status < 400 && finished->error() != QNetworkReply::NoError;
    if (cutOff || status >= 500) {
        if (!lookup->retried) {
            lookup->retried = true;
            QTimer::singleShot(retryDelayMs, this, [this, current = generation] {
                if (current == generation && lookup) {
                    search();
                }
            });
            return;
        }
        if (status != 503) {
            return answer({.status = Core::LyricsAnswer::Status::Failed, .from = Core::LyricsFrom::ThirdParty});
        }
        lookup->serverError = true;
        return nextTitle();
    }
    if (status == 200) {
        const Choice choice = Pick(ParseSearch(finished->readAll()), lookup->durationMs);
        if (choice.answer.status != Core::LyricsAnswer::Status::Missing) {
            return answer(choice.answer);
        }
    }
    nextTitle();
}

void Service::nextTitle() {
    lookup->titles.removeFirst();
    if (lookup->titles.isEmpty()) {
        return answer(Nothing(lookup->serverError));
    }
    lookup->retried = false;
    search();
}

void Service::answer(Core::LyricsAnswer result) {
    const Core::TLyricsDone done = std::move(lookup->done);
    lookup.reset();
    done(std::move(result));
}

}  // namespace Lrclib
