#include "audio/audio_engine.h"
#include "core/lyrics.h"
#include "core/lyrics_finder.h"
#include "core/player.h"
#include "core/track.h"
#include "core/track_source.h"
#include "skins/skin.h"
#include "support/fake_source.h"
#include "ui/lyrics_window.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QEvent>
#include <QFile>
#include <QIODevice>
#include <QList>
#include <QMouseEvent>
#include <QObject>
#include <QPoint>
#include <QPointF>
#include <QPointer>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QTest>
#include <QWheelEvent>

#include <memory>

namespace {

const QString kFake = QStringLiteral("fake");

QList<Core::Track> Tracks(int count) {
    QList<Core::Track> tracks;
    for (int i = 1; i <= count; ++i) {
        Core::Track track;
        track.key = {kFake, QString::number(i)};
        track.title = QStringLiteral("Track %1").arg(i);
        track.durationMs = 3'000;
        tracks << track;
    }
    return tracks;
}

Core::LyricsAnswer Synced(const QList<Core::LyricsLine>& lines) {
    return {.status = Core::LyricsAnswer::Status::Found, .lyrics = {Core::LyricsKind::Synced, lines}};
}

void Click(QWidget* widget, QPoint local) {
    for (const QEvent::Type type : {QEvent::MouseButtonPress, QEvent::MouseButtonRelease}) {
        const Qt::MouseButtons buttons = type == QEvent::MouseButtonPress ? Qt::LeftButton : Qt::NoButton;
        QMouseEvent event(type, QPointF(local), QPointF(widget->mapToGlobal(local)), Qt::LeftButton, buttons, {});
        QCoreApplication::sendEvent(widget, &event);
    }
}

void Wheel(QWidget* widget, QPoint local, int notches) {
    QWheelEvent event(
        QPointF(local),
        QPointF(widget->mapToGlobal(local)),
        {},
        QPoint(0, notches * 120),
        Qt::NoButton,
        Qt::NoModifier,
        Qt::NoScrollPhase,
        false
    );
    QCoreApplication::sendEvent(widget, &event);
}

struct Stack {
    Audio::AudioEngine engine;
    Tests::FakeSource source;
    Core::SourceRegistry registry;
    Core::Player player{&engine, &registry};
    Core::LyricsFinder finder{&player, &registry, nullptr};
    Skins::Skin skin = Skins::Skin::BuiltinBase();
    std::unique_ptr<Ui::LyricsWindow> window;

    Stack() {
        registry.add(kFake, &source);
        engine.setVolume(0);
    }

    bool start(const QByteArray& mp3) {
        if (!engine.init().ok) {
            return false;
        }
        window = std::make_unique<Ui::LyricsWindow>(&finder, &player, &skin);
        window->setSizeSteps({0, 6});
        window->show();
        player.setSource(Core::QueueSource{.title = QStringLiteral("Fake"), .head = Tracks(2)}, false);
        player.playIndex(0);
        const QPointer<Tests::FakeDownload> download = source.complete(0);
        if (!download) {
            return false;
        }
        download->feed(mp3);
        download->finish();
        return QTest::qWaitFor([this] { return player.state() == Core::Player::State::Playing; }, 5'000);
    }

    QPoint pointOf(int line) const {
        for (int y = 0; y < window->skinSize().height(); ++y) {
            const QPoint point(window->skinSize().width() / 2, y);
            if (window->lineAt(point) == line) {
                return point;
            }
        }
        return {-1, -1};
    }
};

}  // namespace

class TestLyricsWindow : public QObject {
    Q_OBJECT
private:
    QByteArray mp3;

private Q_SLOTS:
    void initTestCase() {
        QFile file(QStringLiteral(QIYAA_TEST_DATA "/sine440_3s.mp3"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        mp3 = file.readAll();
    }

    void theCurrentLineFollowsThePositionAndTheSeeks() {  // LYR-06
        Stack stack;
        QVERIFY(stack.start(mp3));
        QCOMPARE(stack.source.lyricsAsked(), QStringList{"1"});
        stack.source.answerLyrics(0, Synced({{500, "first"}, {1500, "second"}, {2500, "third"}}));
        stack.player.pause();
        QVERIFY(stack.player.seekTo(1.6));
        QCOMPARE(stack.window->currentLine(), 1);
        QVERIFY(stack.player.seekTo(0.2));
        QCOMPARE(stack.window->currentLine(), -1);
        QVERIFY(stack.player.seekTo(2.6));
        QCOMPARE(stack.window->currentLine(), 2);
        stack.window->grab();
    }

    void aClickOnASyncedLineSeeksToIt() {  // LYR-07
        Stack stack;
        QVERIFY(stack.start(mp3));
        stack.source.answerLyrics(0, Synced({{500, "first"}, {1500, "second"}, {2500, "third"}}));
        stack.player.pause();
        const QPoint third = stack.pointOf(2);
        QVERIFY(third.x() >= 0);
        QSignalSpy seeked(&stack.player, &Core::Player::seeked);
        Click(stack.window.get(), third);
        QCOMPARE(seeked.count(), 1);
        QCOMPARE(seeked.first().first().toDouble(), 2.5);
        QCOMPARE(stack.player.state(), Core::Player::State::Paused);
        stack.player.stop();
        Click(stack.window.get(), stack.pointOf(0));
        QCOMPARE(seeked.count(), 1);
    }

    void aPlainTextDoesNotSeek() {  // LYR-07
        Stack stack;
        QVERIFY(stack.start(mp3));
        stack.source.answerLyrics(
            0, {.status = Core::LyricsAnswer::Status::Found, .lyrics = {Core::LyricsKind::Plain, {{-1, "only words"}}}}
        );
        QCOMPARE(stack.window->currentLine(), -1);
        const QPoint words = stack.pointOf(0);
        QVERIFY(words.x() >= 0);
        QSignalSpy seeked(&stack.player, &Core::Player::seeked);
        Click(stack.window.get(), words);
        QCOMPARE(seeked.count(), 0);
    }

    void theWheelStopsTheFollowingForAWhile() {  // LYR-08
        Stack stack;
        QVERIFY(stack.start(mp3));
        QList<Core::LyricsLine> lines;
        for (int i = 0; i < 60; ++i) {
            lines << Core::LyricsLine{i * 40, QStringLiteral("line %1").arg(i)};
        }
        stack.source.answerLyrics(0, Synced(lines));
        stack.player.pause();
        QVERIFY(stack.player.seekTo(2.0));
        QCOMPARE(stack.window->currentLine(), 50);
        QVERIFY(stack.pointOf(50).x() >= 0);
        stack.window->setFollowPauseMs(100);
        Wheel(stack.window.get(), {100, 100}, 5);
        QVERIFY(!stack.window->isFollowing());
        QCOMPARE(stack.pointOf(50).x(), -1);
        QTRY_VERIFY(stack.window->isFollowing());
        QVERIFY(stack.pointOf(50).x() >= 0);
    }

    void aLongLineWrapsAndEachOfItsRowsSeeks() {
        Stack stack;
        QVERIFY(stack.start(mp3));
        QString longLine;
        for (int i = 0; i < 30; ++i) {
            longLine += QStringLiteral("word%1 ").arg(i);
        }
        stack.source.answerLyrics(0, Synced({{500, "short"}, {1500, longLine.trimmed()}, {2500, "after"}}));
        stack.player.pause();
        int pixelRows = 0;
        for (int y = 0; y < stack.window->skinSize().height(); ++y) {
            pixelRows += stack.window->lineAt({stack.window->skinSize().width() / 2, y}) == 1 ? 1 : 0;
        }
        QVERIFY2(pixelRows >= 3 * 13, qPrintable(QString::number(pixelRows)));
        QSignalSpy seeked(&stack.player, &Core::Player::seeked);
        const QPoint lastRow = stack.pointOf(2) - QPoint(0, 1);
        QCOMPARE(stack.window->lineAt(lastRow), 1);
        Click(stack.window.get(), lastRow);
        QCOMPARE(seeked.count(), 1);
        QCOMPARE(seeked.first().first().toDouble(), 1.5);
    }

    void onlyTheRowsDrawnTakeClicks() {  // LYR-07
        Stack stack;
        QVERIFY(stack.start(mp3));
        stack.window->setSizeSteps({0, 0});
        QList<Core::LyricsLine> lines;
        for (int i = 0; i < 20; ++i) {
            lines << Core::LyricsLine{i * 100, QStringLiteral("line %1").arg(i)};
        }
        stack.source.answerLyrics(0, Synced(lines));
        int pixelRows = 0;
        for (int y = 0; y < stack.window->skinSize().height(); ++y) {
            pixelRows += stack.window->lineAt({stack.window->skinSize().width() / 2, y}) >= 0 ? 1 : 0;
        }
        QCOMPARE(pixelRows % 13, 0);
        QCOMPARE(pixelRows, 4 * 13);
    }

    void aHiddenWindowAsksNothing() {  // LYR-01
        Stack stack;
        QVERIFY(stack.start(mp3));
        QCOMPARE(stack.source.lyricsAsked(), QStringList{"1"});
        stack.window->hide();
        stack.player.playIndex(1);
        QCOMPARE(stack.source.lyricsAsked(), QStringList{"1"});
        stack.window->show();
        QCOMPARE(stack.source.lyricsAsked(), (QStringList{"1", "2"}));
    }

    void everyStatePaintsOnEverySkin() {
        Skins::Skin base = Skins::Skin::BuiltinBase();
        const Skins::Skin vizor = Skins::Skin::LoadFile(QStringLiteral(":/skins/Vizor1-01.wsz"), &base);
        Stack stack;
        QVERIFY(stack.start(mp3));
        stack.window->grab();
        stack.source.answerLyrics(0, {.status = Core::LyricsAnswer::Status::Missing});
        stack.window->grab();
        stack.window->setSkin(&vizor);
        stack.window->grab();
        stack.player.playIndex(1);
        stack.source.answerLyrics(1, Synced({{0, ""}, {500, "after a pause"}}));
        stack.window->grab();
        QCOMPARE(stack.pointOf(0).x() >= 0, true);
        stack.window->setSkin(&stack.skin);
    }
};

QTEST_MAIN(TestLyricsWindow)
#include "lyrics_window_test.moc"
