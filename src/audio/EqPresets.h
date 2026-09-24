// Winamp's built-in EQ presets (from webamp's presets/builtin.json, MIT).
// Values are in Winamp's .eqf scale 1..64, where 1 = -12 dB and 64 = +12 dB.
#pragma once

#include <array>

#include <QList>
#include <QString>

#include "audio/Equalizer.h"

namespace qiyaa::audio {

struct EqPreset {
    QString name;
    EqSettings settings;
};

inline double eqfToDb(int v) {
    return (double(v) - 1.0) / 63.0 * 24.0 - 12.0;
}

inline QList<EqPreset> builtinEqPresets() {
    struct Raw {
        const char* name;
        int preamp;
        std::array<int, kEqBands> bands;
    };
    static constexpr Raw raw[] = {
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
    QList<EqPreset> out;
    for (const Raw& r : raw) {
        EqPreset p;
        p.name = QString::fromLatin1(r.name);
        // Presets in Winamp keep the preamp at "33" (~0 dB); treat that as flat.
        p.settings.preampDb = r.preamp == 33 ? 0.0 : eqfToDb(r.preamp);
        for (int i = 0; i < kEqBands; ++i) p.settings.bandsDb[i] = r.bands[i] == 33 ? 0.0 : eqfToDb(r.bands[i]);
        out << p;
    }
    return out;
}

}  // namespace qiyaa::audio
