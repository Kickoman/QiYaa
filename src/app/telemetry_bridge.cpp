#include "app/telemetry_bridge.h"

#include "app/account.h"
#include "app/application.h"
#include "app/paths.h"
#include "app/settings.h"
#include "app/spotify_account.h"
#include "app/translations.h"
#include "app/window_set.h"
#include "audio/audio_engine.h"
#include "core/lyrics_finder.h"
#include "core/player.h"
#include "spotify/client/session.h"
#include "spotify/client/web.h"
#include "telemetry/machine_id.h"
#include "telemetry/reporter.h"
#include "telemetry/system_info.h"
#include "ui/equalizer_window.h"
#include "ui/lyrics_window.h"
#include "ui/main_window.h"
#include "ui/milkdrop_window.h"
#include "yandex/api_client.h"
#include "yandex/token.h"
#ifdef QIYAA_HAVE_JAM
#include "jam/host_session.h"
#endif

#include <QCoreApplication>
#include <QDate>
#include <QJsonObject>
#include <QLatin1String>
#include <QRegularExpression>

#include <algorithm>
#include <utility>

namespace App {

namespace {

// The names telemetry gives (spec/telemetry): codes, never a path or a text.
QString SkinName(const QString& path) {
    const QString prefix = QStringLiteral(":/skins/");
    if (path.isEmpty() || path == prefix + QStringLiteral("base-2.91.wsz")) {
        return QStringLiteral("base");
    }
    if (path.startsWith(prefix) && path.endsWith(QLatin1String(".wsz"))) {
        return path.mid(prefix.size()).chopped(4);
    }
    return QStringLiteral("custom");
}

QString VisModeCode(Ui::MainWindow::VisMode mode) {
    switch (mode) {
        case Ui::MainWindow::VisMode::Spectrum: return QStringLiteral("spectrum");
        case Ui::MainWindow::VisMode::Oscilloscope: return QStringLiteral("oscilloscope");
        case Ui::MainWindow::VisMode::Off: break;
    }
    return QStringLiteral("off");
}

QString FailureCode(Core::FailureKind kind) {
    switch (kind) {
        case Core::FailureKind::Network: return QStringLiteral("network");
        case Core::FailureKind::Auth: return QStringLiteral("auth");
        case Core::FailureKind::Track: break;
    }
    return QStringLiteral("track");
}

QString RequestFailureCode(Yandex::RequestError::Kind kind) {
    switch (kind) {
        case Yandex::RequestError::Kind::Network: return QStringLiteral("network");
        case Yandex::RequestError::Kind::Http: return QStringLiteral("http");
        case Yandex::RequestError::Kind::Content:
        case Yandex::RequestError::Kind::None: break;
    }
    return QStringLiteral("content");
}

#ifdef QIYAA_HAVE_MILKDROP
QString MilkdropFailureCode(Vis::MilkdropFailure failure) {
    switch (failure) {
        case Vis::MilkdropFailure::NoOpenGl: return QStringLiteral("no-opengl");
        case Vis::MilkdropFailure::OpenGlEs: return QStringLiteral("gles");
        case Vis::MilkdropFailure::OldOpenGl: return QStringLiteral("old-opengl");
        case Vis::MilkdropFailure::ProjectM: break;
    }
    return QStringLiteral("projectm");
}
#endif

QString LookupField(Core::LyricsLookup::Shown shown) {
    switch (shown) {
        case Core::LyricsLookup::Shown::SourceSynced: return QStringLiteral("lyricsSourceSynced");
        case Core::LyricsLookup::Shown::ThirdPartySynced: return QStringLiteral("lyricsThirdPartySynced");
        case Core::LyricsLookup::Shown::SourceText: return QStringLiteral("lyricsSourceText");
        case Core::LyricsLookup::Shown::ThirdPartyText: return QStringLiteral("lyricsThirdPartyText");
        case Core::LyricsLookup::Shown::Instrumental: return QStringLiteral("lyricsInstrumental");
        case Core::LyricsLookup::Shown::None: break;
    }
    return QStringLiteral("lyricsNone");
}

QString OnOff(bool on) {
    return on ? QStringLiteral("on") : QStringLiteral("off");
}

QString Directory(const Settings* settings) {
    return settings->directory() + QStringLiteral("/telemetry");
}

}  // namespace

struct TelemetryBridge::State {
    Sources sources;
    Config config;
    std::unique_ptr<Telemetry::Reporter> reporter;
    bool equalizerOn = false;

    void error(const QString& area, const QString& kind, int httpStatus = 0) const {
        if (reporter) {
            reporter->recordError(area, kind, httpStatus);
        }
    }

    void feature(const QString& name, const QString& value) const {
        if (reporter) {
            reporter->recordFeature(name, value);
        }
    }
};

TelemetryBridge::TelemetryBridge(const Sources& sources, Config config, QObject* parent)
    : QObject(parent)
    , state(std::make_unique<State>(State{sources, std::move(config), nullptr, false})) {
    State* const telemetry = state.get();
    telemetry->equalizerOn = sources.equalizer->settings().enabled;
    connect(sources.player, &Core::Player::failed, this, [telemetry](Core::FailureKind kind) {
        telemetry->error(QStringLiteral("playback"), FailureCode(kind));
    });
    connect(sources.player, &Core::Player::currentTrackChanged, this, [telemetry] {
        if (telemetry->reporter && telemetry->sources.player->currentTrack()) {
            telemetry->reporter->countTrack();
        }
    });
    connect(sources.engine, &Audio::AudioEngine::errorOccurred, this, [telemetry] {
        telemetry->error(QStringLiteral("audio"), QStringLiteral("device"));
    });
    connect(sources.engine, &Audio::AudioEngine::streamUndecodable, this, [telemetry] {
        telemetry->error(QStringLiteral("audio"), QStringLiteral("undecodable"));
    });
    connect(sources.engine, &Audio::AudioEngine::stateChanged, this, [telemetry](Audio::AudioEngine::State playing) {
        if (telemetry->reporter) {
            telemetry->reporter->setPlaying(playing == Audio::AudioEngine::State::Playing);
        }
    });
    connect(
        sources.api,
        &Yandex::ApiClient::requestFailed,
        this,
        [telemetry](Yandex::RequestError::Kind kind, int httpStatus) {
            const int status = kind == Yandex::RequestError::Kind::Http ? httpStatus : 0;
            telemetry->error(QStringLiteral("api"), RequestFailureCode(kind), status);
        }
    );
    connect(sources.mainWindow, &Ui::MainWindow::visModeChanged, this, [telemetry](Ui::MainWindow::VisMode mode) {
        telemetry->feature(QStringLiteral("vis"), VisModeCode(mode));
    });
    connect(
        sources.equalizer,
        &Ui::EqualizerWindow::settingsChanged,
        this,
        [telemetry](const Audio::EqSettings& equalizer) {
            if (equalizer.enabled != telemetry->equalizerOn) {
                telemetry->equalizerOn = equalizer.enabled;
                telemetry->feature(QStringLiteral("equalizer"), OnOff(equalizer.enabled));
            }
        }
    );
    connect(sources.windows, &WindowSet::visibilityChosen, this, [telemetry](Ui::SkinnedWindow* window, bool on) {
        if (window && window == telemetry->sources.lyricsWindow) {
            return telemetry->feature(QStringLiteral("lyrics"), OnOff(on));
        }
        if (!window || window != telemetry->sources.milkdrop) {
            return;
        }
        telemetry->feature(QStringLiteral("milkdrop"), OnOff(on));
        if (telemetry->reporter) {
            telemetry->reporter->setMilkdropShown(on);
        }
    });
#ifdef QIYAA_HAVE_MILKDROP
    if (sources.milkdrop) {
        connect(sources.milkdrop, &Ui::MilkdropWindow::failed, this, [telemetry](Vis::MilkdropFailure failure) {
            telemetry->error(QStringLiteral("milkdrop"), MilkdropFailureCode(failure));
        });
    }
#endif
    connect(sources.lyrics, &Core::LyricsFinder::thirdPartyChanged, this, [telemetry](bool on) {
        telemetry->feature(QStringLiteral("lyrics-services"), OnOff(on));
    });
    connect(sources.lyrics, &Core::LyricsFinder::lookedUp, this, [telemetry](const Core::LyricsLookup& lookup) {
        if (!telemetry->reporter) {
            return;
        }
        telemetry->reporter->countLyrics(QStringLiteral("lyricsLookups"));
        telemetry->reporter->countLyrics(LookupField(lookup.shown));
        if (lookup.thirdPartyFailed) {
            telemetry->reporter->countLyrics(QStringLiteral("lyricsThirdPartyFailed"));
        }
    });
    connect(sources.application, &Application::languageChanged, this, [telemetry](Language language) {
        telemetry->feature(QStringLiteral("language"), LanguageCode(language));
    });
    connect(sources.application, &Application::skinChanged, this, [telemetry](const QString& path) {
        telemetry->feature(QStringLiteral("skin"), SkinName(path));
    });
    connect(sources.application, &Application::skinFailed, this, [telemetry] {
        telemetry->error(QStringLiteral("skin"), QStringLiteral("load"));
    });
    connect(sources.account, &Account::failed, this, [telemetry] {
        telemetry->error(QStringLiteral("login"), QStringLiteral("failed"));
    });
    connectSpotify(sources);
#ifdef QIYAA_HAVE_JAM
    if (sources.jamHost) {
        connect(sources.jamHost, &Jam::HostSession::refused, this, [telemetry](const QString& reason) {
            static const QRegularExpression code(QStringLiteral("^[a-z0-9-]{1,32}$"));
            telemetry->error(QStringLiteral("jam"), code.match(reason).hasMatch() ? reason : QStringLiteral("other"));
        });
        connect(sources.jamHost, &Jam::HostSession::ended, this, [telemetry](Jam::HostEnd why) {
            switch (why) {
                case Jam::HostEnd::Gone: telemetry->error(QStringLiteral("jam"), QStringLiteral("gone")); break;
                case Jam::HostEnd::Expired: telemetry->error(QStringLiteral("jam"), QStringLiteral("expired")); break;
                case Jam::HostEnd::ByServer:
                    telemetry->error(QStringLiteral("jam"), QStringLiteral("ended-by-server"));
                    break;
                case Jam::HostEnd::ByHost: break;
            }
        });
        connect(sources.jamHost, &Jam::HostSession::activeChanged, this, [telemetry](bool active) {
            telemetry->feature(QStringLiteral("jam"), OnOff(active));
            if (active && telemetry->reporter) {
                telemetry->reporter->countJam();
            }
        });
    }
#endif
}

TelemetryBridge::~TelemetryBridge() = default;

void TelemetryBridge::connectSpotify(const Sources& sources) {
    State* const telemetry = state.get();
    const QString area = QStringLiteral("spotify");
    connect(sources.spotify, &SpotifyAccount::loggedInWith, this, [telemetry, area](SpotifyAccount::Way way) {
        const bool code = way == SpotifyAccount::Way::Code;
        telemetry->feature(area, code ? QStringLiteral("login-code") : QStringLiteral("login-browser"));
    });
    connect(sources.spotify, &SpotifyAccount::loggedOut, this, [telemetry, area] {
        telemetry->feature(area, QStringLiteral("logout"));
    });
    connect(sources.spotify, &SpotifyAccount::refused, this, [telemetry, area](SpotifyAccount::Refusal refusal) {
        switch (refusal) {
            case SpotifyAccount::Refusal::Expired: telemetry->error(area, QStringLiteral("login-expired")); break;
            case SpotifyAccount::Refusal::PremiumRequired:
                telemetry->error(area, QStringLiteral("premium-required"));
                break;
            case SpotifyAccount::Refusal::Other: break;
        }
    });
    connect(sources.spotifySession, &SpotifyClient::Session::keyFailed, this, [telemetry, area](bool refused) {
        telemetry->error(area, refused ? QStringLiteral("key-refused") : QStringLiteral("key-failed"));
    });
    connect(sources.spotifySession, &SpotifyClient::Session::accessPointFailed, this, [telemetry, area] {
        telemetry->error(area, QStringLiteral("access-point"));
    });
    const auto problem = [telemetry, area](SpotifyClient::WebProblem kind, int httpStatus) {
        switch (kind) {
            case SpotifyClient::WebProblem::Http: telemetry->error(area, QStringLiteral("http"), httpStatus); break;
            case SpotifyClient::WebProblem::Content: telemetry->error(area, QStringLiteral("content")); break;
            case SpotifyClient::WebProblem::QueryOutdated:
                telemetry->error(area, QStringLiteral("query-outdated"));
                break;
        }
    };
    connect(sources.spotifyWeb, &SpotifyClient::Web::problem, this, problem);
}

bool TelemetryBridge::available() const {
    return !state->config.url.isEmpty();
}

bool TelemetryBridge::enabled() const {
    return state->sources.settings->telemetryEnabled();
}

void TelemetryBridge::setEnabled(bool on) {
    state->sources.settings->setTelemetryEnabled(on);
    if (on) {
        start();
        return;
    }
    if (state->reporter) {
        state->reporter->forget();
        state->reporter.reset();
    }
    Telemetry::Reporter::ForgetAll(Directory(state->sources.settings));
}

void TelemetryBridge::start() {
    const Sources& from = state->sources;
    const Config& config = state->config;
    if (state->reporter || config.url.isEmpty()) {
        return;
    }
    if (!enabled()) {
        Telemetry::Reporter::ForgetAll(Directory(from.settings));  // TEL-02: nothing left to send
        return;
    }
    const QString version = QCoreApplication::applicationVersion();
    const auto storeMachine = [settings = from.settings](const QString& id) { settings->setMachine(id); };
    state->reporter = std::make_unique<Telemetry::Reporter>(Telemetry::Reporter::Config{
        config.url,
        Directory(from.settings),
        version.isEmpty() ? QStringLiteral("dev") : version,
        Telemetry::MachineIdFor(from.settings->machine(), storeMachine),
        config.network
    });
    connect(state->reporter.get(), &Telemetry::Reporter::sendingDone, this, &TelemetryBridge::sent);
    static const QRegularExpression notBackend(QStringLiteral("[^A-Za-z0-9 _-]"));
    const bool milkdropVisible = from.milkdrop && from.milkdrop->isVisible();
    const QString skin = config.skinOverride.isEmpty() ? from.settings->skin() : config.skinOverride;
    QJsonObject fields = Telemetry::SystemFields();
    fields.insert(QStringLiteral("language"), LanguageCode(from.application->language()));
    fields.insert(QStringLiteral("scale"), std::clamp(from.mainWindow->scale(), 1.0, 4.0));
    fields.insert(QStringLiteral("skin"), SkinName(skin));
    fields.insert(QStringLiteral("vis"), VisModeCode(from.mainWindow->visMode()));
    fields.insert(QStringLiteral("equalizer"), from.equalizer->settings().enabled);
    fields.insert(QStringLiteral("milkdropBuilt"), from.milkdrop != nullptr);
    fields.insert(QStringLiteral("milkdropVisible"), milkdropVisible);
    fields.insert(QStringLiteral("jamBuilt"), from.jamHost != nullptr);
    fields.insert(
        QStringLiteral("audioBackend"),
        config.audioFailed || !config.audio ? QString() : from.engine->backendName().remove(notBackend).left(24)
    );
    fields.insert(QStringLiteral("loggedIn"), !Yandex::FindToken(TokenFile(), YaampTokenFiles()).token.isEmpty());
    fields.insert(QStringLiteral("spotifyLoggedIn"), from.spotify->hasSavedLogin());
    fields.insert(QStringLiteral("firstRun"), from.settings->wasEmpty());
    if (!from.settings->hasFirstStart()) {
        from.settings->setFirstStart(QDate::currentDate());  // a first run that is killed: the next is not first
    }
    state->reporter->begin(fields);
    state->reporter->setMilkdropShown(milkdropVisible);
    state->reporter->setPlaying(from.engine->state() == Audio::AudioEngine::State::Playing);
    if (config.audioFailed) {
        state->error(QStringLiteral("audio"), QStringLiteral("init"));
    }
}

void TelemetryBridge::finish() {
    if (state->reporter) {
        state->reporter->finish();
    }
}

bool TelemetryBridge::isSending() const {
    return state->reporter && state->reporter->isSending();
}

Telemetry::Reporter* TelemetryBridge::reporter() const {
    return state->reporter.get();
}

}  // namespace App
