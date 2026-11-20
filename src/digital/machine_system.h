#pragma once

#include "digital/digital_types.h"
#include "core/world_result.h"
#include <EASTL/optional.h>

namespace cataclysmx {
class World;
struct MachineJobState;

class MachineSystem {
public:
    explicit MachineSystem(World& world);
    WorldResult<MachineResult> Start(ActorHandle actor, ObjectHandle machine,
        MachineRecipeId recipe, std::uint16_t quantity);
    WorldResult<MachineResult> Use(ActorHandle actor, ObjectHandle machine, MachineAction action);
    MachineOutcome QueryStart(ActorHandle actor, ObjectHandle machine,
        MachineRecipeId recipe, std::uint16_t quantity) const;
    void Collect(ActorHandle actor, Vector<MachineView>& output) const;
    bool Busy(ActorHandle actor) const;
    eastl::optional<Tick> ActorEventIn(ActorHandle actor) const;
    void Elapse(Tick elapsed);
    bool ProcessEvents();
    void Attach(ObjectHandle machine);
    void Detach(ObjectHandle machine);
private:
    World& world_;
    Vector<ObjectHandle> machines_;
    Vector<ObjectHandle> due_;
    mutable Vector<ItemHandle> candidates_;
    mutable Vector<ItemHandle> selected_;
    void Gather(ActorHandle actor, ObjectHandle machine) const;
    MachineOutcome Select(ActorHandle actor, ObjectHandle machine,
        MachineRecipeId recipe, std::uint16_t quantity) const;
    bool FundSetup(ObjectHandle machine);
};
}
