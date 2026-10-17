#pragma once

#include <QObject>

#include <optional>

namespace App {

class NetworkWatch : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;

    void start();

Q_SIGNALS:
    void onlineChanged(std::optional<bool> online);
    void back();
};

}  // namespace App
