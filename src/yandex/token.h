// OAuth token discovery for the prototype.
// Real login (device-code OAuth) and keychain storage come in stage 1.
#pragma once

#include <QString>
#include <QStringList>

namespace Yandex {

struct TokenSource {
    QString token;
    QString origin;  // human-readable: "env QIYAA_TOKEN", a file path...
};

// Normalises whatever is stored in a token file: raw token, a JSON string,
// {"access_token": "..."}, or a redirect URL fragment "#access_token=...&...".
QString NormalizeToken(const QByteArray& raw);

// Looks in order: $QIYAA_TOKEN, `tokenFile`, then each of `importFiles`.
TokenSource FindToken(const QString& tokenFile, const QStringList& importFiles);

// Owner-only permissions.
bool SaveToken(const QString& tokenFile, const QString& token);

// Logout: leaves an empty token file, which also stops FindToken() from
// importing the old Yaamp token again.
void ForgetToken(const QString& tokenFile);

}  // namespace Yandex
