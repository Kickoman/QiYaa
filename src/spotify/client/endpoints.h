#pragma once

#include <QString>

namespace SpotifyClient {

struct Endpoints {
    QString apresolve = QStringLiteral("https://apresolve.spotify.com");
    QString accounts = QStringLiteral("https://accounts.spotify.com");
    QString clientToken = QStringLiteral("https://clienttoken.spotify.com");
    QString login5 = QStringLiteral("https://login5.spotify.com");
    QString spclientScheme = QStringLiteral("https");
};

}  // namespace SpotifyClient
