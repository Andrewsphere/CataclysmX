#include "client/crafting_menu.h"

#include <catch2/catch_test_macros.hpp>

namespace {
using namespace cataclysmx;

ContentResult<ContentCatalog> LearningCatalog() {
    const ContentSource source{"skill-ui.json5", R"({format_version:1,definitions:[
        {type:'skill',id:'making',name:'Making'},
        {type:'skill',id:'fighting',name:'Fighting',category:'combat'},
        {type:'item',id:'product',name:'Product',mass_g:0,volume_ml:1},
        {type:'recipe',id:'basic',name:'Basic',inputs:[],result:{item:'product',count:1},work:10,
         skills:[{skill:'making',level:1}],training:[{skill:'making',level:1,xp_per_work:100}],
         knowledge:{crafts_to_learn:2,disassemblies_to_learn:3,autolearn:[{skill:'making',level:4}]}},
        {type:'recipe',id:'advanced',name:'Advanced',inputs:[],result:{item:'product',count:1},work:10,
         skills:[{skill:'making',level:6}],training:[{skill:'making',level:6,xp_per_work:10000}]},
        {type:'recycling_recipe',id:'unmake',target:'product',work:5,returns:[],teaches_recipes:['basic'],
         skills:[{skill:'making',level:6}],training:[{skill:'making',level:6,xp_per_work:10000}]},
        {type:'book',id:'a',name:'Manual',study_work:100,recipes:['basic'],benefits:[
         {skill:'making',first_level:1,last_level:7,direct_xp:50,theory_xp:200,multiplier:20000}]},
        {type:'book',id:'b',name:'Manual',study_work:100,recipes:['basic'],benefits:[
         {skill:'making',first_level:1,last_level:7,direct_xp:50,theory_xp:200,multiplier:20000}]}
    ]})"};
    Diagnostics diagnostics;
    auto content = LoadContent({&source, 1}, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str());
    REQUIRE(content);
    return content;
}

const CraftingMenuRow* FindRow(const Vector<CraftingMenuRow>& rows, CommandKind kind,
    CraftingRecipeId recipe = {}) {
    for (const auto& row : rows) {
        if (row.command.kind == kind && (!recipe.value || row.command.recipe == recipe)) { return &row; }
    }
    return nullptr;
}

bool Contains(const Vector<String>& lines, const char* text) {
    for (const auto& line : lines) { if (line.find(text) != String::npos) { return true; } }
    return false;
}

TEST_CASE("Learning UI uses book identity and excludes consumed or ineligible study actions", "[skill-ui]") {
    const auto content = LearningCatalog(); REQUIRE(content);
    WorldView view;
    const auto a = content->FindBook("a"), b = content->FindBook("b");
    view.learning.books.push_back({a, true, false, true, 125000, 10000, 20000});
    view.learning.books.push_back({b, true, false, true});
    Vector<CraftingMenuRow> rows;
    CollectCraftingMenu(view, *content, Localization{}, rows);
    REQUIRE(rows.size() == 4);
    CHECK(rows[0].command.book == a);
    CHECK(rows[1].command.book == b);
    CHECK_FALSE(SameCraftingChoice(rows[0].command, rows[1].command));
    CHECK(rows[0].label.find("Resume study") != String::npos);
    CHECK(rows[0].label.find("12.5/100") != String::npos);
    CHECK(rows[0].label.find("clipped XP: direct 1, theory 2") != String::npos);
    const auto selected = rows[0].command;
    view.learning.books[0].completedWork += ratioScale;
    CollectCraftingMenu(view, *content, Localization{}, rows);
    CHECK(SameCraftingChoice(selected, rows[0].command));
    view.learning.books[0].used = true;
    view.learning.books[1].eligible = false;
    CollectCraftingMenu(view, *content, Localization{}, rows);
    CHECK(FindRow(rows, CommandKind::study_book) == nullptr);
    Vector<String> lines;
    CollectLearningSummary(view, *content, lines);
    CHECK(Contains(lines, "used; study benefit already consumed"));
    CHECK(Contains(lines, "recipe reference (also useful after study): Basic"));
    CHECK(Contains(lines, "outside teaching range"));
    view.learning.books[1].eligible = true;
    view.learning.books[1].accessible = false;
    CollectCraftingMenu(view, *content, Localization{}, rows);
    CHECK(FindRow(rows, CommandKind::study_book) == nullptr);
}

TEST_CASE("Learning UI offers a fieldless pause during busy study", "[skill-ui]") {
    const auto content = LearningCatalog(); REQUIRE(content);
    WorldView view;
    view.remainingTicks = 50; view.commandQueued = true;
    view.learning.activeBook = content->FindBook("b");
    view.learning.studyRemaining = 50;
    view.learning.studyPauseRequested = true;
    view.learning.books.push_back({view.learning.activeBook, true, false, true});
    Vector<CraftingMenuRow> rows;
    CollectCraftingMenu(view, *content, Localization{}, rows);
    const auto* pause = FindRow(rows, CommandKind::pause_study);
    REQUIRE(pause);
    CHECK(ValidCommandShape(pause->command));
    CHECK(pause->command.book.value == 0);
    CHECK(pause->command.item == 0);
    CHECK(pause->command.recipe.value == 0);
    CHECK(pause->label.find("pause requested") != String::npos);
    CHECK(pause->label.find("50 ticks") != String::npos);
    CHECK(FindRow(rows, CommandKind::study_book) == nullptr);
}

TEST_CASE("Learning recipe labels distinguish gates and show actual training ceilings", "[skill-ui]") {
    const auto content = LearningCatalog(); REQUIRE(content);
    WorldView view;
    const auto recipe = content->FindCraftingRecipe("basic");
    view.learning.recipes.push_back({recipe, false, false, false, 2500});
    SkillView skill;
    skill.progress.skill = content->FindSkill("making"); skill.progress.level = 5;
    skill.progress.theory = 20000; skill.requiredXp = 1800 * ratioScale;
    skill.effectiveLevel = 8;
    view.learning.skills.push_back(skill);
    InventoryEntry item; item.id = 77; item.type = content->FindItem("product");
    view.inventory.push_back(item);
    Vector<CraftingMenuRow> rows;
    CollectCraftingMenu(view, *content, Localization{}, rows);
    const auto* basic = FindRow(rows, CommandKind::craft, recipe); REQUIRE(basic);
    CHECK(basic->label.find("missing skills") != String::npos);
    CHECK(basic->label.find("missing recipe reference") != String::npos);
    CHECK(basic->label.find("memorization 25%") != String::npos);
    CHECK(basic->label.find("2 crafts / 3 disassemblies") != String::npos);
    CHECK(basic->label.find("autolearn requires recipe skills + Making 4") != String::npos);
    CHECK(basic->label.find("training cap 2: no XP (learned level at cap)") != String::npos);
    const auto* advanced = FindRow(rows, CommandKind::craft, content->FindCraftingRecipe("advanced")); REQUIRE(advanced);
    CHECK(advanced->label.find("training cap 7: 1 XP/work practice") != String::npos);
    const auto* recycle = FindRow(rows, CommandKind::recycle); REQUIRE(recycle);
    CHECK(recycle->label.find("training cap 7: 1 XP/work theory") != String::npos);
    view.learning.recipes[0] = {recipe, false, true, true, 5000};
    CollectCraftingMenu(view, *content, Localization{}, rows);
    basic = FindRow(rows, CommandKind::craft, recipe); REQUIRE(basic);
    CHECK(basic->label.find("missing skills") == String::npos);
    CHECK(basic->label.find("missing recipe reference") == String::npos);
    CHECK(basic->label.find("reference available") != String::npos);
    for (const auto& row : rows) { CHECK(ValidCommandShape(row.command)); }
    view.learning.recipes[0].known = true;
    view.learning.recipes[0].referenceAvailable = false;
    CollectCraftingMenu(view, *content, Localization{}, rows);
    basic = FindRow(rows, CommandKind::craft, recipe); REQUIRE(basic);
    CHECK(basic->label.find("[known]") != String::npos);
    CHECK(basic->label.find("missing recipe reference") == String::npos);
}

TEST_CASE("Learning summary is read only reusable and preserves fixed point progress", "[skill-ui]") {
    const auto content = LearningCatalog(); REQUIRE(content);
    WorldView view;
    SkillView skill;
    skill.progress.skill = content->FindSkill("making"); skill.progress.level = 2;
    skill.progress.xp = 12500; skill.progress.directBookXp = 10000; skill.progress.multiplier = 20000;
    skill.effectiveLevel = 4; skill.requiredXp = 400 * ratioScale;
    view.learning.skills.push_back(skill);
    const auto before = view.learning;
    Vector<String> lines; lines.reserve(20);
    CollectLearningSummary(view, *content, lines);
    CHECK(Contains(lines, "learned 2/10 | effective 4 | XP 1.25/400"));
    CHECK(Contains(lines, "direct book XP 1/120"));
    CHECK(Contains(lines, "study boost 2x until next level"));
    CHECK(view.learning == before);
    CHECK(lines.capacity() >= 20);
    auto& advanced = view.learning.skills[0];
    advanced.progress.level = 5; advanced.progress.theory = 20000; advanced.progress.practice = 0;
    advanced.progress.directBookXp = 0; advanced.progress.multiplier = ratioScale;
    CollectLearningSummary(view, *content, lines);
    CHECK(Contains(lines, "theory 2, practice 0"));
    CHECK(Contains(lines, "paired theory + practice advances XP"));
    CHECK_FALSE(Contains(lines, "direct book XP"));
    SkillView combat;
    combat.progress.skill = content->FindSkill("fighting"); combat.progress.multiplier = 12000;
    combat.requiredXp = 100 * ratioScale;
    view.learning.skills.push_back(combat);
    CollectLearningSummary(view, *content, lines);
    CHECK(Contains(lines, "combat: books grant no direct/theory XP; boost cap 1.2x"));
    CHECK(Contains(lines, "study boost 1.2x until next level"));
    CollectLearningSummary({}, *content, lines);
    CHECK_FALSE(Contains(lines, "Making | learned"));
    CHECK_FALSE(Contains(lines, "Fighting | learned"));
    CHECK(Contains(lines, "Attack rating = 0.4 x effective melee"));
    CHECK(Contains(lines, "Training: equal-or-harder opponent 100%"));
    Vector<CraftingMenuRow> rows;
    CollectCraftingMenu({}, *content, Localization{}, rows);
    REQUIRE(rows.size() == 2);
    CHECK(FindRow(rows, CommandKind::study_book) == nullptr);
    CHECK(rows[0].label.find("missing skills") == String::npos);
}

TEST_CASE("Learning UI shows skill ceilings and completed study states", "[skill-ui]") {
    const ContentSource source{"caps-ui.json5", R"({format_version:1,definitions:[
        {type:'skill',id:'cooking',name:'Cooking',max_level:5},
        {type:'item',id:'food',name:'Food',mass_g:0,volume_ml:1},
        {type:'recipe',id:'meal',name:'Meal',inputs:[],result:{item:'food',count:1},work:10,
         skills:[{skill:'cooking',level:5}],training:[{skill:'cooking',level:5,xp_per_work:100}]},
        {type:'book',id:'book',name:'Book',study_work:100,
         benefits:[{skill:'cooking',first_level:1,last_level:5,direct_xp:1}]}
    ]})"};
    Diagnostics diagnostics;
    auto content = LoadContent({&source, 1}, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str()); REQUIRE(content);
    WorldView view;
    SkillView skill; skill.progress.skill = content->FindSkill("cooking"); skill.progress.level = 5;
    skill.effectiveLevel = 7; view.learning.skills.push_back(skill);
    view.learning.books.push_back({content->FindBook("book"), true, false, false, 100 * ratioScale});
    Vector<String> lines;
    CollectLearningSummary(view, *content, lines);
    CHECK(Contains(lines, "learned 5/5 | effective 7 | maximum reached"));
    CHECK_FALSE(Contains(lines, "XP 0/0"));
    CHECK_FALSE(Contains(lines, "paired theory"));
    CHECK(Contains(lines, "completed study; no eligible benefit"));
    view.learning.books[0].eligible = true;
    CollectLearningSummary(view, *content, lines);
    CHECK(Contains(lines, "completed study; eligible benefit"));
    view.learning.books[0].completedWork = ratioScale;
    CollectLearningSummary(view, *content, lines);
    CHECK(Contains(lines, "partial study"));
    Vector<CraftingMenuRow> rows;
    CollectCraftingMenu(view, *content, Localization{}, rows);
    const auto* meal = FindRow(rows, CommandKind::craft, content->FindCraftingRecipe("meal"));
    REQUIRE(meal);
    CHECK(meal->label.find("training cap 5: no XP") != String::npos);
}

TEST_CASE("Learning UI labels authoritative paused job gates", "[skill-ui]") {
    const auto content = LearningCatalog(); REQUIRE(content);
    WorldView view;
    CraftingEntry job; job.id = 500; job.craftingRecipe = content->FindCraftingRecipe("basic");
    job.status = CraftingStatus::MissingSkills;
    view.craftingJobs.push_back(job);
    Vector<CraftingMenuRow> rows;
    CollectCraftingMenu(view, *content, Localization{}, rows);
    const auto* resume = FindRow(rows, CommandKind::resume_craft); REQUIRE(resume);
    CHECK(resume->label.find("[missing skills]") != String::npos);
    view.craftingJobs[0].status = CraftingStatus::MissingRecipeReference;
    CollectCraftingMenu(view, *content, Localization{}, rows);
    resume = FindRow(rows, CommandKind::resume_craft); REQUIRE(resume);
    CHECK(resume->label.find("[missing recipe reference]") != String::npos);
}
}
