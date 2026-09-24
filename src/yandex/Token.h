// OAuth token discovery for the prototype.
// Real login (device-code OAuth) and keychain storage come in stage 1.
#pragma once

#include <QString>

namespace qiyaa::yandex {

struct TokenSource {
    QString token;
    QString origin;  // human-readable: "env QIYAA_TOKEN", a file path...
};

// Normalises whatever is stored in a token file: raw token, a JSON string,
// {"access_token": "..."}, or a redirect URL fragment "#access_token=...&...".
QString normalizeToken(const QByteArray& raw);

// Looks in order: $QIYAA_TOKEN, <configDir>/token, old Yaamp's token.json.
TokenSource findToken();

// Saves the token to <configDir>/token (owner-only permissions).
bool saveToken(const QString& token);

// Logout: leaves an empty token file, which also stops findToken() from
// re-importing the old Yaamp token.
void forgetToken();

}  // namespace qiyaa::yandex
