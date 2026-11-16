#include "content/content.h"
#include "core/server.h"

#include <catch2/catch_test_macros.hpp>

namespace {
using namespace cataclysmx;

ContentCatalog PilotContent() {
    Diagnostics diagnostics;
    auto loaded = LoadContentPack(CATACLYSMX_CONTENT_PACK, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str());
    REQUIRE(loaded);
    return eastl::move(*loaded);
}

bool PoolCanYield(const ContentCatalog& content, LootPoolId pool, ItemTypeId item, unsigned depth = 0) {
    REQUIRE(depth < maxLootDepth);
    for (const auto& entry : content.GetLootPool(pool).entries) {
        if (entry.weight == 0 || entry.count.max == 0) { continue; }
        if (entry.kind == LootEntryKind::Item && entry.item == item) { return true; }
        if (entry.kind == LootEntryKind::Pool && PoolCanYield(content, entry.pool, item, depth + 1)) { return true; }
    }
    return false;
}

bool HasPlacement(const ContentCatalog& content, ItemTypeId item) {
    const auto contains = [&](const PatternPopulation& population) {
        for (const auto& placement : population.loot) {
            if (placement.chance > 0 && placement.rolls > 0 && PoolCanYield(content, placement.pool, item)) { return true; }
        }
        return false;
    };
    for (std::uint32_t value = 1; value <= content.BuildingPatternCount(); ++value) {
        const auto& pattern = content.GetBuildingPattern({value});
        if (contains(pattern.population)) { return true; }
        for (const auto& basement : pattern.basements) {
            if (contains(basement.population)) { return true; }
        }
    }
    return false;
}

TEST_CASE("Town item pilot loads five authored items with meaningful flag boundaries", "[item-pilot][content]") {
    const auto content = PilotContent();
    for (const char* name : {"claw_hammer", "wood_scrap", "glass_bottle", "work_shirt", "arming_sword"}) {
        CAPTURE(name);
        const auto type = content.FindItem(name); REQUIRE(type.value != 0);
        const auto& item = content.GetItem(type);
        CHECK_FALSE(item.description.empty()); CHECK_FALSE(item.flags.Empty());
        REQUIRE_FALSE(item.art.lines.empty());
        CHECK(item.art.lines.size() <= 32); CHECK(item.art.width > 0); CHECK(item.art.width <= 32);
        CHECK((item.art.width >= 24 || item.art.lines.size() >= 24));
        std::uint32_t width = 0;
        bool colored = false;
        for (const auto& line : item.art.lines) {
            std::uint32_t cells = 0;
            for (const auto& run : line) {
                CHECK_FALSE(run.text.empty()); CHECK(run.cells > 0);
                cells += run.cells; colored |= !run.color.useDefault;
            }
            if (cells > width) { width = cells; }
        }
        CHECK(width == item.art.width); CHECK(colored);
    }
    const auto has = [&](const char* item, const char* flag) {
        const auto id = content.FindFlag(flag); REQUIRE(id.value != 0);
        return content.GetItem(content.FindItem(item)).flags.Contains(id);
    };
    CHECK(has("claw_hammer", "tool")); CHECK(has("claw_hammer", "ferromagnetic"));
    CHECK(has("wood_scrap", "salvage")); CHECK(has("wood_scrap", "combustible"));
    CHECK(has("glass_bottle", "container")); CHECK(has("glass_bottle", "brittle"));
    CHECK(has("glass_bottle", "transparent")); CHECK_FALSE(has("glass_bottle", "sharp_edge"));
    CHECK_FALSE(has("glass_bottle", "opaque"));
    const auto* bottle = ItemContainer(content.GetItem(content.FindItem("glass_bottle")));
    REQUIRE(bottle); CHECK(bottle->capacityVolumeMl == 500);
    CHECK(has("work_shirt", "clothing")); CHECK(has("work_shirt", "flexible"));
    CHECK_FALSE(has("work_shirt", "armor")); CHECK_FALSE(has("work_shirt", "rigid"));
    CHECK(has("arming_sword", "melee_weapon")); CHECK(has("arming_sword", "sharp_edge"));
    CHECK(has("arming_sword", "pointed"));
}

TEST_CASE("Town item pilot crafting inputs and tools have authored loot placements", "[item-pilot][content][crafting]") {
    const auto content = PilotContent();
    for (const char* name : {"claw_hammer", "hand_saw", "wood_plank", "scrap"}) {
        CAPTURE(name);
        const auto type = content.FindItem(name); REQUIRE(type.value != 0);
        CHECK(HasPlacement(content, type));
    }
    const auto wire = content.FindItem("wire_bundle"), cord = content.FindItem("electrical_cord");
    REQUIRE(wire.value != 0); REQUIRE(cord.value != 0);
    CHECK((HasPlacement(content, wire) || HasPlacement(content, cord)));
    const auto* salvage = content.TryGetCraftingRecipe(content.FindCraftingRecipe("salvage_wire_bundle"));
    REQUIRE(salvage); REQUIRE(salvage->inputs.Size() == 1);
    CHECK(salvage->inputs[0].Item() == cord); CHECK(salvage->inputs[0].Count() == 1);
    CHECK(salvage->result.item == wire); CHECK(salvage->result.count == 1);
    for (const char* name : {"hammering", "house_cutting"}) {
        CAPTURE(name); CHECK(content.FindToolTrait(name).value != 0);
    }
}

TEST_CASE("Town item pilot crafts plank scraps into a salvaged sword through server commands", "[item-pilot][crafting][server][world]") {
    const auto content = PilotContent();
    // Use the small deterministic arena with the actual Town catalog. Loot
    // reachability is checked above, independently of a generated seed's luck.
    Server server(content);
    auto& world = server.GetWorld();
    const auto player = server.GetPlayer();
    REQUIRE(world.DestroyActor(world.ActorAt({5, 5})));
    auto pacing = server.GetPacing(); pacing.rateLimited = true; pacing.ticksPerSecond = 100;
    REQUIRE(server.SetPacing(pacing));
    const auto add = [&](const char* name) {
        const auto item = world.CreateItem(content.FindItem(name), ItemLocation::Carried(player));
        REQUIRE(item); return *item;
    };
    const auto count = [&](const char* name) {
        unsigned total = 0;
        for (const auto handle : world.GetActor(player)->inventory) {
            if (world.GetItem(handle)->type == content.FindItem(name)) { ++total; }
        }
        return total;
    };
    const auto craft = [&](const char* name) {
        Command command; command.kind = CommandKind::craft; command.recipe = content.FindCraftingRecipe(name);
        REQUIRE(command.recipe.value != 0); return server.Send(command);
    };
    const auto finish = [&] {
        for (unsigned i = 0; i < 100 && !server.View(0).craftingJobs.empty(); ++i) { server.Update(1000000); }
        REQUIRE(server.View(0).craftingJobs.empty());
        CHECK(world.GetActorRemainingTicks(player) == 0);
        REQUIRE(world.CheckInvariants());
    };
    const auto refused = [&](const char* name) {
        const auto tick = server.GetTick();
        const auto result = craft(name);
        CHECK(result.outcome == Outcome::crafting_refused); CHECK(result.craftingOutcome == CraftingOutcome::MissingTools);
        CHECK_FALSE(result.advanced); CHECK(result.cost == 0); CHECK(server.GetTick() == tick);
        CHECK(server.View(0).craftingJobs.empty());
    };

    const auto plank = add("wood_plank");
    refused("cut_wood_scrap");
    REQUIRE(world.GetItem(plank)); CHECK_FALSE(world.IsReservedItem(plank));
    const auto saw = add("hand_saw");
    REQUIRE(craft("cut_wood_scrap").outcome == Outcome::craft_started);
    finish();
    CHECK_FALSE(world.GetItem(plank)); CHECK(count("wood_scrap") == 10); REQUIRE(world.GetItem(saw));

    Vector<ItemHandle> scrap;
    for (unsigned i = 0; i < 15; ++i) { scrap.push_back(add("scrap")); }
    const auto firstWire = add("wire_bundle"); const auto secondWire = add("wire_bundle");
    refused("make_salvaged_arming_sword"); // Cutting alone is insufficient.
    CHECK(count("scrap") == 15); CHECK(count("wood_scrap") == 10); CHECK(count("wire_bundle") == 2);
    const auto hammer = add("claw_hammer");
    REQUIRE(world.SetTile({7, 7}, {content.FindTerrain("floor"), 0}));
    REQUIRE(world.TransferItem(saw, ItemLocation::Ground({7, 7})));
    refused("make_salvaged_arming_sword"); // Hammering alone is insufficient.
    CHECK(count("scrap") == 15); CHECK(count("wood_scrap") == 10); CHECK(count("wire_bundle") == 2);
    REQUIRE(world.TransferItem(saw, ItemLocation::Carried(player)));
    REQUIRE(craft("make_salvaged_arming_sword").outcome == Outcome::craft_started);
    finish();
    CHECK(count("scrap") == 1); CHECK(count("wood_scrap") == 8); CHECK(count("wire_bundle") == 1);
    unsigned remainingScrap = 0;
    for (const auto handle : scrap) { if (world.GetItem(handle)) { ++remainingScrap; } }
    CHECK(remainingScrap == 1);
    CHECK((world.GetItem(firstWire) != nullptr) != (world.GetItem(secondWire) != nullptr));
    REQUIRE(world.GetItem(saw)); REQUIRE(world.GetItem(hammer));
    CHECK(world.GetItem(saw)->location == ItemLocation::Carried(player));
    CHECK(world.GetItem(hammer)->location == ItemLocation::Carried(player));
    REQUIRE(count("arming_sword") == 1);
    for (const auto handle : world.GetActor(player)->inventory) {
        if (world.GetItem(handle)->type != content.FindItem("arming_sword")) { continue; }
        const auto& sword = content.GetItem(world.GetItem(handle)->type);
        for (const char* flag : {"melee_weapon", "sharp_edge", "pointed", "ferromagnetic"}) {
            CAPTURE(flag); CHECK(sword.flags.Contains(content.FindFlag(flag)));
        }
        CHECK(world.GetItemMass(handle) == 1800); CHECK(world.GetItemVolume(handle) == 2000);
        REQUIRE(sword.melee); CHECK(sword.melee->attackCost == 130); CHECK(sword.melee->hands == 1);
        REQUIRE(sword.melee->damage.size() == 1);
        CHECK(sword.melee->damage[0] == DamagePacket{DamageType::Slashing, 1800, 0});
    }
    REQUIRE(world.CheckInvariants());
}
}
