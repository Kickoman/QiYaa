#pragma once

#include "skins/region.h"

#include <QByteArray>
#include <QColor>
#include <QHash>
#include <QImage>
#include <QList>
#include <QPainter>
#include <QPoint>
#include <QRect>
#include <QString>

#include <cstddef>
#include <utility>

namespace Skins {

class Skin {
public:
    enum class Sheet {
        Main,
        CButtons,
        TitleBar,
        Numbers,
        PlayPaus,
        MonoSter,
        PosBar,
        ShufRep,
        Volume,
        Balance,
        Text,
        EqMain,
        PlEdit,
        EqEx,
        Gen,
    };

    struct PlaylistStyle {
        QColor normal{0x00, 0xFF, 0x00};
        QColor current{0xFF, 0xFF, 0xFF};
        QColor normalBg{0x00, 0x00, 0x00};
        QColor selectedBg{0x00, 0x00, 0xC6};
        QString font = QStringLiteral("Arial");
    };

    static Skin LoadWsz(const QByteArray& archive, const Skin* fallback = nullptr);
    static Skin LoadFile(const QString& path, const Skin* fallback = nullptr);

    static Skin BuiltinBase();

    bool isValid() const { return !sheets.value(Sheet::Main).isNull(); }

    const QImage& sheet(Sheet sheet) const;
    bool numbersAreExtended() const { return numbersEx; }

    void draw(QPainter& painter, Sheet bitmap, const QRect& src, const QPoint& dst) const;

    int drawGenText(QPainter& painter, const QPoint& at, const QString& text, bool selected) const;
    int genTextWidth(const QString& text) const;

    int drawText(QPainter& painter, const QPoint& at, const QString& text, int maxWidth = -1) const;
    static int TextWidth(const QString& text);

    const TRegionData& region() const { return regionData; }
    const QList<QColor>& visColors() const { return visualizationColors; }
    const PlaylistStyle& playlistStyle() const { return plStyle; }

    static PlaylistStyle ParsePlaylistStyle(const QByteArray& text);

private:
    QHash<Sheet, QImage> sheets;
    TRegionData regionData;
    QList<QColor> visualizationColors;
    PlaylistStyle plStyle;
    QList<std::pair<int, int>> genLetters;
    QList<std::pair<int, int>> genLettersSelected;
    void loadArchive(const QByteArray& archive, const Skin* fallback);
    void measureGenLetters();
    bool numbersEx = false;
};

size_t qHash(Skin::Sheet sheet, size_t seed = 0) noexcept;

}  // namespace Skins
