#include "app/jam_bridge.h"

namespace App {

JamBridge::JamBridge(const Parts& bridgeParts, QObject* parent)
    : QObject(parent)
    , parts(bridgeParts) { }

JamBridge::~JamBridge() = default;

void JamBridge::offerStored() { }

void JamBridge::addMenu(QMenu*) { }

void JamBridge::showServerDialog() { }

QString JamBridge::trackNote(int) const {
    return {};
}

}  // namespace App
