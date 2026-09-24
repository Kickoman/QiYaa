#include "audio/EqPresets.h"

#include <QStringDecoder>

namespace qiyaa::audio {

namespace {
constexpr char kHeader[] = "Winamp EQ library file v1.1";
constexpr int kHeaderLen = sizeof(kHeader) - 1;  // 27
constexpr int kNameLen = 257;
constexpr int kValues = kEqBands + 1;

// Winamp wrote names in the Windows ANSI code page; we write UTF-8 unless the
// name fits the local 8-bit encoding (which is that code page on Windows).
QString decodeName(const QByteArray& raw) {
    QStringDecoder utf8(QStringConverter::Utf8, QStringConverter::Flag::Stateless);
    const QString s = utf8.decode(raw);
    if (!utf8.hasError()) return s;
#ifdef Q_OS_WIN
    return QString::fromLocal8Bit(raw);
#else
    return QString::fromLatin1(raw);
#endif
}

QByteArray encodeName(const QString& name) {
    QByteArray out;
    bool utf8 = true;
#ifdef Q_OS_WIN
    out = name.toLocal8Bit();
    utf8 = QString::fromLocal8Bit(out) != name;
#endif
    if (utf8) out = name.toUtf8();
    if (out.size() > kNameLen - 1) {
        out.truncate(kNameLen - 1);
        if (utf8) {  // don't cut a character in half
            while (!out.isEmpty() && (quint8(out.back()) & 0xC0) == 0x80) out.chop(1);
            if (!out.isEmpty() && quint8(out.back()) >= 0xC0) out.chop(1);
        }
    }
    return out;
}
}  // namespace

bool parseEqf(const QByteArray& data, QList<EqPreset>* out) {
    if (!data.startsWith(kHeader) || data.size() < kHeaderLen + 4) return false;
    qsizetype i = kHeaderLen + 4;  // skip ^Z "!--"
    QList<EqPreset> presets;
    while (i + kNameLen + kValues <= data.size()) {
        const QByteArray rawName = data.mid(i, kNameLen);
        const qsizetype nul = rawName.indexOf('\0');
        EqPreset p;
        p.name = decodeName(nul >= 0 ? rawName.left(nul) : rawName);
        i += kNameLen;
        auto value = [&](int k) { return 64 - int(quint8(data[i + k])); };
        for (int b = 0; b < kEqBands; ++b) p.settings.bandsDb[b] = std::round(eqfToDb(value(b)) * 10) / 10;
        p.settings.preampDb = std::round(eqfToDb(value(kEqBands)) * 10) / 10;
        i += kValues;
        presets << p;
    }
    if (presets.isEmpty()) return false;
    *out = presets;
    return true;
}

QByteArray writeEqf(const QList<EqPreset>& presets) {
    QByteArray out(kHeader);
    out += char(26);
    out += "!--";
    for (const EqPreset& p : presets) {
        QByteArray name = encodeName(p.name);
        name.append(QByteArray(kNameLen - name.size(), '\0'));
        out += name;
        for (int b = 0; b < kEqBands; ++b) out += char(64 - dbToEqf(p.settings.bandsDb[b]));
        out += char(64 - dbToEqf(p.settings.preampDb));
    }
    return out;
}

}  // namespace qiyaa::audio
