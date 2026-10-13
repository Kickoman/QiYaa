#pragma once

#include <QByteArray>
#include <QString>

#include <functional>

namespace Telemetry {

QString MachineId(const QByteArray& systemId);

QString MachineIdFor(const QString& stored, const std::function<void(const QString&)>& store);

}  // namespace Telemetry
