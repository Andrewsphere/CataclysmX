#include "crafting/crafting_state.h"
#include "content/content.h"
#include "core/ratio.h"

namespace cataclysmx {
bool ValidCraftingSettings(CraftingSettings settings) {
    return settings.nearbyRadius <= 8 && settings.maxSliceTicks > 0 && settings.maxSliceTicks <= 1000;
}
DataObject WriteCraftingState(const CraftingJobState& s) {
    DataObject out;
    out.SetUnsigned("version", 4);
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
    out.SetUnsigned("heat_remainder", s.heatRemainder);
    auto contributions = DataObject::List();
    for (const auto& contribution : s.contributions) {
        DataObject entry;
        entry.SetUnsigned("worker", contribution.worker);
        entry.SetUnsigned("work", contribution.work);
        contributions.Append(eastl::move(entry));
    }
    out.Set("contributions", eastl::move(contributions));
    auto stations = DataObject::List();
    for (auto station : s.stations) { stations.Append(DataObject::Unsigned(station)); }
    out.Set("stations", eastl::move(stations));
    return out;
}
StateResult<CraftingJobState> ReadCraftingState(const DataObject& in) {
    const auto invalid = [] { return eastl::unexpected(StateError::InvalidData); };
    if (in.Type() != DataType::Object) { return invalid(); }
    const auto version = in.GetUnsigned("version");
    if (!version) { return invalid(); }
    if (*version < 1 || *version > 4) { return eastl::unexpected(StateError::UnsupportedVersion); }
    if (in.Size() != 15 + *version) { return invalid(); }
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
    if (!kind || *kind > 1 || !status || *status > (*version == 1 ? 5u : *version == 2 ? 7u : *version == 3 ? 8u
            : static_cast<unsigned>(CraftingStatus::InsufficientFuel)) || !radius || *radius > 8
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
    if (*version >= 4) {
        const auto heat = in.GetUnsigned("heat_remainder");
        if (!heat || *heat >= craftingHeatDenominator) { return invalid(); }
        s.heatRemainder = *heat;
    }
    if (*version >= 2) {
        const auto entries = in.Get("contributions", DataType::List);
        if (!entries) { return invalid(); }
        PersistentId previous = 0;
        std::uint64_t attributed = 0;
        for (std::size_t i = 0; i < (*entries)->Size(); ++i) {
            const auto& entry = (*entries)->At(i);
            const auto contributor = entry.GetUnsigned("worker"), completed = entry.GetUnsigned("work");
            if (entry.Type() != DataType::Object || entry.Size() != 2 || !contributor || *contributor <= previous
                || !completed || !*completed || *completed > s.work - attributed) { return invalid(); }
            s.contributions.push_back({*contributor, *completed});
            previous = *contributor; attributed += *completed;
        }
    }
    if (*version >= 3) {
        const auto entries = in.Get("stations", DataType::List);
        if (!entries || (*entries)->Size() > maxRecipeTraits || (!assigned && (*entries)->Size())) { return invalid(); }
        PersistentId previous = 0;
        for (std::size_t i = 0; i < (*entries)->Size(); ++i) {
            const auto& entry = (*entries)->At(i);
            if (entry.Type() != DataType::Unsigned) { return invalid(); }
            const auto station = entry.UnsignedValue();
            if (station <= previous || !s.stations.PushBack(station)) { return invalid(); }
            previous = station;
        }
    }
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
