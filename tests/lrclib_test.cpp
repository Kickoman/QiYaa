#include "core/lyrics.h"
#include "core/track.h"
#include "lrclib/search.h"
#include "lrclib/service.h"
#include "support/mock_http_server.h"
#include "support/spec_fixtures.h"

#include <QByteArray>
#include <QFile>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTest>
#include <QUrl>

#include <optional>
#include <utility>

namespace {

using Status = Core::LyricsAnswer::Status;

QByteArray SearchFixture(const QString& name) {
    QFile file(Tests::SpecPath(QStringLiteral("fixtures/lrclib/search/%1.json").arg(name)));
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QString KindOf(const Core::LyricsAnswer& answer) {
    switch (answer.status) {
        case Status::Found:
            return answer.lyrics.kind == Core::LyricsKind::Synced ? QStringLiteral("synced") : QStringLiteral("plain");
        case Status::Instrumental: return QStringLiteral("instrumental");
        case Status::Missing: return QStringLiteral("missing");
        case Status::Failed:
        case Status::NoAccount: break;
    }
    return QStringLiteral("other");
}

Core::Track Song(const QString& title, QStringList artists, qint64 durationMs = 215'000) {
    Core::Track track;
    track.key = {QStringLiteral("fake"), QStringLiteral("1")};
    track.title = title;
    track.artists = std::move(artists);
    track.durationMs = durationMs;
    return track;
}

}  // namespace

class TestLrclib : public QObject {
    Q_OBJECT
private:
    Tests::MockHttpServer server;
    Tests::LocalNetworkAccessManager network;

    struct Asked {
        std::optional<Core::LyricsAnswer> answer;
        QList<Tests::MockRequest> requests;
    };

    Asked ask(Lrclib::Service& service, const Core::Track& track) {
        const qsizetype from = server.requests().size();
        std::optional<Core::LyricsAnswer> answer;
        service.lyrics(track, [&](Core::LyricsAnswer reply) { answer = std::move(reply); });
        if (answer) {
            qWarning("the service answered inside lyrics()");
            return {};
        }
        QTest::qWaitFor([&] { return answer.has_value(); }, 10'000);
        return {answer, server.requests().mid(from)};
    }

private Q_SLOTS:
    void searchRepliesPickAsTheSpecSays_data() {  // LYR-04, LYR-05
        QTest::addColumn<QString>("name");
        QTest::addColumn<qint64>("durationMs");
        QTest::addColumn<QString>("kind");
        QTest::addColumn<QJsonValue>("id");
        for (const char* name : {"several-durations", "plain-only", "instrumental", "empty"}) {
            const QJsonObject expected = Tests::SpecObject(QStringLiteral("expected/lrclib/search/%1.json").arg(name));
            for (const QJsonValue& pick : expected[QStringLiteral("picks")].toArray()) {
                const qint64 durationMs = pick[QStringLiteral("durationMs")].toInteger();
                QTest::addRow("%s at %lld ms", name, static_cast<long long>(durationMs)) << QString::fromLatin1(name
                ) << durationMs << pick[QStringLiteral("kind")].toString() << pick[QStringLiteral("id")];
            }
        }
    }

    void searchRepliesPickAsTheSpecSays() {
        QFETCH(QString, name);
        QFETCH(qint64, durationMs);
        QFETCH(QString, kind);
        QFETCH(QJsonValue, id);
        const Lrclib::Choice choice = Lrclib::Pick(Lrclib::ParseSearch(SearchFixture(name)), durationMs);
        QCOMPARE(KindOf(choice.answer), kind);
        QCOMPARE(choice.answer.from, Core::LyricsFrom::ThirdParty);
        if (id.isNull()) {
            QVERIFY(!choice.recordId);
        } else {
            QCOMPARE(choice.recordId, std::optional<qint64>(id.toInteger()));
        }
    }

    void aPickedTextIsParsedWithItsTimes() {  // LYR-04
        const Lrclib::Choice choice =
            Lrclib::Pick(Lrclib::ParseSearch(SearchFixture(QStringLiteral("several-durations"))), 215'500);
        QCOMPARE(choice.answer.lyrics.kind, Core::LyricsKind::Synced);
        QCOMPARE(choice.answer.lyrics.lines.size(), 5);
        QCOMPARE(
            choice.answer.lyrics.lines[0], (Core::LyricsLine{12'400, QStringLiteral("Paper boats along the gutter")})
        );
        QCOMPARE(choice.answer.lyrics.lines[2], (Core::LyricsLine{23'100, QString()}));
    }

    void theVersionIsCutFromTheTitle_data() {
        QTest::addColumn<QString>("title");
        QTest::addColumn<QString>("shorter");
        QTest::newRow("round") << QStringLiteral("Paper Boats (Live)") << QStringLiteral("Paper Boats");
        QTest::newRow("square") << QStringLiteral("Paper Boats [2009 Remaster]") << QStringLiteral("Paper Boats");
        QTest::newRow("only the last") << QStringLiteral("A (B) (C)") << QStringLiteral("A (B)");
        QTest::newRow("none") << QStringLiteral("Paper Boats") << QString();
        QTest::newRow("the whole title") << QStringLiteral("(Intro)") << QString();
        QTest::newRow("not at the end") << QStringLiteral("Boats (Live) Again") << QString();
    }

    void theVersionIsCutFromTheTitle() {
        QFETCH(QString, title);
        QFETCH(QString, shorter);
        QCOMPARE(Lrclib::TitleWithoutVersion(title), shorter);
    }

    void theSearchNamesTheTitleAndTheFirstArtist() {  // LYR-04
        server.on("GET", QStringLiteral("/api/search"), [](const Tests::MockRequest&) {
            return Tests::MockResponse{200, SearchFixture(QStringLiteral("several-durations"))};
        });
        Lrclib::Service service(&network, server.baseUrl(), QStringLiteral("9.9.9"));
        const Asked asked = ask(
            service,
            Song(QStringLiteral("Paper Boats & Co + 1"), {QStringLiteral("The Example Band"), QStringLiteral("Guest")})
        );
        QVERIFY(asked.answer);
        QCOMPARE(KindOf(*asked.answer), QStringLiteral("synced"));
        QCOMPARE(asked.requests.size(), 1);
        const Tests::MockRequest& request = asked.requests.front();
        QCOMPARE(
            request.query.queryItemValue(QStringLiteral("track_name"), QUrl::FullyDecoded),
            QStringLiteral("Paper Boats & Co + 1")
        );
        QCOMPARE(
            request.query.queryItemValue(QStringLiteral("artist_name"), QUrl::FullyDecoded),
            QStringLiteral("The Example Band")
        );
        QCOMPARE(request.headers.value("user-agent"), QByteArray("QiYaa/9.9.9 (https://github.com/Kickoman/QiYaa)"));
    }

    void withoutATitleOrAnArtistNothingIsAsked() {  // LYR-04
        Lrclib::Service service(&network, server.baseUrl(), QStringLiteral("1.0.0"));
        Asked asked = ask(service, Song(QStringLiteral("Song"), {}));
        QVERIFY(asked.answer);
        QCOMPARE(asked.answer->status, Status::Missing);
        asked = ask(service, Song(QStringLiteral("  "), {QStringLiteral("Band")}));
        QVERIFY(asked.answer);
        QCOMPARE(asked.answer->status, Status::Missing);
        QVERIFY(asked.requests.isEmpty());
    }

    void aServerErrorIsTriedAgainOnce() {  // LYR-04
        int calls = 0;
        server.on("GET", QStringLiteral("/api/search"), [&calls](const Tests::MockRequest&) {
            return ++calls == 1
                ? Tests::MockResponse{503, SearchFixture(QStringLiteral("503-server-overloaded"))}
                : Tests::MockResponse{200, SearchFixture(QStringLiteral("plain-only"))};
        });
        Lrclib::Service service(&network, server.baseUrl(), QStringLiteral("1.0.0"));
        service.setRetryDelayMs(10);
        const Asked asked = ask(service, Song(QStringLiteral("Paper Boats"), {QStringLiteral("The Example Band")}));
        QVERIFY(asked.answer);
        QCOMPARE(KindOf(*asked.answer), QStringLiteral("plain"));
        QCOMPARE(asked.requests.size(), 2);
        QVERIFY(!asked.answer->serverError);
    }

    void aServerThatKeepsFailingFindsNothingAndSaysSo() {  // LYR-04
        server.on("GET", QStringLiteral("/api/search"), [](const Tests::MockRequest&) {
            return Tests::MockResponse{503, SearchFixture(QStringLiteral("503-server-overloaded"))};
        });
        Lrclib::Service service(&network, server.baseUrl(), QStringLiteral("1.0.0"));
        service.setRetryDelayMs(10);
        Asked asked = ask(service, Song(QStringLiteral("Paper Boats"), {QStringLiteral("The Example Band")}));
        QVERIFY(asked.answer);
        QCOMPARE(asked.answer->status, Status::Missing);
        QVERIFY(asked.answer->serverError);
        QCOMPARE(asked.requests.size(), 2);
        asked = ask(service, Song(QStringLiteral("Paper Boats (Live)"), {QStringLiteral("The Example Band")}));
        QCOMPARE(asked.requests.size(), 4);
        QCOMPARE(asked.requests[2].query.queryItemValue(QStringLiteral("track_name")), QStringLiteral("Paper Boats"));
    }

    void nothingFoundTriesTheTitleWithoutItsVersion() {  // LYR-04
        server.on("GET", QStringLiteral("/api/search"), [](const Tests::MockRequest& request) {
            const bool full =
                request.query.queryItemValue(QStringLiteral("track_name"), QUrl::FullyDecoded).contains(u'(');
            return Tests::MockResponse{
                200, SearchFixture(full ? QStringLiteral("empty") : QStringLiteral("several-durations"))
            };
        });
        Lrclib::Service service(&network, server.baseUrl(), QStringLiteral("1.0.0"));
        const Asked asked =
            ask(service, Song(QStringLiteral("Paper Boats (Live)"), {QStringLiteral("The Example Band")}));
        QVERIFY(asked.answer);
        QCOMPARE(KindOf(*asked.answer), QStringLiteral("synced"));
        QCOMPARE(asked.requests.size(), 2);
    }

    void aReplyCutOffIsTriedAgainAndThenFails() {  // LYR-04, LYR-17
        server.on("GET", QStringLiteral("/api/search"), [](const Tests::MockRequest&) {
            return Tests::MockResponse{200, SearchFixture(QStringLiteral("several-durations")), 0, 100};
        });
        Lrclib::Service service(&network, server.baseUrl(), QStringLiteral("1.0.0"));
        service.setRetryDelayMs(10);
        const Asked asked =
            ask(service, Song(QStringLiteral("Paper Boats (Live)"), {QStringLiteral("The Example Band")}));
        QVERIFY(asked.answer);
        QCOMPARE(asked.answer->status, Status::Failed);
        QCOMPARE(asked.requests.size(), 2);
    }

    void aServerErrorOtherThan503IsAFailure() {  // LYR-04
        server.on("GET", QStringLiteral("/api/search"), [](const Tests::MockRequest&) {
            return Tests::MockResponse{500, "{}"};
        });
        Lrclib::Service service(&network, server.baseUrl(), QStringLiteral("1.0.0"));
        service.setRetryDelayMs(10);
        const Asked asked =
            ask(service, Song(QStringLiteral("Paper Boats (Live)"), {QStringLiteral("The Example Band")}));
        QVERIFY(asked.answer);
        QCOMPARE(asked.answer->status, Status::Failed);
        QVERIFY(!asked.answer->serverError);
        QCOMPARE(asked.requests.size(), 2);
    }

    void aNetworkFailureIsAFailureAfterItsRetry() {  // LYR-04, LYR-17
        Lrclib::Service service(&network, QStringLiteral("http://127.0.0.1:1"), QStringLiteral("1.0.0"));
        service.setRetryDelayMs(10);
        const Asked asked = ask(service, Song(QStringLiteral("Paper Boats"), {QStringLiteral("The Example Band")}));
        QVERIFY(asked.answer);
        QCOMPARE(asked.answer->status, Status::Failed);
    }

    void aReplyOverTheLimitIsAFailure() {  // LYR-11
        server.on("GET", QStringLiteral("/api/search"), [](const Tests::MockRequest&) {
            return Tests::MockResponse{200, "[" + QByteArray(Lrclib::kMaxReplyBytes, ' ') + "]"};
        });
        Lrclib::Service service(&network, server.baseUrl(), QStringLiteral("1.0.0"));
        const Asked asked = ask(service, Song(QStringLiteral("Paper Boats"), {QStringLiteral("The Example Band")}));
        QVERIFY(asked.answer);
        QCOMPARE(asked.answer->status, Status::Failed);
        QCOMPARE(asked.requests.size(), 1);
    }

    void aCancelledOrReplacedRequestNeverAnswers() {  // LYR-09, LYR-15
        server.on("GET", QStringLiteral("/api/search"), [](const Tests::MockRequest&) {
            return Tests::MockResponse{200, SearchFixture(QStringLiteral("several-durations")), 100};
        });
        Lrclib::Service service(&network, server.baseUrl(), QStringLiteral("1.0.0"));
        int answers = 0;
        service.lyrics(
            Song(QStringLiteral("Paper Boats"), {QStringLiteral("The Example Band")}),
            [&](Core::LyricsAnswer) { ++answers; }
        );
        service.cancel();
        service.lyrics(
            Song(QStringLiteral("Paper Boats"), {QStringLiteral("The Example Band")}),
            [&](Core::LyricsAnswer) { answers += 10; }
        );
        std::optional<Core::LyricsAnswer> last;
        service.lyrics(
            Song(QStringLiteral("Paper Boats"), {QStringLiteral("The Example Band")}),
            [&](Core::LyricsAnswer reply) { last = std::move(reply); }
        );
        QTRY_VERIFY(last);
        QTest::qWait(200);
        QCOMPARE(answers, 0);
        service.lyrics(Song(QStringLiteral("Paper Boats"), {}), [&](Core::LyricsAnswer) { ++answers; });
        service.cancel();
        QTest::qWait(50);
        QCOMPARE(answers, 0);
    }
};

QTEST_GUILESS_MAIN(TestLrclib)
#include "lrclib_test.moc"
