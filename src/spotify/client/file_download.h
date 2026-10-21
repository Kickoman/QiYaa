#pragma once

#include "spotify/protocol/audio_file.h"
#include "spotify/protocol/ids.h"

#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <memory>
#include <optional>

class QNetworkAccessManager;
class QNetworkReply;

namespace SpotifyClient {

class Session;
class Web;

struct DownloadFailure {
    enum class Kind { Network, Track, KeyRefused };
    Kind kind = Kind::Track;
    QString text;
};

class FileDownload : public QObject {
    Q_OBJECT
public:
    FileDownload(
        Web* web,
        Session* session,
        QNetworkAccessManager* networkAccessManager,
        const SpotifyProtocol::TGid& track,
        int preferredKbps,
        QObject* parent = nullptr
    );
    ~FileDownload() override;

    void prepare();
    void start();
    void cancel();

    int kbps() const { return chosenKbps; }
    std::optional<SpotifyProtocol::Normalisation> normalisation() const { return loudness; }

Q_SIGNALS:
    void prepared();
    void received(const QByteArray& bytes);
    void completed();
    void failed(const SpotifyClient::DownloadFailure& failure);

private:
    void resolveStorage();
    void fetchFrom(int index);
    void deliver(QByteArray bytes);
    void fail(DownloadFailure::Kind kind, const QString& text);

    Web* web;
    Session* session;
    QNetworkAccessManager* networkManager;
    SpotifyProtocol::TGid track;
    int preferredKbps;
    SpotifyProtocol::TFileId file{};
    int chosenKbps = 0;
    std::optional<SpotifyProtocol::TAesKey> key;
    std::unique_ptr<SpotifyProtocol::AudioDecryptor> decryptor;
    QStringList urls;
    int urlIndex = 0;
    qint64 fileOffset = 0;
    qint64 skipInReply = 0;
    qint64 fileSize = -1;
    QByteArray head;
    std::optional<SpotifyProtocol::Normalisation> loudness;
    QPointer<QNetworkReply> reply;
    bool done = false;
};

}  // namespace SpotifyClient
