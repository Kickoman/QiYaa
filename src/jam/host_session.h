#pragma once

#include "jam/client.h"
#include "jam/protocol.h"
#include "jam/session_store.h"

#include <QObject>
#include <QString>
#include <QTimer>

#include <functional>
#include <optional>

namespace Core {
class JamMode;
struct JamPlayback;
}  // namespace Core

namespace Yandex {
class Library;
struct Track;
}  // namespace Yandex

namespace Jam {

struct HostConfig {
    QString serverUrl;
    bool waveFeedback = true;
    bool shareAudio = false;  // listening along (experimental): guests may play the host's file
};

struct HostOptions {
    ClientOptions client;
    std::function<HostConfig()> config;
    QString queueTitle;
    std::function<QString()> newId;
    int resumeRetryMs = 30'000;
};

enum class HostPhase { None, Creating, Active };
enum class HostEnd { ByHost, ByServer, Expired, Gone };

inline constexpr int kMaxHostNameLength = 24;
inline constexpr int kMaxSearchResults = 20;
inline constexpr int kMaxOutboxEvents = 500;

std::optional<Track> JamTrackOf(const Yandex::Track& track);
Yandex::Track YandexTrackOf(const Track& track);

// The host's side of a jam (spec/jam/host.md): the Client to the server, the jam mode of the
// queue, and the Library for the guests' search and checks.
class HostSession : public QObject {
    Q_OBJECT
public:
    HostSession(
        Core::JamMode* jamMode,
        Yandex::Library* library,
        SessionStore store,
        HostOptions options,
        QObject* parent = nullptr
    );
    ~HostSession() override;

    HostPhase phase() const;
    Status connection() const { return client.status(); }
    bool isConnected() const { return connected; }
    const std::optional<Room>& room() const { return currentRoom; }
    QString joinUrl() const;
    bool hasStoredSession() const { return stored.has_value(); }

    bool
    create(const QString& hostName, const std::optional<SettingsPatch>& settings = std::nullopt);
    void cancelCreate();
    void continueStored();
    void discardStored();
    void end();

    bool add(const Yandex::Track& track);
    bool playNext(const Yandex::Track& track);
    bool pin(const QString& itemId);
    bool remove(const QString& itemId);
    bool kick(const QString& publicId);
    bool changeSettings(const SettingsPatch& patch);
    bool rotateLink();

    void networkBack();

Q_SIGNALS:
    void changed();
    void refused(const QString& reason);
    void ended(Jam::HostEnd why);

private:
    struct CreateRequest {
        QString hostName;
        std::optional<SettingsPatch> settings;
    };

    void welcomed();
    void received(const ServerMessage& message);
    void created(const Created& message);
    void resumed();
    void rejected(const Rejected& message);
    void applyState(const State& message);
    void search(const SearchRequest& message);
    void validate(const ValidateRequest& message);
    void serverEnded(EndReason reason);
    void sendPlaying(const Core::JamPlayback& playback);
    void sendResume(const Session& session, const QStringList& outbox);
    bool request(const std::function<ClientMessage(const QString& id)>& build);
    QString nextId();
    void endLocally(HostEnd why);
    void finishDiscard();
    void connectTo(const QString& url);
    void dropConnection();
    void retryLater();
    void save(Session updated);

    Core::JamMode* jam;
    Yandex::Library* yandexLibrary;
    SessionStore store;
    HostOptions options;
    Client client;
    std::optional<Session> session;
    std::optional<Session> stored;
    std::optional<Session> discarding;
    std::optional<CreateRequest> creating;
    std::optional<Room> currentRoom;
    bool connected = false;
    QString requestId;
    QString endId;
    qint64 lastVersion = -1;
    QString pendingPinTrack;
    QString pendingPinAdd;
    QString serverUrl;
    QTimer retryTimer;
};

}  // namespace Jam
