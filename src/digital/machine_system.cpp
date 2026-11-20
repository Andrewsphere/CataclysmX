#include "digital/machine_system.h"
#include "core/world.h"
#include "core/ratio.h"
#include "digital/information_system.h"
#include "objects/machine_job.h"
#include "objects/storage_access.h"
#include "skills/learning_system.h"
#include "skills/skill_state.h"
#include <EASTL/algorithm.h>
#include <EASTL/array.h>
#include <EASTL/sort.h>
#include <cstdlib>

namespace cataclysmx {
const char* MachineOutcomeText(MachineOutcome outcome) {
    switch (outcome) {
    case MachineOutcome::Started: return "Ready to start.";
    case MachineOutcome::Resumed: return "Job resumed.";
    case MachineOutcome::PauseRequested: return "Pausing after paid setup.";
    case MachineOutcome::Stopped: return "Job stopped.";
    case MachineOutcome::Cancelled: return "Job cancelled; originals released.";
    case MachineOutcome::Collected: return "Contents collected.";
    case MachineOutcome::ActorUnavailable: return "Actor unavailable.";
    case MachineOutcome::ActorBusy: return "Actor busy.";
    case MachineOutcome::OutOfReach: return "Machine out of reach.";
    case MachineOutcome::Unavailable: return "Machine unavailable.";
    case MachineOutcome::Busy: return "Machine busy.";
    case MachineOutcome::NoJob: return "No machine job.";
    case MachineOutcome::InvalidRecipe: return "Unsupported machine recipe.";
    case MachineOutcome::InvalidQuantity: return "Batch exceeds supported counts or budgets.";
    case MachineOutcome::MissingSkills: return "Missing setup skills.";
    case MachineOutcome::MissingDesign: return "Missing machine design.";
    case MachineOutcome::MissingSoftware: return "Missing control software.";
    case MachineOutcome::MissingInputs: return "Missing materials.";
    case MachineOutcome::NoCapacity: return "Insufficient storage capacity.";
    case MachineOutcome::NoContents: return "No public contents.";
    case MachineOutcome::CompletionPending: return "Completion storage pending.";
    }
    return "Machine unavailable.";
}
namespace {
void Require(bool value) { if (!value) { std::abort(); } }
bool Available(const World& world, ActorHandle handle) {
    const auto* actor = world.GetActor(handle);
    return actor && actor->combat.life == ActorLifeState::Alive && !actor->parked
        && world.IsActive(actor->position);
}
bool Occupied(const World& world, ActorHandle handle) {
    const auto* actor = world.GetActor(handle);
    return actor->actionDeadline.Remaining(world.GetTick())
        || actor->combat.action.kind != ActorActionKind::None
        || world.Learning().Busy(handle) || world.DigitalBusy(handle);
}
bool ActionHeadroom(const World& world, ActorHandle handle) {
    const auto* actor = world.GetActor(handle);
    return actor->turns != UINT64_MAX
        && ActionTicks(100, actor->speed) <= UINT64_MAX - world.GetTick();
}
// Crafting's bounded same-layer flood, including its diagonal corner rule.
bool Nearby(const World& world, TileCoord from, TileCoord to, unsigned radius) {
    if (radius > 8 || from.y != to.y || std::abs(from.x - to.x) > static_cast<int>(radius)
        || std::abs(from.z - to.z) > static_cast<int>(radius)) { return false; }
    eastl::array<TileCoord, 289> queue{};
    eastl::array<bool, 289> seen{};
    const int r = static_cast<int>(radius), width = r * 2 + 1;
    const auto passable = [&world](TileCoord p) {
        return world.GetTile(p) && world.IsActive(p) && !world.BlocksMovement(p);
    };
    std::size_t count = 1;
    queue[0] = from; seen[r * width + r] = true;
    for (std::size_t i = 0; i < count; ++i) {
        const auto current = queue[i];
        if (current == to) { return true; }
        for (int dz = -1; dz <= 1; ++dz) { for (int dx = -1; dx <= 1; ++dx) {
            if (!dx && !dz) { continue; }
            const TileCoord next{current.x + dx, current.z + dz, current.y};
            const int x = next.x - from.x + r, z = next.z - from.z + r;
            if (x < 0 || z < 0 || x >= width || z >= width || seen[z * width + x]) { continue; }
            if (!passable(next) || (dx && dz && (!passable({current.x + dx, current.z, current.y})
                || !passable({current.x, current.z + dz, current.y})))) { continue; }
            seen[z * width + x] = true;
            Require(count < queue.size()); queue[count++] = next;
        } }
    }
    return false;
}
bool Multiply(std::uint64_t a, std::uint64_t b, std::uint64_t& result) {
    if (b && a > UINT64_MAX / b) { return false; }
    result = a * b; return true;
}
bool Add(std::uint64_t& total, std::uint64_t value) {
    if (value > UINT64_MAX - total) { return false; }
    total += value; return true;
}
bool Skills(const World& world, ActorHandle actor, const MachineJobState& job) {
    for (const auto& requirement : job.skills) {
        const auto skill = world.GetContent().FindSkill(requirement.skillName);
        if (!skill.value || LearnedSkillLevel(world.GetActor(actor)->learning, skill) < requirement.level) { return false; }
    }
    return true;
}
}

MachineSystem::MachineSystem(World& world) : world_(world) {}
void MachineSystem::Attach(ObjectHandle machine) {
    const auto* object = world_.GetObject(machine);
    const auto* definition = object ? world_.GetContent().TryGetObject(object->type) : nullptr;
    if (!definition || definition->behavior != ObjectBehaviorKind::SelectableMachine) { return; }
    if (eastl::find(machines_.begin(), machines_.end(), machine) == machines_.end()) { machines_.push_back(machine); }
}
void MachineSystem::Detach(ObjectHandle machine) { eastl::erase(machines_, machine); }
void MachineSystem::Gather(ActorHandle handle, ObjectHandle) const {
    candidates_.clear();
    const auto* actor = world_.GetActor(handle);
    if (!actor) { return; }
    candidates_.insert(candidates_.end(), actor->inventory.begin(), actor->inventory.end());
    const auto position = actor->position;
    const auto radius = world_.GetCraftingSettings().nearbyRadius;
    const int r = static_cast<int>(radius);
    for (int z = -r; z <= r; ++z) { for (int x = -r; x <= r; ++x) {
        const TileCoord tile{position.x + x, position.z + z, position.y};
        const auto object = world_.ObjectAt(tile);
        if (CanReachStorage(world_, position, object, radius)) {
            const auto& contents = world_.GetObject(object)->contents;
            candidates_.insert(candidates_.end(), contents.begin(), contents.end());
        }
        if (Nearby(world_, position, tile, radius)) {
            for (auto item : world_.ItemsAt(tile)) { candidates_.push_back(item); }
        }
    } }
    for (std::size_t i = 0; i < candidates_.size(); ++i) {
        const auto candidate = candidates_[i];
        const auto* item = world_.GetItem(candidate);
        if (!item || item->location.installation.value || world_.IsReservedItem(candidate)
            || world_.GetContent().GetItem(item->type).unfinishedCraft) {
            candidates_[i] = {}; continue;
        }
        candidates_.insert(candidates_.end(), item->contents.begin(), item->contents.end());
        if (!item->contents.empty()) { candidates_[i] = {}; }
    }
    eastl::erase(candidates_, ItemHandle{});
    eastl::sort(candidates_.begin(), candidates_.end(), [this](auto a, auto b) {
        return world_.GetItem(a)->id < world_.GetItem(b)->id;
    });
}
MachineOutcome MachineSystem::Select(ActorHandle handle, ObjectHandle machine,
    MachineRecipeId recipe, std::uint16_t quantity) const {
    selected_.clear();
    if (!Available(world_, handle)) { return MachineOutcome::ActorUnavailable; }
    if (Occupied(world_, handle) || world_.CraftingBusy(handle)) { return MachineOutcome::ActorBusy; }
    const auto* object = world_.GetObject(machine);
    const auto& content = world_.GetContent();
    const auto* definition = object ? content.TryGetObject(object->type) : nullptr;
    if (!definition || definition->behavior != ObjectBehaviorKind::SelectableMachine
        || !object->enabled || object->broken) { return MachineOutcome::Unavailable; }
    if (!world_.CanUseObject(handle, machine)) { return MachineOutcome::OutOfReach; }
    if (world_.GetMachineJob(machine)) { return MachineOutcome::Busy; }
    const auto* selected = content.TryGetMachineRecipe(recipe);
    if (!selected || eastl::find(definition->selectableRecipes.begin(), definition->selectableRecipes.end(), recipe)
        == definition->selectableRecipes.end()) { return MachineOutcome::InvalidRecipe; }
    if (!quantity || quantity > 255 || std::uint64_t{selected->result.count} * quantity > 255) {
        return MachineOutcome::InvalidQuantity;
    }
    std::uint64_t duration = 0, energy = 0, mass = 0, outputVolume = 0;
    const auto& output = content.GetItem(selected->result.item);
    if (!Multiply(selected->duration, quantity, duration)
        || !Multiply(duration, selected->energyPerTick, energy)
        || !Multiply(output.massGrams, std::uint64_t{selected->result.count} * quantity, mass)
        || !Multiply(output.volumeMl, std::uint64_t{selected->result.count} * quantity, outputVolume)
        || std::uint64_t{selected->setupWork} + std::uint64_t{selected->setupWorkPerUnit} * quantity > UINT32_MAX
        || std::uint64_t{selected->learning.xpPerExecution} * quantity > UINT32_MAX) {
        return MachineOutcome::InvalidQuantity;
    }
    for (const auto& input : selected->inputs) {
        if (std::uint64_t{input.Count()} * quantity > 255) { return MachineOutcome::InvalidQuantity; }
    }
    const auto* actor = world_.GetActor(handle);
    const auto setup = (std::uint64_t{selected->setupWork} + std::uint64_t{selected->setupWorkPerUnit} * quantity) * ratioScale;
    constexpr std::uint64_t numerator = std::uint64_t{ticks_per_turn} * ratioScale;
    Tick setupTicks = machineSetupSliceTicks;
    if (setup <= machineSetupSliceTicks * numerator / actor->speed) {
        setupTicks = eastl::max<Tick>(1, (setup * actor->speed + numerator - 1) / numerator);
    }
    if (actor->turns == UINT64_MAX || setupTicks > UINT64_MAX - world_.GetTick()) { return MachineOutcome::Unavailable; }
    if (!MeetsSkillRequirements(world_.GetActor(handle)->learning, selected->skills)) { return MachineOutcome::MissingSkills; }
    const InformationEndpoint endpoint{0, object->id};
    // A selectable entry is supported, not necessarily supplied: supplied designs
    // are packages in the controller's per-instance library.
    bool design = false;
    for (std::size_t i = 1; i <= content.InformationCount(); ++i) {
        const InformationId package{static_cast<std::uint32_t>(i)};
        const auto& designs = content.GetInformation(package).designs;
        if (eastl::find(designs.begin(), designs.end(), recipe) != designs.end()
            && world_.Information().HasPackage(endpoint, package)) { design = true; break; }
    }
    if (!design) { return MachineOutcome::MissingDesign; }
    if (selected->requiredSoftware.value && !world_.Information().HasPackage(endpoint, selected->requiredSoftware)) {
        return MachineOutcome::MissingSoftware;
    }
    std::uint64_t inputVolume = 0, inputMass = 0, alreadyStored = 0;
    for (const auto& input : selected->inputs) {
        unsigned count = 0;
        const auto needed = unsigned{input.Count()} * quantity;
        for (auto candidate : candidates_) {
            const auto* item = world_.GetItem(candidate);
            if (item->type != input.Item()) { continue; }
            if (!Add(inputVolume, world_.GetItemVolume(candidate)) || !Add(inputMass, world_.GetItemMass(candidate))) {
                return MachineOutcome::InvalidQuantity;
            }
            if (item->location.kind == LocationKind::object && item->location.object == machine) {
                if (!Add(alreadyStored, world_.GetItemVolume(candidate))) { return MachineOutcome::InvalidQuantity; }
            }
            selected_.push_back(candidate);
            if (++count == needed) { break; }
        }
        if (count != needed) { return MachineOutcome::MissingInputs; }
    }
    const auto used = world_.GetObjectUsedVolume(machine);
    const auto reservation = eastl::max(inputVolume, outputVolume);
    if (!definition->capacityVolumeMl || used < alreadyStored
        || used - alreadyStored > *definition->capacityVolumeMl
        || reservation > *definition->capacityVolumeMl - (used - alreadyStored)) { return MachineOutcome::NoCapacity; }
    eastl::sort(selected_.begin(), selected_.end(), [this](auto a, auto b) { return world_.GetItem(a)->id < world_.GetItem(b)->id; });
    return MachineOutcome::Started;
}
MachineOutcome MachineSystem::QueryStart(ActorHandle actor, ObjectHandle machine,
    MachineRecipeId recipe, std::uint16_t quantity) const {
    Gather(actor, machine); return Select(actor, machine, recipe, quantity);
}
WorldResult<MachineResult> MachineSystem::Start(ActorHandle actor, ObjectHandle machine,
    MachineRecipeId recipe, std::uint16_t quantity) {
    const auto outcome = QueryStart(actor, machine, recipe, quantity);
    if (outcome != MachineOutcome::Started) { return MachineResult{outcome}; }
    const auto& content = world_.GetContent();
    const auto& definition = content.GetMachineRecipe(recipe);
    MachineJobState job;
    job.recipeName = definition.id; job.outputName = content.GetItem(definition.result.item).id;
    if (definition.requiredSoftware.value) { job.softwareName = content.GetInformation(definition.requiredSoftware).id; }
    job.quantity = quantity; job.outputCount = definition.result.count * quantity;
    job.duration = definition.duration * quantity; job.remainingWork = job.duration;
    job.energyPerTick = definition.energyPerTick;
    job.totalSetupWork = definition.setupWork + definition.setupWorkPerUnit * quantity;
    job.initiator = world_.GetActor(actor)->id; job.worker = job.initiator;
    for (const auto& input : definition.inputs) {
        job.inputs.push_back({content.GetItem(input.Item()).id, std::uint32_t{input.Count()} * quantity});
    }
    for (const auto& skill : definition.skills) { job.skills.push_back({content.GetSkill(skill.skill).id, skill.level}); }
    if (definition.learning.skill.value) {
        job.learningSkillName = content.GetSkill(definition.learning.skill).id;
        job.learningLevel = definition.learning.level; job.learningXp = definition.learning.xpPerExecution * quantity;
    }
    for (auto input : selected_) {
        job.reservedInputs.push_back(world_.GetItem(input)->id);
        job.reservedVolumeMl += world_.GetItemVolume(input);
    }
    job.reservedVolumeMl = eastl::max(job.reservedVolumeMl,
        std::uint64_t{content.GetItem(definition.result.item).volumeMl} * job.outputCount);
    if (!job.totalSetupWork) {
        job.phase = MachineJobPhase::Processing; job.worker = 0; job.runRequested = true;
    }
    world_.BeginCraftingMutation();
    for (auto input : selected_) {
        const auto& location = world_.GetItem(input)->location;
        if (location.kind == LocationKind::object && location.object == machine) { continue; }
        Require(world_.TransferItem(input, ItemLocation::Stored(machine)).has_value());
    }
    Require(world_.SetMachineJob(machine, eastl::move(job)).has_value());
    Attach(machine);
    if (world_.GetMachineJob(machine)->phase == MachineJobPhase::Setup) { Require(FundSetup(machine)); }
    world_.MachineChanged(machine);
    world_.EndCraftingMutation();
    return MachineResult{MachineOutcome::Started};
}
bool MachineSystem::Busy(ActorHandle actor) const {
    const auto* worker = world_.GetActor(actor);
    if (!worker) { return false; }
    for (auto machine : machines_) {
        const auto* job = world_.GetMachineJob(machine);
        if (job && job->phase == MachineJobPhase::Setup && job->worker == worker->id) { return true; }
    }
    return false;
}
eastl::optional<Tick> MachineSystem::ActorEventIn(ActorHandle actor) const {
    if (!Available(world_, actor)) { return eastl::nullopt; }
    const auto id = world_.GetActor(actor)->id;
    for (auto machine : machines_) {
        const auto* job = world_.GetMachineJob(machine);
        if (job && job->phase == MachineJobPhase::Setup && job->worker == id) { return job->remainingSetupTicks; }
    }
    return eastl::nullopt;
}
bool MachineSystem::FundSetup(ObjectHandle machine) {
    auto* job = world_.MutableMachineJob(machine);
    const auto handle = world_.FindActor(job->worker);
    if (!Available(world_, handle) || !world_.CanUseObject(handle, machine) || !Skills(world_, handle, *job)) { return false; }
    const auto* object = world_.GetObject(machine);
    if (!object->enabled || object->broken) { return false; }
    auto* actor = world_.MutableActor(handle);
    const auto left = std::uint64_t{job->totalSetupWork} * ratioScale - job->setupWork;
    constexpr std::uint64_t numerator = std::uint64_t{ticks_per_turn} * ratioScale;
    const auto residue = std::uint64_t{job->timingRemainder} * actor->speed / job->timingSpeed;
    Tick duration = machineSetupSliceTicks;
    auto work = (duration * numerator + residue) / actor->speed;
    if (work >= left) {
        duration = eastl::max<Tick>(1, (left * actor->speed - residue + numerator - 1) / numerator); work = left;
    }
    if (actor->turns == UINT64_MAX || duration > UINT64_MAX - world_.GetTick()) { return false; }
    job->pendingWork = work; job->remainingSetupTicks = duration;
    job->timingRemainder = static_cast<std::uint32_t>((duration * numerator + residue) % actor->speed);
    job->timingSpeed = actor->speed;
    actor->actionDeadline.Set(world_.GetTick(), duration);
    Require(actor->turns != UINT64_MAX); ++actor->turns;
    world_.ActorChanged(handle);
    return true;
}
void MachineSystem::Elapse(Tick elapsed) {
    for (auto machine : machines_) {
        auto* job = world_.MutableMachineJob(machine);
        if (!job || job->phase != MachineJobPhase::Setup || !job->worker) { continue; }
        const auto actor = world_.FindActor(job->worker);
        if (!Available(world_, actor) || !world_.CanUseObject(actor, machine)) {
            job->worker = 0; job->pauseRequested = false; world_.MachineChanged(machine); continue;
        }
        job->remainingSetupTicks -= eastl::min(elapsed, job->remainingSetupTicks);
        world_.MutableActor(actor)->actionDeadline.Set(world_.GetTick(), CheckedTickAdd(elapsed, job->remainingSetupTicks));
        world_.ActorChanged(actor); world_.MachineChanged(machine);
    }
}
bool MachineSystem::ProcessEvents() {
    due_.clear();
    bool changed = false;
    for (auto machine : machines_) {
        auto* job = world_.MutableMachineJob(machine);
        if (!job || job->phase != MachineJobPhase::Setup || !job->worker) { continue; }
        const auto actor = world_.FindActor(job->worker);
        if (!Available(world_, actor) || !world_.CanUseObject(actor, machine)) {
            job->worker = 0; job->pauseRequested = false;
            world_.MachineChanged(machine); changed = true; continue;
        }
        if (!job->remainingSetupTicks) { due_.push_back(machine); }
    }
    eastl::sort(due_.begin(), due_.end(), [this](auto a, auto b) { return world_.GetObject(a)->id < world_.GetObject(b)->id; });
    for (auto machine : due_) {
        auto* job = world_.MutableMachineJob(machine);
        if (!job || job->phase != MachineJobPhase::Setup || !job->worker) { continue; }
        const auto actor = world_.FindActor(job->worker);
        if (!Available(world_, actor) || !world_.CanUseObject(actor, machine)) {
            job->worker = 0; job->pauseRequested = false; world_.MachineChanged(machine); continue;
        }
        job->setupWork += job->pendingWork; job->pendingWork = 0;
        if (job->setupWork == std::uint64_t{job->totalSetupWork} * ratioScale) {
            job->phase = MachineJobPhase::Processing; job->worker = 0;
            job->pauseRequested = false; job->runRequested = true;
        } else if (job->pauseRequested || !FundSetup(machine)) { job->worker = 0; job->pauseRequested = false; }
        world_.MachineChanged(machine);
    }
    return changed || !due_.empty();
}
WorldResult<MachineResult> MachineSystem::Use(ActorHandle actor, ObjectHandle machine, MachineAction action) {
    if (!Available(world_, actor)) { return MachineResult{MachineOutcome::ActorUnavailable}; }
    const auto* object = world_.GetObject(machine);
    const auto* definition = object ? world_.GetContent().TryGetObject(object->type) : nullptr;
    if (!definition || definition->behavior != ObjectBehaviorKind::SelectableMachine) {
        return MachineResult{MachineOutcome::Unavailable};
    }
    if (!world_.CanUseObject(actor, machine)) { return MachineResult{MachineOutcome::OutOfReach}; }
    auto* job = world_.MutableMachineJob(machine);
    if (action == MachineAction::Stop && job && job->phase == MachineJobPhase::Setup
        && job->worker == world_.GetActor(actor)->id) {
        job->pauseRequested = true; world_.MachineChanged(machine);
        return MachineResult{MachineOutcome::PauseRequested};
    }
    const bool paidResume = action == MachineAction::Resume && job && job->phase == MachineJobPhase::Setup
        && !job->worker && (job->remainingSetupTicks || job->pendingWork)
        && !world_.Learning().Busy(actor) && !world_.DigitalBusy(actor) && !world_.CraftingBusy(actor)
        && world_.GetActor(actor)->combat.action.kind == ActorActionKind::None
        && world_.GetActor(actor)->actionDeadline.Remaining(world_.GetTick()) <= job->remainingSetupTicks;
    if ((Occupied(world_, actor) && !paidResume) || world_.CraftingBusy(actor)) {
        return MachineResult{MachineOutcome::ActorBusy};
    }
    if (action == MachineAction::Collect) {
        bool contents = false;
        for (auto item : object->contents) { contents |= !world_.IsReservedItem(item); }
        if (!contents) { return MachineResult{MachineOutcome::NoContents}; }
        if (!ActionHeadroom(world_, actor)) { return MachineResult{MachineOutcome::Unavailable}; }
        const auto result = world_.CollectMachineContents(actor, machine);
        if (!result) { return MachineResult{MachineOutcome::NoCapacity}; }
        return MachineResult{MachineOutcome::Collected, 100};
    }
    if (!job) { return MachineResult{MachineOutcome::NoJob}; }
    if (action == MachineAction::Cancel) {
        if (!ActionHeadroom(world_, actor)) { return MachineResult{MachineOutcome::Unavailable}; }
        world_.ClearMachineJob(machine); world_.MachineChanged(machine);
        return MachineResult{MachineOutcome::Cancelled, 100};
    }
    if (action == MachineAction::Stop) {
        if (job->phase == MachineJobPhase::Setup) {
            if (job->worker) { job->pauseRequested = true; }
            world_.MachineChanged(machine);
            return MachineResult{job->worker ? MachineOutcome::PauseRequested : MachineOutcome::Stopped};
        }
        if (!ActionHeadroom(world_, actor)) { return MachineResult{MachineOutcome::Unavailable}; }
        job->runRequested = false; world_.MachineChanged(machine);
        return MachineResult{MachineOutcome::Stopped, 100};
    }
    if (action != MachineAction::Resume) { return eastl::unexpected(WorldError::invalid_request); }
    if (!object->enabled || object->broken) { return MachineResult{MachineOutcome::Unavailable}; }
    if (job->phase == MachineJobPhase::Processing) {
        if (job->runRequested) { return MachineResult{MachineOutcome::Busy}; }
        if (!ActionHeadroom(world_, actor)) { return MachineResult{MachineOutcome::Unavailable}; }
        job->runRequested = true; world_.MachineChanged(machine);
        return MachineResult{MachineOutcome::Resumed, 100};
    }
    if (job->worker) { return MachineResult{MachineOutcome::Busy}; }
    if (!Skills(world_, actor, *job)) { return MachineResult{MachineOutcome::MissingSkills}; }
    if (job->remainingSetupTicks > UINT64_MAX - world_.GetTick()) { return MachineResult{MachineOutcome::Unavailable}; }
    job->worker = world_.GetActor(actor)->id; job->pauseRequested = false;
    if (job->remainingSetupTicks || job->pendingWork) {
        world_.MutableActor(actor)->actionDeadline.Set(world_.GetTick(), job->remainingSetupTicks);
        world_.ActorChanged(actor);
    } else if (!FundSetup(machine)) {
        job->worker = 0; return MachineResult{MachineOutcome::Unavailable};
    }
    world_.MachineChanged(machine);
    return MachineResult{MachineOutcome::Resumed};
}
void MachineSystem::Collect(ActorHandle actor, Vector<MachineView>& output) const {
    output.clear();
    if (!Available(world_, actor)) { return; }
    Gather(actor, {});
    for (auto machine : machines_) {
        if (!world_.CanUseObject(actor, machine)) { continue; }
        const auto* object = world_.GetObject(machine);
        if (!object) { continue; }
        MachineView view; view.object = object->id;
        if (const auto* job = world_.GetMachineJob(machine)) {
            view.hasJob = true; view.recipe = world_.GetContent().FindMachineRecipe(job->recipeName);
            view.quantity = static_cast<std::uint16_t>(job->quantity); view.setup = job->phase == MachineJobPhase::Setup;
            view.running = view.setup ? job->worker != 0 : job->runRequested;
            view.pauseRequested = job->pauseRequested; view.completionPending = job->waitingError.has_value();
            view.initiator = job->initiator; view.worker = job->worker;
            view.setupWork = std::uint64_t{job->totalSetupWork} * ratioScale; view.setupCompleted = job->setupWork;
            view.remainingTicks = view.setup ? job->remainingSetupTicks : job->remainingWork;
            view.fundedTicks = job->fundedTicks; view.reservedVolumeMl = job->reservedVolumeMl;
            view.learningXp = std::uint64_t{job->learningXp} * (job->learningLevel >= 5 ? 8000u : ratioScale);
        }
        for (auto recipe : world_.GetContent().GetObject(object->type).selectableRecipes) {
            MachineRecipeView entry; entry.recipe = recipe; entry.availability = Select(actor, machine, recipe, 1);
            if (entry.availability == MachineOutcome::Started) {
                // Monotone material/count/capacity budgets; gather once, then at
                // most eight selection passes rather than 255 world traversals.
                unsigned low = 1, high = 255;
                while (low < high) {
                    const auto middle = (low + high + 1) / 2;
                    if (Select(actor, machine, recipe, static_cast<std::uint16_t>(middle)) == MachineOutcome::Started) { low = middle; }
                    else { high = middle - 1; }
                }
                entry.maximumQuantity = static_cast<std::uint16_t>(low);
            }
            view.recipes.push_back(entry);
        }
        output.push_back(eastl::move(view));
    }
    eastl::sort(output.begin(), output.end(), [](const auto& a, const auto& b) { return a.object < b.object; });
}
}
