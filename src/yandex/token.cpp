#include "yandex/token.h"

#include <QByteArray>
#include <QFile>
#include <QFileDevice>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>

namespace Yandex {

namespace {
constexpr qint64 kMaxTokenFileBytes = 64 * 1024;
}  // namespace

QString NormalizeToken(const QByteArray& raw) {
    QString text = QString::fromUtf8(raw).trimmed();
    if (text.isEmpty()) {
        return {};
    }

    if (text.startsWith(u'{') || text.startsWith(u'"')) {
        const QJsonDocument document = QJsonDocument::fromJson(("[" + text + "]").toUtf8());
        if (document.isArray() && !document.array().isEmpty()) {
            const QJsonValue value = document.array().first();
            if (value.isString()) {
                text = value.toString().trimmed();
            } else if (value.isObject()) {
                text = value.toObject().value(QStringLiteral("access_token")).toString().trimmed();
            }
        }
    }

    static const QRegularExpression accessTokenPattern(QStringLiteral("access_token=([^&#\\s]+)"));
    if (const auto match = accessTokenPattern.match(text); match.hasMatch()) {
        text = match.captured(1);
    }

    if (text.startsWith(QLatin1String("OAuth "), Qt::CaseInsensitive)) {
        text = text.mid(6).trimmed();
    }

    static const QRegularExpression tokenPattern(QStringLiteral("^[A-Za-z0-9._\\-]{10,}$"));
    return tokenPattern.match(text).hasMatch() ? text : QString();
}

TokenSource FindToken(const QString& tokenFile, const QStringList& importFiles) {
    if (const QString environmentToken = NormalizeToken(qgetenv("QIYAA_TOKEN"));
        !environmentToken.isEmpty()) {
        return {environmentToken, QStringLiteral("environment variable QIYAA_TOKEN")};
    }

    // Our own file wins, even when empty (= the user logged out).
    if (QFile file(tokenFile); file.open(QIODevice::ReadOnly)) {
        const QString token = NormalizeToken(file.read(kMaxTokenFileBytes));
        return token.isEmpty() ? TokenSource{} : TokenSource{token, tokenFile};
    }

    for (const QString& path : importFiles) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }
        if (const QString token = NormalizeToken(file.read(kMaxTokenFileBytes)); !token.isEmpty()) {
            return {token, path};
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
