// Parser for Winamp's region.txt (window shape masks).
#pragma once

#include <QHash>
#include <QList>
#include <QPolygon>
#include <QRegion>
#include <QString>

namespace qiyaa {

// Section name (lower-case: "normal", "windowshade", "equalizer", "equalizerws")
// -> list of polygons.
using RegionData = QHash<QString, QList<QPolygon>>;

RegionData parseRegionTxt(const QByteArray& text);

// Builds a mask region from polygons. Empty list -> empty region (= no mask).
QRegion regionFromPolygons(const QList<QPolygon>& polygons);

}  // namespace qiyaa
