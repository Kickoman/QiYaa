#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <optional>

namespace Jam {

struct Session {
    QString roomId;
    QString hostSecret;
    QString joinUrl;
    std::optional<QJsonObject> snapshot;
    QStringList outbox;

    bool operator==(const Session&) const = default;
};

inline constexpr int kSessionVersion = 1;
inline constexpr qint64 kMaxSessionFileBytes = 4 * 1024 * 1024;

QByteArray EncodeSession(const Session& session);
std::optional<Session> DecodeSession(QByteArrayView text);

class SessionStore {
public:
    explicit SessionStore(QString filePath);

    const QString& path() const { return filePath; }
    std::optional<Session> load() const;
    bool save(const Session& session) const;
    void clear() const;

private:
    QString filePath;
};

}  // namespace Jam
