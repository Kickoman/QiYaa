#include "core/Player.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QUuid>

#include "yandex/Token.h"

namespace qiyaa {

using audio::AudioEngine;

namespace {
constexpr int kMaxTracksPrototype = 200;  // metadata request size for the prototype
}

Player::Player(yandex::ApiClient* api, AudioEngine* engine, QObject* parent)
    : QObject(parent), m_api(api), m_engine(engine) {
    connect(m_engine, &AudioEngine::trackFinished, this, [this] {
        if (m_repeat && m_playlist.size() == 1) return playIndex(m_index);
        next();
    });
    connect(m_engine, &AudioEngine::errorOccurred, this, [this](const QString& msg) {
        Q_EMIT statusMessage(QStringLiteral("Audio error: ") + msg);
    });
}

void Player::start() {
    const yandex::TokenSource token = yandex::findToken();
    if (token.token.isEmpty()) {
        Q_EMIT statusMessage(QStringLiteral("No token: set QIYAA_TOKEN or put it into %1")
                                 .arg(QStringLiteral("~/.config/QiYaa/token")));
        return;
    }
    qInfo("Using Yandex token from %s", qPrintable(token.origin));
    m_api->setToken(token.token);
    Q_EMIT statusMessage(QStringLiteral("Connecting to Yandex Music..."));

    m_api->accountStatus([this](const yandex::Account& acc, const QString& err) {
        if (!err.isEmpty()) return Q_EMIT statusMessage(QStringLiteral("Login failed: ") + err);
        m_account = acc;
        Q_EMIT statusMessage(QStringLiteral("Hello, %1! Loading likes...").arg(acc.displayName));
        m_api->likedTrackIds(acc.uid, [this](const QStringList& ids, const QString& err) {
            if (!err.isEmpty()) return Q_EMIT statusMessage(QStringLiteral("Likes failed: ") + err);
            if (ids.isEmpty()) return Q_EMIT statusMessage(QStringLiteral("No liked tracks"));
            m_api->tracks(ids.mid(0, kMaxTracksPrototype), [this, total = ids.size()](const QList<yandex::Track>& tracks,
                                                                                      const QString& err) {
                if (!err.isEmpty()) return Q_EMIT statusMessage(QStringLiteral("Tracks failed: ") + err);
                m_playlist.clear();
                for (const auto& t : tracks)
                    if (t.available) m_playlist << t;
                m_index = m_playlist.isEmpty() ? -1 : 0;
                qInfo("Loaded %lld playable of %lld liked tracks", qlonglong(m_playlist.size()), qlonglong(total));
                Q_EMIT playlistLoaded();
                Q_EMIT currentTrackChanged();
                Q_EMIT statusMessage(QStringLiteral("%1 liked tracks. Press Play").arg(m_playlist.size()));
            });
        });
    });
}

const yandex::Track* Player::currentTrack() const {
    return (m_index >= 0 && m_index < m_playlist.size()) ? &m_playlist[m_index] : nullptr;
}

double Player::durationSeconds() const {
    const auto* t = currentTrack();
    return t ? double(t->durationMs) / 1000.0 : 0.0;
}

void Player::play() {
    switch (m_engine->state()) {
    case AudioEngine::State::Paused: m_engine->resume(); return;
    case AudioEngine::State::Playing: playIndex(m_index); return;  // Winamp: Play restarts the track
    case AudioEngine::State::Buffering: return;
    case AudioEngine::State::Stopped: playIndex(m_index < 0 ? 0 : m_index); return;
    }
}

void Player::pause() {
    if (m_engine->state() == AudioEngine::State::Paused) m_engine->resume();
    else m_engine->pause();
}

void Player::stop() {
    ++m_generation;
    abortDownload();
    m_engine->stop();
}

void Player::next() {
    if (m_playlist.isEmpty()) return;
    int i;
    if (m_shuffle && m_playlist.size() > 1) {
        do i = int(QRandomGenerator::global()->bounded(m_playlist.size()));
        while (i == m_index);
    } else {
        i = m_index + 1;
        if (i >= m_playlist.size()) {
            if (!m_repeat) {
                stop();
                return;
            }
            i = 0;
        }
    }
    playIndex(i);
}

void Player::previous() {
    if (m_playlist.isEmpty()) return;
    playIndex(m_index > 0 ? m_index - 1 : (m_repeat ? int(m_playlist.size()) - 1 : 0));
}

void Player::seekFraction(double fraction) {
    const double dur = durationSeconds();
    if (dur > 0) m_engine->seek(std::clamp(fraction, 0.0, 1.0) * dur);
}

void Player::playIndex(int index) {
    if (index < 0 || index >= m_playlist.size()) return;
    const quint64 gen = ++m_generation;
    abortDownload();
    m_index = index;
    m_bitrate = 0;
    m_engine->beginStream();
    Q_EMIT currentTrackChanged();

    const yandex::Track track = m_playlist[index];
    m_api->resolveTrackUrl(track.id, [this, gen, track](const yandex::ResolvedUrl& url, const QString& err) {
        if (gen != m_generation) return;
        if (!err.isEmpty()) {
            Q_EMIT statusMessage(QStringLiteral("Cannot get link: ") + err);
            m_engine->stop();
            return;
        }
        m_bitrate = url.bitrateKbps;
        m_playId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_api->reportPlayStarted(m_account, track, m_playId);
        startDownload(url.url, gen);
        Q_EMIT currentTrackChanged();
    });
}

void Player::startDownload(const QUrl& url, quint64 generation) {
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(30000);
    QNetworkReply* reply = m_api->network()->get(req);
    m_download = reply;
    connect(reply, &QNetworkReply::readyRead, this, [this, reply, generation] {
        if (generation == m_generation) m_engine->appendData(reply->readAll());
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation] {
        reply->deleteLater();
        if (generation != m_generation) return;
        if (reply->error() != QNetworkReply::NoError) {
            Q_EMIT statusMessage(QStringLiteral("Download failed: ") + reply->errorString());
            m_engine->failData();
            return;
        }
        m_engine->appendData(reply->readAll());
        m_engine->finishData();
    });
}

void Player::abortDownload() {
    if (m_download) {
        QNetworkReply* r = m_download;
        m_download.clear();
        r->abort();
    }
}

}  // namespace qiyaa
