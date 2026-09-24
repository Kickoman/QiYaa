#include <QTest>

#include "skin/Region.h"

using namespace qiyaa;

class TestRegion : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void parsesSections() {
        const QByteArray txt =
            "[Normal]\r\n"
            "NumPoints=4,4 ; two rectangles\r\n"
            "PointList=0,0, 275,0, 275,14, 0,14,\r\n"
            "          0,14 275 14 275 116 0 116\r\n"
            "[WindowShade]\n"
            "NumPoints = 4\n"
            "PointList = 0,0,275,0,275,14,0,14\n";
        const RegionData d = parseRegionTxt(txt);
        QCOMPARE(d.size(), 2);
        // INI is line-based: the continuation line is ignored, so only the first
        // rectangle has points.
        QCOMPARE(d.value("normal").size(), 1);
    }
    void multiplePolygons() {
        const QByteArray txt =
            "[Normal]\nNumPoints=4,4\nPointList=0,0,10,0,10,10,0,10,20,0,30,0,30,10,20,10\n";
        const RegionData d = parseRegionTxt(txt);
        QCOMPARE(d.value("normal").size(), 2);
        const QRegion r = regionFromPolygons(d.value("normal"));
        QVERIFY(r.contains(QPoint(5, 5)));
        QVERIFY(!r.contains(QPoint(15, 5)));
        QVERIFY(r.contains(QPoint(25, 5)));
    }
    void skipsDegenerateAndMissingPoints() {
        const QByteArray txt = "[Normal]\nNumPoints=2,4,4\nPointList=1,1,2,2, 0,0,5,0,5,5,0,5\n";
        const RegionData d = parseRegionTxt(txt);
        QCOMPARE(d.value("normal").size(), 1);  // "2" skipped, third polygon has no points
        QCOMPARE(d.value("normal").first().first(), QPoint(0, 0));
    }
    void emptyInput() {
        QVERIFY(parseRegionTxt(QByteArray()).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestRegion)
#include "test_region.moc"
