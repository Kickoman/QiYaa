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

double EqfToDb(int value);
int DbToEqf(double db);

QList<EqPreset> ParseEqf(const QByteArray& data);
QByteArray WriteEqf(const QList<EqPreset>& presets);

QList<EqPreset> BuiltinEqPresets();

}  // namespace Audio
