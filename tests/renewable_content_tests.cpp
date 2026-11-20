#include "content/content.h"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <initializer_list>

namespace {
using namespace cataclysmx;

struct ItemSpec { const char* id; std::uint32_t mass; std::uint32_t volume; };
constexpr ItemSpec items[] = {
    {"recovered_pv_panel", 8000, 18000}, {"solar_charge_controller", 500, 700},
    {"solar_generator_kit", 10000, 22000}, {"recovered_gasoline_engine", 12000, 18000},
    {"generator_alternator", 5000, 6000}, {"generator_drive_assembly", 2000, 2500},
    {"woodgas_processing_assembly", 13000, 22000}, {"woodgas_generator_kit", 33000, 50000},
    {"prepared_wood", 1000, 1500},
};
struct RecipeSpec { const char* id; const char* output; unsigned count; unsigned work; unsigned inputMass; };
constexpr RecipeSpec recipes[] = {
    {"assemble_solar_generator_kit", "solar_generator_kit", 1, 4000, 10250},
    {"assemble_generator_drive", "generator_drive_assembly", 1, 5000, 2200},
    {"weld_woodgas_processing_assembly", "woodgas_processing_assembly", 1, 6000, 14700},
    {"assemble_woodgas_generator_kit", "woodgas_generator_kit", 1, 9000, 34600},
    {"prepare_wood_from_plank", "prepared_wood", 2, 400, 2000},
    {"prepare_wood_from_scrap", "prepared_wood", 1, 250, 1000},
};
struct RecyclerSpec { const char* id; const char* target; unsigned mass; unsigned work; };
constexpr RecyclerSpec recyclers[] = {
    {"dismantle_solar_generator_kit", "solar_generator_kit", 8850, 800},
    {"dismantle_generator_drive", "generator_drive_assembly", 1800, 600},
    {"dismantle_woodgas_processing_assembly", "woodgas_processing_assembly", 10200, 2000},
    {"dismantle_woodgas_generator_kit", "woodgas_generator_kit", 32400, 2400},
};

void CheckSkill(const ContentCatalog& content, const CraftingRecipeDefinition& recipe,
    const char* name, unsigned level) {
    const auto id = content.FindSkill(name);
    REQUIRE(id.value != 0);
    bool gate = false, training = false;
    for (const auto& skill : recipe.skills) { gate |= skill.skill == id && skill.level == level; }
    for (const auto& skill : recipe.training) {
        training |= skill.skill == id && skill.level == level && skill.xpPerWork > 0;
    }
    CHECK(gate);
    CHECK(training);
}

void CheckRenewableContent(const ContentCatalog& content) {
    for (const auto& spec : items) {
        INFO(spec.id);
        const auto id = content.FindItem(spec.id);
        REQUIRE(id.value != 0);
        const auto& item = content.GetItem(id);
        CHECK(item.massGrams == spec.mass);
        CHECK(item.volumeMl == spec.volume);
    }
    for (const auto& spec : recipes) {
        INFO(spec.id);
        const auto id = content.FindCraftingRecipe(spec.id);
        REQUIRE(id.value != 0);
        const auto& recipe = content.GetCraftingRecipe(id);
        CHECK(recipe.knownByDefault);
        CHECK(recipe.work == spec.work);
        CHECK(recipe.result.item == content.FindItem(spec.output));
        CHECK(recipe.result.count == spec.count);
        CHECK(recipe.inputs.Size() <= 8);
        CHECK(recipe.toolTraits.Size() <= 4);
        std::uint32_t mass = 0;
        for (const auto& input : recipe.inputs) {
            if (!input.Retained()) { mass += content.GetItem(input.Item()).massGrams * input.Count(); }
        }
        CHECK(mass == spec.inputMass);
        CHECK(mass >= content.GetItem(recipe.result.item).massGrams * recipe.result.count);
    }
    for (const auto& spec : recyclers) {
        INFO(spec.id);
        const auto id = content.FindRecyclingRecipe(spec.id);
        REQUIRE(id.value != 0);
        const auto& recipe = content.GetRecyclingRecipe(id);
        CHECK(recipe.target == content.FindItem(spec.target));
        CHECK(recipe.work == spec.work);
        std::uint32_t mass = 0;
        for (const auto& result : recipe.returns) {
            mass += content.GetItem(result.item).massGrams * result.count;
        }
        CHECK(mass == spec.mass);
        CHECK(mass <= content.GetItem(recipe.target).massGrams);
    }
    const auto& gasifier = content.GetCraftingRecipe(content.FindCraftingRecipe("weld_woodgas_processing_assembly"));
    CheckSkill(content, gasifier, "fabrication", 3);
    CheckSkill(content, gasifier, "mechanics", 2);
    CHECK(gasifier.toolTraits.Size() == 4);
    bool protection = false;
    for (const auto& trait : gasifier.toolTraits) {
        protection |= trait.trait == content.FindToolTrait("welding_protection") && trait.tier == 1;
    }
    CHECK(protection);
    CheckSkill(content, content.GetCraftingRecipe(content.FindCraftingRecipe("assemble_generator_drive")), "mechanics", 3);
    const auto& finalKit = content.GetCraftingRecipe(content.FindCraftingRecipe("assemble_woodgas_generator_kit"));
    CheckSkill(content, finalKit, "mechanics", 4);
    CheckSkill(content, finalKit, "electronics", 3);

    const auto solarId = content.FindObject("solar_generator_station");
    const auto woodgasId = content.FindObject("woodgas_generator_station");
    REQUIRE(solarId.value != 0);
    REQUIRE(woodgasId.value != 0);
    CHECK(content.GetItem(content.FindItem("solar_generator_kit")).placedObject == solarId);
    CHECK(content.GetItem(content.FindItem("woodgas_generator_kit")).placedObject == woodgasId);
    const auto& solar = content.GetObject(solarId);
    const auto& woodgas = content.GetObject(woodgasId);
    CHECK(solar.behavior == ObjectBehaviorKind::Solar);
    CHECK(solar.solar.peakOutputPerTick == 1);
    CHECK(woodgas.behavior == ObjectBehaviorKind::Generator);
    CHECK(woodgas.generator.fuelItem == content.FindItem("prepared_wood"));
    CHECK(woodgas.generator.fuelCapacity == 24000);
    CHECK(woodgas.generator.energyPerFuel == 4000);
    CHECK(woodgas.generator.outputPerTick == 4);
    for (const auto* object : {&solar, &woodgas}) {
        CHECK(object->blocksMovement);
        CHECK_FALSE(object->blocksSight);
        CHECK_FALSE(object->movable);
        CHECK(object->packingItem.value == 0);
        CHECK_FALSE(object->capacityVolumeMl.has_value());
    }
    const auto bookId = content.FindBook("workshop_primer_text");
    REQUIRE(bookId.value != 0);
    for (const auto& wanted : {SkillRequirement{content.FindSkill("electronics"), 3},
                               SkillRequirement{content.FindSkill("mechanics"), 4}}) {
        bool found = false;
        for (const auto& benefit : content.GetBook(bookId).benefits) {
            found |= benefit.skill == wanted.skill && benefit.firstLevel == 1
                && benefit.lastLevel == wanted.level && benefit.directXp == 20
                && benefit.multiplier == 15000;
        }
        CHECK(found);
    }
}

TEST_CASE("Renewable content budgets and fixed installations load in both packs", "[content][renewable]") {
    const auto fullPath = std::filesystem::path{CATACLYSMX_CONTENT_PACK};
    for (const auto& path : {fullPath, fullPath.parent_path() / "first_house.json5"}) {
        INFO(path.string());
        Diagnostics diagnostics;
        const auto loaded = LoadContentPack(path.string().c_str(), &diagnostics);
        INFO(FormatDiagnostics(diagnostics).c_str());
        REQUIRE(loaded);
        REQUIRE(diagnostics.Empty());
        CheckRenewableContent(*loaded);
    }
}
}
