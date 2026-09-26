#include "audio/eq_presets.h"

#include "audio/error.h"

#include <QStringDecoder>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace Audio {

namespace {
struct WinampPreset {
    const char* name;
    int preamp;
    std::array<int, kEqBands> bands;
};

// Winamp's built-in presets (webamp's presets/builtin.json).
constexpr WinampPreset kWinampPresets[] = {
    {"Classical", 33, {33, 33, 33, 33, 33, 33, 20, 20, 20, 16}},
    {"Club", 33, {33, 33, 38, 42, 42, 42, 38, 33, 33, 33}},
    {"Dance", 33, {48, 44, 36, 32, 32, 22, 20, 20, 32, 32}},
    {"Laptop speakers/headphones", 33, {40, 50, 41, 26, 28, 35, 40, 48, 53, 56}},
    {"Large hall", 33, {49, 49, 42, 42, 33, 24, 24, 24, 33, 33}},
    {"Party", 33, {44, 44, 33, 33, 33, 33, 33, 33, 44, 44}},
    {"Pop", 33, {29, 40, 44, 45, 41, 30, 28, 28, 29, 29}},
    {"Reggae", 33, {33, 33, 31, 22, 33, 43, 43, 33, 33, 33}},
    {"Rock", 33, {45, 40, 23, 19, 26, 39, 47, 50, 50, 50}},
    {"Soft", 33, {40, 35, 30, 28, 30, 39, 46, 48, 50, 52}},
    {"Ska", 33, {28, 24, 25, 31, 39, 42, 47, 48, 50, 48}},
    {"Full Bass", 33, {48, 48, 48, 42, 35, 25, 18, 15, 14, 14}},
    {"Soft Rock", 33, {39, 39, 36, 31, 25, 23, 26, 31, 37, 47}},
    {"Full Treble", 33, {16, 16, 16, 25, 37, 50, 58, 58, 58, 60}},
    {"Full Bass & Treble", 33, {44, 42, 33, 20, 24, 35, 46, 50, 52, 52}},
    {"Live", 33, {24, 33, 39, 41, 42, 42, 39, 37, 37, 36}},
    {"Techno", 33, {45, 42, 33, 23, 24, 33, 45, 48, 48, 47}},
};

constexpr char kHeader[] = "Winamp EQ library file v1.1";
constexpr int kHeaderLength = sizeof(kHeader) - 1;  // 27
constexpr int kNameLength = 257;
constexpr int kValues = kEqBands + 1;

// Winamp wrote names in the Windows ANSI code page; we write UTF-8 unless the
// name fits the local 8-bit encoding (which is that code page on Windows).
QString DecodeName(const QByteArray& raw) {
    QStringDecoder utf8(QStringConverter::Utf8, QStringConverter::Flag::Stateless);
    const QString s = utf8.decode(raw);
    if (!utf8.hasError()) {
        return s;
    }
#ifdef Q_OS_WIN
    return QString::fromLocal8Bit(raw);
#else
    return QString::fromLatin1(raw);
#endif
}

QByteArray EncodeName(const QString& name) {
    QByteArray out;
    bool utf8 = true;
#ifdef Q_OS_WIN
    out = name.toLocal8Bit();
    utf8 = QString::fromLocal8Bit(out) != name;
#endif
    if (utf8) {
        out = name.toUtf8();
    }
    if (out.size() > kNameLength - 1) {
        out.truncate(kNameLength - 1);
        if (utf8) {  // don't cut a character in half
            while (!out.isEmpty() && (quint8(out.back()) & 0xC0) == 0x80) {
                out.chop(1);
            }
            if (!out.isEmpty() && quint8(out.back()) >= 0xC0) {
                out.chop(1);
            }
        }
    }
    return out;
}
}  // namespace

QList<EqPreset> ParseEqf(const QByteArray& data) {
    if (!data.startsWith(kHeader) || data.size() < kHeaderLength + 4) {
        throw Error(
            "not a Winamp EQ file: " + std::to_string(data.size())
            + " bytes that do not start with \"" + kHeader + "\""
        );
    }
    qsizetype i = kHeaderLength + 4;  // skip ^Z "!--"
    QList<EqPreset> presets;
    while (i + kNameLength + kValues <= data.size()) {
        const QByteArray rawName = data.mid(i, kNameLength);
        const qsizetype nul = rawName.indexOf('\0');
        EqPreset preset;
        preset.name = DecodeName(nul >= 0 ? rawName.left(nul) : rawName);
        i += kNameLength;
        auto value = [&](int k) { return 64 - int(quint8(data[i + k])); };
        for (int band = 0; band < kEqBands; ++band) {
            preset.settings.bandsDb[band] = std::round(EqfToDb(value(band)) * 10) / 10;
        }
        preset.settings.preampDb = std::round(EqfToDb(value(kEqBands)) * 10) / 10;
        i += kValues;
        presets << preset;
    }
    if (presets.isEmpty()) {
        throw Error(
            "Winamp EQ file of " + std::to_string(data.size())
            + " bytes holds no preset (one takes " + std::to_string(kNameLength + kValues)
            + " bytes after the " + std::to_string(kHeaderLength + 4) + "-byte header)"
        );
    }
    return presets;
}

QByteArray WriteEqf(const QList<EqPreset>& presets) {
    QByteArray out(kHeader);
    out += char(26);
    out += "!--";
    for (const EqPreset& preset : presets) {
        QByteArray name = EncodeName(preset.name);
        name.append(QByteArray(kNameLength - name.size(), '\0'));
        out += name;
        for (int band = 0; band < kEqBands; ++band) {
            out += char(64 - DbToEqf(preset.settings.bandsDb[band]));
        }
        out += char(64 - DbToEqf(preset.settings.preampDb));
    }
    return out;
}

// Winamp's centre notch is 33 (writing 0 dB gives 33 too), so 33 reads as exactly 0 dB.
double EqfToDb(int value) {
    value = std::clamp(value, 1, 64);
    return value == 33 ? 0.0 : (double(value) - 1.0) / 63.0 * 24.0 - 12.0;
}

int DbToEqf(double db) {
    return std::clamp(int(std::lround((db + 12.0) / 24.0 * 63.0 + 1.0)), 1, 64);
}

QList<EqPreset> BuiltinEqPresets() {
    QList<EqPreset> out;
    for (const WinampPreset& source : kWinampPresets) {
        EqPreset preset;
        preset.name = QString::fromLatin1(source.name);
        preset.settings.preampDb = EqfToDb(source.preamp);
        for (int i = 0; i < kEqBands; ++i) {
            preset.settings.bandsDb[i] = EqfToDb(source.bands[i]);
        }
        out << preset;
    }
    return out;
}

}  // namespace Audio
