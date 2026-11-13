#pragma once

#include "core/data_object.h"
#include "core/world_types.h"
#include "core/state_result.h"
#include "core/timing.h"
#include "actors/movement.h"
#include <EASTL/unique_ptr.h>

namespace cataclysmx {

class World;
class ContentCatalog;

enum class AiBehavior : std::uint8_t { Idle, Pursue, Search, Attack };
enum class ActorIntentKind : std::uint8_t { Wait, Move, Attack };
enum class AiMoveStatus : std::uint8_t {
    Available, Unavailable, Inactive, Impassable, Unsupported, Occupied,
    StairsRequired, Backtracking, OutsideSearch
};

// Bounded, allocation-free evidence from the last actual decision, not a fresh
// sensor query. Scores are derived from origin/goal and actorStepDirections.
// Ephemeral: not persisted and never used as a simulation input.
struct AiDecisionInspection {
    Tick tick = 0;
    TileCoord origin{};
    TileCoord goal{};
    TileCoord previousPosition{};
    eastl::array<AiMoveStatus, actorStepDirections.size()> moves{};
    AiBehavior behavior = AiBehavior::Idle;
    ActorIntentKind intent = ActorIntentKind::Wait;
    int selectedMove = -1;
    bool valid = false;
    bool hasGoal = false;
    bool hasPrevious = false;
};

// Squared geometric separation used for local pursuit, not an action cost or
// global path estimate. World-coordinate differences are evaluated in 64 bits.
std::int64_t AiMovementScore(TileCoord position, TileCoord goal);

// Value-only debug view. Work counters describe the most recent decision.
struct AiInspection {
    AiBehavior behavior = AiBehavior::Idle;
    PersistentId target = 0;
    TileCoord lastKnownPosition{};
    Tick observedTick = 0;
    Tick memoryRemaining = 0;
    Tick scanRemaining = 0;
    std::uint32_t candidates = 0;
    std::uint32_t buckets = 0;
    std::uint32_t rays = 0;
    std::uint32_t cells = 0;
    bool targetVisible = false;
    AiDecisionInspection decision;
};

struct ActorIntent {
    ActorIntentKind kind = ActorIntentKind::Wait;
    TileCoord direction{};
    std::uint32_t baseCost = 100;
    ActorHandle target{};
    bool operator==(const ActorIntent&) const = default;
};

// Callback-scoped borrow. Self is a non-owning handle, never an Actor pointer.
class ActorReadContext {
public:
    ActorReadContext(const World& world, ActorHandle self);
    const World& GetWorld() const;
    ActorHandle Self() const;

private:
    const World& world_;
    ActorHandle self_;
};

enum class AiControllerKind : std::uint8_t { None, Wander, Melee, Sighted };

class AiController {
public:
    virtual ~AiController();
    virtual AiControllerKind Kind() const = 0;
    virtual ActorIntent ChooseAction(const ActorReadContext& context, std::uint32_t& rng) = 0;
    virtual DataObject SaveState() const = 0;
    virtual void Elapse(Tick) {}
    virtual AiInspection Inspect() const { return {}; }
};

// Stable persistence names; invalid enum values return an empty view.
eastl::string_view AiControllerKindName(AiControllerKind kind);
StateResult<AiControllerKind> ReadAiControllerKind(eastl::string_view name,
    Diagnostics* diagnostics = nullptr);

// Validates the payload, not the {type,state} envelope. None requires {};
// Wander and Melee require exactly {version: unsigned(1)}.
StateResult<void> ValidateAiControllerState(AiControllerKind kind, const DataObject& state,
    Diagnostics* diagnostics = nullptr);
// Null state requests defaults. Explicit state must pass strict validation.
// Success for None holds a null pointer (it is not a factory failure).
StateResult<eastl::unique_ptr<AiController>> MakeAiController(AiControllerKind kind,
    const DataObject* state = nullptr, Diagnostics* diagnostics = nullptr);

DataObject CaptureAiController(const AiController* controller);
StateResult<eastl::unique_ptr<AiController>> ReadAiController(const DataObject& envelope,
    Diagnostics* diagnostics = nullptr);

// Empty profile selects intrinsic defaults; World validates named definitions.
eastl::unique_ptr<AiController> MakeSightedAiController(eastl::string_view profile);
StateResult<void> ValidateAiControllerContent(const DataObject& envelope,
    const ContentCatalog& content, Diagnostics* diagnostics = nullptr);

}
