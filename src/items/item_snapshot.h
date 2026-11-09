#pragma once

#include "content/item_definition.h"

namespace cataclysmx {
class ContentCatalog;
struct SavedItem;
struct ChunkArchive;

// Callback-scoped, read-only archive access; no runtime entity ownership.
class ItemSnapshotContext {
public:
    ItemSnapshotContext(const ContentCatalog& content, const ChunkArchive& chunk);
    // Requires an existing item with a valid type. Validation checks all types
    // before dispatching behavior snapshot hooks.
    const ItemDefinition& Definition(PersistentId item) const;
    const SavedItem* FindItem(PersistentId item) const;
    const SavedItem* Installed(PersistentId parent, ItemCapabilityId mount) const;
    const DataObject* State(PersistentId item, ItemCapabilityId capability) const;
    eastl::optional<ItemEnergyView> Energy(PersistentId item, ItemCapabilityId store) const;
private:
    const ContentCatalog& content_;
    const ChunkArchive& chunk_;
};

// Shared item-tree validation for RAM archives and durable chunk snapshots.
// Rejects non-parent-first trees, cycles, missing owners and invalid local state
// before invoking relational behavior hooks. Global entity IDs remain a caller check.
StateResult<void> ValidateItemSnapshot(const ContentCatalog& content,
    const ChunkArchive& chunk, Diagnostics* diagnostics = nullptr);
}
