#pragma once

#include "core/item_state.h"
#include "items/item_behavior.h"

namespace cataclysmx {

struct Item;

class ItemSystem {
public:
    explicit ItemSystem(World& world);
    ~ItemSystem();
    StateResult<void> Initialize(Item& item, const DataObject* archive, Diagnostics* diagnostics = nullptr);
    DataObject Capture(ItemHandle item) const;
    Vector<ItemActionInfo> QueryActions(ActorHandle actor, ItemHandle item) const;
    ItemUseResult Use(ActorHandle actor, ItemHandle item, const ItemUseRequest& request);
    ItemPresentation Describe(ActorHandle actor, ItemHandle item) const;
    eastl::optional<ItemEnergyView> Energy(ItemHandle item, ItemCapabilityId store) const;
    WorldResult<void> SetEnergy(ItemEnergyRef store, std::uint32_t energy);
    eastl::optional<Tick> NextEventIn() const;
    void Elapse(Tick elapsed);
    void ProcessEvents();
    std::size_t ScheduledItemCount() const;
    void Attach(ItemHandle item);
    void Detach(ItemHandle item);
    void Notify(const ItemChange& change);
    bool CheckInvariants() const;
private:
    friend class World;
    friend class ItemReadContext;
    friend class ItemMutationContext;
    struct ScheduledBehavior { ItemHandle item; ItemCapabilityId capability; };
    World& world_;
    Vector<ScheduledBehavior> scheduled_;
    Vector<ItemChange> changes_;
    Vector<ItemHandle> destruction_;
    Vector<ItemHandle> refresh_;
    unsigned callbackDepth_ = 0;
    bool draining_ = false;
    ItemBehavior* Behavior(ItemHandle item, ItemCapabilityId capability);
    const ItemBehavior* Behavior(ItemHandle item, ItemCapabilityId capability) const;
    bool Eligible(ItemHandle item) const;
    bool PendingDestruction(ItemHandle item) const;
    void Refresh(ItemHandle item);
    void Drain();
    void BeginCallback();
    void EndCallback();
    WorldResult<void> TransferEnergy(ItemEnergyRef source, ItemEnergyRef target, std::uint32_t amount);
    void RequestDestroy(ItemHandle item);
};

}
