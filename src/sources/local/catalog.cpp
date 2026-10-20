#include "sources/local/catalog.h"

#include "core/player.h"
#include "core/queue_source.h"

#include <QDir>
#include <QFileInfo>
#include <QMetaObject>

#include <utility>

namespace LocalSource {

namespace {

QString FolderTitle(const QString& path) {
    const QString name = QDir(path).dirName();
    return name.isEmpty() ? QDir::toNativeSeparators(path) : name;
}

}  // namespace

Catalog::Catalog(Core::Player* player, QString coverDirectory, QObject* parent)
    : QObject(parent)
    , corePlayer(player)
    , covers(std::move(coverDirectory)) { }

Catalog::~Catalog() {
    if (cancelled) {
        *cancelled = true;
    }
    for (const QPointer<QThread>& thread : threads) {
        if (thread) {
            thread->wait();
        }
    }
}

void Catalog::openFiles(const QStringList& paths) {
    if (paths.isEmpty()) {
        return;
    }
    const QString first = paths.front();
    const QString title =
        paths.size() == 1 ? QFileInfo(first).completeBaseName() : FolderTitle(QFileInfo(first).absolutePath());
    scan(title, [paths, directory = covers](const std::atomic<bool>& stop) {
        return ScanFiles(paths, directory, stop);
    });
}

void Catalog::openFolder(const QString& path) {
    if (path.isEmpty()) {
        return;
    }
    scan(FolderTitle(path), [path, directory = covers](const std::atomic<bool>& stop) {
        return ScanFolder(path, directory, stop);
    });
}

void Catalog::scan(const QString& title, std::function<ScanResult(const std::atomic<bool>& cancelled)> work) {
    if (cancelled) {
        *cancelled = true;
    }
    cancelled = std::make_shared<std::atomic<bool>>(false);
    const quint64 ticket = corePlayer->newSourceRequest();
    Q_EMIT noticed(Core::SourceNotice{.code = Core::SourceNoticeCode::Loading, .subject = title});
    QThread* thread = QThread::create([this, ticket, title, work = std::move(work), stop = cancelled] {
        const ScanResult result = work(*stop);
        if (!*stop) {
            QMetaObject::invokeMethod(this, [this, ticket, title, result] { apply(ticket, title, result); });
        }
    });
    thread->setParent(this);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    threads.removeAll(nullptr);
    threads << thread;
    thread->start();
}

void Catalog::apply(quint64 ticket, const QString& title, const ScanResult& result) {
    if (!corePlayer->isLatestSourceRequest(ticket)) {
        return;  // SRC-01
    }
    if (result.tracks.isEmpty()) {
        Q_EMIT noticed(Core::SourceNotice{.code = Core::SourceNoticeCode::Empty, .subject = title});  // LOCAL-03
        return;
    }
    corePlayer->setSource(Core::QueueSource{.title = title, .head = result.tracks, .waveFromHistory = false}, true);
    Q_EMIT noticed(Core::SourceNotice{
        .code = result.cut ? Core::SourceNoticeCode::LoadedFirst : Core::SourceNoticeCode::Loaded,
        .subject = title,
        .count = static_cast<int>(result.tracks.size())
    });
}

}  // namespace LocalSource
