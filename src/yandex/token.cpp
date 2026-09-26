#include "yandex/token.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrlQuery>

namespace Yandex {

QString NormalizeToken(const QByteArray& raw) {
    QString s = QString::fromUtf8(raw).trimmed();
    if (s.isEmpty()) {
        return {};
    }

    if (s.startsWith(u'{') || s.startsWith(u'"')) {
        const QJsonDocument doc = QJsonDocument::fromJson(("[" + s + "]").toUtf8());
        if (doc.isArray() && !doc.array().isEmpty()) {
            const QJsonValue value = doc.array().first();
            if (value.isString()) {
                s = value.toString().trimmed();
            } else if (value.isObject()) {
                s = value.toObject().value(QStringLiteral("access_token")).toString().trimmed();
            }
        }
    }

    static const QRegularExpression frag(QStringLiteral("access_token=([^&#\\s]+)"));
    if (const auto m = frag.match(s); m.hasMatch()) {
        s = m.captured(1);
    }

    if (s.startsWith(QLatin1String("OAuth "), Qt::CaseInsensitive)) {
        s = s.mid(6).trimmed();
    }

    // Tokens are URL-safe ASCII; anything else means we parsed garbage.
    static const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9._\\-]{10,}$"));
    return valid.match(s).hasMatch() ? s : QString();
}

TokenSource FindToken(const QString& tokenFile, const QStringList& importFiles) {
    if (const QString env = NormalizeToken(qgetenv("QIYAA_TOKEN")); !env.isEmpty()) {
        return {env, QStringLiteral("environment variable QIYAA_TOKEN")};
    }

    // Our own file wins, even when empty (= the user logged out).
    if (QFile file(tokenFile); file.open(QIODevice::ReadOnly)) {
        const QString t = NormalizeToken(file.read(64 * 1024));
        return t.isEmpty() ? TokenSource{} : TokenSource{t, tokenFile};
    }

    for (const QString& path : importFiles) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }
        if (const QString t = NormalizeToken(file.read(64 * 1024)); !t.isEmpty()) {
            return {t, path};
        }
    }
    return {};
}

void ForgetToken(const QString& tokenFile) {
    QFile file(tokenFile);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }
}

bool SaveToken(const QString& tokenFile, const QString& token) {
    QFile file(tokenFile);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return file.write(token.toUtf8()) > 0;
}

}  // namespace Yandex
