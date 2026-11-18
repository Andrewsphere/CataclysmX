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
    bool runRequested = false;
    Tick fundedTicks = 0;
    std::uint64_t generatorOutputRemaining = 0;
    Tick generatorIntervalStart = 0;
    bool generatorIntervalInitialized = false;
};

StateResult<MachineState> ReadState(const ObjectDefinition& definition, const DataObject& input,
    Diagnostics* diagnostics) {
    StateReader reader(input, diagnostics);
    if (!ValidObjectDefinition(definition)) {
        return reader.Invalid("definition", "invalid object definition");
    }
    if (definition.behavior == ObjectBehaviorKind::None || definition.behavior == ObjectBehaviorKind::GridBattery) {
        const auto result = reader.Result();
        if (!result) { return eastl::unexpected(result.error()); }
        if (input.Size() != 0) { return reader.Invalid({}, "generic object state must be empty"); }
        return MachineState{};
    }
    const auto version = reader.GetUnsignedOrError("version");
    if (const auto result = reader.Result(); !result) { return eastl::unexpected(result.error()); }
    const bool generator = definition.behavior == ObjectBehaviorKind::Generator;
    const bool gridMachine = definition.behavior == ObjectBehaviorKind::CraftingMachine && definition.machine.gridPowered;
    if (version != (generator || gridMachine ? 2 : 1)) {
        return reader.Invalid("version", "unsupported object state version", StateError::UnsupportedVersion);
    }
    MachineState state;
    if (generator || gridMachine) {
        state.runRequested = reader.GetBooleanOrError("run_requested");
        if (generator) {
            state.energy = reader.GetUnsignedOrError("energy");
            state.generatorOutputRemaining = reader.GetUnsignedOrError("generator_output_remaining");
            state.generatorIntervalStart = reader.GetUnsignedOrError("generator_interval_start");
            state.generatorIntervalInitialized = reader.GetBooleanOrError("generator_interval_initialized");
        } else {
            state.hasJob = reader.GetBooleanOrError("has_job");
            state.remainingWork = reader.GetUnsignedOrError("remaining_work");
            state.fundedTicks = reader.GetUnsignedOrError("funded_ticks");
        }
        if (const auto result = reader.Result(); !result) { return eastl::unexpected(result.error()); }
        for (std::size_t i = 0; i < input.Size(); ++i) {
            const auto& name = input.Name(i);
            const bool known = name == "version" || name == "run_requested"
                || (generator ? name == "energy" || name == "generator_output_remaining"
                    || name == "generator_interval_start" || name == "generator_interval_initialized"
                    : name == "has_job" || name == "remaining_work" || name == "funded_ticks");
            if (!known) { return reader.Invalid({name.data(), name.size()}, "unknown object state field"); }
        }
        if (generator) {
            if (state.energy > definition.generator.fuelCapacity) {
                return reader.Invalid("energy", "fuel exceeds capacity");
            }
            if (state.generatorIntervalStart % ticks_per_turn != 0
                || (!state.generatorIntervalInitialized && state.generatorIntervalStart != 0)) {
                return reader.Invalid("generator_interval_start", "invalid interval anchor");
            }
            if (state.generatorOutputRemaining > std::uint64_t{definition.generator.outputPerTick} * ticks_per_turn
                || (state.generatorOutputRemaining != 0 && (!state.runRequested || !state.generatorIntervalInitialized))) {
                return reader.Invalid("generator_output_remaining", "invalid interval allowance");
            }
        } else {
            if (state.remainingWork > definition.machine.duration) {
                return reader.Invalid("remaining_work", "remaining work exceeds duration");
            }
            if (!state.hasJob && (state.runRequested || state.remainingWork != 0 || state.fundedTicks != 0)) {
                return reader.Invalid("has_job", "work or intent without a job");
            }
            if (state.fundedTicks > state.remainingWork) {
                return reader.Invalid("funded_ticks", "funded work exceeds remaining work");
            }
        }
        return state;
    }
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
        if (Config().gridPowered) {
            if (!state_.runRequested || state_.fundedTicks == 0) { return eastl::nullopt; }
            return state_.fundedTicks;
        }
        if (!state_.running) { return eastl::nullopt; }
        return eastl::min(state_.remainingWork, state_.energy / Config().energyPerTick);
    }
    void Elapse(Tick elapsed) override {
        if (Config().gridPowered) {
            if (!state_.runRequested || !state_.hasJob) { return; }
            const Tick work = eastl::min(elapsed, state_.fundedTicks);
            state_.remainingWork -= work;
            state_.fundedTicks -= work;
            return;
        }
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
        state_.runRequested = false;
    }
    ObjectUseReason Check(const ObjectReadContext& context, ActorHandle actor, ObjectAction action) const {
        switch (action) {
        case ObjectAction::Start:
            if (state_.hasJob && (Requested() || state_.remainingWork == 0)) { return ObjectUseReason::Busy; }
            if (!Config().gridPowered && state_.energy < Config().energyPerTick) { return ObjectUseReason::NoPower; }
            return state_.hasJob ? ObjectUseReason::None : context.CheckInputs(actor, Config().input, Config().inputCount);
        case ObjectAction::Stop:
            return state_.hasJob && Requested() && state_.remainingWork != 0 ? ObjectUseReason::None : ObjectUseReason::NoJob;
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
            if (state_.hasJob && (Requested() || state_.remainingWork == 0)) {
                return Refused(ObjectUseReason::Busy);
            }
            if (!Config().gridPowered && state_.energy < Config().energyPerTick) { return Refused(ObjectUseReason::NoPower); }
            if (!state_.hasJob) {
                const auto consumed = context.ConsumeInputs(actor, Config().input, Config().inputCount);
                if (!consumed) { return MutationFailure(consumed.error(), ObjectUseReason::NoInputs); }
                state_.hasJob = true;
                state_.remainingWork = Config().duration;
            }
            if (Config().gridPowered) { state_.runRequested = true; }
            else { state_.running = true; }
            break;
        case ObjectAction::Stop:
            if (!state_.hasJob || !Requested() || state_.remainingWork == 0) {
                return Refused(ObjectUseReason::NoJob);
            }
            state_.running = false;
            state_.runRequested = false;
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
        if (Config().gridPowered) {
            result.SetUnsigned("version", 2);
            result.SetBoolean("has_job", state_.hasJob);
            result.SetBoolean("run_requested", state_.runRequested);
            result.SetUnsigned("remaining_work", state_.remainingWork);
            result.SetUnsigned("funded_ticks", state_.fundedTicks);
            return result;
        }
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
            config.energyPerTick, state_.hasJob,
            config.gridPowered ? state_.runRequested && state_.fundedTicks > 0 : state_.running,
            state_.remainingWork, config.duration,
            config.input, config.inputCount, config.output, config.outputCount,
            config.gridPowered, Requested(), state_.fundedTicks};
    }
    WorldResult<void> SetEnergy(std::uint64_t energy) override {
        if (Config().gridPowered) { return eastl::unexpected(WorldError::invalid_request); }
        if (energy > Config().energyCapacity) { return eastl::unexpected(WorldError::invalid_request); }
        state_.energy = energy;
        if (state_.remainingWork > 0 && energy < Config().energyPerTick) { state_.running = false; }
        return {};
    }
    WorldResult<void> FundGridWork(Tick ticks) override {
        if (!Config().gridPowered || !state_.hasJob || !state_.runRequested
            || state_.fundedTicks != 0 || ticks == 0 || ticks > state_.remainingWork) {
            return eastl::unexpected(WorldError::invalid_request);
        }
        state_.fundedTicks = ticks;
        return {};
    }
private:
    bool Requested() const { return Config().gridPowered ? state_.runRequested : state_.running; }
    const CraftingMachineDefinition& Config() const { return definition_.machine; }
    const ObjectDefinition& definition_;
    MachineState state_;
};

class GeneratorBehavior final : public ObjectBehavior {
public:
    GeneratorBehavior(const ObjectDefinition& definition, MachineState state)
        : definition_(definition), state_(state) {}

    eastl::optional<Tick> NextEventIn() const override { return eastl::nullopt; }
    void Elapse(Tick) override {}
    void ProcessEvent(ObjectMutationContext&) override {}
    ObjectUseReason Check(ObjectAction action) const {
        switch (action) {
        case ObjectAction::Start:
            if (state_.runRequested) { return ObjectUseReason::Busy; }
            return state_.energy > 0 ? ObjectUseReason::None : ObjectUseReason::NoPower;
        case ObjectAction::Stop:
            return state_.runRequested ? ObjectUseReason::None : ObjectUseReason::NoJob;
        case ObjectAction::Refuel:
            return Config().fuelCapacity - state_.energy >= Config().energyPerFuel
                ? ObjectUseReason::None : ObjectUseReason::NoCapacity;
        default: return ObjectUseReason::NoJob;
        }
    }
    void QueryActions(const ObjectReadContext& context, ActorHandle actor, Vector<ObjectActionInfo>& output) const override {
        for (auto action : {ObjectAction::Start, ObjectAction::Stop, ObjectAction::Refuel}) {
            auto reason = Check(action);
            if (action == ObjectAction::Refuel && reason == ObjectUseReason::None) {
                reason = context.CheckInputs(actor, Config().fuelItem, 1);
            }
            output.push_back({action, reason});
        }
    }
    ObjectUseResult Use(ObjectMutationContext& context, ActorHandle actor, ObjectAction action) override {
        if (action != ObjectAction::Start && action != ObjectAction::Stop && action != ObjectAction::Refuel) { return {}; }
        const auto reason = Check(action);
        if (reason != ObjectUseReason::None) { return Refused(reason); }
        switch (action) {
        case ObjectAction::Start: state_.runRequested = true; break;
        case ObjectAction::Stop:
            state_.runRequested = false;
            state_.generatorOutputRemaining = 0;
            break;
        case ObjectAction::Refuel: {
            const auto consumed = context.ConsumeInputs(actor, Config().fuelItem, 1);
            if (!consumed) { return MutationFailure(consumed.error(), ObjectUseReason::NoInputs); }
            state_.energy += Config().energyPerFuel;
            break;
        }
        default: std::abort();
        }
        return {ObjectUseOutcome::Succeeded, 100, ObjectUseReason::None};
    }
    DataObject SaveState() const override {
        DataObject result;
        result.SetUnsigned("version", 2);
        result.SetUnsigned("energy", state_.energy);
        result.SetBoolean("run_requested", state_.runRequested);
        result.SetUnsigned("generator_output_remaining", state_.generatorOutputRemaining);
        result.SetUnsigned("generator_interval_start", state_.generatorIntervalStart);
        result.SetBoolean("generator_interval_initialized", state_.generatorIntervalInitialized);
        return result;
    }
    ObjectStateView Describe() const override {
        ObjectStateView result;
        result.behavior = ObjectBehaviorKind::Generator;
        result.energy = state_.energy;
        result.energyCapacity = Config().fuelCapacity;
        result.energyPerTick = Config().outputPerTick;
        result.running = state_.runRequested && state_.energy > 0;
        result.runRequested = state_.runRequested;
        result.generatorOutputRemaining = state_.generatorOutputRemaining;
        result.generatorIntervalStart = state_.generatorIntervalStart;
        result.generatorIntervalInitialized = state_.generatorIntervalInitialized;
        return result;
    }
    WorldResult<void> SetEnergy(std::uint64_t energy) override {
        if (energy > Config().fuelCapacity) { return eastl::unexpected(WorldError::invalid_request); }
        state_.energy = energy;
        return {};
    }
    WorldResult<void> PrepareGeneratorInterval(Tick now) override {
        const Tick intervalStart = now - now % ticks_per_turn;
        if (state_.generatorIntervalInitialized) {
            if (intervalStart < state_.generatorIntervalStart) { return eastl::unexpected(WorldError::invalid_request); }
            if (intervalStart == state_.generatorIntervalStart) { return {}; }
        }
        // Idle objects do not claim an interval before their first Start.
        if (!state_.runRequested) { state_.generatorOutputRemaining = 0; return {}; }
        state_.generatorIntervalStart = intervalStart;
        state_.generatorIntervalInitialized = true;
        state_.generatorOutputRemaining = std::uint64_t{Config().outputPerTick} * (ticks_per_turn - now % ticks_per_turn);
        return {};
    }
    WorldResult<void> SpendGeneratorEnergy(std::uint64_t energy) override {
        if (!state_.runRequested || !state_.generatorIntervalInitialized
            || energy > state_.generatorOutputRemaining || energy > state_.energy) {
            return eastl::unexpected(WorldError::invalid_request);
        }
        state_.energy -= energy;
        state_.generatorOutputRemaining -= energy;
        return {};
    }
private:
    const GeneratorDefinition& Config() const { return definition_.generator; }
    const ObjectDefinition& definition_;
    MachineState state_;
};

}

ObjectBehavior::~ObjectBehavior() = default;

WorldResult<void> ObjectBehavior::PrepareGeneratorInterval(Tick) {
    return eastl::unexpected(WorldError::invalid_request);
}
WorldResult<void> ObjectBehavior::SpendGeneratorEnergy(std::uint64_t) {
    return eastl::unexpected(WorldError::invalid_request);
}
WorldResult<void> ObjectBehavior::FundGridWork(Tick) {
    return eastl::unexpected(WorldError::invalid_request);
}

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
        if (definition.behavior == ObjectBehaviorKind::CraftingMachine && !definition.machine.gridPowered) {
            parsed.energy = definition.machine.energyCapacity;
        }
    }
    switch (definition.behavior) {
    case ObjectBehaviorKind::None:
    case ObjectBehaviorKind::GridBattery: return eastl::unique_ptr<ObjectBehavior>{};
    case ObjectBehaviorKind::CraftingMachine:
        return eastl::unique_ptr<ObjectBehavior>(new CraftingMachineBehavior(definition, parsed));
    case ObjectBehaviorKind::Generator:
        return eastl::unique_ptr<ObjectBehavior>(new GeneratorBehavior(definition, parsed));
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
    case ObjectAction::Refuel: return "Refuel";
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
    case ObjectUseReason::NoCapacity: return "Insufficient capacity";
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
