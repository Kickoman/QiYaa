// MPRIS 2 (org.mpris.MediaPlayer2) on the D-Bus session bus: media keys,
// GNOME/KDE media controls, lock screen, playerctl.
// https://specifications.freedesktop.org/mpris-spec/latest/
#pragma once

#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusObjectPath>
#include <QObject>
#include <QStringList>
#include <QVariantMap>

namespace qiyaa {

class MediaControls;

class Mpris : public QObject {
    Q_OBJECT
public:
    // `serviceSuffix` goes after "org.mpris.MediaPlayer2." (default "qiyaa").
    // `bus` defaults to the session bus.
    explicit Mpris(MediaControls* controls, const QString& serviceSuffix = QStringLiteral("qiyaa"),
                   const QDBusConnection& bus = QDBusConnection::sessionBus(), QObject* parent = nullptr);
    ~Mpris() override;

    bool isRegistered() const { return m_registered; }
    QString serviceName() const { return m_service; }

    MediaControls* controls() const { return m_controls; }
    QVariantMap metadata() const;
    QString playbackStatus() const;

    void emitPropertiesChanged(const QString& interface, const QVariantMap& changed);

private:
    MediaControls* m_controls;
    QDBusConnection m_bus;
    QString m_service;
    bool m_registered = false;
};

// org.mpris.MediaPlayer2
class MprisRootAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
    Q_PROPERTY(bool CanQuit READ canQuit)
    Q_PROPERTY(bool CanRaise READ canRaise)
    Q_PROPERTY(bool HasTrackList READ hasTrackList)
    Q_PROPERTY(QString Identity READ identity)
    Q_PROPERTY(QString DesktopEntry READ desktopEntry)
    Q_PROPERTY(QStringList SupportedUriSchemes READ supportedUriSchemes)
    Q_PROPERTY(QStringList SupportedMimeTypes READ supportedMimeTypes)
public:
    explicit MprisRootAdaptor(Mpris* parent);
    bool canQuit() const { return true; }
    bool canRaise() const { return true; }
    bool hasTrackList() const { return false; }
    QString identity() const { return QStringLiteral("QiYaa"); }
    QString desktopEntry() const { return QStringLiteral("qiyaa"); }
    QStringList supportedUriSchemes() const { return {}; }
    QStringList supportedMimeTypes() const { return {}; }

public Q_SLOTS:
    void Raise();
    void Quit();

private:
    Mpris* m_mpris;
};

// org.mpris.MediaPlayer2.Player
class MprisPlayerAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(QString PlaybackStatus READ playbackStatus)
    Q_PROPERTY(QString LoopStatus READ loopStatus WRITE setLoopStatus)
    Q_PROPERTY(double Rate READ rate WRITE setRate)
    Q_PROPERTY(bool Shuffle READ shuffle WRITE setShuffle)
    Q_PROPERTY(QVariantMap Metadata READ metadata)
    Q_PROPERTY(double Volume READ volume WRITE setVolume)
    Q_PROPERTY(qlonglong Position READ position)
    Q_PROPERTY(double MinimumRate READ rate)
    Q_PROPERTY(double MaximumRate READ rate)
    Q_PROPERTY(bool CanGoNext READ canControl)
    Q_PROPERTY(bool CanGoPrevious READ canControl)
    Q_PROPERTY(bool CanPlay READ canControl)
    Q_PROPERTY(bool CanPause READ canControl)
    Q_PROPERTY(bool CanSeek READ canSeek)
    Q_PROPERTY(bool CanControl READ canControl)
public:
    explicit MprisPlayerAdaptor(Mpris* parent);

    QString playbackStatus() const;
    QString loopStatus() const;
    void setLoopStatus(const QString& s);
    double rate() const { return 1.0; }
    void setRate(double) {}
    bool shuffle() const;
    void setShuffle(bool on);
    QVariantMap metadata() const;
    double volume() const;
    void setVolume(double v);
    qlonglong position() const;
    bool canControl() const { return true; }
    bool canSeek() const;

public Q_SLOTS:
    void Next();
    void Previous();
    void Pause();
    void PlayPause();
    void Stop();
    void Play();
    void Seek(qlonglong offsetUs);
    void SetPosition(const QDBusObjectPath& trackId, qlonglong positionUs);
    void OpenUri(const QString&) {}

Q_SIGNALS:
    void Seeked(qlonglong positionUs);

private:
    Mpris* m_mpris;
};

}  // namespace qiyaa
