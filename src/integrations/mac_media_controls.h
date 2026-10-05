#pragma once

#include <QObject>

#include <memory>

namespace Integrations {

class MediaControls;

class MacMediaControls : public QObject {
public:
    explicit MacMediaControls(MediaControls* controls, QObject* parent = nullptr);
    ~MacMediaControls() override;

private:
    void updateNowPlaying();

    struct Native;
    std::unique_ptr<Native> native;
    MediaControls* mediaControls;
};

}  // namespace Integrations
