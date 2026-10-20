#pragma once

#include "core/source_notice.h"
#include "sources/local/scan.h"

#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QThread>

#include <atomic>
#include <functional>
#include <memory>

namespace Core {
class Player;
}  // namespace Core

namespace LocalSource {

class Catalog : public QObject {
    Q_OBJECT
public:
    Catalog(Core::Player* player, QString coverDirectory, QObject* parent = nullptr);
    ~Catalog() override;

    void openFiles(const QStringList& paths);
    void openFolder(const QString& path);

Q_SIGNALS:
    void noticed(const Core::SourceNotice& notice);

private:
    void scan(const QString& title, std::function<ScanResult(const std::atomic<bool>& cancelled)> work);
    void apply(quint64 ticket, const QString& title, const ScanResult& result);

    Core::Player* corePlayer;
    const QString covers;
    std::shared_ptr<std::atomic<bool>> cancelled;
    QList<QPointer<QThread>> threads;
};

}  // namespace LocalSource
