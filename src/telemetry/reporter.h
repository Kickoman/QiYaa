#pragma once

#include <QDateTime>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QUrl>

#include <memory>

class QLockFile;
class QNetworkAccessManager;
class QNetworkReply;

namespace Telemetry {

inline constexpr int kMaxQueuedEvents = 500;
inline constexpr int kMaxBatchEvents = 50;
inline constexpr int kMaxBatchBytes = 60 * 1024;
inline constexpr int kMaxErrorsPerSession = 50;

class Reporter : public QObject {
    Q_OBJECT
public:
    struct Config {
        QUrl endpoint;
        QString directory;
        QString version;
        QString machine;
        QNetworkAccessManager* network = nullptr;
        int sendIntervalMs = 5 * 60 * 1000;
    };

    explicit Reporter(Config config, QObject* parent = nullptr);
    ~Reporter() override;

    void begin(const QJsonObject& startFields);
    void recordError(const QString& area, const QString& kind, int httpStatus = 0);
    void recordFeature(const QString& name, const QString& value);
    void countTrack();
    void countJam();
    void setPlaying(bool playing);
    void setMilkdropShown(bool shown);
    void finish();
    void send();
    void forget();

    bool isSending() const { return !reply.isNull(); }
    const QString& session() const { return sessionId; }
    int queued() const { return static_cast<int>(queue.size()); }

    static void ForgetAll(const QString& directory);

Q_SIGNALS:
    void sendingDone();

private:
    void record(const QString& type, QJsonObject fields);
    void reportEarlierRuns();
    void saveQueue() const;
    void saveMarker() const;
    void sent(int count);
    void delay(int seconds);
    QString file(const QString& session, const QString& suffix) const;
    qint64 playedMs() const;
    qint64 milkdropMs() const;

    Config config;
    QString sessionId;
    std::unique_ptr<QLockFile> lock;
    QList<QJsonObject> queue;
    QPointer<QNetworkReply> reply;
    int inFlight = 0;
    int failures = 0;
    QDateTime blockedUntil;
    QTimer sendTimer;
    QTimer markerTimer;
    QElapsedTimer running;
    QElapsedTimer playingSince;
    QElapsedTimer milkdropSince;
    qint64 playedBefore = 0;
    qint64 milkdropBefore = 0;
    bool playing = false;
    bool milkdropShown = false;
    int tracks = 0;
    int jams = 0;
    int errors = 0;
    int errorsDropped = 0;
    bool begun = false;
    bool finished = false;
    bool forgotten = false;
};

}  // namespace Telemetry
