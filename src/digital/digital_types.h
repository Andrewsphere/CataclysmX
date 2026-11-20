#pragma once

#include "content/digital_definition.h"
#include "core/world_types.h"
#include "core/timing.h"

namespace cataclysmx {

enum class MachineAction : std::uint8_t { Stop, Resume, Cancel, Collect };
enum class MachineOutcome : std::uint8_t {
    Started, Resumed, PauseRequested, Stopped, Cancelled, Collected,
    ActorUnavailable, ActorBusy, OutOfReach, Unavailable, Busy, NoJob,
    InvalidRecipe, InvalidQuantity, MissingSkills, MissingDesign, MissingSoftware,
    MissingInputs, NoCapacity, NoContents, CompletionPending
};
struct MachineResult {
    MachineOutcome outcome = MachineOutcome::Unavailable;
    std::uint32_t baseCost = 0; // Setup schedules its own slices; other actions return their cost.
};
struct MachineRecipeView {
    MachineRecipeId recipe{};
    MachineOutcome availability = MachineOutcome::Unavailable;
    std::uint16_t maximumQuantity = 0;
};
struct MachineView {
    PersistentId object = 0;
    MachineRecipeId recipe{};
    std::uint16_t quantity = 0;
    bool hasJob = false;
    bool setup = false;
    bool running = false;
    bool pauseRequested = false;
    bool completionPending = false;
    PersistentId initiator = 0;
    PersistentId worker = 0;
    std::uint64_t setupWork = 0;
    std::uint64_t setupCompleted = 0;
    Tick remainingTicks = 0;
    Tick fundedTicks = 0;
    std::uint64_t reservedVolumeMl = 0;
    std::uint64_t learningXp = 0; // Fixed-point nominal XP, before recipient boosts/caps.
    Vector<MachineRecipeView> recipes;
};

struct InformationEndpoint {
    PersistentId item = 0;
    PersistentId object = 0; // Exactly one nonzero field.
    bool operator==(const InformationEndpoint&) const = default;
};
enum class InformationOutcome : std::uint8_t {
    Started, Copied, Removed, Cancelled, AlreadyPresent,
    ActorUnavailable, ActorBusy, OutOfReach, InvalidDevice, MissingHost,
    NoPower, MissingInformation, Busy
};
struct InformationResult {
    InformationOutcome outcome = InformationOutcome::InvalidDevice;
    std::uint32_t baseCost = 0;
};
struct InformationDeviceView {
    InformationEndpoint endpoint;
    ItemTypeId type{};
    bool powered = false;
    bool writable = false;
    bool reader = false;
    bool transferring = false;
    Tick remainingTicks = 0;
    Vector<InformationId> packages;
};

const char* MachineOutcomeText(MachineOutcome outcome);
const char* InformationOutcomeText(InformationOutcome outcome);

}
