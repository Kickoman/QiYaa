#include "integrations/mpris.h"

#include "core/player.h"
#include "integrations/media_controls.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>

#include <algorithm>
#include <cmath>

namespace Integrations {

namespace {
const QString kObjectPath = QStringLiteral("/org/mpris/MediaPlayer2");
const QString kPlayerIface = QStringLiteral("org.mpris.MediaPlayer2.Player");

QDBusObjectPath TrackPath(const QString& id) {
    // Object paths allow [A-Za-z0-9_] only.
    QString safe;
    for (QChar character : id) {
        safe += (character.isLetterOrNumber() && character.unicode() < 128) ? character : u'_';
    }
    return QDBusObjectPath(
        QStringLiteral("/io/github/kickoman/qiyaa/track/")
        + (safe.isEmpty() ? QStringLiteral("none") : safe)
    );
}
}  // namespace

Mpris::Mpris(
    MediaControls* controls,
    const QString& serviceSuffix,
    const QDBusConnection& bus,
    QObject* parent
)
    : QObject(parent)
    , mediaControls(controls)
    , connection(bus) {
    new MprisRootAdaptor(this);
    auto* player = new MprisPlayerAdaptor(this);

    if (!bus.isConnected()) {
        qInfo("MPRIS: no D-Bus session bus");
        return;
    }
    service = QStringLiteral("org.mpris.MediaPlayer2.") + serviceSuffix;
    if (connection.interface()->isServiceRegistered(service)) {
        service += QStringLiteral(".instance%1").arg(QCoreApplication::applicationPid());
    }
    if (!connection.registerObject(kObjectPath, this)) {
        qWarning("MPRIS: %s is taken on this connection", qPrintable(kObjectPath));
        return;
    }
    if (!connection.registerService(service)) {
        qWarning(
            "MPRIS: cannot register %s: %s", qPrintable(service),
            qPrintable(connection.lastError().message())
        );
        connection.unregisterObject(kObjectPath);
        return;
    }
    registered = true;

    connect(mediaControls, &MediaControls::trackChanged, this, [this, player] {
        emitPropertiesChanged(
            kPlayerIface,
            {{QStringLiteral("Metadata"), metadata()},
             {QStringLiteral("CanSeek"), player->canSeek()}}
        );
    });
    connect(mediaControls, &MediaControls::artChanged, this, [this] {
        emitPropertiesChanged(kPlayerIface, {{QStringLiteral("Metadata"), metadata()}});
    });
    connect(mediaControls, &MediaControls::statusChanged, this, [this, player] {
        emitPropertiesChanged(
            kPlayerIface,
            {{QStringLiteral("PlaybackStatus"), playbackStatus()},
             {QStringLiteral("CanSeek"), player->canSeek()}}
        );
    });
    connect(mediaControls, &MediaControls::modesChanged, this, [this, player] {
        emitPropertiesChanged(
            kPlayerIface,
            {{QStringLiteral("Shuffle"), player->shuffle()},
             {QStringLiteral("LoopStatus"), player->loopStatus()}}
        );
    });
    connect(mediaControls, &MediaControls::volumeChanged, this, [this, player] {
        emitPropertiesChanged(kPlayerIface, {{QStringLiteral("Volume"), player->volume()}});
    });
    connect(mediaControls, &MediaControls::seeked, player, [player](double seconds) {
        Q_EMIT player->Seeked(qlonglong(seconds * 1e6));
    });
}

Mpris::~Mpris() {
    if (!registered) {
        return;
    }
    connection.unregisterService(service);
    connection.unregisterObject(kObjectPath);
}

QString Mpris::playbackStatus() const {
    switch (mediaControls->status()) {
        case MediaControls::Status::Playing: return QStringLiteral("Playing");
        case MediaControls::Status::Paused: return QStringLiteral("Paused");
        case MediaControls::Status::Stopped: return QStringLiteral("Stopped");
    }
    return QStringLiteral("Stopped");
}

QVariantMap Mpris::metadata() const {
    const Yandex::Track* track = mediaControls->player()->currentTrack();
    if (!track) {
        return {
            {QStringLiteral("mpris:trackid"),
             QVariant::fromValue(
                 QDBusObjectPath(QStringLiteral("/org/mpris/MediaPlayer2/TrackList/NoTrack"))
             )}
        };
    }
    QVariantMap m{
        {QStringLiteral("mpris:trackid"), QVariant::fromValue(TrackPath(track->id))},
        {QStringLiteral("mpris:length"), qlonglong(track->durationMs) * 1000},
        {QStringLiteral("xesam:title"), track->title},
        {QStringLiteral("xesam:artist"), track->artists},
        {QStringLiteral("xesam:url"), track->webUrl().toString()},
    };
    if (!track->albumTitle.isEmpty()) {
        m.insert(QStringLiteral("xesam:album"), track->albumTitle);
    }
    if (const QUrl art = mediaControls->artUrl(); !art.isEmpty()) {
        m.insert(QStringLiteral("mpris:artUrl"), art.toString());
    }
    return m;
}

void Mpris::emitPropertiesChanged(const QString& interface, const QVariantMap& changed) {
    if (!registered) {
        return;
    }
    QDBusMessage msg = QDBusMessage::createSignal(
        kObjectPath, QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("PropertiesChanged")
    );
    msg << interface << changed << QStringList();
    connection.send(msg);
}

MprisRootAdaptor::MprisRootAdaptor(Mpris* parent)
    : QDBusAbstractAdaptor(parent)
    , mpris(parent) { }

void MprisRootAdaptor::Raise() {
    if (auto& f = mpris->controls()->hooks().raise) {
        f();
    }
}

void MprisRootAdaptor::Quit() {
    if (auto& f = mpris->controls()->hooks().quit) {
        f();
    }
}

MprisPlayerAdaptor::MprisPlayerAdaptor(Mpris* parent)
    : QDBusAbstractAdaptor(parent)
    , mpris(parent) { }

QString MprisPlayerAdaptor::playbackStatus() const {
    return mpris->playbackStatus();
}

QString MprisPlayerAdaptor::loopStatus() const {
    return mpris->controls()->player()->repeat() ? QStringLiteral("Playlist")
                                                 : QStringLiteral("None");
}

void MprisPlayerAdaptor::setLoopStatus(const QString& text) {
    mpris->controls()->player()->setRepeat(text != QLatin1String("None"));
}

bool MprisPlayerAdaptor::shuffle() const {
    return mpris->controls()->player()->shuffle();
}

void MprisPlayerAdaptor::setShuffle(bool on) {
    mpris->controls()->player()->setShuffle(on);
}

QVariantMap MprisPlayerAdaptor::metadata() const {
    return mpris->metadata();
}

double MprisPlayerAdaptor::volume() const {
    const auto& f = mpris->controls()->hooks().volume;
    return f ? f() / 100.0 : 1.0;
}

void MprisPlayerAdaptor::setVolume(double value) {
    if (const auto& f = mpris->controls()->hooks().setVolume) {
        f(int(std::lround(std::clamp(value, 0.0, 1.0) * 100)));
    }
}

qlonglong MprisPlayerAdaptor::position() const {
    return qlonglong(mpris->controls()->player()->engine()->positionSeconds() * 1e6);
}

bool MprisPlayerAdaptor::canSeek() const {
    return mpris->controls()->canSeek();
}

void MprisPlayerAdaptor::Next() {
    mpris->controls()->next();
}
void MprisPlayerAdaptor::Previous() {
    mpris->controls()->previous();
}
void MprisPlayerAdaptor::Pause() {
    mpris->controls()->pause();
}
void MprisPlayerAdaptor::PlayPause() {
    mpris->controls()->playPause();
}
void MprisPlayerAdaptor::Stop() {
    mpris->controls()->stop();
}
void MprisPlayerAdaptor::Play() {
    mpris->controls()->play();
}

void MprisPlayerAdaptor::Seek(qlonglong offsetUs) {
    if (!canSeek()) {
        return;
    }
    const double target = std::max(0.0, position() / 1e6 + offsetUs / 1e6);
    if (target >= mpris->controls()->player()->durationSeconds()) {
        return Next();
    }
    mpris->controls()->seekTo(target);
}

void MprisPlayerAdaptor::SetPosition(const QDBusObjectPath& trackId, qlonglong positionUs) {
    const auto* t = mpris->controls()->player()->currentTrack();
    if (!canSeek() || !t || trackId != TrackPath(t->id)) {
        return;
    }
    if (positionUs < 0 || positionUs > qlonglong(t->durationMs) * 1000) {
        return;
    }
    mpris->controls()->seekTo(positionUs / 1e6);
}

}  // namespace Integrations
