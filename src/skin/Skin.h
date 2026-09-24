// A loaded Winamp 2.x skin (.wsz = zip of BMP sprite sheets + a few text files).
#pragma once

#include <QByteArray>
#include <QColor>
#include <QHash>
#include <QImage>
#include <QList>
#include <QPainter>
#include <QString>

#include "skin/Region.h"

namespace qiyaa {

class Skin {
public:
    enum class Sheet {
        Main,
        CButtons,
        TitleBar,
        Numbers,   // nums_ex.bmp if present, otherwise numbers.bmp
        PlayPaus,
        MonoSter,
        PosBar,
        ShufRep,
        Volume,
        Balance,   // falls back to volume.bmp like Winamp does
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
    bool loadFromWsz(const QByteArray& zip, const Skin* fallback = nullptr, QString* error = nullptr);
    bool loadFromFile(const QString& path, const Skin* fallback = nullptr, QString* error = nullptr);

    // Built-in default skin from Qt resources.
    static Skin builtinBase();

    bool isValid() const { return !m_sheets.value(Sheet::Main).isNull(); }

    const QImage& sheet(Sheet s) const;
    bool numbersAreExtended() const { return m_numbersEx; }

    // Draws `src` from sheet `s` at `dst` (in skin pixels; painter handles scaling).
    void draw(QPainter& p, Sheet s, const QRect& src, const QPoint& dst) const;

    // Title text of generic windows, from the A-Z letters in GEN.BMP
    // (variable width). Other characters are skipped. Returns the width.
    int drawGenText(QPainter& p, const QPoint& at, const QString& text, bool selected) const;
    int genTextWidth(const QString& text) const;

    // Draws text with the TEXT.BMP font. Returns the width in pixels.
    int drawText(QPainter& p, const QPoint& at, const QString& text, int maxWidth = -1) const;
    static int textWidth(const QString& text);

    const RegionData& region() const { return m_region; }
    const QList<QColor>& visColors() const { return m_visColors; }  // 24 entries
    const PlaylistStyle& playlistStyle() const { return m_plStyle; }

    static PlaylistStyle parsePlaylistStyle(const QByteArray& text);

private:
    QHash<Sheet, QImage> m_sheets;
    RegionData m_region;
    QList<QColor> m_visColors;
    PlaylistStyle m_plStyle;
    // x offset and width of each gen.bmp letter A-Z (same for both rows in practice).
    QList<std::pair<int, int>> m_genLetters;
    QList<std::pair<int, int>> m_genLettersSelected;
    void measureGenLetters();
    bool m_numbersEx = false;
};

size_t qHash(Skin::Sheet s, size_t seed = 0) noexcept;

}  // namespace qiyaa
