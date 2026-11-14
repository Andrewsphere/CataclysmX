#include "client/crafting_menu.h"

#include <catch2/catch_test_macros.hpp>

namespace {
using namespace cataclysmx;

ContentResult<ContentCatalog> Catalog() {
    const ContentSource source{"crafting-ui.json5", R"({format_version:1,definitions:[
        {type:'item',id:'scrap',name:'Scrap',mass_g:0,volume_ml:1},
        {type:'item',id:'product',name:'Product',mass_g:0,volume_ml:1},
        {type:'recipe',id:'a',name:'First',inputs:[{item:'scrap',count:1}],
         result:{item:'product',count:1},work:10,recycling:{id:'recycle',work:5,efficiency:1000}},
        {type:'recipe',id:'b',name:'Second',inputs:[],result:{item:'product',count:1},work:20},
        {type:'recipe',id:'disabled',crafting_enabled:false,inputs:[],result:{item:'scrap',count:1},work:1}
    ]})"};
    return LoadContent({&source, 1});
}

TEST_CASE("Crafting menu keeps distinct recipes and emits numeric command payloads", "[crafting-ui]") {
    const auto content = Catalog(); REQUIRE(content);
    Vector<CraftingMenuRow> rows;
    CollectCraftingMenu({}, *content, Localization{}, rows);
    REQUIRE(rows.size() == 2);
    CHECK(rows[0].command.kind == CommandKind::craft);
    CHECK(rows[0].command.recipe == content->FindCraftingRecipe("a"));
    CHECK(rows[1].command.recipe == content->FindCraftingRecipe("b"));
    CHECK(rows[0].command.item == 0);
    CHECK_FALSE(SameCraftingChoice(rows[0].command, rows[1].command));
    CHECK(rows[0].label.find("First") != String::npos);
}

TEST_CASE("Crafting menu uses reachable snapshot jobs and visible recycling candidates", "[crafting-ui]") {
    const auto content = Catalog(); REQUIRE(content);
    WorldView view;
    CraftingEntry job; job.id = 900; job.craftingRecipe = content->FindCraftingRecipe("a");
    job.status = CraftingStatus::Working; job.worker = 42;
    job.completedWork = 4; job.totalWork = 10; job.fundedTicksRemaining = 6;
    view.craftingJobs.push_back(job);
    InventoryEntry item; item.id = 100; item.type = content->FindItem("product");
    view.inventory.push_back(item);
    item.id = 101; item.parent = 100; view.inventory.push_back(item);
    item.id = 102; item.contained_items = 1; view.inventory.push_back(item);
    item.id = 103; item.parent = 0; item.contained_items = 0; view.groundItems.push_back(item);
    item.id = 104; item.contained_items = 1; view.groundItems.push_back(item);
    item.id = 105; item.parent = 100; item.contained_items = 0; item.installation = {1}; view.inventory.push_back(item);
    Vector<CraftingMenuRow> rows;
    CollectCraftingMenu(view, *content, Localization{}, rows);
    REQUIRE(rows.size() == 8);
    for (const auto& row : rows) { CHECK(ValidCommandShape(row.command)); }
    CHECK(rows[2].command.kind == CommandKind::resume_craft);
    CHECK(rows[3].command.kind == CommandKind::pause_craft);
    CHECK(rows[4].command.kind == CommandKind::cancel_craft);
    for (std::size_t i = 2; i < 5; ++i) {
        CHECK(rows[i].command.item == 900);
        CHECK(rows[i].command.recipe.value == 0);
        CHECK(rows[i].label.find("4/10") != String::npos);
        CHECK(rows[i].label.find("working") != String::npos);
    }
    CHECK(rows[5].command.item == 100);
    CHECK(rows[6].command.item == 101);
    CHECK(rows[7].command.item == 103);
    CHECK(rows[7].command.kind == CommandKind::recycle);
    const Command selection = rows[3].command;
    view.craftingJobs.front().completedWork = 5;
    CollectCraftingMenu(view, *content, Localization{}, rows);
    CHECK(SameCraftingChoice(selection, rows[3].command));
    view.craftingJobs.clear(); view.inventory.clear(); view.groundItems.clear();
    CollectCraftingMenu(view, *content, Localization{}, rows);
    REQUIRE(rows.size() == 2);
    for (const auto& row : rows) { CHECK_FALSE(SameCraftingChoice(selection, row.command)); }
}

TEST_CASE("Crafting has shared configurable entry keys and authoritative result text", "[crafting-ui]") {
    const auto keys = DefaultKeymap();
    CHECK(ResolveAction(keys, InputContext::Gameplay, {"&"}) == InputAction::Crafting);
    CHECK(ResolveAction(keys, InputContext::Interaction, {"&"}) == InputAction::Crafting);
    CHECK(ParseAction("crafting") == InputAction::Crafting);
    CommandResult refused; refused.outcome = Outcome::crafting_refused; refused.craftingOutcome = CraftingOutcome::MissingTools;
    CHECK(Localization{}.ResultText(refused) == CommandResultMessage(refused));
}
}
