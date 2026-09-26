#include "skins/region.h"

#include <QByteArray>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>

namespace Skins {
namespace {

QList<int> ParseIntegers(const QString& text) {
    static const QRegularExpression separatorPattern(QStringLiteral("[\\s,]+"));
    QList<int> numbers;
    for (const QString& part : text.split(separatorPattern, Qt::SkipEmptyParts)) {
        bool ok = false;
        const int number = part.toInt(&ok);
        if (ok) {
            numbers.append(number);
        }
    }
    return numbers;
}

}  // namespace

TRegionData ParseRegionTxt(const QByteArray& text) {
    QHash<QString, QHash<QString, QString>> sections;
    QString section;
    const QStringList lines =
        QString::fromLatin1(text).split(QRegularExpression(QStringLiteral("[\r\n]+")));
    for (QString line : lines) {
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith(u';') || line.startsWith(u'#')
            || line.startsWith(QStringLiteral("//"))) {
            continue;
        }
        if (line.startsWith(u'[')) {
            const int end = line.indexOf(u']');
            section = line.mid(1, end > 0 ? end - 1 : -1).trimmed().toLower();
            continue;
        }
        const int equalsPosition = line.indexOf(u'=');
        if (equalsPosition <= 0 || section.isEmpty()) {
            continue;
        }
        QString value = line.mid(equalsPosition + 1);
        const int comment = value.indexOf(u';');
        if (comment >= 0) {
            value.truncate(comment);
        }
        sections[section][line.left(equalsPosition).trimmed().toLower()] = value.trimmed();
    }

    TRegionData data;
    for (auto it = sections.cbegin(); it != sections.cend(); ++it) {
        const QList<int> counts = ParseIntegers(it->value(QStringLiteral("numpoints")));
        const QList<int> coordinates = ParseIntegers(it->value(QStringLiteral("pointlist")));
        if (counts.isEmpty() || coordinates.size() < 2) {
            continue;
        }

        QList<QPolygon> polygons;
        qsizetype pointIndex = 0;
        const qsizetype totalPoints = coordinates.size() / 2;
        for (int count : counts) {
            if (count < 3) {
                pointIndex += std::max(count, 0);
                continue;
            }
            if (pointIndex + count > totalPoints) {
                break;
            }
            QPolygon polygon;
            polygon.reserve(count);
            for (int i = 0; i < count; ++i, ++pointIndex) {
                polygon << QPoint(coordinates[pointIndex * 2], coordinates[pointIndex * 2 + 1]);
            }
            polygons.append(polygon);
        }
        if (!polygons.isEmpty()) {
            data.insert(it.key(), polygons);
        }
    }
    return data;
}

QRegion RegionFromPolygons(const QList<QPolygon>& polygons) {
    QRegion region;
    for (const QPolygon& polygon : polygons) {
        region += QRegion(polygon, Qt::WindingFill);
    }
    return region;
}

}  // namespace Skins
