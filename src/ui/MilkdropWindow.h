// Winamp's Milkdrop, via projectM: a GEN.BMP frame with the visualization
// inside, preset switching (in order or at random), lock, fullscreen.
#pragma once

#include <memory>

#include <QList>
#include <QSet>
#include <QStringList>

#include "ui/GenWindow.h"
#include "vis/MilkdropPresets.h"

namespace qiyaa {

namespace audio {
class AudioEngine;
}
class MilkdropView;

class MilkdropWindow : public GenWindow {
    Q_OBJECT
public:
    // Presets: built-ins from `builtInDir` (resources), the user's own from `userDir`.
    MilkdropWindow(audio::AudioEngine* engine, const QString& builtInDir, const QString& userDir, const Skin* skin,
                   QWidget* parent = nullptr);
    ~MilkdropWindow() override;

    const MilkdropPresets& presets() const { return m_presets; }
    int currentIndex() const { return m_current; }
    QString currentPreset() const;
    // `byUser`: picked by hand (shown in the main window's marquee) rather
    // than by the automatic switching.
    void selectPreset(int index, bool smooth = true, bool byUser = true);
    void selectPreset(const QString& name);
    void nextPreset();
    void previousPreset();
    void reloadPresets();

    bool shuffle() const { return m_shuffle; }
    void setShuffle(bool on);
    bool locked() const { return m_locked; }
    void setLocked(bool on);
    int presetSeconds() const { return m_seconds; }
    void setPresetSeconds(int seconds);
    // Slower frames while nothing plays (the picture still moves, the GPU rests).
    void setPlaying(bool playing);

    bool isFullScreenMode() const { return m_fullView != nullptr; }
    void setFullScreenMode(bool on);

    // Created when the window is first shown (no OpenGL work before that);
    // stays null when OpenGL isn't usable.
    MilkdropView* view() const { return m_view; }
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
    QString userPresetDir() const { return m_userDir; }

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
    void wireView(MilkdropView* view);
    void placeView();
    void updateRendering();
    void handleKey(int key, Qt::KeyboardModifiers mods);
    void showMenu(const QPoint& globalPos);
    int fps() const;
    // The next preset to switch to on its own (in order or at random), skipping
    // the ones known to show black here.
    int followingPreset() const;

    audio::AudioEngine* m_engine;
    QString m_builtInDir;
    QString m_userDir;
    MilkdropPresets m_presets;
    MilkdropView* m_view = nullptr;      // owned by m_container; null without OpenGL
    QString m_glProblem;
    bool m_viewTried = false;
    QWidget* m_container = nullptr;
    std::unique_ptr<MilkdropView> m_fullView;
    int m_current = -1;
    QList<int> m_history;                // for "previous" in shuffle mode
    int m_failuresInARow = 0;
    bool m_shuffle = true;
    bool m_locked = false;
    int m_seconds = 30;
    bool m_playing = false;
    QSet<QString> m_black;
    int m_blackInARow = 0;  // many in a row: the problem isn't the presets
};

}  // namespace qiyaa
