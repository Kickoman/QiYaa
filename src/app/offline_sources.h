#pragma once

#include "yandex/api_client.h"

#include <QList>
#include <QString>

namespace Audio {
class AudioEngine;
}  // namespace Audio

namespace App {

void StreamLocalFile(Audio::AudioEngine* engine, const QString& path);

QList<Yandex::Track> DemoTracks();

}  // namespace App
