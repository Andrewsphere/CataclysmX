#pragma once

#include "core/world_types.h"
#include "core/containers.h"
#include "core/timing.h"
#include <EASTL/optional.h>

namespace cataclysmx {

// Owned command/snapshot identity, never a runtime entity handle. Inventory is
// always the acting player's direct inventory. Only Ground uses position; only
// Object and Container use id. Unused fields must be zero.
enum class ItemStorageKind : std::uint8_t { Inventory, Ground, Object, Container };
struct ItemStorage {
    ItemStorageKind kind = ItemStorageKind::Inventory;
    TileCoord position{};
    PersistentId id = 0;
    bool operator==(const ItemStorage&) const = default;
};

enum class InventoryOutcome : std::uint8_t {
    Succeeded, Inaccessible, InvalidItem, InvalidDestination, NoCapacity,
    NeedsHands, AlreadyThere, Reserved, Busy
};
struct InventoryResult {
    InventoryOutcome outcome = InventoryOutcome::Inaccessible;
    Tick baseCost = 0;
};

// Source metadata is separate from entries, avoiding copied item trees in every
// pane. The frontend filters entries by their exact immediate storage identity.
struct ItemStorageView {
    ItemStorage storage;
    bool accessible = false;
    InventoryOutcome reason = InventoryOutcome::Inaccessible;
    std::uint64_t massGrams = 0;
    std::uint64_t volumeMl = 0;
    eastl::optional<std::uint64_t> massLimitGrams;
    eastl::optional<std::uint64_t> volumeLimitMl;
    // Authored display name for furniture/container sources; frontend localizes
    // inventory/floor labels and item names through its catalog.
    String name;
    // Object's live tile, independent of the rendered map radius/layer. Only
    // Object sources use this field; all others retain the zero coordinate.
    TileCoord objectPosition{};
    bool operator==(const ItemStorageView&) const = default;
};

const char* InventoryOutcomeMessage(InventoryOutcome outcome);
bool ValidItemStorage(const ItemStorage& storage);

}
