#include "app/network_watch.h"

#include <QNetworkInformation>

namespace App {

void NetworkWatch::start() {
    using Information = QNetworkInformation;
    if (!Information::loadDefaultBackend() && !Information::loadBackendByFeatures(Information::Feature::Reachability)) {
        return;
    }
    Information* information = Information::instance();
    if (!information || !information->supports(Information::Feature::Reachability)) {
        return;
    }
    const auto report = [this](Information::Reachability reachability) {
        // Local and Site say little about the Yandex servers: leave those to the timer.
        std::optional<bool> online;
        if (reachability == Information::Reachability::Online) {
            online = true;
        } else if (reachability == Information::Reachability::Disconnected) {
            online = false;
        }
        Q_EMIT onlineChanged(online);
        if (reachability == Information::Reachability::Online) {
            Q_EMIT back();
        }
    };
    connect(information, &Information::reachabilityChanged, this, report);
    report(information->reachability());
}

}  // namespace App
