#include "integrations/smtc.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "core/player.h"
#include "integrations/media_controls.h"

#include <QCoreApplication>
#include <QPointer>
#include <QWidget>
#include <systemmediatransportcontrolsinterop.h>
#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.h>
#include <winrt/Windows.Storage.Streams.h>

namespace Integrations {

namespace WinMedia = winrt::Windows::Media;
namespace WinFoundation = winrt::Windows::Foundation;
namespace WinStreams = winrt::Windows::Storage::Streams;

namespace {
winrt::hstring ToHstring(const QString& text) {
    return winrt::hstring(text.toStdWString());
}
}  // namespace

struct Smtc::Implementation {
    WinMedia::SystemMediaTransportControls controls{nullptr};
    winrt::event_token buttonToken{};
};

Smtc::Smtc(MediaControls* controls, QWidget* window, QObject* parent)
    : QObject(parent)
    , implementation(std::make_unique<Implementation>())
    , mediaControls(controls) {
    try {
        auto interop = winrt::get_activation_factory<
            WinMedia::SystemMediaTransportControls, ISystemMediaTransportControlsInterop>();
        const HWND windowHandle = reinterpret_cast<HWND>(window->winId());
        winrt::check_hresult(interop->GetForWindow(
            windowHandle, winrt::guid_of<WinMedia::SystemMediaTransportControls>(),
            winrt::put_abi(implementation->controls)
        ));
        implementation->controls.IsEnabled(true);
        implementation->controls.IsPlayEnabled(true);
        implementation->controls.IsPauseEnabled(true);
        implementation->controls.IsStopEnabled(true);
        implementation->controls.IsNextEnabled(true);
        implementation->controls.IsPreviousEnabled(true);

        // Button presses arrive on a WinRT thread: hop to the Qt main thread.
        QPointer<Smtc> self(this);
        implementation->buttonToken = implementation->controls.ButtonPressed(
            [self](
                const WinMedia::SystemMediaTransportControls&,
                const WinMedia::SystemMediaTransportControlsButtonPressedEventArgs& arguments
            ) {
                const int button = static_cast<int>(arguments.Button());
                QMetaObject::invokeMethod(
                    QCoreApplication::instance(),
                    [self, button] {
                        if (self) {
                            self->handleButton(button);
                        }
                    },
                    Qt::QueuedConnection
                );
            }
        );
    } catch (const winrt::hresult_error& error) {
        qWarning(
            "SMTC unavailable: %s",
            qPrintable(QString::fromStdWString(std::wstring(error.message())))
        );
        implementation->controls = nullptr;
        return;
    }

    connect(mediaControls, &MediaControls::statusChanged, this, &Smtc::updateStatus);
    connect(mediaControls, &MediaControls::trackChanged, this, &Smtc::updateMetadata);
    updateStatus();
    updateMetadata();
}

Smtc::~Smtc() {
    if (!implementation->controls) {
        return;
    }
    try {
        implementation->controls.ButtonPressed(implementation->buttonToken);
        implementation->controls.IsEnabled(false);
    } catch (...) {
    }
}

bool Smtc::isActive() const {
    return static_cast<bool>(implementation->controls);
}

void Smtc::handleButton(int button) {
    switch (static_cast<WinMedia::SystemMediaTransportControlsButton>(button)) {
        case WinMedia::SystemMediaTransportControlsButton::Play: mediaControls->play(); break;
        case WinMedia::SystemMediaTransportControlsButton::Pause: mediaControls->pause(); break;
        case WinMedia::SystemMediaTransportControlsButton::Stop: mediaControls->stop(); break;
        case WinMedia::SystemMediaTransportControlsButton::Next: mediaControls->next(); break;
        case WinMedia::SystemMediaTransportControlsButton::Previous:
            mediaControls->previous();
            break;
        default: break;
    }
}

void Smtc::updateStatus() {
    if (!implementation->controls) {
        return;
    }
    try {
        switch (mediaControls->status()) {
            case MediaControls::Status::Playing:
                implementation->controls.PlaybackStatus(WinMedia::MediaPlaybackStatus::Playing);
                break;
            case MediaControls::Status::Paused:
                implementation->controls.PlaybackStatus(WinMedia::MediaPlaybackStatus::Paused);
                break;
            case MediaControls::Status::Stopped:
                implementation->controls.PlaybackStatus(WinMedia::MediaPlaybackStatus::Stopped);
                break;
        }
    } catch (const winrt::hresult_error&) {
    }
}

void Smtc::updateMetadata() {
    if (!implementation->controls) {
        return;
    }
    try {
        auto updater = implementation->controls.DisplayUpdater();
        updater.ClearAll();
        const Yandex::Track* track = mediaControls->player()->currentTrack();
        if (!track) {
            updater.Update();
            return;
        }
        updater.Type(WinMedia::MediaPlaybackType::Music);
        auto music = updater.MusicProperties();
        music.Title(ToHstring(track->title));
        music.Artist(ToHstring(track->artists.join(QStringLiteral(", "))));
        music.AlbumTitle(ToHstring(track->albumTitle));
        // https, not artUrl(): SMTC fetches the thumbnail itself and refuses file:// URIs.
        const QUrl coverUrl = mediaControls->remoteArtUrl();
        if (!coverUrl.isEmpty()) {
            updater.Thumbnail(WinStreams::RandomAccessStreamReference::CreateFromUri(
                WinFoundation::Uri(ToHstring(coverUrl.toString()))
            ));
        }
        updater.Update();
    } catch (const winrt::hresult_error& error) {
        qWarning(
            "SMTC metadata: %s", qPrintable(QString::fromStdWString(std::wstring(error.message())))
        );
    }
}

}  // namespace Integrations
