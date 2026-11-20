#pragma once

#include "content/digital_definition.h"
#include "core/data_object.h"
#include "core/save_result.h"
#include "core/state_result.h"
#include "core/world_types.h"
#include <EASTL/optional.h>

namespace cataclysmx {
class ContentCatalog;
enum class MachineJobPhase : std::uint8_t { Setup, Processing };
inline constexpr Tick machineCompletionRetryTicks = 100;
inline constexpr Tick machineSetupSliceTicks = 100;
struct CapturedMachineInput {
    String itemName;
    std::uint32_t count = 0; // Expanded batch count, 1..255.
};
struct CapturedMachineSkill {
    String skillName;
    std::uint8_t level = 0;
};
struct MachineJobState {
    String recipeName;
    String outputName;
    String softwareName; // Empty when no software was required.
    std::uint32_t quantity = 1;
    SmallVector<CapturedMachineInput, 1> inputs;
    Vector<PersistentId> reservedInputs; // Strictly increasing original item IDs.
    // Captured max(actual original input volume, complete output volume). World
    // rejects restore when either current volume exceeds this durable claim.
    std::uint64_t reservedVolumeMl = 0;
    std::uint32_t outputCount = 0;
    Tick duration = 0; // Captured expanded processing budget.
    std::uint32_t energyPerTick = 0;
    std::uint32_t totalSetupWork = 0;
    PersistentId initiator = 0;
    PersistentId worker = 0;
    SmallVector<CapturedMachineSkill, 1> skills;
    String learningSkillName;
    std::uint8_t learningLevel = 0;
    std::uint32_t learningXp = 0; // Captured expanded nominal XP.
    MachineJobPhase phase = MachineJobPhase::Setup;
    std::uint64_t setupWork = 0; // Completed work in ratioScale units.
    // Already paid actor slice, same units. Can remain due at zero ticks or be
    // retained with worker=0; a fractional slice may have ticks but zero work.
    std::uint64_t pendingWork = 0;
    Tick remainingSetupTicks = 0;
    std::uint32_t timingSpeed = 100;
    std::uint32_t timingRemainder = 0;
    bool pauseRequested = false;
    Tick remainingWork = 0;
    Tick fundedTicks = 0;
    bool runRequested = false;
    Tick completionRetryTicks = 0;
    eastl::optional<SaveError> waitingError;
};

// Structural checks need no catalog. Captured budgets remain authoritative even
// when current recipe tuning changes; World validates actual reserved ownership.
bool ValidMachineJobState(const MachineJobState& state);
DataObject WriteMachineJobState(const MachineJobState& state);
StateResult<MachineJobState> ReadMachineJobState(const DataObject& input,
    Diagnostics* diagnostics = nullptr);
StateResult<void> ValidateCapturedMachineJob(const ContentCatalog& content,
    const MachineJobState& state);
}
