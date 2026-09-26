#pragma once

#include <QObject>

#include <memory>

class QWidget;

namespace Integrations {

class MediaControls;

class Smtc : public QObject {
    Q_OBJECT
public:
    Smtc(MediaControls* controls, QWidget* window, QObject* parent = nullptr);
    ~Smtc() override;

    bool isActive() const;

private:
    void updateStatus();
    void updateMetadata();
    void handleButton(int button);

    struct Implementation;
    std::unique_ptr<Implementation> implementation;
    MediaControls* mediaControls;
};

}  // namespace Integrations
