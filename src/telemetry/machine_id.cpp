#include "telemetry/machine_id.h"

#include <QCryptographicHash>
#include <QMessageAuthenticationCode>
#include <QSettings>
#include <QSysInfo>
#include <QUuid>

namespace Telemetry {

QString MachineId(const QByteArray& systemId) {
    const QByteArray mac = QMessageAuthenticationCode::hash(
        systemId, QByteArrayLiteral("QiYaa telemetry"), QCryptographicHash::Sha256
    );
    return QString::fromLatin1(mac.left(16).toHex());
}

QString MachineIdFor(QSettings& settings) {
    QByteArray systemId = QSysInfo::machineUniqueId();
    if (systemId.isEmpty()) {
        const QString key = QStringLiteral("telemetry/machine");
        if (!settings.contains(key)) {
            settings.setValue(key, QUuid::createUuid().toString(QUuid::WithoutBraces));
        }
        systemId = settings.value(key).toString().toLatin1();
    }
    return MachineId(systemId);
}

}  // namespace Telemetry
