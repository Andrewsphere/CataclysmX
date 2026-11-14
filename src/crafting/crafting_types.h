#pragma once

#include "content/crafting_definition.h"
#include "core/timing.h"
#include "core/world_types.h"

namespace cataclysmx {
enum class CraftingKind : std::uint8_t { Craft, Recycle };
enum class CraftingStatus : std::uint8_t {
    Paused, Working, PauseRequested, MissingTools, InsufficientEnergy, WorkerUnavailable
};
enum class CraftingOutcome : std::uint8_t {
    Started, Resumed, PauseRequested, Paused, Cancelled, ActorUnavailable,
    ActorBusy, JobBusy, OutOfReach, RecipeDisabled, InvalidTarget,
    MissingInputs, MissingTools, InsufficientEnergy, CapacityExceeded
};
struct CraftingSettings {
    std::uint8_t nearbyRadius = 1;
    Tick maxSliceTicks = 100;
};
struct CraftingResult {
    CraftingOutcome outcome;
    ItemHandle unfinished{};
};
struct CraftingJobView {
    PersistentId id = 0;
    ItemHandle unfinished{};
    CraftingKind kind = CraftingKind::Craft;
    CraftingRecipeId craftingRecipe{};
    RecyclingRecipeId recyclingRecipe{};
    CraftingStatus status = CraftingStatus::Paused;
    PersistentId worker = 0;
    std::uint32_t totalWork = 0;
    std::uint32_t completedWork = 0;
    Tick fundedTicksRemaining = 0;
    bool pauseRequested = false;
};
}
