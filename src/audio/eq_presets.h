// Winamp's built-in EQ presets (from webamp's presets/builtin.json, MIT).
// Values are in Winamp's .eqf scale 1..64, where 1 = -12 dB and 64 = +12 dB.
#pragma once

#include "audio/equalizer.h"

#include <QByteArray>
#include <QList>
#include <QString>

namespace Audio {

struct EqPreset {
    QString name;
    EqSettings settings;
};

// Winamp's .eqf scale 1..64 (33 = 0 dB) to decibels and back.
double EqfToDb(int value);
int DbToEqf(double db);

// Winamp .eqf / .q1 files: "Winamp EQ library file v1.1" + ^Z + "!--", then
// per preset a 257-byte zero-padded name and 11 bytes (10 bands + preamp),
// each stored as 64 - value (value 1..64). Format as in webamp's winamp-eqf.
// Throws Audio::Error when `data` isn't an .eqf file or holds no preset.
QList<EqPreset> ParseEqf(const QByteArray& data);
QByteArray WriteEqf(const QList<EqPreset>& presets);

QList<EqPreset> BuiltinEqPresets();

}  // namespace Audio
