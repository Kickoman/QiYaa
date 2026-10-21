#pragma once

#include "core/lyrics.h"
#include "core/lyrics_finder.h"
#include "core/track.h"

#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <optional>

class QNetworkAccessManager;
class QNetworkReply;

namespace Lrclib {

inline const QString kBaseUrl = QStringLiteral("https://lrclib.net");

class Service : public Core::LyricsService {
    Q_OBJECT
public:
    Service(QNetworkAccessManager* network, QString baseUrl, const QString& appVersion, QObject* parent = nullptr);
    ~Service() override;

    void lyrics(const Core::Track& track, Core::TLyricsDone done) override;
    void cancel() override;
    void setRetryDelayMs(int ms) { retryDelayMs = ms; }

private:
    struct Lookup {
        Core::TLyricsDone done;
        QString artist;
        QStringList titles;
        qint64 durationMs = 0;
        bool retried = false;
        bool serverError = false;
    };

    void search();
    void searched(QNetworkReply* finished);
    void nextTitle();
    void answer(Core::LyricsAnswer result);

    QNetworkAccessManager* network;
    QString baseUrl;
    QByteArray userAgent;
    int retryDelayMs = 1000;
    quint64 generation = 0;
    std::optional<Lookup> lookup;
    QPointer<QNetworkReply> reply;
    bool tooLarge = false;
};

}  // namespace Lrclib
