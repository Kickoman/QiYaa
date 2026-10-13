#include "telemetry/machine_id.h"

#include <QCryptographicHash>
#include <QMessageAuthenticationCode>
#include <QSysInfo>
#include <QUuid>

namespace Telemetry {

QString MachineId(const QByteArray& systemId) {
    const QByteArray mac =
        QMessageAuthenticationCode::hash(systemId, QByteArrayLiteral("QiYaa telemetry"), QCryptographicHash::Sha256);
    return QString::fromLatin1(mac.left(16).toHex());
}

QString MachineIdFor(const QString& stored, const std::function<void(const QString&)>& store) {
    const QByteArray systemId = QSysInfo::machineUniqueId();
    if (!systemId.isEmpty()) {
        return MachineId(systemId);
    }
    if (!stored.isEmpty()) {
        return MachineId(stored.toLatin1());
    }
    const QString made = QUuid::createUuid().toString(QUuid::WithoutBraces);
    store(made);
    return MachineId(made.toLatin1());
}

}  // namespace Telemetry
