#pragma once

#include "core/world.h"
#include "core/pacing.h"

#include <EASTL/optional.h>

namespace cataclysmx {

enum class CommandKind { wait, move, pick_up, drop, interact, use_item, store_item };
struct Command {
    CommandKind kind = CommandKind::wait;
    int dx = 0;
    int dz = 0;
    // Pickup/drop: zero picks the first eligible root in persistent-ID order.
    // Item use and storage require an explicit persistent ID.
    PersistentId item = 0;
    // Only wait uses this field. Item-use costs come from the content catalog.
    std::uint32_t waitCost = ticks_per_turn;
    ItemCapabilityId capability{};
    String itemAction; // Stable action name local to the selected behavior.
    // Use: battery to install. Store: destination container; zero extracts to inventory.
    PersistentId otherItem = 0;
};
bool ValidCommandShape(const Command& command);
enum class Outcome {
    waited, moved, picked_up, dropped, opened, closed, toggled,
    blocked, occupied, nothing_here, invalid_command, not_carried, unavailable, not_ready, queued,
    stale_input, duplicate_input, used_item, stored_item, item_use_failed,
    capacity_exceeded, invalid_container
};
const char* OutcomeMessage(Outcome outcome);
// The demo requires floor, wall, and closed_door; floor must be passable.
// It also requires rock, backpack, and bandage; the backpack must fit the bandage.
// Call at the application boundary before constructing a Server.
ContentResult<void> ValidateServerContent(const ContentCatalog& content,
    Diagnostics* diagnostics = nullptr);
struct CommandResult {
    Outcome outcome = Outcome::invalid_command;
    // An action was committed; under a rate cap, tick may still be unchanged.
    bool advanced = false;
    std::uint64_t tick = 0;
    eastl::optional<SaveError> storageError = {};
    Tick cost = 0;
    ItemUseOutcome itemOutcome = ItemUseOutcome::Succeeded;
    ItemUseReason itemReason;
};
const char* CommandResultMessage(const CommandResult& result);

enum class PlayerConnectionState { Parked, Joining, Active };
struct PlayerStatus {
    PersistentId id = 0;
    String name;
    PlayerConnectionState state = PlayerConnectionState::Parked;
    bool ready = false;
    bool commandQueued = false;
    std::uint64_t opportunity = 1;
    std::uint64_t graceMicroseconds = 0;
};
struct PlayerInput {
    std::uint64_t sequence = 0;
    std::uint64_t opportunity = 0;
    Command command;
};
struct PlayerCommandResult {
    PersistentId player = 0;
    std::uint64_t sequence = 0;
    std::uint64_t opportunity = 0;
    CommandResult result;
};
enum class SessionError {
    InvalidPlayer, InvalidName, InvalidToken, UnknownToken, AlreadyConnected,
    Full, Unavailable, StorageFailure
};
template<class T> using SessionResult = eastl::expected<T, SessionError>;
struct PlayerJoin {
    PersistentId player = 0;
    std::uint64_t reconnectToken = 0;
};

struct SessionUpdate {
    Tick tick = 0;
    std::uint64_t skippedActions = 0;
    bool needsInput = true;
    eastl::optional<SaveError> storageError = {};
    // Resolution of the one buffered intent, with its execution tick and cost.
    eastl::optional<CommandResult> commandResult = {};
    Vector<PlayerCommandResult> commandResults;
};

struct ViewCell {
    bool visible = false;
    Tile tile{};
    PersistentId actor = 0;
    bool player = false;
    unsigned int ground_items = 0;
    PersistentId object = 0;
    bool object_enabled = false;
    bool operator==(const ViewCell&) const = default;
};
struct InventoryEntry {
    PersistentId id = 0;
    ItemTypeId type{};
    unsigned int contained_items = 0;
    PersistentId parent = 0;
    ItemCapabilityId installation{};
    std::uint64_t massGrams = 0; // Includes descendants.
    std::uint32_t volumeMl = 0;
    std::uint32_t capacityVolumeMl = 0;
    std::uint64_t usedVolumeMl = 0;
    Vector<ItemEnergyView> energy;
    Vector<ItemBehaviorView> activities;
    Vector<ItemActionInfo> actions;
    bool operator==(const InventoryEntry&) const = default;
};
struct WorldView {
    TileCoord origin{};
    int width = 0;
    int height = 0; // Display rows along world Z, not world elevation.
    TileCoord player_position{};
    std::uint64_t tick = 0;
    Vector<ViewCell> cells; // Row major. Hidden cells contain no world information.
    Vector<InventoryEntry> inventory;
    // Both lists include nested contents in persistent-ID order; parent links
    // describe trees. Ground items are limited to the player's current tile.
    Vector<InventoryEntry> groundItems;
    std::uint64_t carriedMassGrams = 0;
    std::uint64_t carryCapacityGrams = 100000;
    Tick remainingTicks = 0;
    std::uint32_t speed = 100;
    PacingSettings pacing{};
    bool commandQueued = false;
    PersistentId playerId = 0;
    std::uint64_t revision = 0;
    std::uint64_t opportunity = 1;
    std::uint64_t lastInputSequence = 0;
    Vector<PlayerStatus> players;
};

// Authoritative deterministic endpoint. No transport or clock access.
class Server {
public:
    explicit Server(const ContentCatalog& content, std::uint32_t seed = 42);
    // While busy, retain one intent; the latest valid command replaces it.
    CommandResult Send(Command command);
    // Real elapsed time is supplied by the application; no clock is read here.
    SessionUpdate Update(std::uint64_t elapsedMicroseconds);
    bool SetPacing(PacingSettings settings);
    PacingSettings GetPacing() const;
    WorldView View(int radius = 8) const;
    SessionResult<PlayerJoin> JoinPlayer(eastl::string_view name,
        std::uint64_t reconnectToken, std::uint64_t newToken);
    SessionResult<void> ActivatePlayer(PersistentId player);
    SessionResult<void> DisconnectPlayer(PersistentId player);
    CommandResult Submit(PersistentId player, PlayerInput input);
    PersistentId GetHostPlayerId() const;
    Vector<PlayerStatus> GetPlayers() const;
    WorldView ViewForPlayer(PersistentId player, int radius = 8) const;
    std::uint64_t GetRevision() const;
    WorldResult<void> SetBubbleRadii(int active_radius, int resident_radius,
        Diagnostics* diagnostics = nullptr);
    SaveResult<void> CreateSave(const char* path, Diagnostics* diagnostics = nullptr);
    SaveResult<void> Save(Diagnostics* diagnostics = nullptr);
    static SaveResult<eastl::unique_ptr<Server>> LoadSave(const ContentCatalog& content, const char* path,
        Diagnostics* diagnostics = nullptr);
    void SetAutosaveInterval(std::uint64_t turns);
    std::uint64_t GetAutosaveInterval() const;

    // Administrative/headless inspection; ordinary clients consume View().
    World& GetWorld();
    const World& GetWorld() const;
    ActorHandle GetPlayer() const;
    std::uint64_t GetTick() const;

private:
    struct EmptyTag {};
    explicit Server(const ContentCatalog& content, EmptyTag);
    World world_;
    ActorHandle player_{};
    LoadRequest active_request_{};
    LoadRequest resident_request_{};
    int active_radius_ = 1;
    int resident_radius_ = 2;
    std::uint64_t tick_ = 0;
    std::uint32_t rng_state_ = 0;
    ChunkCoord bubbleCenter_{};
    std::uint64_t autosaveInterval_ = 0;
    std::uint64_t lastSaveTick_ = 0;
    TickPacer pacer_;
    struct PlayerSlot {
        PersistentId id = 0;
        String name;
        std::uint64_t token = 0;
        TileCoord position{};
        PlayerConnectionState state = PlayerConnectionState::Parked;
        LoadRequest activeRequest{};
        LoadRequest residentRequest{};
        ChunkCoord center{};
        eastl::optional<PlayerInput> pending;
        std::uint64_t opportunity = 1;
        std::uint64_t lastSequence = 0;
    };
    Vector<PlayerSlot> players_;
    std::uint64_t revision_ = 1;
    std::uint64_t legacySequence_ = 0;

    CommandResult Execute(ActorHandle handle, PlayerSlot& slot, Command command);
    PlayerSlot* FindPlayer(PersistentId id);
    const PlayerSlot* FindPlayer(PersistentId id) const;
    ActorHandle PlayerActor(const PlayerSlot& slot) const;
    SaveResult<void> UpdatePlayerBubble(PlayerSlot& slot, int activeRadius, int residentRadius,
        Diagnostics* diagnostics = nullptr);
    bool ResolveRound(SessionUpdate& result, const Vector<ActorHandle>& expired);
    void SynchronizePacing();
    eastl::optional<SaveError> Autosave();
    SessionState Checkpoint() const;
};

}
