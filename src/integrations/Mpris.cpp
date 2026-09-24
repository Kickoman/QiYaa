#include "integrations/Mpris.h"

#include <algorithm>
#include <cmath>

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>

#include "core/Player.h"
#include "integrations/MediaControls.h"

namespace qiyaa {

namespace {
const QString kObjectPath = QStringLiteral("/org/mpris/MediaPlayer2");
const QString kPlayerIface = QStringLiteral("org.mpris.MediaPlayer2.Player");

QDBusObjectPath trackPath(const QString& id) {
    // Object paths allow [A-Za-z0-9_] only.
    QString safe;
    for (QChar c : id) safe += (c.isLetterOrNumber() && c.unicode() < 128) ? c : u'_';
    return QDBusObjectPath(QStringLiteral("/io/github/kickoman/qiyaa/track/") + (safe.isEmpty() ? QStringLiteral("none") : safe));
}
}  // namespace

// ------------------------------------------------------------------ Mpris

Mpris::Mpris(MediaControls* controls, const QString& serviceSuffix, const QDBusConnection& bus, QObject* parent)
    : QObject(parent), m_controls(controls), m_bus(bus) {
    new MprisRootAdaptor(this);
    auto* player = new MprisPlayerAdaptor(this);

    if (!bus.isConnected()) {
        qInfo("MPRIS: no D-Bus session bus");
        return;
    }
    // A second running copy gets its own name, as the spec suggests.
    m_service = QStringLiteral("org.mpris.MediaPlayer2.") + serviceSuffix;
    if (m_bus.interface()->isServiceRegistered(m_service))
        m_service += QStringLiteral(".instance%1").arg(QCoreApplication::applicationPid());
    if (!m_bus.registerObject(kObjectPath, this)) {
        qWarning("MPRIS: %s is taken on this connection", qPrintable(kObjectPath));
        return;
    }
    if (!m_bus.registerService(m_service)) {
        qWarning("MPRIS: cannot register %s: %s", qPrintable(m_service), qPrintable(m_bus.lastError().message()));
        m_bus.unregisterObject(kObjectPath);
        return;
    }
    m_registered = true;

    // Clients (GNOME/KDE panels, playerctl --follow) cache properties and rely on these.
    connect(m_controls, &MediaControls::trackChanged, this, [this, player] {
        emitPropertiesChanged(kPlayerIface, {{QStringLiteral("Metadata"), metadata()}, {QStringLiteral("CanSeek"), player->canSeek()}});
    });
    connect(m_controls, &MediaControls::artChanged, this, [this] {
        emitPropertiesChanged(kPlayerIface, {{QStringLiteral("Metadata"), metadata()}});
    });
    connect(m_controls, &MediaControls::statusChanged, this, [this, player] {
        emitPropertiesChanged(kPlayerIface, {{QStringLiteral("PlaybackStatus"), playbackStatus()}, {QStringLiteral("CanSeek"), player->canSeek()}});
    });
    connect(m_controls, &MediaControls::modesChanged, this, [this, player] {
        emitPropertiesChanged(kPlayerIface, {{QStringLiteral("Shuffle"), player->shuffle()}, {QStringLiteral("LoopStatus"), player->loopStatus()}});
    });
    connect(m_controls, &MediaControls::volumeChanged, this, [this, player] {
        emitPropertiesChanged(kPlayerIface, {{QStringLiteral("Volume"), player->volume()}});
    });
    connect(m_controls, &MediaControls::seeked, player, [player](double seconds) { Q_EMIT player->Seeked(qlonglong(seconds * 1e6)); });
}

Mpris::~Mpris() {
    if (!m_registered) return;
    m_bus.unregisterService(m_service);
    m_bus.unregisterObject(kObjectPath);
}

QString Mpris::playbackStatus() const {
    switch (m_controls->status()) {
    case MediaControls::Status::Playing: return QStringLiteral("Playing");
    case MediaControls::Status::Paused: return QStringLiteral("Paused");
    case MediaControls::Status::Stopped: return QStringLiteral("Stopped");
    }
    return QStringLiteral("Stopped");
}

QVariantMap Mpris::metadata() const {
    const yandex::Track* t = m_controls->player()->currentTrack();
    if (!t) return {{QStringLiteral("mpris:trackid"), QVariant::fromValue(QDBusObjectPath(QStringLiteral("/org/mpris/MediaPlayer2/TrackList/NoTrack")))}};
    QVariantMap m{
        {QStringLiteral("mpris:trackid"), QVariant::fromValue(trackPath(t->id))},
        {QStringLiteral("mpris:length"), qlonglong(t->durationMs) * 1000},
        {QStringLiteral("xesam:title"), t->title},
        {QStringLiteral("xesam:artist"), t->artists},
        {QStringLiteral("xesam:url"), t->webUrl().toString()},
    };
    if (!t->albumTitle.isEmpty()) m.insert(QStringLiteral("xesam:album"), t->albumTitle);
    if (const QUrl art = m_controls->artUrl(); !art.isEmpty()) m.insert(QStringLiteral("mpris:artUrl"), art.toString());
    return m;
}

void Mpris::emitPropertiesChanged(const QString& interface, const QVariantMap& changed) {
    if (!m_registered) return;
    QDBusMessage msg = QDBusMessage::createSignal(kObjectPath, QStringLiteral("org.freedesktop.DBus.Properties"),
                                                  QStringLiteral("PropertiesChanged"));
    msg << interface << changed << QStringList();
    m_bus.send(msg);
}

// ------------------------------------------------------------------ org.mpris.MediaPlayer2

MprisRootAdaptor::MprisRootAdaptor(Mpris* parent) : QDBusAbstractAdaptor(parent), m_mpris(parent) {}

void MprisRootAdaptor::Raise() {
    if (auto& f = m_mpris->controls()->hooks().raise) f();
}

void MprisRootAdaptor::Quit() {
    if (auto& f = m_mpris->controls()->hooks().quit) f();
}

// ------------------------------------------------------------------ org.mpris.MediaPlayer2.Player

MprisPlayerAdaptor::MprisPlayerAdaptor(Mpris* parent) : QDBusAbstractAdaptor(parent), m_mpris(parent) {}

QString MprisPlayerAdaptor::playbackStatus() const { return m_mpris->playbackStatus(); }

QString MprisPlayerAdaptor::loopStatus() const {
    return m_mpris->controls()->player()->repeat() ? QStringLiteral("Playlist") : QStringLiteral("None");
}

void MprisPlayerAdaptor::setLoopStatus(const QString& s) {
    // Winamp repeats the playlist; "Track" is the closest we have.
    m_mpris->controls()->player()->setRepeat(s != QLatin1String("None"));  // notifies via modesChanged
}

bool MprisPlayerAdaptor::shuffle() const { return m_mpris->controls()->player()->shuffle(); }

void MprisPlayerAdaptor::setShuffle(bool on) {
    m_mpris->controls()->player()->setShuffle(on);  // notifies via modesChanged
}

QVariantMap MprisPlayerAdaptor::metadata() const { return m_mpris->metadata(); }

double MprisPlayerAdaptor::volume() const {
    const auto& f = m_mpris->controls()->hooks().volume;
    return f ? f() / 100.0 : 1.0;
}

void MprisPlayerAdaptor::setVolume(double v) {
    // The app emits MediaControls::volumeChanged, which notifies clients.
    if (const auto& f = m_mpris->controls()->hooks().setVolume) f(int(std::lround(std::clamp(v, 0.0, 1.0) * 100)));
}

qlonglong MprisPlayerAdaptor::position() const {
    return qlonglong(m_mpris->controls()->player()->engine()->positionSeconds() * 1e6);
}

bool MprisPlayerAdaptor::canSeek() const { return m_mpris->controls()->canSeek(); }

void MprisPlayerAdaptor::Next() { m_mpris->controls()->next(); }
void MprisPlayerAdaptor::Previous() { m_mpris->controls()->previous(); }
void MprisPlayerAdaptor::Pause() { m_mpris->controls()->pause(); }
void MprisPlayerAdaptor::PlayPause() { m_mpris->controls()->playPause(); }
void MprisPlayerAdaptor::Stop() { m_mpris->controls()->stop(); }
void MprisPlayerAdaptor::Play() { m_mpris->controls()->play(); }

// Seeked is emitted through MediaControls::seeked, only when a seek happened.
void MprisPlayerAdaptor::Seek(qlonglong offsetUs) {
    if (!canSeek()) return;  // spec: no-op when CanSeek is false
    const double target = std::max(0.0, position() / 1e6 + offsetUs / 1e6);
    if (target >= m_mpris->controls()->player()->durationSeconds()) return Next();  // spec: past the end = next
    m_mpris->controls()->seekTo(target);
}

void MprisPlayerAdaptor::SetPosition(const QDBusObjectPath& trackId, qlonglong positionUs) {
    const auto* t = m_mpris->controls()->player()->currentTrack();
    if (!canSeek() || !t || trackId != trackPath(t->id)) return;  // stale request
    if (positionUs < 0 || positionUs > qlonglong(t->durationMs) * 1000) return;
    m_mpris->controls()->seekTo(positionUs / 1e6);
}

}  // namespace qiyaa
