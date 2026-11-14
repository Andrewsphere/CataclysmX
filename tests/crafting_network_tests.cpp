#include "network/network.h"
#include "network/wire.h"

#include <catch2/catch_test_macros.hpp>
#include <cstring>

namespace {
using namespace cataclysmx;
using namespace cataclysmx::network;
constexpr char CraftingData[] = R"({format_version:1,definitions:[
  {type:'tool_trait',id:'cut',name:'Cut'},
  {type:'item',id:'material',name:'material',mass_g:10,volume_ml:20},
  {type:'item',id:'product',name:'product',mass_g:2,volume_ml:5},
  {type:'item',id:'tool',name:'tool',mass_g:1,volume_ml:1,recovery_allowed:true,tool_traits:[{id:'cut',tier:1}],
    crafting_power:{store:'power',energy_per_work:10000},capabilities:[
      {type:'energy_store',id:'power',capacity_energy:100,initial_energy:100,rechargeable:true}]},
  {type:'recipe',id:'make',name:'Make',inputs:[{item:'material',count:1}],tool_traits:[{id:'cut',tier:1}],
    result:{item:'product',count:1},work:250,crafting_enabled:true},
  {type:'recycling_recipe',id:'recycle',target:'product',inputs:[],returns:[{item:'material',count:1}],work:150}
]})";
ContentCatalog Catalog(eastl::string_view text = CraftingData) {
    const ContentSource source{"crafting-network.json5", text}; Diagnostics diagnostics;
    auto result = LoadContent({&source, 1}, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str()); REQUIRE(result); return eastl::move(*result);
}
WorldView JobView(const ContentCatalog& content) {
    WorldView view; view.width = 1; view.height = 1; view.playerId = 1; view.cells.push_back({});
    view.craftingJobs.push_back({10, CraftingKind::Craft, content.FindCraftingRecipe("make"), {},
        CraftingStatus::PauseRequested, 1, 250, 100, 70, true});
    InventoryEntry wrapper; wrapper.id = 10; wrapper.type = content.UnfinishedCraftItem();
    wrapper.massGrams = 10; wrapper.volumeMl = 20;
    view.inventory.push_back(wrapper); return view;
}

TEST_CASE("Crafting command codec preserves recipe and persistent job identities", "[crafting][network][codec]") {
    for (auto kind : {CommandKind::craft, CommandKind::recycle, CommandKind::resume_craft,
        CommandKind::pause_craft, CommandKind::cancel_craft}) {
        Command command; command.kind = kind;
        if (kind == CommandKind::craft) command.recipe = {23}; else command.item = 999;
        wire::Writer writer; REQUIRE(writer.CommandValue(command));
        wire::Reader reader{{writer.bytes.data(), writer.bytes.size()}};
        Command decoded; REQUIRE(reader.CommandValue(decoded)); CHECK(reader.Done());
        CHECK(decoded.kind == kind); CHECK(decoded.item == command.item); CHECK(decoded.recipe == command.recipe);
        for (std::size_t size = 0; size < writer.bytes.size(); ++size) {
            wire::Reader truncated{{writer.bytes.data(), size}}; CHECK_FALSE(truncated.CommandValue(decoded));
        }
        // Mutate the appended recipe field to make both recipe and item nonzero.
        if (kind != CommandKind::craft) {
            writer.bytes.back() = 1;
            wire::Reader malformed{{writer.bytes.data(), writer.bytes.size()}}; CHECK_FALSE(malformed.CommandValue(decoded));
        }
        command.otherItem = 2; CHECK_FALSE(ValidCommandShape(command));
    }
    Command command; command.kind = CommandKind::craft;
    CHECK_FALSE(ValidCommandShape(command)); command.recipe = {1};
    SECTION("target") { command.item = 2; }
    SECTION("direction") { command.dx = 1; }
    SECTION("capability") { command.capability = {1}; }
    SECTION("action") { command.itemAction = "start"; }
    SECTION("wait cost") { command.waitCost = 1; }
    SECTION("combat target") { command.target = 3; }
    SECTION("style") { command.style = CombatStyle::Defensive; }
    SECTION("interaction") { command.interaction.object = 1; }
    CHECK_FALSE(ValidCommandShape(command));
}

TEST_CASE("Crafting results and job snapshots roundtrip with bounded invariants", "[crafting][network][codec]") {
    auto content = Catalog(); auto view = JobView(content);
    REQUIRE(wire::ValidItemDefinitions(view, content));
    wire::Writer writer; REQUIRE(writer.View(view));
    wire::Reader reader{{writer.bytes.data(), writer.bytes.size()}}; WorldView decoded;
    REQUIRE(reader.View(decoded)); CHECK(reader.Done()); CHECK(decoded.craftingJobs == view.craftingJobs);
    CHECK(decoded.inventory == view.inventory); CHECK(wire::ValidItemDefinitions(decoded, content));
    writer.bytes.back() = 2;
    wire::Reader malformed{{writer.bytes.data(), writer.bytes.size()}}; CHECK_FALSE(malformed.View(decoded));
    view.craftingJobs.front().completedWork = 250;
    wire::Writer invalid; CHECK_FALSE(invalid.View(view));
    view = JobView(content); view.craftingJobs.front().craftingRecipe = {999};
    CHECK_FALSE(wire::ValidItemDefinitions(view, content));
    view = JobView(content); view.craftingJobs.front().recyclingRecipe = {1};
    CHECK_FALSE(invalid.View(view));
    for (auto outcome : {Outcome::craft_started, Outcome::recycling_started, Outcome::craft_resumed,
        Outcome::craft_paused, Outcome::craft_cancelled, Outcome::crafting_refused}) {
        PlayerCommandResult receipt{1, 2, 3, {outcome, false, 40}};
        switch (outcome) {
        case Outcome::craft_started:
        case Outcome::recycling_started:
            receipt.result.craftingOutcome = CraftingOutcome::Started; receipt.result.advanced = true; receipt.result.cost = 100; break;
        case Outcome::craft_resumed:
            receipt.result.craftingOutcome = CraftingOutcome::Resumed; receipt.result.advanced = true; receipt.result.cost = 70; break;
        case Outcome::craft_paused: receipt.result.craftingOutcome = CraftingOutcome::PauseRequested; break;
        case Outcome::craft_cancelled: receipt.result.craftingOutcome = CraftingOutcome::Cancelled; break;
        default: receipt.result.craftingOutcome = CraftingOutcome::MissingTools; break;
        }
        wire::Writer encoded; REQUIRE(encoded.Result(receipt));
        wire::Reader resultReader{{encoded.bytes.data(), encoded.bytes.size()}}; PlayerCommandResult result;
        REQUIRE(resultReader.Result(result)); CHECK(resultReader.Done());
        CHECK(result.result.outcome == outcome); CHECK(result.result.craftingOutcome == receipt.result.craftingOutcome);
        CHECK(result.result.advanced == receipt.result.advanced); CHECK(result.result.cost == receipt.result.cost);
        encoded.bytes.back() = 255;
        wire::Reader badResult{{encoded.bytes.data(), encoded.bytes.size()}}; CHECK_FALSE(badResult.Result(result));
    }
}

TEST_CASE("Only unfinished wrappers permit dynamic dimensions and hidden storage", "[crafting][network]") {
    const auto content = Catalog(); auto view = JobView(content);
    REQUIRE(wire::ValidItemDefinitions(view, content));
    SECTION("wrapper storage cannot be browsed") { view.inventory.front().capacityVolumeMl = UINT32_MAX; }
    SECTION("wrapper child count is hidden") { view.inventory.front().contained_items = 1; }
    SECTION("ordinary item volume must match") {
        view.inventory.front().type = content.FindItem("material"); view.inventory.front().volumeMl = 21;
    }
    SECTION("ordinary item mass must match") {
        view.inventory.front().type = content.FindItem("material"); view.inventory.front().massGrams = 11;
    }
    CHECK_FALSE(wire::ValidItemDefinitions(view, content));
}

TEST_CASE("Content fingerprints include compiled crafting semantics", "[crafting][network]") {
    const auto original = Catalog(); const auto fingerprint = ContentFingerprint(original);
    String modified = CraftingData;
    const auto replace = [&](const char* before, const char* after) {
        const auto position = modified.find(before); REQUIRE(position != String::npos);
        modified.replace(position, std::strlen(before), after);
    };
    SECTION("work") { replace("work:250", "work:251"); }
    SECTION("enabled") { replace("crafting_enabled:true", "crafting_enabled:false"); }
    SECTION("input count") { replace("inputs:[{item:'material',count:1}]", "inputs:[{item:'material',count:2}]"); }
    SECTION("input retention") { replace("inputs:[{item:'material',count:1}]", "inputs:[{item:'material',count:1,retained:true}]"); }
    SECTION("recycling returns") { replace("returns:[{item:'material',count:1}]", "returns:[{item:'material',count:2}]"); }
    SECTION("recycling work") { replace("work:150", "work:151"); }
    SECTION("tool trait tier") { replace("tool_traits:[{id:'cut',tier:1}]", "tool_traits:[{id:'cut',tier:2}]"); }
    SECTION("tool energy") { replace("energy_per_work:10000", "energy_per_work:10001"); }
    SECTION("recovery") { replace("recovery_allowed:true", "recovery_allowed:false"); }
    SECTION("trait name") { replace("name:'Cut'", "name:'Cutting'"); }
    const auto changed = Catalog(modified); CHECK(ContentFingerprint(changed) != fingerprint);
    CHECK(ContentFingerprint(Catalog()) == fingerprint);
}
}
