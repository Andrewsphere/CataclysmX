#pragma once

#include "core/data_object.h"
#include "core/world_types.h"
#include "items/item_types.h"
#include <EASTL/unique_ptr.h>

namespace cataclysmx {

class World;

enum class ActorIntentKind : std::uint8_t { Wait, Move };
struct ActorIntent {
    ActorIntentKind kind = ActorIntentKind::Wait;
    TileCoord direction{};
    std::uint32_t baseCost = 100;
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

enum class AiControllerKind : std::uint8_t { None, Wander };

class AiController {
public:
    virtual ~AiController();
    virtual AiControllerKind Kind() const = 0;
    virtual ActorIntent ChooseAction(const ActorReadContext& context, std::uint32_t& rng) = 0;
    virtual DataObject SaveState() const = 0;
};

// Stable persistence names; invalid enum values return an empty view.
eastl::string_view AiControllerKindName(AiControllerKind kind);
StateResult<AiControllerKind> ReadAiControllerKind(eastl::string_view name,
    Diagnostics* diagnostics = nullptr);

// Validates the payload, not the {type,state} envelope. None requires {};
// Wander requires exactly {version: unsigned(1)}.
StateResult<void> ValidateAiControllerState(AiControllerKind kind, const DataObject& state,
    Diagnostics* diagnostics = nullptr);
// Null state requests defaults. Explicit state must pass strict validation.
// Success for None holds a null pointer (it is not a factory failure).
StateResult<eastl::unique_ptr<AiController>> MakeAiController(AiControllerKind kind,
    const DataObject* state = nullptr, Diagnostics* diagnostics = nullptr);

DataObject CaptureAiController(const AiController* controller);
StateResult<eastl::unique_ptr<AiController>> ReadAiController(const DataObject& envelope,
    Diagnostics* diagnostics = nullptr);

}
