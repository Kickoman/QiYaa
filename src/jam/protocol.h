#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <optional>
#include <variant>
#include <vector>

namespace Jam {

inline constexpr int kProtocol = 1;

struct Track {
    QString id;
    QString albumId;
    QString title;
    QStringList artists;
    qint64 durationMs = 0;
    QString coverUri;

    bool operator==(const Track&) const = default;
};

enum class Order { RoundRobin, Fifo };

struct Settings {
    Order order = Order::RoundRobin;
    bool guestsCanSkip = false;
    bool joinOpen = true;
    int maxPendingPerGuest = 10;

    bool operator==(const Settings&) const = default;
};

struct SettingsPatch {
    std::optional<Order> order;
    std::optional<bool> guestsCanSkip;
    std::optional<bool> joinOpen;
    std::optional<int> maxPendingPerGuest;

    bool isEmpty() const;
    bool operator==(const SettingsPatch&) const = default;
};

enum class App { Desktop, Android, Web };
enum class Source { Item, Wave, Idle };
enum class ParticipantKind { Host, Web, Qiyaa };

struct Participant {
    QString publicId;
    QString name;
    ParticipantKind kind = ParticipantKind::Web;
    bool online = false;
    int pending = 0;

    bool operator==(const Participant&) const = default;
};

struct NowPlaying {
    Source source = Source::Idle;
    QString itemId;
    std::optional<Track> track;
    QString addedBy;
    qint64 positionMs = 0;
    bool paused = true;
    qint64 reportedAt = 0;

    bool operator==(const NowPlaying&) const = default;
};

struct QueueItem {
    QString itemId;
    Track track;
    QString addedBy;
    qint64 addedAt = 0;
    bool pinned = false;

    bool operator==(const QueueItem&) const = default;
};

struct RecentItem {
    QString itemId;
    Track track;
    QString addedBy;
    qint64 playedAt = 0;

    bool operator==(const RecentItem&) const = default;
};

struct Fallback {
    QStringList seeds;
    int seedsVersion = 0;

    bool operator==(const Fallback&) const = default;
};

struct You {
    QString publicId;
    bool isHost = false;

    bool operator==(const You&) const = default;
};

struct Room {
    QString id;
    QString hostName;
    bool hostOnline = false;
    Settings settings;
    You you;
    std::vector<Participant> participants;
    NowPlaying nowPlaying;
    std::vector<QueueItem> queue;
    std::vector<RecentItem> recent;
    Fallback fallback;

    bool operator==(const Room&) const = default;
};

// Client to server.

struct Hello {
    int protocol = kProtocol;
    App app = App::Desktop;
    QString appVersion;

    bool operator==(const Hello&) const = default;
};

struct Create {
    QString id;
    QString hostName;
    std::optional<SettingsPatch> settings;

    bool operator==(const Create&) const = default;
};

struct Resume {
    QString id;
    QString roomId;
    QString hostSecret;
    std::optional<QJsonObject> snapshot;
    QStringList outbox;

    bool operator==(const Resume&) const = default;
};

struct Playing {
    Source source = Source::Idle;
    QString itemId;
    std::optional<Track> track;
    qint64 positionMs = 0;
    bool paused = true;

    bool operator==(const Playing&) const = default;
};

struct Started {
    QString itemId;

    bool operator==(const Started&) const = default;
};

struct Add {
    QString id;
    QString trackId;
    std::optional<Track> track;

    bool operator==(const Add&) const = default;
};

struct Pin {
    QString id;
    QString itemId;

    bool operator==(const Pin&) const = default;
};

struct Remove {
    QString id;
    QString itemId;

    bool operator==(const Remove&) const = default;
};

struct Kick {
    QString id;
    QString publicId;

    bool operator==(const Kick&) const = default;
};

struct ChangeSettings {
    QString id;
    SettingsPatch settings;

    bool operator==(const ChangeSettings&) const = default;
};

struct RotateLink {
    QString id;

    bool operator==(const RotateLink&) const = default;
};

struct End {
    QString id;

    bool operator==(const End&) const = default;
};

enum class SearchError { Failed, Unauthorized };

struct SearchResult {
    QString requestId;
    std::optional<std::vector<Track>> tracks;
    std::optional<SearchError> error;

    bool operator==(const SearchResult&) const = default;
};

enum class ValidateReason { TrackUnavailable, Failed };

struct ValidateEntry {
    QString trackId;
    std::optional<Track> track;
    std::optional<ValidateReason> reason;

    bool operator==(const ValidateEntry&) const = default;
};

struct ValidateResult {
    QString requestId;
    std::vector<ValidateEntry> results;

    bool operator==(const ValidateResult&) const = default;
};

struct Join {
    QString id;
    QString roomId;
    QString joinSecret;
    QString participantId;
    QString name;

    bool operator==(const Join&) const = default;
};

struct Search {
    QString id;
    QString text;

    bool operator==(const Search&) const = default;
};

struct Skip {
    QString id;
    QString itemId;

    bool operator==(const Skip&) const = default;
};

using ClientMessage = std::variant<
    Hello,
    Create,
    Resume,
    Playing,
    Started,
    Add,
    Pin,
    Remove,
    Kick,
    ChangeSettings,
    RotateLink,
    End,
    SearchResult,
    ValidateResult,
    Join,
    Search,
    Skip>;

// Server to client.

struct Welcome {
    int protocol = kProtocol;
    qint64 serverTime = 0;

    bool operator==(const Welcome&) const = default;
};

struct Rejected {
    QString id;
    QString reason;
    QString detail;
    int serverProtocol = 0;

    bool operator==(const Rejected&) const = default;
};

struct Ack {
    QString id;

    bool operator==(const Ack&) const = default;
};

struct Created {
    QString id;
    QString roomId;
    QString hostSecret;
    QString joinSecret;
    QString joinUrl;
    QString publicId;

    bool operator==(const Created&) const = default;
};

struct Resumed {
    QString id;
    bool restored = false;

    bool operator==(const Resumed&) const = default;
};

struct Joined {
    QString id;
    QString publicId;

    bool operator==(const Joined&) const = default;
};

struct SearchResults {
    QString id;
    std::vector<Track> tracks;

    bool operator==(const SearchResults&) const = default;
};

struct LinkRotated {
    QString id;
    QString joinSecret;
    QString joinUrl;

    bool operator==(const LinkRotated&) const = default;
};

struct SearchRequest {
    QString requestId;
    QString text;

    bool operator==(const SearchRequest&) const = default;
};

struct ValidateRequest {
    QString requestId;
    QStringList trackIds;

    bool operator==(const ValidateRequest&) const = default;
};

enum class CommandKind { Skip };

struct Command {
    CommandKind kind = CommandKind::Skip;
    QString itemId;

    bool operator==(const Command&) const = default;
};

struct Snapshot {
    QJsonObject data;

    bool operator==(const Snapshot&) const = default;
};

struct State {
    qint64 version = 0;
    qint64 serverTime = 0;
    Room room;

    bool operator==(const State&) const = default;
};

enum class EndReason { HostEnded, Expired };

struct Ended {
    EndReason reason = EndReason::HostEnded;

    bool operator==(const Ended&) const = default;
};

struct Kicked {
    bool operator==(const Kicked&) const = default;
};

using ServerMessage = std::variant<
    Welcome,
    Rejected,
    Ack,
    Created,
    Resumed,
    Joined,
    SearchResults,
    LinkRotated,
    SearchRequest,
    ValidateRequest,
    Command,
    Snapshot,
    State,
    Ended,
    Kicked>;

}  // namespace Jam
