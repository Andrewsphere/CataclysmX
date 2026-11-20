#include "content/content.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <initializer_list>

namespace {
using namespace cataclysmx;

eastl::string_view View(const String& text) { return {text.data(), text.size()}; }

String Document(eastl::string_view definitions) {
    String text = "{format_version:1,definitions:[";
    text.append(definitions.data(), definitions.size());
    text += "]}";
    return text;
}

ContentResult<ContentCatalog> Load(eastl::string_view definitions, Diagnostics* diagnostics = nullptr) {
    const auto text = Document(definitions);
    const ContentSource source{"skills.json5", View(text)};
    return LoadContent({&source, 1}, diagnostics);
}

String WithSkills(eastl::string_view definitions) {
    String text = R"({type:'skill',id:'alpha',name:'Alpha'},
        {type:'skill',id:'beta',name:'Beta'},
        {type:'skill',id:'combat',name:'Combat',category:'combat'},
        {type:'item',id:'product',name:'Product',mass_g:0,volume_ml:1})";
    if (!definitions.empty()) { text += ','; text.append(definitions.data(), definitions.size()); }
    return text;
}

String Recipe(eastl::string_view fields) {
    String text = "{type:'recipe',id:'make',inputs:[],result:{item:'product',count:1},work:1";
    if (!fields.empty()) { text += ','; text.append(fields.data(), fields.size()); }
    text += '}';
    return text;
}

void Invalid(eastl::string_view definitions, eastl::string_view path = {}) {
    Diagnostics diagnostics;
    const auto loaded = Load(definitions, &diagnostics);
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error() == ContentError::InvalidData);
    REQUIRE_FALSE(diagnostics.Empty());
    if (!path.empty()) {
        REQUIRE(diagnostics.Frames()[0].location.has_value());
        CHECK(diagnostics.Frames()[0].location->file == "skills.json5");
        CHECK(View(diagnostics.Frames()[0].location->path) == path);
    }
    const auto quiet = Load(definitions);
    REQUIRE_FALSE(quiet.has_value());
    CHECK(quiet.error() == ContentError::InvalidData);
}

TEST_CASE("Skill ceilings normalize default and explicit curves independently of field order", "[content][skill-content]") {
    const SkillDefinition defaults;
    CHECK(defaults.maxLevel == 10);
    for (const unsigned maximum : {1u, 5u, 6u, 10u}) {
        CAPTURE(maximum);
        char maximumText[16];
        std::snprintf(maximumText, sizeof(maximumText), "%u", maximum);
        String base = "{type:'skill',id:'limited',name:'Limited',max_level:";
        base += maximumText;
        base += '}';
        String curve = "[";
        for (unsigned i = 0; i < maximum; ++i) {
            char xp[32];
            std::snprintf(xp, sizeof(xp), "%u", defaults.xpToNext[i]);
            if (i) { curve += ','; }
            curve += xp;
        }
        curve += ']';
        String explicitCurve = "{type:'skill',id:'limited',name:'Limited',xp_to_next:";
        explicitCurve += curve;
        explicitCurve += ",max_level:";
        explicitCurve += maximumText;
        explicitCurve += '}';
        const auto implicit = Load(View(base));
        const auto explicitLoaded = Load(View(explicitCurve));
        REQUIRE(implicit);
        REQUIRE(explicitLoaded);
        const auto& skill = implicit->GetSkill({1});
        CHECK(skill.maxLevel == maximum);
        CHECK(skill.xpToNext == explicitLoaded->GetSkill({1}).xpToNext);
        for (unsigned i = 0; i < maximumSkillLevel; ++i) {
            CHECK(skill.xpToNext[i] == (i < maximum ? defaults.xpToNext[i] : 0));
        }
        // Exact lengths are relative to the authored maximum, not storage capacity.
        for (const unsigned length : {maximum - 1, maximum + 1}) {
            String invalid = "{type:'skill',id:'limited',name:'Limited',max_level:";
            invalid += maximumText;
            invalid += ",xp_to_next:[";
            for (unsigned i = 0; i < length; ++i) {
                if (i) { invalid += ','; }
                invalid += '1';
            }
            invalid += "]}";
            Invalid(View(invalid), "definitions[0].xp_to_next");
        }
    }
    for (const char* maximum : {"0", "11", "-1", "1.5", "true", "null", "'5'", "4294967296"}) {
        String invalid = "{type:'skill',id:'limited',name:'Limited',max_level:";
        invalid += maximum;
        invalid += '}';
        Invalid(View(invalid), "definitions[0].max_level");
    }
    Invalid("{type:'skill',id:'limited',name:'Limited',max_level:5,max_level:5}");
    for (const char* xp : {"0", "1000000001", "-1", "1.5", "true", "null"}) {
        String invalid = "{type:'skill',id:'limited',name:'Limited',xp_to_next:[";
        invalid += xp;
        invalid += "],max_level:1}";
        Invalid(View(invalid), "definitions[0].xp_to_next[0]");
    }
    const auto boundary = Load("{type:'skill',id:'limited',name:'Limited',xp_to_next:[1,1000000000,1,1,1],max_level:5}");
    REQUIRE(boundary);
    CHECK(boundary->GetSkill({1}).xpToNext[1] == 1000000000);
}

TEST_CASE("Resolved skill gates reject levels above forward referenced ceilings", "[content][skill-content]") {
    struct Example { const char* definition; const char* path; };
    for (const auto& example : {
        Example{"{type:'recipe',id:'make',inputs:[],result:{item:'product',count:1},work:1,skills:[{skill:'limited',level:6}]}",
                "definitions[0].skills[0].level"},
        Example{"{type:'recipe',id:'make',inputs:[],result:{item:'product',count:1},work:1,training:[{skill:'limited',level:6}]}",
                "definitions[0].training[0].level"},
        Example{"{type:'recipe',id:'make',inputs:[],result:{item:'product',count:1},work:1,knowledge:{autolearn:[{skill:'limited',level:6}]}}",
                "definitions[0].knowledge.autolearn[0].level"},
        Example{"{type:'recycling_recipe',id:'unmake',target:'product',work:1,returns:[],skills:[{skill:'limited',level:6}]}",
                "definitions[0].skills[0].level"},
        Example{"{type:'recycling_recipe',id:'unmake',target:'product',work:1,returns:[],training:[{skill:'limited',level:6}]}",
                "definitions[0].training[0].level"},
        Example{"{type:'recipe',id:'make',inputs:[],result:{item:'product',count:1},work:1,recycling:{id:'unmake',work:1,efficiency:0,skills:[{skill:'limited',level:6}]}}",
                "definitions[0].recycling.skills[0].level"},
        Example{"{type:'recipe',id:'make',inputs:[],result:{item:'product',count:1},work:1,recycling:{id:'unmake',work:1,efficiency:0,training:[{skill:'limited',level:6}]}}",
                "definitions[0].recycling.training[0].level"},
        Example{"{type:'book',id:'manual',name:'Manual',study_work:1,benefits:[{skill:'limited',first_level:6,last_level:6}]}",
                "definitions[0].benefits[0].first_level"},
        Example{"{type:'book',id:'manual',name:'Manual',study_work:1,benefits:[{skill:'limited',first_level:1,last_level:6}]}",
                "definitions[0].benefits[0].last_level"}
    }) {
        CAPTURE(example.definition);
        String definitions = example.definition;
        definitions += R"(,{type:'skill',id:'limited',name:'Limited',max_level:5},
            {type:'item',id:'product',name:'Product',mass_g:0,volume_ml:1})";
        Invalid(View(definitions), example.path);
        Diagnostics diagnostics;
        const auto loaded = Load(View(definitions), &diagnostics);
        REQUIRE_FALSE(loaded);
        CHECK(diagnostics.Frames()[0].message.find("limited") != String::npos);
        CHECK(diagnostics.Frames()[0].message.find("maximum 5") != String::npos);
    }
}

TEST_CASE("Capped skill references retain inclusive bounds and inherited default training", "[content][skill-content]") {
    const auto loaded = Load(R"(
        {type:'recipe',id:'make',inputs:[],result:{item:'product',count:1},work:1,
         skills:[{skill:'limited',level:5}],knowledge:{autolearn:[{skill:'limited',level:5}]},
         recycling:{id:'unmake',work:1,efficiency:0,skills:[{skill:'limited',level:5}]}},
        {type:'recipe',id:'intro',inputs:[],result:{item:'product',count:1},work:1,
         training:[{skill:'limited',level:0}],knowledge:{autolearn:[{skill:'limited',level:0}]}},
        {type:'recycling_recipe',id:'explicit',target:'other_product',work:1,returns:[],
         skills:[{skill:'limited',level:5}],teaches_recipes:[]},
        {type:'book',id:'manual',name:'Manual',study_work:1,benefits:[{skill:'limited',first_level:1,last_level:5}]},
        {type:'skill',id:'limited',name:'Limited',max_level:5},
        {type:'item',id:'product',name:'Product',mass_g:0,volume_ml:1},
        {type:'item',id:'other_product',name:'Other product',mass_g:0,volume_ml:1})");
    REQUIRE(loaded);
    const auto skill = loaded->FindSkill("limited");
    const auto& craft = loaded->GetCraftingRecipe(loaded->FindCraftingRecipe("make"));
    REQUIRE(craft.training.size() == 1);
    CHECK(craft.training[0] == SkillTraining{skill, 5, 100});
    CHECK(craft.autolearnSkills[0] == SkillRequirement{skill, 5});
    for (const char* id : {"unmake", "explicit"}) {
        const auto& recycler = loaded->GetRecyclingRecipe(loaded->FindRecyclingRecipe(id));
        REQUIRE(recycler.training.size() == 1);
        CHECK(recycler.training[0] == SkillTraining{skill, 5, 100});
    }
    CHECK(loaded->GetBook({1}).benefits[0].lastLevel == 5);
}

TEST_CASE("Skills books and carriers resolve forward across sources with canonical IDs", "[content][skill-content]") {
    const auto first = Document(R"(
        {type:'book',id:'z_manual',name:'Other manual',study_work:4294967295,recipes:['make']},
        {type:'book',id:'a_manual',name:'Manual',study_work:1,recipes:['make'],benefits:[
            {skill:'combat',first_level:1,last_level:10,multiplier:12000},
            {skill:'alpha',first_level:1,last_level:10,direct_xp:1000000000,theory_xp:1000000000,multiplier:40000}]},
        {type:'item',id:'print',name:'Printed',mass_g:0,volume_ml:1,books:['z_manual','a_manual']},
        {type:'item',id:'electronic',name:'Electronic',mass_g:0,volume_ml:1,books:['a_manual'],reading_power:'battery',
         capabilities:[{id:'battery',type:'energy_store',capacity_energy:10,rechargeable:false}]},
        {type:'recipe',id:'make',inputs:[],result:{item:'product',count:1},work:1,skills:[{skill:'alpha',level:1}],
         knowledge:{autolearn:[{skill:'alpha',level:1}]}}
    )");
    String definitions = WithSkills("");
    definitions += ",{type:'skill',id:'custom',name:'Custom',xp_to_next:[1,2,3,4,5,6,7,8,9,1000000000]}";
    const auto second = Document(View(definitions));
    const ContentSource sources[] = {{"first.json5", View(first)}, {"second.json5", View(second)}};
    const ContentSource reversed[] = {sources[1], sources[0]};
    const auto loaded = LoadContent({sources, 2});
    const auto reordered = LoadContent({reversed, 2});
    REQUIRE(loaded.has_value());
    REQUIRE(reordered.has_value());
    CHECK(loaded->SkillCount() == 4);
    CHECK(loaded->BookCount() == 2);
    CHECK(loaded->FindSkill("alpha") == SkillId{1});
    CHECK(loaded->FindBook("a_manual") == BookId{1});
    CHECK(loaded->FindBook("z_manual") == reordered->FindBook("z_manual"));
    CHECK(loaded->FindSkill("custom") == reordered->FindSkill("custom"));
    CHECK(loaded->GetSkill(loaded->FindSkill("custom")).xpToNext[9] == 1000000000);
    CHECK(loaded->GetSkill({1}).category == SkillCategory::Noncombat);
    CHECK(loaded->GetSkill({1}).xpToNext[0] == 100);
    const auto recipe = loaded->FindCraftingRecipe("make");
    CHECK(loaded->GetBook({1}).recipes[0] == recipe);
    CHECK(loaded->GetBook({2}).recipes[0] == recipe);
    CHECK(loaded->GetBook({2}).studyWork == UINT32_MAX);
    REQUIRE(loaded->GetBook({1}).benefits.size() == 2);
    CHECK(loaded->GetBook({1}).benefits[0].skill == SkillId{1});
    CHECK(loaded->GetBook({1}).benefits[1].lastLevel == 10);
    const auto& print = loaded->GetItem(loaded->FindItem("print"));
    const auto& electronic = loaded->GetItem(loaded->FindItem("electronic"));
    REQUIRE(print.books.size() == 2);
    CHECK(print.books[0] == electronic.books[0]);
    CHECK(print.books[1] == BookId{2});
    CHECK(print.readingPower == ItemCapabilityId{});
    CHECK(electronic.readingPower == FindItemCapability(electronic, "battery"));
    REQUIRE(ItemEnergyStore(electronic, electronic.readingPower) != nullptr);
    auto copy = *loaded;
    CHECK(copy.GetItem(copy.FindItem("electronic")) == electronic);
    CHECK(copy.GetBook({1}).benefits == loaded->GetBook({1}).benefits);
    auto changed = electronic;
    changed.books.clear();
    CHECK_FALSE(changed == electronic);
    changed = electronic;
    changed.readingPower = {};
    CHECK_FALSE(changed == electronic);
    CHECK(loaded->FindSkill("missing") == SkillId{});
    CHECK(loaded->FindBook("missing") == BookId{});
    CHECK(loaded->TryGetSkill({}) == nullptr);
    CHECK(loaded->TryGetSkill({5}) == nullptr);
    CHECK(loaded->TryGetBook({}) == nullptr);
    CHECK(loaded->TryGetBook({3}) == nullptr);
}

TEST_CASE("Recipe training merges per-skill overrides and preserves zero rewards", "[content][skill-content]") {
    const auto recipe = Recipe(R"(skills:[{skill:'beta',level:10},{skill:'alpha',level:1}],
        training:[{skill:'combat',level:0,xp_per_work:10000},{skill:'beta',level:10,xp_per_work:0}],
        knowledge:{autolearn:[{skill:'beta',level:10},{skill:'alpha',level:1}],crafts_to_learn:65535,disassemblies_to_learn:1},
        recycling:{id:'unmake',work:1,efficiency:10000,skills:[{skill:'beta',level:1},{skill:'alpha',level:10}],
          training:[{skill:'alpha',level:10,xp_per_work:10000},{skill:'combat',level:0,xp_per_work:0}]})");
    const auto definitions = WithSkills(View(recipe));
    const auto loaded = Load(View(definitions));
    REQUIRE(loaded.has_value());
    const auto& craft = loaded->GetCraftingRecipe({1});
    REQUIRE(craft.skills.size() == 2);
    CHECK(craft.skills[0] == SkillRequirement{loaded->FindSkill("alpha"), 1});
    REQUIRE(craft.training.size() == 3);
    CHECK(craft.training[0] == SkillTraining{loaded->FindSkill("alpha"), 1, 100});
    CHECK(craft.training[1] == SkillTraining{loaded->FindSkill("beta"), 10, 0});
    CHECK(craft.training[2] == SkillTraining{loaded->FindSkill("combat"), 0, 10000});
    CHECK_FALSE(craft.knownByDefault);
    CHECK(craft.autolearn);
    CHECK(craft.autolearnSkills == craft.skills);
    CHECK(craft.craftsToLearn == 65535);
    CHECK(craft.disassembliesToLearn == 1);
    const auto& recycle = loaded->GetRecyclingRecipe({1});
    REQUIRE(recycle.training.size() == 3);
    CHECK(recycle.training[0] == SkillTraining{loaded->FindSkill("alpha"), 10, 10000});
    CHECK(recycle.training[1] == SkillTraining{loaded->FindSkill("beta"), 1, 100});
    CHECK(recycle.training[2] == SkillTraining{loaded->FindSkill("combat"), 0, 0});
    REQUIRE(recycle.teachesRecipes.size() == 1);
    CHECK(recycle.teachesRecipes[0] == CraftingRecipeId{1});
}

TEST_CASE("Knowledge omission empty objects and empty autolearn have distinct defaults", "[content][skill-content]") {
    for (const char* fields : {"", "knowledge:{}", "knowledge:{autolearn:[]}",
                               "knowledge:{known_by_default:true,autolearn:[{skill:'alpha',level:0}]}",
                               "skills:[{skill:'alpha',level:10}],training:[],knowledge:{autolearn:[]}"}) {
        CAPTURE(fields);
        const auto recipe = Recipe(fields);
        const auto definitions = WithSkills(View(recipe));
        const auto loaded = Load(View(definitions));
        REQUIRE(loaded.has_value());
        const auto& craft = loaded->GetCraftingRecipe({1});
        const eastl::string_view view = fields;
        CHECK(craft.knownByDefault == (view.empty() || view.find("known_by_default:true") != view.npos));
        CHECK(craft.autolearn == (view.find("autolearn") != view.npos));
        CHECK(craft.craftsToLearn == 2);
        CHECK(craft.disassembliesToLearn == 3);
        if (!craft.skills.empty()) {
            REQUIRE(craft.training.size() == 1);
            CHECK(craft.training[0] == SkillTraining{loaded->FindSkill("alpha"), 10, 100});
        }
    }
    const auto empty = Load("");
    REQUIRE(empty.has_value());
    CHECK(empty->SkillCount() == 0);
    CHECK(empty->BookCount() == 0);
}

TEST_CASE("Book XP and multipliers accept inclusive limits and retain defaults", "[content][skill-content]") {
    const auto definitions = WithSkills(R"(
        {type:'book',id:'manual',name:'Manual',study_work:1,benefits:[
          {skill:'combat',first_level:1,last_level:10,direct_xp:0,theory_xp:0,multiplier:12000},
          {skill:'beta',first_level:10,last_level:10,direct_xp:1000000000,theory_xp:1000000000,multiplier:40000},
          {skill:'alpha',first_level:1,last_level:1}]})");
    const auto loaded = Load(View(definitions));
    REQUIRE(loaded.has_value());
    const auto& benefits = loaded->GetBook({1}).benefits;
    REQUIRE(benefits.size() == 3);
    CHECK(benefits[0].directXp == 0);
    CHECK(benefits[0].theoryXp == 0);
    CHECK(benefits[0].multiplier == 10000);
    CHECK(benefits[1].directXp == 1000000000);
    CHECK(benefits[1].theoryXp == 1000000000);
    CHECK(benefits[1].multiplier == 40000);
    CHECK(benefits[2].firstLevel == 1);
    CHECK(benefits[2].lastLevel == 10);
    CHECK(benefits[2].multiplier == 12000);
}

TEST_CASE("Ambiguous legacy recyclers teach nothing while generated ones teach their source", "[content][skill-content]") {
    const auto definitions = WithSkills(R"(
        {type:'recipe',id:'z_make',inputs:[],result:{item:'product',count:1},work:1},
        {type:'recipe',id:'a_make',inputs:[],result:{item:'product',count:1},work:1},
        {type:'recycling_recipe',id:'unmake',target:'product',work:1,returns:[],
         skills:[{skill:'alpha',level:1}]})");
    const auto loaded = Load(View(definitions));
    REQUIRE(loaded.has_value());
    const auto& recycle = loaded->GetRecyclingRecipe({1});
    CHECK(recycle.teachesRecipes.empty());
    CHECK(recycle.training[0].xpPerWork == 100);
    const auto generated = WithSkills(R"(
        {type:'recipe',id:'z_make',inputs:[],result:{item:'product',count:1},work:1,
         recycling:{id:'unmake',work:1,efficiency:0}},
        {type:'recipe',id:'a_make',inputs:[],result:{item:'product',count:1},work:1})");
    const auto exact = Load(View(generated));
    REQUIRE(exact.has_value());
    REQUIRE(exact->GetRecyclingRecipe({1}).teachesRecipes.size() == 1);
    CHECK(exact->GetRecyclingRecipe({1}).teachesRecipes[0] == exact->FindCraftingRecipe("z_make"));
    const auto unmatched = WithSkills("{type:'recycling_recipe',id:'unmake',target:'product',work:1,returns:[]}");
    const auto noRecipes = Load(View(unmatched));
    REQUIRE(noRecipes.has_value());
    CHECK(noRecipes->GetRecyclingRecipe({1}).teachesRecipes.empty());
}

TEST_CASE("Explicit recycler knowledge overrides inference including empty lists", "[content][skill-content]") {
    for (const char* teaches : {"[]", "['z_make']", "['z_make','a_make']"}) {
        CAPTURE(teaches);
        String definitions = WithSkills(R"(
            {type:'recipe',id:'z_make',inputs:[],result:{item:'product',count:1},work:1,knowledge:{}},
            {type:'recipe',id:'a_make',inputs:[],result:{item:'product',count:1},work:1},
            {type:'recycling_recipe',id:'unmake',target:'product',work:1,returns:[],teaches_recipes:)");
        definitions += teaches;
        definitions += '}';
        const auto loaded = Load(View(definitions));
        REQUIRE(loaded.has_value());
        const auto& learning = loaded->GetRecyclingRecipe({1}).teachesRecipes;
        if (eastl::string_view(teaches) == "[]") { CHECK(learning.empty()); }
        else if (eastl::string_view(teaches) == "['z_make']") {
            REQUIRE(learning.size() == 1);
            CHECK(learning[0] == loaded->FindCraftingRecipe("z_make"));
        } else {
            REQUIRE(learning.size() == 2);
            CHECK(learning[0] == loaded->FindCraftingRecipe("a_make"));
            CHECK(learning[1] == loaded->FindCraftingRecipe("z_make"));
        }
    }
    for (const char* teaches : {"[]", "['a_make']"}) {
        String definitions = WithSkills(R"(
            {type:'recipe',id:'z_make',inputs:[],result:{item:'product',count:1},work:1,knowledge:{},
             recycling:{id:'unmake',work:1,efficiency:0,teaches_recipes:)");
        definitions += teaches;
        definitions += R"(}},{type:'recipe',id:'a_make',inputs:[],result:{item:'product',count:1},work:1})";
        const auto loaded = Load(View(definitions));
        REQUIRE(loaded.has_value());
        const auto& learning = loaded->GetRecyclingRecipe({1}).teachesRecipes;
        if (eastl::string_view(teaches) == "[]") { CHECK(learning.empty()); }
        else {
            REQUIRE(learning.size() == 1);
            CHECK(learning[0] == loaded->FindCraftingRecipe("a_make"));
        }
    }
}

TEST_CASE("Explicit recycler inference requires one candidate or unambiguous legacy compatibility", "[content][skill-content]") {
    for (const char* knowledge : {"", ",knowledge:{}"}) {
        String definitions = WithSkills("{type:'recipe',id:'make',inputs:[],result:{item:'product',count:1},work:1");
        definitions += knowledge;
        definitions += "},{type:'recycling_recipe',id:'unmake',target:'product',work:1,returns:[]}";
        const auto loaded = Load(View(definitions));
        REQUIRE(loaded.has_value());
        REQUIRE(loaded->GetRecyclingRecipe({1}).teachesRecipes.size() == 1);
        CHECK(loaded->GetRecyclingRecipe({1}).teachesRecipes[0] == loaded->FindCraftingRecipe("make"));
    }
    for (const char* gatedId : {"a_make", "z_make"}) {
        String definitions = WithSkills("{type:'recipe',id:'a_make',inputs:[],result:{item:'product',count:1},work:1");
        if (eastl::string_view(gatedId) == "a_make") { definitions += ",knowledge:{}"; }
        definitions += "},{type:'recipe',id:'z_make',inputs:[],result:{item:'product',count:1},work:1";
        if (eastl::string_view(gatedId) == "z_make") { definitions += ",knowledge:{}"; }
        definitions += "},{type:'recycling_recipe',id:'unmake',target:'product',work:1,returns:[]}";
        Invalid(View(definitions), "definitions[6].teaches_recipes");
        Diagnostics diagnostics;
        const auto loaded = Load(View(definitions), &diagnostics);
        REQUIRE_FALSE(loaded.has_value());
        CHECK(diagnostics.Frames()[0].message.find("explicit teaches_recipes") != String::npos);
        CHECK(diagnostics.Frames()[0].message.find("explicit teaches_recipes") != String::npos);
    }
    for (const char* teaches : {"['missing']", "['make','make']", "null", "true"}) {
        String definitions = WithSkills("{type:'recipe',id:'make',inputs:[],result:{item:'product',count:1},work:1},");
        definitions += "{type:'recycling_recipe',id:'unmake',target:'product',work:1,returns:[],teaches_recipes:";
        definitions += teaches;
        definitions += '}';
        Invalid(View(definitions));
    }
}

TEST_CASE("Skill recipe validation rejects duplicate references invalid ranges and unknown fields", "[content][skill-content]") {
    for (const char* fields : {
        "skills:[{skill:'missing',level:1}]", "skills:[{skill:'alpha',level:0}]",
        "skills:[{skill:'alpha',level:11}]", "skills:[{skill:'alpha',level:-1}]",
        "skills:[{skill:'alpha',level:1.5}]", "skills:[{skill:'alpha',level:true}]",
        "skills:[{skill:'alpha',level:1},{skill:'alpha',level:2}]",
        "skills:[{skill:'alpha',level:1,extra:0}]", "skills:{}",
        "training:[{skill:'missing',level:0}]", "training:[{skill:'alpha',level:1}]",
        "training:[{skill:'alpha',level:11}]", "training:[{skill:'alpha',level:-1}]",
        "training:[{skill:'alpha',level:0,xp_per_work:10001}]",
        "training:[{skill:'alpha',level:0,xp_per_work:-1}]",
        "training:[{skill:'alpha',level:0},{skill:'alpha',level:0}]",
        "skills:[{skill:'alpha',level:2}],training:[{skill:'alpha',level:1}]",
        "skills:[{skill:'alpha',level:2}],training:[{skill:'alpha',level:0,xp_per_work:0}]",
        "knowledge:{autolearn:[{skill:'alpha',level:11}]}",
        "knowledge:{autolearn:[{skill:'missing',level:0}]}",
        "knowledge:{autolearn:[{skill:'alpha',level:0},{skill:'alpha',level:0}]}",
        "skills:[{skill:'alpha',level:2}],knowledge:{autolearn:[{skill:'alpha',level:1}]}",
        "knowledge:{autolearn:true}", "knowledge:{autolearn:null}",
        "knowledge:{known_by_default:0}", "knowledge:{crafts_to_learn:0}",
        "knowledge:{crafts_to_learn:65536}", "knowledge:{disassemblies_to_learn:0}",
        "knowledge:{disassemblies_to_learn:65536}", "knowledge:{extra:true}",
        "knowledge:{crafts_to_learn:1,crafts_to_learn:2}"
    }) {
        CAPTURE(fields);
        const auto recipe = Recipe(fields);
        const auto definitions = WithSkills(View(recipe));
        Invalid(View(definitions));
    }
    const auto recipe = Recipe("skills:[{skill:'missing',level:1}]");
    const auto definitions = WithSkills(View(recipe));
    Invalid(View(definitions), "definitions[4].skills[0].skill");
}

TEST_CASE("Skills books and reading carriers enforce their closed schemas", "[content][skill-content]") {
    for (const char* definition : {
        "{type:'skill',id:'new',name:'New',category:'other'}",
        "{type:'skill',id:'new',name:'New',xp_to_next:[1]}",
        "{type:'skill',id:'new',name:'New',xp_to_next:[0,1,1,1,1,1,1,1,1,1]}",
        "{type:'skill',id:'new',name:'New',xp_to_next:[1000000001,1,1,1,1,1,1,1,1,1]}",
        "{type:'skill',id:'new',name:'New',xp_to_next:[1,1,1,1,1,1,1,1,1,1,1]}",
        "{type:'skill',id:'new',name:'New',extra:0}",
        "{type:'skill',id:'product',name:'Collision'}",
        "{type:'book',id:'alpha',name:'Collision',study_work:1}",
        "{type:'skill',id:'new',name:'New',xp_to_next:[1,1,1,1,1,1,1,1,1,1000000001]}",
        "{type:'skill',id:'new',name:'New',unknown:1}",
        "{type:'skill',id:'alpha',name:'Duplicate'}",
        "{type:'book',id:'alpha',name:'Collision',study_work:1}",
        "{type:'book',id:'manual',name:'Manual',study_work:0}",
        "{type:'book',id:'manual',name:'Manual',study_work:4294967296}",
        "{type:'book',id:'manual',name:'Manual',study_work:1,recipes:['missing']}",
        "{type:'book',id:'manual',name:'Manual',study_work:1,recipes:['make','make']}",
        "{type:'item',id:'carrier',name:'Carrier',mass_g:0,volume_ml:1,books:['missing']}",
        "{type:'item',id:'carrier',name:'Carrier',mass_g:0,volume_ml:1,books:['manual','manual']}",
        "{type:'item',id:'carrier',name:'Carrier',mass_g:0,volume_ml:1,reading_power:'power'}",
        "{type:'item',id:'carrier',name:'Carrier',mass_g:0,volume_ml:1,reading_power:'power',capabilities:[{id:'power',type:'energy_store',capacity_energy:1,rechargeable:false}]}",
        "{type:'book',id:'manual',name:'Manual',study_work:1},{type:'item',id:'carrier',name:'Carrier',mass_g:0,volume_ml:1,books:['manual'],reading_power:'power'}",
        "{type:'book',id:'manual',name:'Manual',study_work:1},{type:'item',id:'carrier',name:'Carrier',mass_g:0,volume_ml:1,books:['manual'],reading_power:'box',capabilities:[{id:'box',type:'container',capacity_volume_ml:1}]}"
    }) {
        CAPTURE(definition);
        const auto definitions = WithSkills(definition);
        Invalid(View(definitions));
    }
    for (const char* benefit : {
        "skill:'alpha',first_level:0,last_level:1", "skill:'alpha',first_level:1,last_level:11",
        "skill:'alpha',first_level:2,last_level:1", "skill:'missing',first_level:1,last_level:1",
        "skill:'alpha',first_level:1,last_level:1,direct_xp:1000000001",
        "skill:'alpha',first_level:1,last_level:1,theory_xp:-1",
        "skill:'alpha',first_level:1,last_level:1,multiplier:9999",
        "skill:'alpha',first_level:1,last_level:1,multiplier:40001",
        "skill:'alpha',first_level:1,last_level:1,direct_xp:1000000001",
        "skill:'alpha',first_level:1,last_level:1,theory_xp:-1",
        "skill:'alpha',first_level:1,last_level:1,multiplier:9999",
        "skill:'alpha',first_level:1,last_level:1,multiplier:40001",
        "skill:'combat',first_level:1,last_level:10,direct_xp:1",
        "skill:'combat',first_level:1,last_level:10,theory_xp:1",
        "skill:'combat',first_level:1,last_level:10,multiplier:12001",
        "skill:'alpha',first_level:1,last_level:1,extra:0"
    }) {
        CAPTURE(benefit);
        String book = "{type:'book',id:'manual',name:'Manual',study_work:1,benefits:[{";
        book += benefit;
        book += "}]}";
        const auto definitions = WithSkills(View(book));
        Invalid(View(definitions));
    }
    const auto duplicate = WithSkills(R"({type:'book',id:'manual',name:'Manual',study_work:1,benefits:[
        {skill:'alpha',first_level:1,last_level:1},{skill:'alpha',first_level:2,last_level:10}]})");
    Invalid(View(duplicate), "definitions[4].benefits[1].skill");
}

TEST_CASE("Recycler skill validation uses the same gates and overrides as crafting", "[content][skill-content]") {
    for (const char* fields : {
        "skills:[{skill:'alpha',level:0}]", "skills:[{skill:'alpha',level:11}]",
        "skills:[{skill:'missing',level:1}]", "training:[{skill:'alpha',level:1}]",
        "skills:[{skill:'alpha',level:1}],training:[{skill:'alpha',level:2}]",
        "training:[{skill:'alpha',level:0,xp_per_work:10001}]",
        "training:[{skill:'alpha',level:0},{skill:'alpha',level:0}]"
    }) {
        CAPTURE(fields);
        String recycler = "{type:'recycling_recipe',id:'unmake',target:'product',work:1,returns:[],";
        recycler += fields;
        recycler += '}';
        auto definitions = WithSkills(View(recycler));
        Invalid(View(definitions));
        recycler = "recycling:{id:'unmake',work:1,efficiency:0,";
        recycler += fields;
        recycler += '}';
        const auto recipe = Recipe(View(recycler));
        definitions = WithSkills(View(recipe));
        Invalid(View(definitions));
    }
}

TEST_CASE("Skill and book collisions identify both source locations transactionally", "[content][skill-content]") {
    const auto first = Document("{type:'skill',id:'shared',name:'Skill'}");
    const auto second = Document("{type:'book',id:'shared',name:'Book',study_work:1}");
    const ContentSource sources[] = {{"first.json5", View(first)}, {"second.json5", View(second)}};
    Diagnostics diagnostics;
    const auto loaded = LoadContent({sources, 2}, &diagnostics);
    REQUIRE_FALSE(loaded.has_value());
    REQUIRE(diagnostics.Frames().size() >= 2);
    REQUIRE(diagnostics.Frames()[0].location.has_value());
    REQUIRE(diagnostics.Frames()[1].location.has_value());
    CHECK(diagnostics.Frames()[0].location->file == "second.json5");
    CHECK(diagnostics.Frames()[1].location->file == "first.json5");
    CHECK(diagnostics.Frames()[0].location->path == "definitions[0].id");
    CHECK(diagnostics.Frames()[1].location->path == "definitions[0].id");
}
}
