#include "content/content.h"
#include "worldgen/worldgen.h"

#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <filesystem>

namespace {
using namespace cataclysmx;

struct ItemSpec { const char* id; unsigned mass, volume; };
constexpr ItemSpec items[] = {
    {"home_polymer_printer",8000,65000}, {"polymer_filament",10,15},
    {"plastic_cup",40,400}, {"small_parts_case",120,850}, {"plastic_buckle_set",40,100},
    {"printed_machete_grip",80,180}, {"printed_electronics_case",180,900},
    {"usb_data_stick",10,15}, {"phone",180,150}, {"legacy_laptop",1800,2500},
};
struct PrintSpec {
    const char* id; const char* product;
    unsigned filament, level, ticks, xp, maximum;
};
constexpr PrintSpec prints[] = {
    {"print_plastic_cup","plastic_cup",5,0,1200,10,30},
    {"print_small_parts_case","small_parts_case",14,1,2400,20,14},
    {"print_plastic_storage_box","plastic_storage_box",34,1,6000,30,4},
    {"print_plastic_buckle_set","plastic_buckle_set",5,1,1000,10,51},
    {"print_machete_grip","printed_machete_grip",10,1,1800,15,25},
    {"print_electronics_case","printed_electronics_case",21,2,3600,30,12},
};

template<class Container, class T>
bool Contains(const Container& values, const T& value) {
    for (const auto& entry : values) { if (entry == value) { return true; } }
    return false;
}

void CheckPack(const ContentCatalog& content) {
    for (const auto& spec : items) {
        INFO(spec.id);
        const auto id = content.FindItem(spec.id);
        REQUIRE(id.value);
        const auto& item = content.GetItem(id);
        CHECK(item.massGrams == spec.mass);
        CHECK(item.volumeMl == spec.volume);
        CHECK_FALSE(item.description.empty());
        CHECK_FALSE(item.art.lines.empty());
    }
    const auto software = content.FindInformation("home_printer_software");
    const auto designs = content.FindInformation("home_printer_designs");
    REQUIRE(software.value);
    REQUIRE(designs.value);
    CHECK(content.GetInformation(software).software);
    CHECK_FALSE(content.GetInformation(designs).software);
    CHECK(content.GetInformation(designs).designs.size() == 6);
    const auto printerId = content.FindItem("home_polymer_printer");
    const auto& printer = content.GetItem(printerId);
    CHECK(printer.digitalDevice == DigitalDeviceKind::Controller);
    CHECK(printer.suppliedInformation.size() == 2);
    CHECK(Contains(printer.suppliedInformation, software));
    CHECK(Contains(printer.suppliedInformation, designs));
    REQUIRE(printer.placedObject.value);
    const auto& station = content.GetObject(printer.placedObject);
    CHECK(station.behavior == ObjectBehaviorKind::SelectableMachine);
    CHECK(station.packingItem == printerId);
    CHECK(station.blocksMovement);
    CHECK_FALSE(station.blocksSight);
    REQUIRE(station.capacityVolumeMl.has_value());
    CHECK(*station.capacityVolumeMl == 12000);
    CHECK(station.selectableRecipes.size() == 6);
    const auto filament = content.FindItem("polymer_filament");
    const auto computers = content.FindSkill("computers");
    REQUIRE(computers.value);
    for (const auto& spec : prints) {
        INFO(spec.id);
        const auto id = content.FindMachineRecipe(spec.id);
        REQUIRE(id.value);
        CHECK(Contains(content.GetInformation(designs).designs, id));
        CHECK(Contains(station.selectableRecipes, id));
        const auto& recipe = content.GetMachineRecipe(id);
        REQUIRE(recipe.inputs.Size() == 1);
        CHECK(recipe.inputs[0].Item() == filament);
        CHECK(recipe.inputs[0].Count() == spec.filament);
        CHECK_FALSE(recipe.inputs[0].Retained());
        CHECK(recipe.result.item == content.FindItem(spec.product));
        CHECK(recipe.result.count == 1);
        CHECK(recipe.duration == spec.ticks);
        CHECK(recipe.energyPerTick == 1);
        CHECK(recipe.setupWork == 200);
        CHECK(recipe.setupWorkPerUnit == 20);
        CHECK(recipe.requiredSoftware == software);
        CHECK(recipe.learning.skill == computers);
        CHECK(recipe.learning.level == spec.level);
        CHECK(recipe.learning.xpPerExecution == spec.xp);
        if (spec.level == 0) { CHECK(recipe.skills.empty()); }
        else {
            REQUIRE(recipe.skills.size() == 1);
            CHECK(recipe.skills[0].skill == computers);
            CHECK(recipe.skills[0].level == spec.level);
        }
        const unsigned inputVolume = spec.filament * content.GetItem(filament).volumeMl;
        const unsigned outputVolume = content.GetItem(recipe.result.item).volumeMl;
        const unsigned maximum = std::min(255u / spec.filament,
            12000u / std::max(inputVolume, outputVolume));
        CHECK(maximum == spec.maximum);
        CHECK(spec.filament * content.GetItem(filament).massGrams >=
            content.GetItem(recipe.result.item).massGrams);
    }
    for (const auto& spec : {ItemSpec{"plastic_cup",300,0}, ItemSpec{"small_parts_case",600,0},
                            ItemSpec{"plastic_storage_box",2000,0}}) {
        const auto* container = ItemContainer(content.GetItem(content.FindItem(spec.id)));
        REQUIRE(container);
        CHECK(container->capacityVolumeMl == spec.mass);
    }
    const auto& usb = content.GetItem(content.FindItem("usb_data_stick"));
    CHECK(usb.digitalDevice == DigitalDeviceKind::Storage);
    CHECK(usb.suppliedInformation.empty());
    for (const auto& spec : {ItemSpec{"phone",1000,0}, ItemSpec{"legacy_laptop",4000,0}}) {
        INFO(spec.id);
        const auto& device = content.GetItem(content.FindItem(spec.id));
        CHECK(device.digitalDevice == DigitalDeviceKind::Reader);
        REQUIRE(device.energyStores.size() == 1);
        const auto* store = ItemEnergyStore(device, device.readingPower);
        REQUIRE(store);
        CHECK(store->capacityEnergy == spec.mass);
        CHECK(store->initialEnergy == spec.mass);
        CHECK(store->rechargeable);
        REQUIRE(device.suppliedInformation.size() == 1);
        const auto& package = content.GetInformation(device.suppliedInformation[0]);
        REQUIRE(package.books.size() == 1);
        const char* expected = spec.mass == 1000 ? "workshop_primer_text" : "salvage_notes_text";
        CHECK(package.books[0] == content.FindBook(expected));
        CHECK(package.designs.empty());
        CHECK_FALSE(package.software);
    }
}

struct ConsumerSpec { const char* recipe; const char* product; const char* skill; unsigned level, work, inputMass; };
constexpr ConsumerSpec consumers[] = {
    {"stitch_canvas_duffel_with_printed_buckles","canvas_duffel","tailoring",1,1800,940},
    {"fit_machete_with_printed_grip","salvaged_machete","fabrication",1,2200,1280},
    {"assemble_workshop_battery_pack_in_printed_case","workshop_battery_pack","electronics",2,3500,800},
};
struct RecyclerSpec { const char* id; const char* target; unsigned mass, work; };
constexpr RecyclerSpec recyclers[] = {
    {"salvage_small_parts_case","small_parts_case",100,150},
    {"salvage_plastic_storage_box","plastic_storage_box",200,250},
    {"salvage_printed_electronics_case","printed_electronics_case",100,200},
    {"dismantle_home_polymer_printer","home_polymer_printer",4810,2000},
};

void CheckRecipes(const ContentCatalog& content) {
    for (const auto& spec : consumers) {
        INFO(spec.recipe);
        const auto id = content.FindCraftingRecipe(spec.recipe);
        REQUIRE(id.value);
        const auto& recipe = content.GetCraftingRecipe(id);
        CHECK(recipe.knownByDefault);
        CHECK(recipe.work == spec.work);
        CHECK(recipe.result.item == content.FindItem(spec.product));
        CHECK(recipe.result.count == 1);
        REQUIRE(recipe.skills.size() == 1);
        CHECK(recipe.skills[0].skill == content.FindSkill(spec.skill));
        CHECK(recipe.skills[0].level == spec.level);
        unsigned mass = 0;
        for (const auto& input : recipe.inputs) {
            if (!input.Retained()) { mass += content.GetItem(input.Item()).massGrams * input.Count(); }
        }
        CHECK(mass == spec.inputMass);
        CHECK(mass >= content.GetItem(recipe.result.item).massGrams);
        CHECK(content.FindRecyclingFor(recipe.result.item).value);
    }
    for (const auto& spec : recyclers) {
        INFO(spec.id);
        const auto id = content.FindRecyclingRecipe(spec.id);
        REQUIRE(id.value);
        const auto& recipe = content.GetRecyclingRecipe(id);
        CHECK(recipe.target == content.FindItem(spec.target));
        CHECK(content.FindRecyclingFor(recipe.target) == id);
        CHECK(recipe.work == spec.work);
        unsigned mass = 0;
        for (const auto& output : recipe.returns) {
            CHECK(output.item != content.FindItem("polymer_filament"));
            mass += content.GetItem(output.item).massGrams * output.count;
        }
        CHECK(mass == spec.mass);
        CHECK(mass <= content.GetItem(recipe.target).massGrams);
    }
    for (const char* id : {"plastic_cup","plastic_buckle_set","printed_machete_grip"}) {
        CHECK_FALSE(content.FindRecyclingFor(content.FindItem(id)).value);
    }
    struct InputSpec { const char* recipe; const char* item; unsigned count; bool retained, noRecovery; };
    constexpr InputSpec inputs[] = {
        {"stitch_canvas_duffel_with_printed_buckles","canvas_patch",5,false,false},
        {"stitch_canvas_duffel_with_printed_buckles","nylon_webbing",1,false,false},
        {"stitch_canvas_duffel_with_printed_buckles","sewing_thread",1,false,true},
        {"stitch_canvas_duffel_with_printed_buckles","plastic_buckle_set",1,false,false},
        {"stitch_canvas_duffel_with_printed_buckles","retractable_utility_knife",1,true,false},
        {"stitch_canvas_duffel_with_printed_buckles","sewing_supplies",1,true,false},
        {"fit_machete_with_printed_grip","steel_scrap",10,false,false},
        {"fit_machete_with_printed_grip","steel_wire",1,false,false},
        {"fit_machete_with_printed_grip","printed_machete_grip",1,false,false},
        {"fit_machete_with_printed_grip","claw_hammer",1,true,false},
        {"fit_machete_with_printed_grip","hacksaw",1,true,false},
        {"fit_machete_with_printed_grip","metal_file",1,true,false},
        {"assemble_workshop_battery_pack_in_printed_case","aa_rechargeable_battery",4,false,false},
        {"assemble_workshop_battery_pack_in_printed_case","electrical_lead_set",1,false,false},
        {"assemble_workshop_battery_pack_in_printed_case","printed_electronics_case",1,false,false},
        {"assemble_workshop_battery_pack_in_printed_case","solder_wire",1,false,true},
    };
    for (const auto& spec : inputs) {
        INFO(spec.recipe << " input=" << spec.item);
        const auto& recipe = content.GetCraftingRecipe(content.FindCraftingRecipe(spec.recipe));
        bool found = false;
        for (const auto& input : recipe.inputs) {
            if (input.Item() != content.FindItem(spec.item)) { continue; }
            found = true;
            CHECK(input.Count() == spec.count);
            CHECK(input.Retained() == spec.retained);
            CHECK(input.NoRecovery() == spec.noRecovery);
        }
        CHECK(found);
    }
    const auto& battery = content.GetCraftingRecipe(
        content.FindCraftingRecipe("assemble_workshop_battery_pack_in_printed_case"));
    REQUIRE(battery.toolTraits.Size() == 2);
    for (const char* name : {"soldering", "electrical_testing"}) {
        bool found = false;
        for (const auto& trait : battery.toolTraits) {
            found |= trait.trait == content.FindToolTrait(name) && trait.tier == 1;
        }
        CHECK(found);
    }
}

const BasementVariant& Basement(const BuildingPattern& pattern, const char* name) {
    for (const auto& basement : pattern.basements) { if (basement.id == name) { return basement; } }
    FAIL("Missing basement " << name);
}

void CheckSite(const ContentCatalog& content, const char* site, const char* variant,
    TileCoord printer, TileCoord filament, const char* pool, unsigned rolls, unsigned printerChance) {
    INFO(site << " variant=" << (variant ? variant : "surface"));
    const auto id = content.FindBuildingPattern(site);
    REQUIRE(id.value);
    const auto& pattern = content.GetBuildingPattern(id);
    const BasementVariant* basement = variant ? &Basement(pattern, variant) : nullptr;
    const auto& population = basement ? basement->population : pattern.population;
    const auto& tiles = basement ? basement->layer.tiles : pattern.tiles;
    const auto printerType = content.FindObject("home_polymer_printer_station");
    const auto filamentPool = content.FindLootPool(pool);
    REQUIRE(filamentPool.value);
    bool hasPrinter = false, hasFilament = false;
    for (const auto& object : population.furniture) {
        hasPrinter |= object.position == printer && object.type == printerType;
    }
    for (const auto& marker : population.loot) {
        if (marker.position == filament && marker.pool == filamentPool) {
            hasFilament = marker.destination == LootDestination::Ground && marker.chance == 10000 && marker.rolls == rolls;
        }
        if (printerChance && marker.position == printer && marker.pool == content.FindLootPool("loot_packed_home_printer")) {
            hasPrinter = marker.destination == LootDestination::Ground && marker.chance == printerChance && marker.rolls == 1;
        }
    }
    CHECK(hasPrinter);
    CHECK(hasFilament);
    // Reachable from the entrance or basement stair using doors that can be opened,
    // with every blocking furniture marker applied. Ground loot must be on a reachable tile.
    Vector<std::uint8_t> walk(tiles.size(), 0), seen(tiles.size(), 0);
    const auto index = [&](int x, int z) { return static_cast<std::size_t>(z * pattern.width + x); };
    PatternPoint source = pattern.entrance;
    for (int z = 0; z < pattern.height; ++z) {
        for (int x = 0; x < pattern.width; ++x) {
            const auto& terrain = content.GetTerrain(tiles[index(x,z)]);
            bool passable = terrain.passable && !terrain.openSpace;
            if (!passable && terrain.opensTo.value) {
                const auto& opened = content.GetTerrain(terrain.opensTo);
                passable = opened.passable && !opened.openSpace;
            }
            walk[index(x,z)] = passable;
            if (basement && terrain.stairs) { source = {x,z}; }
        }
    }
    for (const auto& object : population.furniture) {
        if (content.GetObject(object.type).blocksMovement) { walk[index(object.position.x,object.position.z)] = 0; }
    }
    REQUIRE(walk[index(source.x,source.z)]);
    Vector<PatternPoint> queue;
    queue.push_back(source);
    seen[index(source.x,source.z)] = 1;
    constexpr PatternPoint directions[] = {{1,0},{-1,0},{0,1},{0,-1}};
    for (std::size_t i = 0; i < queue.size(); ++i) {
        for (const auto d : directions) {
            const int x = queue[i].x + d.x, z = queue[i].z + d.z;
            if (x < 0 || z < 0 || x >= pattern.width || z >= pattern.height) { continue; }
            const auto at = index(x,z);
            if (walk[at] && !seen[at]) { seen[at] = 1; queue.push_back({x,z}); }
        }
    }
    CHECK(seen[index(filament.x,filament.z)]);
    if (printerChance) { CHECK(seen[index(printer.x,printer.z)]); }
    else {
        bool approach = false;
        for (const auto d : directions) {
            const int x = printer.x + d.x, z = printer.z + d.z;
            if (x >= 0 && z >= 0 && x < pattern.width && z < pattern.height) { approach |= seen[index(x,z)] != 0; }
        }
        CHECK(approach);
    }
}

TEST_CASE("Printer pack budgets and information in both principal packs", "[content][printer][pack]") {
    for (const char* path : {CATACLYSMX_CONTENT_PACK, CATACLYSMX_FIRST_HOUSE_PACK}) {
        INFO(path);
        Diagnostics diagnostics;
        const auto content = LoadContentPack(path, &diagnostics);
        INFO(FormatDiagnostics(diagnostics).c_str());
        REQUIRE(content);
        REQUIRE(ValidateWorldgenContent(*content, &diagnostics));
        CheckPack(*content);
        CheckRecipes(*content);
    }
}

TEST_CASE("Printer acquisition placements and approaches", "[content][printer][worldgen]") {
    const auto content = LoadContentPack(CATACLYSMX_CONTENT_PACK);
    REQUIRE(content);
    for (const auto& spec : {ItemSpec{"loot_printer_filament_large",100,200},
                            ItemSpec{"loot_printer_filament_small",50,100}}) {
        const auto pool = content->FindLootPool(spec.id);
        REQUIRE(pool.value);
        const auto& entries = content->GetLootPool(pool).entries;
        REQUIRE(entries.size() == 1);
        CHECK(entries[0].kind == LootEntryKind::Item);
        CHECK(entries[0].item == content->FindItem("polymer_filament"));
        CHECK(entries[0].count.min == spec.mass);
        CHECK(entries[0].count.max == spec.volume);
    }
    CheckSite(*content,"electronics_store",nullptr,{6,2,0},{5,2,0},"loot_printer_filament_large",2,0);
    CheckSite(*content,"school",nullptr,{5,1,0},{6,1,0},"loot_printer_filament_large",1,0);
    CheckSite(*content,"duplex","hobby_pantry",{4,2,-1},{4,3,-1},"loot_printer_filament_small",1,0);
    CheckSite(*content,"furnished_house","workshop_cellar",{3,2,-1},{3,2,-1},"loot_printer_filament_small",1,3000);
    const auto& house = content->GetBuildingPattern(content->FindBuildingPattern("furnished_house"));
    const auto& storage = Basement(house, "storage_cellar");
    for (const auto& marker : storage.population.loot) {
        CHECK(marker.pool != content->FindLootPool("loot_packed_home_printer"));
        CHECK(marker.pool != content->FindLootPool("loot_printer_filament_small"));
    }
    const auto& duplex = content->GetBuildingPattern(content->FindBuildingPattern("duplex"));
    const auto& laundry = Basement(duplex, "shared_laundry_utility");
    for (const auto& marker : laundry.population.furniture) {
        CHECK(marker.type != content->FindObject("home_polymer_printer_station"));
    }
    for (const auto& marker : laundry.population.loot) {
        CHECK(marker.pool != content->FindLootPool("loot_printer_filament_small"));
    }
    const auto& packed = content->GetLootPool(content->FindLootPool("loot_packed_home_printer"));
    REQUIRE(packed.entries.size() == 1);
    CHECK(packed.entries[0].item == content->FindItem("home_polymer_printer"));
    CHECK(packed.entries[0].count.min == 1);
    CHECK(packed.entries[0].count.max == 1);
}
}
