// OAuth token discovery for the prototype.
// Real login (device-code OAuth) and keychain storage come in stage 1.
#pragma once

#include <QString>

namespace Yandex {

struct TokenSource {
    QString token;
    QString origin;  // human-readable: "env QIYAA_TOKEN", a file path...
};

// Normalises whatever is stored in a token file: raw token, a JSON string,
// {"access_token": "..."}, or a redirect URL fragment "#access_token=...&...".
QString NormalizeToken(const QByteArray& raw);

// Looks in order: $QIYAA_TOKEN, <configDir>/token, old Yaamp's token.json.
TokenSource FindToken();

// Saves the token to <configDir>/token (owner-only permissions).
bool SaveToken(const QString& token);

// Logout: leaves an empty token file, which also stops findToken() from
// re-importing the old Yaamp token.
void ForgetToken();

}  // namespace Yandex
