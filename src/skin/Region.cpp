#include "skin/Region.h"

#include <QRegularExpression>
#include <QStringList>

namespace qiyaa {
namespace {

QList<int> parseInts(const QString& s) {
    static const QRegularExpression sep(QStringLiteral("[\\s,]+"));
    QList<int> out;
    for (const QString& part : s.split(sep, Qt::SkipEmptyParts)) {
        bool ok = false;
        const int v = part.toInt(&ok);
        if (ok) out.append(v);
    }
    return out;
}

}  // namespace

RegionData parseRegionTxt(const QByteArray& text) {
    // Simple INI reader: sections, key=value, ';' / '#' / '//' comments, case-insensitive keys.
    QHash<QString, QHash<QString, QString>> ini;
    QString section;
    const QStringList lines = QString::fromLatin1(text).split(QRegularExpression(QStringLiteral("[\r\n]+")));
    for (QString line : lines) {
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith(u';') || line.startsWith(u'#') || line.startsWith(QStringLiteral("//")))
            continue;
        if (line.startsWith(u'[')) {
            const int end = line.indexOf(u']');
            section = line.mid(1, end > 0 ? end - 1 : -1).trimmed().toLower();
            continue;
        }
        const int eq = line.indexOf(u'=');
        if (eq <= 0 || section.isEmpty()) continue;
        QString value = line.mid(eq + 1);
        const int comment = value.indexOf(u';');
        if (comment >= 0) value.truncate(comment);
        ini[section][line.left(eq).trimmed().toLower()] = value.trimmed();
    }

    RegionData data;
    for (auto it = ini.cbegin(); it != ini.cend(); ++it) {
        const QList<int> counts = parseInts(it->value(QStringLiteral("numpoints")));
        const QList<int> coords = parseInts(it->value(QStringLiteral("pointlist")));
        if (counts.isEmpty() || coords.size() < 2) continue;

        QList<QPolygon> polygons;
        qsizetype point = 0;  // index in points (pairs)
        const qsizetype totalPoints = coords.size() / 2;
        for (int n : counts) {
            if (n < 3) {  // not a polygon
                point += std::max(n, 0);
                continue;
            }
            if (point + n > totalPoints) break;  // author declared more than provided
            QPolygon poly;
            poly.reserve(n);
            for (int i = 0; i < n; ++i, ++point)
                poly << QPoint(coords[point * 2], coords[point * 2 + 1]);
            polygons.append(poly);
        }
        if (!polygons.isEmpty()) data.insert(it.key(), polygons);
    }
    return data;
}

QRegion regionFromPolygons(const QList<QPolygon>& polygons) {
    QRegion r;
    for (const QPolygon& p : polygons) r += QRegion(p, Qt::WindingFill);
    return r;
}

}  // namespace qiyaa
