#pragma once

#include "crafting/crafting_types.h"
#include "core/data_object.h"
#include "core/save_result.h"

namespace cataclysmx {
class ContentCatalog;
// Work is stored in 1/10000 base-work units. Energy debt belongs to the job,
// not requirement slots or tool identities, so reassignment cannot erase it.
inline constexpr std::uint64_t craftingEnergyDenominator = 100000000;
struct CraftingJobState {
    CraftingKind kind = CraftingKind::Craft;
    String recipeName;
    CraftingRecipeId craftingRecipe{};
    RecyclingRecipeId recyclingRecipe{};
    CraftingStatus status = CraftingStatus::Paused;
    CraftingSettings settings{};
    PersistentId worker = 0;
    // Actual reserved original, never inferred from child order or type alone.
    PersistentId recyclingTarget = 0;
    std::uint32_t totalWork = 0;
    std::uint64_t work = 0;
    std::uint64_t pendingWork = 0;
    Tick remaining = 0;
    std::uint32_t timingSpeed = 100;
    std::uint32_t timingRemainder = 0;
    // Strictly less than craftingEnergyDenominator, including persisted states.
    std::uint64_t energyRemainder = 0;
    bool pauseRequested = false;
};
bool ValidCraftingSettings(CraftingSettings settings);
DataObject WriteCraftingState(const CraftingJobState& state);
StateResult<CraftingJobState> ReadCraftingState(const DataObject& input);
StateResult<void> ResolveCraftingState(const ContentCatalog& content, CraftingJobState& state);
}
