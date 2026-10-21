#include "app/application.h"
#include "app/spotify_account.h"
#include "app/telemetry_bridge.h"
#include "app/window_set.h"
#include "core/lyrics_finder.h"
#include "spotify/client/session.h"
#include "spotify/client/web.h"
#include "support/mock_http_server.h"
#include "support/spec_fixtures.h"
#include "telemetry/machine_id.h"
#include "telemetry/reporter.h"
#include "telemetry/system_info.h"
#include "ui/lyrics_window.h"
#include "ui/main_window.h"
#include "ui/milkdrop_window.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLatin1String>
#include <QNetworkAccessManager>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

#include <memory>

using Telemetry::Reporter;

namespace {

const QString kPath = QStringLiteral("/api/telemetry");
const QString kMachine = QStringLiteral("0123456789abcdef0123456789abcdef");

QJsonObject Batch(const Tests::MockRequest& request) {
    return QJsonDocument::fromJson(request.body).object();
}

QJsonArray Events(const Tests::MockRequest& request) {
    return Batch(request).value(QStringLiteral("events")).toArray();
}

QStringList Types(const QJsonArray& events) {
    QStringList types;
    for (const QJsonValue& event : events) {
        types << event.toObject().value(QStringLiteral("type")).toString();
    }
    return types;
}

QByteArray Marker(int seconds) {
    const QJsonObject marker{
        {QStringLiteral("version"), QStringLiteral("0.4.9")},
        {QStringLiteral("seconds"), seconds},
    };
    return QJsonDocument(marker).toJson(QJsonDocument::Compact);
}

QSet<QString> Keys(const QJsonObject& object) {
    const QStringList keys = object.keys();
    return {keys.cbegin(), keys.cend()};
}

// Everything a start event needs besides the system: as the app fills it.
QJsonObject StartFields() {
    QJsonObject fields = Telemetry::SystemFields();
    fields.insert(QStringLiteral("language"), QStringLiteral("be"));
    fields.insert(QStringLiteral("scale"), 2);
    fields.insert(QStringLiteral("skin"), QStringLiteral("base"));
    fields.insert(QStringLiteral("vis"), QStringLiteral("spectrum"));
    fields.insert(QStringLiteral("equalizer"), true);
    fields.insert(QStringLiteral("milkdropBuilt"), false);
    fields.insert(QStringLiteral("milkdropVisible"), false);
    fields.insert(QStringLiteral("jamBuilt"), true);
    fields.insert(QStringLiteral("audioBackend"), QStringLiteral("Null"));
    fields.insert(QStringLiteral("loggedIn"), false);
    fields.insert(QStringLiteral("spotifyLoggedIn"), false);
    fields.insert(QStringLiteral("firstRun"), true);
    return fields;
}

}  // namespace

class TestTelemetry : public QObject {
    Q_OBJECT
private:
    std::unique_ptr<Tests::MockHttpServer> server;
    std::unique_ptr<QNetworkAccessManager> network;
    std::unique_ptr<QTemporaryDir> directory;
    int status = 204;

    std::unique_ptr<Reporter> reporter() {
        return std::make_unique<Reporter>(Reporter::Config{
            QUrl(server->baseUrl() + kPath), directory->path(), QStringLiteral("0.5.0"), kMachine, network.get()
        });
    }

    int posts() const {
        int count = 0;
        for (const Tests::MockRequest& request : server->requests()) {
            count += request.path == kPath ? 1 : 0;
        }
        return count;
    }

    const Tests::MockRequest& lastPost() const { return *server->last(kPath); }

    QStringList sessionFiles(const QString& session) const {
        return QDir(directory->path()).entryList({session + QStringLiteral(".*")}, QDir::Files);
    }

    // A run that ended without closing: its marker and its queue, no lock.
    void writeDeadRun(const QString& session, const QByteArray& marker, const QByteArray& queue) {
        QDir().mkpath(directory->path());
        QFile json(QDir(directory->path()).filePath(session + QStringLiteral(".json")));
        QVERIFY(json.open(QIODevice::WriteOnly));
        json.write(marker);
        QFile queued(QDir(directory->path()).filePath(session + QStringLiteral(".queue")));
        QVERIFY(queued.open(QIODevice::WriteOnly));
        queued.write(queue);
    }

private Q_SLOTS:
    void init() {
        status = 204;
        server = std::make_unique<Tests::MockHttpServer>();
        server->on("POST", kPath, [this](const Tests::MockRequest&) { return Tests::MockResponse{status, {}}; });
        network = std::make_unique<QNetworkAccessManager>();
        directory = std::make_unique<QTemporaryDir>();
    }

    void cleanup() {
        directory.reset();
        network.reset();
        server.reset();
    }

    void machineIdIsTheAppsHashOfTheSystemId() {
        QCOMPARE(Telemetry::MachineId("abc"), QStringLiteral("3a8271ff2b263f300989f47f507a3804"));
        QString stored;
        const auto store = [&stored](const QString& id) { stored = id; };
        const QString id = Telemetry::MachineIdFor(stored, store);
        QVERIFY(QRegularExpression(QStringLiteral("^[0-9a-f]{32}$")).match(id).hasMatch());
        QCOMPARE(Telemetry::MachineIdFor(stored, store), id);
        QVERIFY(!id.startsWith(QString::fromLatin1(QSysInfo::machineUniqueId().left(8))));
    }

    void startCarriesEveryFieldOfTheSpecsExample() {  // TEL-03
        auto run = reporter();
        run->begin(StartFields());
        QTRY_COMPARE(posts(), 1);
        const QJsonObject batch = Batch(lastPost());
        QCOMPARE(batch.value(QStringLiteral("app")).toString(), QStringLiteral("desktop"));
        QCOMPARE(batch.value(QStringLiteral("machine")).toString(), kMachine);
        const QJsonObject start = Events(lastPost()).at(0).toObject();
        const QJsonObject example =
            Tests::SpecObject(QStringLiteral("telemetry/examples/start.json"))
                .value(QStringLiteral("events"))
                .toArray()
                .at(0)
                .toObject();
        QCOMPARE(Keys(start), Keys(example));
        QCOMPARE(start.value(QStringLiteral("session")).toString(), run->session());
        QCOMPARE(start.value(QStringLiteral("version")).toString(), QStringLiteral("0.5.0"));
        QTRY_COMPARE(run->queued(), 0);
    }

    void answersDecideWhatStays() {  // TEL-04
        status = 500;
        auto run = reporter();
        run->begin(StartFields());
        QTRY_VERIFY(posts() == 1 && !run->isSending());
        QCOMPARE(run->queued(), 1);
        run->send();
        QTest::qWait(100);
        QCOMPARE(posts(), 1);  // waits a minute before the next try

        directory = std::make_unique<QTemporaryDir>();
        status = 400;
        auto refused = reporter();
        refused->begin(StartFields());
        QTRY_VERIFY(posts() == 2 && !refused->isSending());
        QCOMPARE(refused->queued(), 0);

        directory = std::make_unique<QTemporaryDir>();
        status = 429;
        auto limited = reporter();
        limited->begin(StartFields());
        QTRY_VERIFY(posts() == 3 && !limited->isSending());
        QCOMPARE(limited->queued(), 1);
        limited->send();
        QTest::qWait(100);
        QCOMPARE(posts(), 3);
    }

    void aBatchHoldsAtMostFiftyEvents() {
        status = 500;
        auto run = reporter();
        run->begin(StartFields());
        QTRY_VERIFY(posts() == 1 && !run->isSending());
        for (int i = 0; i < 120; ++i) {
            run->recordFeature(QStringLiteral("vis"), QStringLiteral("off"));
        }
        QCOMPARE(run->queued(), 121);
        status = 204;
        auto next = reporter();  // a later run: no delay of its own, takes the queue over
        run.reset();
        next->begin(StartFields());
        QTRY_VERIFY(next->queued() == 0 && !next->isSending());
        QList<qsizetype> sizes;
        for (const Tests::MockRequest& request : server->requests()) {
            sizes << Events(request).size();
        }
        // The first run's start; then 121 of its events, its unclean exit and the new start.
        QCOMPARE(sizes, QList<qsizetype>({1, 50, 50, 23}));
    }

    void aRunThatDidNotCloseIsReportedWithItsQueue() {  // TEL-03, TEL-06
        const QString dead = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QJsonObject feature{
            {QStringLiteral("type"), QStringLiteral("feature")},
            {QStringLiteral("at"), QStringLiteral("2026-10-08T10:00:00.000Z")},
            {QStringLiteral("session"), dead},
            {QStringLiteral("version"), QStringLiteral("0.4.9")},
            {QStringLiteral("name"), QStringLiteral("vis")},
            {QStringLiteral("value"), QStringLiteral("off")},
        };
        writeDeadRun(dead, Marker(420), QJsonDocument(feature).toJson(QJsonDocument::Compact) + '\n');
        auto run = reporter();
        run->begin(StartFields());
        QTRY_COMPARE(posts(), 1);
        const QJsonArray events = Events(lastPost());
        QCOMPARE(Types(events), QStringList({"feature", "unclean_exit", "start"}));
        const QJsonObject unclean = events.at(1).toObject();
        QCOMPARE(unclean.value(QStringLiteral("session")).toString(), dead);
        QCOMPARE(unclean.value(QStringLiteral("version")).toString(), QStringLiteral("0.4.9"));
        QCOMPARE(unclean.value(QStringLiteral("seconds")).toInt(), 420);
        QVERIFY(sessionFiles(dead).isEmpty());
    }

    void aRunningInstanceIsLeftAlone() {
        auto first = reporter();
        first->begin(StartFields());
        QTRY_VERIFY(posts() == 1 && !first->isSending());
        auto second = reporter();
        second->begin(StartFields());
        QTRY_COMPARE(posts(), 2);
        QCOMPARE(Types(Events(lastPost())), QStringList({"start"}));
        QVERIFY(!sessionFiles(first->session()).isEmpty());
    }

    void aClosedRunLeavesNothingBehind() {  // TEL-08
        auto run = reporter();
        run->begin(StartFields());
        run->countTrack();
        run->countTrack();
        run->countJam();
        run->finish();
        QTRY_VERIFY(!run->isSending() && run->queued() == 0);
        const QJsonObject exit = Events(lastPost()).last().toObject();
        QCOMPARE(exit.value(QStringLiteral("type")).toString(), QStringLiteral("exit"));
        QCOMPARE(exit.value(QStringLiteral("tracks")).toInt(), 2);
        QCOMPARE(exit.value(QStringLiteral("jams")).toInt(), 1);
        QCOMPARE(exit.value(QStringLiteral("errorsDropped")).toInt(), 0);
        const QString session = run->session();
        run.reset();
        QVERIFY2(sessionFiles(session).isEmpty(), qPrintable(sessionFiles(session).join(u' ')));
    }

    void theLyricsCountsComeOnlyAfterALookupAndAllAtOnce() {  // TEL-17
        auto quiet = reporter();
        quiet->begin(StartFields());
        quiet->finish();
        QTRY_VERIFY(!quiet->isSending() && quiet->queued() == 0);
        const QJsonObject without = Events(lastPost()).last().toObject();
        QCOMPARE(without.value(QStringLiteral("type")).toString(), QStringLiteral("exit"));
        QVERIFY(!without.contains(QStringLiteral("lyricsLookups")));
        quiet.reset();

        auto run = reporter();
        run->begin(StartFields());
        run->countLyrics(QStringLiteral("lyricsLookups"));
        run->countLyrics(QStringLiteral("lyricsThirdPartySynced"));
        run->countLyrics(QStringLiteral("lyricsLookups"));
        run->countLyrics(QStringLiteral("lyricsNone"));
        run->countLyrics(QStringLiteral("lyricsThirdPartyFailed"));
        run->countLyrics(QStringLiteral("trackTitle"));
        run->finish();
        QTRY_VERIFY(!run->isSending() && run->queued() == 0);
        const QJsonObject exit = Events(lastPost()).last().toObject();
        QCOMPARE(exit.value(QStringLiteral("lyricsLookups")).toInt(), 2);
        QCOMPARE(exit.value(QStringLiteral("lyricsThirdPartySynced")).toInt(), 1);
        QCOMPARE(exit.value(QStringLiteral("lyricsNone")).toInt(), 1);
        QCOMPARE(exit.value(QStringLiteral("lyricsThirdPartyFailed")).toInt(), 1);
        QCOMPARE(exit.value(QStringLiteral("lyricsSourceText")).toInt(), 0);
        QVERIFY(!exit.contains(QStringLiteral("trackTitle")));
        QJsonObject example;
        for (const QJsonValue& event :
             Tests::SpecObject(QStringLiteral("telemetry/examples/session.json"))[QStringLiteral("events")].toArray()) {
            if (event[QStringLiteral("type")].toString() == QLatin1String("exit")) {
                example = event.toObject();
            }
        }
        QStringList ours;
        QStringList theirs;
        for (const QString& key : exit.keys()) {
            if (key.startsWith(QLatin1String("lyrics"))) {
                ours << key;
            }
        }
        for (const QString& key : example.keys()) {
            if (key.startsWith(QLatin1String("lyrics"))) {
                theirs << key;
            }
        }
        QCOMPARE(ours, theirs);
    }

    void errorsAreCappedPerSession() {  // TEL-07
        status = 500;
        auto run = reporter();
        run->begin(StartFields());
        for (int i = 0; i < Telemetry::kMaxErrorsPerSession + 3; ++i) {
            run->recordError(QStringLiteral("api"), QStringLiteral("http"), 502);
        }
        run->finish();
        QCOMPARE(run->queued(), 1 + Telemetry::kMaxErrorsPerSession + 1);
        status = 204;
        auto next = reporter();
        run.reset();
        next->begin(StartFields());
        QTRY_VERIFY(next->queued() == 0 && !next->isSending());
        bool sawStatus = false;
        bool sawDropped = false;
        for (const Tests::MockRequest& request : server->requests()) {
            for (const QJsonValue& value : Events(request)) {
                const QJsonObject event = value.toObject();
                sawStatus |= event.value(QStringLiteral("httpStatus")).toInt() == 502;
                sawDropped |= event.value(QStringLiteral("errorsDropped")).toInt() == 3;
            }
        }
        QVERIFY(sawStatus);
        QVERIFY(sawDropped);
    }

    void switchedOffItForgetsEverything() {  // TEL-02
        status = 500;
        const QString dead = QUuid::createUuid().toString(QUuid::WithoutBraces);
        writeDeadRun(dead, Marker(1), {});
        auto run = reporter();
        run->begin(StartFields());
        QTRY_VERIFY(posts() == 1 && !run->isSending());
        run->forget();
        Reporter::ForgetAll(directory->path());
        QCOMPARE(QDir(directory->path()).entryList(QDir::Files), QStringList());
        run->recordFeature(QStringLiteral("vis"), QStringLiteral("off"));
        run->send();
        QTest::qWait(100);
        QCOMPARE(posts(), 1);
    }

    void crashIsReportedByTheNextRun_data() {  // TEL-05
        QTest::addColumn<QString>("how");
        QTest::addColumn<QStringList>("expected");
        QTest::newRow("segv") << QStringLiteral("segv"
        ) << QStringList{"SIGSEGV", "SIGBUS", "EXCEPTION_ACCESS_VIOLATION"};
        QTest::newRow("terminate") << QStringLiteral("terminate") << QStringList{"TERMINATE", "CPP_EXCEPTION"};
        QTest::newRow("abort") << QStringLiteral("abort") << QStringList{"SIGABRT"};
    }

    void crashIsReportedByTheNextRun() {
        QFETCH(QString, how);
        QFETCH(QStringList, expected);
        const QString crashed = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QDir().mkpath(directory->path());
        QProcess probe;
        probe.start(
            QStringLiteral(QIYAA_CRASH_PROBE),
            {QDir(directory->path()).filePath(crashed + QStringLiteral(".crash")), crashed, QStringLiteral("0.4.9"), how
            }
        );
        QVERIFY(QTest::qWaitFor([&] { return probe.state() == QProcess::NotRunning; }, 30000));

        auto run = reporter();
        run->begin(StartFields());
        QTRY_COMPARE(posts(), 1);
        const QJsonArray events = Events(lastPost());
        QCOMPARE(Types(events), QStringList({"crash", "start"}));
        const QJsonObject crash = events.at(0).toObject();
        QCOMPARE(crash.value(QStringLiteral("session")).toString(), crashed);
        QCOMPARE(crash.value(QStringLiteral("version")).toString(), QStringLiteral("0.4.9"));
        const QString signal = crash.value(QStringLiteral("signal")).toString();
        QVERIFY2(expected.contains(signal), qPrintable(signal));
        const QJsonArray frames = crash.value(QStringLiteral("frames")).toArray();
        QVERIFY(!frames.isEmpty());
        const QRegularExpression frame(QStringLiteral("^[A-Za-z0-9._+-]{1,64}\\+0x[0-9a-f]{1,16}$"));
        bool inProbe = false;
        for (const QJsonValue& value : frames) {
            QVERIFY2(frame.match(value.toString()).hasMatch(), qPrintable(value.toString()));
            inProbe |= value.toString().startsWith(QLatin1String("crash_probe"));
        }
        QVERIFY2(inProbe, qPrintable(QJsonDocument(frames).toJson()));
        QVERIFY(sessionFiles(crashed).isEmpty());
    }

    void applicationSendsAndCanBeSwitchedOff() {  // TEL-02, TEL-03
        App::Application::Options options;
        options.offline = true;
        options.audio = false;
        options.mediaIntegration = false;
        options.settingsFile = directory->filePath(QStringLiteral("settings.ini"));
        options.network = network.get();
        options.telemetryUrl = QUrl(server->baseUrl() + kPath);
        App::Application application(options);
        application.start();
        QVERIFY(application.telemetry()->available());
        QVERIFY(application.telemetry()->reporter());
        QTRY_COMPARE(posts(), 1);
        const QJsonObject start = Events(lastPost()).at(0).toObject();
        QCOMPARE(start.value(QStringLiteral("type")).toString(), QStringLiteral("start"));
        QCOMPARE(start.value(QStringLiteral("skin")).toString(), QStringLiteral("base"));
        QCOMPARE(start.value(QStringLiteral("firstRun")).toBool(), true);

        application.telemetry()->setEnabled(false);
        QVERIFY(!application.telemetry()->reporter());
        QCOMPARE(QDir(directory->filePath(QStringLiteral("telemetry"))).entryList(QDir::Files), QStringList());
        application.telemetry()->setEnabled(true);
        QVERIFY(application.telemetry()->reporter());
        QTRY_COMPARE(posts(), 2);
    }

    void spotifyLoginsAndFailuresAreSentAsCodes() {  // TEL-18
        App::Application::Options options;
        options.offline = true;
        options.audio = false;
        options.mediaIntegration = false;
        options.settingsFile = directory->filePath(QStringLiteral("settings.ini"));
        options.network = network.get();
        options.telemetryUrl = QUrl(server->baseUrl() + kPath);
        App::Application application(options);
        application.start();
        QTRY_COMPARE(posts(), 1);
        QVERIFY(Events(lastPost()).at(0).toObject().value(QStringLiteral("spotifyLoggedIn")).isBool());
        Q_EMIT application.spotify()->loggedInWith(App::SpotifyAccount::Way::Browser);
        Q_EMIT application.spotify()->loggedOut();
        Q_EMIT application.spotify()->refused(App::SpotifyAccount::Refusal::Expired);
        Q_EMIT application.spotify()->refused(App::SpotifyAccount::Refusal::PremiumRequired);
        Q_EMIT application.spotify()->refused(App::SpotifyAccount::Refusal::Other);
        Q_EMIT application.spotifySession()->keyFailed(true);
        Q_EMIT application.spotifySession()->keyFailed(false);
        Q_EMIT application.spotifySession()->accessPointFailed();
        application.spotifyApi()->report(SpotifyClient::WebProblem::Http, 503);
        application.spotifyApi()->report(SpotifyClient::WebProblem::Content);
        application.spotifyApi()->report(SpotifyClient::WebProblem::QueryOutdated);
        application.telemetry()->reporter()->send();
        QTRY_COMPARE(posts(), 2);
        QStringList features;
        QStringList errors;
        for (const QJsonValue& value : Events(lastPost())) {
            const QJsonObject event = value.toObject();
            if (event.value(QStringLiteral("name")).toString() == QLatin1String("spotify")) {
                features << event.value(QStringLiteral("value")).toString();
            }
            if (event.value(QStringLiteral("area")).toString() == QLatin1String("spotify")) {
                const int status = event.value(QStringLiteral("httpStatus")).toInt();
                errors << event.value(QStringLiteral("kind")).toString()
                        + (status > 0 ? QStringLiteral(":%1").arg(status) : QString());
            }
        }
        QCOMPARE(features, (QStringList{"login-browser", "logout"}));
        QCOMPARE(
            errors,
            (QStringList{
                "login-expired",
                "premium-required",
                "key-refused",
                "key-failed",
                "access-point",
                "http:503",
                "content",
                "query-outdated"
            })
        );
    }

    void milkdropShownByHandIsAFeatureAndMinimisingIsNot() {
        App::Application::Options options;
        options.offline = true;
        options.audio = false;
        options.mediaIntegration = false;
        options.settingsFile = directory->filePath(QStringLiteral("settings.ini"));
        options.network = network.get();
        options.telemetryUrl = QUrl(server->baseUrl() + kPath);
        App::Application application(options);
        application.start();
        if (!application.milkdropWindow()) {
            QSKIP("built without Milkdrop");
        }
        QTRY_COMPARE(posts(), 1);
        application.windows()->setVisible(application.milkdropWindow(), true);
        application.mainWindow()->minimizedChanged(true);
        application.mainWindow()->minimizedChanged(false);
        application.telemetry()->reporter()->send();
        QTRY_COMPARE(posts(), 2);
        QStringList milkdrop;
        for (const QJsonValue& event : Events(lastPost())) {
            const QJsonObject fields = event.toObject();
            if (fields.value(QStringLiteral("name")).toString() == QLatin1String("milkdrop")) {
                milkdrop << fields.value(QStringLiteral("value")).toString();
            }
        }
        QCOMPARE(milkdrop, QStringList{QStringLiteral("on")});
    }

    void theLyricsWindowAndServiceAreFeatures() {  // TEL-17
        App::Application::Options options;
        options.offline = true;
        options.audio = false;
        options.mediaIntegration = false;
        options.settingsFile = directory->filePath(QStringLiteral("settings.ini"));
        options.network = network.get();
        options.telemetryUrl = QUrl(server->baseUrl() + kPath);
        App::Application application(options);
        application.start();
        QTRY_COMPARE(posts(), 1);
        application.windows()->setVisible(application.lyricsWindow(), true);
        application.lyrics()->setThirdPartyEnabled(true);
        application.lyrics()->setThirdPartyEnabled(false);
        application.windows()->setVisible(application.lyricsWindow(), false);
        application.telemetry()->reporter()->send();
        QTRY_COMPARE(posts(), 2);
        QStringList features;
        for (const QJsonValue& event : Events(lastPost())) {
            const QJsonObject fields = event.toObject();
            if (fields.value(QStringLiteral("type")).toString() == QLatin1String("feature")) {
                features << fields.value(QStringLiteral("name")).toString() + u'='
                        + fields.value(QStringLiteral("value")).toString();
            }
        }
        QCOMPARE(features, (QStringList{"lyrics=on", "lyrics-services=on", "lyrics-services=off", "lyrics=off"}));
    }
};

QTEST_MAIN(TestTelemetry)
#include "telemetry_test.moc"
