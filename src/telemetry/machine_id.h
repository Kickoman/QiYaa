#pragma once

#include <QByteArray>
#include <QString>

class QSettings;

namespace Telemetry {

QString MachineId(const QByteArray& systemId);

QString MachineIdFor(QSettings& settings);

}  // namespace Telemetry
