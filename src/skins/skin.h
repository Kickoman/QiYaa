// A loaded Winamp 2.x skin (.wsz = zip of BMP sprite sheets + a few text files).
#pragma once

#include "skins/region.h"

#include <QByteArray>
#include <QColor>
#include <QHash>
#include <QImage>
#include <QList>
#include <QPainter>
#include <QString>

namespace Skins {

class Skin {
public:
    enum class Sheet {
        Main,
        CButtons,
        TitleBar,
        Numbers,  // nums_ex.bmp if present, otherwise numbers.bmp
        PlayPaus,
        MonoSter,
        PosBar,
        ShufRep,
        Volume,
        Balance,  // falls back to volume.bmp like Winamp does
        Text,
        EqMain,
        PlEdit,
        EqEx,
        Gen,
    };

    // Colours and font from PLEDIT.TXT.
    struct PlaylistStyle {
        QColor normal{0x00, 0xFF, 0x00};
        QColor current{0xFF, 0xFF, 0xFF};
        QColor normalBg{0x00, 0x00, 0x00};
        QColor selectedBg{0x00, 0x00, 0xC6};
        QString font = QStringLiteral("Arial");
    };

    // Loads a .wsz from memory. Missing sheets are taken from `fallback` (normally
    // the built-in base skin). Returns false and fills `error` if the archive is unusable.
    bool
    loadFromWsz(const QByteArray& zip, const Skin* fallback = nullptr, QString* error = nullptr);
    bool
    loadFromFile(const QString& path, const Skin* fallback = nullptr, QString* error = nullptr);

    // Built-in default skin from Qt resources.
    static Skin BuiltinBase();

    bool isValid() const { return !sheets.value(Sheet::Main).isNull(); }

    const QImage& sheet(Sheet sheet) const;
    bool numbersAreExtended() const { return numbersEx; }

    // Draws `src` from sheet `s` at `dst` (in skin pixels; painter handles scaling).
    void draw(QPainter& painter, Sheet bitmap, const QRect& src, const QPoint& dst) const;

    // Title text of generic windows, from the A-Z letters in GEN.BMP
    // (variable width). Other characters are skipped. Returns the width.
    int drawGenText(QPainter& painter, const QPoint& at, const QString& text, bool selected) const;
    int genTextWidth(const QString& text) const;

    // Draws text with the TEXT.BMP font. Returns the width in pixels.
    int drawText(QPainter& painter, const QPoint& at, const QString& text, int maxWidth = -1) const;
    static int TextWidth(const QString& text);

    const TRegionData& region() const { return regionData; }
    const QList<QColor>& visColors() const { return visualizationColors; }  // 24 entries
    const PlaylistStyle& playlistStyle() const { return plStyle; }

    static PlaylistStyle ParsePlaylistStyle(const QByteArray& text);

private:
    QHash<Sheet, QImage> sheets;
    TRegionData regionData;
    QList<QColor> visualizationColors;
    PlaylistStyle plStyle;
    // x offset and width of each gen.bmp letter A-Z (same for both rows in practice).
    QList<std::pair<int, int>> genLetters;
    QList<std::pair<int, int>> genLettersSelected;
    void measureGenLetters();
    bool numbersEx = false;
};

size_t qHash(Skin::Sheet sheet, size_t seed = 0) noexcept;

}  // namespace Skins
