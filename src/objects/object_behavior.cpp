#include "objects/object_behavior.h"

#include <EASTL/algorithm.h>
#include <EASTL/utility.h>
#include <cstdlib>

namespace cataclysmx {
namespace {

StateResult<void> Invalid(Diagnostics* diagnostics, eastl::string_view field, eastl::string_view message,
    StateError error = StateError::InvalidData) {
    if (diagnostics) {
        DiagnosticFrame frame;
        frame.message.assign(message.data(), message.size());
        frame.location = DiagnosticLocation{{}, String(field.data(), field.size()), {}, {}};
        diagnostics->Add(eastl::move(frame));
    }
    return eastl::unexpected(error);
}

struct MachineState {
    std::uint64_t energy = 0;
    bool hasJob = false;
    bool running = false;
    Tick remainingWork = 0;
};

StateResult<void> ReadState(const ObjectDefinition& definition, const DataObject& input,
    MachineState& state, Diagnostics* diagnostics) {
    if (!ValidObjectDefinition(definition)) {
        return Invalid(diagnostics, "definition", "invalid object definition");
    }
    if (input.Type() != DataType::Object) { return Invalid(diagnostics, "", "expected an object state"); }
    if (definition.behavior == ObjectBehaviorKind::None) {
        if (input.Size() != 0) { return Invalid(diagnostics, "", "generic object state must be empty"); }
        return {};
    }
    for (std::size_t i = 0; i < input.Size(); ++i) {
        const auto& name = input.Name(i);
        if (name != "version" && name != "energy" && name != "has_job"
            && name != "running" && name != "remaining_work") {
            return Invalid(diagnostics, {name.data(), name.size()}, "unknown object state field");
        }
    }
    const auto version = input.GetUnsigned("version");
    if (!version) { return Invalid(diagnostics, "version", "expected an unsigned state version"); }
    if (*version != 1) {
        return Invalid(diagnostics, "version", "unsupported object state version", StateError::UnsupportedVersion);
    }
    const auto energy = input.GetUnsigned("energy");
    const auto hasJob = input.GetBoolean("has_job");
    const auto running = input.GetBoolean("running");
    const auto remaining = input.GetUnsigned("remaining_work");
    if (!energy || *energy > definition.machine.energyCapacity) {
        return Invalid(diagnostics, "energy", "energy is absent, wrongly typed, or exceeds capacity");
    }
    if (!hasJob) { return Invalid(diagnostics, "has_job", "expected a boolean"); }
    if (!running) { return Invalid(diagnostics, "running", "expected a boolean"); }
    if (!remaining || *remaining > definition.machine.duration) {
        return Invalid(diagnostics, "remaining_work", "remaining work is absent, wrongly typed, or exceeds duration");
    }
    if ((!*hasJob && (*running || *remaining != 0))
        || (*running && *remaining > 0 && *energy < definition.machine.energyPerTick)) {
        return Invalid(diagnostics, "running", "inconsistent job, work, or power state");
    }
    state = {*energy, *hasJob, *running, *remaining};
    return {};
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
    MachineState parsed;
    return ReadState(definition, state, parsed, diagnostics);
}

StateResult<eastl::unique_ptr<ObjectBehavior>> MakeObjectBehavior(const ObjectDefinition& definition,
    const DataObject* state, Diagnostics* diagnostics) {
    MachineState parsed;
    if (state) {
        const auto validated = ReadState(definition, *state, parsed, diagnostics);
        if (!validated) { return eastl::unexpected(validated.error()); }
    } else {
        if (!ValidObjectDefinition(definition)) {
            Invalid(diagnostics, "definition", "invalid object definition");
            return eastl::unexpected(StateError::InvalidData);
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

}
