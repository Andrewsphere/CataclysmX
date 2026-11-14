#include "content/content.h"
#include "core/ratio.h"

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
    const ContentSource source{"crafting.json5", View(text)};
    return LoadContent({&source, 1}, diagnostics);
}

String Item(eastl::string_view id, eastl::string_view fields = {}) {
    String text = "{type:'item',id:'";
    text.append(id.data(), id.size());
    text += "',name:'item',mass_g:0,volume_ml:1";
    if (!fields.empty()) { text += ','; text.append(fields.data(), fields.size()); }
    text += '}';
    return text;
}

String WithItems(eastl::string_view definitions) {
    String text = Item("material");
    text += ',';
    text += Item("product");
    if (!definitions.empty()) { text += ','; text.append(definitions.data(), definitions.size()); }
    return text;
}

void Invalid(eastl::string_view definitions) {
    Diagnostics diagnostics;
    const auto result = Load(definitions, &diagnostics);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error() == ContentError::InvalidData);
    REQUIRE_FALSE(diagnostics.Empty());
    const auto withoutDiagnostics = Load(definitions);
    REQUIRE_FALSE(withoutDiagnostics.has_value());
    CHECK(withoutDiagnostics.error() == ContentError::InvalidData);
}

TEST_CASE("Crafting ratio scale preserves unit boundaries and full-width arithmetic", "[content][crafting-content]") {
    CHECK(ratioScale == 10000);
    CHECK(ValidUnitRatio(0));
    CHECK(ValidUnitRatio(ratioScale));
    CHECK_FALSE(ValidUnitRatio(ratioScale + 1));
    CHECK(ScaleRatioFloor(10, 1000) == 1);
    CHECK(ScaleRatioFloor(8, 7500) == 6);
    CHECK(ScaleRatioFloor(1, 7500) == 0);
    CHECK(ScaleRatioFloor(UINT64_MAX, ratioScale) == UINT64_MAX);
    CHECK(ScaleRatioFloor(UINT64_MAX, 0) == 0);
    CHECK(ScaleRatioFloor(UINT64_MAX, 5000) == UINT64_MAX / 2);
    CHECK(ScaleRatioFloor(7, 15000) == 10);
}

TEST_CASE("Crafting resolves cross-source forward references with stable IDs", "[content][crafting-content]") {
    const auto recipes = Document(R"(
        {type:'recipe',id:'z_recipe',inputs:[{item:'material',count:11}],
         tool_traits:[{id:'cutting',tier:2}],result:{item:'product',count:1},work:4294967295,
         recycling:{id:'z_recycler',work:3,efficiency:1000}},
        {type:'recipe',id:'a_recipe',name:'Alternative',crafting_enabled:false,
         inputs:[],result:{item:'product',count:255},work:1}
    )");
    String items = Item("product");
    items += ',';
    items += Item("material", "tool_traits:[{id:'cutting',tier:255}]");
    items += ",{type:'tool_trait',id:'cutting',name:'Cutting'}";
    const auto itemDocument = Document(View(items));
    const ContentSource sources[] = {{"recipes.json5", View(recipes)}, {"items.json5", View(itemDocument)}};
    const ContentSource reversed[] = {sources[1], sources[0]};
    const auto loaded = LoadContent({sources, 2});
    const auto reordered = LoadContent({reversed, 2});
    REQUIRE(loaded.has_value());
    REQUIRE(reordered.has_value());
    CHECK(loaded->CraftingRecipeCount() == 2);
    CHECK(loaded->RecyclingRecipeCount() == 1);
    CHECK(loaded->ToolTraitCount() == 1);
    CHECK(loaded->FindCraftingRecipe("a_recipe") == CraftingRecipeId{1});
    CHECK(loaded->FindCraftingRecipe("z_recipe") == reordered->FindCraftingRecipe("z_recipe"));
    CHECK(loaded->FindRecyclingRecipe("z_recycler") == reordered->FindRecyclingRecipe("z_recycler"));
    CHECK(loaded->FindItem("product") == reordered->FindItem("product"));
    CHECK(loaded->FindToolTrait("cutting") == ToolTraitId{1});
    CHECK(loaded->GetToolTrait({1}).name == "Cutting");
    const auto& recipe = loaded->GetCraftingRecipe({2});
    CHECK(recipe.name == "z_recipe");
    CHECK(recipe.enabled);
    CHECK(recipe.work == UINT32_MAX);
    REQUIRE(recipe.inputs.Size() == 1);
    CHECK(recipe.inputs[0].Item() == loaded->FindItem("material"));
    CHECK(recipe.inputs[0].Count() == 11);
    CHECK(recipe.toolTraits[0] == TraitRequirement{{1}, 2});
    CHECK_FALSE(loaded->GetCraftingRecipe({1}).enabled);
    CHECK(loaded->GetCraftingRecipe({1}).result.count == 255);
    const auto recycler = loaded->FindRecyclingFor(loaded->FindItem("product"));
    CHECK(recycler == loaded->FindRecyclingRecipe("z_recycler"));
    CHECK(loaded->GetRecyclingRecipe(recycler).returns[0].count == 1);
    CHECK(loaded->GetItem(loaded->FindItem("material")).toolTraits[0].tier == 255);
    CHECK(loaded->FindCraftingRecipe("absent") == CraftingRecipeId{});
    CHECK(loaded->FindRecyclingRecipe("absent") == RecyclingRecipeId{});
    CHECK(loaded->FindToolTrait("absent") == ToolTraitId{});
    CHECK(loaded->TryGetCraftingRecipe({}) == nullptr);
    CHECK(loaded->TryGetCraftingRecipe({3}) == nullptr);
    CHECK(loaded->TryGetRecyclingRecipe({}) == nullptr);
    CHECK(loaded->TryGetRecyclingRecipe({2}) == nullptr);
    CHECK(loaded->TryGetToolTrait({}) == nullptr);
    CHECK(loaded->TryGetToolTrait({2}) == nullptr);
    CHECK(loaded->FindRecyclingFor({}) == RecyclingRecipeId{});
    const auto& unfinished = loaded->GetItem(loaded->UnfinishedCraftItem());
    CHECK(unfinished.id == "unfinished_craft");
    CHECK(unfinished.name == "unfinished craft");
    CHECK(unfinished.unfinishedCraft);
    CHECK(unfinished.massGrams == 0);
    CHECK(unfinished.volumeMl == 1);
    REQUIRE(ItemContainer(unfinished) != nullptr);
    CHECK(ItemContainer(unfinished)->capacityVolumeMl == UINT32_MAX);
    CHECK(unfinished.capabilities[0].type == "container");
    CHECK(unfinished.energyStores.empty());
    CHECK(unfinished.behaviors.empty());
}

TEST_CASE("Generated recycling excludes tools bans and losses with integer flooring", "[content][crafting-content]") {
    String text = Item("banned", "recovery_allowed:false");
    for (const char* id : {"lost", "tool", "tiny", "material", "product"}) { text += ','; text += Item(id); }
    text += R"(,{type:'recipe',id:'make',inputs:[
        {item:'banned',count:100},{item:'lost',count:100,recoverable:false},
        {item:'tool',count:100,retained:true},{item:'tiny',count:9},{item:'material',count:19}],
        result:{item:'product',count:1},work:1,
        recycling:{id:'unmake',work:2,efficiency:1000,inputs:[{item:'tool',count:1,retained:true}]}})";
    const auto loaded = Load(View(text));
    REQUIRE(loaded.has_value());
    const auto& recycler = loaded->GetRecyclingRecipe({1});
    REQUIRE(recycler.returns.Size() == 1);
    CHECK(recycler.returns[0] == ItemOutput{loaded->FindItem("material"), 1});
    REQUIRE(recycler.inputs.Size() == 1);
    CHECK(recycler.inputs[0].Retained());
    CHECK(recycler.toolTraits.Empty());
    for (const char* efficiency : {"0", "10000"}) {
        String allLost = WithItems("{type:'recipe',id:'make',inputs:[{item:'material',count:1,recoverable:false}],result:{item:'product',count:1},work:1,recycling:{id:'unmake',work:1,efficiency:");
        allLost += efficiency;
        allLost += "}}";
        const auto empty = Load(View(allLost));
        REQUIRE(empty.has_value());
        CHECK(empty->GetRecyclingRecipe({1}).returns.Empty());
    }
}

TEST_CASE("Explicit recyclers are independent and target conflicts name both recipes", "[content][crafting-content]") {
    const auto explicitRecipe = WithItems(R"(
        {type:'recycling_recipe',id:'explicit_recycler',target:'product',work:1,
         inputs:[{item:'material',count:2}],returns:[],tool_traits:[]}
    )");
    const auto explicitLoaded = Load(View(explicitRecipe));
    REQUIRE(explicitLoaded.has_value());
    CHECK(explicitLoaded->CraftingRecipeCount() == 0);
    CHECK(explicitLoaded->GetRecyclingRecipe({1}).returns.Empty());
    CHECK(explicitLoaded->UnfinishedCraftItem().value != 0);
    String conflict = explicitRecipe;
    conflict += R"(,{type:'recipe',id:'make',inputs:[],result:{item:'product',count:1},work:1,
                      recycling:{id:'generated_recycler',work:1,efficiency:10000}})";
    Diagnostics diagnostics;
    const auto failed = Load(View(conflict), &diagnostics);
    REQUIRE_FALSE(failed.has_value());
    REQUIRE_FALSE(diagnostics.Empty());
    CHECK(diagnostics.Frames()[0].message.find("explicit_recycler") != String::npos);
    CHECK(diagnostics.Frames()[0].message.find("generated_recycler") != String::npos);
    CHECK(diagnostics.Frames()[0].message.find("product") != String::npos);
    Invalid(View(conflict));
    conflict = explicitRecipe;
    conflict += ",{type:'recycling_recipe',id:'other_recycler',target:'product',work:1,returns:[]}";
    Invalid(View(conflict));
    conflict = WithItems(R"(
        {type:'recipe',id:'first',inputs:[],result:{item:'product',count:1},work:1,recycling:{id:'unmake_first',work:1,efficiency:1}},
        {type:'recipe',id:'second',inputs:[],result:{item:'product',count:1},work:1,recycling:{id:'unmake_second',work:1,efficiency:1}}
    )");
    Invalid(View(conflict));
}

TEST_CASE("Crafting schema rejects duplicates unknown references and numeric overflow", "[content][crafting-content]") {
    for (const char* definition : {
        "{type:'recipe',id:'make',inputs:[{item:'missing',count:1}],result:{item:'product',count:1},work:1}",
        "{type:'recipe',id:'make',inputs:[],result:{item:'missing',count:1},work:1}",
        "{type:'recipe',id:'make',inputs:[],tool_traits:[{id:'missing',tier:1}],result:{item:'product',count:1},work:1}",
        "{type:'recipe',id:'make',inputs:[{item:'material',count:1},{item:'material',count:1,retained:true}],result:{item:'product',count:1},work:1}",
        "{type:'recipe',id:'make',inputs:[{item:'material',count:256}],result:{item:'product',count:1},work:1}",
        "{type:'recipe',id:'make',inputs:[{item:'material',count:0}],result:{item:'product',count:1},work:1}",
        "{type:'recipe',id:'make',inputs:[],result:{item:'product',count:256},work:1}",
        "{type:'recipe',id:'make',inputs:[],result:{item:'product',count:4294967296},work:1}",
        "{type:'recipe',id:'make',inputs:[],result:{item:'product',count:0},work:1}",
        "{type:'recipe',id:'make',inputs:[],result:{item:'product',count:1},work:0}",
        "{type:'recipe',id:'make',inputs:[],result:{item:'product',count:1},work:4294967296}",
        "{type:'recipe',id:'make',inputs:[],result:{item:'product',count:1},work:1,recycling:{id:'unmake',work:1,efficiency:10001}}",
        "{type:'recipe',id:'make',inputs:[],result:{item:'product',count:2},work:1,recycling:{id:'unmake',work:1,efficiency:10000}}",
        "{type:'recycling_recipe',id:'unmake',target:'product',work:1,returns:[{item:'material',count:1},{item:'material',count:2}]}",
        "{type:'recycling_recipe',id:'unmake',target:'missing',work:1,returns:[]}",
        "{type:'recycling_recipe',id:'unmake',target:'product',work:1,returns:[],recipe:'make'}",
        "{type:'recipe',id:'make',inputs:[],result:{item:'product',count:1},work:1,work:2}",
        "{type:'recipe',id:'material',inputs:[],result:{item:'product',count:1},work:1}",
        "{type:'recipe',id:'make',inputs:[],result:{item:'product',count:1},work:1,recycling:{id:'material',work:1,efficiency:10000}}",
        "{type:'recipe',id:'make',inputs:[],result:{item:'product',count:1},work:1,recycling:{id:'make',work:1,efficiency:10000}}",
        "{type:'tool_trait',id:'product',name:'collision'}",
        "{type:'item',id:'unfinished_craft',name:'user',mass_g:0,volume_ml:1},{type:'recipe',id:'make',inputs:[],result:{item:'product',count:1},work:1}"
    }) {
        CAPTURE(definition);
        const auto text = WithItems(definition);
        Invalid(View(text));
    }
    String banned = Item("material", "recovery_allowed:false");
    banned += ',';
    banned += Item("product");
    banned += ",{type:'recycling_recipe',id:'unmake',target:'product',work:1,returns:[{item:'material',count:1}]}";
    Invalid(View(banned));
}

TEST_CASE("Generated and explicit recycler IDs sort together with independent requirements", "[content][crafting-content]") {
    const auto first = Document(R"(
        {type:'recipe',id:'make',inputs:[{item:'material',count:255}],
         tool_traits:[{id:'cutting',tier:3}],result:{item:'product',count:1},work:1,
         recycling:{id:'a_generated',work:4294967295,efficiency:10000,
                    tool_traits:[{id:'cutting',tier:1}]}},
        {type:'recycling_recipe',id:'z_explicit',target:'material',work:1,
         returns:[{item:'product',count:255}]}
    )");
    String definitions = WithItems("{type:'tool_trait',id:'cutting',name:'Cutting'}");
    const auto second = Document(View(definitions));
    const ContentSource sources[] = {{"first.json5", View(first)}, {"second.json5", View(second)}};
    const ContentSource reversed[] = {sources[1], sources[0]};
    const auto loaded = LoadContent({sources, 2});
    const auto reordered = LoadContent({reversed, 2});
    REQUIRE(loaded.has_value());
    REQUIRE(reordered.has_value());
    CHECK(loaded->FindRecyclingRecipe("a_generated") == RecyclingRecipeId{1});
    CHECK(loaded->FindRecyclingRecipe("z_explicit") == RecyclingRecipeId{2});
    CHECK(loaded->FindRecyclingRecipe("a_generated") == reordered->FindRecyclingRecipe("a_generated"));
    CHECK(loaded->FindRecyclingRecipe("z_explicit") == reordered->FindRecyclingRecipe("z_explicit"));
    const auto& generated = loaded->GetRecyclingRecipe({1});
    REQUIRE(generated.toolTraits.Size() == 1);
    CHECK(generated.toolTraits[0].tier == 1);
    CHECK(generated.work == UINT32_MAX);
    REQUIRE(generated.returns.Size() == 1);
    CHECK(generated.returns[0].count == 255);
    CHECK(loaded->GetRecyclingRecipe({2}).returns[0].count == 255);
    CHECK(loaded->FindRecyclingFor(loaded->FindItem("material")) == RecyclingRecipeId{2});
    const auto duplicate = Document("{type:'tool_trait',id:'a_generated',name:'Duplicate'}");
    const ContentSource conflicting[] = {sources[0], sources[1], {"duplicate.json5", View(duplicate)}};
    Diagnostics diagnostics;
    const auto failure = LoadContent({conflicting, 3}, &diagnostics);
    REQUIRE_FALSE(failure.has_value());
    REQUIRE(diagnostics.Frames().size() >= 2);
    CHECK(diagnostics.Frames()[0].message.find("a_generated") != String::npos);
    REQUIRE(diagnostics.Frames()[0].location.has_value());
    REQUIRE(diagnostics.Frames()[1].location.has_value());
    CHECK(diagnostics.Frames()[0].location->file == "duplicate.json5");
    CHECK(diagnostics.Frames()[1].location->file == "first.json5");
}

TEST_CASE("Crafting bounded lists accept eight items four traits and canonicalize order", "[content][crafting-content]") {
    String definitions;
    for (const char* id : {"a", "b", "c", "d", "e", "f", "g", "h", "i"}) {
        if (!definitions.empty()) { definitions += ','; }
        definitions += Item(id, eastl::string_view(id) == "i"
            ? "tool_traits:[{id:'td',tier:255},{id:'tc',tier:1},{id:'tb',tier:1},{id:'ta',tier:1}]"
            : "");
    }
    for (const char* id : {"ta", "tb", "tc", "td", "te"}) {
        definitions += ",{type:'tool_trait',id:'";
        definitions += id;
        definitions += "',name:'trait'}";
    }
    const String inputs = "{item:'h',count:255,retained:true},{item:'g',count:1},{item:'f',count:1},{item:'e',count:1},{item:'d',count:1},{item:'c',count:1},{item:'b',count:1},{item:'a',count:1}";
    const String traits = "{id:'td',tier:255},{id:'tc',tier:1},{id:'tb',tier:1},{id:'ta',tier:1}";
    auto recipe = [&](const String& inputList, const String& traitList) {
        String text = definitions;
        text += ",{type:'recipe',id:'make',work:1,result:{item:'i',count:1},inputs:[";
        text += inputList;
        text += "],tool_traits:[";
        text += traitList;
        text += "]}";
        return text;
    };
    const auto valid = recipe(inputs, traits);
    const auto loaded = Load(View(valid));
    REQUIRE(loaded.has_value());
    const auto& result = loaded->GetCraftingRecipe({1});
    REQUIRE(result.inputs.Size() == 8);
    REQUIRE(result.toolTraits.Size() == 4);
    CHECK(result.inputs[0].Item() == loaded->FindItem("a"));
    CHECK(result.inputs[7].Retained());
    CHECK(result.inputs[7].Count() == 255);
    CHECK(result.toolTraits[0].trait == loaded->FindToolTrait("ta"));
    const auto& itemTraits = loaded->GetItem(loaded->FindItem("i")).toolTraits;
    REQUIRE(itemTraits.Size() == 4);
    CHECK(itemTraits[0].trait == loaded->FindToolTrait("ta"));
    CHECK(itemTraits[3].tier == 255);
    for (const auto& invalid : {recipe(inputs + ",{item:'i',count:1}", traits),
                               recipe(inputs, traits + ",{id:'te',tier:1}"),
                               recipe(inputs, "{id:'ta',tier:1},{id:'ta',tier:2}"),
                               recipe(inputs, "{id:'ta',tier:256}"),
                               recipe(inputs, "{id:'ta',tier:0}")}) { Invalid(View(invalid)); }
    String recycler = definitions;
    recycler += ",{type:'recycling_recipe',id:'unmake',target:'i',work:1,returns:[";
    recycler += inputs;
    // Return entries have no retained flag.
    const auto retained = recycler.find(",retained:true");
    REQUIRE(retained != String::npos);
    recycler.erase(retained, eastl::string_view(",retained:true").size());
    const String eightReturns = recycler + "],inputs:[" + inputs + "],tool_traits:[" + traits + "]}";
    const auto returns = Load(View(eightReturns));
    REQUIRE(returns.has_value());
    CHECK(returns->GetRecyclingRecipe({1}).returns.Size() == 8);
    CHECK(returns->GetRecyclingRecipe({1}).returns[0].item == returns->FindItem("a"));
    REQUIRE(returns->GetRecyclingRecipe({1}).inputs.Size() == 8);
    CHECK(returns->GetRecyclingRecipe({1}).inputs[7].Retained());
    REQUIRE(returns->GetRecyclingRecipe({1}).toolTraits.Size() == 4);
    CHECK(returns->GetRecyclingRecipe({1}).toolTraits[3].tier == 255);
    recycler += ",{item:'i',count:1}]}";
    Invalid(View(recycler));
}

TEST_CASE("Crafting power resolves only explicitly named own energy capabilities", "[content][crafting-content]") {
    const auto valid = Item("tool", R"(crafting_power:{store:'power',energy_per_work:10000},
        capabilities:[{id:'z_box',type:'container',capacity_volume_ml:2},
                      {id:'power',type:'energy_store',capacity_energy:100,rechargeable:false}])");
    const auto loaded = Load(View(valid));
    REQUIRE(loaded.has_value());
    const auto& item = loaded->GetItem({1});
    CHECK(item.craftingPower == FindItemCapability(item, "power"));
    CHECK(item.craftingEnergyPerWork == ratioScale);
    REQUIRE(ItemEnergyStore(item, item.craftingPower) != nullptr);
    CHECK(item.recoveryAllowed);
    auto changed = item;
    changed.craftingEnergyPerWork = 1;
    CHECK_FALSE(item == changed);
    changed = item;
    changed.craftingPower = {};
    CHECK_FALSE(item == changed);
    changed = item;
    changed.recoveryAllowed = false;
    CHECK_FALSE(item == changed);
    changed = item;
    changed.unfinishedCraft = true;
    CHECK_FALSE(item == changed);
    changed = item;
    REQUIRE(changed.toolTraits.PushBack({{1}, 1}));
    CHECK_FALSE(item == changed);
    for (const char* fields : {
        "crafting_power:{store:'missing',energy_per_work:10000}",
        "crafting_power:{store:'box',energy_per_work:1},capabilities:[{id:'box',type:'container',capacity_volume_ml:1}]",
        "crafting_power:{energy_per_work:1}", "crafting_power:{store:'power',energy_per_work:0}",
        "crafting_power:{store:'power',energy_per_work:4294967296}",
        "crafting_power:{store:'power'}", "recovery_allowed:1", "unfinishedCraft:true", "unfinished_craft:true",
        "tool_traits:[{id:'missing',tier:1}]"
    }) { const auto invalid = Item("tool", fields); Invalid(View(invalid)); }
    const auto noBilling = Item("tool", "capabilities:[{id:'power',type:'energy_store',capacity_energy:1,rechargeable:false}]");
    const auto unpowered = Load(View(noBilling));
    REQUIRE(unpowered.has_value());
    CHECK(unpowered->GetItem({1}).craftingPower == ItemCapabilityId{});
    CHECK(unpowered->GetItem({1}).craftingEnergyPerWork == 0);
}

TEST_CASE("Crafting empty catalogs and conditional reserved IDs preserve existing content", "[content][crafting-content]") {
    const auto empty = Load("");
    REQUIRE(empty.has_value());
    CHECK(empty->ItemCount() == 0);
    CHECK(empty->CraftingRecipeCount() == 0);
    CHECK(empty->RecyclingRecipeCount() == 0);
    CHECK(empty->ToolTraitCount() == 0);
    CHECK(empty->UnfinishedCraftItem() == ItemTypeId{});
    const auto traitOnly = Load("{type:'tool_trait',id:'cutting',name:'Cutting'}");
    REQUIRE(traitOnly.has_value());
    CHECK(traitOnly->UnfinishedCraftItem() == ItemTypeId{});
    const auto authored = Item("unfinished_craft");
    const auto ordinary = Load(View(authored));
    REQUIRE(ordinary.has_value());
    CHECK_FALSE(ordinary->GetItem({1}).unfinishedCraft);
    CHECK(ordinary->UnfinishedCraftItem() == ItemTypeId{});
}

TEST_CASE("Authored recipes cannot reference internal unfinished craft wrappers", "[content][crafting-content]") {
    struct InvalidReference {
        const char* definition;
        const char* path;
    };
    const InvalidReference cases[] = {
        {"{type:'recipe',id:'make',inputs:[{item:'unfinished_craft',count:1}],result:{item:'product',count:1},work:1}",
         "definitions[2].inputs[0].item"},
        {"{type:'recipe',id:'make',inputs:[{item:'unfinished_craft',count:1,retained:true}],result:{item:'product',count:1},work:1}",
         "definitions[2].inputs[0].item"},
        {"{type:'recipe',id:'make',inputs:[],result:{item:'unfinished_craft',count:1},work:1}",
         "definitions[2].result.item"},
        {"{type:'recipe',id:'make',inputs:[],result:{item:'unfinished_craft',count:1},work:1,recycling:{id:'unmake',work:1,efficiency:10000}}",
         "definitions[2].result.item"},
        {"{type:'recipe',id:'make',inputs:[{item:'unfinished_craft',count:1,recoverable:false}],result:{item:'product',count:1},work:1,recycling:{id:'unmake',work:1,efficiency:0}}",
         "definitions[2].inputs[0].item"},
        {"{type:'recycling_recipe',id:'unmake',target:'unfinished_craft',returns:[],work:1}",
         "definitions[2].target"},
        {"{type:'recycling_recipe',id:'unmake',target:'product',inputs:[{item:'unfinished_craft',count:1}],returns:[],work:1}",
         "definitions[2].inputs[0].item"},
        {"{type:'recycling_recipe',id:'unmake',target:'product',inputs:[{item:'unfinished_craft',count:1,retained:true}],returns:[],work:1}",
         "definitions[2].inputs[0].item"},
        {"{type:'recycling_recipe',id:'unmake',target:'product',returns:[{item:'unfinished_craft',count:1}],work:1}",
         "definitions[2].returns[0].item"},
        {"{type:'recipe',id:'make',inputs:[],result:{item:'product',count:1},work:1,recycling:{id:'unmake',work:1,efficiency:0,inputs:[{item:'unfinished_craft',count:1}]}}",
         "definitions[2].recycling.inputs[0].item"},
        {"{type:'recipe',id:'make',inputs:[],result:{item:'product',count:1},work:1,recycling:{id:'unmake',work:1,efficiency:10000,inputs:[{item:'unfinished_craft',count:1,retained:true}]}}",
         "definitions[2].recycling.inputs[0].item"},
    };
    for (const auto& reference : cases) {
        CAPTURE(reference.definition);
        const auto text = WithItems(reference.definition);
        Diagnostics diagnostics;
        const auto result = Load(View(text), &diagnostics);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error() == ContentError::InvalidData);
        REQUIRE_FALSE(diagnostics.Empty());
        const auto& frame = diagnostics.Frames().front();
        CHECK(frame.message.find("internal item 'unfinished_craft'") != String::npos);
        REQUIRE(frame.location.has_value());
        CHECK(frame.location->file == "crafting.json5");
        CHECK(frame.location->path == reference.path);
        const auto withoutDiagnostics = Load(View(text));
        REQUIRE_FALSE(withoutDiagnostics.has_value());
        CHECK(withoutDiagnostics.error() == ContentError::InvalidData);
    }
}

TEST_CASE("Tool trait IDs cover the complete nonzero byte range", "[content][crafting-content]") {
    String definitions;
    for (unsigned i = 0; i < 255; ++i) {
        char entry[128];
        std::snprintf(entry, sizeof(entry), "%s{type:'tool_trait',id:'trait_%03u',name:'Trait'}", i ? "," : "", i);
        definitions += entry;
    }
    const auto loaded = Load(View(definitions));
    REQUIRE(loaded.has_value());
    CHECK(loaded->ToolTraitCount() == 255);
    CHECK(loaded->FindToolTrait("trait_254") == ToolTraitId{255});
    CHECK(loaded->GetToolTrait({255}).id == "trait_254");
    CHECK(loaded->FindToolTrait("trait_999") == ToolTraitId{});
    definitions += ",{type:'tool_trait',id:'trait_255',name:'Overflow'}";
    Invalid(View(definitions));
}
}
