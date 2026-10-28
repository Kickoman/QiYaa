#include "core/queue_source.h"
#include "core/search.h"
#include "core/source_notice.h"
#include "core/track.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QTest>

namespace {

Core::Track TrackOf(const QString& id, bool available = true) {
    Core::Track track;
    track.key = {QStringLiteral("test"), id};
    track.title = QStringLiteral("T") + id;
    track.available = available;
    return track;
}

Core::SearchPick PickOf(Core::SearchPick::Kind kind, const QString& id) {
    return Core::SearchPick{.kind = kind, .id = id, .name = QStringLiteral("P") + id};
}

}  // namespace

class TestSearch : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void theBestResultIsShownOnceAndOnlyAvailableTracks() {  // SRC-13
        Core::SearchResults results{
            .bestTrack = TrackOf(QStringLiteral("1")),
            .bestArtist = PickOf(Core::SearchPick::Kind::Artist, QStringLiteral("a")),
            .bestAlbum = PickOf(Core::SearchPick::Kind::Album, QStringLiteral("b")),
            .tracks = {TrackOf(QStringLiteral("1")), TrackOf(QStringLiteral("2"), false), TrackOf(QStringLiteral("3"))},
            .artists =
                {PickOf(Core::SearchPick::Kind::Artist, QStringLiteral("a")),
                 PickOf(Core::SearchPick::Kind::Artist, QStringLiteral("c"))},
            .albums = {PickOf(Core::SearchPick::Kind::Album, QStringLiteral("b"))},
        };
        const Core::SearchResults shown = Core::Shown(results);
        QCOMPARE(shown.tracks.size(), 1);
        QCOMPARE(shown.tracks.front().key.id, QStringLiteral("3"));
        QCOMPARE(shown.artists.size(), 1);
        QCOMPARE(shown.artists.front().id, QStringLiteral("c"));
        QVERIFY(shown.albums.isEmpty());
        QCOMPARE(shown.playable().size(), 2);
        QCOMPARE(shown.playable().front().key.id, QStringLiteral("1"));
    }

    void anUnavailableBestTrackIsNotShown() {  // SRC-13, SRC-19
        const Core::SearchResults shown =
            Core::Shown(Core::SearchResults{.bestTrack = TrackOf(QStringLiteral("1"), false)});
        QVERIFY(!shown.bestTrack);
        QVERIFY(shown.isEmpty());
    }

    void theAddedNoticeNamesOneTrackOrWhatTheyCameFrom() {  // ADD-01, ADD-03
        const Core::SourceNotice one = Core::AddedNotice(
            QStringLiteral("Album"),
            {TrackOf(QStringLiteral("1"), false), TrackOf(QStringLiteral("2"))},
            Core::AddPlacement::Next
        );
        QCOMPARE(one.code, Core::SourceNoticeCode::AddedNext);
        QCOMPARE(one.subject, QStringLiteral("T2"));
        QCOMPARE(one.count, 1);
        const Core::SourceNotice many = Core::AddedNotice(
            QStringLiteral("Album"),
            {TrackOf(QStringLiteral("1")), TrackOf(QStringLiteral("2"))},
            Core::AddPlacement::End
        );
        QCOMPARE(many.code, Core::SourceNoticeCode::AddedToEnd);
        QCOMPARE(many.subject, QStringLiteral("Album"));
        QCOMPARE(many.count, 2);
    }
};

QTEST_GUILESS_MAIN(TestSearch)
#include "search_test.moc"
