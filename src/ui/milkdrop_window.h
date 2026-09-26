#pragma once

#include "ui/gen_window.h"
#include "vis/milkdrop_presets.h"

#include <QList>
#include <QPoint>
#include <QRect>
#include <QSet>
#include <QStringList>
#include <QWidget>

#include <memory>

class QPainter;

namespace Skins {
class Skin;
}  // namespace Skins

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
    enum class PresetOrigin { User, Automatic };
    Q_ENUM(PresetOrigin)

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
    void selectPreset(
        int index,
        Vis::PresetTransition transition = Vis::PresetTransition::Blend,
        PresetOrigin origin = PresetOrigin::User
    );
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
    void setPlaying(bool playing);

    bool isFullScreenMode() const { return fullView != nullptr; }
    void setFullScreenMode(bool on);

    Vis::MilkdropView* view() const { return milkdropView; }
    QString failure() const;

    void onSwitchRequested(Vis::PresetTransition transition);
    void onPresetFailed(const QString& message);
    void onStaysBlack();

    QStringList blackPresets() const;
    void setBlackPresets(const QStringList& names);
    bool isBlack(int index) const;
    QString userPresetDir() const { return userDirectory; }

Q_SIGNALS:
    void presetChanged(const QString& name, Ui::MilkdropWindow::PresetOrigin origin);
    void settingsChanged();
    void transportKey(int key);

protected:
    void paintContent(QPainter& painter, const QRect& area) override;
    bool contentMousePress(QPoint pos, Qt::MouseButton button) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void ensureView();
    void wireView(Vis::MilkdropView* view);
    void placeView();
    void updateRendering();
    void handleKey(int key, Qt::KeyboardModifiers mods);
    void showMenu(const QPoint& globalPos);
    int fps() const;
    int followingPreset() const;

    Audio::AudioEngine* audioEngine;
    QString builtInDirectory;
    QString userDirectory;
    Vis::MilkdropPresets presetList;
    Vis::MilkdropView* milkdropView = nullptr;
    QString glProblem;
    bool viewTried = false;
    QWidget* container = nullptr;
    std::unique_ptr<Vis::MilkdropView> fullView;
    int selectedIndex = -1;
    QList<int> history;
    int failuresInARow = 0;
    bool shuffleEnabled = true;
    bool lockEnabled = false;
    int switchIntervalSeconds = 30;
    bool musicPlaying = false;
    QSet<QString> blackPresetNames;
    int blackInARow = 0;
};

}  // namespace Ui
