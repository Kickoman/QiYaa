#include "spotify/access_point.h"
#include "spotify/audio_file.h"
#include "spotify/ids.h"
#include "spotify/login.h"
#include "spotify/metadata.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QSysInfo>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include <functional>
#include <optional>
#include <variant>
#include <vector>

namespace {

const QString kKeymasterClientId = QStringLiteral("65b708073fc0480ea92a077233ca87bd");
const QString kClientVersion = QStringLiteral("1.2.52.442");
const QString kSystemInformation = QStringLiteral("QiYaa " QIYAA_VERSION);
const QString kVersionString = QStringLiteral("QiYaa " QIYAA_VERSION);
const QByteArray kUserAgent = "Spotify/124200290 Linux/0 (QiYaa " QIYAA_VERSION ")";
constexpr int kHttpTimeoutMs = 20000;
constexpr qint64 kFirstChunk = 128 * 1024;

struct HttpReply {
    int status = 0;
    QByteArray body;
    QByteArray contentRange;
    QString error;
};

class Spike {
public:
    Spike(QString credentials, QString output, QString track)
        : credentialsPath(std::move(credentials))
        , outputDir(std::move(output))
        , trackId(std::move(track)) { }

    int run();

private:
    QString loadDeviceId() const;
    bool loadCredentials();
    bool resolveAccessPoints();
    bool loginToAccessPoint();
    bool connectTo(const QString& hostPort);
    bool waitFor(const std::function<bool()>& done, int timeoutMs);
    void pump();
    bool fetchClientToken();
    bool loginFive();
    std::optional<Spotify::TrackInfo> fetchTrack();
    std::optional<Spotify::TAesKey> fetchKey(const Spotify::PlayableFile& playable);
    std::optional<Spotify::StorageUrls> resolveStorage(const Spotify::TFileId& file);
    std::optional<QByteArray> download(const Spotify::StorageUrls& storage);
    HttpReply send(
        const QString& method,
        const QUrl& url,
        const QList<QPair<QByteArray, QByteArray>>& headers,
        const QByteArray& body
    );
    QUrl spclientUrl(const QString& path) const;

    QString credentialsPath;
    QString outputDir;
    QString trackId;
    QNetworkAccessManager network;
    QTcpSocket socket;
    std::optional<Spotify::AccessPoint> accessPoint;
    std::vector<Spotify::TApEvent> events;
    Spotify::StoredCredentials stored;
    Spotify::StoredCredentials reusable;
    QStringList accessPoints;
    QStringList spclients;
    QString country;
    QString deviceId;
    QString clientToken;
    QString bearer;
    bool loggedIn = false;
};

void FillRandom(std::uint8_t* data, std::size_t size) {
    for (std::size_t i = 0; i < size; ++i) {
        data[i] = static_cast<std::uint8_t>(QRandomGenerator::system()->generate());
    }
}

QByteArray ToQt(const Spotify::TBytes& bytes) {
    return QByteArray(reinterpret_cast<const char*>(bytes.data()), static_cast<qsizetype>(bytes.size()));
}

Spotify::ByteView View(const QByteArray& bytes) {
    return {reinterpret_cast<const std::uint8_t*>(bytes.constData()), static_cast<std::size_t>(bytes.size())};
}

int Run(QString credentialsPath, QString outputDir, QString trackId) {
    Spike spike(std::move(credentialsPath), std::move(outputDir), std::move(trackId));
    return spike.run();
}

int Spike::run() {
    deviceId = loadDeviceId();
    QObject::connect(&socket, &QTcpSocket::readyRead, [this] { pump(); });
    if (!loadCredentials() || !resolveAccessPoints() || !loginToAccessPoint()) {
        return 1;
    }
    if (!fetchClientToken() || !loginFive()) {
        return 1;
    }
    const std::optional<Spotify::TrackInfo> track = fetchTrack();
    if (!track) {
        return 1;
    }
    const std::optional<Spotify::PlayableFile> playable = Spotify::PickOggVorbis(*track, 160);
    if (!playable) {
        qWarning() << "no Ogg Vorbis file in the metadata";
        return 1;
    }
    qInfo().noquote(
    ) << "file"
      << QString::fromStdString(Spotify::ToHex(playable->file.id)) << "format" << playable->file.format;
    const std::optional<Spotify::TAesKey> key = fetchKey(*playable);
    if (!key) {
        return 1;
    }
    const std::optional<Spotify::StorageUrls> storage = resolveStorage(playable->file.id);
    if (!storage) {
        return 1;
    }
    std::optional<QByteArray> encrypted = download(*storage);
    if (!encrypted) {
        return 1;
    }
    const Spotify::AudioDecryptor decryptor(*key);
    QByteArray whole = *encrypted;
    decryptor.apply(reinterpret_cast<std::uint8_t*>(whole.data()), static_cast<std::size_t>(whole.size()), 0);
    QByteArray piece = encrypted->mid(1000, 777);
    decryptor.apply(reinterpret_cast<std::uint8_t*>(piece.data()), static_cast<std::size_t>(piece.size()), 1000);
    qInfo() << "decrypting at an unaligned offset matches:" << (piece == whole.mid(1000, 777));
    qInfo(
    ) << "file starts with OggS:"
      << whole.startsWith("OggS")
      << "and again after the Spotify header:" << (whole.mid(Spotify::kOggHeaderSize, 4) == "OggS");
    if (const std::optional<Spotify::Normalisation> gain = Spotify::ParseNormalisation(View(whole.left(160)))) {
        qInfo(
        ) << "normalisation: track gain"
          << gain->trackGainDb << "dB, peak" << gain->trackPeak << "; album gain" << gain->albumGainDb << "dB, peak"
          << gain->albumPeak;
    }
    QDir().mkpath(outputDir);
    QFile out(QDir(outputDir).filePath(QStringLiteral("quo-vadis.ogg")));
    if (!out.open(QIODevice::WriteOnly)) {
        qWarning() << "cannot write" << out.fileName();
        return 1;
    }
    out.write(whole.mid(Spotify::kOggHeaderSize));
    qInfo().noquote() << "wrote" << out.fileName() << out.size() << "bytes";
    socket.disconnectFromHost();
    return 0;
}

QString Spike::loadDeviceId() const {
    QDir().mkpath(outputDir);
    QFile file(QDir(outputDir).filePath(QStringLiteral("device-id")));
    if (file.open(QIODevice::ReadOnly)) {
        const QByteArray saved = file.readAll().trimmed();
        if (saved.size() == 40) {
            return QString::fromLatin1(saved);
        }
        file.close();
    }
    QByteArray random(20, '\0');
    FillRandom(reinterpret_cast<std::uint8_t*>(random.data()), static_cast<std::size_t>(random.size()));
    const QByteArray id = random.toHex();
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(id);
    }
    return QString::fromLatin1(id);
}

bool Spike::loadCredentials() {
    QFile file(credentialsPath);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 64 * 1024) {
        qWarning() << "cannot read the credentials file";
        return false;
    }
    const QJsonObject json = QJsonDocument::fromJson(file.readAll()).object();
    const std::optional<Spotify::TBytes> data = Spotify::FromBase64(json.value("auth_data").toString().toStdString());
    if (!data || json.value("username").toString().isEmpty()) {
        qWarning() << "the credentials file has no username or data";
        return false;
    }
    stored.username = json.value("username").toString().toStdString();
    stored.authType = static_cast<std::uint32_t>(json.value("auth_type").toInt());
    stored.data = *data;
    qInfo() << "credentials: type" << stored.authType << "," << stored.data.size() << "bytes";
    return true;
}

bool Spike::resolveAccessPoints() {
    const QUrl url("https://apresolve.spotify.com/?type=accesspoint&type=dealer&type=spclient");
    const HttpReply reply = send("GET", url, {}, {});
    const QJsonObject json = QJsonDocument::fromJson(reply.body).object();
    for (const QJsonValue& value : json.value("accesspoint").toArray()) {
        accessPoints << value.toString();
    }
    for (const QJsonValue& value : json.value("spclient").toArray()) {
        spclients << value.toString();
    }
    if (accessPoints.isEmpty()) {
        accessPoints << "ap.spotify.com:443";
    }
    if (spclients.isEmpty()) {
        spclients << "spclient.wg.spotify.com:443";
    }
    qInfo() << "access points:" << accessPoints.mid(0, 3) << "spclient:" << spclients.first();
    return true;
}

bool Spike::loginToAccessPoint() {
    for (int attempt = 0; attempt < 3 && attempt < accessPoints.size(); ++attempt) {
        if (connectTo(accessPoints.at(attempt))) {
            return true;
        }
        QThread::sleep(2);
    }
    qWarning() << "could not log in to any access point";
    return false;
}

bool Spike::connectTo(const QString& hostPort) {
    const QString host = hostPort.section(':', 0, 0);
    const quint16 port = static_cast<quint16>(hostPort.section(':', 1, 1).toUInt());
    socket.abort();
    events.clear();
    loggedIn = false;
    QElapsedTimer timer;
    timer.start();
    socket.connectToHost(host, port);
    if (!socket.waitForConnected(5000)) {
        qWarning() << "connect to" << hostPort << "failed:" << socket.errorString();
        return false;
    }
    Spotify::ClientIdentity identity;
    identity.systemInformation = kSystemInformation.toStdString();
    identity.versionString = kVersionString.toStdString();
    identity.deviceId = deviceId.toStdString();
    accessPoint.emplace(identity, stored, FillRandom);
    accessPoint->start();
    pump();
    std::optional<QString> failure;
    const bool done = waitFor(
        [&] {
            for (const Spotify::TApEvent& event : events) {
                if (const auto* welcome = std::get_if<Spotify::ApLoggedIn>(&event)) {
                    reusable = welcome->reusable;
                    loggedIn = true;
                } else if (const auto* failed = std::get_if<Spotify::ApLoginFailed>(&event)) {
                    failure =
                        QStringLiteral("login failed, code %1 %2")
                            .arg(failed->code)
                            .arg(QString::fromStdString(failed->description));
                } else if (const auto* broken = std::get_if<Spotify::ApFailure>(&event)) {
                    failure = QString::fromStdString(broken->reason);
                }
            }
            return loggedIn || failure.has_value();
        },
        10000
    );
    if (!done || failure) {
        qWarning().noquote() << "access point" << hostPort << ":" << failure.value_or("timeout");
        return false;
    }
    qInfo(
    ) << "logged in to"
      << hostPort << "in" << timer.elapsed() << "ms; reusable credentials type" << reusable.authType << ","
      << reusable.data.size() << "bytes";
    waitFor([this] { return !country.isEmpty(); }, 3000);
    qInfo() << "country:" << country;
    return true;
}

void Spike::pump() {
    if (!accessPoint) {
        return;
    }
    const QByteArray data = socket.readAll();
    if (!data.isEmpty()) {
        accessPoint->receive(View(data));
    }
    for (Spotify::TApEvent& event : accessPoint->takeEvents()) {
        if (const auto* code = std::get_if<Spotify::ApCountry>(&event)) {
            country = QString::fromStdString(code->code);
        } else if (std::holds_alternative<Spotify::ApPing>(event)) {
            QTimer::singleShot(60000, [this] {
                accessPoint->pong();
                pump();
            });
        }
        events.push_back(std::move(event));
    }
    const Spotify::TBytes outgoing = accessPoint->takeOutgoing();
    if (!outgoing.empty()) {
        socket.write(ToQt(outgoing));
    }
}

bool Spike::waitFor(const std::function<bool()>& done, int timeoutMs) {
    QElapsedTimer timer;
    timer.start();
    while (!done()) {
        if (timer.elapsed() > timeoutMs) {
            return false;
        }
        QEventLoop loop;
        QTimer::singleShot(50, &loop, &QEventLoop::quit);
        loop.exec();
    }
    return true;
}

bool Spike::fetchClientToken() {
    Spotify::ClientTokenParams params;
    params.clientVersion = kClientVersion.toStdString();
    params.clientId = kKeymasterClientId.toStdString();
    params.deviceId = deviceId.toStdString();
    params.linuxSystem.release = QSysInfo::kernelVersion().toStdString();
    params.linuxSystem.version = QSysInfo::productVersion().toStdString();
    params.linuxSystem.hardware = QSysInfo::currentCpuArchitecture() == "x86_64" ? "x86_64" : "x86";
    const QUrl url("https://clienttoken.spotify.com/v1/clienttoken");
    const QList<QPair<QByteArray, QByteArray>> headers = {
        {"Accept", "application/x-protobuf"},
        {"Content-Type", "application/x-protobuf"},
    };
    HttpReply reply = send("POST", url, headers, ToQt(Spotify::ClientTokenRequest(params)));
    for (int round = 0; round < 3; ++round) {
        if (reply.status != 200) {
            qWarning() << "client token: HTTP" << reply.status << reply.error;
            return false;
        }
        const Spotify::TClientTokenReply parsed = Spotify::ParseClientToken(View(reply.body));
        if (const auto* granted = std::get_if<Spotify::ClientTokenGranted>(&parsed)) {
            clientToken = QString::fromStdString(granted->token);
            qInfo(
            ) << "client token granted,"
              << clientToken.size() << "chars, refresh after" << granted->refreshAfterSeconds
              << "s; challenge rounds:" << round;
            return true;
        }
        const auto* challenge = std::get_if<Spotify::ClientTokenChallenge>(&parsed);
        if (!challenge || !challenge->hashcash) {
            qWarning() << "client token: unexpected reply";
            return false;
        }
        const std::optional<Spotify::HashcashSolution> solution = Spotify::SolveHashcash({}, *challenge->hashcash);
        if (!solution) {
            qWarning() << "client token: hashcash not solved";
            return false;
        }
        qInfo(
        ) << "client token: hashcash of length"
          << challenge->hashcash->length << "solved in" << solution->took.count() / 1000 << "us";
        reply = send("POST", url, headers, ToQt(Spotify::ClientTokenAnswer(challenge->state, *solution)));
    }
    return false;
}

bool Spike::loginFive() {
    const QUrl url("https://login5.spotify.com/v3/login");
    const QList<QPair<QByteArray, QByteArray>> headers = {
        {"Accept", "application/x-protobuf"},
        {"Content-Type", "application/x-protobuf"},
        {"client-token", clientToken.toLatin1()},
    };
    Spotify::TBytes context;
    std::optional<Spotify::HashcashSolution> solution;
    for (int round = 0; round < 3; ++round) {
        const Spotify::TBytes body = Spotify::Login5Request(
            kKeymasterClientId.toStdString(), deviceId.toStdString(), reusable, context, solution
        );
        const HttpReply reply = send("POST", url, headers, ToQt(body));
        if (reply.status != 200) {
            qWarning() << "login5: HTTP" << reply.status << reply.error;
            return false;
        }
        const Spotify::TLogin5Reply parsed = Spotify::ParseLogin5(View(reply.body));
        if (const auto* ok = std::get_if<Spotify::Login5Ok>(&parsed)) {
            bearer = QString::fromStdString(ok->accessToken);
            qInfo(
            ) << "login5: bearer token,"
              << bearer.size() << "chars, expires in" << ok->expiresInSeconds << "s; challenge rounds:" << round;
            return true;
        }
        if (const auto* error = std::get_if<Spotify::Login5Error>(&parsed)) {
            qWarning() << "login5: error code" << error->code;
            return false;
        }
        const auto* challenge = std::get_if<Spotify::Login5Challenge>(&parsed);
        if (!challenge) {
            qWarning() << "login5:" << QString::fromStdString(std::get<Spotify::WebFailure>(parsed).reason);
            return false;
        }
        context = challenge->context;
        solution = Spotify::SolveHashcash(context, challenge->hashcash);
        if (!solution) {
            qWarning() << "login5: hashcash not solved";
            return false;
        }
        qInfo(
        ) << "login5: hashcash of length"
          << challenge->hashcash.length << "solved in" << solution->took.count() / 1000 << "us";
    }
    return false;
}

QUrl Spike::spclientUrl(const QString& path) const {
    const QString salt = QString::number(QRandomGenerator::global()->generate());
    return QUrl(QStringLiteral("https://%1%2?product=0&country=%3&salt=%4").arg(spclients.first(), path, country, salt)
    );
}

std::optional<Spotify::TrackInfo> Spike::fetchTrack() {
    const std::string uri = "spotify:track:" + trackId.toStdString();
    const QList<QPair<QByteArray, QByteArray>> headers = {
        {"Authorization", "Bearer " + bearer.toLatin1()},
        {"client-token", clientToken.toLatin1()},
        {"Content-Type", "application/x-protobuf"},
    };
    const QUrl url = spclientUrl("/extended-metadata/v0/extended-metadata");
    const HttpReply reply = send("POST", url, headers, ToQt(Spotify::TrackMetadataRequest(uri)));
    if (reply.status != 200) {
        qWarning() << "extended-metadata: HTTP" << reply.status << reply.error;
        return std::nullopt;
    }
    std::optional<Spotify::TrackInfo> track = Spotify::ParseTrackMetadata(View(reply.body));
    if (!track) {
        qWarning() << "extended-metadata: no track in" << reply.body.size() << "bytes";
        return std::nullopt;
    }
    QStringList formats;
    for (const Spotify::AudioFileRef& file : track->files) {
        formats << QString::number(file.format);
    }
    qInfo().noquote(
    ) << "track"
      << QString::fromStdString(track->name) << "," << track->durationMs << "ms, gid"
      << QString::fromStdString(Spotify::Base62FromGid(track->gid)) << ", formats" << formats.join(',')
      << ", alternatives" << track->alternatives.size();
    return track;
}

std::optional<Spotify::TAesKey> Spike::fetchKey(const Spotify::PlayableFile& playable) {
    for (int attempt = 0; attempt < 3; ++attempt) {
        events.clear();
        const std::optional<std::uint32_t> sequence = accessPoint->requestAudioKey(playable.track, playable.file.id);
        pump();
        if (!sequence) {
            qWarning() << "audio key: the access point is not ready";
            return std::nullopt;
        }
        std::optional<Spotify::TAesKey> key;
        std::optional<std::uint16_t> refused;
        waitFor(
            [&] {
                for (const Spotify::TApEvent& event : events) {
                    if (const auto* got = std::get_if<Spotify::ApAudioKey>(&event); got && got->sequence == *sequence) {
                        key = got->key;
                    } else if (const auto* no = std::get_if<Spotify::ApAudioKeyRefused>(&event);
                               no && no->sequence == *sequence) {
                        refused = no->code;
                    }
                }
                return key.has_value() || refused.has_value();
            },
            1500
        );
        if (key) {
            qInfo() << "audio key received for sequence" << *sequence;
            return key;
        }
        if (refused) {
            qWarning() << "audio key refused, code" << Qt::hex << *refused;
            if (*refused != 2) {
                return std::nullopt;
            }
        } else {
            qWarning() << "audio key: no answer in 1.5 s";
        }
        QThread::sleep(static_cast<unsigned long>(attempt + 1));
    }
    return std::nullopt;
}

std::optional<Spotify::StorageUrls> Spike::resolveStorage(const Spotify::TFileId& file) {
    const QList<QPair<QByteArray, QByteArray>> headers = {
        {"Authorization", "Bearer " + bearer.toLatin1()},
        {"client-token", clientToken.toLatin1()},
    };
    const QString path = "/storage-resolve/files/audio/interactive/" + QString::fromStdString(Spotify::ToHex(file));
    const HttpReply reply = send("GET", spclientUrl(path), headers, {});
    if (reply.status != 200) {
        qWarning() << "storage-resolve: HTTP" << reply.status << reply.error;
        return std::nullopt;
    }
    std::optional<Spotify::StorageUrls> storage = Spotify::ParseStorageResolve(View(reply.body));
    if (!storage || storage->urls.empty() || storage->result != Spotify::StorageResult::Cdn) {
        qWarning() << "storage-resolve: no CDN urls";
        return std::nullopt;
    }
    QStringList hosts;
    for (const std::string& url : storage->urls) {
        hosts << QUrl(QString::fromStdString(url)).host();
    }
    qInfo() << "CDN hosts:" << hosts;
    return storage;
}

std::optional<QByteArray> Spike::download(const Spotify::StorageUrls& storage) {
    for (const std::string& candidate : storage.urls) {
        const QUrl url(QString::fromStdString(candidate));
        const HttpReply first = send("GET", url, {{"Range", "bytes=0-" + QByteArray::number(kFirstChunk - 1)}}, {});
        if (first.status != 206 && first.status != 200) {
            qWarning() << "CDN" << url.host() << "HTTP" << first.status << first.error;
            continue;
        }
        if (first.status == 200) {
            return first.body;
        }
        const qint64 total = QString::fromLatin1(first.contentRange).section('/', 1, 1).toLongLong();
        qInfo() << "CDN" << url.host() << ": 206, file size" << total;
        if (total <= first.body.size()) {
            return first.body;
        }
        const QByteArray range = "bytes=" + QByteArray::number(first.body.size()) + "-" + QByteArray::number(total - 1);
        const HttpReply rest = send("GET", url, {{"Range", range}}, {});
        if (rest.status != 206) {
            qWarning() << "CDN" << url.host() << "second range: HTTP" << rest.status << rest.error;
            continue;
        }
        return first.body + rest.body;
    }
    return std::nullopt;
}

HttpReply Spike::send(
    const QString& method,
    const QUrl& url,
    const QList<QPair<QByteArray, QByteArray>>& headers,
    const QByteArray& body
) {
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", kUserAgent);
    for (const auto& [name, value] : headers) {
        request.setRawHeader(name, value);
    }
    QNetworkReply* reply = method == "POST" ? network.post(request, body) : network.get(request);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(kHttpTimeoutMs, &loop, &QEventLoop::quit);
    loop.exec();
    HttpReply result;
    if (!reply->isFinished()) {
        reply->abort();
        result.error = "timeout";
    }
    result.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    result.body = reply->readAll();
    result.contentRange = reply->rawHeader("Content-Range");
    if (reply->error() != QNetworkReply::NoError && result.error.isEmpty()) {
        result.error = reply->errorString();
    }
    reply->deleteLater();
    return result;
}

}  // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.size() < 3) {
        qWarning() << "usage: spotify_spike <credentials.json> <output dir> [track id]";
        return 2;
    }
    const QString track = args.size() > 3 ? args.at(3) : QStringLiteral("6PCoZdjSPbRMehHviTrpB3");
    return Run(args.at(1), args.at(2), track);
}
