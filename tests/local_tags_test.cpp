#include "sources/local/tags.h"

#include <QByteArray>
#include <QFile>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <fileref.h>
#include <tbytevector.h>
#include <tlist.h>
#include <tpropertymap.h>
#include <tstring.h>
#include <tvariant.h>

#include <cstdlib>
#include <optional>

namespace {

const QByteArray kPng = QByteArray("\x89PNG\r\n\x1A\n", 8) + QByteArray(64, 'p');
const QByteArray kJpeg = QByteArray("\xFF\xD8\xFF\xE0", 4) + QByteArray(64, 'j');
const QByteArray kGif = QByteArray("GIF89a") + QByteArray(64, 'g');

TagLib::String TagText(const QString& text) {
    return TagLib::String(text.toStdWString());
}

TagLib::VariantMap Picture(const QByteArray& bytes, const char* type) {
    TagLib::VariantMap picture;
    picture.insert("data", TagLib::ByteVector(bytes.constData(), static_cast<unsigned int>(bytes.size())));
    picture.insert("mimeType", TagLib::String("image/png"));
    picture.insert("pictureType", TagLib::String(type));
    picture.insert("description", TagLib::String());
    return picture;
}

}  // namespace

class TestLocalTags : public QObject {
    Q_OBJECT
private:
    QTemporaryDir directory;

    QString copied(const QString& fixture, const QString& name) const {
        const QString path = directory.filePath(name);
        QFile::remove(path);
        if (!QFile::copy(QStringLiteral(QIYAA_TEST_DATA "/") + fixture, path)) {
            return {};
        }
        QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);
        return path;
    }

    static bool
    Tag(const QString& path, const TagLib::PropertyMap& properties, const TagLib::List<TagLib::VariantMap>& pictures) {
#ifdef _WIN32
        TagLib::FileRef file(reinterpret_cast<const wchar_t*>(path.utf16()));
#else
        TagLib::FileRef file(QFile::encodeName(path).constData());
#endif
        if (file.isNull()) {
            return false;
        }
        file.setProperties(properties);
        file.setComplexProperties("PICTURE", pictures);
        return file.save();
    }

    static TagLib::PropertyMap Properties() {
        TagLib::PropertyMap properties;
        properties.replace("TITLE", TagLib::StringList(TagText(QStringLiteral("Купалінка"))));
        TagLib::StringList artists;
        artists.append(TagText(QStringLiteral("Наш гурт")));
        artists.append(TagText(QStringLiteral("Гасцёва")));
        properties.replace("ARTIST", artists);
        properties.replace("ALBUM", TagLib::StringList(TagText(QStringLiteral("Песні"))));
        properties.replace("DATE", TagLib::StringList(TagLib::String("2001")));
        properties.replace("GENRE", TagLib::StringList(TagLib::String("Folk")));
        return properties;
    }

private Q_SLOTS:
    void initTestCase() { QVERIFY(directory.isValid()); }

    void aFileGivesItsTagsItsDurationAndItsFrontCover_data() {
        QTest::addColumn<QString>("fixture");
        QTest::addColumn<int>("durationMs");
        QTest::addColumn<QByteArray>("cover");
        QTest::newRow("mp3") << QStringLiteral("sine440_3s.mp3") << 3'030 << kPng;
        QTest::newRow("flac") << QStringLiteral("sine440_1s.flac") << 1'000 << kPng;
        QTest::newRow("wav") << QStringLiteral("sine440_1s.wav") << 1'000 << kPng;
        QTest::newRow("aac in mp4, no picture types") << QStringLiteral("sine440_3s_heaac.m4a") << 3'160 << kJpeg;
    }

    void aFileGivesItsTagsItsDurationAndItsFrontCover() {  // LOCAL-04, LOCAL-05
        QFETCH(QString, fixture);
        QFETCH(int, durationMs);
        QFETCH(QByteArray, cover);
        const QString path = copied(fixture, QStringLiteral("Наш гурт — Купалінка.") + fixture.section(u'.', -1));
        QVERIFY(!path.isEmpty());
        TagLib::List<TagLib::VariantMap> pictures;
        pictures.append(Picture(kJpeg, "Back Cover"));
        pictures.append(Picture(kPng, "Front Cover"));
        QVERIFY(Tag(path, Properties(), pictures));
        const std::optional<LocalSource::FileTags> tags = LocalSource::ReadTags(path);
        QVERIFY(tags);
        QCOMPARE(tags->title, QStringLiteral("Купалінка"));
        QCOMPARE(tags->artists, (QStringList{QStringLiteral("Наш гурт"), QStringLiteral("Гасцёва")}));
        QCOMPARE(tags->album, QStringLiteral("Песні"));
        QCOMPARE(tags->year, 2001);
        QCOMPARE(tags->genre, QStringLiteral("Folk"));
        QVERIFY2(std::abs(tags->durationMs - durationMs) <= 100, qPrintable(QString::number(tags->durationMs)));
        QVERIFY(tags->playable);
        QCOMPARE(tags->picture, cover);
    }

    void aFileWithoutTagsHasNoTitleButItsDuration() {  // LOCAL-04
        const QString path = copied(QStringLiteral("sine440_1s.flac"), QStringLiteral("bare.flac"));
        const std::optional<LocalSource::FileTags> tags = LocalSource::ReadTags(path);
        QVERIFY(tags);
        QCOMPARE(tags->title, QString());
        QVERIFY(tags->artists.isEmpty());
        QVERIFY(tags->picture.isEmpty());
        QVERIFY(tags->durationMs > 900);
    }

    void aPictureOverTheLimitIsLeftOutAndTheRestIsRead() {  // LOCAL-05
        const QString path = copied(QStringLiteral("sine440_3s.mp3"), QStringLiteral("big cover.mp3"));
        const QByteArray big = kPng + QByteArray(static_cast<qsizetype>(LocalSource::kMaxPictureBytes), '\0');
        TagLib::List<TagLib::VariantMap> pictures;
        pictures.append(Picture(big, "Front Cover"));
        QVERIFY(Tag(path, Properties(), pictures));
        QVERIFY(QFile(path).size() > LocalSource::kMaxPictureBytes);
        const std::optional<LocalSource::FileTags> tags = LocalSource::ReadTags(path);
        QVERIFY(tags);
        QCOMPARE(tags->title, QStringLiteral("Купалінка"));
        QVERIFY(tags->picture.isEmpty());
    }

    void onlyJpegAndPngAreTakenAsTheCover() {  // LOCAL-05
        const QString path = copied(QStringLiteral("sine440_1s.flac"), QStringLiteral("gif.flac"));
        TagLib::List<TagLib::VariantMap> pictures;
        pictures.append(Picture(kGif, "Front Cover"));
        pictures.append(Picture(kJpeg, "Back Cover"));
        QVERIFY(Tag(path, Properties(), pictures));
        QCOMPARE(LocalSource::ReadTags(path)->picture, kJpeg);
        TagLib::List<TagLib::VariantMap> onlyGif;
        onlyGif.append(Picture(kGif, "Front Cover"));
        QVERIFY(Tag(path, Properties(), onlyGif));
        QVERIFY(LocalSource::ReadTags(path)->picture.isEmpty());
    }

    void aFileTheEngineDoesNotDecodeCannotPlay_data() {  // LOCAL-02
        QTest::addColumn<QString>("fixture");
        QTest::addColumn<QString>("name");
        QTest::newRow("ALAC in MP4") << QStringLiteral("sine440_1s_alac.m4a") << QStringLiteral("alac.m4a");
        QTest::newRow("ADTS AAC") << QStringLiteral("sine440_3s_heaac.aac") << QStringLiteral("adts.aac");
        QTest::newRow("ADTS AAC named mp3") << QStringLiteral("sine440_3s_heaac.aac") << QStringLiteral("adts.mp3");
    }

    void aFileTheEngineDoesNotDecodeCannotPlay() {  // LOCAL-02
        QFETCH(QString, fixture);
        QFETCH(QString, name);
        const std::optional<LocalSource::FileTags> tags = LocalSource::ReadTags(copied(fixture, name));
        QVERIFY(tags);
        QVERIFY(!tags->playable);
        QVERIFY(tags->durationMs > 900);
    }

    void aTagOverTheReadLimitIsLeftOutWhole() {
        QFile fixture(QStringLiteral(QIYAA_TEST_DATA "/sine440_3s.mp3"));
        QVERIFY(fixture.open(QIODevice::ReadOnly));
        const QByteArray mp3 = fixture.readAll();
        const auto byte = [&](qsizetype index) { return static_cast<qint64>(static_cast<uchar>(mp3.at(index))); };
        const qint64 ownTag = 10 + (byte(6) << 21 | byte(7) << 14 | byte(8) << 7 | byte(9));
        const qint64 tagSize = LocalSource::kMaxReadBytes + 1024 * 1024;
        QByteArray header("ID3\x03\x00\x00", 6);
        for (const int shift : {21, 14, 7, 0}) {
            header += static_cast<char>((tagSize >> shift) & 0x7f);
        }
        const QByteArray title = QByteArray(1, '\0') + "Big tag";
        header += "TIT2";
        for (const int shift : {24, 16, 8, 0}) {
            header += static_cast<char>((title.size() >> shift) & 0xff);
        }
        header += QByteArray(2, '\0') + title;
        const QString path = directory.filePath(QStringLiteral("big tag.mp3"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(header), header.size());
        QVERIFY(file.resize(10 + tagSize));
        QVERIFY(file.seek(10 + tagSize));
        QCOMPARE(file.write(mp3.mid(ownTag)), mp3.size() - ownTag);
        file.close();
        const std::optional<LocalSource::FileTags> tags = LocalSource::ReadTags(path);
        QVERIFY(tags);
        QCOMPARE(tags->title, QString());
        QVERIFY2(std::abs(tags->durationMs - 3'030) <= 100, qPrintable(QString::number(tags->durationMs)));
        QVERIFY(QFile::remove(path));
    }

    void aFileOfNoKnownFormatGivesNothing() {
        const QString text = directory.filePath(QStringLiteral("notes.txt"));
        QFile notes(text);
        QVERIFY(notes.open(QIODevice::WriteOnly));
        notes.write("not a song");
        notes.close();
        QVERIFY(!LocalSource::ReadTags(text));
        const QString empty = directory.filePath(QStringLiteral("empty.flac"));
        QFile file(empty);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        QVERIFY(!LocalSource::ReadTags(empty));
        QVERIFY(!LocalSource::ReadTags(directory.filePath(QStringLiteral("none.mp3"))));
    }
};

QTEST_GUILESS_MAIN(TestLocalTags)
#include "local_tags_test.moc"
