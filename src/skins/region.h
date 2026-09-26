// Parser for Winamp's region.txt (window shape masks).
#pragma once

#include <QHash>
#include <QList>
#include <QPolygon>
#include <QRegion>
#include <QString>

namespace Skins {

// Section name (lower-case: "normal", "windowshade", "equalizer", "equalizerws")
// -> list of polygons.
using TRegionData = QHash<QString, QList<QPolygon>>;

TRegionData ParseRegionTxt(const QByteArray& text);

// Builds a mask region from polygons. Empty list -> empty region (= no mask).
QRegion RegionFromPolygons(const QList<QPolygon>& polygons);

}  // namespace Skins
