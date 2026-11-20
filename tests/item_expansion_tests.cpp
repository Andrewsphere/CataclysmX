#include "content/content.h"
#include "core/server.h"
#include "core/simulation.h"
#include "skills/learning_system.h"
#include "worldgen/worldgen.h"

#include <catch2/catch_test_macros.hpp>
#include <EASTL/algorithm.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

// Shared with the SDL browser integration test, independently of catalog order.
extern const char* const expansionItemIds[80] = {
    "steel_pipe", "steel_rod", "steel_wire", "steel_chain", "wire_mesh", "aluminum_sheet", "aluminum_tube", "copper_sheet", "plywood_panel", "wooden_dowel",
    "rubber_sheet", "foam_padding", "canvas_patch", "leather_patch", "nylon_webbing", "framing_nails", "wood_screws", "nuts_and_bolts", "door_hinge", "coil_spring",
    "resistor", "capacitor", "light_emitting_diode", "toggle_switch", "power_relay", "cartridge_fuse", "electrical_connector", "insulated_power_cable", "transformer", "induction_coil",
    "heating_element", "processor_chip", "memory_module", "solid_state_drive", "computer_power_supply", "cooling_fan", "display_module", "distance_sensor", "toaster", "desk_radio",
    "retractable_utility_knife", "tin_snips", "wood_chisel", "hand_drill", "multimeter", "socket_wrench_set", "cotton_socks", "hooded_sweatshirt", "sneakers", "winter_coat",
    "work_coveralls", "hard_hat", "welding_gloves", "tool_belt", "canvas_duffel", "padded_jacket", "wooden_club", "quarterstaff", "salvaged_machete", "steel_spear",
    "utility_bucket", "water_jerrycan", "metal_toolbox", "glass_jar", "sleeping_bag", "wool_blanket", "foam_sleeping_mat", "camping_tent", "candle", "matchbox",
    "compass", "sewing_thread", "epoxy_adhesive", "solder_wire", "rolled_oats", "canned_tuna", "trail_mix", "toilet_paper", "toothpaste", "toy_robot",
};

namespace {
using namespace cataclysmx;

const ContentCatalog& ExpansionContent() {
    static const ContentCatalog content = [] {
        Diagnostics diagnostics;
        auto loaded = LoadContentPack(CATACLYSMX_CONTENT_PACK, &diagnostics);
        INFO(FormatDiagnostics(diagnostics).c_str()); REQUIRE(loaded);
        REQUIRE(ValidateWorldgenContent(*loaded, &diagnostics));
        INFO(FormatDiagnostics(diagnostics).c_str()); REQUIRE(diagnostics.Empty());
        return eastl::move(*loaded);
    }();
    return content;
}

bool Current(const ContentCatalog& content, ItemTypeId item) {
    return !content.GetItem(item).flags.Contains(content.FindFlag("obsolete"));
}

bool PoolYields(const ContentCatalog& content, LootPoolId pool, ItemTypeId item, unsigned depth = 0) {
    REQUIRE(depth < maxLootDepth);
    for (const auto& entry : content.GetLootPool(pool).entries) {
        if (!entry.weight || !entry.count.max) { continue; }
        if (entry.kind == LootEntryKind::Item && entry.item == item) { return true; }
        if (entry.kind == LootEntryKind::Pool && PoolYields(content, entry.pool, item, depth + 1)) { return true; }
    }
    return false;
}

bool Placed(const ContentCatalog& content, ItemTypeId item) {
    const auto contains = [&](const PatternPopulation& population) {
        for (const auto& marker : population.loot) {
            if (marker.chance && marker.rolls && PoolYields(content, marker.pool, item)) { return true; }
        }
        return false;
    };
    for (std::uint32_t i = 1; i <= content.BuildingPatternCount(); ++i) {
        const auto& pattern = content.GetBuildingPattern({i});
        if (contains(pattern.population)) { return true; }
        for (const auto& basement : pattern.basements) { if (contains(basement.population)) { return true; } }
    }
    return false;
}

bool Expansion(const ContentCatalog& content, ItemTypeId type) {
    for (const char* id : expansionItemIds) { if (content.FindItem(id) == type) { return true; } }
    return false;
}

TEST_CASE("Item expansion loads eighty current colored definitions and placed routes", "[item-expansion][content][worldgen]") {
    const auto& content = ExpansionContent();
    unsigned placed = 0;
    for (const char* name : expansionItemIds) {
        CAPTURE(name);
        const auto type = content.FindItem(name); REQUIRE(type.value);
        const auto& item = content.GetItem(type);
        CHECK(Current(content, type)); CHECK_FALSE(item.description.empty()); CHECK_FALSE(item.flags.Empty());
        const auto path = std::filesystem::path(CATACLYSMX_CONTENT_PACK).parent_path() / "art" / "items" / name / "art.txt";
        std::ifstream file(path, std::ios::binary); REQUIRE(file.is_open());
        const std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        const auto parsed = ParseTextArtFile({source.data(), source.size()}); REQUIRE(parsed);
        CHECK(item.art == *parsed);
        REQUIRE_FALSE(item.art.lines.empty());
        CHECK(item.art.width > 0); CHECK(item.art.width <= 32); CHECK(item.art.lines.size() <= 32);
        bool colored = false; std::uint32_t width = 0;
        for (const auto& line : item.art.lines) {
            std::uint32_t cells = 0;
            for (const auto& run : line) {
                CHECK_FALSE(run.text.empty()); CHECK(run.cells > 0);
                cells += run.cells; colored |= !run.color.useDefault;
            }
            if (cells > width) { width = cells; }
        }
        CHECK(width == item.art.width); CHECK(colored);
        const bool craftOnly = eastl::string_view(name) == "wooden_club" || eastl::string_view(name) == "quarterstaff"
            || eastl::string_view(name) == "salvaged_machete" || eastl::string_view(name) == "steel_spear";
        const bool hasPlacement = Placed(content, type);
        CHECK(hasPlacement != craftOnly); placed += hasPlacement;
    }
    CHECK(placed == 76);
}

TEST_CASE("Item expansion recipes have reachable current prerequisites and bounded mass", "[item-expansion][content][crafting]") {
    const auto& content = ExpansionContent();
    Vector<bool> reachable(content.ItemCount() + 1, false);
    for (std::uint32_t i = 1; i <= content.ItemCount(); ++i) { reachable[i] = Current(content, {i}) && Placed(content, {i}); }
    bool changed = true;
    while (changed) {
        changed = false;
        for (std::uint32_t i = 1; i <= content.CraftingRecipeCount(); ++i) {
            const auto& recipe = content.GetCraftingRecipe({i});
            if (!recipe.enabled || reachable[recipe.result.item.value] || !Current(content, recipe.result.item)) { continue; }
            bool ready = true;
            for (const auto& input : recipe.inputs) { ready &= reachable[input.Item().value]; }
            for (const auto& required : recipe.toolTraits) {
                bool found = false;
                for (std::uint32_t j = 1; j <= content.ItemCount(); ++j) {
                    if (!reachable[j]) { continue; }
                    for (const auto& provided : content.GetItem({j}).toolTraits) {
                        found |= provided.trait == required.trait && provided.tier >= required.tier;
                    }
                }
                ready &= found;
            }
            if (ready) { reachable[recipe.result.item.value] = true; changed = true; }
        }
        for (std::uint32_t i = 1; i <= content.MachineRecipeCount(); ++i) {
            const MachineRecipeId id{i};
            const auto& recipe = content.GetMachineRecipe(id);
            if (reachable[recipe.result.item.value] || !Current(content, recipe.result.item)) { continue; }
            bool ready = true;
            for (const auto& input : recipe.inputs) { ready &= reachable[input.Item().value]; }
            bool supported = false;
            for (std::uint32_t j = 1; ready && j <= content.ObjectCount(); ++j) {
                const auto& machine = content.GetObject({j});
                if (!machine.packingItem.value || !reachable[machine.packingItem.value]
                    || eastl::find(machine.selectableRecipes.begin(), machine.selectableRecipes.end(), id)
                        == machine.selectableRecipes.end()) { continue; }
                bool design = false;
                bool software = !recipe.requiredSoftware.value;
                for (auto package : content.GetItem(machine.packingItem).suppliedInformation) {
                    const auto& information = content.GetInformation(package);
                    design |= eastl::find(information.designs.begin(), information.designs.end(), id) != information.designs.end();
                    software |= package == recipe.requiredSoftware;
                }
                supported |= design && software;
            }
            if (ready && supported) { reachable[recipe.result.item.value] = true; changed = true; }
        }
    }
    for (const char* name : {"wooden_club", "quarterstaff", "salvaged_machete", "steel_spear"}) {
        CAPTURE(name); const auto type = content.FindItem(name); REQUIRE(type.value); CHECK(reachable[type.value]);
    }
    for (std::uint32_t i = 1; i <= content.CraftingRecipeCount(); ++i) {
        const auto& recipe = content.GetCraftingRecipe({i});
        if (!Expansion(content, recipe.result.item)) { continue; }
        CAPTURE(recipe.id.c_str()); CHECK(Current(content, recipe.result.item));
        std::uint64_t mass = 0;
        for (const auto& input : recipe.inputs) {
            CHECK(Current(content, input.Item())); CHECK(reachable[input.Item().value]);
            if (!input.Retained()) { mass += std::uint64_t(content.GetItem(input.Item()).massGrams) * input.Count(); }
        }
        CHECK(std::uint64_t(content.GetItem(recipe.result.item).massGrams) * recipe.result.count <= mass);
    }
    for (std::uint32_t i = 1; i <= content.RecyclingRecipeCount(); ++i) {
        const auto& recipe = content.GetRecyclingRecipe({i});
        if (!Expansion(content, recipe.target)) { continue; }
        CAPTURE(recipe.id.c_str()); CHECK(Current(content, recipe.target));
        std::uint64_t mass = 0;
        for (const auto& input : recipe.inputs) { CHECK(Current(content, input.Item())); }
        for (const auto& output : recipe.returns) {
            CHECK(Current(content, output.item));
            mass += std::uint64_t(content.GetItem(output.item).massGrams) * output.count;
            // When a return is also a crafting ingredient, it cannot exceed
            // the amount consumed to make this single target.
            for (std::uint32_t j = 1; j <= content.CraftingRecipeCount(); ++j) {
                const auto& craft = content.GetCraftingRecipe({j});
                if (craft.result.item != recipe.target || craft.result.count != 1) { continue; }
                for (const auto& input : craft.inputs) {
                    if (input.Item() == output.item && !input.Retained()) {
                        CHECK(output.count <= input.Count());
                        if (input.NoRecovery()) { CHECK(output.count == 0); }
                    }
                }
            }
        }
        CHECK(mass <= content.GetItem(recipe.target).massGrams);
    }
}

template<class T> T Take(WorldResult<T> value) { REQUIRE(value); return eastl::move(*value); }
struct ExpansionRig {
    const ContentCatalog& content = ExpansionContent();
    Server server{content};
    ExpansionRig() {
        REQUIRE(server.GetWorld().DestroyActor(server.GetWorld().ActorAt({5, 5})));
        auto pacing = server.GetPacing(); pacing.rateLimited = true; pacing.ticksPerSecond = 100; REQUIRE(server.SetPacing(pacing));
    }
    ItemHandle Add(const char* name) { return Take(server.GetWorld().CreateItem(content.FindItem(name), ItemLocation::Carried(server.GetPlayer()))); }
    unsigned Count(ItemTypeId type) const {
        unsigned count = 0; const auto& world = server.GetWorld();
        for (const auto item : world.GetActor(server.GetPlayer())->inventory) { count += world.GetItem(item)->type == type; }
        return count;
    }
    void Finish(ItemHandle job) {
        std::uint32_t rng = 1;
        CHECK(AdvanceUntilInput(server.GetWorld(), rng, server.GetWorld().GetTick() + 10000).stop == SimulationStop::InputRequired);
        CHECK_FALSE(server.GetWorld().GetItem(job)); REQUIRE(server.GetWorld().CheckInvariants());
    }
};

TEST_CASE("Item expansion crafts cross batch storage and spear with retained tools", "[item-expansion][crafting][world]") {
    for (const char* name : {"make_tool_belt", "make_steel_spear"}) {
        CAPTURE(name); ExpansionRig r; auto& world = r.server.GetWorld(); const auto actor = r.server.GetPlayer();
        for (const char* skill : {"tailoring", "fabrication", "mechanics"}) { REQUIRE(world.Learning().SetLevel(actor, r.content.FindSkill(skill), 3)); }
        const auto id = r.content.FindCraftingRecipe(name); REQUIRE(id.value);
        const auto& recipe = r.content.GetCraftingRecipe(id);
        Vector<ItemHandle> materials, tools;
        for (const auto& input : recipe.inputs) {
            for (unsigned n = 0; n < input.Count(); ++n) {
                auto handle = Take(world.CreateItem(input.Item(), ItemLocation::Carried(actor)));
                if (input.Retained()) { tools.push_back(handle); } else { materials.push_back(handle); }
            }
        }
        REQUIRE_FALSE(tools.empty());
        REQUIRE(world.SetTile({7, 7}, {r.content.FindTerrain("floor"), 0}));
        REQUIRE(world.TransferItem(tools.back(), ItemLocation::Ground({7, 7})));
        CHECK(Take(world.StartCrafting(actor, id)).outcome == CraftingOutcome::MissingTools);
        for (const auto material : materials) { REQUIRE(world.GetItem(material)); CHECK_FALSE(world.IsReservedItem(material)); }
        REQUIRE(world.TransferItem(tools.back(), ItemLocation::Carried(actor)));
        const auto started = Take(world.StartCrafting(actor, id)); REQUIRE(started.outcome == CraftingOutcome::Started); r.Finish(started.unfinished);
        for (const auto material : materials) { CHECK_FALSE(world.GetItem(material)); }
        for (const auto tool : tools) { REQUIRE(world.GetItem(tool)); }
        CHECK(r.Count(recipe.result.item) == recipe.result.count);
        if (eastl::string_view(name) == "make_tool_belt") {
            ItemHandle belt{};
            for (const auto item : world.GetActor(actor)->inventory) { if (world.GetItem(item)->type == recipe.result.item) { belt = item; } }
            REQUIRE(world.GetItem(belt));
            const auto* storage = ItemContainer(r.content.GetItem(recipe.result.item)); REQUIRE(storage); CHECK(storage->capacityVolumeMl > 0);
            const auto& armor = r.content.GetItem(recipe.result.item).armor; REQUIRE(armor);
            bool torso = false;
            for (const auto& region : armor->regions) {
                if (region.region == ContactRegion::Torso) {
                    torso = true; CHECK(region.coverage > 0); CHECK(region.coverage < ratioScale); CHECK(region.encumbrance > 0);
                }
            }
            CHECK(torso);
            REQUIRE(world.WearItem(actor, belt).outcome == InventoryOutcome::Succeeded);
            const auto small = r.Add("resistor"); REQUIRE(world.TransferItem(small, ItemLocation::Inside(belt)));
            const auto large = r.Add("camping_tent"); CHECK_FALSE(world.TransferItem(large, ItemLocation::Inside(belt)));
            REQUIRE(world.CheckInvariants());
        }
    }
}

TEST_CASE("Item expansion donor dismantling yields actual components and retains tools", "[item-expansion][recycling][world]") {
    ExpansionRig r; auto& world = r.server.GetWorld(); const auto actor = r.server.GetPlayer();
    const auto target = r.Add("toaster"); const auto screwdriver = r.Add("flathead_screwdriver");
    const auto recipeId = r.content.FindRecyclingRecipe("dismantle_toaster"); REQUIRE(recipeId.value);
    CHECK(Take(world.StartRecycling(actor, target, recipeId)).outcome == CraftingOutcome::MissingTools);
    const auto pliers = r.Add("combination_pliers");
    const auto started = Take(world.StartRecycling(actor, target, recipeId)); REQUIRE(started.outcome == CraftingOutcome::Started); r.Finish(started.unfinished);
    CHECK_FALSE(world.GetItem(target)); REQUIRE(world.GetItem(screwdriver)); REQUIRE(world.GetItem(pliers));
    const auto& recipe = r.content.GetRecyclingRecipe(recipeId);
    REQUIRE_FALSE(recipe.returns.Empty());
    for (const auto& output : recipe.returns) { CHECK(r.Count(output.item) == output.count); }
}
}
