#pragma once

#include <QImage>
#include <QList>
#include <QObject>
#include <QSize>
#include <QString>

#include <vector>

namespace Skins {
class Skin;
}  // namespace Skins

namespace Ui {
class MainWindow;
class SkinnedWindow;
}  // namespace Ui

namespace App {

class Settings;

class WindowSet : public QObject {
    Q_OBJECT
public:
    enum class Place { Main, Stacked, RightOfMain, BelowRightOfMain };
    enum class Button { None, Equalizer, Playlist };
    enum class ScaleScope { Saved, ThisRun };

    struct Spec {
        QString group;
        Place place = Place::RightOfMain;
        bool visibleByDefault = false;
        bool shownAtStart = true;
        bool keepsShade = false;
        bool raisedWhenShown = false;
        QSize stepsByDefault = {};
        Button button = Button::None;
    };

    WindowSet(Settings* settings, Ui::MainWindow* mainWindow, Spec mainSpec, QObject* parent = nullptr);

    void add(Ui::SkinnedWindow* window, Spec spec);
    void restore();
    void showAtStart();

    void setVisible(Ui::SkinnedWindow* window, bool on);
    void toggle(Ui::SkinnedWindow* window);
    void raise();
    void bringToFront();
    void hideAll();

    void setScale(double scale, ScaleScope scope = ScaleScope::Saved);
    void setAlwaysOnTop(bool on);
    void setSkin(const Skins::Skin* skin);
    void savePositions();

    QList<Ui::SkinnedWindow*> all() const;
    QImage snapshot() const;

Q_SIGNALS:
    void visibilityChosen(Ui::SkinnedWindow* window, bool on);
    void saveRequested();

private:
    struct Entry {
        Ui::SkinnedWindow* window;
        Spec spec;
    };

    void layout();
    void minimizedChanged(bool minimized);
    void light(const Spec& spec, bool on);
    bool savedVisible(const Entry& entry) const;

    Settings* settings;
    Ui::MainWindow* mainWindow;
    Spec mainSpec;
    std::vector<Entry> entries;
    bool transientScale = false;
};

}  // namespace App
