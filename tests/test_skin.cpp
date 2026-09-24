#include <QDir>
#include <QTest>

#include "skin/Skin.h"

using namespace qiyaa;

class TestSkin : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void baseSkinLoads() {
        const Skin s = Skin::builtinBase();
        QVERIFY(s.isValid());
        QCOMPARE(s.sheet(Skin::Sheet::Main).size(), QSize(275, 116));
        QVERIFY(!s.sheet(Skin::Sheet::CButtons).isNull());
        QVERIFY(!s.sheet(Skin::Sheet::Text).isNull());
        QCOMPARE(s.visColors().size(), 24);
    }
    void allBuiltinSkinsLoad_data() {
        QTest::addColumn<QString>("path");
        for (const QString& f : QDir(":/skins").entryList({"*.wsz"}))
            QTest::newRow(qPrintable(f)) << QStringLiteral(":/skins/") + f;
    }
    void allBuiltinSkinsLoad() {
        QFETCH(QString, path);
        const Skin base = Skin::builtinBase();
        Skin s;
        QString err;
        QVERIFY2(s.loadFromFile(path, &base, &err), qPrintable(err));
        for (auto sheet : {Skin::Sheet::Main, Skin::Sheet::CButtons, Skin::Sheet::TitleBar, Skin::Sheet::Numbers,
                           Skin::Sheet::PosBar, Skin::Sheet::Volume, Skin::Sheet::Balance, Skin::Sheet::Text})
            QVERIFY(!s.sheet(sheet).isNull());
    }
    void garbageIsRejected() {
        Skin s;
        QString err;
        QVERIFY(!s.loadFromWsz("definitely not a zip", nullptr, &err));
        QVERIFY(!err.isEmpty());
    }
};

QTEST_MAIN(TestSkin)
#include "test_skin.moc"
