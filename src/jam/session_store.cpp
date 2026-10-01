#include "jam/session_store.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QSaveFile>
#include <QtGlobal>

#include <utility>

namespace Jam {

QByteArray EncodeSession(const Session& session) {
    QJsonObject json{
        {QStringLiteral("version"), kSessionVersion},
        {QStringLiteral("roomId"), session.roomId},
        {QStringLiteral("hostSecret"), session.hostSecret},
        {QStringLiteral("joinUrl"), session.joinUrl},
        {QStringLiteral("outbox"), QJsonArray::fromStringList(session.outbox)},
    };
    if (session.snapshot) {
        json.insert(QStringLiteral("snapshot"), *session.snapshot);
    }
    return QJsonDocument(json).toJson(QJsonDocument::Compact);
}

std::optional<Session> DecodeSession(QByteArrayView text) {
    const QJsonDocument document = QJsonDocument::fromJson(text.toByteArray());
    if (!document.isObject()) {
        return std::nullopt;
    }
    const QJsonObject json = document.object();
    const QJsonValue version = json.value(QStringLiteral("version"));
    if (!version.isDouble() || version.toDouble() != kSessionVersion) {
        return std::nullopt;
    }
    const QJsonValue roomId = json.value(QStringLiteral("roomId"));
    const QJsonValue hostSecret = json.value(QStringLiteral("hostSecret"));
    const QJsonValue joinUrl = json.value(QStringLiteral("joinUrl"));
    if (!roomId.isString() || !hostSecret.isString() || !joinUrl.isString()) {
        return std::nullopt;
    }
    Session session;
    session.roomId = roomId.toString();
    session.hostSecret = hostSecret.toString();
    session.joinUrl = joinUrl.toString();
    const QJsonValue snapshot = json.value(QStringLiteral("snapshot"));
    if (snapshot.isObject()) {
        session.snapshot = snapshot.toObject();
    }
    for (const QJsonValue& itemId : json.value(QStringLiteral("outbox")).toArray()) {
        if (itemId.isString()) {
            session.outbox.append(itemId.toString());
        }
    }
    return session;
}

SessionStore::SessionStore(QString path)
    : filePath(std::move(path)) { }

std::optional<Session> SessionStore::load() const {
    QFile file(filePath);
    if (!file.exists()) {
        return std::nullopt;
    }
    if (file.size() > kMaxSessionFileBytes) {
        qWarning(
            "jam: %s is %lld bytes, over %lld; ignored", qUtf8Printable(filePath),
            static_cast<long long>(file.size()), static_cast<long long>(kMaxSessionFileBytes)
        );
        return std::nullopt;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    return DecodeSession(file.read(kMaxSessionFileBytes));
}

bool SessionStore::save(const Session& session) const {
    QDir().mkpath(QFileInfo(filePath).absolutePath());
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning(
            "jam: cannot write %s: %s", qUtf8Printable(filePath), qUtf8Printable(file.errorString())
        );
        return false;
    }
    file.write(EncodeSession(session));
    if (!file.commit()) {
        qWarning(
            "jam: cannot write %s: %s", qUtf8Printable(filePath), qUtf8Printable(file.errorString())
        );
        return false;
    }
    return true;
}

void SessionStore::clear() const {
    QFile::remove(filePath);
}

}  // namespace Jam
