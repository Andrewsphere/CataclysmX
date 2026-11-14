#include "crafting/crafting_state.h"
#include "content/content.h"
#include "core/ratio.h"

namespace cataclysmx {
bool ValidCraftingSettings(CraftingSettings settings) {
    return settings.nearbyRadius <= 8 && settings.maxSliceTicks > 0 && settings.maxSliceTicks <= 1000;
}
DataObject WriteCraftingState(const CraftingJobState& s) {
    DataObject out;
    out.SetUnsigned("version", 1);
    out.SetUnsigned("kind", static_cast<unsigned>(s.kind));
    out.SetString("recipe", s.recipeName);
    out.SetUnsigned("status", static_cast<unsigned>(s.status));
    out.SetUnsigned("radius", s.settings.nearbyRadius);
    out.SetUnsigned("slice", s.settings.maxSliceTicks);
    out.SetUnsigned("worker", s.worker);
    out.SetUnsigned("target", s.recyclingTarget);
    out.SetUnsigned("total", s.totalWork);
    out.SetUnsigned("work", s.work);
    out.SetUnsigned("pending", s.pendingWork);
    out.SetUnsigned("remaining", s.remaining);
    out.SetUnsigned("speed", s.timingSpeed);
    out.SetUnsigned("timing_remainder", s.timingRemainder);
    out.SetBoolean("pause", s.pauseRequested);
    out.SetUnsigned("energy_remainder", s.energyRemainder);
    return out;
}
StateResult<CraftingJobState> ReadCraftingState(const DataObject& in) {
    const auto invalid = [] { return eastl::unexpected(StateError::InvalidData); };
    if (in.Type() != DataType::Object || in.Size() != 16) { return invalid(); }
    const auto version = in.GetUnsigned("version");
    if (!version) { return invalid(); }
    if (*version != 1) { return eastl::unexpected(StateError::UnsupportedVersion); }
    const auto kind = in.GetUnsigned("kind"), status = in.GetUnsigned("status");
    const auto radius = in.GetUnsigned("radius"), slice = in.GetUnsigned("slice");
    const auto worker = in.GetUnsigned("worker"), total = in.GetUnsigned("total");
    const auto target = in.GetUnsigned("target");
    const auto work = in.GetUnsigned("work"), pending = in.GetUnsigned("pending");
    const auto remaining = in.GetUnsigned("remaining"), speed = in.GetUnsigned("speed");
    const auto remainder = in.GetUnsigned("timing_remainder");
    const auto recipe = in.GetString("recipe");
    const auto pause = in.GetBoolean("pause");
    const auto energy = in.GetUnsigned("energy_remainder");
    if (!kind || *kind > 1 || !status || *status > 5 || !radius || *radius > 8
        || !slice || *slice == 0 || *slice > 1000 || !worker || !total || !*total || *total > UINT32_MAX
        || !work || !pending || !remaining || *remaining > 1000 || !speed || !*speed || *speed > UINT32_MAX
        || !remainder || *remainder >= *speed || !recipe || recipe->empty() || !pause
        || !energy || *energy >= craftingEnergyDenominator || !target
        || ((*kind == static_cast<unsigned>(CraftingKind::Recycle)) != (*target != 0))) { return invalid(); }
    const auto maximum = *total * ratioScale;
    if (*work >= maximum || *pending > maximum - *work) { return invalid(); }
    const auto jobStatus = static_cast<CraftingStatus>(*status);
    const bool assigned = jobStatus == CraftingStatus::Working || jobStatus == CraftingStatus::PauseRequested;
    if (assigned != (*worker != 0) || *pause != (jobStatus == CraftingStatus::PauseRequested)
        || (!assigned && jobStatus != CraftingStatus::WorkerUnavailable && (*remaining || *pending))) { return invalid(); }
    CraftingJobState s;
    s.kind = static_cast<CraftingKind>(*kind); s.status = static_cast<CraftingStatus>(*status);
    s.recipeName = *recipe; s.settings = {static_cast<std::uint8_t>(*radius), *slice};
    s.worker = *worker; s.totalWork = static_cast<std::uint32_t>(*total);
    s.recyclingTarget = *target;
    s.work = *work; s.pendingWork = *pending; s.remaining = *remaining;
    s.timingSpeed = static_cast<std::uint32_t>(*speed); s.timingRemainder = static_cast<std::uint32_t>(*remainder);
    s.pauseRequested = *pause;
    s.energyRemainder = *energy;
    return s;
}
StateResult<void> ResolveCraftingState(const ContentCatalog& content, CraftingJobState& s) {
    std::uint32_t work = 0;
    if (s.kind == CraftingKind::Craft) {
        s.craftingRecipe = content.FindCraftingRecipe(s.recipeName);
        const auto* recipe = content.TryGetCraftingRecipe(s.craftingRecipe);
        if (!recipe) { return eastl::unexpected(StateError::InvalidData); }
        work = recipe->work;
    } else {
        s.recyclingRecipe = content.FindRecyclingRecipe(s.recipeName);
        const auto* recipe = content.TryGetRecyclingRecipe(s.recyclingRecipe);
        if (!recipe) { return eastl::unexpected(StateError::InvalidData); }
        work = recipe->work;
    }
    if (!work || work != s.totalWork || s.work >= std::uint64_t{work} * ratioScale
        || (s.status == CraftingStatus::Working && !s.worker)) {
        return eastl::unexpected(StateError::InvalidData);
    }
    return {};
}
}
