#include "audio/audio_engine.h"
#include "core/cover_cache.h"
#include "core/player.h"
#include "integrations/media_controls.h"
#include "integrations/mpris.h"
#include "yandex/api_client.h"
#include "yandex/library.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QList>
#include <QNetworkAccessManager>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QString>
#include <QTemporaryDir>
#include <QTest>
#include <QVariantMap>

#include <memory>

namespace {
const QString kService = QStringLiteral("org.mpris.MediaPlayer2.qiyaatest");
const QString kPath = QStringLiteral("/org/mpris/MediaPlayer2");

QList<Yandex::Track> MakeTracks(int count) {
    QList<Yandex::Track> tracks;
    for (int i = 0; i < count; ++i) {
        Yandex::Track track;
        track.id = QString::number(100 + i);
        track.albumId = QStringLiteral("7");
        track.title = QStringLiteral("Песня %1").arg(i + 1);
        track.artists = {QStringLiteral("Кино")};
        track.albumTitle = QStringLiteral("Альбом");
        track.durationMs = 200'000;
        tracks << track;
    }
    return tracks;
}

class ChangeSink : public QObject {
    Q_OBJECT
public:
    QList<QVariantMap> changes;
public Q_SLOTS:
    void onChanged(const QString&, const QVariantMap& changed, const QStringList&) {
        changes << changed;
    }
};

}  // namespace

class TestMpris : public QObject {
    Q_OBJECT
private:
    QTemporaryDir coverDirectory;
    QNetworkAccessManager networkManager;
    std::unique_ptr<Yandex::ApiClient> api;
    std::unique_ptr<Yandex::Library> library;
    std::unique_ptr<Audio::AudioEngine> engine;
    std::unique_ptr<Core::Player> player;
    std::unique_ptr<Core::CoverCache> covers;
    std::unique_ptr<Integrations::MediaControls> controls;
    std::unique_ptr<Integrations::Mpris> mpris;
    int volume = 50;
    int raised = 0;
    QString gdbus;

    // Runs gdbus asynchronously so our event loop can serve the call.
    QString gdbusCall(const QStringList& arguments) {
        QProcess process;
        process.start(
            gdbus,
            QStringList{"call", "--session", "--dest", kService, "--object-path", kPath, "--method"}
                + arguments
        );
        if (!QTest::qWaitFor([&] { return process.state() == QProcess::NotRunning; }, 5000)) {
            return QStringLiteral("<timeout>");
        }
        return QString::fromUtf8(process.readAllStandardOutput() + process.readAllStandardError())
            .trimmed();
    }

private Q_SLOTS:
    void initTestCase() {
        if (!QDBusConnection::sessionBus().isConnected()) {
            QSKIP("no D-Bus session bus (run under dbus-run-session)");
        }
        gdbus = QStandardPaths::findExecutable(QStringLiteral("gdbus"));
        api = std::make_unique<Yandex::ApiClient>(&networkManager);
        api->setBaseUrl(QStringLiteral("http://127.0.0.1:9")
        );  // nothing listens: link requests fail fast
        library = std::make_unique<Yandex::Library>(api.get());
        engine = std::make_unique<Audio::AudioEngine>();
        player = std::make_unique<Core::Player>(library.get(), engine.get());
        covers = std::make_unique<Core::CoverCache>(nullptr, coverDirectory.path());
        Integrations::MediaControls::Hooks hooks;
        hooks.volume = [this] { return volume; };
        hooks.setVolume = [this](int value) {
            if (value == volume) {
                return;
            }
            volume = value;
            Q_EMIT controls->volumeChanged();
        };
        hooks.raise = [this] { ++raised; };
        controls = std::make_unique<Integrations::MediaControls>(player.get(), covers.get(), hooks);
        mpris = std::make_unique<Integrations::Mpris>(controls.get(), QStringLiteral("qiyaatest"));
        QVERIFY(mpris->isRegistered());
        QCOMPARE(mpris->serviceName(), kService);
        player->setQueue(MakeTracks(3), QStringLiteral("T"), false);
    }

    void rootInterfaceNamesTheAppAndRaisesIt() {
        QDBusInterface root(kService, kPath, QStringLiteral("org.mpris.MediaPlayer2"));
        QVERIFY(root.isValid());
        QCOMPARE(root.property("Identity").toString(), QStringLiteral("QiYaa"));
        QCOMPARE(root.property("CanRaise").toBool(), true);
        if (gdbus.isEmpty()) {
            QSKIP("gdbus not installed");
        }
        gdbusCall({QStringLiteral("org.mpris.MediaPlayer2.Raise")});
        QCOMPARE(raised, 1);
    }

    void metadataDescribesTheCurrentTrack() {
        QDBusInterface playerInterface(
            kService, kPath, QStringLiteral("org.mpris.MediaPlayer2.Player")
        );
        const QVariantMap metadata = playerInterface.property("Metadata").toMap();
        QCOMPARE(metadata.value("xesam:title").toString(), QStringLiteral("Песня 1"));
        QCOMPARE(
            metadata.value("xesam:artist").toStringList(), QStringList{QStringLiteral("Кино")}
        );
        QCOMPARE(metadata.value("mpris:length").toLongLong(), 200'000'000LL);
        QCOMPARE(playerInterface.property("PlaybackStatus").toString(), QStringLiteral("Stopped"));
    }

    void externalClientControlsPlayer() {
        if (gdbus.isEmpty()) {
            QSKIP("gdbus not installed");
        }
        ChangeSink sink;
        QVERIFY(QDBusConnection::sessionBus().connect(
            kService, kPath, QStringLiteral("org.freedesktop.DBus.Properties"),
            QStringLiteral("PropertiesChanged"), &sink,
            SLOT(onChanged(QString, QVariantMap, QStringList))
        ));
        const QString reply = gdbusCall({QStringLiteral("org.mpris.MediaPlayer2.Player.Next")});
        QVERIFY2(reply == QStringLiteral("()"), qPrintable(reply));
        QCOMPARE(player->currentIndex(), 1);
        // Clients (GNOME's media panel) learn about the new track from PropertiesChanged.
        QVERIFY(QTest::qWaitFor(
            [&] {
                for (const auto& change : sink.changes) {
                    if (change.contains("Metadata")) {
                        return true;
                    }
                }
                return false;
            },
            3000
        ));

        gdbusCall({QStringLiteral("org.mpris.MediaPlayer2.Player.Previous")});
        QCOMPARE(player->currentIndex(), 0);
    }

    void writablePropertiesReachThePlayerAndAreAnnounced() {
        if (gdbus.isEmpty()) {
            QSKIP("gdbus not installed");
        }
        ChangeSink sink;
        QVERIFY(QDBusConnection::sessionBus().connect(
            kService, kPath, QStringLiteral("org.freedesktop.DBus.Properties"),
            QStringLiteral("PropertiesChanged"), &sink,
            SLOT(onChanged(QString, QVariantMap, QStringList))
        ));
        const QString setMethod = QStringLiteral("org.freedesktop.DBus.Properties.Set");
        const QString playerInterfaceName = QStringLiteral("org.mpris.MediaPlayer2.Player");
        gdbusCall(
            {setMethod, playerInterfaceName, QStringLiteral("Volume"), QStringLiteral("<0.3>")}
        );
        QCOMPARE(volume, 30);
        gdbusCall(
            {setMethod, playerInterfaceName, QStringLiteral("Shuffle"), QStringLiteral("<true>")}
        );
        QVERIFY(player->shuffle());
        gdbusCall(
            {setMethod, playerInterfaceName, QStringLiteral("LoopStatus"),
             QStringLiteral("<'Playlist'>")}
        );
        QVERIFY(player->repeat());
        QDBusInterface playerInterface(kService, kPath, playerInterfaceName);
        QVERIFY(qFuzzyCompare(playerInterface.property("Volume").toDouble(), 0.3));
        QVERIFY(QTest::qWaitFor(
            [&] {
                bool volumeChanged = false, shuffleChanged = false;
                for (const auto& change : sink.changes) {
                    volumeChanged = volumeChanged || change.contains("Volume");
                    shuffleChanged = shuffleChanged || change.contains("Shuffle");
                }
                return volumeChanged && shuffleChanged;
            },
            3000
        ));
    }

    void appSideChangesAreAnnounced() {
        ChangeSink sink;
        QVERIFY(QDBusConnection::sessionBus().connect(
            kService, kPath, QStringLiteral("org.freedesktop.DBus.Properties"),
            QStringLiteral("PropertiesChanged"), &sink,
            SLOT(onChanged(QString, QVariantMap, QStringList))
        ));
        player->setShuffle(false);
        controls->hooks().setVolume(80);
        QVERIFY(QTest::qWaitFor(
            [&] {
                bool volumeChanged = false, shuffleChanged = false;
                for (const auto& change : sink.changes) {
                    volumeChanged = volumeChanged
                        || (change.contains("Volume")
                            && qFuzzyCompare(change.value("Volume").toDouble(), 0.8));
                    shuffleChanged = shuffleChanged
                        || (change.contains("Shuffle") && !change.value("Shuffle").toBool());
                }
                return volumeChanged && shuffleChanged;
            },
            3000
        ));
    }

    void seekIsIgnoredWhenNotSeekable() {
        if (gdbus.isEmpty()) {
            QSKIP("gdbus not installed");
        }
        QDBusInterface playerInterface(
            kService, kPath, QStringLiteral("org.mpris.MediaPlayer2.Player")
        );
        QCOMPARE(playerInterface.property("CanSeek").toBool(), false);  // stopped
        const int before = player->currentIndex();
        gdbusCall({QStringLiteral("org.mpris.MediaPlayer2.Player.Seek"), QStringLiteral("1000000")}
        );
        QCOMPARE(player->currentIndex(), before);  // not "past the end = next"
    }

    void secondInstanceGetsItsOwnName() {
        // Another copy of the app has its own connection to the bus.
        QDBusConnection second =
            QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("second"));
        {
            Integrations::Mpris other(controls.get(), QStringLiteral("qiyaatest"), second);
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
#include "mpris_test.moc"
