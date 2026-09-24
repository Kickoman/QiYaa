// MPRIS over a real session bus (CTest runs this under dbus-run-session).
// Commands come from an external client (gdbus), like GNOME / playerctl would send them.
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QNetworkAccessManager>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include "core/CoverCache.h"
#include "core/Player.h"
#include "integrations/MediaControls.h"
#include "integrations/Mpris.h"

using namespace qiyaa;

namespace {
const QString kService = QStringLiteral("org.mpris.MediaPlayer2.qiyaatest");
const QString kPath = QStringLiteral("/org/mpris/MediaPlayer2");

QList<yandex::Track> tracks(int n) {
    QList<yandex::Track> out;
    for (int i = 0; i < n; ++i) {
        yandex::Track t;
        t.id = QString::number(100 + i);
        t.albumId = QStringLiteral("7");
        t.title = QStringLiteral("Песня %1").arg(i + 1);
        t.artists = {QStringLiteral("Кино")};
        t.albumTitle = QStringLiteral("Альбом");
        t.durationMs = 200000;
        out << t;
    }
    return out;
}
}  // namespace

// Receives org.freedesktop.DBus.Properties.PropertiesChanged.
class ChangeSink : public QObject {
    Q_OBJECT
public:
    QList<QVariantMap> changes;
public Q_SLOTS:
    void onChanged(const QString&, const QVariantMap& changed, const QStringList&) { changes << changed; }
};

class TestMpris : public QObject {
    Q_OBJECT
private:
    QTemporaryDir tmp;
    QNetworkAccessManager nam;
    std::unique_ptr<yandex::ApiClient> api;
    std::unique_ptr<yandex::Library> lib;
    std::unique_ptr<audio::AudioEngine> engine;
    std::unique_ptr<Player> player;
    std::unique_ptr<CoverCache> covers;
    std::unique_ptr<MediaControls> controls;
    std::unique_ptr<Mpris> mpris;
    int volume = 50;
    int raised = 0;
    QString gdbus;

    // Runs gdbus asynchronously so our event loop can serve the call.
    QString gdbusCall(const QStringList& args) {
        QProcess p;
        p.start(gdbus, QStringList{"call", "--session", "--dest", kService, "--object-path", kPath, "--method"} + args);
        if (!QTest::qWaitFor([&] { return p.state() == QProcess::NotRunning; }, 5000)) return QStringLiteral("<timeout>");
        return QString::fromUtf8(p.readAllStandardOutput() + p.readAllStandardError()).trimmed();
    }

private Q_SLOTS:
    void initTestCase() {
        if (!QDBusConnection::sessionBus().isConnected()) QSKIP("no D-Bus session bus (run under dbus-run-session)");
        gdbus = QStandardPaths::findExecutable(QStringLiteral("gdbus"));
        api = std::make_unique<yandex::ApiClient>(&nam);
        api->setBaseUrl(QStringLiteral("http://127.0.0.1:9"));  // nothing listens: link requests fail fast
        lib = std::make_unique<yandex::Library>(api.get());
        engine = std::make_unique<audio::AudioEngine>();
        player = std::make_unique<Player>(lib.get(), engine.get());
        covers = std::make_unique<CoverCache>(nullptr, tmp.path());
        MediaControls::Hooks hooks;
        hooks.volume = [this] { return volume; };
        hooks.setVolume = [this](int v) {  // like MainWindow::setVolume, which the app wires to volumeChanged
            if (v == volume) return;
            volume = v;
            Q_EMIT controls->volumeChanged();
        };
        hooks.raise = [this] { ++raised; };
        controls = std::make_unique<MediaControls>(player.get(), covers.get(), hooks);
        mpris = std::make_unique<Mpris>(controls.get(), QStringLiteral("qiyaatest"));
        QVERIFY(mpris->isRegistered());
        QCOMPARE(mpris->serviceName(), kService);
        player->setQueue(tracks(3), QStringLiteral("T"), false);
    }

    void rootInterface() {
        QDBusInterface root(kService, kPath, QStringLiteral("org.mpris.MediaPlayer2"));
        QVERIFY(root.isValid());
        QCOMPARE(root.property("Identity").toString(), QStringLiteral("QiYaa"));
        QCOMPARE(root.property("CanRaise").toBool(), true);
        if (gdbus.isEmpty()) QSKIP("gdbus not installed");
        gdbusCall({QStringLiteral("org.mpris.MediaPlayer2.Raise")});
        QCOMPARE(raised, 1);
    }

    void metadataOfCurrentTrack() {
        QDBusInterface pl(kService, kPath, QStringLiteral("org.mpris.MediaPlayer2.Player"));
        const QVariantMap md = pl.property("Metadata").toMap();
        QCOMPARE(md.value("xesam:title").toString(), QStringLiteral("Песня 1"));
        QCOMPARE(md.value("xesam:artist").toStringList(), QStringList{QStringLiteral("Кино")});
        QCOMPARE(md.value("mpris:length").toLongLong(), 200000000LL);
        QCOMPARE(pl.property("PlaybackStatus").toString(), QStringLiteral("Stopped"));
    }

    void externalClientControlsPlayer() {
        if (gdbus.isEmpty()) QSKIP("gdbus not installed");
        ChangeSink sink;
        QVERIFY(QDBusConnection::sessionBus().connect(kService, kPath, QStringLiteral("org.freedesktop.DBus.Properties"),
                                                      QStringLiteral("PropertiesChanged"), &sink,
                                                      SLOT(onChanged(QString, QVariantMap, QStringList))));
        const QString out = gdbusCall({QStringLiteral("org.mpris.MediaPlayer2.Player.Next")});
        QVERIFY2(out == QStringLiteral("()"), qPrintable(out));
        QCOMPARE(player->currentIndex(), 1);
        // Clients (GNOME's media panel) learn about the new track from PropertiesChanged.
        QVERIFY(QTest::qWaitFor([&] {
            for (const auto& c : sink.changes)
                if (c.contains("Metadata")) return true;
            return false;
        }, 3000));

        gdbusCall({QStringLiteral("org.mpris.MediaPlayer2.Player.Previous")});
        QCOMPARE(player->currentIndex(), 0);
    }

    void writableProperties() {
        if (gdbus.isEmpty()) QSKIP("gdbus not installed");
        ChangeSink sink;
        QVERIFY(QDBusConnection::sessionBus().connect(kService, kPath, QStringLiteral("org.freedesktop.DBus.Properties"),
                                                      QStringLiteral("PropertiesChanged"), &sink,
                                                      SLOT(onChanged(QString, QVariantMap, QStringList))));
        const QString set = QStringLiteral("org.freedesktop.DBus.Properties.Set");
        const QString iface = QStringLiteral("org.mpris.MediaPlayer2.Player");
        gdbusCall({set, iface, QStringLiteral("Volume"), QStringLiteral("<0.3>")});
        QCOMPARE(volume, 30);
        gdbusCall({set, iface, QStringLiteral("Shuffle"), QStringLiteral("<true>")});
        QVERIFY(player->shuffle());
        gdbusCall({set, iface, QStringLiteral("LoopStatus"), QStringLiteral("<'Playlist'>")});
        QVERIFY(player->repeat());
        QDBusInterface pl(kService, kPath, iface);
        QVERIFY(qFuzzyCompare(pl.property("Volume").toDouble(), 0.3));
        QVERIFY(QTest::qWaitFor([&] {
            bool vol = false, shuffle = false;
            for (const auto& c : sink.changes) {
                vol = vol || c.contains("Volume");
                shuffle = shuffle || c.contains("Shuffle");
            }
            return vol && shuffle;
        }, 3000));
    }

    // Changes made in the app (not over D-Bus) must reach clients too.
    void appSideChangesAreAnnounced() {
        ChangeSink sink;
        QVERIFY(QDBusConnection::sessionBus().connect(kService, kPath, QStringLiteral("org.freedesktop.DBus.Properties"),
                                                      QStringLiteral("PropertiesChanged"), &sink,
                                                      SLOT(onChanged(QString, QVariantMap, QStringList))));
        player->setShuffle(false);
        controls->hooks().setVolume(80);
        QVERIFY(QTest::qWaitFor([&] {
            bool vol = false, shuffle = false;
            for (const auto& c : sink.changes) {
                vol = vol || (c.contains("Volume") && qFuzzyCompare(c.value("Volume").toDouble(), 0.8));
                shuffle = shuffle || (c.contains("Shuffle") && !c.value("Shuffle").toBool());
            }
            return vol && shuffle;
        }, 3000));
    }

    void seekIsIgnoredWhenNotSeekable() {
        if (gdbus.isEmpty()) QSKIP("gdbus not installed");
        QDBusInterface pl(kService, kPath, QStringLiteral("org.mpris.MediaPlayer2.Player"));
        QCOMPARE(pl.property("CanSeek").toBool(), false);  // stopped
        const int before = player->currentIndex();
        gdbusCall({QStringLiteral("org.mpris.MediaPlayer2.Player.Seek"), QStringLiteral("1000000")});
        QCOMPARE(player->currentIndex(), before);  // not "past the end = next"
    }

    void secondInstanceGetsItsOwnName() {
        // Another copy of the app has its own connection to the bus.
        QDBusConnection second = QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("second"));
        {
            Mpris other(controls.get(), QStringLiteral("qiyaatest"), second);
            QVERIFY(other.isRegistered());
            QVERIFY(other.serviceName().startsWith(kService + QStringLiteral(".instance")));
        }
        QDBusConnection::disconnectFromBus(QStringLiteral("second"));
    }

    void cleanupTestCase() {
        mpris.reset();
        controls.reset();
    }
};

QTEST_GUILESS_MAIN(TestMpris)
#include "test_mpris.moc"
