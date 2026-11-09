#pragma once

#include "core/containers.h"
#include "core/timing.h"
#include "core/world_types.h"
#include <EASTL/expected.h>

namespace cataclysmx {

struct ItemTypeId {
    std::uint32_t value = 0;
    bool operator==(const ItemTypeId&) const = default;
};
struct ItemCapabilityId {
    std::uint16_t value = 0;
    bool operator==(const ItemCapabilityId&) const = default;
};
inline constexpr std::uint16_t maxItemCapabilities = 16;
inline constexpr std::size_t maxItemActionName = 64;
inline constexpr std::size_t maxItemText = 192;
inline constexpr std::size_t maxItemActionsPerItem = 64;

enum class StateError { InvalidData, UnsupportedVersion };
template<class T> using StateResult = eastl::expected<T, StateError>;

struct ItemText {
    String key;
    String fallback;
    bool operator==(const ItemText&) const = default;
};
struct ItemUseReason {
    String code;
    ItemText message;
    bool operator==(const ItemUseReason&) const = default;
};
enum class ItemActionArgument : std::uint8_t { None, Item, Direction };
enum class ItemActionCategory : std::uint8_t { Operate, ManageContents };
enum class ItemUseOutcome : std::uint8_t { Succeeded, InvalidAction, Inaccessible, Refused };

struct ItemActionDefinition {
    String id;
    ItemActionArgument argument = ItemActionArgument::None;
    ItemActionCategory category = ItemActionCategory::Operate;
    std::uint32_t baseCost = 100;
    ItemText label;
    bool operator==(const ItemActionDefinition&) const = default;
};
struct ItemBehaviorSchema {
    Vector<ItemActionDefinition> actions; // Unique local IDs, lexically sorted.
    bool hasActivity = false;
    bool countdown = false;
    Tick maximumDelay = 0;
    std::uint32_t implementationVersion = 1;
    bool operator==(const ItemBehaviorSchema&) const = default;
};
struct ItemActionInfo {
    ItemCapabilityId capability{};
    String action; // Stable identity local to this behavior, e.g. "start".
    ItemActionArgument argument = ItemActionArgument::None;
    ItemUseOutcome availability = ItemUseOutcome::Succeeded;
    std::uint32_t baseCost = 100;
    ItemActionCategory category = ItemActionCategory::Operate;
    ItemText label;
    ItemUseReason reason;
    bool operator==(const ItemActionInfo&) const = default;
};
struct ItemUseRequest {
    ItemCapabilityId capability{};
    String action;
    ItemHandle otherItem{};
    int dx = 0;
    int dz = 0;
};
struct ItemUseResult {
    ItemUseOutcome outcome = ItemUseOutcome::InvalidAction;
    std::uint32_t baseCost = 0;
    ItemUseReason reason;
};
struct ItemEnergyView {
    ItemCapabilityId capability{};
    std::uint32_t energy = 0;
    std::uint32_t capacity = 0;
    bool operator==(const ItemEnergyView&) const = default;
};
struct ItemBehaviorView {
    ItemCapabilityId capability{};
    bool active = false;
    Tick remainingTicks = 0;
    ItemText status;
    bool operator==(const ItemBehaviorView&) const = default;
};
struct ItemPresentation {
    Vector<ItemEnergyView> energy;
    Vector<ItemBehaviorView> activities;
    Vector<ItemActionInfo> actions;
};
struct ItemEnergyRef {
    ItemHandle item{};
    ItemCapabilityId capability{};
};

bool ValidItemActionId(eastl::string_view id);
bool ValidItemText(const ItemText& text);
bool ValidItemUseReason(ItemUseOutcome outcome, const ItemUseReason& reason);
bool ValidItemBehaviorSchema(const ItemBehaviorSchema& schema);
const char* ItemUseMessage(ItemUseOutcome outcome);

}
