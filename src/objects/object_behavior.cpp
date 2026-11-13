#include "objects/object_behavior.h"
#include "core/state_reader.h"

#include <EASTL/algorithm.h>
#include <EASTL/utility.h>
#include <cstdlib>

namespace cataclysmx {
namespace {

struct MachineState {
    std::uint64_t energy = 0;
    bool hasJob = false;
    bool running = false;
    Tick remainingWork = 0;
};

StateResult<MachineState> ReadState(const ObjectDefinition& definition, const DataObject& input,
    Diagnostics* diagnostics) {
    StateReader reader(input, diagnostics);
    if (!ValidObjectDefinition(definition)) {
        return reader.Invalid("definition", "invalid object definition");
    }
    if (definition.behavior == ObjectBehaviorKind::None) {
        const auto result = reader.Result();
        if (!result) { return eastl::unexpected(result.error()); }
        if (input.Size() != 0) { return reader.Invalid({}, "generic object state must be empty"); }
        return MachineState{};
    }
    const auto version = reader.GetUnsignedOrError("version");
    if (const auto result = reader.Result(); !result) { return eastl::unexpected(result.error()); }
    if (version != 1) {
        return reader.Invalid("version", "unsupported object state version", StateError::UnsupportedVersion);
    }
    MachineState state;
    state.energy = reader.GetUnsignedOrDefault("energy", 0);
    state.hasJob = reader.GetBooleanOrDefault("has_job", false);
    state.running = reader.GetBooleanOrDefault("running", false);
    state.remainingWork = state.hasJob ? reader.GetUnsignedOrError("remaining_work")
        : reader.GetUnsignedOrDefault("remaining_work", 0);
    const auto result = reader.Result();
    if (!result) { return eastl::unexpected(result.error()); }
    for (std::size_t i = 0; i < input.Size(); ++i) {
        const auto& name = input.Name(i);
        if (name != "version" && name != "energy" && name != "has_job"
            && name != "running" && name != "remaining_work") {
            return reader.Invalid({name.data(), name.size()}, "unknown object state field");
        }
    }
    if (state.energy > definition.machine.energyCapacity) {
        return reader.Invalid("energy", "energy exceeds capacity");
    }
    if (state.remainingWork > definition.machine.duration) {
        return reader.Invalid("remaining_work", "remaining work exceeds duration");
    }
    if ((!state.hasJob && (state.running || state.remainingWork != 0))
        || (state.running && state.remainingWork > 0 && state.energy < definition.machine.energyPerTick)) {
        return reader.Invalid("running", "inconsistent job, work, or power state");
    }
    return state;
}

ObjectUseResult Refused(ObjectUseReason reason) { return {ObjectUseOutcome::Refused, 0, reason}; }
ObjectUseResult MutationFailure(WorldError error, ObjectUseReason fallback) {
    switch (error) {
    case WorldError::invalid_handle:
        return {ObjectUseOutcome::Inaccessible, 0, ObjectUseReason::InvalidActor};
    case WorldError::blocked:
    case WorldError::chunk_unavailable:
    case WorldError::invalid_position:
        return {ObjectUseOutcome::Inaccessible, 0, ObjectUseReason::OutOfReach};
    case WorldError::capacity_exceeded: return Refused(ObjectUseReason::NoCapacity);
    default: return Refused(fallback);
    }
}

class CraftingMachineBehavior final : public ObjectBehavior {
public:
    CraftingMachineBehavior(const ObjectDefinition& definition, MachineState state)
        : definition_(definition), state_(state) {}

    eastl::optional<Tick> NextEventIn() const override {
        if (!state_.hasJob) { return eastl::nullopt; }
        if (state_.remainingWork == 0) { return Tick{0}; }
        if (!state_.running) { return eastl::nullopt; }
        return eastl::min(state_.remainingWork, state_.energy / Config().energyPerTick);
    }
    void Elapse(Tick elapsed) override {
        if (!state_.running || !state_.hasJob || state_.remainingWork == 0) { return; }
        const Tick work = eastl::min(elapsed,
            eastl::min(state_.remainingWork, state_.energy / Config().energyPerTick));
        state_.remainingWork -= work;
        state_.energy -= work * Config().energyPerTick;
        if (state_.remainingWork > 0 && state_.energy < Config().energyPerTick) { state_.running = false; }
    }
    void ProcessEvent(ObjectMutationContext& context) override {
        if (!state_.hasJob || state_.remainingWork != 0) { return; }
        // Produce is all-or-nothing; failure means World violated the callback contract.
        if (!context.Produce(Config().output, Config().outputCount)) { std::abort(); }
        state_.hasJob = false;
        state_.running = false;
    }
    ObjectUseReason Check(const ObjectReadContext& context, ActorHandle actor, ObjectAction action) const {
        switch (action) {
        case ObjectAction::Start:
            if (state_.hasJob && (state_.running || state_.remainingWork == 0)) { return ObjectUseReason::Busy; }
            if (state_.energy < Config().energyPerTick) { return ObjectUseReason::NoPower; }
            return state_.hasJob ? ObjectUseReason::None : context.CheckInputs(actor, Config().input, Config().inputCount);
        case ObjectAction::Stop:
            return state_.hasJob && state_.running && state_.remainingWork != 0 ? ObjectUseReason::None : ObjectUseReason::NoJob;
        case ObjectAction::Collect: return context.CheckCollect(actor);
        default: return ObjectUseReason::NoJob;
        }
    }
    void QueryActions(const ObjectReadContext& context, ActorHandle actor, Vector<ObjectActionInfo>& output) const override {
        for (auto action : {ObjectAction::Start, ObjectAction::Stop, ObjectAction::Collect}) {
            output.push_back({action, Check(context, actor, action)});
        }
    }
    ObjectUseResult Use(ObjectMutationContext& context, ActorHandle actor, ObjectAction action) override {
        switch (action) {
        case ObjectAction::Start:
            if (state_.hasJob && (state_.running || state_.remainingWork == 0)) {
                return Refused(ObjectUseReason::Busy);
            }
            if (state_.energy < Config().energyPerTick) { return Refused(ObjectUseReason::NoPower); }
            if (!state_.hasJob) {
                const auto consumed = context.ConsumeInputs(actor, Config().input, Config().inputCount);
                if (!consumed) { return MutationFailure(consumed.error(), ObjectUseReason::NoInputs); }
                state_.hasJob = true;
                state_.remainingWork = Config().duration;
            }
            state_.running = true;
            break;
        case ObjectAction::Stop:
            if (!state_.hasJob || !state_.running || state_.remainingWork == 0) {
                return Refused(ObjectUseReason::NoJob);
            }
            state_.running = false;
            break;
        case ObjectAction::Collect: {
            const auto collected = context.CollectContents(actor);
            if (!collected) { return MutationFailure(collected.error(), ObjectUseReason::NoContents); }
            break;
        }
        default: return {};
        }
        return {ObjectUseOutcome::Succeeded, 100, ObjectUseReason::None};
    }
    DataObject SaveState() const override {
        DataObject result;
        result.SetUnsigned("version", 1);
        result.SetUnsigned("energy", state_.energy);
        result.SetBoolean("has_job", state_.hasJob);
        result.SetBoolean("running", state_.running);
        result.SetUnsigned("remaining_work", state_.remainingWork);
        return result;
    }
    ObjectStateView Describe() const override {
        const auto& config = Config();
        return {ObjectBehaviorKind::CraftingMachine, state_.energy, config.energyCapacity,
            config.energyPerTick, state_.hasJob, state_.running, state_.remainingWork, config.duration,
            config.input, config.inputCount, config.output, config.outputCount};
    }
    WorldResult<void> SetEnergy(std::uint64_t energy) override {
        if (energy > Config().energyCapacity) { return eastl::unexpected(WorldError::invalid_request); }
        state_.energy = energy;
        if (state_.remainingWork > 0 && energy < Config().energyPerTick) { state_.running = false; }
        return {};
    }
private:
    const CraftingMachineDefinition& Config() const { return definition_.machine; }
    const ObjectDefinition& definition_;
    MachineState state_;
};

}

ObjectBehavior::~ObjectBehavior() = default;

StateResult<void> ValidateObjectBehaviorState(const ObjectDefinition& definition, const DataObject& state,
    Diagnostics* diagnostics) {
    const auto parsed = ReadState(definition, state, diagnostics);
    if (!parsed) { return eastl::unexpected(parsed.error()); }
    return {};
}

StateResult<eastl::unique_ptr<ObjectBehavior>> MakeObjectBehavior(const ObjectDefinition& definition,
    const DataObject* state, Diagnostics* diagnostics) {
    MachineState parsed;
    if (state) {
        const auto saved = ReadState(definition, *state, diagnostics);
        if (!saved) { return eastl::unexpected(saved.error()); }
        parsed = *saved;
    } else {
        if (!ValidObjectDefinition(definition)) {
            return cataclysmx::InvalidState(diagnostics, {}, "definition", "invalid object definition");
        }
        parsed.energy = definition.machine.energyCapacity;
    }
    switch (definition.behavior) {
    case ObjectBehaviorKind::None: return eastl::unique_ptr<ObjectBehavior>{};
    case ObjectBehaviorKind::CraftingMachine:
        return eastl::unique_ptr<ObjectBehavior>(new CraftingMachineBehavior(definition, parsed));
    default: return eastl::unexpected(StateError::InvalidData);
    }
}

const char* ObjectActionLabel(ObjectAction action) {
    switch (action) {
    case ObjectAction::Start: return "Start / resume";
    case ObjectAction::Stop: return "Stop";
    case ObjectAction::Collect: return "Collect output";
    case ObjectAction::Enable: return "Enable";
    case ObjectAction::Disable: return "Disable";
    }
    std::abort();
}
const char* ObjectReasonText(ObjectUseReason reason) {
    switch (reason) {
    case ObjectUseReason::None: return "";
    case ObjectUseReason::NoInputs: return "Missing inputs";
    case ObjectUseReason::NoPower: return "Insufficient power";
    case ObjectUseReason::NoJob: return "No running job";
    case ObjectUseReason::Busy: return "Already running";
    case ObjectUseReason::NoCapacity: return "Insufficient carrying capacity";
    case ObjectUseReason::NoContents: return "No output to collect";
    case ObjectUseReason::OutOfReach: return "Out of reach";
    case ObjectUseReason::InvalidActor: return "Actor unavailable";
    case ObjectUseReason::AlreadyOpen: return "Already open";
    case ObjectUseReason::AlreadyClosed: return "Already closed";
    case ObjectUseReason::AlreadyEnabled: return "Already enabled";
    case ObjectUseReason::AlreadyDisabled: return "Already disabled";
    case ObjectUseReason::Occupied: return "Tile occupied";
    case ObjectUseReason::Locked: return "Door is locked";
    case ObjectUseReason::AlreadyLocked: return "Already locked";
    case ObjectUseReason::AlreadyUnlocked: return "Already unlocked";
    case ObjectUseReason::MustClose: return "Close the door first";
    }
    std::abort();
}
}
