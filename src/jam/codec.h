#pragma once

#include "jam/protocol.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QString>

#include <optional>

namespace Jam {

template <typename T>
struct Decoded {
    std::optional<T> message;
    QString problem;
    bool unknownType = false;

    bool isValid() const { return message.has_value() && problem.isEmpty(); }
};

QByteArray Encode(const ClientMessage& message);

Decoded<ServerMessage> DecodeServer(QByteArrayView text);
Decoded<ClientMessage> DecodeClient(QByteArrayView text);

bool IsKnownReason(const QString& reason);

QString TypeOf(const ClientMessage& message);
QString TypeOf(const ServerMessage& message);

}  // namespace Jam
