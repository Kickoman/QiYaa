#include "yandex/Token.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrlQuery>

#include "app/Paths.h"

namespace qiyaa::yandex {

QString normalizeToken(const QByteArray& raw) {
    QString s = QString::fromUtf8(raw).trimmed();
    if (s.isEmpty()) return {};

    if (s.startsWith(u'{') || s.startsWith(u'"')) {
        const QJsonDocument doc = QJsonDocument::fromJson(("[" + s + "]").toUtf8());
        if (doc.isArray() && !doc.array().isEmpty()) {
            const QJsonValue v = doc.array().first();
            if (v.isString()) s = v.toString().trimmed();
            else if (v.isObject()) s = v.toObject().value(QStringLiteral("access_token")).toString().trimmed();
        }
    }

    static const QRegularExpression frag(QStringLiteral("access_token=([^&#\\s]+)"));
    if (const auto m = frag.match(s); m.hasMatch()) s = m.captured(1);

    if (s.startsWith(QLatin1String("OAuth "), Qt::CaseInsensitive)) s = s.mid(6).trimmed();

    // Tokens are URL-safe ASCII; anything else means we parsed garbage.
    static const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9._\\-]{10,}$"));
    return valid.match(s).hasMatch() ? s : QString();
}

TokenSource findToken() {
    if (const QString env = normalizeToken(qgetenv("QIYAA_TOKEN")); !env.isEmpty())
        return {env, QStringLiteral("environment variable QIYAA_TOKEN")};

    // Our own file wins, even when empty (= the user logged out).
    const QString own = paths::configDir() + QStringLiteral("/token");
    if (QFile f(own); f.open(QIODevice::ReadOnly)) {
        const QString t = normalizeToken(f.read(64 * 1024));
        return t.isEmpty() ? TokenSource{} : TokenSource{t, own};
    }

    for (const QString& dir : paths::yaampDataDirs()) {
        const QString path = dir + QStringLiteral("/token.json");
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) continue;
        if (const QString t = normalizeToken(f.read(64 * 1024)); !t.isEmpty()) return {t, path};
    }
    return {};
}

void forgetToken() {
    QFile f(paths::configDir() + QStringLiteral("/token"));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

bool saveToken(const QString& token) {
    QFile f(paths::configDir() + QStringLiteral("/token"));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return f.write(token.toUtf8()) > 0;
}

}  // namespace qiyaa::yandex
