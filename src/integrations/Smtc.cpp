#include "integrations/Smtc.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <systemmediatransportcontrolsinterop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.h>
#include <winrt/Windows.Storage.Streams.h>

#include <QCoreApplication>
#include <QPointer>
#include <QWidget>

#include "core/Player.h"
#include "integrations/MediaControls.h"

namespace qiyaa {

namespace wm = winrt::Windows::Media;
namespace wf = winrt::Windows::Foundation;
namespace wss = winrt::Windows::Storage::Streams;

namespace {
winrt::hstring toH(const QString& s) {
    return winrt::hstring(s.toStdWString());
}
}  // namespace

struct Smtc::Impl {
    wm::SystemMediaTransportControls controls{nullptr};
    winrt::event_token buttonToken{};
};

Smtc::Smtc(MediaControls* controls, QWidget* window, QObject* parent)
    : QObject(parent), d(std::make_unique<Impl>()), m_controls(controls) {
    try {
        // Qt has already initialised COM on this thread; that's fine for WinRT.
        auto interop = winrt::get_activation_factory<wm::SystemMediaTransportControls, ISystemMediaTransportControlsInterop>();
        const HWND hwnd = reinterpret_cast<HWND>(window->winId());
        winrt::check_hresult(interop->GetForWindow(hwnd, winrt::guid_of<wm::SystemMediaTransportControls>(),
                                                   winrt::put_abi(d->controls)));
        d->controls.IsEnabled(true);
        d->controls.IsPlayEnabled(true);
        d->controls.IsPauseEnabled(true);
        d->controls.IsStopEnabled(true);
        d->controls.IsNextEnabled(true);
        d->controls.IsPreviousEnabled(true);

        // Button presses arrive on a WinRT thread: hop to the Qt main thread.
        QPointer<Smtc> self(this);
        d->buttonToken = d->controls.ButtonPressed(
            [self](const wm::SystemMediaTransportControls&, const wm::SystemMediaTransportControlsButtonPressedEventArgs& args) {
                const int button = static_cast<int>(args.Button());
                QMetaObject::invokeMethod(
                    QCoreApplication::instance(), [self, button] {
                        if (self) self->handleButton(button);
                    },
                    Qt::QueuedConnection);
            });
    } catch (const winrt::hresult_error& e) {
        qWarning("SMTC unavailable: %s", qPrintable(QString::fromStdWString(std::wstring(e.message()))));
        d->controls = nullptr;
        return;
    }

    connect(m_controls, &MediaControls::statusChanged, this, &Smtc::updateStatus);
    connect(m_controls, &MediaControls::trackChanged, this, &Smtc::updateMetadata);
    updateStatus();
    updateMetadata();
}

Smtc::~Smtc() {
    if (!d->controls) return;
    try {
        d->controls.ButtonPressed(d->buttonToken);
        d->controls.IsEnabled(false);
    } catch (...) {
    }
}

bool Smtc::isActive() const {
    return static_cast<bool>(d->controls);
}

void Smtc::handleButton(int button) {
    switch (static_cast<wm::SystemMediaTransportControlsButton>(button)) {
    case wm::SystemMediaTransportControlsButton::Play: m_controls->play(); break;
    case wm::SystemMediaTransportControlsButton::Pause: m_controls->pause(); break;
    case wm::SystemMediaTransportControlsButton::Stop: m_controls->stop(); break;
    case wm::SystemMediaTransportControlsButton::Next: m_controls->next(); break;
    case wm::SystemMediaTransportControlsButton::Previous: m_controls->previous(); break;
    default: break;
    }
}

void Smtc::updateStatus() {
    if (!d->controls) return;
    try {
        switch (m_controls->status()) {
        case MediaControls::Status::Playing: d->controls.PlaybackStatus(wm::MediaPlaybackStatus::Playing); break;
        case MediaControls::Status::Paused: d->controls.PlaybackStatus(wm::MediaPlaybackStatus::Paused); break;
        case MediaControls::Status::Stopped: d->controls.PlaybackStatus(wm::MediaPlaybackStatus::Stopped); break;
        }
    } catch (const winrt::hresult_error&) {
    }
}

void Smtc::updateMetadata() {
    if (!d->controls) return;
    try {
        auto updater = d->controls.DisplayUpdater();
        updater.ClearAll();  // also drops the previous track's cover
        const yandex::Track* t = m_controls->player()->currentTrack();
        if (!t) {
            updater.Update();
            return;
        }
        updater.Type(wm::MediaPlaybackType::Music);
        auto music = updater.MusicProperties();
        music.Title(toH(t->title));
        music.Artist(toH(t->artists.join(QStringLiteral(", "))));
        music.AlbumTitle(toH(t->albumTitle));
        // The https URL: SMTC fetches it itself, and file:// URIs aren't accepted here.
        const QUrl art = m_controls->remoteArtUrl();
        if (!art.isEmpty()) updater.Thumbnail(wss::RandomAccessStreamReference::CreateFromUri(wf::Uri(toH(art.toString()))));
        updater.Update();
    } catch (const winrt::hresult_error& e) {
        qWarning("SMTC metadata: %s", qPrintable(QString::fromStdWString(std::wstring(e.message()))));
    }
}

}  // namespace qiyaa
