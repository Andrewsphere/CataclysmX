#include "items/item_behavior.h"
#include "items/item_system.h"
#include "core/world.h"

namespace cataclysmx {
ItemReadContext::ItemReadContext(const ItemSystem& system, ItemHandle self) : system_(system), self_(self) {}
ItemHandle ItemReadContext::Self() const { return self_; }
const World& ItemReadContext::GetWorld() const { return system_.world_; }
eastl::optional<ItemEnergyView> ItemReadContext::Energy(ItemHandle item, ItemCapabilityId store) const {
    return system_.Energy(item, store);
}
ItemHandle ItemReadContext::Installed(ItemHandle parent, ItemCapabilityId mount) const {
    const Item* item = GetWorld().GetItem(parent);
    if (item) {
        for (ItemHandle child : item->contents) {
            const Item* value = GetWorld().GetItem(child);
            if (value && value->location.installation == mount) { return child; }
        }
    }
    return {};
}
WorldResult<void> ItemReadContext::CheckTransfer(ItemHandle item, ItemLocation destination) const {
    return GetWorld().CheckItemTransfer(item, destination);
}
ItemMutationContext::ItemMutationContext(ItemSystem& system, ItemHandle self) : system_(system), read_(system, self) {}
const ItemReadContext& ItemMutationContext::Read() const { return read_; }
WorldResult<void> ItemMutationContext::TransferEnergy(ItemEnergyRef source, ItemEnergyRef target, std::uint32_t amount) {
    return system_.TransferEnergy(source, target, amount);
}
WorldResult<void> ItemMutationContext::SetEnergy(ItemEnergyRef store, std::uint32_t energy) { return system_.SetEnergy(store, energy); }
WorldResult<void> ItemMutationContext::TransferItem(ItemHandle item, ItemLocation destination) { return system_.world_.TransferItem(item, destination); }
WorldResult<void> ItemMutationContext::SetTile(TileCoord position, Tile tile) { return system_.world_.SetTile(position, tile); }
void ItemMutationContext::RequestDestroy(ItemHandle item) { system_.RequestDestroy(item); }
ItemBehavior::ItemBehavior(ItemCapabilityId capability) : capability_(capability) {}
ItemBehavior::~ItemBehavior() = default;
ItemCapabilityId ItemBehavior::Capability() const { return capability_; }
eastl::optional<Tick> ItemBehavior::NextEventIn(const ItemReadContext&) const { return eastl::nullopt; }
void ItemBehavior::Elapse(ItemMutationContext&, Tick) {}
void ItemBehavior::ProcessEvent(ItemMutationContext&) {}
void ItemBehavior::OnChange(ItemMutationContext&, const ItemChange&) {}
ItemBehaviorDefinition::ItemBehaviorDefinition(ItemBehaviorSchema schema) : schema_(eastl::move(schema)) {}
ItemBehaviorDefinition::~ItemBehaviorDefinition() = default;
const ItemBehaviorSchema& ItemBehaviorDefinition::Schema() const { return schema_; }
const ItemActionDefinition* ItemBehaviorDefinition::FindAction(eastl::string_view id) const {
    for (const auto& action : schema_.actions) { if (action.id == id) { return &action; } }
    return nullptr;
}
ContentResult<void> ItemBehaviorDefinition::ValidateComposition(const ItemDefinition&, ItemCapabilityId, Diagnostics*) const { return {}; }
bool ItemBehaviorDefinition::AcceptsInstallation(const ItemDefinition&) const { return false; }
}
