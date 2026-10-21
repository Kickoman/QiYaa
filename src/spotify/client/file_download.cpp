#include "spotify/client/file_download.h"

#include "spotify/client/session.h"
#include "spotify/client/web.h"
#include "spotify/protocol/bytes.h"
#include "spotify/protocol/metadata.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include <algorithm>
#include <string>

namespace SpotifyClient {

namespace {

const QByteArray kProtobuf = "application/x-protobuf";

SpotifyProtocol::ByteView View(const QByteArray& bytes) {
    return {reinterpret_cast<const std::uint8_t*>(bytes.constData()), static_cast<std::size_t>(bytes.size())};
}

int KbpsOf(std::uint32_t format) {
    switch (static_cast<SpotifyProtocol::AudioFormat>(format)) {
        case SpotifyProtocol::AudioFormat::OggVorbis96: return 96;
        case SpotifyProtocol::AudioFormat::OggVorbis160: return 160;
        case SpotifyProtocol::AudioFormat::OggVorbis320: return 320;
        default: return 0;
    }
}

qint64 TotalOf(QNetworkReply* reply) {
    const QByteArray range = reply->rawHeader("Content-Range");
    const qsizetype slash = range.lastIndexOf('/');
    if (slash < 0) {
        return -1;
    }
    bool ok = false;
    const qint64 total = range.mid(slash + 1).toLongLong(&ok);
    return ok ? total : -1;
}

}  // namespace

FileDownload::FileDownload(
    Web* spotifyWeb,
    Session* spotifySession,
    QNetworkAccessManager* networkAccessManager,
    const SpotifyProtocol::TGid& trackId,
    int kbps,
    QObject* parent
)
    : QObject(parent)
    , web(spotifyWeb)
    , session(spotifySession)
    , networkManager(networkAccessManager)
    , track(trackId)
    , preferredKbps(kbps) { }

FileDownload::~FileDownload() {
    cancel();
}

void FileDownload::cancel() {
    done = true;
    if (reply) {
        reply->disconnect(this);
        reply->abort();
    }
}

void FileDownload::fail(DownloadFailure::Kind kind, const QString& text) {
    if (done) {
        return;
    }
    done = true;
    Q_EMIT failed(DownloadFailure{kind, text});
}

void FileDownload::prepare() {
    const std::string uri = "spotify:track:" + SpotifyProtocol::Base62FromGid(track);
    const SpotifyProtocol::TBytes request = SpotifyProtocol::TrackMetadataRequest(uri);
    const QByteArray body(reinterpret_cast<const char*>(request.data()), static_cast<qsizetype>(request.size()));
    QPointer<FileDownload> self(this);
    const WebRequest metadata{
        .method = "POST",
        .path = QStringLiteral("/extended-metadata/v0/extended-metadata"),
        .body = body,
        .contentType = kProtobuf,
    };
    web->spclient(metadata, [self](const WebReply& answer) {
        if (!self || self->done) {
            return;
        }
        if (!answer.ok()) {
            const DownloadFailure::Kind kind =
                answer.network ? DownloadFailure::Kind::Network : DownloadFailure::Kind::Track;
            self->fail(kind, answer.error.isEmpty() ? tr("Spotify: HTTP %1").arg(answer.status) : answer.error);
            return;
        }
        const std::optional<SpotifyProtocol::TrackInfo> info = SpotifyProtocol::ParseTrackMetadata(View(answer.body));
        const std::optional<SpotifyProtocol::PlayableFile> playable =
            info ? SpotifyProtocol::PickOggVorbis(*info, self->preferredKbps) : std::nullopt;
        if (!playable) {
            self->fail(DownloadFailure::Kind::Track, tr("Spotify has no file of this track to play"));
            return;
        }
        self->file = playable->file.id;
        self->chosenKbps = KbpsOf(playable->file.format);
        self->session->audioKey(playable->track, playable->file.id, [self](const KeyResult& result) {
            if (!self || self->done) {
                return;
            }
            if (!result.key) {
                switch (result.failure) {
                    case KeyFailure::Refused:
                        self->fail(
                            DownloadFailure::Kind::KeyRefused, tr("Spotify does not let this account play here")
                        );
                        return;
                    case KeyFailure::Network:
                        self->fail(DownloadFailure::Kind::Network, tr("no connection to Spotify"));
                        return;
                    case KeyFailure::Failed: break;
                }
                self->fail(DownloadFailure::Kind::Track, tr("Spotify did not give the key of this track"));
                return;
            }
            self->key = result.key;
            self->decryptor = std::make_unique<SpotifyProtocol::AudioDecryptor>(*result.key);
            self->resolveStorage();
        });
    });
}

void FileDownload::resolveStorage() {
    const QString path = QStringLiteral("/storage-resolve/files/audio/interactive/")
        + QString::fromStdString(SpotifyProtocol::ToHex(file));
    QPointer<FileDownload> self(this);
    web->spclient(WebRequest{.path = path}, [self](const WebReply& answer) {
        if (!self || self->done) {
            return;
        }
        if (!answer.ok()) {
            const DownloadFailure::Kind kind =
                answer.network ? DownloadFailure::Kind::Network : DownloadFailure::Kind::Track;
            self->fail(kind, answer.error.isEmpty() ? tr("Spotify: HTTP %1").arg(answer.status) : answer.error);
            return;
        }
        const std::optional<SpotifyProtocol::StorageUrls> storage =
            SpotifyProtocol::ParseStorageResolve(View(answer.body));
        if (!storage || storage->result != SpotifyProtocol::StorageResult::Cdn || storage->urls.empty()) {
            if (!storage) {
                self->web->report(WebProblem::Content);
            }
            self->fail(DownloadFailure::Kind::Track, tr("Spotify gave no link to this track's file"));
            return;
        }
        for (const std::string& url : storage->urls) {
            self->urls << QString::fromStdString(url);
        }
        Q_EMIT self->prepared();
    });
}

void FileDownload::start() {
    if (done || urls.isEmpty() || reply) {
        return;
    }
    fetchFrom(0);
}

void FileDownload::fetchFrom(int index) {
    urlIndex = index;
    QNetworkRequest request{QUrl(urls.at(index))};
    request.setRawHeader("Range", "bytes=" + QByteArray::number(fileOffset) + "-");
    request.setRawHeader("User-Agent", web->userAgent());
    request.setTransferTimeout(Web::kTimeoutMs);
    reply = networkManager->get(request);
    skipInReply = 0;
    QNetworkReply* current = reply;
    connect(current, &QNetworkReply::metaDataChanged, this, [this, current] {
        const int status = current->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 200) {
            skipInReply = fileOffset;
            fileSize = current->header(QNetworkRequest::ContentLengthHeader).toLongLong();
        } else if (status == 206) {
            fileSize = TotalOf(current);
        }
    });
    connect(current, &QNetworkReply::readyRead, this, [this, current] {
        const int status = current->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status != 200 && status != 206) {
            return;
        }
        QByteArray bytes = current->readAll();
        if (skipInReply > 0) {
            const qint64 dropped = std::min<qint64>(skipInReply, bytes.size());
            bytes.remove(0, dropped);
            skipInReply -= dropped;
        }
        deliver(std::move(bytes));
    });
    connect(current, &QNetworkReply::finished, this, [this, current] {
        current->deleteLater();
        if (done) {
            return;
        }
        const int status = current->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool answered = status == 200 || status == 206;
        if (answered && current->error() == QNetworkReply::NoError && (fileSize < 0 || fileOffset >= fileSize)) {
            done = true;
            Q_EMIT completed();
            return;
        }
        if (urlIndex + 1 < urls.size()) {
            fetchFrom(urlIndex + 1);
            return;
        }
        const bool network = status == 0 || (answered && current->error() != QNetworkReply::NoError);
        if (!answered && status > 0) {
            web->report(WebProblem::Http, status);
        }
        const QString text = current->errorString();
        fail(network ? DownloadFailure::Kind::Network : DownloadFailure::Kind::Track, text);
    });
}

void FileDownload::deliver(QByteArray bytes) {
    if (bytes.isEmpty()) {
        return;
    }
    decryptor->apply(
        reinterpret_cast<std::uint8_t*>(bytes.data()),
        static_cast<std::size_t>(bytes.size()),
        static_cast<std::uint64_t>(fileOffset)
    );
    const qint64 start = fileOffset;
    fileOffset += bytes.size();
    const auto header = static_cast<qint64>(SpotifyProtocol::kOggHeaderSize);
    if (start < header) {
        const qint64 inHeader = std::min<qint64>(header - start, bytes.size());
        head += bytes.left(inHeader);
        bytes.remove(0, inHeader);
        if (head.size() == header) {
            loudness = SpotifyProtocol::ParseNormalisation(View(head));
        }
    }
    if (!bytes.isEmpty()) {
        Q_EMIT received(bytes);
    }
}

}  // namespace SpotifyClient
