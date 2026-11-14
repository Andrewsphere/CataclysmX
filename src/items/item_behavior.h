#pragma once

#include "content/item_definition.h"
#include "content/content_result.h"
#include "core/world_result.h"
#include <EASTL/unique_ptr.h>

namespace cataclysmx {

class World;
class ItemSystem;
class ItemSnapshotContext;
struct ItemLocation;
struct Tile;

enum class ItemChangeKind { Moved, ContentsChanged, EnergyChanged };
struct ItemChange {
    ItemChangeKind kind = ItemChangeKind::ContentsChanged;
    ItemHandle item{};
    // Energy store or affected installation mount; zero for ordinary contents/movement.
    ItemCapabilityId capability{};
};

class ItemReadContext {
public:
    ItemHandle Self() const;
    const World& GetWorld() const;
    eastl::optional<ItemEnergyView> Energy(ItemHandle item, ItemCapabilityId store) const;
    ItemHandle Installed(ItemHandle parent, ItemCapabilityId mount) const;
    WorldResult<void> CheckTransfer(ItemHandle item, ItemLocation destination) const;
private:
    friend class ItemSystem;
    friend class ItemMutationContext;
    ItemReadContext(const ItemSystem& system, ItemHandle self);
    const ItemSystem& system_;
    ItemHandle self_;
};

// Callback-scoped access: mutations use validated services; destruction is deferred.
class ItemMutationContext {
public:
    const ItemReadContext& Read() const;
    WorldResult<void> TransferEnergy(ItemEnergyRef source, ItemEnergyRef target, std::uint32_t amount);
    WorldResult<void> SetEnergy(ItemEnergyRef store, std::uint32_t energy);
    WorldResult<void> TransferItem(ItemHandle item, ItemLocation destination);
    WorldResult<void> SetTile(TileCoord position, Tile tile);
    void RequestDestroy(ItemHandle item);
private:
    friend class ItemSystem;
    ItemMutationContext(ItemSystem& system, ItemHandle self);
    ItemSystem& system_;
    ItemReadContext read_;
};

class ItemBehavior {
public:
    virtual ~ItemBehavior();
    ItemCapabilityId Capability() const;
    virtual void QueryActions(const ItemReadContext& context, ActorHandle actor,
        ItemActionList& output) const = 0;
    virtual ItemUseResult Use(ItemMutationContext& context, ActorHandle actor,
        const ItemUseRequest& request) = 0;
    virtual eastl::optional<Tick> NextEventIn(const ItemReadContext& context) const;
    virtual void Elapse(ItemMutationContext& context, Tick elapsed);
    virtual void ProcessEvent(ItemMutationContext& context);
    virtual void OnChange(ItemMutationContext& context, const ItemChange& change);
    virtual DataObject SaveState() const = 0;
    virtual ItemBehaviorView Describe(const ItemReadContext& context) const = 0;
protected:
    explicit ItemBehavior(ItemCapabilityId capability);
private:
    ItemCapabilityId capability_;
};

class ItemBehaviorDefinition {
public:
    virtual ~ItemBehaviorDefinition();
    const ItemBehaviorSchema& Schema() const;
    const ItemActionDefinition* FindAction(eastl::string_view id) const;
    virtual ContentResult<void> ValidateComposition(const ItemDefinition& item,
        ItemCapabilityId self, Diagnostics* diagnostics) const;
    virtual StateResult<eastl::unique_ptr<ItemBehavior>> Create(ItemCapabilityId self,
        const DataObject* archive, Diagnostics* diagnostics) const = 0;
    virtual bool AcceptsInstallation(const ItemDefinition& child) const;
    virtual StateResult<void> ValidateSnapshot(const ItemSnapshotContext& context,
        PersistentId self, ItemCapabilityId capability, Diagnostics* diagnostics) const = 0;
protected:
    explicit ItemBehaviorDefinition(ItemBehaviorSchema schema);
private:
    ItemBehaviorSchema schema_;
};

}
