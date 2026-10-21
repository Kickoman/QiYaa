#include "skins/error.h"
#include "skins/skin.h"

#include <QBuffer>
#include <QByteArray>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QIODevice>
#include <QImage>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <miniz.h>

#include <cstddef>

namespace {

using TFiles = QHash<QString, QByteArray>;

TFiles FilesOf(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QByteArray bytes = file.readAll();
    mz_zip_archive archive{};
    if (!mz_zip_reader_init_mem(&archive, bytes.constData(), static_cast<size_t>(bytes.size()), 0)) {
        return {};
    }
    TFiles files;
    for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&archive); ++i) {
        mz_zip_archive_file_stat entry{};
        if (mz_zip_reader_is_file_a_directory(&archive, i) || !mz_zip_reader_file_stat(&archive, i, &entry)) {
            continue;
        }
        size_t size = 0;
        void* data = mz_zip_reader_extract_to_heap(&archive, i, &size, 0);
        if (data) {
            const QString name = QFileInfo(QString::fromUtf8(entry.m_filename)).fileName().toLower();
            files.insert(name, QByteArray(static_cast<const char*>(data), static_cast<qsizetype>(size)));
            mz_free(data);
        }
    }
    mz_zip_reader_end(&archive);
    return files;
}

QByteArray Wsz(const TFiles& files) {
    mz_zip_archive archive{};
    if (!mz_zip_writer_init_heap(&archive, 0, 0)) {
        return {};
    }
    for (auto it = files.cbegin(); it != files.cend(); ++it) {
        const QByteArray name = it.key().toUtf8();
        mz_zip_writer_add_mem(
            &archive, name.constData(), it->constData(), static_cast<size_t>(it->size()), MZ_DEFAULT_COMPRESSION
        );
    }
    void* data = nullptr;
    size_t size = 0;
    mz_zip_writer_finalize_heap_archive(&archive, &data, &size);
    const QByteArray zip(static_cast<const char*>(data), static_cast<qsizetype>(size));
    mz_zip_writer_end(&archive);
    return zip;
}

}  // namespace

class TestSkin : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void baseSkinLoads() {
        const Skins::Skin skin = Skins::Skin::BuiltinBase();
        QVERIFY(skin.isValid());
        QCOMPARE(skin.sheet(Skins::Skin::Sheet::Main).size(), QSize(275, 116));
        QVERIFY(!skin.sheet(Skins::Skin::Sheet::CButtons).isNull());
        QVERIFY(!skin.sheet(Skins::Skin::Sheet::Text).isNull());
        QCOMPARE(skin.visColors().size(), 24);
        QCOMPARE(skin.genFrame(), Skins::Skin::GenFrame::Gen);
    }
    void allBuiltinSkinsLoad_data() {
        QTest::addColumn<QString>("path");
        for (const QString& fileName : QDir(":/skins").entryList({"*.wsz"})) {
            QTest::newRow(qPrintable(fileName)) << QStringLiteral(":/skins/") + fileName;
        }
    }
    void allBuiltinSkinsLoad() {
        QFETCH(QString, path);
        const Skins::Skin base = Skins::Skin::BuiltinBase();
        const Skins::Skin skin = Skins::Skin::LoadFile(path, &base);
        for (auto sheet :
             {Skins::Skin::Sheet::Main,
              Skins::Skin::Sheet::CButtons,
              Skins::Skin::Sheet::TitleBar,
              Skins::Skin::Sheet::Numbers,
              Skins::Skin::Sheet::PosBar,
              Skins::Skin::Sheet::Volume,
              Skins::Skin::Sheet::Balance,
              Skins::Skin::Sheet::Text}) {
            QVERIFY(!skin.sheet(sheet).isNull());
        }
    }
    void theGenFrameComesFromTheSkinsOwnSheets_data() {
        QTest::addColumn<QString>("path");
        QTest::addColumn<bool>("fromPlaylist");
        const QStringList withGen{"base-2.91.wsz", "Green-Dimension-V2.wsz", "Skinner_Atlas.wsz"};
        const QStringList skins = QDir(":/skins").entryList({"*.wsz"});
        QCOMPARE(skins.size(), 8);
        for (const QString& fileName : skins) {
            QTest::newRow(qPrintable(fileName)) << QStringLiteral(":/skins/") + fileName << !withGen.contains(fileName);
        }
    }
    void theGenFrameComesFromTheSkinsOwnSheets() {
        QFETCH(QString, path);
        QFETCH(bool, fromPlaylist);
        const Skins::Skin base = Skins::Skin::BuiltinBase();
        const Skins::Skin skin = Skins::Skin::LoadFile(path, &base);
        QCOMPARE(skin.genFrame(), fromPlaylist ? Skins::Skin::GenFrame::Playlist : Skins::Skin::GenFrame::Gen);
    }

    void aSheetEqualToTheFallbacksIsNotTheSkinsOwn() {
        const Skins::Skin base = Skins::Skin::BuiltinBase();
        const TFiles baseFiles = FilesOf(QStringLiteral(":/skins/base-2.91.wsz"));
        const TFiles vizor = FilesOf(QStringLiteral(":/skins/Vizor1-01.wsz"));
        const TFiles green = FilesOf(QStringLiteral(":/skins/Green-Dimension-V2.wsz"));
        QVERIFY(baseFiles.contains("gen.bmp") && green.contains("gen.bmp") && vizor.contains("pledit.bmp"));
        const auto frameOf = [&base, &vizor](const QByteArray& gen, const QByteArray& pledit) {
            TFiles files{{"main.bmp", vizor.value("main.bmp")}, {"gen.bmp", gen}, {"pledit.bmp", pledit}};
            return Skins::Skin::LoadWsz(Wsz(files), &base).genFrame();
        };
        const QByteArray vizorPlaylist = vizor.value("pledit.bmp");
        QCOMPARE(frameOf(baseFiles.value("gen.bmp"), vizorPlaylist), Skins::Skin::GenFrame::Playlist);
        QCOMPARE(frameOf(green.value("gen.bmp"), vizorPlaylist), Skins::Skin::GenFrame::Gen);
        QCOMPARE(frameOf("not a bitmap", vizorPlaylist), Skins::Skin::GenFrame::Playlist);
        QCOMPARE(frameOf(baseFiles.value("gen.bmp"), baseFiles.value("pledit.bmp")), Skins::Skin::GenFrame::Gen);
        QImage baseGen;
        QVERIFY(baseGen.loadFromData(baseFiles.value("gen.bmp")));
        QBuffer resaved;
        QVERIFY(resaved.open(QIODevice::WriteOnly));
        QVERIFY(baseGen.convertToFormat(QImage::Format_RGB32).save(&resaved, "BMP"));
        QVERIFY(resaved.data() != baseFiles.value("gen.bmp"));
        QCOMPARE(frameOf(resaved.data(), vizorPlaylist), Skins::Skin::GenFrame::Playlist);
        const TFiles mainOnly{{"main.bmp", vizor.value("main.bmp")}};
        QCOMPARE(Skins::Skin::LoadWsz(Wsz(mainOnly), &base).genFrame(), Skins::Skin::GenFrame::Gen);
    }

    void theTextBackgroundIsTheColourBehindTheTextFont() {
        const Skins::Skin base = Skins::Skin::BuiltinBase();
        const QImage& text = base.sheet(Skins::Skin::Sheet::Text);
        QCOMPARE(base.textBackground(), QColor::fromRgb(text.pixel(30 * 5 + 2, 3)));
        const TFiles mainOnly{{"main.bmp", FilesOf(QStringLiteral(":/skins/Vizor1-01.wsz")).value("main.bmp")}};
        const Skins::Skin noText = Skins::Skin::LoadWsz(Wsz(mainOnly));
        QVERIFY(noText.sheet(Skins::Skin::Sheet::Text).isNull());
        QCOMPARE(noText.textBackground(), QColor(Qt::black));
    }

    void garbageIsRejected() { QVERIFY_THROWS_EXCEPTION(Skins::Error, Skins::Skin::LoadWsz("definitely not a zip")); }

    void failuresNameTheFileAndTheProblem() {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("broken.wsz"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("definitely not a zip");
        file.close();
        try {
            Skins::Skin::LoadFile(path);
            QFAIL("a broken skin loaded");
        } catch (const Skins::Error& error) {
            const QString message = QString::fromUtf8(error.what());
            QVERIFY2(message.startsWith(path), error.what());
            QVERIFY2(message.contains(QStringLiteral("not a zip archive (20 bytes)")), error.what());
        }
        QVERIFY_THROWS_EXCEPTION(Skins::Error, Skins::Skin::LoadFile(path + QStringLiteral(".missing")));
    }
};

QTEST_MAIN(TestSkin)
#include "skin_test.moc"
