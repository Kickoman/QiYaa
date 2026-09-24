#include "skin/Skin.h"

#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QHash>
#include <QImageReader>
#include <QRegularExpression>

#include <miniz.h>

#include "skin/SkinSprites.h"

namespace qiyaa {

size_t qHash(Skin::Sheet s, size_t seed) noexcept {
    return ::qHash(static_cast<int>(s), seed);
}

namespace {

// Reads all files of a zip into memory, keyed by lower-case base name
// ("main.bmp"). Skins often put files in a sub-folder and use random case.
QHash<QString, QByteArray> readZip(const QByteArray& zip, QString* error) {
    QHash<QString, QByteArray> files;
    mz_zip_archive archive{};
    if (!mz_zip_reader_init_mem(&archive, zip.constData(), size_t(zip.size()), 0)) {
        if (error) *error = QStringLiteral("not a zip archive");
        return files;
    }
    const mz_uint count = mz_zip_reader_get_num_files(&archive);
    for (mz_uint i = 0; i < count; ++i) {
        if (mz_zip_reader_is_file_a_directory(&archive, i)) continue;
        mz_zip_archive_file_stat st{};
        if (!mz_zip_reader_file_stat(&archive, i, &st)) continue;
        const QString name = QFileInfo(QString::fromUtf8(st.m_filename)).fileName().toLower();
        if (files.contains(name)) continue;  // first one wins
        if (st.m_uncomp_size > 32u * 1024u * 1024u) continue;  // sanity limit
        size_t size = 0;
        void* data = mz_zip_reader_extract_to_heap(&archive, i, &size, 0);
        if (!data) continue;
        files.insert(name, QByteArray(static_cast<const char*>(data), qsizetype(size)));
        mz_free(data);
    }
    mz_zip_reader_end(&archive);
    return files;
}

QImage decodeImage(const QByteArray& bytes) {
    QBuffer buf;
    buf.setData(bytes);
    buf.open(QIODevice::ReadOnly);
    QImageReader reader(&buf);
    reader.setDecideFormatFromContent(true);
    QImage img = reader.read();
    if (img.isNull()) return img;
    return img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

QList<QColor> parseVisColors(const QByteArray& text) {
    static const QRegularExpression rgb(QStringLiteral("^\\s*(\\d+)\\s*,\\s*(\\d+)\\s*,\\s*(\\d+)"));
    QList<QColor> colors;
    for (const QByteArray& line : text.split('\n')) {
        const auto m = rgb.match(QString::fromLatin1(line));
        if (!m.hasMatch()) continue;
        colors << QColor(m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt());
        if (colors.size() == 24) break;
    }
    return colors;
}

// Position of a character in TEXT.BMP (row, column), per webamp's FONT_LOOKUP.
bool fontCell(QChar c, int* row, int* col) {
    static const QHash<char16_t, std::pair<int, int>> table = [] {
        QHash<char16_t, std::pair<int, int>> t;
        for (int i = 0; i < 26; ++i) t.insert(char16_t(u'a' + i), {0, i});
        for (int i = 0; i < 10; ++i) t.insert(char16_t(u'0' + i), {1, i});
        const std::pair<char16_t, std::pair<int, int>> extra[] = {
            {u'"', {0, 26}}, {u'@', {0, 27}}, {u' ', {0, 30}},
            {u'…', {1, 10}}, {u'.', {1, 11}}, {u':', {1, 12}}, {u'(', {1, 13}},
            {u')', {1, 14}}, {u'-', {1, 15}}, {u'\'', {1, 16}}, {u'!', {1, 17}},
            {u'_', {1, 18}}, {u'+', {1, 19}}, {u'\\', {1, 20}}, {u'/', {1, 21}},
            {u'[', {1, 22}}, {u']', {1, 23}}, {u'^', {1, 24}}, {u'&', {1, 25}},
            {u'%', {1, 26}}, {u',', {1, 27}}, {u'=', {1, 28}}, {u'$', {1, 29}},
            {u'#', {1, 30}}, {u'Å', {2, 0}}, {u'Ö', {2, 1}}, {u'Ä', {2, 2}},
            {u'?', {2, 3}}, {u'*', {2, 4}}, {u'<', {1, 22}}, {u'>', {1, 23}},
            {u'{', {1, 22}}, {u'}', {1, 23}},
        };
        for (const auto& [ch, pos] : extra) t.insert(ch, pos);
        return t;
    }();
    auto it = table.constFind(c.toLower().unicode());
    if (it == table.cend()) {
        // Upper-case Å/Ö/Ä are in the table as-is.
        it = table.constFind(c.unicode());
        if (it == table.cend()) return false;
    }
    *row = it->first;
    *col = it->second;
    return true;
}

// TEXT.BMP only has Latin letters. Cyrillic letters that look like Latin ones
// reuse the skin's own glyphs; the rest are drawn from these 6-row bitmaps in
// the skin's text colour, so titles look native in any skin.
struct PixelGlyph {
    char16_t ch;
    int width;  // ink columns; advance is width + 1
    const char* rows[6];
};

constexpr PixelGlyph kCyrillicGlyphs[] = {
    {u'Б', 4, {"####", "#...", "###.", "#..#", "#..#", "###."}},       // Б
    {u'Г', 4, {"####", "#...", "#...", "#...", "#...", "#..."}},       // Г
    {u'Ґ', 4, {"...#", "####", "#...", "#...", "#...", "#..."}},       // Ґ
    {u'Д', 4, {".###", ".#.#", ".#.#", ".#.#", "####", "#..#"}},       // Д
    {u'Ж', 5, {"#.#.#", "#.#.#", ".###.", "#.#.#", "#.#.#", "#.#.#"}}, // Ж
    {u'И', 4, {"#..#", "#..#", "#.##", "##.#", "#..#", "#..#"}},       // И
    {u'Й', 4, {".##.", "....", "#..#", "#.##", "##.#", "#..#"}},       // Й
    {u'Л', 4, {".###", ".#.#", ".#.#", ".#.#", ".#.#", "##.#"}},       // Л
    {u'П', 4, {"####", "#..#", "#..#", "#..#", "#..#", "#..#"}},       // П
    {u'Ф', 5, {".###.", "#.#.#", "#.#.#", ".###.", "..#..", "..#.."}}, // Ф
    {u'Ц', 4, {"#.#.", "#.#.", "#.#.", "#.#.", "####", "...#"}},       // Ц
    {u'Ч', 4, {"#..#", "#..#", "#..#", ".###", "...#", "...#"}},       // Ч
    {u'Ш', 5, {"#.#.#", "#.#.#", "#.#.#", "#.#.#", "#.#.#", "#####"}}, // Ш
    {u'Щ', 5, {"#.#.#", "#.#.#", "#.#.#", "#.#.#", "#####", "....#"}}, // Щ
    {u'Ъ', 5, {"##...", ".#...", ".###.", ".#..#", ".#..#", ".###."}}, // Ъ
    {u'Ы', 5, {"#...#", "#...#", "###.#", "#.#.#", "#.#.#", "###.#"}}, // Ы
    {u'Ь', 4, {"#...", "#...", "###.", "#..#", "#..#", "###."}},       // Ь
    {u'Э', 4, {"###.", "...#", ".###", "...#", "...#", "###."}},       // Э
    {u'Є', 4, {".###", "#...", "###.", "#...", "#...", ".###"}},       // Є
    {u'Ю', 5, {"#..#.", "#.#.#", "###.#", "#.#.#", "#.#.#", "#..#."}}, // Ю
    {u'Я', 4, {".###", "#..#", "#..#", ".###", ".#.#", "#..#"}},       // Я
};

// Cyrillic -> Latin/digit glyph that looks the same in the Winamp font.
char16_t cyrillicLookalike(char16_t upper) {
    switch (upper) {
    case u'А': return u'a';  // А
    case u'В': return u'b';  // В
    case u'Е': case u'Ё': return u'e';  // Е Ё
    case u'З': return u'3';  // З
    case u'К': return u'k';  // К
    case u'М': return u'm';  // М
    case u'Н': return u'h';  // Н
    case u'О': return u'o';  // О
    case u'Р': return u'p';  // Р
    case u'С': return u'c';  // С
    case u'Т': return u't';  // Т
    case u'У': case u'Ў': return u'y';  // У Ў
    case u'Х': return u'x';  // Х
    case u'І': case u'Ї': return u'i';  // І Ї
    default: return 0;
    }
}

const PixelGlyph* pixelGlyph(char16_t upper) {
    for (const PixelGlyph& g : kCyrillicGlyphs)
        if (g.ch == upper) return &g;
    return nullptr;
}

// How one character is drawn.
struct CharRender {
    enum Kind { Cell, Pixel, SystemFont } kind = SystemFont;
    int row = 0, col = 0;              // Cell
    const PixelGlyph* glyph = nullptr; // Pixel
    int advance = 0;
};

// Font for characters that TEXT.BMP doesn't have (Cyrillic etc.).
const QFont& fallbackFont() {
    static const QFont f = [] {
        QFont font(QStringLiteral("Sans Serif"));
        font.setPixelSize(7);
        font.setHintingPreference(QFont::PreferFullHinting);
        font.setStyleStrategy(QFont::NoAntialias);
        return font;
    }();
    return f;
}

// The "ink" colour of TEXT.BMP: the most common colour that is not the
// background (pixel 0,0 is the background in practice).
QColor textInkColor(const QImage& text) {
    if (text.isNull()) return Qt::green;
    const QRgb bg = text.pixel(text.width() - 1, 0);
    QHash<QRgb, int> counts;
    const int h = std::min(text.height(), 6);
    const int w = std::min(text.width(), 26 * sprites::kCharW);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const QRgb px = text.pixel(x, y);
            if (px != bg) ++counts[px];
        }
    QRgb best = qRgb(0, 255, 0);
    int bestCount = 0;
    for (auto it = counts.cbegin(); it != counts.cend(); ++it)
        if (it.value() > bestCount) {
            bestCount = it.value();
            best = it.key();
        }
    return QColor::fromRgb(best);
}

}  // namespace

bool Skin::loadFromWsz(const QByteArray& zip, const Skin* fallback, QString* error) {
    const QHash<QString, QByteArray> files = readZip(zip, error);
    if (files.isEmpty()) {
        if (error && error->isEmpty()) *error = QStringLiteral("empty archive");
        return false;
    }

    auto load = [&](Sheet s, std::initializer_list<const char*> names) {
        for (const char* n : names) {
            const auto it = files.constFind(QString::fromLatin1(n));
            if (it == files.cend()) continue;
            QImage img = decodeImage(*it);
            if (!img.isNull()) {
                m_sheets.insert(s, img);
                return true;
            }
        }
        if (fallback && !fallback->sheet(s).isNull()) m_sheets.insert(s, fallback->sheet(s));
        return false;
    };

    m_sheets.clear();
    load(Sheet::Main, {"main.bmp"});
    load(Sheet::CButtons, {"cbuttons.bmp"});
    load(Sheet::TitleBar, {"titlebar.bmp"});
    load(Sheet::PlayPaus, {"playpaus.bmp"});
    load(Sheet::MonoSter, {"monoster.bmp"});
    load(Sheet::PosBar, {"posbar.bmp"});
    load(Sheet::ShufRep, {"shufrep.bmp"});
    load(Sheet::Text, {"text.bmp"});
    load(Sheet::EqMain, {"eqmain.bmp"});
    load(Sheet::PlEdit, {"pledit.bmp"});
    load(Sheet::EqEx, {"eq_ex.bmp"});
    load(Sheet::Gen, {"gen.bmp"});
    measureGenLetters();
    const bool hasVolume = load(Sheet::Volume, {"volume.bmp"});
    if (!load(Sheet::Balance, {"balance.bmp"}) && hasVolume)
        m_sheets.insert(Sheet::Balance, m_sheets.value(Sheet::Volume));

    if (files.contains(QStringLiteral("nums_ex.bmp")) && load(Sheet::Numbers, {"nums_ex.bmp"})) {
        m_numbersEx = true;
    } else if (load(Sheet::Numbers, {"numbers.bmp"})) {
        m_numbersEx = false;
    } else {
        m_numbersEx = fallback ? fallback->m_numbersEx : false;
    }

    m_region = parseRegionTxt(files.value(QStringLiteral("region.txt")));
    if (files.contains(QStringLiteral("pledit.txt")))
        m_plStyle = parsePlaylistStyle(files.value(QStringLiteral("pledit.txt")));
    else if (fallback)
        m_plStyle = fallback->m_plStyle;
    m_visColors = parseVisColors(files.value(QStringLiteral("viscolor.txt")));
    if (m_visColors.size() < 24 && fallback) m_visColors = fallback->m_visColors;

    if (!isValid()) {
        if (error) *error = QStringLiteral("main.bmp is missing or unreadable");
        return false;
    }
    return true;
}

void Skin::measureGenLetters() {
    // Letters sit side by side, separated by one column of the background
    // colour (taken from x=0). Port of webamp's genGenTextSprites().
    auto measure = [](const QImage& img, int y) {
        QList<std::pair<int, int>> out;
        if (img.isNull() || y >= img.height()) return out;
        const QRgb bg = img.pixel(0, y);
        int x = 1;
        for (int i = 0; i < 26; ++i) {
            int next = x;
            while (next < img.width() && img.pixel(next, y) != bg) ++next;
            out.append({x, next - x});
            x = next + 1;
        }
        return out;
    };
    const QImage& gen = sheet(Sheet::Gen);
    m_genLettersSelected = measure(gen, sprites::gen::kLettersYSelected);
    m_genLetters = measure(gen, sprites::gen::kLettersY);
}

int Skin::genTextWidth(const QString& text) const {
    int w = 0;
    for (QChar c : text) {
        const int i = c.toUpper().unicode() - u'A';
        if (c == u' ') w += 5;
        else if (i >= 0 && i < m_genLetters.size()) w += m_genLetters[i].second;
    }
    return w;
}

int Skin::drawGenText(QPainter& p, const QPoint& at, const QString& text, bool selected) const {
    const auto& letters = selected ? m_genLettersSelected : m_genLetters;
    const int y = selected ? sprites::gen::kLettersYSelected : sprites::gen::kLettersY;
    int x = at.x();
    for (QChar c : text) {
        const int i = c.toUpper().unicode() - u'A';
        if (c == u' ') {
            x += 5;
        } else if (i >= 0 && i < letters.size()) {
            draw(p, Sheet::Gen, QRect(letters[i].first, y, letters[i].second, sprites::gen::kLetterH), QPoint(x, at.y()));
            x += letters[i].second;
        }
    }
    return x - at.x();
}

Skin::PlaylistStyle Skin::parsePlaylistStyle(const QByteArray& text) {
    PlaylistStyle st;
    static const QRegularExpression line(QStringLiteral("^\\s*([A-Za-z]+)\\s*=\\s*(.*?)\\s*$"));
    for (const QByteArray& raw : text.split('\n')) {
        const auto m = line.match(QString::fromLatin1(raw).remove(u'\r'));
        if (!m.hasMatch()) continue;
        const QString key = m.captured(1).toLower();
        QString value = m.captured(2);
        if (key == QLatin1String("font")) {
            if (!value.isEmpty()) st.font = value;
            continue;
        }
        if (!value.startsWith(u'#')) value.prepend(u'#');
        const QColor c = QColor::fromString(value.left(7));
        if (!c.isValid()) continue;
        if (key == QLatin1String("normal")) st.normal = c;
        else if (key == QLatin1String("current")) st.current = c;
        else if (key == QLatin1String("normalbg")) st.normalBg = c;
        else if (key == QLatin1String("selectedbg")) st.selectedBg = c;
    }
    return st;
}

bool Skin::loadFromFile(const QString& path, const Skin* fallback, QString* error) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    return loadFromWsz(f.readAll(), fallback, error);
}

Skin Skin::builtinBase() {
    Skin s;
    QString err;
    if (!s.loadFromFile(QStringLiteral(":/skins/base-2.91.wsz"), nullptr, &err))
        qWarning("Built-in skin failed to load: %s", qPrintable(err));
    return s;
}

const QImage& Skin::sheet(Sheet s) const {
    static const QImage empty;
    const auto it = m_sheets.constFind(s);
    return it == m_sheets.cend() ? empty : *it;
}

void Skin::draw(QPainter& p, Sheet s, const QRect& src, const QPoint& dst) const {
    const QImage& img = sheet(s);
    if (img.isNull()) return;
    p.drawImage(dst, img, src);
}

namespace {

CharRender resolveChar(QChar ch) {
    CharRender r;
    r.advance = sprites::kCharW;
    if (fontCell(ch, &r.row, &r.col)) {
        r.kind = CharRender::Cell;
        return r;
    }
    const char16_t upper = ch.toUpper().unicode();
    if (const char16_t alike = cyrillicLookalike(upper); alike && fontCell(QChar(alike), &r.row, &r.col)) {
        r.kind = CharRender::Cell;
        return r;
    }
    if (const PixelGlyph* g = pixelGlyph(upper)) {
        r.kind = CharRender::Pixel;
        r.glyph = g;
        r.advance = g->width + 1;
        return r;
    }
    // Accented Latin (é, ñ, ü...): drop the accent like webamp's deburr().
    const QString base = QString(ch).normalized(QString::NormalizationForm_D);
    if (!base.isEmpty() && base.at(0) != ch && fontCell(base.at(0), &r.row, &r.col)) {
        r.kind = CharRender::Cell;
        return r;
    }
    r.kind = CharRender::SystemFont;
    r.advance = QFontMetrics(fallbackFont()).horizontalAdvance(ch);
    return r;
}

}  // namespace

int Skin::textWidth(const QString& text) {
    int w = 0;
    for (QChar ch : text) w += resolveChar(ch).advance;
    return w;
}

int Skin::drawText(QPainter& p, const QPoint& at, const QString& text, int maxWidth) const {
    const QImage& font = sheet(Sheet::Text);
    const QRect spaceCell(30 * sprites::kCharW, 0, sprites::kCharW, sprites::kCharH);
    QColor ink;
    int x = at.x();
    for (QChar ch : text) {
        const CharRender r = resolveChar(ch);
        if (maxWidth >= 0 && x + r.advance > at.x() + maxWidth) break;
        if ((r.kind == CharRender::Pixel || r.kind == CharRender::SystemFont) && !ink.isValid())
            ink = textInkColor(font);

        switch (r.kind) {
        case CharRender::Cell:
            p.drawImage(QPoint(x, at.y()), font,
                        QRect(r.col * sprites::kCharW, r.row * sprites::kCharH, sprites::kCharW, sprites::kCharH));
            break;
        case CharRender::Pixel:
            // Background from the skin's space glyph, then the ink pixels.
            for (int bx = 0; bx < r.advance; bx += sprites::kCharW)
                p.drawImage(QPoint(x + bx, at.y()), font, spaceCell.adjusted(0, 0, std::min(0, r.advance - bx - sprites::kCharW), 0));
            for (int row = 0; row < 6; ++row)
                for (int col = 0; col < r.glyph->width; ++col)
                    if (r.glyph->rows[row][col] == '#') p.fillRect(x + col, at.y() + row, 1, 1, ink);
            break;
        case CharRender::SystemFont:
            p.setFont(fallbackFont());
            p.setPen(ink);
            p.drawText(QRect(x, at.y() - 1, r.advance, sprites::kCharH + 2), Qt::AlignLeft | Qt::AlignVCenter, QString(ch));
            break;
        }
        x += r.advance;
    }
    return x - at.x();
}

}  // namespace qiyaa
