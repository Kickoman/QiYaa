#include "app/telemetry_bridge.h"

namespace App {

struct TelemetryBridge::State { };

TelemetryBridge::TelemetryBridge(const Sources&, Config, QObject* parent)
    : QObject(parent) { }

TelemetryBridge::~TelemetryBridge() = default;

bool TelemetryBridge::available() const {
    return false;
}

bool TelemetryBridge::enabled() const {
    return false;
}

void TelemetryBridge::setEnabled(bool) { }

void TelemetryBridge::start() { }

void TelemetryBridge::finish() { }

bool TelemetryBridge::isSending() const {
    return false;
}

Telemetry::Reporter* TelemetryBridge::reporter() const {
    return nullptr;
}

}  // namespace App
