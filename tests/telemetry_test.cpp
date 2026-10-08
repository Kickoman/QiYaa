#include "app/application.h"
#include "support/mock_http_server.h"
#include "support/spec_fixtures.h"
#include "telemetry/machine_id.h"
#include "telemetry/reporter.h"
#include "telemetry/system_info.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
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
    return QJsonDocument(QJsonObject{
                             {QStringLiteral("version"), QStringLiteral("0.4.9")},
                             {QStringLiteral("seconds"), seconds},
                         })
        .toJson(QJsonDocument::Compact);
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
            QUrl(server->baseUrl() + kPath), directory->path(), QStringLiteral("0.5.0"), kMachine,
            network.get()
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
        server->on("POST", kPath, [this](const Tests::MockRequest&) {
            return Tests::MockResponse{status, {}};
        });
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
        QSettings settings(
            directory->filePath(QStringLiteral("settings.ini")), QSettings::IniFormat
        );
        const QString id = Telemetry::MachineIdFor(settings);
        QVERIFY(QRegularExpression(QStringLiteral("^[0-9a-f]{32}$")).match(id).hasMatch());
        QCOMPARE(Telemetry::MachineIdFor(settings), id);
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
        writeDeadRun(
            dead, Marker(420), QJsonDocument(feature).toJson(QJsonDocument::Compact) + '\n'
        );
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
        QTest::newRow("segv") << QStringLiteral("segv")
                              << QStringList{"SIGSEGV", "SIGBUS", "EXCEPTION_ACCESS_VIOLATION"};
        QTest::newRow("terminate")
            << QStringLiteral("terminate") << QStringList{"TERMINATE", "CPP_EXCEPTION"};
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
            {QDir(directory->path()).filePath(crashed + QStringLiteral(".crash")), crashed,
             QStringLiteral("0.4.9"), how}
        );
        QVERIFY(probe.waitForFinished(30000));

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
        const QRegularExpression frame(QStringLiteral("^[A-Za-z0-9._+-]{1,64}\\+0x[0-9a-f]{1,16}$")
        );
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
        QVERIFY(application.telemetry());
        QTRY_COMPARE(posts(), 1);
        const QJsonObject start = Events(lastPost()).at(0).toObject();
        QCOMPARE(start.value(QStringLiteral("type")).toString(), QStringLiteral("start"));
        QCOMPARE(start.value(QStringLiteral("skin")).toString(), QStringLiteral("base"));
        QCOMPARE(start.value(QStringLiteral("firstRun")).toBool(), true);

        application.setTelemetryEnabled(false);
        QVERIFY(!application.telemetry());
        QCOMPARE(
            QDir(directory->filePath(QStringLiteral("telemetry"))).entryList(QDir::Files),
            QStringList()
        );
        application.setTelemetryEnabled(true);
        QVERIFY(application.telemetry());
        QTRY_COMPARE(posts(), 2);
    }
};

QTEST_MAIN(TestTelemetry)
#include "telemetry_test.moc"
