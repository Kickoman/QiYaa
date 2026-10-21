#include "audio/audio_engine.h"
#include "core/cover_cache.h"
#include "core/player.h"
#include "core/source_notice.h"
#include "core/track.h"
#include "core/track_source.h"
#include "integrations/media_controls.h"
#include "sources/local/catalog.h"
#include "sources/local/lyrics.h"
#include "sources/local/scan.h"
#include "sources/local/source.h"
#include "sources/local/tracks.h"

#include <QBuffer>
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QList>
#include <QSignalSpy>
#include <QString>
#include <QStringConverter>
#include <QStringEncoder>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QUrl>
#include <fileref.h>
#include <tbytevector.h>
#include <tlist.h>
#include <tpropertymap.h>
#include <tstring.h>
#include <tvariant.h>

#include <atomic>
#include <optional>
#include <variant>

namespace {

QByteArray Png(int width, int height) {
    QImage image(width, height, QImage::Format_RGB32);
    image.fill(Qt::darkCyan);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

bool Tag(const QString& path, const QString& title, const QStringList& artists, const QByteArray& picture = {}) {
#ifdef _WIN32
    TagLib::FileRef file(reinterpret_cast<const wchar_t*>(path.utf16()));
#else
    TagLib::FileRef file(QFile::encodeName(path).constData());
#endif
    if (file.isNull()) {
        return false;
    }
    TagLib::PropertyMap properties;
    properties.replace("TITLE", TagLib::StringList(TagLib::String(title.toStdWString())));
    TagLib::StringList names;
    for (const QString& artist : artists) {
        names.append(TagLib::String(artist.toStdWString()));
    }
    properties.replace("ARTIST", names);
    properties.replace("ALBUM", TagLib::StringList(TagLib::String("Песні", TagLib::String::UTF8)));
    file.setProperties(properties);
    if (!picture.isEmpty()) {
        TagLib::VariantMap cover;
        cover.insert("data", TagLib::ByteVector(picture.constData(), static_cast<unsigned int>(picture.size())));
        cover.insert("mimeType", TagLib::String("image/png"));
        cover.insert("pictureType", TagLib::String("Front Cover"));
        TagLib::List<TagLib::VariantMap> pictures;
        pictures.append(cover);
        file.setComplexProperties("PICTURE", pictures);
    }
    return file.save();
}

bool TagLyrics(const QString& path, const QString& text) {
#ifdef _WIN32
    TagLib::FileRef file(reinterpret_cast<const wchar_t*>(path.utf16()));
#else
    TagLib::FileRef file(QFile::encodeName(path).constData());
#endif
    if (file.isNull()) {
        return false;
    }
    TagLib::PropertyMap properties = file.properties();
    properties.replace("LYRICS", TagLib::StringList(TagLib::String(text.toStdWString())));
    file.setProperties(properties);
    return file.save();
}

QStringList Titles(const Core::Player& player) {
    QStringList titles;
    for (const Core::QueueEntry& entry : player.entries()) {
        titles << entry.track.title;
    }
    return titles;
}

}  // namespace

class TestLocalSource : public QObject {
    Q_OBJECT
private:
    QTemporaryDir directory;

    struct Stack {
        explicit Stack(const QString& coverDirectory)
            : catalog(&player, coverDirectory) {
            registry.add(LocalSource::kSourceName, &source);
            engine.setVolume(0);
            engine.init();
            QObject::connect(&catalog, &LocalSource::Catalog::noticed, [this](const Core::SourceNotice& notice) {
                notices << notice;
            });
        }

        bool saw(Core::SourceNoticeCode code, const QString& subject, int count = -1) const {
            return std::any_of(notices.cbegin(), notices.cend(), [&](const Core::SourceNotice& notice) {
                return notice.code == code && notice.subject == subject && (count < 0 || notice.count == count);
            });
        }

        Audio::AudioEngine engine;
        LocalSource::Source source;
        Core::SourceRegistry registry;
        Core::Player player{&engine, &registry};
        LocalSource::Catalog catalog;
        QList<Core::SourceNotice> notices;
    };

    QString path(const QString& relative) const { return directory.filePath(relative); }

    QString copied(const QString& fixture, const QString& relative) const {
        const QString target = path(relative);
        QDir().mkpath(QFileInfo(target).absolutePath());
        QFile::remove(target);
        if (!QFile::copy(QStringLiteral(QIYAA_TEST_DATA "/") + fixture, target)) {
            return {};
        }
        QFile::setPermissions(target, QFile::ReadOwner | QFile::WriteOwner);
        return target;
    }

    QString written(const QString& relative, const QByteArray& bytes) const {
        const QString target = path(relative);
        QDir().mkpath(QFileInfo(target).absolutePath());
        QFile file(target);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) {
            return {};
        }
        return target;
    }

    QString covers() const { return path(QStringLiteral("local-covers")); }

private Q_SLOTS:
    void initTestCase() {
        QVERIFY(directory.isValid());
        QVERIFY(Tag(
            copied(QStringLiteral("sine440_3s.mp3"), "music/02 Two.mp3"), "Two", {"Наш гурт", "Гасцёва"}, Png(800, 600)
        ));
        QVERIFY(Tag(copied(QStringLiteral("sine440_3s.mp3"), "music/10 Ten.mp3"), "Ten", {"Наш гурт"}));
        QVERIFY(!copied(QStringLiteral("sine440_1s.flac"), QStringLiteral("music/1 One.flac")).isEmpty());
        QVERIFY(!copied(QStringLiteral("sine440_1s.wav"), QStringLiteral("music/Album/a.wav")).isEmpty());
        QVERIFY(!written(QStringLiteral("music/Album/Folder.JPG"), Png(300, 300)).isEmpty());
        QVERIFY(!copied(QStringLiteral("sine440_1s_alac.m4a"), QStringLiteral("music/Lossless.m4a")).isEmpty());
        QVERIFY(!copied(QStringLiteral("sine440_3s.mp3"), QStringLiteral("music/.hidden.mp3")).isEmpty());
        QVERIFY(!copied(QStringLiteral("sine440_3s.mp3"), QStringLiteral("music/.secret/inside.mp3")).isEmpty());
        QVERIFY(!written(QStringLiteral("music/notes.txt"), "not a song").isEmpty());
        QFile::link(path(QStringLiteral("music/Album")), path(QStringLiteral("music/Link to the album")));
        QVERIFY(!written(QStringLiteral("empty/notes.txt"), "not a song").isEmpty());
    }

    void aFolderPlaysInTheNaturalOrderOfItsPaths() {  // LOCAL-02, LOCAL-01, CONT-12
        Stack stack(covers());
        stack.catalog.openFolder(path(QStringLiteral("music")));
        QVERIFY(stack.saw(Core::SourceNoticeCode::Loading, QStringLiteral("music")));
        QTRY_COMPARE(Titles(stack.player), (QStringList{"1 One", "Two", "Ten", "a"}));
        QVERIFY(stack.saw(Core::SourceNoticeCode::Loaded, QStringLiteral("music"), 4));
        QCOMPARE(
            stack.player.entries()[3].track.key, (Core::TrackKey{"local", path(QStringLiteral("music/Album/a.wav"))})
        );
        QCOMPARE(stack.player.currentIndex(), 0);
        QVERIFY(!stack.player.continuesWithVibe());
        QVERIFY(!stack.player.canSwitchVibe());
        QTRY_COMPARE(stack.engine.state(), Audio::AudioEngine::State::Playing);
    }

    void eachTrackComesWithItsTagsAndItsCoverKeptSmall() {  // LOCAL-04, LOCAL-05
        const std::atomic<bool> going{false};
        const LocalSource::ScanResult result = LocalSource::ScanFolder(path(QStringLiteral("music")), covers(), going);
        QCOMPARE(result.tracks.size(), 4);
        QVERIFY(!result.cut);
        const Core::Track& one = result.tracks[0];
        QCOMPARE(one.title, QStringLiteral("1 One"));
        QVERIFY(one.artists.isEmpty());
        QVERIFY(one.cover.value.isEmpty());
        QVERIFY(one.durationMs > 900);
        const Core::Track& two = result.tracks[1];
        QCOMPARE(two.artists, (QStringList{QStringLiteral("Наш гурт"), QStringLiteral("Гасцёва")}));
        QCOMPARE(two.album, QStringLiteral("Песні"));
        QVERIFY(two.durationMs > 2900);
        const QImage tagged(two.cover.value);
        QCOMPARE(tagged.size(), QSize(400, 300));
        QCOMPARE(QFileInfo(two.cover.value).absolutePath(), QFileInfo(covers()).absoluteFilePath());
        QVERIFY(result.tracks[2].cover.value.isEmpty());
        const QImage folder(result.tracks[3].cover.value);
        QCOMPARE(folder.size(), QSize(300, 300));
    }

    void aLocalCoverReachesTheWindowsAndMprisButNotSmtc() {  // LOCAL-05
        Stack stack(covers());
        stack.catalog.openFolder(path(QStringLiteral("music")));
        QTRY_COMPARE(stack.player.entries().size(), 4);
        stack.player.playIndex(1);
        const QUrl url = stack.registry.coverUrl(stack.player.entries()[1].track, 400);
        QVERIFY(url.isLocalFile());
        Core::CoverCache cache(nullptr, path(QStringLiteral("cache")));
        QVERIFY(!cache.get(url).isNull());
        QCOMPARE(cache.localFile(url), url.toLocalFile());
        QVERIFY(QDir(path(QStringLiteral("cache"))).isEmpty());
        Integrations::MediaControls controls(&stack.player, &stack.registry, &cache, {});
        QCOMPARE(controls.artUrl(), url);
        QVERIFY(controls.remoteArtUrl().isEmpty());
        QVERIFY(stack.registry.webPage(stack.player.entries()[1].track).isEmpty());
    }

    void theStatusNamesOneFileOrTheFolderOfSeveral() {  // LOCAL-01
        Stack stack(covers());
        stack.catalog.openFiles({path(QStringLiteral("music/10 Ten.mp3"))});
        QTRY_COMPARE(Titles(stack.player), QStringList{"Ten"});
        QVERIFY(stack.saw(Core::SourceNoticeCode::Loaded, QStringLiteral("10 Ten"), 1));
        stack.catalog.openFiles({path(QStringLiteral("music/10 Ten.mp3")), path(QStringLiteral("music/02 Two.mp3"))});
        QTRY_COMPARE(Titles(stack.player), (QStringList{"Two", "Ten"}));
        QVERIFY(stack.saw(Core::SourceNoticeCode::Loaded, QStringLiteral("music"), 2));
    }

    void aFileThatCannotPlayIsSkippedAndNeverWaitsForTheNetwork() {  // LOCAL-07, ERR-04, ERR-05, ERR-07
        const QString broken = written(QStringLiteral("bad/broken.mp3"), QByteArray(20'000, 'x'));
        const QString alsoBroken = written(QStringLiteral("bad/also broken.mp3"), QByteArray(20'000, 'y'));
        const QString good = copied(QStringLiteral("sine440_3s.mp3"), QStringLiteral("bad/good.mp3"));
        Stack stack(covers());
        QSignalSpy failures(&stack.player, &Core::Player::failed);
        stack.catalog.openFiles({broken, alsoBroken, good});
        QTRY_COMPARE(stack.player.entries().size(), 3);
        QTRY_COMPARE_WITH_TIMEOUT(stack.player.currentIndex(), 2, 5000);
        QTRY_COMPARE(stack.engine.state(), Audio::AudioEngine::State::Playing);
        QCOMPARE(failures.size(), 2);
        for (const QList<QVariant>& failure : failures) {
            QCOMPARE(failure.at(0).value<Core::FailureKind>(), Core::FailureKind::Track);
        }

        stack.catalog.openFiles({broken});
        QTRY_COMPARE(stack.player.entries().size(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(stack.player.state(), Core::Player::State::Stopped, 5000);
        QCOMPARE(failures.size(), 3);
        QCOMPARE(failures.last().at(0).value<Core::FailureKind>(), Core::FailureKind::Track);
    }

    void aNewPickWinsOverAFolderStillBeingRead() {  // LOCAL-03, SRC-01
        Stack stack(covers());
        stack.catalog.openFolder(path(QStringLiteral("music")));
        stack.catalog.openFiles({path(QStringLiteral("music/10 Ten.mp3"))});
        QTRY_COMPARE(Titles(stack.player), QStringList{"Ten"});
        QTest::qWait(200);
        QCOMPARE(Titles(stack.player), QStringList{"Ten"});
        stack.notices.clear();
        stack.catalog.openFolder(path(QStringLiteral("music")));
        stack.player.newSourceRequest();
        QTest::qWait(500);
        QCOMPARE(Titles(stack.player), QStringList{"Ten"});
        QVERIFY(!stack.saw(Core::SourceNoticeCode::Loaded, QStringLiteral("music")));
    }

    void aPickWithNothingToPlayChangesNothing() {  // LOCAL-03, SRC-07
        Stack stack(covers());
        stack.catalog.openFiles({path(QStringLiteral("music/10 Ten.mp3"))});
        QTRY_COMPARE(Titles(stack.player), QStringList{"Ten"});
        stack.catalog.openFolder(path(QStringLiteral("empty")));
        QTRY_VERIFY(stack.saw(Core::SourceNoticeCode::Empty, QStringLiteral("empty")));
        stack.catalog.openFiles({path(QStringLiteral("music/notes.txt")), path(QStringLiteral("music/Lossless.m4a"))});
        QTRY_VERIFY(stack.saw(Core::SourceNoticeCode::Empty, QStringLiteral("music")));
        QCOMPARE(Titles(stack.player), QStringList{"Ten"});
    }

    void theSourceOpensTheFileItselfAfterOpenReturns() {  // LOCAL-06, LOCAL-07
        LocalSource::Source source;
        Core::Track track;
        track.key = {LocalSource::kSourceName, path(QStringLiteral("music/10 Ten.mp3"))};
        track.durationMs = 3'000;
        std::optional<Core::TOpenResult> answer;
        source.open(track, [&](Core::TOpenResult result) { answer = std::move(result); });
        QVERIFY(!answer);
        QTRY_VERIFY(answer);
        const auto* opened = std::get_if<Core::OpenedTrack>(&*answer);
        QVERIFY(opened);
        QVERIFY(opened->file);
        QVERIFY(!opened->download);
        QVERIFY(opened->link.isEmpty());
        QVERIFY(opened->bitrateKbps > 50 && opened->bitrateKbps < 100);
        track.key.id = path(QStringLiteral("music/none.mp3"));
        answer.reset();
        source.open(track, [&](Core::TOpenResult result) { answer = std::move(result); });
        QTRY_VERIFY(answer);
        const auto* failure = std::get_if<Core::TrackFailure>(&*answer);
        QVERIFY(failure);
        QCOMPARE(failure->kind, Core::FailureKind::Track);
    }

    void lyricsTakeTheFilesOwnOrder_data() {  // LYR-10
        QTest::addColumn<QString>("lrcName");
        QTest::addColumn<QString>("lrc");
        QTest::addColumn<QString>("tags");
        QTest::addColumn<int>("from");
        QTest::addColumn<QString>("first");
        const int lrcFile = static_cast<int>(Core::LyricsFrom::LrcFile);
        const int fileTags = static_cast<int>(Core::LyricsFrom::FileTags);
        const QString song = QStringLiteral("song.lrc");
        QTest::newRow("synced lrc over synced tags"
        ) << song
          << QStringLiteral("[00:01.00]lrc") << QStringLiteral("[00:01.00]tags") << lrcFile << QStringLiteral("lrc");
        QTest::newRow("synced tags over a plain lrc"
        ) << song
          << QStringLiteral("lrc") << QStringLiteral("[00:01.00]tags") << fileTags << QStringLiteral("tags");
        QTest::newRow("plain tags over a plain lrc"
        ) << song
          << QStringLiteral("lrc") << QStringLiteral("tags") << fileTags << QStringLiteral("tags");
        QTest::newRow("a plain lrc alone"
        ) << song
          << QStringLiteral("lrc") << QString() << lrcFile << QStringLiteral("lrc");
        QTest::newRow("an lrc in capitals"
        ) << QStringLiteral("song.LRC")
          << QStringLiteral("[00:01.00]upper") << QString() << lrcFile << QStringLiteral("upper");
        QTest::newRow("another song's lrc"
        ) << QStringLiteral("song 2.lrc")
          << QStringLiteral("[00:01.00]other") << QString() << -1 << QString();
    }

    void lyricsTakeTheFilesOwnOrder() {
        QFETCH(QString, lrcName);
        QFETCH(QString, lrc);
        QFETCH(QString, tags);
        QFETCH(int, from);
        QFETCH(QString, first);
        const QString folder =
            QStringLiteral("lyrics/") + QString::fromLatin1(QTest::currentDataTag()).replace(u' ', u'-');
        const QString audio = copied(QStringLiteral("sine440_3s.mp3"), folder + QStringLiteral("/song.mp3"));
        QVERIFY(!audio.isEmpty());
        if (!tags.isEmpty()) {
            QVERIFY(TagLyrics(audio, tags));
        }
        QVERIFY(!written(folder + u'/' + lrcName, lrc.toUtf8()).isEmpty());
        const Core::LyricsAnswer answer = LocalSource::ReadLyrics(audio);
        if (from < 0) {
            QCOMPARE(answer.status, Core::LyricsAnswer::Status::Missing);
            return;
        }
        QCOMPARE(answer.status, Core::LyricsAnswer::Status::Found);
        QCOMPARE(static_cast<int>(answer.from), from);
        QCOMPARE(answer.lyrics.lines.front().text, first);
    }

    void anLrcIsReadInItsEncoding_data() {  // LYR-10
        QTest::addColumn<int>("encoding");
        QTest::addColumn<bool>("mark");
        QTest::newRow("UTF-8") << static_cast<int>(QStringConverter::Utf8) << false;
        QTest::newRow("UTF-8 with a byte order mark") << static_cast<int>(QStringConverter::Utf8) << true;
        QTest::newRow("UTF-16 LE with a byte order mark") << static_cast<int>(QStringConverter::Utf16LE) << true;
        QTest::newRow("UTF-16 BE with a byte order mark") << static_cast<int>(QStringConverter::Utf16BE) << true;
    }

    void anLrcIsReadInItsEncoding() {
        QFETCH(int, encoding);
        QFETCH(bool, mark);
        const QString text = QStringLiteral("[00:01.00]Ноч і зоры над ракой");
        QStringEncoder encoder(
            static_cast<QStringConverter::Encoding>(encoding),
            mark ? QStringConverter::Flag::WriteBom : QStringConverter::Flag::Default
        );
        const QByteArray bytes = encoder(text);
        const QString lrcPath = written(QStringLiteral("encodings/song.lrc"), bytes);
        QCOMPARE(LocalSource::ReadLrc(lrcPath), std::optional<QString>(text));
    }

    void anLrcOverTheLimitIsNotRead() {  // LYR-11
        const QString audio = copied(QStringLiteral("sine440_3s.mp3"), QStringLiteral("big lrc/song.mp3"));
        QVERIFY(TagLyrics(audio, QStringLiteral("from the tags")));
        const QByteArray big = "[00:01.00]" + QByteArray(Core::kMaxLyricsBytes, 'a');
        QVERIFY(!written(QStringLiteral("big lrc/song.lrc"), big).isEmpty());
        QVERIFY(!LocalSource::ReadLrc(path(QStringLiteral("big lrc/song.lrc"))));
        const Core::LyricsAnswer answer = LocalSource::ReadLyrics(audio);
        QCOMPARE(answer.from, Core::LyricsFrom::FileTags);
        QCOMPARE(answer.lyrics.lines.front().text, QStringLiteral("from the tags"));
    }

    void theSourceReadsLyricsAfterItReturns() {  // LYR-10
        const QString audio = copied(QStringLiteral("sine440_3s.mp3"), QStringLiteral("threaded/song.mp3"));
        QVERIFY(!written(QStringLiteral("threaded/song.lrc"), "[00:01.00]line").isEmpty());
        LocalSource::Source source;
        Core::Track track;
        track.key = {LocalSource::kSourceName, audio};
        std::optional<Core::LyricsAnswer> answer;
        QThread* answeredOn = nullptr;
        source.lyrics(track, [&](Core::LyricsAnswer lyrics) {
            answer = std::move(lyrics);
            answeredOn = QThread::currentThread();
        });
        QVERIFY(!answer);
        QTRY_VERIFY(answer);
        QCOMPARE(answeredOn, QThread::currentThread());
        QCOMPARE(answer->from, Core::LyricsFrom::LrcFile);
        track.key.source = QStringLiteral("yandex");
        answer.reset();
        source.lyrics(track, [&](Core::LyricsAnswer lyrics) { answer = std::move(lyrics); });
        QVERIFY(!answer);
        QTRY_VERIFY(answer);
        QCOMPARE(answer->status, Core::LyricsAnswer::Status::Missing);
    }

    void aSourceGoneBeforeItsReaderEndsDropsTheAnswer() {
        const QString audio = path(QStringLiteral("threaded/song.mp3"));
        bool answered = false;
        {
            LocalSource::Source source;
            Core::Track track;
            track.key = {LocalSource::kSourceName, audio};
            source.lyrics(track, [&](Core::LyricsAnswer) { answered = true; });
        }
        QTest::qWait(50);
        QVERIFY(!answered);
    }

    void aFolderIsReadUpToItsLimits() {  // LOCAL-02
        const QString many = path(QStringLiteral("many"));
        QVERIFY(QDir().mkpath(many));
        for (int i = 0; i <= LocalSource::kMaxFiles; ++i) {
            QFile file(many + QStringLiteral("/%1.mp3").arg(i));
            QVERIFY(file.open(QIODevice::WriteOnly));
        }
        const std::atomic<bool> going{false};
        const LocalSource::ScanResult files = LocalSource::ScanFolder(many, {}, going);
        QCOMPARE(files.tracks.size(), LocalSource::kMaxFiles);
        QVERIFY(files.cut);
        QCOMPARE(files.tracks.first().title, QStringLiteral("0"));
        QCOMPARE(files.tracks.last().title, QStringLiteral("9999"));
        QStringList chosen;
        for (int i = LocalSource::kMaxFiles; i >= 0; --i) {
            chosen << many + QStringLiteral("/%1.mp3").arg(i);
        }
        const LocalSource::ScanResult picked = LocalSource::ScanFiles(chosen, {}, going);
        QCOMPARE(picked.tracks.size(), LocalSource::kMaxFiles);
        QVERIFY(picked.cut);
        QCOMPARE(picked.tracks.first().title, QStringLiteral("0"));
        QCOMPARE(picked.tracks.last().title, QStringLiteral("9999"));
        const QString deep = path(QStringLiteral("deep"));
        for (int i = 0; i < LocalSource::kMaxFolders; ++i) {
            QVERIFY(QDir().mkpath(deep + QStringLiteral("/%1").arg(i)));
        }
        QVERIFY(LocalSource::ScanFolder(deep, {}, going).cut);
        const std::atomic<bool> stopped{true};
        QVERIFY(LocalSource::ScanFolder(path(QStringLiteral("music")), {}, stopped).tracks.isEmpty());
    }
};

QTEST_MAIN(TestLocalSource)
#include "local_source_test.moc"
