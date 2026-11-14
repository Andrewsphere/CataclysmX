#include "client/crafting_menu.h"
#include "client/text.h"

#include <initializer_list>

namespace cataclysmx {
namespace {
const char* Status(CraftingStatus status) {
    switch (status) {
    case CraftingStatus::Paused: return "paused";
    case CraftingStatus::Working: return "working";
    case CraftingStatus::PauseRequested: return "pause requested";
    case CraftingStatus::MissingTools: return "missing tools";
    case CraftingStatus::InsufficientEnergy: return "insufficient energy";
    case CraftingStatus::WorkerUnavailable: return "worker unavailable";
    }
    return "unknown";
}
}

bool SameCraftingChoice(const Command& left, const Command& right) {
    return left.kind == right.kind && left.item == right.item && left.recipe == right.recipe;
}

void CollectCraftingMenu(const WorldView& view, const ContentCatalog& content,
    const Localization& localization, Vector<CraftingMenuRow>& rows) {
    rows.clear();
    for (std::size_t i = 1; i <= content.CraftingRecipeCount(); ++i) {
        const CraftingRecipeId id{static_cast<std::uint32_t>(i)};
        const auto& recipe = content.GetCraftingRecipe(id);
        if (!recipe.enabled) { continue; }
        Command command; command.kind = CommandKind::craft; command.recipe = id;
        rows.push_back({command, "Craft #" + UnsignedText(i) + " " + recipe.name
            + " -> " + localization.ItemName(content, recipe.result.item) + " x" + UnsignedText(recipe.result.count)});
    }
    for (const auto& job : view.craftingJobs) {
        String label = " job #" + UnsignedText(job.id) + " ";
        if (job.kind == CraftingKind::Craft) {
            if (const auto* recipe = content.TryGetCraftingRecipe(job.craftingRecipe)) { label += recipe->name; }
        } else if (const auto* recipe = content.TryGetRecyclingRecipe(job.recyclingRecipe)) { label += recipe->name; }
        label += " " + UnsignedText(job.completedWork) + "/" + UnsignedText(job.totalWork);
        label += " ["; label += Status(job.status); label += "] worker #" + UnsignedText(job.worker);
        label += " paid ticks " + UnsignedText(job.fundedTicksRemaining);
        if (job.pauseRequested) { label += " (pause requested)"; }
        for (const auto kind : {CommandKind::resume_craft, CommandKind::pause_craft, CommandKind::cancel_craft}) {
            Command command; command.kind = kind; command.item = job.id;
            rows.push_back({command, String(kind == CommandKind::resume_craft ? "Resume" : kind == CommandKind::pause_craft ? "Pause" : "Cancel") + label});
        }
    }
    const auto items = [&](const Vector<InventoryEntry>& entries, const char* location) {
        for (const auto& item : entries) {
            if (!content.FindRecyclingFor(item.type).value || item.contained_items != 0 || item.installation.value) { continue; }
            bool job = false;
            for (const auto& entry : view.craftingJobs) { job |= entry.id == item.id; }
            if (job) { continue; }
            Command command; command.kind = CommandKind::recycle; command.item = item.id;
            rows.push_back({command, "Recycle #" + UnsignedText(item.id) + " " + localization.ItemName(content, item.type) + location});
        }
    };
    items(view.inventory, " (carried)");
    items(view.groundItems, " (ground)");
}
}
