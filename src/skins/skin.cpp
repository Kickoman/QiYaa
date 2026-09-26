#include "skins/skin.h"

#include "skins/error.h"
#include "skins/sprites.h"

#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QHash>
#include <QImageReader>
#include <QRegularExpression>
#include <miniz.h>

#include <string>

namespace Skins {

size_t qHash(Skin::Sheet sheet, size_t seed) noexcept {
    return ::qHash(static_cast<int>(sheet), seed);
}

namespace {

constexpr qint64 kMaxArchiveBytes = 64 * 1024 * 1024;
constexpr mz_uint kMaxEntries = 4096;
constexpr mz_uint64 kMaxEntryBytes = 32 * 1024 * 1024;
constexpr qint64 kMaxTotalBytes = 64 * 1024 * 1024;

QHash<QString, QByteArray> ReadZip(const QByteArray& zip) {
    QHash<QString, QByteArray> files;
    mz_zip_archive archive{};
    if (!mz_zip_reader_init_mem(&archive, zip.constData(), size_t(zip.size()), 0)) {
        throw Skins::Error("not a zip archive (" + std::to_string(zip.size()) + " bytes)");
    }
    const mz_uint count = mz_zip_reader_get_num_files(&archive);
    if (count > kMaxEntries) {
        mz_zip_reader_end(&archive);
        throw Skins::Error(
            "the archive lists " + std::to_string(count) + " files; a skin has at most "
            + std::to_string(kMaxEntries)
        );
    }
    qint64 total = 0;
    for (mz_uint i = 0; i < count; ++i) {
        if (mz_zip_reader_is_file_a_directory(&archive, i)) {
            continue;
        }
        mz_zip_archive_file_stat st{};
        if (!mz_zip_reader_file_stat(&archive, i, &st)) {
            continue;
        }
        const QString name = QFileInfo(QString::fromUtf8(st.m_filename)).fileName().toLower();
        if (files.contains(name)) {
            continue;
        }
        if (st.m_uncomp_size > kMaxEntryBytes
            || total + qint64(st.m_uncomp_size) > kMaxTotalBytes) {
            continue;
        }
        total += qint64(st.m_uncomp_size);
        size_t size = 0;
        void* data = mz_zip_reader_extract_to_heap(&archive, i, &size, 0);
        if (!data) {
            continue;
        }
        files.insert(name, QByteArray(static_cast<const char*>(data), qsizetype(size)));
        mz_free(data);
    }
    mz_zip_reader_end(&archive);
    return files;
}

QImage DecodeImage(const QByteArray& bytes) {
    QBuffer buf;
    buf.setData(bytes);
    buf.open(QIODevice::ReadOnly);
    QImageReader reader(&buf);
    reader.setDecideFormatFromContent(true);
    QImage image = reader.read();
    if (image.isNull()) {
        return image;
    }
    return image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

QList<QColor> ParseVisColors(const QByteArray& text) {
    static const QRegularExpression rgb(QStringLiteral("^\\s*(\\d+)\\s*,\\s*(\\d+)\\s*,\\s*(\\d+)")
    );
    QList<QColor> colors;
    for (const QByteArray& line : text.split('\n')) {
        const auto m = rgb.match(QString::fromLatin1(line));
        if (!m.hasMatch()) {
            continue;
        }
        colors << QColor(m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt());
        if (colors.size() == 24) {
            break;
        }
    }
    return colors;
}

// Port of webamp's FONT_LOOKUP.
bool FontCell(QChar c, int* row, int* col) {
    static const QHash<char16_t, std::pair<int, int>> table = [] {
        QHash<char16_t, std::pair<int, int>> t;
        for (int i = 0; i < 26; ++i) {
            t.insert(char16_t(u'a' + i), {0, i});
        }
        for (int i = 0; i < 10; ++i) {
            t.insert(char16_t(u'0' + i), {1, i});
        }
        const std::pair<char16_t, std::pair<int, int>> extra[] = {
            {u'"', {0, 26}}, {u'@', {0, 27}}, {u' ', {0, 30}}, {u'…', {1, 10}},  {u'.', {1, 11}},
            {u':', {1, 12}}, {u'(', {1, 13}}, {u')', {1, 14}}, {u'-', {1, 15}},  {u'\'', {1, 16}},
            {u'!', {1, 17}}, {u'_', {1, 18}}, {u'+', {1, 19}}, {u'\\', {1, 20}}, {u'/', {1, 21}},
            {u'[', {1, 22}}, {u']', {1, 23}}, {u'^', {1, 24}}, {u'&', {1, 25}},  {u'%', {1, 26}},
            {u',', {1, 27}}, {u'=', {1, 28}}, {u'$', {1, 29}}, {u'#', {1, 30}},  {u'Å', {2, 0}},
            {u'Ö', {2, 1}},  {u'Ä', {2, 2}},  {u'?', {2, 3}},  {u'*', {2, 4}},   {u'<', {1, 22}},
            {u'>', {1, 23}}, {u'{', {1, 22}}, {u'}', {1, 23}},
        };
        for (const auto& [ch, pos] : extra) {
            t.insert(ch, pos);
        }
        return t;
    }();
    auto it = table.constFind(c.toLower().unicode());
    if (it == table.cend()) {
        it = table.constFind(c.unicode());
        if (it == table.cend()) {
            return false;
        }
    }
    *row = it->first;
    *col = it->second;
    return true;
}

struct PixelGlyph {
    char16_t ch;
    int width;
    const char* rows[6];
};

constexpr PixelGlyph kCyrillicGlyphs[] = {
    {u'Б', 4, {"####", "#...", "###.", "#..#", "#..#", "###."}},
    {u'Г', 4, {"####", "#...", "#...", "#...", "#...", "#..."}},
    {u'Ґ', 4, {"...#", "####", "#...", "#...", "#...", "#..."}},
    {u'Д', 4, {".###", ".#.#", ".#.#", ".#.#", "####", "#..#"}},
    {u'Ж', 5, {"#.#.#", "#.#.#", ".###.", "#.#.#", "#.#.#", "#.#.#"}},
    {u'И', 4, {"#..#", "#..#", "#.##", "##.#", "#..#", "#..#"}},
    {u'Й', 4, {".##.", "....", "#..#", "#.##", "##.#", "#..#"}},
    {u'Л', 4, {".###", ".#.#", ".#.#", ".#.#", ".#.#", "##.#"}},
    {u'П', 4, {"####", "#..#", "#..#", "#..#", "#..#", "#..#"}},
    {u'Ф', 5, {".###.", "#.#.#", "#.#.#", ".###.", "..#..", "..#.."}},
    {u'Ц', 4, {"#.#.", "#.#.", "#.#.", "#.#.", "####", "...#"}},
    {u'Ч', 4, {"#..#", "#..#", "#..#", ".###", "...#", "...#"}},
    {u'Ш', 5, {"#.#.#", "#.#.#", "#.#.#", "#.#.#", "#.#.#", "#####"}},
    {u'Щ', 5, {"#.#.#", "#.#.#", "#.#.#", "#.#.#", "#####", "....#"}},
    {u'Ъ', 5, {"##...", ".#...", ".###.", ".#..#", ".#..#", ".###."}},
    {u'Ы', 5, {"#...#", "#...#", "###.#", "#.#.#", "#.#.#", "###.#"}},
    {u'Ь', 4, {"#...", "#...", "###.", "#..#", "#..#", "###."}},
    {u'Э', 4, {"###.", "...#", ".###", "...#", "...#", "###."}},
    {u'Є', 4, {".###", "#...", "###.", "#...", "#...", ".###"}},
    {u'Ю', 5, {"#..#.", "#.#.#", "###.#", "#.#.#", "#.#.#", "#..#."}},
    {u'Я', 4, {".###", "#..#", "#..#", ".###", ".#.#", "#..#"}},
};

char16_t CyrillicLookalike(char16_t upper) {
    switch (upper) {
        case u'А': return u'a';
        case u'В': return u'b';
        case u'Е':
        case u'Ё': return u'e';
        case u'З': return u'3';
        case u'К': return u'k';
        case u'М': return u'm';
        case u'Н': return u'h';
        case u'О': return u'o';
        case u'Р': return u'p';
        case u'С': return u'c';
        case u'Т': return u't';
        case u'У':
        case u'Ў': return u'y';
        case u'Х': return u'x';
        case u'І':
        case u'Ї': return u'i';
        default: return 0;
    }
}

const PixelGlyph* FindPixelGlyph(char16_t upper) {
    for (const PixelGlyph& g : kCyrillicGlyphs) {
        if (g.ch == upper) {
            return &g;
        }
    }
    return nullptr;
}

struct CharRender {
    enum Kind { Cell, Pixel, SystemFont } kind = SystemFont;
    int row = 0, col = 0;
    const PixelGlyph* glyph = nullptr;
    int advance = 0;
};

const QFont& FallbackFont() {
    static const QFont f = [] {
        QFont font(QStringLiteral("Sans Serif"));
        font.setPixelSize(7);
        font.setHintingPreference(QFont::PreferFullHinting);
        font.setStyleStrategy(QFont::NoAntialias);
        return font;
    }();
    return f;
}

QColor TextInkColor(const QImage& text) {
    if (text.isNull()) {
        return Qt::green;
    }
    const QRgb bg = text.pixel(text.width() - 1, 0);
    QHash<QRgb, int> counts;
    const int h = std::min(text.height(), 6);
    const int w = std::min(text.width(), 26 * Skins::kCharWidth);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const QRgb px = text.pixel(x, y);
            if (px != bg) {
                ++counts[px];
            }
        }
    }
    QRgb best = qRgb(0, 255, 0);
    int bestCount = 0;
    for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
        if (it.value() > bestCount) {
            bestCount = it.value();
            best = it.key();
        }
    }
    return QColor::fromRgb(best);
}

}  // namespace

Skin Skin::LoadWsz(const QByteArray& archive, const Skin* fallback) {
    Skin skin;
    skin.loadArchive(archive, fallback);
    return skin;
}

Skin Skin::LoadFile(const QString& path, const Skin* fallback) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        throw Error((path + QStringLiteral(": ") + file.errorString()).toStdString());
    }
    if (file.size() > kMaxArchiveBytes) {
        throw Error((path
                     + QStringLiteral(": %1 bytes, more than the %2 a skin may have")
                           .arg(file.size())
                           .arg(kMaxArchiveBytes))
                        .toStdString());
    }
    try {
        return LoadWsz(file.readAll(), fallback);
    } catch (const Error& error) {
        throw Error(path.toStdString() + ": " + error.what());
    }
}

void Skin::loadArchive(const QByteArray& archive, const Skin* fallback) {
    const QHash<QString, QByteArray> files = ReadZip(archive);
    if (files.isEmpty()) {
        throw Error("the archive holds no files");
    }

    auto load = [&](Sheet sheet, std::initializer_list<const char*> names) {
        for (const char* n : names) {
            const auto it = files.constFind(QString::fromLatin1(n));
            if (it == files.cend()) {
                continue;
            }
            QImage image = DecodeImage(*it);
            if (!image.isNull()) {
                sheets.insert(sheet, image);
                return true;
            }
        }
        if (fallback && !fallback->sheet(sheet).isNull()) {
            sheets.insert(sheet, fallback->sheet(sheet));
        }
        return false;
    };

    sheets.clear();
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
    if (!load(Sheet::Balance, {"balance.bmp"}) && hasVolume) {
        sheets.insert(Sheet::Balance, sheets.value(Sheet::Volume));
    }

    if (files.contains(QStringLiteral("nums_ex.bmp")) && load(Sheet::Numbers, {"nums_ex.bmp"})) {
        numbersEx = true;
    } else if (load(Sheet::Numbers, {"numbers.bmp"})) {
        numbersEx = false;
    } else {
        numbersEx = fallback ? fallback->numbersEx : false;
    }

    regionData = ParseRegionTxt(files.value(QStringLiteral("region.txt")));
    if (files.contains(QStringLiteral("pledit.txt"))) {
        plStyle = ParsePlaylistStyle(files.value(QStringLiteral("pledit.txt")));
    } else if (fallback) {
        plStyle = fallback->plStyle;
    }
    visualizationColors = ParseVisColors(files.value(QStringLiteral("viscolor.txt")));
    if (visualizationColors.size() < 24 && fallback) {
        visualizationColors = fallback->visualizationColors;
    }

    if (!isValid()) {
        throw Error("main.bmp is missing or cannot be decoded");
    }
}

void Skin::measureGenLetters() {
    // Port of webamp's genGenTextSprites().
    auto measure = [](const QImage& image, int y) {
        QList<std::pair<int, int>> out;
        if (image.isNull() || y >= image.height()) {
            return out;
        }
        const QRgb bg = image.pixel(0, y);
        int x = 1;
        for (int i = 0; i < 26; ++i) {
            int next = x;
            while (next < image.width() && image.pixel(next, y) != bg) {
                ++next;
            }
            out.append({x, next - x});
            x = next + 1;
        }
        return out;
    };
    const QImage& gen = sheet(Sheet::Gen);
    genLettersSelected = measure(gen, Skins::GenWindowSprites::kLettersYSelected);
    genLetters = measure(gen, Skins::GenWindowSprites::kLettersY);
}

int Skin::genTextWidth(const QString& text) const {
    int w = 0;
    for (QChar character : text) {
        const int i = character.toUpper().unicode() - u'A';
        if (character == u' ') {
            w += 5;
        } else if (i >= 0 && i < genLetters.size()) {
            w += genLetters[i].second;
        }
    }
    return w;
}

int Skin::drawGenText(QPainter& painter, const QPoint& at, const QString& text, bool selected)
    const {
    const auto& letters = selected ? genLettersSelected : genLetters;
    const int y =
        selected ? Skins::GenWindowSprites::kLettersYSelected : Skins::GenWindowSprites::kLettersY;
    int x = at.x();
    for (QChar character : text) {
        const int i = character.toUpper().unicode() - u'A';
        if (character == u' ') {
            x += 5;
        } else if (i >= 0 && i < letters.size()) {
            draw(
                painter, Sheet::Gen,
                QRect(
                    letters[i].first, y, letters[i].second, Skins::GenWindowSprites::kLetterHeight
                ),
                QPoint(x, at.y())
            );
            x += letters[i].second;
        }
    }
    return x - at.x();
}

Skin::PlaylistStyle Skin::ParsePlaylistStyle(const QByteArray& text) {
    PlaylistStyle style;
    static const QRegularExpression line(QStringLiteral("^\\s*([A-Za-z]+)\\s*=\\s*(.*?)\\s*$"));
    for (const QByteArray& raw : text.split('\n')) {
        const auto m = line.match(QString::fromLatin1(raw).remove(u'\r'));
        if (!m.hasMatch()) {
            continue;
        }
        const QString key = m.captured(1).toLower();
        QString value = m.captured(2);
        if (key == QLatin1String("font")) {
            if (!value.isEmpty()) {
                style.font = value;
            }
            continue;
        }
        if (!value.startsWith(u'#')) {
            value.prepend(u'#');
        }
        const QColor c = QColor::fromString(value.left(7));
        if (!c.isValid()) {
            continue;
        }
        if (key == QLatin1String("normal")) {
            style.normal = c;
        } else if (key == QLatin1String("current")) {
            style.current = c;
        } else if (key == QLatin1String("normalbg")) {
            style.normalBg = c;
        } else if (key == QLatin1String("selectedbg")) {
            style.selectedBg = c;
        }
    }
    return style;
}

Skin Skin::BuiltinBase() {
    return LoadFile(QStringLiteral(":/skins/base-2.91.wsz"));
}

const QImage& Skin::sheet(Sheet sheet) const {
    static const QImage empty;
    const auto it = sheets.constFind(sheet);
    return it == sheets.cend() ? empty : *it;
}

void Skin::draw(QPainter& painter, Sheet bitmap, const QRect& src, const QPoint& dst) const {
    const QImage& image = sheet(bitmap);
    if (image.isNull()) {
        return;
    }
    painter.drawImage(dst, image, src);
}

namespace {

CharRender ResolveChar(QChar ch) {
    CharRender r;
    r.advance = Skins::kCharWidth;
    if (FontCell(ch, &r.row, &r.col)) {
        r.kind = CharRender::Cell;
        return r;
    }
    const char16_t upper = ch.toUpper().unicode();
    if (const char16_t alike = CyrillicLookalike(upper);
        alike && FontCell(QChar(alike), &r.row, &r.col)) {
        r.kind = CharRender::Cell;
        return r;
    }
    if (const PixelGlyph* g = FindPixelGlyph(upper)) {
        r.kind = CharRender::Pixel;
        r.glyph = g;
        r.advance = g->width + 1;
        return r;
    }
    // Accented Latin: drop the accent, like webamp's deburr().
    const QString base = QString(ch).normalized(QString::NormalizationForm_D);
    if (!base.isEmpty() && base.at(0) != ch && FontCell(base.at(0), &r.row, &r.col)) {
        r.kind = CharRender::Cell;
        return r;
    }
    r.kind = CharRender::SystemFont;
    r.advance = QFontMetrics(FallbackFont()).horizontalAdvance(ch);
    return r;
}

}  // namespace

int Skin::TextWidth(const QString& text) {
    int w = 0;
    for (QChar ch : text) {
        w += ResolveChar(ch).advance;
    }
    return w;
}

int Skin::drawText(QPainter& painter, const QPoint& at, const QString& text, int maxWidth) const {
    const QImage& font = sheet(Sheet::Text);
    const QRect spaceCell(30 * Skins::kCharWidth, 0, Skins::kCharWidth, Skins::kCharHeight);
    QColor ink;
    int x = at.x();
    for (QChar ch : text) {
        const CharRender r = ResolveChar(ch);
        if (maxWidth >= 0 && x + r.advance > at.x() + maxWidth) {
            break;
        }
        if ((r.kind == CharRender::Pixel || r.kind == CharRender::SystemFont) && !ink.isValid()) {
            ink = TextInkColor(font);
        }

        switch (r.kind) {
            case CharRender::Cell:
                painter.drawImage(
                    QPoint(x, at.y()), font,
                    QRect(
                        r.col * Skins::kCharWidth, r.row * Skins::kCharHeight, Skins::kCharWidth,
                        Skins::kCharHeight
                    )
                );
                break;
            case CharRender::Pixel:
                for (int bx = 0; bx < r.advance; bx += Skins::kCharWidth) {
                    painter.drawImage(
                        QPoint(x + bx, at.y()), font,
                        spaceCell.adjusted(0, 0, std::min(0, r.advance - bx - Skins::kCharWidth), 0)
                    );
                }
                for (int row = 0; row < 6; ++row) {
                    for (int col = 0; col < r.glyph->width; ++col) {
                        if (r.glyph->rows[row][col] == '#') {
                            painter.fillRect(x + col, at.y() + row, 1, 1, ink);
                        }
                    }
                }
                break;
            case CharRender::SystemFont:
                painter.setFont(FallbackFont());
                painter.setPen(ink);
                painter.drawText(
                    QRect(x, at.y() - 1, r.advance, Skins::kCharHeight + 2),
                    Qt::AlignLeft | Qt::AlignVCenter, QString(ch)
                );
                break;
        }
        x += r.advance;
    }
    return x - at.x();
}

}  // namespace Skins
