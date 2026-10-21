#include "audio/audio_engine.h"
#include "core/lyrics.h"
#include "core/lyrics_finder.h"
#include "core/player.h"
#include "core/track.h"
#include "core/track_source.h"
#include "support/fake_source.h"
#include "support/spec_fixtures.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTest>

#include <memory>
#include <optional>
#include <utility>

namespace {

const QString kFake = QStringLiteral("fake");

using Status = Core::LyricsAnswer::Status;
using Phase = Core::LyricsFinder::Phase;
using Shown = Core::LyricsLookup::Shown;

QList<Core::Track> Tracks(int count, int first = 1) {
    QList<Core::Track> tracks;
    for (int i = first; i < first + count; ++i) {
        Core::Track track;
        track.key = {kFake, QString::number(i)};
        track.title = QStringLiteral("Track %1").arg(i);
        track.artists = {QStringLiteral("Artist")};
        track.durationMs = 3'000;
        tracks << track;
    }
    return tracks;
}

Core::LyricsAnswer Synced(const QString& text, Core::LyricsFrom from = Core::LyricsFrom::Catalog) {
    return {.status = Status::Found, .lyrics = {Core::LyricsKind::Synced, {{1000, text}}}, .from = from};
}

Core::LyricsAnswer Plain(const QString& text, Core::LyricsFrom from = Core::LyricsFrom::Catalog) {
    return {.status = Status::Found, .lyrics = {Core::LyricsKind::Plain, {{-1, text}}}, .from = from};
}

Core::LyricsAnswer Answer(Status status) {
    return {.status = status};
}

class FakeService : public Core::LyricsService {
public:
    void lyrics(const Core::Track& track, Core::TLyricsDone done) override {
        asked << track.key.id;
        pending = std::move(done);
    }

    void cancel() override {
        ++cancels;
        pending = {};
    }

    void answer(Core::LyricsAnswer reply) { std::exchange(pending, {})(std::move(reply)); }

    QStringList asked;
    Core::TLyricsDone pending;
    int cancels = 0;
};

struct Stack {
    Audio::AudioEngine engine;
    Tests::FakeSource source;
    Core::SourceRegistry registry;
    Core::Player player{&engine, &registry};
    FakeService service;
    Core::LyricsFinder finder{&player, &registry, &service};
    QList<Core::LyricsLookup> lookups;

    Stack() {
        registry.add(kFake, &source);
        engine.setVolume(0);
        finder.setWaits(60'000, 60'000);
        QObject::connect(&finder, &Core::LyricsFinder::lookedUp, [this](const Core::LyricsLookup& lookup) {
            lookups << lookup;
        });
    }

    bool start(int tracks) {
        if (!engine.init().ok) {
            return false;
        }
        player.setSource(Core::QueueSource{.title = QStringLiteral("Fake"), .head = Tracks(tracks)}, false);
        return true;
    }

    QString shownText() const {
        const Core::LyricsFinder::View& view = finder.view();
        return view.phase == Phase::Shown && !view.lyrics.lines.isEmpty() ? view.lyrics.lines.front().text : QString();
    }

    QString shownTrack() const { return finder.view().track ? finder.view().track->key.id : QString(); }
};

QList<Core::LyricsLine> LinesOf(const QJsonArray& lines) {
    QList<Core::LyricsLine> out;
    for (const QJsonValue& line : lines) {
        const QJsonValue ms = line[QStringLiteral("ms")];
        out << Core::LyricsLine{ms.isNull() ? -1 : ms.toInteger(), line[QStringLiteral("text")].toString()};
    }
    return out;
}

}  // namespace

class TestLyrics : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void textsParseAsTheSpecsVectorsSay_data() {  // LYR-12
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("kind");
        QTest::addColumn<QJsonArray>("lines");
        const QJsonObject vectors = Tests::SpecObject(QStringLiteral("lyrics/lrc.json"));
        const QJsonObject limits = vectors[QStringLiteral("limits")].toObject();
        QCOMPARE(limits[QStringLiteral("maxBytes")].toInteger(), Core::kMaxLyricsBytes);
        QCOMPARE(limits[QStringLiteral("maxLines")].toInteger(), Core::kMaxLyricsLines);
        QCOMPARE(limits[QStringLiteral("maxLineCharacters")].toInteger(), Core::kMaxLyricsLineCharacters);
        for (const QJsonValue& value : vectors[QStringLiteral("cases")].toArray()) {
            const QJsonObject vector = value.toObject();
            QTest::newRow(qPrintable(vector[QStringLiteral("name")].toString())
            ) << vector[QStringLiteral("text")].toString()
              << vector[QStringLiteral("kind")].toString() << vector[QStringLiteral("lines")].toArray();
        }
    }

    void textsParseAsTheSpecsVectorsSay() {
        QFETCH(QString, text);
        QFETCH(QString, kind);
        QFETCH(QJsonArray, lines);
        const std::optional<Core::Lyrics> lyrics = Core::ParseLyricsText(text);
        if (kind.isEmpty()) {
            QVERIFY(!lyrics);
            return;
        }
        QVERIFY(lyrics);
        QCOMPARE(lyrics->kind, kind == u"synced" ? Core::LyricsKind::Synced : Core::LyricsKind::Plain);
        QCOMPARE(lyrics->lines, LinesOf(lines));
    }

    void aTextOverTheLimitIsNotTaken() {  // LYR-11
        const QString limit = QString(Core::kMaxLyricsBytes - 10, u'a');
        QVERIFY(Core::ParseLyricsText(QStringLiteral("[00:01.00]") + limit));
        QVERIFY(!Core::ParseLyricsText(QStringLiteral("[00:01.00]x") + limit));
        QVERIFY(!Core::ParseLyricsText(QString(Core::kMaxLyricsBytes / 2 + 1, QChar(0x0439))));
    }

    void onlyTheFirstLinesAreKept() {  // LYR-11
        QStringList synced;
        QStringList plain;
        for (int i = 0; i < Core::kMaxLyricsLines + 5; ++i) {
            synced << QStringLiteral("[%1:00.00]line %2").arg(i).arg(i);
            plain << QStringLiteral("line %1").arg(i);
        }
        const std::optional<Core::Lyrics> timed = Core::ParseLyricsText(synced.join(u'\n'));
        QVERIFY(timed);
        QCOMPARE(timed->lines.size(), Core::kMaxLyricsLines);
        QCOMPARE(timed->lines.back().text, QStringLiteral("line %1").arg(Core::kMaxLyricsLines - 1));
        const std::optional<Core::Lyrics> untimed = Core::ParseLyricsText(plain.join(u'\n'));
        QVERIFY(untimed);
        QCOMPARE(untimed->lines.size(), Core::kMaxLyricsLines);
    }

    void anOffsetOutOfRangeIsHeldToADay() {  // LYR-12
        const std::optional<Core::Lyrics> back = Core::ParseLyricsText(u"[offset:-9223372036854775807]\n[00:01.00]x");
        QVERIFY(back);
        QCOMPARE(back->lines.front().ms, 1000 + 24LL * 60 * 60 * 1000);
        const std::optional<Core::Lyrics> ahead = Core::ParseLyricsText(u"[offset:+9223372036854775807]\n[00:01.00]x");
        QVERIFY(ahead);
        QCOMPARE(ahead->lines.front().ms, 0);
    }

    void aPlainTextCutAtItsLimitEndsWithWords() {  // LYR-11
        QStringList lines;
        for (int i = 0; i < Core::kMaxLyricsLines - 1; ++i) {
            lines << QStringLiteral("line %1").arg(i);
        }
        lines << QString() << QStringLiteral("past the limit");
        const std::optional<Core::Lyrics> lyrics = Core::ParseLyricsText(lines.join(u'\n'));
        QVERIFY(lyrics);
        QCOMPARE(lyrics->lines.size(), Core::kMaxLyricsLines - 1);
        QVERIFY(!lyrics->lines.back().text.isEmpty());
    }

    void aPlainTextStaysPlainWhateverItHolds() {
        const std::optional<Core::Lyrics> lyrics = Core::PlainLyrics(u"[00:01.00]not a time\n\n\nnext");
        QVERIFY(lyrics);
        QCOMPARE(lyrics->kind, Core::LyricsKind::Plain);
        QCOMPARE(
            lyrics->lines,
            (QList<Core::LyricsLine>{{-1, QStringLiteral("[00:01.00]not a time")}, {}, {-1, QStringLiteral("next")}})
        );
        QVERIFY(!Core::PlainLyrics(u" \n\t"));
    }

    void aClosedViewAsksNothing() {  // LYR-01
        Stack stack;
        QVERIFY(stack.start(3));
        stack.finder.setThirdPartyEnabled(true);
        stack.player.playIndex(0);
        stack.player.playIndex(1);
        QVERIFY(stack.source.lyricsAsked().isEmpty());
        QVERIFY(stack.service.asked.isEmpty());
        QCOMPARE(stack.finder.view().phase, Phase::Idle);
        stack.finder.setActive(true);
        QCOMPARE(stack.source.lyricsAsked(), QStringList{"2"});
        QCOMPARE(stack.finder.view().phase, Phase::Loading);
        stack.finder.setActive(false);
        stack.player.playIndex(2);
        QCOMPARE(stack.source.lyricsAsked(), QStringList{"2"});
        QCOMPARE(stack.finder.view().phase, Phase::Idle);
    }

    void theSourcesTextShowsAndTheServiceStaysOff() {  // LYR-02
        Stack stack;
        QVERIFY(stack.start(1));
        stack.finder.setActive(true);
        stack.source.answerLyrics(0, Plain(QStringLiteral("words")));
        QCOMPARE(stack.finder.view().phase, Phase::Shown);
        QCOMPARE(stack.shownText(), QStringLiteral("words"));
        QCOMPARE(stack.finder.view().from, Core::LyricsFrom::Catalog);
        QVERIFY(!stack.finder.view().better);
        QVERIFY(stack.service.asked.isEmpty());
    }

    void theServiceIsAskedOnlyWithoutASyncedText() {  // LYR-03
        Stack stack;
        QVERIFY(stack.start(2));
        stack.finder.setThirdPartyEnabled(true);
        stack.finder.setActive(true);
        QVERIFY(stack.service.asked.isEmpty());
        stack.source.answerLyrics(0, Synced(QStringLiteral("own")));
        QVERIFY(stack.service.asked.isEmpty());
        QCOMPARE(stack.shownText(), QStringLiteral("own"));

        stack.player.playIndex(1);
        stack.source.answerLyrics(1, Plain(QStringLiteral("plain")));
        QCOMPARE(stack.service.asked, QStringList{"2"});
        QCOMPARE(stack.shownText(), QStringLiteral("plain"));
        QVERIFY(stack.finder.view().better);
        stack.service.answer(Synced(QStringLiteral("timed")));
        QCOMPARE(stack.shownText(), QStringLiteral("timed"));
        QCOMPARE(stack.finder.view().from, Core::LyricsFrom::ThirdParty);
        QVERIFY(!stack.finder.view().better);
    }

    void theFirstTextAvailableShowsInTheOrder_data() {  // LYR-03
        QTest::addColumn<int>("source");
        QTest::addColumn<int>("service");
        QTest::addColumn<QString>("shown");
        enum { None, PlainText, SyncedText };
        QTest::newRow("source synced") << int(SyncedText) << int(None) << QStringLiteral("source synced");
        QTest::newRow("service synced over source plain") << int(PlainText
        ) << int(SyncedText) << QStringLiteral("service synced");
        QTest::newRow("source plain over service plain") << int(PlainText
        ) << int(PlainText) << QStringLiteral("source plain");
        QTest::newRow("service plain") << int(None) << int(PlainText) << QStringLiteral("service plain");
        QTest::newRow("service synced alone") << int(None) << int(SyncedText) << QStringLiteral("service synced");
        QTest::newRow("nothing") << int(None) << int(None) << QString();
    }

    void theFirstTextAvailableShowsInTheOrder() {
        QFETCH(int, source);
        QFETCH(int, service);
        QFETCH(QString, shown);
        const auto answerOf = [](int kind, const QString& who) {
            switch (kind) {
                case 1: return Plain(who + QStringLiteral(" plain"));
                case 2: return Synced(who + QStringLiteral(" synced"));
                default: return Answer(Status::Missing);
            }
        };
        Stack stack;
        QVERIFY(stack.start(1));
        stack.finder.setThirdPartyEnabled(true);
        stack.finder.setActive(true);
        stack.source.answerLyrics(0, answerOf(source, QStringLiteral("source")));
        if (stack.service.pending) {
            stack.service.answer(answerOf(service, QStringLiteral("service")));
        }
        QCOMPARE(stack.shownText(), shown);
        QCOMPARE(stack.finder.view().phase, shown.isEmpty() ? Phase::Missing : Phase::Shown);
    }

    void aSilentSourceLetsTheServiceAskAndTheWaitsEnd() {  // LYR-03
        Stack stack;
        QVERIFY(stack.start(1));
        stack.finder.setWaits(200, 20);
        stack.finder.setThirdPartyEnabled(true);
        stack.finder.setActive(true);
        QVERIFY(stack.service.asked.isEmpty());
        QTRY_COMPARE(stack.service.asked, QStringList{"1"});
        QCOMPARE(stack.finder.view().phase, Phase::Loading);
        QTRY_COMPARE(stack.finder.view().phase, Phase::Missing);
        QCOMPARE(stack.service.cancels, 1);
        stack.source.answerLyrics(0, Synced(QStringLiteral("too late")));
        QCOMPARE(stack.finder.view().phase, Phase::Missing);
    }

    void aSyncedSourceTextCancelsTheService() {  // LYR-03
        Stack stack;
        QVERIFY(stack.start(1));
        stack.finder.setWaits(60'000, 0);
        stack.finder.setThirdPartyEnabled(true);
        stack.finder.setActive(true);
        QTRY_COMPARE(stack.service.asked, QStringList{"1"});
        stack.source.answerLyrics(0, Synced(QStringLiteral("own")));
        QCOMPARE(stack.service.cancels, 1);
        QCOMPARE(stack.shownText(), QStringLiteral("own"));
        QCOMPARE(stack.lookups.size(), 1);
    }

    void instrumentalOnlyWhenTheSourceHasNoText() {  // LYR-05
        Stack stack;
        QVERIFY(stack.start(2));
        stack.finder.setThirdPartyEnabled(true);
        stack.finder.setActive(true);
        stack.source.answerLyrics(0, Answer(Status::Missing));
        stack.service.answer(Answer(Status::Instrumental));
        QCOMPARE(stack.finder.view().phase, Phase::Instrumental);
        stack.player.playIndex(1);
        stack.source.answerLyrics(1, Plain(QStringLiteral("words")));
        stack.service.answer(Answer(Status::Instrumental));
        QCOMPARE(stack.finder.view().phase, Phase::Shown);
        QCOMPARE(stack.shownText(), QStringLiteral("words"));
    }

    void answersForAnEarlierTrackAreDropped() {  // LYR-09
        Stack stack;
        QVERIFY(stack.start(2));
        stack.finder.setThirdPartyEnabled(true);
        stack.finder.setActive(true);
        stack.source.answerLyrics(0, Answer(Status::Missing));
        QCOMPARE(stack.service.asked, QStringList{"1"});
        stack.player.playIndex(1);
        QCOMPARE(stack.service.cancels, 1);
        QCOMPARE(stack.shownTrack(), QStringLiteral("2"));
        QCOMPARE(stack.finder.view().phase, Phase::Loading);
        QCOMPARE(stack.source.lyricsAsked(), (QStringList{"1", "2"}));
        stack.source.answerLyrics(1, Plain(QStringLiteral("second")));
        QCOMPARE(stack.shownText(), QStringLiteral("second"));
    }

    void aLateAnswerForTheSameTrackAfterAReopenIsDropped() {  // LYR-09
        Stack stack;
        QVERIFY(stack.start(1));
        stack.finder.setActive(true);
        stack.finder.setActive(false);
        stack.finder.setActive(true);
        QCOMPARE(stack.source.lyricsAsked(), (QStringList{"1", "1"}));
        stack.source.answerLyrics(0, Plain(QStringLiteral("stale")));
        QCOMPARE(stack.finder.view().phase, Phase::Loading);
        stack.source.answerLyrics(1, Plain(QStringLiteral("fresh")));
        QCOMPARE(stack.shownText(), QStringLiteral("fresh"));
    }

    void anAnswerAfterTheFinderIsGoneIsDropped() {
        Stack stack;
        QVERIFY(stack.start(1));
        auto finder = std::make_unique<Core::LyricsFinder>(&stack.player, &stack.registry, nullptr);
        finder->setActive(true);
        QCOMPARE(stack.source.lyricsAsked(), (QStringList{"1"}));
        finder.reset();
        stack.source.answerLyrics(0, Plain(QStringLiteral("nobody listens")));
    }

    void theSameTrackIsAskedOnce() {
        Stack stack;
        QVERIFY(stack.start(2));
        stack.finder.setActive(true);
        stack.player.playIndex(0);
        Q_EMIT stack.player.currentTrackChanged();
        Q_EMIT stack.player.playlistChanged();
        QCOMPARE(stack.source.lyricsAsked(), QStringList{"1"});
    }

    void aCursorMovedWithoutATrackChangeIsFollowed() {
        Stack stack;
        QVERIFY(stack.start(2));
        stack.finder.setActive(true);
        stack.player.blockSignals(true);
        stack.player.playIndex(1);
        stack.player.blockSignals(false);
        QCOMPARE(stack.source.lyricsAsked(), QStringList{"1"});
        Q_EMIT stack.player.playlistChanged();
        QCOMPARE(stack.source.lyricsAsked(), (QStringList{"1", "2"}));
    }

    void answersAreRememberedForTenTracks() {  // LYR-14
        Stack stack;
        QVERIFY(stack.start(12));
        stack.finder.setActive(true);
        for (int i = 0; i < 11; ++i) {
            stack.player.playIndex(i);
            stack.source.answerLyrics(i, Plain(QStringLiteral("text %1").arg(i + 1)));
        }
        stack.player.playIndex(10);
        stack.player.playIndex(1);
        QCOMPARE(stack.source.lyricsAsked().size(), 11);
        QCOMPARE(stack.shownText(), QStringLiteral("text 2"));
        stack.player.playIndex(0);
        QCOMPARE(stack.source.lyricsAsked().size(), 12);
        QCOMPARE(stack.finder.view().phase, Phase::Loading);
    }

    void failuresAndAMissingLoginAreNotRemembered() {  // LYR-14, LYR-16
        Stack stack;
        QVERIFY(stack.start(3));
        stack.finder.setActive(true);
        stack.source.answerLyrics(0, Answer(Status::Failed));
        QCOMPARE(stack.finder.view().phase, Phase::Failed);
        stack.player.playIndex(1);
        stack.source.answerLyrics(1, Answer(Status::NoAccount));
        QCOMPARE(stack.finder.view().phase, Phase::NoAccount);
        stack.player.playIndex(2);
        stack.source.answerLyrics(2, Answer(Status::Missing));
        stack.player.playIndex(0);
        stack.player.playIndex(1);
        stack.player.playIndex(2);
        QCOMPARE(stack.source.lyricsAsked(), (QStringList{"1", "2", "3", "1", "2"}));
    }

    void forgettingAsksAgain() {  // LYR-14
        Stack stack;
        QVERIFY(stack.start(1));
        stack.finder.setActive(true);
        stack.source.answerLyrics(0, Answer(Status::NoAccount));
        stack.finder.forget();
        QCOMPARE(stack.source.lyricsAsked(), (QStringList{"1", "1"}));
        QCOMPARE(stack.finder.view().phase, Phase::Loading);
    }

    void turningTheServiceOnAndOff() {  // LYR-15
        Stack stack;
        QVERIFY(stack.start(1));
        stack.finder.setActive(true);
        stack.source.answerLyrics(0, Plain(QStringLiteral("plain")));
        QVERIFY(stack.service.asked.isEmpty());
        stack.finder.setThirdPartyEnabled(true);
        QCOMPARE(stack.service.asked, QStringList{"1"});
        stack.service.answer(Synced(QStringLiteral("timed")));
        QCOMPARE(stack.shownText(), QStringLiteral("timed"));
        stack.finder.setThirdPartyEnabled(false);
        QCOMPARE(stack.shownText(), QStringLiteral("plain"));
        stack.finder.setThirdPartyEnabled(true);
        QCOMPARE(stack.service.asked, QStringList{"1"});
        QCOMPARE(stack.shownText(), QStringLiteral("timed"));
        stack.finder.setThirdPartyEnabled(false);
        stack.player.playIndex(0);
        QCOMPARE(stack.source.lyricsAsked(), QStringList{"1"});
    }

    void turningTheServiceOffCancelsItsRequest() {  // LYR-15
        Stack stack;
        QVERIFY(stack.start(1));
        stack.finder.setThirdPartyEnabled(true);
        stack.finder.setActive(true);
        stack.source.answerLyrics(0, Answer(Status::Missing));
        QVERIFY(stack.service.pending);
        stack.finder.setThirdPartyEnabled(false);
        QCOMPARE(stack.service.cancels, 1);
        QCOMPARE(stack.finder.view().phase, Phase::Missing);
    }

    void aFailedSourceIsAskedAgainWhenTheNetworkIsBack() {  // LYR-17
        Stack stack;
        QVERIFY(stack.start(1));
        stack.finder.setActive(true);
        stack.finder.retryFailed();
        QCOMPARE(stack.source.lyricsAsked(), QStringList{"1"});
        stack.source.answerLyrics(0, Answer(Status::Failed));
        QCOMPARE(stack.finder.view().phase, Phase::Failed);
        stack.finder.retryFailed();
        QCOMPARE(stack.source.lyricsAsked(), (QStringList{"1", "1"}));
        QCOMPARE(stack.finder.view().phase, Phase::Loading);
        stack.source.answerLyrics(1, Plain(QStringLiteral("back")));
        QCOMPARE(stack.shownText(), QStringLiteral("back"));
    }

    void aRememberedServerErrorIsNotCountedAgain() {  // TEL-17
        Stack stack;
        QVERIFY(stack.start(2));
        stack.finder.setThirdPartyEnabled(true);
        stack.finder.setActive(true);
        stack.source.answerLyrics(0, Answer(Status::Failed));
        Core::LyricsAnswer busy;
        busy.serverError = true;
        stack.service.answer(busy);
        QCOMPARE(stack.lookups.size(), 1);
        QVERIFY(stack.lookups[0].thirdPartyFailed);
        stack.player.playIndex(1);
        stack.source.answerLyrics(1, Synced(QStringLiteral("own")));
        stack.player.playIndex(0);
        QCOMPARE(stack.service.asked, QStringList{"1"});
        stack.source.answerLyrics(2, Plain(QStringLiteral("back")));
        QCOMPARE(stack.lookups.size(), 3);
        QVERIFY(!stack.lookups[2].thirdPartyFailed);
    }

    void lookupsAreCountedOnceByWhatShows() {  // TEL-17
        Stack stack;
        QVERIFY(stack.start(3));
        stack.finder.setActive(true);
        stack.source.answerLyrics(0, Plain(QStringLiteral("one")));
        QCOMPARE(stack.lookups.size(), 1);
        QCOMPARE(stack.lookups[0].shown, Shown::SourceText);

        stack.finder.setThirdPartyEnabled(true);
        stack.service.answer(LyricsAnswerWithServerError());
        QCOMPARE(stack.lookups.size(), 2);
        QCOMPARE(stack.lookups[1].shown, Shown::SourceText);
        QVERIFY(stack.lookups[1].thirdPartyFailed);

        stack.player.playIndex(1);
        stack.player.playIndex(2);
        QCOMPARE(stack.lookups.size(), 3);
        QCOMPARE(stack.lookups[2].shown, Shown::None);
        stack.source.answerLyrics(2, Answer(Status::Missing));
        stack.service.answer(Synced(QStringLiteral("timed")));
        QCOMPARE(stack.lookups.size(), 4);
        QCOMPARE(stack.lookups[3].shown, Shown::ThirdPartySynced);
        QVERIFY(!stack.lookups[3].thirdPartyFailed);

        stack.player.playIndex(0);
        QCOMPARE(stack.lookups.size(), 4);
        stack.finder.setActive(false);
        QCOMPARE(stack.lookups.size(), 4);
    }

private:
    static Core::LyricsAnswer LyricsAnswerWithServerError() {
        Core::LyricsAnswer answer;
        answer.serverError = true;
        return answer;
    }
};

QTEST_GUILESS_MAIN(TestLyrics)
#include "lyrics_test.moc"
