#include "integrations/mac_media_controls.h"

#include "audio/audio_engine.h"
#include "core/player.h"
#include "integrations/media_controls.h"
#include "yandex/api_client.h"

#import <Foundation/Foundation.h>
#import <MediaPlayer/MediaPlayer.h>
#include <QByteArray>
#include <QMetaObject>
#include <QPointer>
#include <QString>

namespace Integrations {

struct MacMediaControls::Native {
    NSMutableArray<MPRemoteCommand*>* commands = [NSMutableArray array];
    NSMutableArray* targets = [NSMutableArray array];
};

MacMediaControls::MacMediaControls(MediaControls* controls, QObject* parent)
    : QObject(parent)
    , native(std::make_unique<Native>())
    , mediaControls(controls) {
    MPRemoteCommandCenter* center = [MPRemoteCommandCenter sharedCommandCenter];
    const auto bind = [this](MPRemoteCommand* command, void (MediaControls::*action)()) {
        QPointer<MacMediaControls> guard(this);
        id target =
            [command addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent*) {
              if (!guard) {
                  return MPRemoteCommandHandlerStatusCommandFailed;
              }
              QMetaObject::invokeMethod(
                  guard.data(),
                  [guard, action] {
                      if (guard) {
                          (guard->mediaControls->*action)();
                      }
                  },
                  Qt::QueuedConnection
              );
              return MPRemoteCommandHandlerStatusSuccess;
            }];
        command.enabled = YES;
        [native->commands addObject:command];
        [native->targets addObject:target];
    };
    bind(center.playCommand, &MediaControls::play);
    bind(center.pauseCommand, &MediaControls::pause);
    bind(center.togglePlayPauseCommand, &MediaControls::playPause);
    bind(center.stopCommand, &MediaControls::stop);
    bind(center.nextTrackCommand, &MediaControls::next);
    bind(center.previousTrackCommand, &MediaControls::previous);

    connect(mediaControls, &MediaControls::trackChanged, this, &MacMediaControls::updateNowPlaying);
    connect(
        mediaControls, &MediaControls::statusChanged, this, &MacMediaControls::updateNowPlaying
    );
    connect(mediaControls, &MediaControls::seeked, this, &MacMediaControls::updateNowPlaying);
    connect(
        mediaControls->player(), &Core::Player::positionTick, this,
        &MacMediaControls::updateNowPlaying
    );
    updateNowPlaying();
}

MacMediaControls::~MacMediaControls() {
    for (NSUInteger index = 0; index < native->commands.count; ++index) {
        MPRemoteCommand* command = native->commands[index];
        [command removeTarget:native->targets[index]];
        command.enabled = NO;
    }
    MPNowPlayingInfoCenter* center = [MPNowPlayingInfoCenter defaultCenter];
    center.playbackState = MPNowPlayingPlaybackStateStopped;
    center.nowPlayingInfo = nil;
}

void MacMediaControls::updateNowPlaying() {
    MPNowPlayingInfoCenter* center = [MPNowPlayingInfoCenter defaultCenter];
    const auto* player = mediaControls->player();
    const auto* track = player->currentTrack();
    if (!track) {
        center.nowPlayingInfo = nil;
        center.playbackState = MPNowPlayingPlaybackStateStopped;
        return;
    }
    const QByteArray title = track->title.toUtf8();
    const QByteArray artists = track->artists.join(QStringLiteral(", ")).toUtf8();
    const bool playing = mediaControls->status() == MediaControls::Status::Playing;
    center.nowPlayingInfo = @{
        MPMediaItemPropertyTitle : [NSString stringWithUTF8String:title.constData()],
        MPMediaItemPropertyArtist : [NSString stringWithUTF8String:artists.constData()],
        MPMediaItemPropertyPlaybackDuration : @(player->durationSeconds()),
        MPNowPlayingInfoPropertyElapsedPlaybackTime : @(player->engine()->positionSeconds()),
        MPNowPlayingInfoPropertyPlaybackRate : @(playing ? 1.0 : 0.0)
    };
    switch (mediaControls->status()) {
        case MediaControls::Status::Playing:
            center.playbackState = MPNowPlayingPlaybackStatePlaying;
            break;
        case MediaControls::Status::Paused:
            center.playbackState = MPNowPlayingPlaybackStatePaused;
            break;
        case MediaControls::Status::Stopped:
            center.playbackState = MPNowPlayingPlaybackStateStopped;
            break;
    }
}

}  // namespace Integrations
