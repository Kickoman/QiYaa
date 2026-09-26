#include "skins/error.h"
#include "skins/skin.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

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
    }
    void allBuiltinSkinsLoad_data() {
        QTest::addColumn<QString>("path");
        for (const QString& f : QDir(":/skins").entryList({"*.wsz"})) {
            QTest::newRow(qPrintable(f)) << QStringLiteral(":/skins/") + f;
        }
    }
    void allBuiltinSkinsLoad() {
        QFETCH(QString, path);
        const Skins::Skin base = Skins::Skin::BuiltinBase();
        const Skins::Skin skin = Skins::Skin::LoadFile(path, &base);
        for (auto sheet :
             {Skins::Skin::Sheet::Main, Skins::Skin::Sheet::CButtons, Skins::Skin::Sheet::TitleBar,
              Skins::Skin::Sheet::Numbers, Skins::Skin::Sheet::PosBar, Skins::Skin::Sheet::Volume,
              Skins::Skin::Sheet::Balance, Skins::Skin::Sheet::Text}) {
            QVERIFY(!skin.sheet(sheet).isNull());
        }
    }
    void garbageIsRejected() {
        QVERIFY_THROWS_EXCEPTION(Skins::Error, Skins::Skin::LoadWsz("definitely not a zip"));
    }

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
            QVERIFY2(
                message.contains(QStringLiteral("not a zip archive (20 bytes)")), error.what()
            );
        }
        QVERIFY_THROWS_EXCEPTION(
            Skins::Error, Skins::Skin::LoadFile(path + QStringLiteral(".missing"))
        );
    }
};

QTEST_MAIN(TestSkin)
#include "skin_test.moc"
