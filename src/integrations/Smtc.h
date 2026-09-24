// Windows System Media Transport Controls: media keys, the volume flyout's
// media panel, lock screen. Windows 10+ only; built when C++/WinRT is available.
#pragma once

#include <memory>

#include <QObject>

class QWidget;

namespace qiyaa {

class MediaControls;

class Smtc : public QObject {
    Q_OBJECT
public:
    // `window` is the main window: SMTC for desktop apps is bound to an HWND.
    Smtc(MediaControls* controls, QWidget* window, QObject* parent = nullptr);
    ~Smtc() override;

    bool isActive() const;

private:
    void updateStatus();
    void updateMetadata();
    void handleButton(int button);

    struct Impl;
    std::unique_ptr<Impl> d;
    MediaControls* m_controls;
};

}  // namespace qiyaa
