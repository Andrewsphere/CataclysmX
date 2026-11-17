#pragma once

#include "skills/learning_types.h"
#include "core/world_result.h"
#include "core/world_types.h"
#include <EASTL/optional.h>

namespace cataclysmx {
class World;
class LearningSystem {
public:
    explicit LearningSystem(World& world);
    WorldResult<StudyResult> Study(ActorHandle actor, BookId book);
    WorldResult<StudyResult> PauseStudy(ActorHandle actor);
    bool Busy(ActorHandle actor) const;
    eastl::optional<Tick> ActorEventIn(ActorHandle actor) const;
    void Elapse(Tick elapsed);
    bool ProcessEvents();
    bool HasBookReference(ActorHandle actor, BookId book) const;
    bool HasRecipeReference(ActorHandle actor, CraftingRecipeId recipe) const;
    bool HasRecipeReference(ActorHandle actor, CraftingRecipeId recipe, unsigned radius) const;
    void CollectView(ActorHandle actor, LearningView& output) const;
    WorldResult<SkillAward> Train(ActorHandle actor, const SkillTraining& training,
        SkillActivity activity, std::uint64_t fixedWork);
    // Explicit setup/admin operation; resets current-level accounting.
    WorldResult<void> SetLevel(ActorHandle actor, SkillId skill, std::uint8_t level);
private:
    World& world_;
    Vector<ActorHandle> actors_;
    Vector<ActorHandle> due_;
    mutable Vector<ItemHandle> references_;
    void GatherReferences(ActorHandle actor, unsigned radius) const;
    bool CraftingBusy(ActorHandle actor) const;
    bool FundStudy(ActorHandle actor);
};
}
