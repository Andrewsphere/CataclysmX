#pragma once

#include "core/server.h"
#include "core/debug_items.h"
#include "client/settings.h"
#include "client/ai_debug.h"
#include "sdl/item_visual.h"
#include "network/network.h"
#include "client/lan_profile.h"

#include <EASTL/array.h>
#include <condition_variable>
#include <mutex>

namespace cataclysmx::sdl {

enum class SessionMode { Local, Host, Guest };
enum class RequestKind {
    Command, Pause, Resume, Inspect, ApplySettings, ResumeAndCommand, InspectAi, ItemCommand, PauseForItems,
    OpenLan, CloseLan, Connect, Disconnect, RefreshDiscovery, StopDiscovery, Save, DebugGiveItems
};
struct SessionRequest {
    RequestKind kind = RequestKind::Command;
    std::uint64_t generation = 0;
    std::uint64_t id = 0;
    PersistentId player = 0;
    std::uint64_t opportunity = 0;
    Command command;
    TileCoord cursor{};
    SettingsOverrides globalSettings;
    SettingsOverrides worldSettings;
    network::HostSettings hostSettings;
    network::ConnectionSettings connectionSettings;
    bool newCharacter = false;
    String discoveryAddress;
    DebugItemGrant grant;
};
enum class ReplyKind {
    Started, StartupFailed, Admission, Completed, Paused, Resumed, Stale, Cancelled, Inspected,
    SettingsApplied, SettingsFailed, AiInspected, MenuOpened, ControlCompleted, ControlFailed, ItemsGiven
};
enum class StartupError { None, InvalidContent, InvalidSettings, Network, Storage };
struct SessionReply {
    ReplyKind kind = ReplyKind::Started;
    std::uint64_t generation = 0;
    std::uint64_t id = 0;
    CommandResult result{};
    StartupError startupError = StartupError::None;
    Vector<String> details;
    WorldView inspectionView;
    Vector<MapItemVisual> inspectionItems;
    Settings runtimeSettings;
    AiDebugView aiDebug;
    // Authoritative input context may be newer than the last rendered view.
    PlayerInputContext input;
    eastl::optional<network::NetworkError> networkError;
    eastl::optional<client::ProfileError> profileError;
    eastl::optional<SaveError> storageError;
    DebugGiveResult debugGive;
};
struct SessionSnapshot {
    std::uint64_t generation = 0;
    bool paused = false;
    WorldView view;
    Vector<InteractionChoice> interactions;
    std::uint32_t seed = 0;
    std::uint32_t generationVersion = 0;
    Settings runtimeSettings;
    Vector<MapItemVisual> mapItems;
    eastl::optional<SaveError> storageError;
    SessionMode mode = SessionMode::Local;
    network::ConnectionState connectionState = network::ConnectionState::Disconnected;
    PlayerInputContext input;
    bool menuOpen = false; // Input ownership; only Local mode also pauses simulation.
    bool hasSave = false; // Save authority belongs to Local/Host, never Guest.
    std::uint16_t hostPort = 0;
    std::uint16_t discoveryPort = 24681;
    Vector<network::DiscoveredSession> sessions;
    std::uint64_t statusVersion = 0;
    String status;
    eastl::optional<network::NetworkError> networkError;
    eastl::optional<client::ProfileError> profileError;
};

enum class EnqueueResult { Accepted, Busy, Stopped };

// One frontend and one session owner. Each accepted request reserves two reply slots.
class SessionChannels {
public:
    static constexpr std::size_t requestCapacity = 32;
    static constexpr std::size_t replyCapacity = 2 * requestCapacity + 1;
    EnqueueResult TrySubmit(SessionRequest request);
    bool TryPopReply(SessionReply& reply);
    bool TakeSnapshot(SessionSnapshot& current);
    void Stop();

    // Owner-side operations.
    bool TakeRequest(SessionRequest& request);
    void PublishReply(SessionReply reply, bool releasesUnusedSlot = false);
    void PublishSnapshot(SessionSnapshot& building);
    bool Wait();
    bool Stopping();
private:
    std::mutex mutex_;
    std::condition_variable condition_;
    eastl::array<SessionRequest, requestCapacity> requests_{};
    eastl::array<SessionReply, replyCapacity> replies_{};
    std::size_t requestHead_ = 0;
    std::size_t requestCount_ = 0;
    std::size_t replyHead_ = 0;
    std::size_t replyCount_ = 0;
    std::size_t reserved_ = 1; // Startup handshake.
    bool stopping_ = false;
    SessionSnapshot pending_;
    bool fresh_ = false;
};

}
