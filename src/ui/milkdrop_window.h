// Winamp's Milkdrop, via projectM: a GEN.BMP frame with the visualization
// inside, preset switching (in order or at random), lock, fullscreen.
#pragma once

#include "ui/gen_window.h"
#include "vis/milkdrop_presets.h"

#include <QList>
#include <QSet>
#include <QStringList>

#include <memory>

namespace Audio {
class AudioEngine;
}  // namespace Audio

namespace Vis {
class MilkdropView;
}  // namespace Vis

namespace Ui {

class MilkdropWindow : public GenWindow {
    Q_OBJECT
public:
    // Presets: built-ins from `builtInDir` (resources), the user's own from `userDir`.
    MilkdropWindow(
        Audio::AudioEngine* engine,
        const QString& builtInDir,
        const QString& userDir,
        const Skins::Skin* skin,
        QWidget* parent = nullptr
    );
    ~MilkdropWindow() override;

    const Vis::MilkdropPresets& presets() const { return presetList; }
    int currentIndex() const { return selectedIndex; }
    QString currentPreset() const;
    // `byUser`: picked by hand (shown in the main window's marquee) rather
    // than by the automatic switching.
    void selectPreset(int index, bool smooth = true, bool byUser = true);
    void selectPreset(const QString& name);
    void nextPreset();
    void previousPreset();
    void reloadPresets();

    bool shuffle() const { return shuffleEnabled; }
    void setShuffle(bool on);
    bool locked() const { return lockEnabled; }
    void setLocked(bool on);
    int presetSeconds() const { return switchIntervalSeconds; }
    void setPresetSeconds(int seconds);
    // Slower frames while nothing plays (the picture still moves, the GPU rests).
    void setPlaying(bool playing);

    bool isFullScreenMode() const { return fullView != nullptr; }
    void setFullScreenMode(bool on);

    // Created when the window is first shown (no OpenGL work before that);
    // stays null when OpenGL isn't usable.
    Vis::MilkdropView* view() const { return milkdropView; }
    QString failure() const;  // why Milkdrop can't show anything

    // What the view reports (public for tests).
    void onSwitchRequested(bool hardCut);
    void onPresetFailed(const QString& message);
    void onStaysBlack();

    // Presets that showed only black here (the GPU/driver can't run them):
    // skipped when switching, remembered in the settings.
    QStringList blackPresets() const;
    void setBlackPresets(const QStringList& names);
    bool isBlack(int index) const;
    QString userPresetDir() const { return userDirectory; }

Q_SIGNALS:
    void presetChanged(const QString& name, bool byUser);
    void settingsChanged();
    // Winamp's transport keys pressed while the visualization has the focus.
    void transportKey(int key);

protected:
    void paintContent(QPainter& p, const QRect& area) override;
    bool contentMousePress(QPoint pos, Qt::MouseButton button) override;
    void resizeEvent(QResizeEvent* e) override;
    void showEvent(QShowEvent* e) override;
    void hideEvent(QHideEvent* e) override;

private:
    void ensureView();
    void wireView(Vis::MilkdropView* view);
    void placeView();
    void updateRendering();
    void handleKey(int key, Qt::KeyboardModifiers mods);
    void showMenu(const QPoint& globalPos);
    int fps() const;
    // The next preset to switch to on its own (in order or at random), skipping
    // the ones known to show black here.
    int followingPreset() const;

    Audio::AudioEngine* audioEngine;
    QString builtInDirectory;
    QString userDirectory;
    Vis::MilkdropPresets presetList;
    Vis::MilkdropView* milkdropView = nullptr;  // owned by container; null without OpenGL
    QString glProblem;
    bool viewTried = false;
    QWidget* container = nullptr;
    std::unique_ptr<Vis::MilkdropView> fullView;
    int selectedIndex = -1;
    QList<int> history;  // for "previous" in shuffle mode
    int failuresInARow = 0;
    bool shuffleEnabled = true;
    bool lockEnabled = false;
    int switchIntervalSeconds = 30;
    bool musicPlaying = false;
    QSet<QString> blackPresetNames;
    int blackInARow = 0;  // many in a row: the problem isn't the presets
};

}  // namespace Ui
