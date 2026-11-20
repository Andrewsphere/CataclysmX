#include "content/content.h"

#include <catch2/catch_test_macros.hpp>

namespace {
using namespace cataclysmx;

bool PoolContains(const ContentCatalog& content, const char* poolName, const char* itemName) {
    const auto pool = content.FindLootPool(poolName);
    const auto item = content.FindItem(itemName);
    if (!pool.value || !item.value) { return false; }
    for (const auto& entry : content.GetLootPool(pool).entries) {
        if (entry.kind == LootEntryKind::Item && entry.item == item && entry.weight > 0) { return true; }
    }
    return false;
}

TEST_CASE("Principal packs expose independent Cooking and Chemistry definitions", "[content][skill-pack]") {
    String firstHouse = CATACLYSMX_CONTENT_PACK;
    firstHouse.resize(firstHouse.find_last_of("/\\") + 1);
    firstHouse += "first_house.json5";
    for (const char* path : {CATACLYSMX_CONTENT_PACK, firstHouse.c_str()}) {
        CAPTURE(path);
        const auto loaded = LoadContentPack(path);
        REQUIRE(loaded);
        CHECK(loaded->SkillCount() == 18);
        CHECK_FALSE(loaded->FindSkill("cooking_chemistry").value);
        const auto cookingId = loaded->FindSkill("cooking");
        const auto chemistryId = loaded->FindSkill("chemistry");
        REQUIRE(cookingId.value);
        REQUIRE(chemistryId.value);
        CHECK(cookingId != chemistryId);
        const auto& cooking = loaded->GetSkill(cookingId);
        const auto& chemistry = loaded->GetSkill(chemistryId);
        CHECK(cooking.category == SkillCategory::Noncombat);
        CHECK(chemistry.category == SkillCategory::Noncombat);
        CHECK(cooking.maxLevel == 5);
        CHECK(chemistry.maxLevel == 10);
        for (unsigned i = 0; i < maximumSkillLevel; ++i) {
            CHECK(cooking.xpToNext[i] == (i < 5 ? chemistry.xpToNext[i] : 0));
        }
        CHECK(chemistry.xpToNext[9] == 6400);
    }
}

TEST_CASE("Town skill pack supplies progression and equivalent reading carriers", "[content][skill-pack]") {
    Diagnostics diagnostics;
    const auto loaded = LoadContentPack(CATACLYSMX_CONTENT_PACK, &diagnostics);
    REQUIRE(loaded.has_value());
    const auto& content = *loaded;
    CHECK(content.SkillCount() == 18); // Nine noncombat and nine combat skills.
    CHECK_FALSE(content.FindSkill("cooking_chemistry").value);
    const auto& cooking = content.GetSkill(content.FindSkill("cooking"));
    CHECK(cooking.category == SkillCategory::Noncombat);
    CHECK(cooking.maxLevel == 5);
    CHECK(cooking.xpToNext[4] == 1200);
    CHECK(cooking.xpToNext[5] == 0);
    for (const char* name : {"chemistry", "fabrication", "mechanics", "electronics",
                             "computers", "tailoring", "survival", "medicine"}) {
        CAPTURE(name);
        const auto id = content.FindSkill(name);
        REQUIRE(id.value);
        const auto& skill = content.GetSkill(id);
        CHECK(skill.category == SkillCategory::Noncombat);
        CHECK(skill.maxLevel == 10);
        CHECK(skill.xpToNext[0] == 100);
        CHECK(skill.xpToNext[9] == 6400);
    }
    for (const char* name : {"melee", "unarmed", "knives", "swords", "clubs_hammers", "axes", "polearms", "shields", "dodge"}) {
        const auto id = content.FindSkill(name);
        REQUIRE(id.value);
        CHECK(content.GetSkill(id).category == SkillCategory::Combat);
    }
    const auto fabrication = content.FindSkill("fabrication");
    const auto spearId = content.FindCraftingRecipe("make_coil_spear");
    REQUIRE(spearId.value);
    const auto& spear = content.GetCraftingRecipe(spearId);
    REQUIRE(spear.skills.size() == 1);
    CHECK(spear.skills[0] == SkillRequirement{fabrication, 1});
    REQUIRE(spear.autolearnSkills.size() == 1);
    CHECK(spear.autolearnSkills[0] == SkillRequirement{fabrication, 2});
    CHECK(spear.autolearn);
    CHECK_FALSE(spear.knownByDefault);
    CHECK(spear.craftsToLearn == 2);
    CHECK(spear.disassembliesToLearn == 3);
    const auto recyclerId = content.FindRecyclingRecipe("disassemble_coil_spear");
    REQUIRE(recyclerId.value);
    const auto& recycler = content.GetRecyclingRecipe(recyclerId);
    CHECK(recycler.target == content.FindItem("coil_spear"));
    CHECK(content.FindRecyclingFor(recycler.target) == recyclerId);
    CHECK(recycler.skills.empty());
    REQUIRE(recycler.training.size() == 1);
    CHECK(recycler.training[0] == SkillTraining{fabrication, 0, 100});
    REQUIRE(recycler.teachesRecipes.size() == 1);
    CHECK(recycler.teachesRecipes[0] == spearId);
    const auto swordId = content.FindCraftingRecipe("make_current_arming_sword");
    REQUIRE(swordId.value);
    const auto& sword = content.GetCraftingRecipe(swordId);
    REQUIRE(sword.skills.size() == 1);
    CHECK(sword.skills[0] == SkillRequirement{fabrication, 4});
    CHECK(sword.knownByDefault);

    const auto primerId = content.FindBook("workshop_primer_text");
    const auto notesId = content.FindBook("salvage_notes_text");
    REQUIRE(primerId.value);
    REQUIRE(notesId.value);
    CHECK_FALSE(primerId == notesId);
    const auto& primer = content.GetBook(primerId);
    const auto& notes = content.GetBook(notesId);
    REQUIRE(primer.recipes.size() == 1);
    REQUIRE(notes.recipes.size() == 1);
    CHECK(primer.recipes[0] == spearId);
    CHECK(notes.recipes[0] == spearId);
    REQUIRE(primer.benefits.size() == 3);
    REQUIRE(notes.benefits.size() == 1);
    for (const auto benefit : {
            SkillBookBenefit{fabrication, 1, 3, 20, 100, 20000},
            SkillBookBenefit{content.FindSkill("electronics"), 1, 3, 20, 0, 15000},
            SkillBookBenefit{content.FindSkill("mechanics"), 1, 4, 20, 0, 15000}}) {
        CHECK(eastl::find(primer.benefits.begin(), primer.benefits.end(), benefit) != primer.benefits.end());
    }
    CHECK(notes.benefits[0] == SkillBookBenefit{fabrication, 1, 10, 15, 100, 15000});
    CHECK(primer.studyWork > 0);
    CHECK(notes.studyWork > 0);

    const auto printId = content.FindItem("workshop_primer_print");
    const auto notesPrintId = content.FindItem("salvage_notes_print");
    const auto readerId = content.FindItem("workshop_ebook_reader");
    REQUIRE(printId.value);
    REQUIRE(notesPrintId.value);
    REQUIRE(readerId.value);
    const auto& print = content.GetItem(printId);
    const auto& notesPrint = content.GetItem(notesPrintId);
    const auto& reader = content.GetItem(readerId);
    REQUIRE(print.books.size() == 1);
    REQUIRE(notesPrint.books.size() == 1);
    REQUIRE(reader.books.size() == 1);
    CHECK(print.books[0] == primerId);
    CHECK(notesPrint.books[0] == notesId);
    CHECK(reader.books[0] == primerId);
    const auto* store = ItemEnergyStore(reader, reader.readingPower);
    REQUIRE(store != nullptr);
    CHECK(store->initialEnergy > 0);
    CHECK_FALSE(store->rechargeable);

    for (const char* pool : {"loot_reading_room_supplies", "loot_hobby"}) {
        CHECK(PoolContains(content, pool, "workshop_primer_print"));
        CHECK(PoolContains(content, pool, "salvage_notes_print"));
    }
    CHECK(PoolContains(content, "loot_electronics", "workshop_ebook_reader"));
    for (const char* pool : {"loot_reading_room_supplies", "loot_hobby", "house_storage"}) {
        CHECK(PoolContains(content, pool, "boxing_manual"));
        CHECK(PoolContains(content, pool, "weapon_forms_manual"));
    }
    CHECK(PoolContains(content, "house_storage", "wooden_shield"));
    CHECK(PoolContains(content, "house_storage", "brass_knuckles"));
    CHECK(PoolContains(content, "loot_outdoors", "brass_knuckles"));
}

TEST_CASE("Town starter recipes and recyclers offer ungated training", "[content][skill-pack]") {
    const auto loaded = LoadContentPack(CATACLYSMX_CONTENT_PACK);
    REQUIRE(loaded.has_value());
    const auto& content = *loaded;
    for (const auto& entry : {eastl::pair<const char*, const char*>{"cut_cloth_bandage", "tailoring"},
                              eastl::pair<const char*, const char*>{"cut_timber_scrap", "fabrication"}}) {
        const auto id = content.FindCraftingRecipe(entry.first);
        REQUIRE(id.value);
        const auto& recipe = content.GetCraftingRecipe(id);
        CHECK(recipe.skills.empty());
        REQUIRE(recipe.training.size() == 1);
        CHECK(recipe.training[0] == SkillTraining{content.FindSkill(entry.second), 0, 100});
    }
    for (const auto& entry : {eastl::pair<const char*, const char*>{"cut_up_cotton_tshirt", "tailoring"},
                              eastl::pair<const char*, const char*>{"cut_up_denim_jeans", "tailoring"},
                              eastl::pair<const char*, const char*>{"cut_steel_sheet", "fabrication"},
                              eastl::pair<const char*, const char*>{"salvage_appliance_motor", "mechanics"}}) {
        const auto id = content.FindRecyclingRecipe(entry.first);
        REQUIRE(id.value);
        const auto& recipe = content.GetRecyclingRecipe(id);
        CHECK(recipe.skills.empty());
        REQUIRE(recipe.training.size() == 1);
        CHECK(recipe.training[0] == SkillTraining{content.FindSkill(entry.second), 0, 100});
    }
    for (const auto& entry : {eastl::pair<const char*, const char*>{"salvage_water_pump", "mechanics"},
                              eastl::pair<const char*, const char*>{"dismantle_computer_power_supply", "electronics"}}) {
        const auto id = content.FindRecyclingRecipe(entry.first);
        REQUIRE(id.value);
        const auto& recipe = content.GetRecyclingRecipe(id);
        const auto skill = content.FindSkill(entry.second);
        REQUIRE(recipe.skills.size() == 1);
        CHECK(recipe.skills[0] == SkillRequirement{skill, 1});
        REQUIRE(recipe.training.size() == 1);
        CHECK(recipe.training[0] == SkillTraining{skill, 1, 100});
        CHECK(recipe.work == (eastl::string_view(entry.first) == "salvage_water_pump" ? 600u : 700u));
    }
}
}
