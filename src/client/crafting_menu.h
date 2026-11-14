#pragma once

#include "client/localization.h"

namespace cataclysmx {

struct CraftingMenuRow {
    Command command;
    String label;
};

// Reuses caller storage. Only server-provided reachable jobs/items are listed.
void CollectCraftingMenu(const WorldView& view, const ContentCatalog& content,
    const Localization& localization, Vector<CraftingMenuRow>& rows);
// Identity excludes presentation, so refreshed progress cannot retarget a choice.
bool SameCraftingChoice(const Command& left, const Command& right);

}
