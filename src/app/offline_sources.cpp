#include "app/offline_sources.h"

#include "audio/audio_engine.h"

#include <QByteArray>
#include <QFile>
#include <QIODevice>
#include <QObject>
#include <QTimer>

#include <memory>
#include <utility>

namespace App {

void StreamLocalFile(Audio::AudioEngine* engine, const QString& path) {
    auto file = std::make_shared<QFile>(path);
    if (!file->open(QIODevice::ReadOnly)) {
        qWarning("Cannot open %s", qPrintable(path));
        return;
    }
    engine->beginStream();
    auto* timer = new QTimer(engine);
    QObject::connect(timer, &QTimer::timeout, engine, [engine, file, timer] {
        const QByteArray chunk = file->read(64 * 1024);
        if (!chunk.isEmpty()) {
            engine->appendData(chunk);
        }
        if (file->atEnd()) {
            engine->finishData();
            timer->deleteLater();
        }
    });
    timer->start(20);
}

QList<Yandex::Track> DemoTracks() {
    const std::pair<const char*, int> samples[] = {
        {"Кино - Группа крови", 285},       {"Земфира - Искала", 237},
        {"Сплин - Выхода нет", 227},        {"Björk - Jóga", 305},
        {"Daft Punk - Digital Love", 301},  {"Мумий Тролль - Владивосток 2000", 164},
        {"Radiohead - Karma Police", 264},  {"Кино - Кукушка", 395},
        {"Nirvana - Come As You Are", 219},
    };
    QList<Yandex::Track> out;
    int id = 1;
    for (const auto& [name, seconds] : samples) {
        Yandex::Track track;
        const QString text = QString::fromUtf8(name);
        track.id = QString::number(id++);
        track.artists << text.section(QStringLiteral(" - "), 0, 0);
        track.title = text.section(QStringLiteral(" - "), 1);
        track.durationMs = seconds * 1000;
        out << track;
    }
    return out;
}

}  // namespace App
