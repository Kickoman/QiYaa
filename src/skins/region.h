#pragma once

#include <QHash>
#include <QList>
#include <QPolygon>
#include <QRegion>
#include <QString>

namespace Skins {

using TRegionData = QHash<QString, QList<QPolygon>>;

TRegionData ParseRegionTxt(const QByteArray& text);

QRegion RegionFromPolygons(const QList<QPolygon>& polygons);

}  // namespace Skins
