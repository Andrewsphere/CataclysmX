#include "content/content.h"
#include "core/world.h"
#include "core/save_database.h"
#include "crafting/crafting_state.h"
#include "power/power_system.h"
#include "skills/learning_system.h"
#include "skills/skill_state.h"
#include "worldgen/worldgen.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <initializer_list>
#include <string>

namespace {
using namespace cataclysmx;
template<class T, class E> T Take(eastl::expected<T, E> value) {
    REQUIRE(value); return eastl::move(*value);
}

const ContentCatalog& WorkshopContent() {
    static const ContentCatalog content = [] {
        Diagnostics diagnostics;
        auto loaded = LoadContentPack(CATACLYSMX_CONTENT_PACK, &diagnostics);
        INFO(FormatDiagnostics(diagnostics).c_str());
        REQUIRE(loaded); REQUIRE(diagnostics.Empty());
        return eastl::move(*loaded);
    }();
    return content;
}

constexpr const char* newItems[] = {
    "solid_steel_block", "refractory_brick", "charcoal_charge", "crude_anvil_kit",
    "charcoal_forge_kit", "forging_tongs", "smithing_hammer", "fabricated_anvil_kit",
    "spring_steel_bar", "tool_steel_blank", "wooden_workshop_crate", "sunglasses",
    "improvised_welding_goggles", "welding_filter", "welding_mask",
    "salvaged_welder_power_module", "electrical_lead_set", "makeshift_arc_welder",
    "arc_welder", "welding_electrode_packet", "welded_workshop_rack_kit",
    "workshop_battery_pack"
};
struct LearningLink { const char* recipe; const char* skill; std::uint8_t level; };
constexpr LearningLink learningLinks[] = {
    {"assemble_crude_anvil", "fabrication", 1},
    {"assemble_charcoal_forge", "fabrication", 2},
    {"forge_tongs", "fabrication", 2},
    {"cut_tool_steel_blanks", "fabrication", 2},
    {"make_wooden_workshop_crate", "fabrication", 2},
    {"forge_pry_bar", "fabrication", 2},
    {"forge_smithing_hammer", "fabrication", 3},
    {"assemble_fabricated_anvil", "fabrication", 3},
    {"forge_wood_chisel", "fabrication", 3},
    {"assemble_electrical_lead_set", "electronics", 1},
    {"assemble_soldering_iron", "electronics", 2},
    {"assemble_workshop_battery_pack", "electronics", 2},
    {"assemble_makeshift_arc_welder", "electronics", 3},
    {"assemble_improvised_welding_goggles", "fabrication", 1},
    {"assemble_welding_mask", "fabrication", 2},
    {"weld_workshop_rack", "fabrication", 2},
    {"weld_fabricated_anvil", "fabrication", 3}
};

template<class Traits>
unsigned Tier(const ContentCatalog& content, const Traits& traits, const char* name) {
    const auto id = content.FindToolTrait(name);
    REQUIRE(id.value != 0);
    for (const auto& trait : traits) { if (trait.trait == id) { return trait.tier; } }
    return 0;
}
template<class Recipe>
void CheckLearning(const ContentCatalog& content, const Recipe& recipe, const char* name,
    std::uint8_t level) {
    const auto skill = content.FindSkill(name); REQUIRE(skill.value != 0);
    bool gated = false, trained = false;
    for (const auto& gate : recipe.skills) { gated |= gate.skill == skill && gate.level == level; }
    for (const auto& source : recipe.training) {
        trained |= source.skill == skill && source.level == level && source.xpPerWork > 0;
    }
    if (level == 0) {
        for (const auto& gate : recipe.skills) { CHECK(gate.level == 0); }
    } else {
        CHECK(gated);
    }
    CHECK(trained);
}
const CraftingRecipeDefinition& Recipe(const ContentCatalog& content, const char* name) {
    const auto id = content.FindCraftingRecipe(name); REQUIRE(id.value != 0);
    return content.GetCraftingRecipe(id);
}
RecyclingRecipeId Recycler(const ContentCatalog& content, ItemTypeId target) {
    RecyclingRecipeId found{};
    for (std::uint32_t i = 1; i <= content.RecyclingRecipeCount(); ++i) {
        const RecyclingRecipeId id{i};
        if (content.GetRecyclingRecipe(id).target != target) { continue; }
        REQUIRE(found.value == 0); found = id;
    }
    return found;
}

// Use the real authored catalog and runtime, but supply scavenged raw stock in
// a cleared work area. This is a construction-route test, not a loot playthrough.
struct Rig {
    const ContentCatalog& content = WorkshopContent();
    World world{content};
    ActorHandle actor{};
    ObjectHandle forge{}, grid{};
    ItemHandle gridBattery{};
    Rig() {
        Take(world.RequestChunks({{0, 0}, 0}, LoadMode::active));
        REQUIRE(world.ReconcileChunks());
        Vector<ItemHandle> groundItems;
        for (int z = 0; z < 8; ++z) { for (int x = 0; x < 8; ++x) {
            const TileCoord position{x, z};
            world.CollectItemsAt(position, groundItems);
            for (const auto item : groundItems) { REQUIRE(world.DestroyItem(item)); }
            if (const auto object = world.ObjectAt(position); object.value) { REQUIRE(world.DestroyObject(object)); }
            if (const auto occupant = world.ActorAt(position); occupant.value) { REQUIRE(world.DestroyActor(occupant)); }
            REQUIRE(world.SetTile(position, {content.FindTerrain("floor")}));
        } }
        actor = Take(world.CreateActor({2, 2}, true));
    }
    ItemHandle Add(ItemTypeId type) {
        REQUIRE(type.value != 0); return Take(world.CreateItem(type, ItemLocation::Carried(actor)));
    }
    ItemHandle Add(const char* name) { return Add(content.FindItem(name)); }
    ItemHandle Find(ItemTypeId type) const {
        for (const auto item : world.GetActor(actor)->inventory) {
            if (world.GetItem(item)->type == type && !world.IsReservedItem(item)) { return item; }
        }
        return {};
    }
    ItemHandle Find(const char* name) const { return Find(content.FindItem(name)); }
    unsigned Count(ItemTypeId type) const {
        unsigned count = 0;
        for (const auto item : world.GetActor(actor)->inventory) {
            count += world.GetItem(item)->type == type && !world.IsReservedItem(item);
        }
        return count;
    }
    void Level(const char* name, std::uint8_t level) {
        REQUIRE(world.Learning().SetLevel(actor, content.FindSkill(name), level));
    }
    std::uint8_t Level(const char* name) const {
        return LearnedSkillLevel(world.GetActor(actor)->learning, content.FindSkill(name));
    }
    std::uint64_t Xp(const char* name) const {
        const auto* progress = FindSkillProgress(world.GetActor(actor)->learning, content.FindSkill(name));
        return progress ? progress->xp : 0;
    }
    void HandTools() {
        for (const auto name : {"claw_hammer", "combination_pliers", "hacksaw", "hand_drill",
             "adjustable_wrench", "flathead_screwdriver", "metal_file", "wood_chisel", "multimeter"}) {
            Add(name);
        }
    }
    void Inputs(const CraftingRecipeDefinition& recipe) {
        for (const auto& input : recipe.inputs) {
            for (unsigned count = Count(input.Item()); count < input.Count(); ++count) { Add(input.Item()); }
        }
    }
    ObjectHandle Place(ItemHandle equipment, TileCoord position) {
        REQUIRE(Take(world.PlaceStation(actor, equipment, position)).outcome == StationActionOutcome::Placed);
        const auto station = world.ObjectAt(position); REQUIRE(station.value != 0); return station;
    }
    void FillForge() {
        REQUIRE(forge.value != 0);
        auto state = world.DescribeObjectState(forge)->forge;
        const auto& config = content.GetObject(world.GetObject(forge)->type).forge;
        REQUIRE(config.fuels.size() == 1);
        while (config.fuelCapacity - state.fuel >= config.fuels[0].heat) {
            REQUIRE(Take(world.UseForge(actor, forge, ForgeAction::LoadFuel,
                Add(config.fuels[0].item))).outcome == StationActionOutcome::FuelLoaded);
            state = world.DescribeObjectState(forge)->forge;
        }
        if (!state.ready) {
            REQUIRE(Take(world.UseForge(actor, forge, ForgeAction::Ignite)).outcome == StationActionOutcome::Ignited);
        }
    }
    void Energy(ItemHandle item, std::uint32_t amount) {
        const auto& definition = content.GetItem(world.GetItem(item)->type);
        REQUIRE(definition.energyStores.size() == 1);
        REQUIRE(world.SetItemEnergy(item, definition.energyStores[0].capability, amount));
    }
    std::uint32_t Energy(ItemHandle item) const {
        const auto& definition = content.GetItem(world.GetItem(item)->type);
        REQUIRE(definition.energyStores.size() == 1);
        return world.GetItemEnergy(item, definition.energyStores[0].capability)->energy;
    }
    void Grid() {
        Take(world.Power().ExtendBounds({{1, 2}, {2, 2}}));
        grid = Take(world.CreateObject(content.FindObject("grid_battery_station"), {1, 2}));
        gridBattery = Add("workshop_battery_pack");
        REQUIRE(world.TransferItem(gridBattery, ItemLocation::Stored(grid)));
        Energy(gridBattery, 4000); world.Power().SyncObject(grid);
    }
    CraftingResult Start(const char* name) {
        return Take(world.StartCrafting(actor, content.FindCraftingRecipe(name)));
    }
    void Step(Tick ticks) { world.ElapseTicks(ticks); world.ProcessCraftingEvents(); }
    void Finish(ItemHandle job) {
        const auto totalWork = world.InspectCrafting(job)->totalWork;
        for (std::uint32_t slice = 0; slice <= totalWork + 1 && world.GetItem(job); ++slice) {
            const auto status = world.InspectCrafting(job)->status;
            if (status == CraftingStatus::ColdForge || status == CraftingStatus::InsufficientFuel) {
                FillForge();
                REQUIRE(Take(world.ResumeCrafting(actor, job)).outcome == CraftingOutcome::Resumed);
            }
            REQUIRE(world.InspectCrafting(job)->status == CraftingStatus::Working);
            if (gridBattery.value != 0) { Energy(gridBattery, 4000); }
            const auto ticks = world.InspectCrafting(job)->fundedTicksRemaining;
            REQUIRE(ticks > 0); Step(ticks);
        }
        REQUIRE_FALSE(world.GetItem(job)); REQUIRE(world.CheckInvariants());
    }
    ItemHandle Craft(const char* name) {
        INFO("recipe=" << name);
        const auto& recipe = Recipe(content, name); Inputs(recipe);
        if (forge.value != 0) { FillForge(); }
        const auto before = Count(recipe.result.item);
        const auto started = Start(name); REQUIRE(started.outcome == CraftingOutcome::Started);
        Finish(started.unfinished);
        REQUIRE(Count(recipe.result.item) == before + recipe.result.count);
        return Find(recipe.result.item);
    }
    void StashPracticeOutputs(ItemTypeId type) {
        const auto& definition = content.GetItem(type);
        const bool keepProvider = !definition.toolTraits.Empty() && definition.placedObject.value == 0;
        bool keptProvider = false;
        Vector<ItemHandle> toStash;
        for (const auto item : world.GetActor(actor)->inventory) {
            if (world.GetItem(item)->type != type || world.IsReservedItem(item)) { continue; }
            if (keepProvider && !keptProvider) { keptProvider = true; continue; }
            toStash.push_back(item);
        }
        // Transfer only after collecting handles: inventory removal changes its
        // storage. Preserve every batch output on supported, out-of-reach floor.
        for (const auto item : toStash) {
            REQUIRE(world.TransferItem(item, ItemLocation::Ground({7, 7})));
        }
    }
    void RecycleForPractice(const char* name) {
        INFO("recycler=" << name);
        const auto id = content.FindRecyclingRecipe(name); REQUIRE(id.value != 0);
        const auto& recipe = content.GetRecyclingRecipe(id);
        const auto target = Add(recipe.target);
        for (const auto& input : recipe.inputs) {
            for (unsigned count = Count(input.Item()); count < input.Count(); ++count) { Add(input.Item()); }
        }
        const auto started = Take(world.StartRecycling(actor, target, id));
        REQUIRE(started.outcome == CraftingOutcome::Started);
        Finish(started.unfinished);
        CHECK_FALSE(world.GetItem(target));
        for (const auto& output : recipe.returns) { StashPracticeOutputs(output.item); }
    }
    void LearnThroughWork(const char* skill, std::uint8_t target,
        std::initializer_list<const char*> recipes) {
        std::uint64_t work = 0;
        unsigned crafts = 0;
        while (Level(skill) < target && crafts < 512) {
            for (const auto name : recipes) {
                if (Level(skill) >= target) { break; }
                const auto beforeLevel = Level(skill);
                const auto beforeXp = Xp(skill);
                Craft(name); work += Recipe(content, name).work; ++crafts;
                REQUIRE((Level(skill) > beforeLevel || Xp(skill) > beforeXp));
                StashPracticeOutputs(Recipe(content, name).result.item);
            }
        }
        INFO("skill=" << skill << " target=" << unsigned(target)
            << " completed crafts=" << crafts << " authored base work=" << work);
        REQUIRE(Level(skill) == target);
    }
};

TEST_CASE("W3 shared roster loads in Town and FirstHouse with useful learning and bootstrap links",
    "[workshop][content][w3]") {
    for (const auto pack : {CATACLYSMX_CONTENT_PACK, CATACLYSMX_FIRST_HOUSE_PACK}) {
        INFO("pack=" << pack);
        Diagnostics diagnostics;
        const auto loaded = LoadContentPack(pack, &diagnostics);
        INFO(FormatDiagnostics(diagnostics).c_str()); REQUIRE(loaded); REQUIRE(diagnostics.Empty());
        const auto& content = *loaded;
        REQUIRE(ValidateWorldgenContent(content, &diagnostics));
        for (const auto name : newItems) { INFO(name); CHECK(content.FindItem(name).value != 0); }
        for (const auto& link : learningLinks) {
            INFO(link.recipe);
            const auto& recipe = Recipe(content, link.recipe);
            CHECK(recipe.enabled); CHECK(recipe.knownByDefault); CHECK(recipe.work > 0);
            CheckLearning(content, recipe, link.skill, link.level);
        }
        const auto& welder = Recipe(content, "assemble_makeshift_arc_welder");
        CheckLearning(content, welder, "mechanics", 2);
        for (const auto name : {"assemble_crude_anvil", "assemble_charcoal_forge"}) {
            const auto& recipe = Recipe(content, name);
            CHECK(Tier(content, recipe.toolTraits, "anvil") == 0);
            CHECK(Tier(content, recipe.toolTraits, "forge_heat") == 0);
            CHECK(Tier(content, recipe.toolTraits, "welding") == 0);
        }
        CHECK(Tier(content, Recipe(content, "assemble_soldering_iron").toolTraits, "soldering") == 0);
        CHECK(Tier(content, welder.toolTraits, "welding") == 0);
        for (const auto name : {"assemble_improvised_welding_goggles", "assemble_welding_mask"}) {
            CHECK(Tier(content, Recipe(content, name).toolTraits, "welding") == 0);
        }
        for (const auto name : {"dismantle_computer_power_supply", "salvage_water_pump"}) {
            const auto id = content.FindRecyclingRecipe(name); REQUIRE(id.value != 0);
            CheckLearning(content, content.GetRecyclingRecipe(id),
                eastl::string_view(name) == "salvage_water_pump" ? "mechanics" : "electronics", 1);
        }
        for (const auto name : {"assemble_wiring_kit", "solder_electrical_connectors"}) {
            CheckLearning(content, Recipe(content, name), "electronics", 0);
        }
    }
}

TEST_CASE("W3 every sword producer requires learned F4 continuous steel and all four forging gates",
    "[workshop][content][w3]") {
    const auto& content = WorkshopContent();
    unsigned producers = 0;
    for (std::uint32_t i = 1; i <= content.CraftingRecipeCount(); ++i) {
        const auto& recipe = content.GetCraftingRecipe({i});
        if (recipe.result.item != content.FindItem("arming_sword")) { continue; }
        INFO(recipe.id.c_str()); ++producers;
        CHECK(recipe.knownByDefault);
        bool fabrication4 = false, continuousBar = false;
        for (const auto& skill : recipe.skills) {
            fabrication4 |= skill.skill == content.FindSkill("fabrication") && skill.level == 4;
        }
        for (const auto& input : recipe.inputs) {
            continuousBar |= !input.Retained() && input.Item() == content.FindItem("spring_steel_bar");
            CHECK(input.Item() != content.FindItem("steel_scrap"));
            CHECK(input.Item() != content.FindItem("scrap"));
        }
        CHECK(fabrication4); CHECK(continuousBar); CHECK(recipe.heatPerWork > 0);
        REQUIRE(recipe.toolTraits.Size() == 4);
        CHECK(Tier(content, recipe.toolTraits, "anvil") == 2);
        CHECK(Tier(content, recipe.toolTraits, "hammering") == 2);
        CHECK(Tier(content, recipe.toolTraits, "metal_holding") == 2);
        CHECK(Tier(content, recipe.toolTraits, "forge_heat") == 1);
    }
    CHECK(producers == 2);
    REQUIRE(content.FindCraftingRecipe("make_current_arming_sword").value != 0);
    REQUIRE(content.FindCraftingRecipe("make_salvaged_arming_sword").value != 0);
    for (std::uint32_t i = 1; i <= content.CraftingRecipeCount(); ++i) {
        CHECK(content.GetCraftingRecipe({i}).result.item != content.FindItem("spring_steel_bar"));
    }
}

TEST_CASE("W3 product salvage conserves every producer budget and never refunds operating reagents",
    "[workshop][content][w3]") {
    const auto& content = WorkshopContent();
    for (const auto name : newItems) {
        INFO("new item=" << name);
        const auto target = content.FindItem(name); REQUIRE(target.value != 0);
        const auto recyclerId = Recycler(content, target);
        if (recyclerId.value == 0) { continue; }
        std::uint64_t returnedMass = 0;
        for (const auto& output : content.GetRecyclingRecipe(recyclerId).returns) {
            returnedMass += static_cast<std::uint64_t>(content.GetItem(output.item).massGrams) * output.count;
            for (const auto reagent : {"charcoal_charge", "welding_electrode_packet", "solder_wire"}) {
                CHECK(output.item != content.FindItem(reagent));
            }
        }
        CHECK(returnedMass <= content.GetItem(target).massGrams);
    }
    for (const auto reagent : {"charcoal_charge", "welding_electrode_packet"}) {
        CHECK(Recycler(content, content.FindItem(reagent)).value == 0);
    }
    for (const auto name : {"crude_anvil_kit", "charcoal_forge_kit", "forging_tongs",
        "smithing_hammer", "fabricated_anvil_kit", "tool_steel_blank", "wooden_workshop_crate",
        "pry_bar", "wood_chisel", "arming_sword", "improvised_welding_goggles", "welding_mask",
        "electrical_lead_set", "soldering_iron", "makeshift_arc_welder",
        "welded_workshop_rack_kit", "workshop_battery_pack"}) {
        INFO("product=" << name);
        const auto target = content.FindItem(name); REQUIRE(target.value != 0);
        const auto recyclerId = Recycler(content, target);
        if (eastl::string_view(name) == "arming_sword" && recyclerId.value == 0) { continue; }
        REQUIRE(recyclerId.value != 0);
        const auto& recycler = content.GetRecyclingRecipe(recyclerId);
        std::uint64_t recoveredMass = 0;
        for (const auto& output : recycler.returns) {
            recoveredMass += static_cast<std::uint64_t>(content.GetItem(output.item).massGrams) * output.count;
            for (const auto reagent : {"charcoal_charge", "welding_electrode_packet", "solder_wire"}) {
                CHECK(output.item != content.FindItem(reagent));
            }
        }
        unsigned producers = 0;
        for (std::uint32_t i = 1; i <= content.CraftingRecipeCount(); ++i) {
            const auto& producer = content.GetCraftingRecipe({i});
            if (producer.result.item != target) { continue; }
            INFO("producer=" << producer.id.c_str()); ++producers;
            std::uint64_t consumedMass = 0;
            for (const auto& input : producer.inputs) {
                if (!input.Retained()) {
                    consumedMass += static_cast<std::uint64_t>(content.GetItem(input.Item()).massGrams) * input.Count();
                }
            }
            CHECK(recoveredMass * producer.result.count <= consumedMass);
            for (const auto& output : recycler.returns) {
                // Suitable blade stock cannot be conjured by dismantling scrap products.
                if (output.item != content.FindItem("spring_steel_bar")
                    && output.item != content.FindItem("tool_steel_blank")
                    && output.item != content.FindItem("aa_rechargeable_battery")) { continue; }
                unsigned consumed = 0;
                for (const auto& input : producer.inputs) {
                    if (!input.Retained() && input.Item() == output.item) { consumed += input.Count(); }
                }
                CHECK(output.count * producer.result.count <= consumed);
            }
        }
        REQUIRE(producers > 0);
    }
}

TEST_CASE("W3 crafted forge and forming tools reach a sword without preexisting advanced stations",
    "[workshop][content][w3]") {
    Rig r; r.HandTools(); r.Level("fabrication", 1);
    const auto crude = r.Craft("assemble_crude_anvil");
    r.Place(crude, {2, 3});
    r.Level("fabrication", 2);
    const auto hearth = r.Craft("assemble_charcoal_forge");
    r.forge = r.Place(hearth, {3, 2});
    CHECK_FALSE(r.world.DescribeObjectState(r.forge)->forge.ready);
    CHECK(r.world.DescribeObjectState(r.forge)->forge.fuel == 0);
    r.Inputs(Recipe(r.content, "forge_tongs"));
    CHECK(r.Start("forge_tongs").outcome == CraftingOutcome::ColdForge);
    r.FillForge();
    const auto tongs = r.Craft("forge_tongs");
    REQUIRE(r.world.GetItem(tongs)->type == r.content.FindItem("forging_tongs"));
    r.Craft("cut_tool_steel_blanks");
    r.Craft("make_wooden_workshop_crate");
    r.Craft("forge_pry_bar");
    r.Level("fabrication", 3);
    r.Craft("forge_smithing_hammer");
    const auto anvilKit = r.Craft("assemble_fabricated_anvil");
    const auto crudeStation = r.world.ObjectAt({2, 3});
    REQUIRE(Take(r.world.PackStation(r.actor, crudeStation)).outcome == StationActionOutcome::Packed);
    r.Place(anvilKit, {2, 3});
    r.Inputs(Recipe(r.content, "make_current_arming_sword"));
    CHECK(r.Start("make_current_arming_sword").outcome == CraftingOutcome::MissingSkills);
    r.Level("fabrication", 4);
    r.Craft("make_current_arming_sword");
}

TEST_CASE("W3 found professional anvil skips construction but not sword tools skill stock or fuel",
    "[workshop][content][w3]") {
    Rig r; r.HandTools();
    Take(r.world.CreateObject(r.content.FindObject("workshop_anvil"), {2, 3}));
    r.forge = Take(r.world.CreateObject(r.content.FindObject("charcoal_forge"), {3, 2}));
    r.Add("forging_tongs"); r.Add("smithing_hammer");
    CHECK(r.world.DescribeObjectState(r.forge)->forge.fuel == 0);
    r.Level("fabrication", 3);
    r.Inputs(Recipe(r.content, "make_current_arming_sword"));
    CHECK(r.Start("make_current_arming_sword").outcome == CraftingOutcome::MissingSkills);
    r.Level("fabrication", 4);
    CHECK(r.Start("make_current_arming_sword").outcome == CraftingOutcome::ColdForge);
    r.FillForge();
    const auto bar = r.Find("spring_steel_bar"); REQUIRE(bar.value != 0);
    REQUIRE(r.world.TransferItem(bar, ItemLocation::Ground({7, 7})));
    CHECK(r.Start("make_current_arming_sword").outcome == CraftingOutcome::MissingInputs);
    REQUIRE(r.world.TransferItem(bar, ItemLocation::Carried(r.actor)));
    for (const auto name : {"forging_tongs", "smithing_hammer"}) {
        const auto tool = r.Find(name); REQUIRE(tool.value != 0);
        REQUIRE(r.world.TransferItem(tool, ItemLocation::Ground({7, 7})));
        CHECK(r.Start("make_current_arming_sword").outcome == CraftingOutcome::MissingTools);
        REQUIRE(r.world.TransferItem(tool, ItemLocation::Carried(r.actor)));
    }
    const auto anvil = r.world.ObjectAt({2, 3});
    REQUIRE(r.world.SetObjectEnabled(anvil, false));
    CHECK(r.Start("make_current_arming_sword").outcome == CraftingOutcome::MissingTools);
    REQUIRE(r.world.SetObjectEnabled(anvil, true));
    r.Craft("make_current_arming_sword");
    r.Craft("make_salvaged_arming_sword");
}

TEST_CASE("W3 useful completed fabrication work learns the entire forge ladder and an equipped sword",
    "[workshop][content][w3]") {
    Rig r; r.HandTools();
    REQUIRE(r.Level("fabrication") == 0);
    // No SetLevel, direct XP awards or books: every advancement comes from
    // runtime completion of an authored recipe at its eligible training band.
    r.LearnThroughWork("fabrication", 1, {"shape_timber_dowels"});
    const auto crude = r.Craft("assemble_crude_anvil");
    r.Place(crude, {2, 3});
    r.LearnThroughWork("fabrication", 2, {"assemble_crude_anvil"});
    const auto hearth = r.Craft("assemble_charcoal_forge");
    r.forge = r.Place(hearth, {3, 2});
    r.Craft("forge_tongs");
    r.Craft("cut_tool_steel_blanks");
    r.LearnThroughWork("fabrication", 3,
        {"forge_pry_bar", "make_wooden_workshop_crate", "cut_tool_steel_blanks"});
    const auto xpAt3 = r.Xp("fabrication");
    r.Craft("make_wooden_workshop_crate"); // Useful F2 work cannot train through F4.
    CHECK(r.Level("fabrication") == 3); CHECK(r.Xp("fabrication") == xpAt3);
    r.Craft("forge_smithing_hammer");
    const auto anvil = r.Craft("assemble_fabricated_anvil");
    REQUIRE(Take(r.world.PackStation(r.actor, r.world.ObjectAt({2, 3}))).outcome == StationActionOutcome::Packed);
    r.Place(anvil, {2, 3});
    r.LearnThroughWork("fabrication", 4,
        {"forge_wood_chisel", "forge_smithing_hammer", "assemble_fabricated_anvil"});
    const auto sword = r.Craft("make_current_arming_sword");
    REQUIRE(r.world.WieldItem(r.actor, sword).outcome == InventoryOutcome::Succeeded);
    CHECK(r.world.GetActor(r.actor)->wieldedItem == sword);
    CHECK(r.world.CanUseWieldedItem(r.actor));
    REQUIRE(r.world.CheckInvariants());
}

TEST_CASE("W3 useful electrical assemblies learn E1 E2 and E3 without administrative skill grants",
    "[workshop][content][w3]") {
    Rig r; r.HandTools(); r.Grid();
    REQUIRE(r.Level("electronics") == 0);
    r.LearnThroughWork("electronics", 1, {"assemble_wiring_kit"});
    r.LearnThroughWork("electronics", 2, {"assemble_electrical_lead_set"});
    const auto xpAt2 = r.Xp("electronics");
    r.Craft("assemble_electrical_lead_set");
    CHECK(r.Level("electronics") == 2); CHECK(r.Xp("electronics") == xpAt2);
    // First iron really is assembled before any soldering provider is supplied.
    REQUIRE_FALSE(r.Find("soldering_iron").value);
    r.Craft("assemble_soldering_iron");
    r.LearnThroughWork("electronics", 3,
        {"assemble_workshop_battery_pack", "assemble_soldering_iron"});
    CHECK(r.Level("mechanics") == 0); // Electrical practice grants no unrelated shortcut.
    r.Inputs(Recipe(r.content, "assemble_makeshift_arc_welder"));
    CHECK(r.Start("assemble_makeshift_arc_welder").outcome == CraftingOutcome::MissingSkills);
}

TEST_CASE("W3 motor recovery pump recovery and fitted spears teach learned Mechanics zero through two",
    "[workshop][content][w3]") {
    Rig r; r.HandTools();
    REQUIRE(r.Level("mechanics") == 0);
    unsigned motors = 0;
    while (r.Level("mechanics") == 0 && motors < 128) {
        const auto beforeXp = r.Xp("mechanics");
        r.RecycleForPractice("salvage_appliance_motor"); ++motors;
        REQUIRE((r.Level("mechanics") > 0 || r.Xp("mechanics") > beforeXp));
    }
    INFO("completed appliance motor recoveries=" << motors);
    REQUIRE(r.Level("mechanics") == 1);
    const auto xpAt1 = r.Xp("mechanics");
    r.RecycleForPractice("salvage_appliance_motor");
    CHECK(r.Level("mechanics") == 1); CHECK(r.Xp("mechanics") == xpAt1);
    r.RecycleForPractice("salvage_water_pump");
    CHECK(r.Xp("mechanics") > xpAt1);
    r.LearnThroughWork("fabrication", 1, {"shape_timber_dowels"});
    r.LearnThroughWork("mechanics", 2, {"make_steel_spear"});
    REQUIRE(r.world.CheckInvariants());
}

struct SaveFile {
    std::filesystem::path directory = std::filesystem::temp_directory_path()
        / ("cataclysmx-w3-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::string path;
    SaveFile() {
        REQUIRE(std::filesystem::create_directory(directory));
        path = (directory / "world.sqlite").string();
    }
    ~SaveFile() { std::error_code error; std::filesystem::remove_all(directory, error); }
};
TEST_CASE("W3 authored tongs retain paid heat through pause disk reload and cancellation",
    "[workshop][content][w3]") {
    Rig r; r.HandTools(); r.Level("fabrication", 2);
    Take(r.world.CreateObject(r.content.FindObject("crude_anvil"), {2, 3}));
    r.forge = Take(r.world.CreateObject(r.content.FindObject("charcoal_forge"), {3, 2}));
    r.FillForge();
    const auto& recipe = Recipe(r.content, "forge_tongs"); r.Inputs(recipe);
    Vector<PersistentId> originals;
    for (const auto item : r.world.GetActor(r.actor)->inventory) { originals.push_back(r.world.GetItem(item)->id); }
    const auto initialFuel = r.world.DescribeObjectState(r.forge)->forge.fuel;
    const auto started = r.Start("forge_tongs"); REQUIRE(started.outcome == CraftingOutcome::Started);
    const auto paidTicks = r.world.InspectCrafting(started.unfinished)->fundedTicksRemaining;
    REQUIRE(paidTicks > 1);
    const auto paidFuel = r.world.DescribeObjectState(r.forge)->forge.fuel;
    CHECK(initialFuel - paidFuel == static_cast<std::uint64_t>(paidTicks) * recipe.heatPerWork / ratioScale);
    r.Step(1);
    Take(r.world.PauseCrafting(r.actor, started.unfinished));
    const auto actorId = r.world.GetActor(r.actor)->id;
    const auto forgeId = r.world.GetObject(r.forge)->id;
    const auto jobId = r.world.GetItem(started.unfinished)->id;
    const auto before = r.world.CaptureItemState(started.unfinished);
    SaveFile file; REQUIRE(r.world.CreateSave(file.path.c_str(), {}));
    World restored(r.content); Take(restored.LoadSave(file.path.c_str()));
    Take(restored.RequestChunks({{0, 0}, 0}, LoadMode::active)); REQUIRE(restored.ReconcileChunks());
    const auto actor = restored.FindActor(actorId);
    const auto job = restored.FindItem(jobId);
    const auto forge = restored.FindObject(forgeId);
    REQUIRE(job.value != 0); REQUIRE(forge.value != 0);
    CHECK(restored.CaptureItemState(job) == before);
    CHECK(restored.DescribeObjectState(forge)->forge.fuel == paidFuel);
    restored.ElapseTicks(paidTicks - 1); restored.ProcessCraftingEvents();
    REQUIRE(restored.InspectCrafting(job));
    CHECK(restored.InspectCrafting(job)->status == CraftingStatus::Paused);
    CHECK(restored.InspectCrafting(job)->completedWork == paidTicks);
    CHECK(restored.DescribeObjectState(forge)->forge.fuel == paidFuel);
    CHECK(Take(restored.ResumeCrafting(actor, job)).outcome == CraftingOutcome::Resumed);
    const auto afterResume = restored.DescribeObjectState(forge)->forge.fuel;
    CHECK(afterResume < paidFuel);
    REQUIRE(Take(restored.CancelCrafting(actor, job)).outcome == CraftingOutcome::Cancelled);
    CHECK(restored.DescribeObjectState(forge)->forge.fuel == afterResume);
    for (const auto id : originals) {
        const auto item = restored.FindItem(id); REQUIRE(item.value != 0);
        CHECK_FALSE(restored.IsReservedItem(item));
    }
    REQUIRE(restored.CheckInvariants());
}

TEST_CASE("W3 battery assembly and recycling transfer only actual recovered cell charge",
    "[workshop][content][w3]") {
    for (const unsigned cellCharge : {0u, 321u, 1000u}) {
        INFO("input cell charge=" << cellCharge);
        Rig r; r.HandTools(); r.Grid(); r.Add("soldering_iron");
        r.Level("electronics", 2);
        const auto& recipe = Recipe(r.content, "assemble_workshop_battery_pack");
        r.Inputs(recipe);
        const auto cellType = r.content.FindItem("aa_rechargeable_battery");
        unsigned cells = 0;
        for (const auto item : r.world.GetActor(r.actor)->inventory) {
            if (r.world.GetItem(item)->type == cellType) { r.Energy(item, cellCharge); ++cells; }
        }
        REQUIRE(cells == 4);
        const auto pack = r.Craft("assemble_workshop_battery_pack");
        CHECK(r.Energy(pack) == cells * cellCharge);
        const auto& definition = r.content.GetItem(r.world.GetItem(pack)->type);
        REQUIRE(definition.energyStores.size() == 1);
        CHECK(definition.energyStores[0].config.capacityEnergy == 4000);
        const auto packId = r.world.GetItem(pack)->id;
        const auto packState = r.world.CaptureItemState(pack);
        const auto wiringKit = r.Add("wiring_kit");
        const auto& wiring = r.content.GetItem(r.world.GetItem(wiringKit)->type);
        REQUIRE(wiring.wiringMaterial.value != 0);
        REQUIRE(wiring.wiringTilesPerMaterial > 0);
        const auto wireCount = (4u + wiring.wiringTilesPerMaterial - 1) / wiring.wiringTilesPerMaterial;
        for (unsigned i = 0; i < wireCount; ++i) { r.Add(wiring.wiringMaterial); }
        REQUIRE(r.world.PlacePowerObject(r.actor, pack, wiringKit, {3, 2}).outcome
            == PowerActionOutcome::Succeeded);
        const auto installed = r.world.ObjectAt({3, 2}); REQUIRE(installed.value != 0);
        CHECK(r.world.GetObject(installed)->type == r.content.FindObject("grid_battery_station"));
        CHECK(r.world.FindItem(packId) == pack);
        CHECK(r.world.GetItem(pack)->location == ItemLocation::Stored(installed));
        REQUIRE(r.world.GetObject(installed)->contents.size() == 1);
        CHECK(r.world.GetObject(installed)->contents.front() == pack);
        CHECK(r.world.CaptureItemState(pack) == packState);
        CHECK(r.Energy(pack) == cells * cellCharge);
        // D1 removal is an ordinary transfer of the installed original, not
        // packing/recreating an item with its authored initial charge.
        REQUIRE(r.world.TransferItem(pack, ItemLocation::Carried(r.actor)));
        CHECK(r.world.GetObject(installed)->contents.empty());
        CHECK(r.world.FindItem(packId) == pack);
        CHECK(r.world.CaptureItemState(pack) == packState);
        CHECK(r.Energy(pack) == cells * cellCharge);
        const auto recycler = Recycler(r.content, r.content.FindItem("workshop_battery_pack"));
        REQUIRE(recycler.value != 0);
        unsigned promisedCells = 0;
        for (const auto& output : r.content.GetRecyclingRecipe(recycler).returns) {
            if (output.item == cellType) { promisedCells += output.count; }
        }
        REQUIRE(promisedCells > 0); REQUIRE(promisedCells <= cells);
        // Provision only the real disassembly tools/materials, not synthetic outputs.
        for (const auto& input : r.content.GetRecyclingRecipe(recycler).inputs) {
            for (unsigned i = r.Count(input.Item()); i < input.Count(); ++i) { r.Add(input.Item()); }
        }
        const auto recycled = Take(r.world.StartRecycling(r.actor, pack, recycler));
        REQUIRE(recycled.outcome == CraftingOutcome::Started); r.Finish(recycled.unfinished);
        unsigned recoveredCells = 0, recoveredEnergy = 0;
        for (const auto item : r.world.GetActor(r.actor)->inventory) {
            if (r.world.GetItem(item)->type == cellType) { ++recoveredCells; recoveredEnergy += r.Energy(item); }
        }
        CHECK(recoveredCells == promisedCells);
        CHECK(recoveredEnergy == cells * cellCharge);
        if (cellCharge == 0) { CHECK(recoveredEnergy == 0); }
    }
}

TEST_CASE("W3 electrical bootstrap assembles the first iron and welder without their own traits",
    "[workshop][content][w3]") {
    Rig r; r.HandTools(); r.Grid();
    r.Level("electronics", 1); r.Craft("assemble_electrical_lead_set");
    r.Level("electronics", 2); r.Craft("assemble_soldering_iron");
    r.Level("electronics", 3); r.Level("mechanics", 2);
    const auto welder = r.Craft("assemble_makeshift_arc_welder");
    const auto& definition = r.content.GetItem(r.world.GetItem(welder)->type);
    CHECK(definition.craftingGridPower); CHECK(definition.craftingEnergyPerWork == 30000);
    CHECK(Tier(r.content, definition.toolTraits, "welding") == 1);
}

TEST_CASE("W3 stronger welding tools retain ordinary accessible protection gates and spent power",
    "[workshop][content][w3]") {
    Rig r; r.HandTools(); r.Level("fabrication", 3);
    const auto welder = r.Add("arc_welder");
    const auto& definition = r.content.GetItem(r.world.GetItem(welder)->type);
    CHECK(definition.craftingGridPower); CHECK(definition.craftingEnergyPerWork == 20000);
    r.Inputs(Recipe(r.content, "weld_workshop_rack"));
    r.Add("safety_goggles"); r.Add("welding_gloves");
    CHECK(r.Start("weld_workshop_rack").outcome == CraftingOutcome::MissingTools);
    const auto goggles = r.Add("improvised_welding_goggles");
    // Ordinary shared-tool access is sufficient: no wear slot or dedicated channel.
    REQUIRE(r.world.TransferItem(goggles, ItemLocation::Ground({2, 2})));
    CHECK(r.Start("weld_workshop_rack").outcome == CraftingOutcome::InsufficientEnergy);
    r.Grid(); r.Energy(r.gridBattery, 0);
    const auto inventory = r.world.GetActor(r.actor)->inventory;
    CHECK(r.Start("weld_workshop_rack").outcome == CraftingOutcome::InsufficientEnergy);
    CHECK(r.world.GetActor(r.actor)->inventory == inventory);
    r.Energy(r.gridBattery, 4000);
    const auto electrodes = r.Find("welding_electrode_packet"); REQUIRE(electrodes.value != 0);
    const auto electrodeId = r.world.GetItem(electrodes)->id;
    const auto started = r.Start("weld_workshop_rack"); REQUIRE(started.outcome == CraftingOutcome::Started);
    CHECK(r.world.IsReservedItem(electrodes));
    const auto spent = 4000 - r.Energy(r.gridBattery); CHECK(spent > 0);
    CHECK(spent == r.world.InspectCrafting(started.unfinished)->fundedTicksRemaining
        * definition.craftingEnergyPerWork / ratioScale);
    REQUIRE(Take(r.world.CancelCrafting(r.actor, started.unfinished)).outcome == CraftingOutcome::Cancelled);
    CHECK(r.Energy(r.gridBattery) == 4000 - spent);
    CHECK(r.world.FindItem(electrodeId) == electrodes);
    CHECK_FALSE(r.world.IsReservedItem(electrodes));
    r.Inputs(Recipe(r.content, "weld_fabricated_anvil"));
    CHECK(r.Start("weld_fabricated_anvil").outcome == CraftingOutcome::MissingTools);
    const auto mask = r.Add("welding_mask");
    REQUIRE(r.world.TransferItem(mask, ItemLocation::Ground({2, 2})));
    r.Craft("weld_fabricated_anvil"); // Found welding 2 + ordinary ground protection 2.
    r.Craft("weld_workshop_rack");   // Same stronger tool performs protection-1 work.
}

TEST_CASE("W3 crafted rack provides real storage and only packs empty with the original kit",
    "[workshop][content][w3]") {
    Rig r; r.HandTools(); r.Grid(); r.Level("fabrication", 2);
    r.Add("makeshift_arc_welder"); r.Add("improvised_welding_goggles");
    const auto kit = r.Craft("weld_workshop_rack");
    const auto kitId = r.world.GetItem(kit)->id;
    const auto rack = r.Place(kit, {3, 2});
    const auto& definition = r.content.GetObject(r.world.GetObject(rack)->type);
    REQUIRE(definition.capacityVolumeMl); CHECK(*definition.capacityVolumeMl == 120000);
    CHECK(definition.toolTraits.Empty()); CHECK(definition.behavior == ObjectBehaviorKind::None);
    const auto stored = r.Add("metal_file");
    REQUIRE(r.world.TransferItem(stored, ItemLocation::Stored(rack)));
    CHECK(r.world.QueryPackStation(r.actor, rack) != StationActionOutcome::Packed);
    REQUIRE(r.world.GetObject(rack));
    REQUIRE(r.world.TransferItem(stored, ItemLocation::Carried(r.actor)));
    REQUIRE(Take(r.world.PackStation(r.actor, rack)).outcome == StationActionOutcome::Packed);
    CHECK_FALSE(r.world.GetObject(rack));
    CHECK(r.world.FindItem(kitId) == kit);
    CHECK(r.world.GetItem(kit)->location == ItemLocation::Carried(r.actor));
    CHECK(r.Count(r.content.FindItem("welded_workshop_rack_kit")) == 1);
    REQUIRE(r.world.CheckInvariants());
}
}
