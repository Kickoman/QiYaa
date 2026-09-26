#include "skins/skin.h"

#include <QDir>
#include <QTest>

class TestSkin : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void baseSkinLoads() {
        const Skins::Skin s = Skins::Skin::BuiltinBase();
        QVERIFY(s.isValid());
        QCOMPARE(s.sheet(Skins::Skin::Sheet::Main).size(), QSize(275, 116));
        QVERIFY(!s.sheet(Skins::Skin::Sheet::CButtons).isNull());
        QVERIFY(!s.sheet(Skins::Skin::Sheet::Text).isNull());
        QCOMPARE(s.visColors().size(), 24);
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
        Skins::Skin s;
        QString err;
        QVERIFY2(s.loadFromFile(path, &base, &err), qPrintable(err));
        for (auto sheet :
             {Skins::Skin::Sheet::Main, Skins::Skin::Sheet::CButtons, Skins::Skin::Sheet::TitleBar,
              Skins::Skin::Sheet::Numbers, Skins::Skin::Sheet::PosBar, Skins::Skin::Sheet::Volume,
              Skins::Skin::Sheet::Balance, Skins::Skin::Sheet::Text}) {
            QVERIFY(!s.sheet(sheet).isNull());
        }
    }
    void garbageIsRejected() {
        Skins::Skin s;
        QString err;
        QVERIFY(!s.loadFromWsz("definitely not a zip", nullptr, &err));
        QVERIFY(!err.isEmpty());
    }
};

QTEST_MAIN(TestSkin)
#include "skin_test.moc"
