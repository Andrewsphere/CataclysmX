#pragma once

#include "content/object_definition.h"
#include "core/data_object.h"
#include "core/world_result.h"

#include <EASTL/optional.h>
#include <EASTL/unique_ptr.h>

namespace cataclysmx {

class World;

enum class ObjectAction { Start, Stop, Collect, Enable, Disable };
enum class ObjectUseOutcome { Succeeded, Refused, Inaccessible, InvalidAction };
enum class ObjectUseReason {
    None, NoInputs, NoPower, NoJob, Busy, NoCapacity, NoContents, OutOfReach, InvalidActor,
    AlreadyOpen, AlreadyClosed, AlreadyEnabled, AlreadyDisabled, Occupied, Locked, AlreadyLocked, AlreadyUnlocked, MustClose
};
struct ObjectUseResult {
    ObjectUseOutcome outcome = ObjectUseOutcome::InvalidAction;
    std::uint32_t baseCost = 0;
    ObjectUseReason reason = ObjectUseReason::None;
};
struct ObjectActionInfo {
    ObjectAction action = ObjectAction::Start;
    ObjectUseReason reason = ObjectUseReason::None;
};
const char* ObjectActionLabel(ObjectAction action);
const char* ObjectReasonText(ObjectUseReason reason);

class ObjectReadContext {
public:
    ObjectUseReason CheckInputs(ActorHandle actor, ItemTypeId type, std::uint32_t count) const;
    ObjectUseReason CheckCollect(ActorHandle actor) const;
private:
    friend class World;
    ObjectReadContext(const World& world, ObjectHandle self) : world_(world), self_(self) {}
    const World& world_;
    ObjectHandle self_;
};

struct ObjectStateView {
    ObjectBehaviorKind behavior = ObjectBehaviorKind::None;
    std::uint64_t energy = 0;
    std::uint64_t energyCapacity = 0;
    std::uint32_t energyPerTick = 0;
    bool hasJob = false;
    bool running = false;
    Tick remainingWork = 0;
    Tick duration = 0;
    ItemTypeId input{};
    std::uint32_t inputCount = 0;
    ItemTypeId output{};
    std::uint32_t outputCount = 0;
};

// Callback-scoped mutation services. World implements the atomic transactions
// and access checks. No reference into entity storage may survive these calls.
class ObjectMutationContext {
public:
    ObjectMutationContext(const ObjectMutationContext&) = delete;
    ObjectMutationContext& operator=(const ObjectMutationContext&) = delete;
    const World& GetWorld() const;
    ObjectHandle Self() const;
    WorldResult<void> ConsumeInputs(ActorHandle actor, ItemTypeId type, std::uint32_t count);
    WorldResult<void> Produce(ItemTypeId type, std::uint32_t count);
    WorldResult<void> CollectContents(ActorHandle actor);
    void RequestDestroy();
private:
    friend class World;
    ObjectMutationContext(World& world, ObjectHandle self);
    World& world_;
    ObjectHandle self_;
    bool destroyRequested_ = false;
};

class ObjectBehavior {
public:
    virtual ~ObjectBehavior();
    virtual eastl::optional<Tick> NextEventIn() const = 0;
    virtual void Elapse(Tick elapsed) = 0;
    virtual void ProcessEvent(ObjectMutationContext& context) = 0;
    virtual void QueryActions(const ObjectReadContext& context, ActorHandle actor,
        Vector<ObjectActionInfo>& output) const = 0;
    virtual ObjectUseResult Use(ObjectMutationContext& context, ActorHandle actor, ObjectAction action) = 0;
    virtual DataObject SaveState() const = 0;
    virtual ObjectStateView Describe() const = 0;
    virtual WorldResult<void> SetEnergy(std::uint64_t energy) = 0;
};

// The immutable definition must outlive the returned behavior.
StateResult<eastl::unique_ptr<ObjectBehavior>> MakeObjectBehavior(const ObjectDefinition& definition,
    const DataObject* state = nullptr, Diagnostics* diagnostics = nullptr);
StateResult<void> ValidateObjectBehaviorState(const ObjectDefinition& definition, const DataObject& state,
    Diagnostics* diagnostics = nullptr);

}
