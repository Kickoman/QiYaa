#include "app/translations.h"
#include "audio/audio_engine.h"
#include "core/player.h"
#include "core/queue_source.h"
#include "core/search.h"
#include "core/track.h"
#include "core/track_source.h"
#include "skins/skin.h"
#include "ui/search_window.h"

#include <QCoreApplication>
#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QImage>
#include <QList>
#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QTest>

#include <optional>
#include <utility>

// Ui::SearchWindow against spec/player/sources.md (SRC-13 to SRC-19) and queue.md (ADD-): its
// controls, by their labels (the English source texts), drive a fake Core::SearchCatalog; its
// pictures, in the default language, are compared with tests/data/golden, drawn with
// tests/data/fonts/Tiny5 so that every system draws the same.

namespace {

const QString kGoldenDir = QStringLiteral(QIYAA_TEST_DATA "/golden");

QString Source(const char* text) {
    return QCoreApplication::translate("Ui::SearchWindow", text);
}

Core::Track TrackOf(int id, const QString& title, const QString& artist = QStringLiteral("КИНО")) {
    Core::Track track;
    track.key = {QStringLiteral("test"), QString::number(id)};
    track.title = title;
    track.artists = {artist};
    track.durationMs = 180'000 + id * 7'000;
    return track;
}

Core::SearchPick ArtistOf(const QString& id, const QString& name) {
    return Core::SearchPick{.kind = Core::SearchPick::Kind::Artist, .id = id, .name = name};
}

Core::SearchPick AlbumOf(const QString& id, const QString& name, int year, bool single = false) {
    return Core::SearchPick{
        .kind = Core::SearchPick::Kind::Album,
        .id = id,
        .name = name,
        .artists = {QStringLiteral("КИНО")},
        .year = year,
        .single = single
    };
}

Core::SearchResults Results() {
    Core::SearchResults results;
    results.bestArtist = ArtistOf(QStringLiteral("41"), QStringLiteral("КИНО"));
    const QStringList titles{
        QStringLiteral("Группа крови"),
        QStringLiteral("Кукушка"),
        QStringLiteral("Звезда по имени Солнце"),
        QStringLiteral("Хочу перемен!"),
        QStringLiteral("Пачка сигарет"),
        QStringLiteral("Последний герой"),
        QStringLiteral("Восьмиклассница"),
        QStringLiteral("Солнечные дни"),
    };
    for (qsizetype i = 0; i < titles.size(); ++i) {
        results.tracks << TrackOf(static_cast<int>(i) + 1, titles[i]);
    }
    results.artists = {ArtistOf(QStringLiteral("53"), QStringLiteral("Виктор Цой"))};
    results.albums = {
        AlbumOf(QStringLiteral("70"), QStringLiteral("Группа крови"), 1988),
        AlbumOf(QStringLiteral("71"), QStringLiteral("Звезда по имени Солнце"), 1989),
        AlbumOf(QStringLiteral("72"), QStringLiteral("Кукушка"), 2026, true),
        AlbumOf(QStringLiteral("73"), QStringLiteral("Чёрный альбом"), 1990),
    };
    return results;
}

QString DescribeDifference(const QImage& actual, const QImage& expected) {
    if (actual.size() != expected.size()) {
        return QStringLiteral("size %1x%2, expected %3x%4")
            .arg(actual.width())
            .arg(actual.height())
            .arg(expected.width())
            .arg(expected.height());
    }
    int differing = 0;
    for (int y = 0; y < actual.height(); ++y) {
        for (int x = 0; x < actual.width(); ++x) {
            differing += actual.pixel(x, y) != expected.pixel(x, y);
        }
    }
    return differing == 0 ? QString() : QStringLiteral("%1 pixels differ").arg(differing);
}

struct FakeCatalog : Core::SearchCatalog {
    struct Played {
        QString title;
        QList<Core::Track> tracks;
        int start = 0;
        std::optional<Core::SearchPick> from;
    };
    struct Added {
        QString title;
        QList<Core::Track> tracks;
        Core::AddPlacement placement = Core::AddPlacement::Next;
    };

    bool searchable = true;
    QStringList asked;
    QList<TFound> answers;
    QList<std::pair<Core::SearchPick, TTracks>> opened;
    QList<Core::SearchPick> picks;
    QList<Played> played;
    QList<Added> added;
    QList<std::pair<QString, Core::AddPlacement>> albums;

    bool canSearch() const override { return searchable; }
    void find(const QString& text, TFound done) override {
        asked << text;
        answers << std::move(done);
    }
    void open(const Core::SearchPick& pick, TTracks done) override { opened << std::pair(pick, std::move(done)); }
    void playPick(const Core::SearchPick& pick) override { picks << pick; }
    void playTracks(const QString& title, const QList<Core::Track>& tracks, int start, const TFrom& from) override {
        played << Played{title, tracks, start, from};
    }
    void add(const QString& title, const QList<Core::Track>& tracks, Core::AddPlacement placement) override {
        added << Added{title, tracks, placement};
    }
    void addAlbum(const Core::SearchPick& album, Core::AddPlacement placement) override {
        albums << std::pair(album.id, placement);
    }
};

}  // namespace

class TestSearchWindow : public QObject {
    Q_OBJECT
private:
    Skins::Skin baseSkin = Skins::Skin::BuiltinBase();
    QFont pixelFont;

    struct Stack {
        Audio::AudioEngine engine;
        Core::SourceRegistry registry;
        Core::Player player{&engine, &registry};
        FakeCatalog yandex;
        FakeCatalog spotify;
        Ui::SearchWindow window;

        Stack(const Skins::Skin* skin, const QFont& font)
            : window({{"Yandex Music", &yandex}, {"Spotify", &spotify}}, &player, skin) {
            window.setTextFont(font);
            window.setSizeSteps(QSize(3, 12));
            window.show();
        }

        std::optional<Ui::SearchWindow::Control> control(const QString& label, int skip = 0) {
            const QList<Ui::SearchWindow::Control> all = window.controls();
            for (const Ui::SearchWindow::Control& each : all) {
                if (each.label == label && skip-- == 0) {
                    return each;
                }
            }
            return std::nullopt;
        }

        // The Play or Add button of the row with this text: they come before its text.
        std::optional<Ui::SearchWindow::Control> buttonOf(const QString& row, const QString& label) {
            const QList<Ui::SearchWindow::Control> all = window.controls();
            for (qsizetype i = 0; i < all.size(); ++i) {
                if (all[i].label != row) {
                    continue;
                }
                for (qsizetype j = i - 1; j >= 0 && j >= i - 2; --j) {
                    if (all[j].label == label) {
                        return all[j];
                    }
                }
            }
            return std::nullopt;
        }

        bool click(const std::optional<Ui::SearchWindow::Control>& target, Qt::KeyboardModifiers modifiers = {}) {
            if (!target) {
                return false;
            }
            QTest::mouseClick(&window, Qt::LeftButton, modifiers, target->rect.center());
            return true;
        }

        bool click(const QString& label) { return click(control(label)); }

        void answer(const Core::SearchResults& results, int index = -1) {
            yandex.answers.value(index < 0 ? yandex.answers.size() - 1 : index)(results, {});
        }
    };

    void compareWithGolden(QWidget* window, const QString& name) {
        const QImage actual = window->grab().toImage().convertToFormat(QImage::Format_ARGB32);
        const QString path = kGoldenDir + QLatin1Char('/') + name + QStringLiteral(".png");
        if (const QString shots = qEnvironmentVariable("QIYAA_TEST_SHOTS"); !shots.isEmpty()) {
            actual.save(shots + QLatin1Char('/') + name + QStringLiteral(".png"));
        }
        if (qEnvironmentVariable("QIYAA_UPDATE_GOLDEN") == QLatin1String("1") && !QFile::exists(path)) {
            QVERIFY(actual.save(path));
            QSKIP(qPrintable(QStringLiteral("recorded new golden image ") + path));
        }
        const QImage expected(path);
        QVERIFY2(!expected.isNull(), qPrintable(QStringLiteral("no golden image ") + path));
        const QString difference = DescribeDifference(actual, expected.convertToFormat(QImage::Format_ARGB32));
        QVERIFY2(difference.isEmpty(), qPrintable(name + QStringLiteral(": ") + difference));
    }

    static QString RowText(int number, const QString& title) {
        return QStringLiteral("%1. КИНО - %2").arg(number).arg(title);
    }

private Q_SLOTS:
    void initTestCase() {
        const int id = QFontDatabase::addApplicationFont(QStringLiteral(QIYAA_TEST_DATA "/fonts/Tiny5-Regular.ttf"));
        QVERIFY(id >= 0);
        pixelFont = QFont(QFontDatabase::applicationFontFamilies(id).value(0));
        pixelFont.setPixelSize(8);
        pixelFont.setStyleStrategy(QFont::NoAntialias);
        pixelFont.setHintingPreference(QFont::PreferNoHinting);
    }

    void aSearchShowsItsPartsAndPlaysNothingByItself() {  // SRC-13
        Stack stack(&baseSkin, pixelFont);
        stack.window.search(QStringLiteral("  кино "));
        QCOMPARE(stack.yandex.asked, QStringList{"кино"});
        stack.answer(Results());
        QVERIFY(stack.control(QStringLiteral("Tracks · 8")));
        QVERIFY(stack.control(QStringLiteral("Artists · 1")));
        QVERIFY(stack.control(QStringLiteral("Albums · 4")));
        QVERIFY(stack.control(RowText(6, QStringLiteral("Последний герой"))));
        QVERIFY(!stack.control(RowText(7, QStringLiteral("Восьмиклассница"))));
        QVERIFY(stack.click(stack.control(Source("All"))));
        QVERIFY(stack.control(RowText(7, QStringLiteral("Восьмиклассница"))));
        QVERIFY(stack.yandex.played.isEmpty());
        QVERIFY(stack.yandex.added.isEmpty());
    }

    void aFoundTrackPlaysTheFoundTracksFromIt() {  // SRC-18
        Stack stack(&baseSkin, pixelFont);
        stack.window.search(QStringLiteral("кино"));
        Core::SearchResults results = Results();
        results.bestArtist.reset();
        results.bestTrack = TrackOf(9, QStringLiteral("Кончится лето"));
        stack.answer(results);
        QVERIFY(stack.click(stack.buttonOf(RowText(3, QStringLiteral("Звезда по имени Солнце")), Source("Play"))));
        QCOMPARE(stack.yandex.played.size(), 1);
        const FakeCatalog::Played played = stack.yandex.played.front();
        QCOMPARE(played.title, QStringLiteral("Search: кино"));
        QCOMPARE(played.tracks.size(), 9);
        QCOMPARE(played.tracks.front().title, QStringLiteral("Кончится лето"));
        QCOMPARE(played.start, 3);
        QVERIFY(!played.from);
    }

    void aNewerSearchWinsOverAnOlderOne() {  // SRC-14
        Stack stack(&baseSkin, pixelFont);
        stack.window.search(QStringLiteral("кино"));
        stack.window.search(QStringLiteral("цой"));
        Core::SearchResults newer;
        newer.tracks = {TrackOf(30, QStringLiteral("Кукушка"), QStringLiteral("Цой"))};
        stack.answer(newer, 1);
        stack.answer(Results(), 0);
        QVERIFY(stack.control(QStringLiteral("1. Цой - Кукушка")));
        QVERIFY(!stack.control(QStringLiteral("Tracks · 8")));
    }

    void aFailedSearchSaysSoAndCanBeTriedAgain() {  // SRC-15
        Stack stack(&baseSkin, pixelFont);
        stack.window.search(QStringLiteral("кино"));
        stack.yandex.answers.back()({}, QStringLiteral("HTTP 500"));
        QVERIFY(stack.click(Source("Try again")));
        QCOMPARE(stack.yandex.asked, (QStringList{"кино", "кино"}));
    }

    void nothingFoundShowsNoRows() {  // SRC-19
        Stack stack(&baseSkin, pixelFont);
        stack.window.search(QStringLiteral("кинл"));
        stack.answer({});
        QVERIFY(!stack.control(Source("Play")));
        QVERIFY(!stack.control(Source("Try again")));
    }

    void anAlbumOpensPlaysFromATrackAndGoesBack() {  // SRC-16
        Stack stack(&baseSkin, pixelFont);
        stack.window.search(QStringLiteral("кино"));
        stack.answer(Results());
        const QString album = QStringLiteral("Группа крови · КИНО · 1988");
        QVERIFY(stack.click(album));
        QCOMPARE(stack.window.selectedRow(), 8);
        QTest::keyClick(&stack.window, Qt::Key_Return);
        QCOMPARE(stack.yandex.opened.size(), 1);
        QCOMPARE(stack.yandex.opened.front().first.id, QStringLiteral("70"));
        stack.yandex.opened.front().second(
            {TrackOf(1, QStringLiteral("Группа крови")), TrackOf(2, QStringLiteral("Война"))}, {}
        );
        QVERIFY(stack.control(Source("Play all")));
        QVERIFY(stack.click(stack.buttonOf(RowText(2, QStringLiteral("Война")), Source("Play"))));
        const FakeCatalog::Played played = stack.yandex.played.back();
        QCOMPARE(played.title, QStringLiteral("Группа крови"));
        QCOMPARE(played.start, 1);
        QCOMPARE(played.from->id, QStringLiteral("70"));
        QVERIFY(stack.click(Source("Back")));
        QVERIFY(stack.control(QStringLiteral("Tracks · 8")));
        QCOMPARE(stack.yandex.asked.size(), 1);
        QVERIFY(stack.click(stack.buttonOf(QStringLiteral("Виктор Цой · artist"), Source("Play"))));
        QCOMPARE(stack.yandex.picks.back().id, QStringLiteral("53"));
    }

    void addingPutsTracksNextOrWithShiftAtTheEnd() {  // SRC-17, ADD-01, ADD-03
        Stack stack(&baseSkin, pixelFont);
        stack.window.search(QStringLiteral("кино"));
        stack.answer(Results());
        const QString kukushka = RowText(2, QStringLiteral("Кукушка"));
        QVERIFY(stack.click(stack.buttonOf(kukushka, Source("Add"))));
        QCOMPARE(stack.yandex.added.back().placement, Core::AddPlacement::Next);
        QCOMPARE(stack.yandex.added.back().tracks.front().title, QStringLiteral("Кукушка"));
        QCOMPARE(stack.yandex.added.back().title, QStringLiteral("Search: кино"));
        QVERIFY(stack.click(stack.control(kukushka)));
        QTest::keyClick(&stack.window, Qt::Key_Insert, Qt::ShiftModifier);
        QCOMPARE(stack.yandex.added.back().placement, Core::AddPlacement::End);
        QVERIFY(stack.click(stack.buttonOf(QStringLiteral("Группа крови · КИНО · 1988"), Source("Add"))));
        QCOMPARE(stack.yandex.albums.back(), std::pair(QStringLiteral("70"), Core::AddPlacement::Next));
        QVERIFY(stack.yandex.played.isEmpty());
    }

    void theKeysMoveFromTheFieldThroughTheRowsAndBack() {
        Stack stack(&baseSkin, pixelFont);
        stack.window.search(QStringLiteral("кино"));
        stack.answer(Results());
        stack.window.focusField();
        QTest::keyClick(&stack.window, Qt::Key_Down);
        QCOMPARE(stack.window.selectedRow(), 0);
        QTest::keyClick(&stack.window, Qt::Key_Down);
        QCOMPARE(stack.window.selectedRow(), 1);
        QTest::keyClick(&stack.window, Qt::Key_Return);
        QCOMPARE(stack.yandex.played.back().start, 0);
        QTest::keyClick(&stack.window, Qt::Key_Up);
        QTest::keyClick(&stack.window, Qt::Key_Up);
        QCOMPARE(stack.window.selectedRow(), -1);
        QTest::keyClick(&stack.window, Qt::Key_Return);
        QCOMPARE(stack.yandex.asked.size(), 2);
    }

    void aTabSearchesItsSourceWithTheSameText() {  // SPOT-11
        Stack stack(&baseSkin, pixelFont);
        stack.window.search(QStringLiteral("кино"));
        stack.answer(Results());
        QVERIFY(stack.click(Source("Spotify")));
        QCOMPARE(stack.window.currentTab(), 1);
        QCOMPARE(stack.spotify.asked, QStringList{"кино"});
        QVERIFY(!stack.control(QStringLiteral("Tracks · 8")));
    }

    void aSourceWithoutALoginOffersOne() {
        Stack stack(&baseSkin, pixelFont);
        stack.spotify.searchable = false;
        QSignalSpy login(&stack.window, &Ui::SearchWindow::loginRequested);
        stack.window.showTab(1);
        QVERIFY(stack.click(Source("Log in…")));
        QCOMPARE(login.size(), 1);
        QCOMPARE(login.front().front().toInt(), 1);
    }

    void theResultsLookAsRecorded() {  // SRC-13
        App::Translations translations;  // the pictures are in the default language
        QVERIFY(translations.apply(App::kDefaultLanguage));
        Stack stack(&baseSkin, pixelFont);
        Core::Track current = TrackOf(1, QStringLiteral("Группа крови"));
        stack.player.setSource(Core::QueueSource{.title = QStringLiteral("Q"), .head = {current}}, false);
        stack.window.search(QStringLiteral("кино"));
        stack.answer(Results());
        QVERIFY(stack.click(stack.buttonOf(RowText(2, QStringLiteral("Кукушка")), Source("Add"))));
        QVERIFY(stack.click(stack.control(RowText(3, QStringLiteral("Звезда по имени Солнце")))));
        compareWithGolden(&stack.window, QStringLiteral("search-results"));
    }

    void anOpenedAlbumLooksAsRecorded() {  // SRC-16
        App::Translations translations;  // the pictures are in the default language
        QVERIFY(translations.apply(App::kDefaultLanguage));
        Stack stack(&baseSkin, pixelFont);
        stack.window.search(QStringLiteral("кино"));
        stack.answer(Results());
        QVERIFY(stack.click(QStringLiteral("Группа крови · КИНО · 1988")));
        QTest::keyClick(&stack.window, Qt::Key_Return);
        stack.yandex.opened.front().second(
            {TrackOf(1, QStringLiteral("Группа крови")),
             TrackOf(2, QStringLiteral("Закрой за мной дверь, я ухожу")),
             TrackOf(3, QStringLiteral("Война"))},
            {}
        );
        compareWithGolden(&stack.window, QStringLiteral("search-album"));
    }

    void anEmptyWindowLooksAsRecorded() {
        App::Translations translations;  // the pictures are in the default language
        QVERIFY(translations.apply(App::kDefaultLanguage));
        Stack stack(&baseSkin, pixelFont);
        compareWithGolden(&stack.window, QStringLiteral("search-start"));
    }
};

QTEST_MAIN(TestSearchWindow)
#include "search_window_test.moc"
