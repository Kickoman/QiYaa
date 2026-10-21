#include "spotify/client/credentials_file.h"

#include <QByteArray>
#include <QFile>
#include <QFileDevice>
#include <QIODevice>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

namespace SpotifyClient {

namespace {

const QString kUsername = QStringLiteral("username");
const QString kType = QStringLiteral("type");
const QString kData = QStringLiteral("data");

}  // namespace

std::optional<SpotifyProtocol::Credentials> LoadCredentials(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    const QJsonObject json = QJsonDocument::fromJson(file.read(kMaxCredentialsFileBytes)).object();
    const QString username = json.value(kUsername).toString();
    const QByteArray data = QByteArray::fromBase64(json.value(kData).toString().toLatin1());
    const int type = json.value(kType).toInt(-1);
    if (username.isEmpty() || data.isEmpty()
        || type != static_cast<int>(SpotifyProtocol::AuthenticationType::StoredCredentials)) {
        return std::nullopt;
    }
    SpotifyProtocol::Credentials credentials;
    credentials.username = username.toStdString();
    credentials.type = SpotifyProtocol::AuthenticationType::StoredCredentials;
    credentials.data.assign(data.begin(), data.end());
    return credentials;
}

bool SaveCredentials(const QString& path, const SpotifyProtocol::Credentials& credentials) {
    const QByteArray data(
        reinterpret_cast<const char*>(credentials.data.data()), static_cast<qsizetype>(credentials.data.size())
    );
    const QJsonObject json{
        {kUsername, QString::fromStdString(credentials.username)},
        {kType, static_cast<int>(credentials.type)},
        {kData, QString::fromLatin1(data.toBase64())},
    };
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return file.write(QJsonDocument(json).toJson(QJsonDocument::Compact)) > 0;
}

void ForgetCredentials(const QString& path) {
    QFile::remove(path);
}

}  // namespace SpotifyClient
