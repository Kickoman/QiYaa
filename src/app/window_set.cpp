#include "app/window_set.h"

#include "app/settings.h"
#include "ui/main_window.h"
#include "ui/skinned_window.h"
#include "ui/snap.h"

#include <QGuiApplication>
#include <QHash>
#include <QPainter>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QScreen>

#include <algorithm>
#include <iterator>
#include <utility>

namespace App {

namespace {

QRect DockedTo(QRect frame, const QRect& before, const QRect& neighbourBefore, const QRect& neighbourAfter) {
    if (!Ui::Touching(before, neighbourBefore)) {
        return frame;
    }
    if (before.y() == neighbourBefore.y() + neighbourBefore.height()) {
        frame.moveTop(neighbourAfter.y() + neighbourAfter.height());
    } else if (before.y() + before.height() == neighbourBefore.y()) {
        frame.moveTop(neighbourAfter.y() - frame.height());
    }
    if (before.x() == neighbourBefore.x() + neighbourBefore.width()) {
        frame.moveLeft(neighbourAfter.x() + neighbourAfter.width());
    } else if (before.x() + before.width() == neighbourBefore.x()) {
        frame.moveLeft(neighbourAfter.x() - frame.width());
    }
    return frame;
}

}  // namespace

WindowSet::WindowSet(Settings* appSettings, Ui::MainWindow* main, Spec spec, QObject* parent)
    : QObject(parent)
    , settings(appSettings)
    , mainWindow(main)
    , mainSpec(std::move(spec)) {
    connect(mainWindow, &Ui::MainWindow::minimizedChanged, this, &WindowSet::minimizedChanged);
    connect(mainWindow, &Ui::MainWindow::activated, this, &WindowSet::raise);
}

void WindowSet::add(Ui::SkinnedWindow* window, Spec spec) {
    window->setSecondary();
    if (spec.stepsByDefault.isValid()) {
        window->setSizeSteps(settings->window(spec.group, spec.visibleByDefault, spec.stepsByDefault).steps);
        connect(window, &Ui::SkinnedWindow::sizeStepsChanged, this, [this, group = spec.group](QSize steps) {
            settings->setWindowSteps(group, steps);
        });
    }
    connect(window, &Ui::SkinnedWindow::closeRequested, this, [this, window] { setVisible(window, false); });
    entries.push_back({window, std::move(spec)});
}

void WindowSet::restore() {
    for (Ui::SkinnedWindow* window : all()) {
        connect(window, &Ui::SkinnedWindow::moveFinished, this, &WindowSet::saveRequested);
    }
    std::vector<Entry> shaded{{mainWindow, mainSpec}};
    std::copy_if(entries.cbegin(), entries.cend(), std::back_inserter(shaded), [](const Entry& entry) {
        return entry.spec.keepsShade;
    });
    for (const auto& [window, spec] : shaded) {
        window->setShaded(settings->window(spec.group, spec.visibleByDefault).shaded);
        connect(window, &Ui::SkinnedWindow::shadeChanged, this, [this, group = spec.group](bool on) {
            settings->setWindowShaded(group, on);
            Q_EMIT saveRequested();
        });
    }
    const double scale = settings->scale();
    for (Ui::SkinnedWindow* window : all()) {
        window->setScale(scale);
    }
    if (settings->alwaysOnTop()) {
        setAlwaysOnTop(true);
    }
}

void WindowSet::showAtStart() {
    mainWindow->show();
    for (const Entry& entry : entries) {
        if (entry.spec.shownAtStart && savedVisible(entry)) {
            entry.window->show();
        }
    }
    layout();
    for (const Entry& entry : entries) {
        light(entry.spec, entry.window->isVisible());
    }
    mainWindow->activateWindow();
}

void WindowSet::layout() {
    const QPoint mainPosition = settings->window(mainSpec.group, true).pos.value_or(QPoint(100, 100));
    mainWindow->placeAt(mainPosition);
    const QPoint rightOfMain = mainPosition + QPoint(mainWindow->width(), 0);
    QPoint stackBottom = mainPosition + QPoint(0, mainWindow->height());
    for (const auto& [window, spec] : entries) {
        QPoint place = rightOfMain;
        switch (spec.place) {
            case Place::Stacked:
                place = stackBottom;
                stackBottom = place + QPoint(0, window->height());
                break;
            case Place::BelowRightOfMain: place = rightOfMain + QPoint(0, mainWindow->height()); break;
            case Place::RightOfMain: break;
        }
        window->placeAt(settings->window(spec.group, spec.visibleByDefault).pos.value_or(place));
    }
}

void WindowSet::minimizedChanged(bool minimized) {
    for (const Entry& entry : entries) {
        if (minimized) {
            entry.window->hide();
        } else if (savedVisible(entry)) {
            entry.window->show();
        }
    }
}

bool WindowSet::savedVisible(const Entry& entry) const {
    return settings->window(entry.spec.group, entry.spec.visibleByDefault).visible;
}

void WindowSet::light(const Spec& spec, bool on) {
    switch (spec.button) {
        case Button::Equalizer: mainWindow->setEqButton(on); break;
        case Button::Playlist: mainWindow->setPlaylistButton(on); break;
        case Button::None: break;
    }
}

void WindowSet::setVisible(Ui::SkinnedWindow* window, bool on) {
    const auto entry = std::find_if(entries.cbegin(), entries.cend(), [window](const Entry& candidate) {
        return candidate.window == window;
    });
    if (entry == entries.cend()) {
        return;
    }
    window->setVisible(on);
    if (on) {
        window->ensureVisible();
    }
    if (on && entry->spec.raisedWhenShown) {
        window->raise();
    }
    light(entry->spec, on);
    settings->setWindowVisible(entry->spec.group, on);
    Q_EMIT visibilityChosen(window, on);
}

void WindowSet::toggle(Ui::SkinnedWindow* window) {
    if (window) {
        setVisible(window, !window->isVisible());
    }
}

void WindowSet::raise() {
    for (const Entry& entry : entries) {
        if (entry.window->isVisible()) {
            entry.window->raise();
        }
    }
    mainWindow->raise();
}

void WindowSet::bringToFront() {
    if (mainWindow->isMinimized()) {
        mainWindow->showNormal();
    }
    raise();
    mainWindow->activateWindow();
}

void WindowSet::hideAll() {
    for (Ui::SkinnedWindow* window : all()) {
        window->hide();
    }
}

void WindowSet::setScale(double scale, ScaleScope scope) {
    const double oldScale = mainWindow->scale();
    const QList<Ui::SkinnedWindow*> windows = all();
    QList<QRect> frames;
    for (Ui::SkinnedWindow* window : windows) {
        frames << window->frameGeometry();
    }
    QList<std::pair<Ui::SkinnedWindow*, QPoint>> docked;
    for (int i : Ui::ConnectedGroup(0, frames)) {
        const QPointF offset = QPointF(windows[i]->pos() - mainWindow->pos()) / oldScale;
        docked.append({windows[i], QPoint(qRound(offset.x()), qRound(offset.y()))});
    }

    for (Ui::SkinnedWindow* window : windows) {
        window->setScale(scale);
    }
    const double newScale = mainWindow->scale();
    const QPoint mainPosition = mainWindow->pos();

    QHash<Ui::SkinnedWindow*, std::pair<QRect, QRect>> placed{
        {mainWindow, {frames.front(), mainWindow->frameGeometry()}}
    };
    for (const auto& [window, offset] : docked) {
        QRect frame(
            mainPosition + QPoint(qRound(offset.x() * newScale), qRound(offset.y() * newScale)), window->size()
        );
        QList<QRect> placedFrames;
        for (const auto& [before, after] : placed) {
            placedFrames << after;
        }
        frame.moveTopLeft(Ui::SnapToOthers(frame, placedFrames, 4));
        const QRect before = frames[windows.indexOf(window)];
        for (const auto& [neighbourBefore, neighbourAfter] : placed) {
            frame = DockedTo(frame, before, neighbourBefore, neighbourAfter);
        }
        window->move(frame.topLeft());
        placed.insert(window, {before, frame});
    }
    QRect bounds = mainWindow->frameGeometry();
    for (const auto& [window, offset] : docked) {
        if (window->isVisible()) {
            bounds |= window->frameGeometry();
        }
    }
    QList<QRect> screens;
    for (QScreen* monitor : QGuiApplication::screens()) {
        screens << monitor->availableGeometry();
    }
    const QRect screen = Ui::PickScreen(bounds, screens);
    const QPoint shift = Ui::ClampInside(bounds, screen) - bounds.topLeft();
    if (!shift.isNull()) {
        mainWindow->move(mainWindow->pos() + shift);
        for (const auto& [window, offset] : docked) {
            window->move(window->pos() + shift);
        }
    }
    if (!screen.isEmpty() && (bounds.width() > screen.width() || bounds.height() > screen.height())) {
        for (Ui::SkinnedWindow* window : windows) {
            window->ensureVisible();
        }
    }

    transientScale = scope == ScaleScope::ThisRun;
    if (scope == ScaleScope::Saved) {
        settings->setScale(newScale);
        Q_EMIT saveRequested();
    }
}

void WindowSet::setAlwaysOnTop(bool on) {
    for (Ui::SkinnedWindow* window : all()) {
        const bool visible = window->isVisible();
        window->setWindowFlag(Qt::WindowStaysOnTopHint, on);
        if (visible) {
            window->show();  // changing flags hides the window
        }
    }
    settings->setAlwaysOnTop(on);
}

void WindowSet::setSkin(const Skins::Skin* skin) {
    for (Ui::SkinnedWindow* window : all()) {
        window->setSkin(skin);
    }
}

void WindowSet::savePositions() {
    if (!Ui::SkinnedWindow::CanPositionWindows() || !mainWindow->isVisible() || transientScale) {
        return;
    }
    settings->setWindowPos(mainSpec.group, mainWindow->pos());
    for (const auto& [window, spec] : entries) {
        settings->setWindowPos(spec.group, window->pos());
    }
}

QList<Ui::SkinnedWindow*> WindowSet::all() const {
    QList<Ui::SkinnedWindow*> windows{mainWindow};
    for (const Entry& entry : entries) {
        windows << entry.window;
    }
    return windows;
}

const WindowSet::Spec& WindowSet::spec(const Ui::SkinnedWindow* window) const {
    const auto entry = std::find_if(entries.cbegin(), entries.cend(), [window](const Entry& candidate) {
        return candidate.window == window;
    });
    return entry != entries.cend() ? entry->spec : mainSpec;
}

QImage WindowSet::snapshot() const {
    QRect bounds;
    for (Ui::SkinnedWindow* window : all()) {
        if (window->isVisible()) {
            bounds |= window->frameGeometry();
        }
    }
    QImage image(bounds.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    for (Ui::SkinnedWindow* window : all()) {
        if (window->isVisible()) {
            painter.drawPixmap(window->pos() - bounds.topLeft(), window->grab());
        }
    }
    return image;
}

}  // namespace App
