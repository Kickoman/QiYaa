#include "telemetry/reporter.h"

#include "telemetry/crash_handler.h"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QUuid>

#include <algorithm>

namespace Telemetry {

namespace {

constexpr int kMarkerIntervalMs = 60 * 1000;
constexpr int kFirstRetrySeconds = 60;
constexpr int kLastRetrySeconds = 60 * 60;

const QRegularExpression& SessionFile() {
    static const QRegularExpression pattern(QStringLiteral(
        "^([0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12})\\.(lock|json|crash|"
        "queue)$"
    ));
    return pattern;
}

QSet<QString> SessionsIn(const QString& directory) {
    QSet<QString> sessions;
    for (const QString& name : QDir(directory).entryList(QDir::Files)) {
        const QRegularExpressionMatch match = SessionFile().match(name);
        if (match.hasMatch()) {
            sessions.insert(match.captured(1));
        }
    }
    return sessions;
}

bool IsRunning(const QString& lockPath) {
    if (!QFile::exists(lockPath)) {
        return false;
    }
    QLockFile lock(lockPath);
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0)) {
        return true;
    }
    lock.unlock();
    return false;
}

void RemoveSession(const QString& directory, const QString& session) {
    for (const QString& suffix :
         {QStringLiteral("json"), QStringLiteral("crash"), QStringLiteral("queue"),
          QStringLiteral("lock")}) {
        QFile::remove(QDir(directory).filePath(session + u'.' + suffix));
    }
}

QList<QJsonObject> ReadQueue(const QString& path) {
    QList<QJsonObject> events;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return events;
    }
    for (const QByteArray& line : file.readAll().split('\n')) {
        const QJsonDocument document = QJsonDocument::fromJson(line);
        if (document.isObject()) {
            events << document.object();
        }
    }
    return events;
}

QByteArray Compact(const QJsonObject& object) {
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QString Now() {
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

}  // namespace

Reporter::Reporter(Config configuration, QObject* parent)
    : QObject(parent)
    , config(std::move(configuration))
    , sessionId(QUuid::createUuid().toString(QUuid::WithoutBraces)) {
    running.start();
    sendTimer.setInterval(config.sendIntervalMs);
    connect(&sendTimer, &QTimer::timeout, this, &Reporter::send);
    markerTimer.setInterval(kMarkerIntervalMs);
    connect(&markerTimer, &QTimer::timeout, this, &Reporter::saveMarker);
}

Reporter::~Reporter() {
    if (forgotten || !begun) {
        return;
    }
    if (reply) {
        reply->abort();
    }
    UninstallCrashHandler();
    QFile::remove(file(sessionId, QStringLiteral("crash")));
    saveQueue();
    if (queue.isEmpty()) {
        QFile::remove(file(sessionId, QStringLiteral("queue")));
    }
    lock->unlock();
}

QString Reporter::file(const QString& session, const QString& suffix) const {
    return QDir(config.directory).filePath(session + u'.' + suffix);
}

void Reporter::begin(const QJsonObject& startFields) {
    if (begun || forgotten) {
        return;
    }
    begun = true;
    QDir().mkpath(config.directory);
    lock = std::make_unique<QLockFile>(file(sessionId, QStringLiteral("lock")));
    lock->setStaleLockTime(0);
    lock->tryLock(0);
    reportEarlierRuns();
    saveMarker();
    InstallCrashHandler(file(sessionId, QStringLiteral("crash")), sessionId, config.version);
    record(QStringLiteral("start"), startFields);
    markerTimer.start();
    sendTimer.start();
    send();
}

void Reporter::reportEarlierRuns() {
    for (const QString& session : SessionsIn(config.directory)) {
        if (session == sessionId || IsRunning(file(session, QStringLiteral("lock")))) {
            continue;
        }
        for (const QJsonObject& event : ReadQueue(file(session, QStringLiteral("queue")))) {
            queue << event;
        }
        if (const std::optional<CrashReport> crash =
                ReadCrashReport(file(session, QStringLiteral("crash")))) {
            QJsonObject event{
                {QStringLiteral("type"), QStringLiteral("crash")},
                {QStringLiteral("at"), Now()},
                {QStringLiteral("session"), crash->session},
                {QStringLiteral("version"), crash->version},
                {QStringLiteral("signal"), crash->signal},
                {QStringLiteral("frames"), QJsonArray::fromStringList(crash->frames)},
                {QStringLiteral("seconds"), crash->seconds},
            };
            if (!crash->exceptionType.isEmpty()) {
                event.insert(QStringLiteral("exceptionType"), crash->exceptionType);
            }
            queue << event;
        } else {
            QFile marker(file(session, QStringLiteral("json")));
            if (marker.open(QIODevice::ReadOnly)) {
                const QJsonObject run = QJsonDocument::fromJson(marker.readAll()).object();
                queue << QJsonObject{
                    {QStringLiteral("type"), QStringLiteral("unclean_exit")},
                    {QStringLiteral("at"), Now()},
                    {QStringLiteral("session"), session},
                    {QStringLiteral("version"), run.value(QStringLiteral("version")).toString()},
                    {QStringLiteral("seconds"), run.value(QStringLiteral("seconds")).toInteger()},
                };
            }
        }
        RemoveSession(config.directory, session);
    }
    while (queue.size() > kMaxQueuedEvents) {
        queue.removeFirst();
    }
}

void Reporter::record(const QString& type, QJsonObject fields) {
    if (forgotten) {
        return;
    }
    fields.insert(QStringLiteral("type"), type);
    fields.insert(QStringLiteral("at"), Now());
    fields.insert(QStringLiteral("session"), sessionId);
    fields.insert(QStringLiteral("version"), config.version);
    queue << fields;
    while (queue.size() > kMaxQueuedEvents) {
        queue.removeFirst();
        inFlight = std::max(0, inFlight - 1);
    }
    saveQueue();
}

void Reporter::recordError(const QString& area, const QString& kind, int httpStatus) {
    if (errors >= kMaxErrorsPerSession) {
        ++errorsDropped;
        return;
    }
    ++errors;
    QJsonObject fields{{QStringLiteral("area"), area}, {QStringLiteral("kind"), kind}};
    if (httpStatus >= 100 && httpStatus <= 599) {
        fields.insert(QStringLiteral("httpStatus"), httpStatus);
    }
    record(QStringLiteral("error"), fields);
}

void Reporter::recordFeature(const QString& name, const QString& value) {
    record(
        QStringLiteral("feature"),
        {{QStringLiteral("name"), name}, {QStringLiteral("value"), value}}
    );
}

void Reporter::countTrack() {
    ++tracks;
}

void Reporter::countJam() {
    ++jams;
}

void Reporter::setPlaying(bool on) {
    if (on == playing) {
        return;
    }
    if (playing) {
        playedBefore += playingSince.elapsed();
    } else {
        playingSince.start();
    }
    playing = on;
}

void Reporter::setMilkdropShown(bool shown) {
    if (shown == milkdropShown) {
        return;
    }
    if (milkdropShown) {
        milkdropBefore += milkdropSince.elapsed();
    } else {
        milkdropSince.start();
    }
    milkdropShown = shown;
}

qint64 Reporter::playedMs() const {
    return playedBefore + (playing ? playingSince.elapsed() : 0);
}

qint64 Reporter::milkdropMs() const {
    return milkdropBefore + (milkdropShown ? milkdropSince.elapsed() : 0);
}

void Reporter::finish() {
    if (!begun || finished || forgotten) {
        return;
    }
    finished = true;
    record(
        QStringLiteral("exit"),
        {
            {QStringLiteral("seconds"), running.elapsed() / 1000},
            {QStringLiteral("tracks"), tracks},
            {QStringLiteral("playMinutes"), playedMs() / 60000},
            {QStringLiteral("milkdropMinutes"), milkdropMs() / 60000},
            {QStringLiteral("jams"), jams},
            {QStringLiteral("errorsDropped"), errorsDropped},
        }
    );
    markerTimer.stop();
    QFile::remove(file(sessionId, QStringLiteral("json")));
    send();
}

void Reporter::saveQueue() const {
    QSaveFile out(file(sessionId, QStringLiteral("queue")));
    if (!out.open(QIODevice::WriteOnly)) {
        return;
    }
    for (const QJsonObject& event : queue) {
        out.write(Compact(event) + '\n');
    }
    out.commit();
}

void Reporter::saveMarker() const {
    if (finished || forgotten) {
        return;
    }
    QSaveFile out(file(sessionId, QStringLiteral("json")));
    if (out.open(QIODevice::WriteOnly)) {
        out.write(Compact({
            {QStringLiteral("version"), config.version},
            {QStringLiteral("seconds"), running.elapsed() / 1000},
        }));
        out.commit();
    }
}

void Reporter::send() {
    if (forgotten || reply || queue.isEmpty() || !config.network || config.endpoint.isEmpty()) {
        return;
    }
    if (blockedUntil.isValid() && QDateTime::currentDateTimeUtc() < blockedUntil) {
        return;
    }
    QJsonArray events;
    qsizetype bytes = 128;
    for (const QJsonObject& event : queue) {
        const qsizetype size = Compact(event).size() + 1;
        if (events.size() == kMaxBatchEvents
            || (!events.isEmpty() && bytes + size > kMaxBatchBytes)) {
            break;
        }
        events.append(event);
        bytes += size;
    }
    inFlight = static_cast<int>(events.size());
    const QJsonObject batch{
        {QStringLiteral("app"), QStringLiteral("desktop")},
        {QStringLiteral("machine"), config.machine},
        {QStringLiteral("events"), events},
    };
    QNetworkRequest request(config.endpoint);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setTransferTimeout(30 * 1000);
    reply = config.network->post(request, Compact(batch));
    connect(reply, &QNetworkReply::finished, this, [this, answer = reply] {
        answer->deleteLater();
        const int status = answer->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if ((status >= 200 && status < 300) || status == 400 || status == 413) {
            failures = 0;
            sent(inFlight);
        } else if (status == 429) {
            const int retryAfter = answer->rawHeader("Retry-After").toInt();
            delay(retryAfter > 0 ? std::min(retryAfter, kLastRetrySeconds) : kFirstRetrySeconds);
        } else {
            ++failures;
            delay(std::min(kLastRetrySeconds, kFirstRetrySeconds << std::min(failures - 1, 6)));
        }
        inFlight = 0;
        reply = nullptr;
        Q_EMIT sendingDone();
        if (status >= 200 && status < 300 && !queue.isEmpty()) {
            send();
        }
    });
}

void Reporter::sent(int count) {
    for (int i = 0; i < count && !queue.isEmpty(); ++i) {
        queue.removeFirst();
    }
    blockedUntil = {};
    saveQueue();
}

void Reporter::delay(int seconds) {
    blockedUntil = QDateTime::currentDateTimeUtc().addSecs(seconds);
}

void Reporter::forget() {
    if (forgotten) {
        return;
    }
    forgotten = true;
    sendTimer.stop();
    markerTimer.stop();
    if (reply) {
        reply->abort();
    }
    queue.clear();
    if (begun) {
        UninstallCrashHandler();
        RemoveSession(config.directory, sessionId);
        lock->unlock();
    }
}

void Reporter::ForgetAll(const QString& directory) {
    for (const QString& session : SessionsIn(directory)) {
        if (!IsRunning(QDir(directory).filePath(session + QStringLiteral(".lock")))) {
            RemoveSession(directory, session);
        }
    }
}

}  // namespace Telemetry
