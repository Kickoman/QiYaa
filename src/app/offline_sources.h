#pragma once

#include "core/track.h"

#include <QList>
#include <QString>

namespace Audio {
class AudioEngine;
}  // namespace Audio

namespace App {

void StreamLocalFile(Audio::AudioEngine* engine, const QString& path);

QList<Core::Track> DemoTracks();

}  // namespace App
