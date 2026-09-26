#include "skins/region.h"

#include <QByteArray>
#include <QObject>
#include <QRegion>
#include <QTest>

class TestRegion : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void parsesSections() {
        const QByteArray regionText = "[Normal]\r\n"
                                      "NumPoints=4,4 ; two rectangles\r\n"
                                      "PointList=0,0, 275,0, 275,14, 0,14,\r\n"
                                      "          0,14 275 14 275 116 0 116\r\n"
                                      "[WindowShade]\n"
                                      "NumPoints = 4\n"
                                      "PointList = 0,0,275,0,275,14,0,14\n";
        const Skins::TRegionData regions = Skins::ParseRegionTxt(regionText);
        QCOMPARE(regions.size(), 2);
        // INI is line-based: the continuation line is ignored, so only the first
        // rectangle has points.
        QCOMPARE(regions.value("normal").size(), 1);
    }
    void multiplePolygonsJoinIntoOneRegion() {
        const QByteArray regionText =
            "[Normal]\nNumPoints=4,4\nPointList=0,0,10,0,10,10,0,10,20,0,30,0,30,10,20,10\n";
        const Skins::TRegionData regions = Skins::ParseRegionTxt(regionText);
        QCOMPARE(regions.value("normal").size(), 2);
        const QRegion region = Skins::RegionFromPolygons(regions.value("normal"));
        QVERIFY(region.contains(QPoint(5, 5)));
        QVERIFY(!region.contains(QPoint(15, 5)));
        QVERIFY(region.contains(QPoint(25, 5)));
    }
    void skipsDegenerateAndMissingPoints() {
        const QByteArray regionText =
            "[Normal]\nNumPoints=2,4,4\nPointList=1,1,2,2, 0,0,5,0,5,5,0,5\n";
        const Skins::TRegionData regions = Skins::ParseRegionTxt(regionText);
        QCOMPARE(regions.value("normal").size(), 1);  // "2" skipped, third polygon has no points
        QCOMPARE(regions.value("normal").first().first(), QPoint(0, 0));
    }
    void emptyInputGivesNoRegions() { QVERIFY(Skins::ParseRegionTxt(QByteArray()).isEmpty()); }
};

QTEST_GUILESS_MAIN(TestRegion)
#include "region_test.moc"
