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

struct Smtc::Impl {
    WinMedia::SystemMediaTransportControls controls{nullptr};
    winrt::event_token buttonToken{};
};

Smtc::Smtc(MediaControls* controls, QWidget* window, QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Impl>())
    , mediaControls(controls) {
    try {
        auto interop = winrt::get_activation_factory<
            WinMedia::SystemMediaTransportControls, ISystemMediaTransportControlsInterop>();
        const HWND hwnd = reinterpret_cast<HWND>(window->winId());
        winrt::check_hresult(interop->GetForWindow(
            hwnd, winrt::guid_of<WinMedia::SystemMediaTransportControls>(),
            winrt::put_abi(d->controls)
        ));
        d->controls.IsEnabled(true);
        d->controls.IsPlayEnabled(true);
        d->controls.IsPauseEnabled(true);
        d->controls.IsStopEnabled(true);
        d->controls.IsNextEnabled(true);
        d->controls.IsPreviousEnabled(true);

        // Button presses arrive on a WinRT thread: hop to the Qt main thread.
        QPointer<Smtc> self(this);
        d->buttonToken = d->controls.ButtonPressed(
            [self](
                const WinMedia::SystemMediaTransportControls&,
                const WinMedia::SystemMediaTransportControlsButtonPressedEventArgs& args
            ) {
                const int button = static_cast<int>(args.Button());
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
        d->controls = nullptr;
        return;
    }

    connect(mediaControls, &MediaControls::statusChanged, this, &Smtc::updateStatus);
    connect(mediaControls, &MediaControls::trackChanged, this, &Smtc::updateMetadata);
    updateStatus();
    updateMetadata();
}

Smtc::~Smtc() {
    if (!d->controls) {
        return;
    }
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
    if (!d->controls) {
        return;
    }
    try {
        switch (mediaControls->status()) {
            case MediaControls::Status::Playing:
                d->controls.PlaybackStatus(WinMedia::MediaPlaybackStatus::Playing);
                break;
            case MediaControls::Status::Paused:
                d->controls.PlaybackStatus(WinMedia::MediaPlaybackStatus::Paused);
                break;
            case MediaControls::Status::Stopped:
                d->controls.PlaybackStatus(WinMedia::MediaPlaybackStatus::Stopped);
                break;
        }
    } catch (const winrt::hresult_error&) {
    }
}

void Smtc::updateMetadata() {
    if (!d->controls) {
        return;
    }
    try {
        auto updater = d->controls.DisplayUpdater();
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
        const QUrl art = mediaControls->remoteArtUrl();
        if (!art.isEmpty()) {
            updater.Thumbnail(WinStreams::RandomAccessStreamReference::CreateFromUri(
                WinFoundation::Uri(ToHstring(art.toString()))
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
