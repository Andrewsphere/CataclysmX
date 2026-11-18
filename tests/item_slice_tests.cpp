#include "content/content.h"
#include "core/server.h"
#include "core/simulation.h"
#include "skills/learning_system.h"
#include "worldgen/worldgen.h"

#include <catch2/catch_test_macros.hpp>

#include <initializer_list>

namespace {
using namespace cataclysmx;

constexpr const char* sliceIds[] = {
    "steel_scrap", "copper_wire", "copper_tube", "steel_sheet", "aluminum_scrap",
    "rigid_plastic_scrap", "corrugated_cardboard", "glass_shard", "rubber_hose",
    "cloth_patch", "timber_plank", "transistor", "diode", "microcontroller_board",
    "desktop_motherboard", "tiny_electric_motor", "appliance_electric_motor",
    "traction_electric_motor", "servo", "water_pump",
    "adjustable_wrench", "combination_pliers", "hacksaw", "flathead_screwdriver",
    "soldering_iron", "metal_file", "chef_knife", "pry_bar", "cotton_tshirt",
    "denim_jeans", "rain_jacket", "leather_work_gloves", "safety_goggles", "fire_axe",
    "police_baton", "pump_shotgun", "shotgun_shells", "coil_spear", "cloth_repair_tape",
    "camping_backpack", "canned_beans", "dill_pickles", "potato_chips", "chocolate_bar",
    "instant_noodles", "long_grain_rice", "peanut_butter", "bottled_water", "antiseptic",
    "sterile_gauze", "cloth_bandage", "bar_soap", "sewing_supplies", "nylon_rope",
    "camping_tarp", "steel_water_bottle", "plastic_storage_box", "aa_alkaline_battery",
    "aa_rechargeable_battery", "battery_charger",
};
static_assert(sizeof(sliceIds) / sizeof(sliceIds[0]) == 60);

constexpr const char* retainedLegacy[] = {
    "rock", "stone_marker", "backpack", "bandage", "scrap", "metal_pipe", "wood_plank",
    "blinking_lightbulb", "toggle_lightbulb", "single_blink_lightbulb",
    "disposable_battery", "rechargeable_battery", "field_charger", "door_controller",
    "utility_controller", "clean_rag", "house_bandage",
    "wood_handle", "wire_bundle", "scrap_knife", "electrical_cord", "work_trousers",
};
constexpr const char* removedLegacy[] = {
    "kitchen_knife", "canned_food", "soap", "nails", "screwdriver", "cooking_pot", "hand_saw",
    "dry_pasta", "rice_packet", "cereal_box", "snack_packet", "coffee_tin", "tea_box",
    "cooking_oil", "flour_bag", "frying_pan", "serving_spoon", "dish_towel",
    "detergent_box", "scrub_brush", "plastic_bucket", "gauze_roll", "medical_gloves",
    "medical_scissors", "wrench", "pliers", "utility_knife", "bolts", "duct_tape",
    "circuit_board", "apron", "work_gloves", "paperback_book", "reference_book",
    "notebook", "pencil", "paper_stack", "plastic_bottle", "canteen", "rope_coil",
    "sewing_kit", "toy_blocks", "first_aid_pouch", "parts_box",
};
static_assert(sizeof(retainedLegacy) / sizeof(retainedLegacy[0]) == 22);
static_assert(sizeof(removedLegacy) / sizeof(removedLegacy[0]) == 44);

const ContentCatalog& SliceContent() {
    static const ContentCatalog content = [] {
        Diagnostics diagnostics;
        auto loaded = LoadContentPack(CATACLYSMX_CONTENT_PACK, &diagnostics);
        INFO(FormatDiagnostics(diagnostics).c_str());
        REQUIRE(loaded);
        REQUIRE(ValidateWorldgenContent(*loaded, &diagnostics));
        INFO(FormatDiagnostics(diagnostics).c_str());
        REQUIRE(diagnostics.Empty());
        return eastl::move(*loaded);
    }();
    return content;
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
        for (const auto& basement : pattern.basements) {
            if (contains(basement.population)) { return true; }
        }
    }
    return false;
}

bool Current(const ContentCatalog& content, ItemTypeId item) {
    return !content.GetItem(item).flags.Contains(content.FindFlag("obsolete"));
}

TEST_CASE("Town slice publishes sixty complete current definitions and retires the old catalog", "[item-slice][content]") {
    const auto& content = SliceContent();
    const auto obsolete = content.FindFlag("obsolete");
    REQUIRE(obsolete.value);
    const auto& flag = content.GetFlag(obsolete);
    CHECK(flag.name == "Obsolete");
    CHECK(flag.kind == FlagKind::Category);
    CHECK(flag.domains.Contains(FlagDomain::Item));
    CHECK_FALSE(flag.domains.Contains(FlagDomain::Monster));
    CHECK_FALSE(flag.domains.Contains(FlagDomain::Terrain));
    CHECK_FALSE(flag.domains.Contains(FlagDomain::Object));
    CHECK_FALSE(flag.description.empty());
    CHECK(content.ItemCount() == 178); // Previous 98 plus 80 expansion items.
    for (const char* name : sliceIds) {
        CAPTURE(name);
        const auto type = content.FindItem(name);
        REQUIRE(type.value);
        const auto& item = content.GetItem(type);
        CHECK(Current(content, type));
        CHECK_FALSE(item.description.empty());
        CHECK_FALSE(item.flags.Empty());
        REQUIRE_FALSE(item.art.lines.empty());
        CHECK(item.art.width > 0);
        CHECK(item.art.width <= 32);
        CHECK(item.art.lines.size() <= 32);
        bool colored = false;
        for (const auto& line : item.art.lines) {
            for (const auto& run : line) { colored |= !run.color.useDefault; }
        }
        CHECK(colored);
    }
    for (const char* name : retainedLegacy) {
        CAPTURE(name);
        const auto type = content.FindItem(name);
        REQUIRE(type.value);
        CHECK_FALSE(Current(content, type));
    }
    for (const char* name : removedLegacy) {
        CAPTURE(name);
        CHECK(content.FindItem(name).value == 0);
    }
    for (const char* name : {"wood_scrap", "claw_hammer", "glass_bottle", "work_shirt", "arming_sword"}) {
        CAPTURE(name);
        CHECK(Current(content, content.FindItem(name)));
    }
    CHECK(content.FindLootPool("loot_books").value == 0);
    for (std::uint32_t i = 1; i <= content.LootPoolCount(); ++i) {
        const auto& pool = content.GetLootPool({i});
        for (const auto& entry : pool.entries) {
            if (entry.kind != LootEntryKind::Item || Current(content, entry.item)) { continue; }
            CAPTURE(pool.id.c_str(), content.GetItem(entry.item).id.c_str());
            bool dependency = false;
            for (const char* name : {"clean_rag", "wood_plank", "scrap",
                     "electrical_cord", "wire_bundle", "work_trousers"}) {
                dependency |= entry.item == content.FindItem(name);
            }
            CHECK(dependency);
        }
    }
    unsigned obsoleteCount = 0, servoCount = 0, pumpCount = 0;
    for (std::uint32_t i = 1; i <= content.ItemCount(); ++i) {
        const auto& item = content.GetItem({i});
        obsoleteCount += item.flags.Contains(obsolete);
        servoCount += item.id.find("servo") != String::npos;
        pumpCount += item.id.find("water_pump") != String::npos;
    }
    CHECK(obsoleteCount == 22);
    CHECK(servoCount == 1);
    CHECK(pumpCount == 1);
}

TEST_CASE("Town slice acquisition reaches every new item and all current recipe prerequisites", "[item-slice][content][crafting][worldgen]") {
    const auto& content = SliceContent();
    Vector<bool> reachable(content.ItemCount() + 1, false);
    for (std::uint32_t i = 1; i <= content.ItemCount(); ++i) {
        reachable[i] = Current(content, {i}) && Placed(content, {i});
    }
    CHECK_FALSE(reachable[content.FindItem("coil_spear").value]);
    for (const char* name : sliceIds) {
        CAPTURE(name);
        if (eastl::string_view(name) != "coil_spear") { CHECK(reachable[content.FindItem(name).value]); }
    }
    // Resolve only current-only routes, including traits supplied by obtainable tools.
    bool changed = true;
    while (changed) {
        changed = false;
        for (std::uint32_t i = 1; i <= content.CraftingRecipeCount(); ++i) {
            const auto& recipe = content.GetCraftingRecipe({i});
            if (!recipe.enabled || reachable[recipe.result.item.value]) { continue; }
            bool ready = true;
            for (const auto& input : recipe.inputs) { ready &= reachable[input.Item().value]; }
            for (const auto& required : recipe.toolTraits) {
                bool tool = false;
                for (std::uint32_t j = 1; j <= content.ItemCount(); ++j) {
                    if (!reachable[j]) { continue; }
                    for (const auto& provided : content.GetItem({j}).toolTraits) {
                        tool |= provided.trait == required.trait && provided.tier >= required.tier;
                    }
                }
                ready &= tool;
            }
            if (ready && Current(content, recipe.result.item)) {
                reachable[recipe.result.item.value] = true;
                changed = true;
            }
        }
    }
    for (const char* name : sliceIds) { CAPTURE(name); CHECK(reachable[content.FindItem(name).value]); }
    CHECK(reachable[content.FindItem("wood_scrap").value]);
    CHECK(reachable[content.FindItem("arming_sword").value]);
    for (const char* name : {"cut_cloth_bandage", "cut_timber_scrap", "make_current_arming_sword", "make_coil_spear"}) {
        CAPTURE(name);
        const auto id = content.FindCraftingRecipe(name);
        REQUIRE(id.value);
        const auto& recipe = content.GetCraftingRecipe(id);
        CHECK(Current(content, recipe.result.item));
        std::uint64_t inputMass = 0;
        for (const auto& input : recipe.inputs) {
            CHECK(Current(content, input.Item()));
            CHECK(reachable[input.Item().value]);
            if (!input.Retained()) { inputMass += std::uint64_t(content.GetItem(input.Item()).massGrams) * input.Count(); }
        }
        CHECK(inputMass >= std::uint64_t(content.GetItem(recipe.result.item).massGrams) * recipe.result.count);
    }
    // Only these historical routes may still name obsolete inputs. New routes
    // cannot acquire that exception just by choosing a different recipe ID.
    for (std::uint32_t i = 1; i <= content.CraftingRecipeCount(); ++i) {
        const auto& recipe = content.GetCraftingRecipe({i});
        bool legacy = false;
        for (const char* name : {"shape_stone_marker", "cut_house_bandage", "carve_wood_handle",
                 "salvage_wire_bundle", "make_scrap_knife", "cut_wood_scrap", "make_salvaged_arming_sword"}) {
            legacy |= recipe.id == name;
        }
        if (legacy) { continue; }
        CAPTURE(recipe.id.c_str());
        CHECK(Current(content, recipe.result.item));
        for (const auto& input : recipe.inputs) { CHECK(Current(content, input.Item())); }
    }
}

TEST_CASE("Town slice motor scales and placement keep traction assemblies on garage ground", "[item-slice][content][worldgen]") {
    const auto& content = SliceContent();
    const auto& tiny = content.GetItem(content.FindItem("tiny_electric_motor"));
    const auto& appliance = content.GetItem(content.FindItem("appliance_electric_motor"));
    const auto traction = content.FindItem("traction_electric_motor");
    const auto& motor = content.GetItem(traction);
    CHECK(tiny.massGrams == 20); CHECK(tiny.volumeMl == 30);
    CHECK(appliance.massGrams == 4000); CHECK(appliance.volumeMl == 3500);
    CHECK(motor.massGrams == 60000); CHECK(motor.volumeMl == 45000);
    const auto garageId = content.FindBuildingPattern("auto_repair");
    unsigned motorMarkers = 0;
    for (std::uint32_t i = 1; i <= content.BuildingPatternCount(); ++i) {
        const auto& pattern = content.GetBuildingPattern({i});
        const auto check = [&](const PatternPopulation& population) {
            for (const auto& marker : population.loot) {
                if (!PoolYields(content, marker.pool, traction)) { continue; }
                ++motorMarkers;
                CHECK(BuildingPatternId{i} == garageId);
                CHECK(marker.destination == LootDestination::Ground);
                CHECK(marker.position == TileCoord{5, 2, 0});
                CHECK(marker.chance == 10000); CHECK(marker.rolls == 1);
            }
        };
        check(pattern.population);
        for (const auto& basement : pattern.basements) { check(basement.population); }
    }
    CHECK(motorMarkers == 1);
    // Also exercise the production generator rather than only inspecting a pool.
    bool generatedMotor = false;
    ChunkPlacements placements;
    for (std::uint32_t seed = 0; seed < 64 && !generatedMotor; ++seed) {
        const auto plan = GenerateRegionPlan(content, seed, {0, 0});
        for (const auto& building : plan.buildings) {
            if (building.pattern != garageId) { continue; }
            const auto first = ChunkOf(building.bounds.min);
            const auto last = ChunkOf({building.bounds.max.x - 1, building.bounds.max.z - 1});
            for (int z = first.z; z <= last.z; ++z) {
                for (int x = first.x; x <= last.x; ++x) {
                    GenerateChunkPlacements(content, seed, plan, {x, z, 0}, placements);
                    for (const auto& item : placements.items) {
                        if (item.type != traction) { continue; }
                        CHECK(item.destination == LootDestination::Ground);
                        CHECK(item.key.owner == building.owner);
                        generatedMotor = true;
                    }
                }
            }
        }
    }
    CHECK(generatedMotor);
}

template<class T> T Take(WorldResult<T> value) { REQUIRE(value); return eastl::move(*value); }

struct SliceRig {
    const ContentCatalog& content = SliceContent();
    Server server{content};
    SliceRig() {
        REQUIRE(server.GetWorld().DestroyActor(server.GetWorld().ActorAt({5, 5})));
        auto pacing = server.GetPacing();
        pacing.rateLimited = true; pacing.ticksPerSecond = 100;
        REQUIRE(server.SetPacing(pacing));
    }
    ItemHandle Add(const char* name) {
        return Take(server.GetWorld().CreateItem(content.FindItem(name), ItemLocation::Carried(server.GetPlayer())));
    }
    unsigned Count(ItemTypeId type) const {
        unsigned count = 0;
        const auto& world = server.GetWorld();
        for (const auto item : world.GetActor(server.GetPlayer())->inventory) { count += world.GetItem(item)->type == type; }
        return count;
    }
    CraftingResult Start(const char* name) {
        return Take(server.GetWorld().StartCrafting(server.GetPlayer(), content.FindCraftingRecipe(name)));
    }
    void Finish(ItemHandle job) {
        std::uint32_t rng = 1;
        const auto advanced = AdvanceUntilInput(server.GetWorld(), rng, server.GetWorld().GetTick() + 10000);
        CHECK(advanced.stop == SimulationStop::InputRequired);
        CHECK_FALSE(server.GetWorld().GetItem(job));
        REQUIRE(server.GetWorld().CheckInvariants());
    }
};

TEST_CASE("Current cloth timber and sword recipes consume exact counts and keep real tools", "[item-slice][crafting][world]") {
    struct Route { const char* recipe; const char* material; unsigned count; const char* result; unsigned output; };
    const Route routes[] = {
        {"cut_cloth_bandage", "cloth_patch", 1, "cloth_bandage", 2},
        {"cut_timber_scrap", "timber_plank", 1, "wood_scrap", 10},
        {"make_current_arming_sword", "steel_scrap", 14, "arming_sword", 1},
    };
    for (const auto& route : routes) {
        CAPTURE(route.recipe);
        SliceRig r;
        auto& world = r.server.GetWorld();
        for (unsigned i = 0; i < route.count + 1; ++i) { r.Add(route.material); }
        const bool sword = eastl::string_view(route.result) == "arming_sword";
        if (sword) {
            REQUIRE(world.Learning().SetLevel(r.server.GetPlayer(), r.content.FindSkill("fabrication"), 3));
            r.Add("wood_scrap"); r.Add("wood_scrap"); r.Add("copper_wire");
        }
        CHECK(r.Start(route.recipe).outcome == CraftingOutcome::MissingTools);
        CHECK(r.Count(r.content.FindItem(route.material)) == route.count + 1);
        const auto saw = r.Add("hacksaw");
        ItemHandle hammer{};
        if (sword) {
            CHECK(r.Start(route.recipe).outcome == CraftingOutcome::MissingTools);
            hammer = r.Add("claw_hammer");
        }
        const auto started = r.Start(route.recipe);
        REQUIRE(started.outcome == CraftingOutcome::Started);
        r.Finish(started.unfinished);
        CHECK(r.Count(r.content.FindItem(route.material)) == 1);
        CHECK(r.Count(r.content.FindItem(route.result)) == route.output);
        REQUIRE(world.GetItem(saw));
        if (sword) {
            REQUIRE(world.GetItem(hammer));
            CHECK(r.Count(r.content.FindItem("wood_scrap")) == 0);
            CHECK(r.Count(r.content.FindItem("copper_wire")) == 0);
        }
    }
}

TEST_CASE("Current coil spear needs both retained tools and consumes only its four materials", "[item-slice][crafting][world]") {
    SliceRig r;
    auto& world = r.server.GetWorld();
    REQUIRE(world.Learning().SetLevel(r.server.GetPlayer(), r.content.FindSkill("fabrication"), 3));
    Vector<ItemHandle> materials;
    for (const char* name : {"timber_plank", "steel_scrap", "copper_wire", "cloth_repair_tape"}) { materials.push_back(r.Add(name)); }
    const auto knife = r.Add("chef_knife"); // A generic cutting tier alone cannot replace the specified saw.
    CHECK(r.Start("make_coil_spear").outcome == CraftingOutcome::MissingTools);
    const auto saw = r.Add("hacksaw");
    CHECK(r.Start("make_coil_spear").outcome == CraftingOutcome::MissingTools);
    for (const auto material : materials) { REQUIRE(world.GetItem(material)); CHECK_FALSE(world.IsReservedItem(material)); }
    const auto pliers = r.Add("combination_pliers");
    const auto started = r.Start("make_coil_spear");
    REQUIRE(started.outcome == CraftingOutcome::Started);
    r.Finish(started.unfinished);
    for (const auto material : materials) { CHECK_FALSE(world.GetItem(material)); }
    for (const auto tool : {knife, saw, pliers}) {
        REQUIRE(world.GetItem(tool));
        CHECK(world.GetItem(tool)->location == ItemLocation::Carried(r.server.GetPlayer()));
    }
    CHECK(r.Count(r.content.FindItem("coil_spear")) == 1);
}

TEST_CASE("Current recyclers preserve mass and produce usable current stock with retained tools", "[item-slice][recycling][world]") {
    const auto& content = SliceContent();
    for (const char* name : {"cut_up_cotton_tshirt", "cut_up_denim_jeans", "cut_steel_sheet", "salvage_appliance_motor", "salvage_water_pump", "disassemble_coil_spear"}) {
        CAPTURE(name);
        const auto recipeId = content.FindRecyclingRecipe(name);
        REQUIRE(recipeId.value);
        const auto& recipe = content.GetRecyclingRecipe(recipeId);
        CHECK(Current(content, recipe.target));
        std::uint64_t outputMass = 0;
        for (const auto& output : recipe.returns) {
            CHECK(Current(content, output.item));
            outputMass += std::uint64_t(content.GetItem(output.item).massGrams) * output.count;
        }
        CHECK(outputMass > 0);
        CHECK(outputMass <= content.GetItem(recipe.target).massGrams);
        CHECK(content.FindRecyclingFor(recipe.target) == recipeId);
        SliceRig r;
        auto& world = r.server.GetWorld();
        const auto target = Take(world.CreateItem(recipe.target, ItemLocation::Carried(r.server.GetPlayer())));
        CHECK(Take(world.StartRecycling(r.server.GetPlayer(), target, recipeId)).outcome == CraftingOutcome::MissingTools);
        Vector<ItemHandle> tools;
        for (const auto& input : recipe.inputs) {
            CHECK(Current(content, input.Item()));
            REQUIRE(input.Retained());
            for (unsigned i = 0; i < input.Count(); ++i) {
                tools.push_back(Take(world.CreateItem(input.Item(), ItemLocation::Carried(r.server.GetPlayer()))));
            }
        }
        if (!recipe.toolTraits.Empty()) { tools.push_back(r.Add("hacksaw")); }
        const auto started = Take(world.StartRecycling(r.server.GetPlayer(), target, recipeId));
        REQUIRE(started.outcome == CraftingOutcome::Started);
        r.Finish(started.unfinished);
        CHECK_FALSE(world.GetItem(target));
        for (const auto tool : tools) { REQUIRE(world.GetItem(tool)); }
        for (const auto& output : recipe.returns) { CHECK(r.Count(output.item) == output.count); }
    }
}

TEST_CASE("Current batteries charge and current containers enforce authored capacity", "[item-slice][items][world]") {
    SliceRig r;
    auto& world = r.server.GetWorld();
    const auto actor = r.server.GetPlayer();
    const auto charger = r.Add("battery_charger");
    const auto battery = r.Add("aa_rechargeable_battery");
    const auto alkaline = r.Add("aa_alkaline_battery");
    const auto& chargerDefinition = r.content.GetItem(r.content.FindItem("battery_charger"));
    const auto capability = FindItemCapability(chargerDefinition, "charger");
    const auto power = FindItemCapability(chargerDefinition, "power");
    const auto cell = FindItemCapability(r.content.GetItem(r.content.FindItem("aa_rechargeable_battery")), "power");
    CHECK(world.GetItemEnergy(battery, cell)->energy == 0);
    CHECK(world.UseItem(actor, charger, {capability, "install", alkaline}).outcome == ItemUseOutcome::Refused);
    REQUIRE(world.UseItem(actor, charger, {capability, "install", battery}).outcome == ItemUseOutcome::Succeeded);
    REQUIRE(world.UseItem(actor, charger, {capability, "start"}).outcome == ItemUseOutcome::Succeeded);
    world.ElapseTicks(100);
    world.ProcessItemEvents();
    CHECK(world.GetItemEnergy(battery, cell)->energy == 3);
    CHECK(world.GetItemEnergy(charger, power)->energy == 1997);
    const auto box = r.Add("plastic_storage_box");
    const auto bottle = r.Add("steel_water_bottle");
    const auto pack = r.Add("camping_backpack");
    CHECK(ItemContainer(r.content.GetItem(r.content.FindItem("plastic_storage_box")))->capacityVolumeMl == 2000);
    CHECK(ItemContainer(r.content.GetItem(r.content.FindItem("steel_water_bottle")))->capacityVolumeMl == 1000);
    CHECK(ItemContainer(r.content.GetItem(r.content.FindItem("camping_backpack")))->capacityVolumeMl == 20000);
    // A full-size pump fits the pack but neither small container; sizes stay physical.
    const auto pump = r.Add("water_pump");
    CHECK_FALSE(world.TransferItem(pump, ItemLocation::Inside(box)));
    CHECK_FALSE(world.TransferItem(pump, ItemLocation::Inside(bottle)));
    REQUIRE(world.TransferItem(pump, ItemLocation::Inside(pack)));
    const auto motor = r.Add("traction_electric_motor");
    CHECK_FALSE(world.TransferItem(motor, ItemLocation::Inside(pack)));
    REQUIRE(world.CheckInvariants());
}

TEST_CASE("All shipped manifests independently load the shared obsolete vocabulary", "[item-slice][content][manifest]") {
    for (const char* pack : {CATACLYSMX_TERRAIN_FILE, CATACLYSMX_FIRST_HOUSE_PACK, CATACLYSMX_ZOMBIE_SHOWCASE_PACK}) {
        CAPTURE(pack);
        Diagnostics diagnostics;
        const auto loaded = LoadContentPack(pack, &diagnostics);
        INFO(FormatDiagnostics(diagnostics).c_str());
        REQUIRE(loaded);
        REQUIRE(ValidateServerContent(*loaded, &diagnostics));
        REQUIRE(diagnostics.Empty());
        CHECK(loaded->GetItem(loaded->FindItem("rock")).flags.Contains(loaded->FindFlag("obsolete")));
    }
}
}
