#pragma once

#include "crafting/crafting_state.h"
#include "core/world_result.h"
#include <EASTL/array.h>
#include <EASTL/optional.h>

namespace cataclysmx {
class World;
class CraftingSystem {
public:
    explicit CraftingSystem(World& world);
    CraftingSettings Settings() const;
    WorldResult<void> SetSettings(CraftingSettings settings);
    WorldResult<CraftingResult> Start(ActorHandle actor, CraftingRecipeId recipe);
    WorldResult<CraftingResult> Recycle(ActorHandle actor, ItemHandle target, RecyclingRecipeId recipe);
    WorldResult<CraftingResult> Resume(ActorHandle actor, ItemHandle item);
    WorldResult<CraftingResult> Pause(ActorHandle actor, ItemHandle item);
    WorldResult<CraftingResult> Cancel(ActorHandle actor, ItemHandle item);
    eastl::optional<CraftingJobView> Inspect(ItemHandle item) const;
    void Collect(ActorHandle actor, Vector<CraftingJobView>& output) const;
    void Attach(ItemHandle item);
    void Detach(ItemHandle item);
    void Elapse(Tick elapsed);
    bool ProcessEvents();
    bool Busy(ActorHandle actor) const;
    eastl::optional<Tick> ActorEventIn(ActorHandle actor) const;
private:
    World& world_;
    CraftingSettings settings_{};
    Vector<ItemHandle> assigned_;
    Vector<ItemHandle> due_;
    Vector<ItemHandle> candidates_;
    Vector<ItemHandle> consumed_;
    Vector<ItemHandle> retained_;
    Vector<ItemHandle> traversal_;
    struct ToolCharge {
        ItemHandle item{};
        ItemCapabilityId power{};
        std::uint32_t energyAfter = 0;
    };
    // Reused funding scratch, bounded by 8*255 retained tools + 4 trait tools.
    Vector<ToolCharge> charges_;
    CraftingJobState* State(ItemHandle item);
    const CraftingJobState* State(ItemHandle item) const;
    bool Reachable(ActorHandle actor, ItemHandle item, unsigned radius) const;
    void Gather(ActorHandle actor, unsigned radius);
    CraftingOutcome Select(const CraftingJobState& state, bool inputs, ItemHandle target,
        eastl::array<ItemHandle, 4>& tools);
    CraftingOutcome Fund(ActorHandle actor, CraftingJobState& state,
        const eastl::array<ItemHandle, 4>& tools, bool commit);
    WorldResult<CraftingResult> Begin(ActorHandle actor, CraftingJobState state, ItemHandle target);
    CraftingOutcome ActorAvailability(ActorHandle actor) const;
    void Finish(ItemHandle item);
    void ReleaseWorker(CraftingJobState& state);
};
}
