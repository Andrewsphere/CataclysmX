#include "content/content.h"
#include "network/network.h"

#include <catch2/catch_test_macros.hpp>
#include <initializer_list>

namespace {
using namespace cataclysmx;

constexpr eastl::string_view forgeObject = R"(
    {id:'forge',name:'Forge',behavior:'forge',tool_traits:[{id:'forge_heat',tier:1}],
     forge:{fuel_capacity:24000,ignition_cost:1000,fuels:[{item:'charcoal',heat:6000}]}})";

ContentResult<ContentCatalog> LoadForge(eastl::string_view definitions = {},
    eastl::string_view objects = forgeObject, Diagnostics* diagnostics = nullptr) {
    String text = R"({format_version:1,definitions:[
        {type:'tool_trait',id:'forge_heat',name:'Forge heat'},
        {type:'tool_trait',id:'hammering',name:'Hammering'},
        {type:'item',id:'charcoal',name:'Charcoal',mass_g:1,volume_ml:1},
        {type:'item',id:'coke',name:'Coke',mass_g:1,volume_ml:1},
        {type:'item',id:'product',name:'Product',mass_g:1,volume_ml:1})";
    if (!definitions.empty()) { text += ','; text.append(definitions.data(), definitions.size()); }
    text += "],objects:[";
    text.append(objects.data() ? objects.data() : "", objects.size());
    text += "]}";
    const ContentSource source{"forge-content.json5", {text.data(), text.size()}};
    return LoadContent({&source, 1}, diagnostics);
}

String ForgeWithConfig(eastl::string_view config) {
    String text = "{id:'forge',name:'Forge',behavior:'forge',tool_traits:[{id:'forge_heat',tier:1}],forge:";
    text.append(config.data(), config.size());
    text += '}';
    return text;
}

String CraftWithFields(eastl::string_view fields, eastl::string_view recycling = {}) {
    String text = "{type:'recipe',id:'make',inputs:[{item:'charcoal',count:1}],result:{item:'product',count:1},work:123";
    if (!fields.empty()) { text += ','; text.append(fields.data(), fields.size()); }
    if (!recycling.empty()) {
        text += ",recycling:{id:'unmake',work:7,efficiency:10000";
        if (recycling != "default") { text += ','; text.append(recycling.data(), recycling.size()); }
        text += '}';
    }
    text += '}';
    return text;
}

TEST_CASE("Forge content resolves fuels and accepts full unsigned accounting bounds", "[content][forge-content]") {
    const auto object = ForgeWithConfig(R"({fuel_capacity:9223372036854775807,ignition_cost:9223372036854775806,
        fuels:[{item:'charcoal',heat:9223372036854775807},{item:'coke',heat:1}]})");
    const auto recipe = CraftWithFields("tool_traits:[{id:'forge_heat',tier:255}],heat_per_work:4294967295");
    const auto loaded = LoadForge({recipe.data(), recipe.size()}, {object.data(), object.size()});
    REQUIRE(loaded.has_value());
    const auto& forge = loaded->GetObject(loaded->FindObject("forge"));
    CHECK(forge.behavior == ObjectBehaviorKind::Forge);
    CHECK(ValidObjectDefinition(forge));
    CHECK(forge.forge.fuelCapacity == UINT64_MAX / 2);
    CHECK(forge.forge.ignitionCost == UINT64_MAX / 2 - 1);
    REQUIRE(forge.forge.fuels.size() == 2);
    CHECK(forge.forge.fuels[0].item == loaded->FindItem("charcoal"));
    CHECK(forge.forge.fuels[0].heat == UINT64_MAX / 2);
    CHECK(forge.forge.fuels[1].item == loaded->FindItem("coke"));
    CHECK(loaded->GetCraftingRecipe(loaded->FindCraftingRecipe("make")).heatPerWork == UINT32_MAX);
}

TEST_CASE("Forge content rejects malformed budgets fuels and unresolved references", "[content][forge-content]") {
    // The ordinary recipe injects unfinished_craft, so that case checks a
    // resolved internal item rather than merely an unknown reference.
    const auto recipe = CraftWithFields({});
    for (const auto config : {
        "{}",
        "{fuel_capacity:0,ignition_cost:1,fuels:[{item:'charcoal',heat:1}]}",
        "{fuel_capacity:9223372036854775808,ignition_cost:1,fuels:[{item:'charcoal',heat:1}]}",
        "{fuel_capacity:10,ignition_cost:0,fuels:[{item:'charcoal',heat:1}]}",
        "{fuel_capacity:10,ignition_cost:10,fuels:[{item:'charcoal',heat:1}]}",
        "{fuel_capacity:10,ignition_cost:11,fuels:[{item:'charcoal',heat:1}]}",
        "{fuel_capacity:10,ignition_cost:1,fuels:[]}",
        "{fuel_capacity:10,ignition_cost:1,fuels:[{item:'charcoal',heat:0}]}",
        "{fuel_capacity:10,ignition_cost:1,fuels:[{item:'charcoal',heat:11}]}",
        "{fuel_capacity:10,ignition_cost:1,fuels:[{item:'charcoal',heat:-1}]}",
        "{fuel_capacity:10,ignition_cost:1,fuels:[{item:'charcoal',heat:1.5}]}",
        "{fuel_capacity:10,ignition_cost:1,fuels:[{item:'missing',heat:1}]}",
        "{fuel_capacity:10,ignition_cost:1,fuels:[{item:'unfinished_craft',heat:1}]}",
        "{fuel_capacity:10,ignition_cost:1,fuels:[{item:'charcoal',heat:1},{item:'charcoal',heat:2}]}",
        "{fuel_capacity:10,ignition_cost:1,fuels:[{item:'charcoal',heat:1,extra:true}]}",
        "{fuel_capacity:10,ignition_cost:1,fuels:[{item:'charcoal',heat:1}],extra:true}"}) {
        INFO(config);
        const auto object = ForgeWithConfig(config);
        Diagnostics diagnostics;
        CHECK_FALSE(LoadForge({recipe.data(), recipe.size()}, {object.data(), object.size()}, &diagnostics).has_value());
        CHECK_FALSE(diagnostics.Empty());
    }
}

TEST_CASE("Forge heat providers and behavior configs cannot be mismatched", "[content][forge-content]") {
    for (const auto object : {
        "{id:'forge',name:'Forge',behavior:'forge'}",
        "{id:'forge',name:'Forge',behavior:'forge',forge:{fuel_capacity:10,ignition_cost:1,fuels:[{item:'charcoal',heat:1}]}}",
        "{id:'forge',name:'Forge',behavior:'none',tool_traits:[{id:'forge_heat',tier:1}]}",
        "{id:'forge',name:'Forge',behavior:'none',forge:{}}",
        "{id:'forge',name:'Forge',behavior:'grid_battery',forge:{}}",
        "{id:'forge',name:'Forge',behavior:'forge',tool_traits:[{id:'forge_heat',tier:1}],generator:{}}",
        "{id:'forge',name:'Forge',behavior:'forge',tool_traits:[{id:'forge_heat',tier:1}],machine:{}}"}) {
        INFO(object);
        CHECK_FALSE(LoadForge({}, object).has_value());
    }
    CHECK_FALSE(LoadForge(R"({type:'item',id:'tool',name:'Tool',mass_g:1,volume_ml:1,
        tool_traits:[{id:'forge_heat',tier:1}]})").has_value());

    const auto object = ForgeWithConfig("{fuel_capacity:10,ignition_cost:1,fuels:[{item:'equipment',heat:1}]}");
    String objects = object;
    objects += ",{id:'station',name:'Station',behavior:'none',packing_item:'equipment'}";
    CHECK_FALSE(LoadForge(R"({type:'item',id:'equipment',name:'Equipment',mass_g:1,volume_ml:1,
        placed_object:'station'})", {objects.data(), objects.size()}).has_value());

    String portable(forgeObject.data(), forgeObject.size());
    portable.insert(portable.find("forge:{"), "packing_item:'equipment',");
    const auto valid = LoadForge(R"({type:'item',id:'equipment',name:'Equipment',mass_g:1,volume_ml:1,
        placed_object:'forge'})", {portable.data(), portable.size()});
    REQUIRE(valid.has_value());
    CHECK(valid->GetObject(valid->FindObject("forge")).packingItem == valid->FindItem("equipment"));
}

TEST_CASE("Crafting and both recycler forms require heat rates exactly with forge heat", "[content][forge-content]") {
    for (const auto fields : {
        "tool_traits:[{id:'forge_heat',tier:1}]",
        "heat_per_work:1",
        "tool_traits:[{id:'hammering',tier:1}],heat_per_work:1",
        "tool_traits:[{id:'forge_heat',tier:1}],heat_per_work:0",
        "tool_traits:[{id:'forge_heat',tier:1}],heat_per_work:-1",
        "tool_traits:[{id:'forge_heat',tier:1}],heat_per_work:1.5",
        "tool_traits:[{id:'forge_heat',tier:1}],heat_per_work:true",
        "tool_traits:[{id:'forge_heat',tier:1}],heat_per_work:4294967296"}) {
        INFO(fields);
        const auto craft = CraftWithFields(fields);
        CHECK_FALSE(LoadForge({craft.data(), craft.size()}).has_value());
        const auto generated = CraftWithFields("tool_traits:[{id:'forge_heat',tier:1}],heat_per_work:10000", fields);
        CHECK_FALSE(LoadForge({generated.data(), generated.size()}).has_value());
        String explicitRecycler = "{type:'recycling_recipe',id:'unmake',target:'product',work:7,returns:[],";
        explicitRecycler += fields;
        explicitRecycler += '}';
        CHECK_FALSE(LoadForge({explicitRecycler.data(), explicitRecycler.size()}).has_value());
    }
    CHECK(LoadForge(R"({type:'recycling_recipe',id:'unmake',target:'product',work:7,returns:[],
        tool_traits:[{id:'forge_heat',tier:1}],heat_per_work:1})").has_value());
}

TEST_CASE("Generated recycler heat traits rates and work are independent of crafting", "[content][forge-content]") {
    for (const auto fields : {"default", "tool_traits:[]", "tool_traits:[{id:'hammering',tier:2}]"}) {
        const auto recipe = CraftWithFields("tool_traits:[{id:'forge_heat',tier:1}],heat_per_work:10000", fields);
        const auto loaded = LoadForge({recipe.data(), recipe.size()});
        REQUIRE(loaded.has_value());
        const auto& recycler = loaded->GetRecyclingRecipe(loaded->FindRecyclingRecipe("unmake"));
        CHECK(recycler.work == 7);
        CHECK(recycler.heatPerWork == 0);
        for (const auto& trait : recycler.toolTraits) { CHECK(trait.trait != loaded->FindToolTrait("forge_heat")); }
        const auto canonical = WriteCombatContent(*loaded);
        const auto* written = canonical.Find("recycling_recipes");
        REQUIRE(written != nullptr);
        CHECK(written->At(0).GetUnsigned("work") == 7);
        CHECK(written->At(0).Find("heat_per_work") == nullptr);
        const auto* traits = written->At(0).Find("tool_traits");
        REQUIRE(traits != nullptr);
        CHECK(traits->Size() == recycler.toolTraits.Size());
        for (std::size_t i = 0; i < traits->Size(); ++i) {
            CHECK(traits->At(i).GetString("id") != eastl::string_view("forge_heat"));
        }
    }
    for (const auto craftFields : {"", "tool_traits:[{id:'forge_heat',tier:1}],heat_per_work:10000"}) {
        const auto recipe = CraftWithFields(craftFields, "tool_traits:[{id:'forge_heat',tier:2}],heat_per_work:123");
        const auto loaded = LoadForge({recipe.data(), recipe.size()});
        REQUIRE(loaded.has_value());
        const auto& recycler = loaded->GetRecyclingRecipe(loaded->FindRecyclingRecipe("unmake"));
        CHECK(recycler.heatPerWork == 123);
        REQUIRE(recycler.toolTraits.Size() == 1);
        CHECK(recycler.toolTraits[0].tier == 2);
    }
}

TEST_CASE("Forge budgets fuel identities yields and recipe heat affect canonical digest and fingerprint", "[content][forge-content][fingerprint]") {
    const auto recipe = CraftWithFields("tool_traits:[{id:'forge_heat',tier:1}],heat_per_work:10000",
        "tool_traits:[{id:'forge_heat',tier:1}],heat_per_work:123");
    const auto baseline = LoadForge({recipe.data(), recipe.size()});
    REQUIRE(baseline.has_value());
    const auto digest = WriteCombatContent(*baseline);
    const auto fingerprint = network::ContentFingerprint(*baseline);
    for (const auto config : {
        "{fuel_capacity:24001,ignition_cost:1000,fuels:[{item:'charcoal',heat:6000}]}",
        "{fuel_capacity:24000,ignition_cost:1001,fuels:[{item:'charcoal',heat:6000}]}",
        "{fuel_capacity:24000,ignition_cost:1000,fuels:[{item:'charcoal',heat:6001}]}",
        "{fuel_capacity:24000,ignition_cost:1000,fuels:[{item:'coke',heat:6000}]}",
        "{fuel_capacity:24000,ignition_cost:1000,fuels:[{item:'charcoal',heat:6000},{item:'coke',heat:6000}]}"}) {
        const auto object = ForgeWithConfig(config);
        const auto changed = LoadForge({recipe.data(), recipe.size()}, {object.data(), object.size()});
        REQUIRE(changed.has_value());
        CHECK(network::ContentFingerprint(*changed) != fingerprint);
        CHECK_FALSE(WriteCombatContent(*changed) == digest);
    }
    for (const bool changeRecycler : {false, true}) {
        const auto changedRecipe = CraftWithFields(
            changeRecycler ? "tool_traits:[{id:'forge_heat',tier:1}],heat_per_work:10000"
                           : "tool_traits:[{id:'forge_heat',tier:1}],heat_per_work:10001",
            changeRecycler ? "tool_traits:[{id:'forge_heat',tier:1}],heat_per_work:124"
                           : "tool_traits:[{id:'forge_heat',tier:1}],heat_per_work:123");
        const auto changed = LoadForge({changedRecipe.data(), changedRecipe.size()});
        REQUIRE(changed.has_value());
        CHECK(network::ContentFingerprint(*changed) != fingerprint);
        CHECK_FALSE(WriteCombatContent(*changed) == digest);
    }
    const auto reordered = LoadForge({recipe.data(), recipe.size()}, R"(
        {name:'Forge',id:'forge',tool_traits:[{tier:1,id:'forge_heat'}],behavior:'forge',
         forge:{fuels:[{heat:6000,item:'charcoal'}],ignition_cost:1000,fuel_capacity:24000}})");
    REQUIRE(reordered.has_value());
    CHECK(network::ContentFingerprint(*reordered) == fingerprint);
    CHECK(WriteCombatContent(*reordered) == digest);

    const auto* objects = digest.Find("objects");
    REQUIRE(objects != nullptr);
    const auto* forge = objects->At(0).Find("forge");
    REQUIRE(forge != nullptr);
    CHECK(forge->GetUnsigned("fuel_capacity") == 24000);
    CHECK(forge->GetUnsigned("ignition_cost") == 1000);
    const auto* fuels = forge->Find("fuels");
    REQUIRE(fuels != nullptr);
    CHECK(fuels->At(0).GetString("item") == eastl::string_view("charcoal"));
    CHECK(fuels->At(0).GetUnsigned("heat") == 6000);
    const auto* crafts = digest.Find("recipes");
    const auto* recyclers = digest.Find("recycling_recipes");
    REQUIRE(crafts != nullptr);
    REQUIRE(recyclers != nullptr);
    CHECK(crafts->At(0).GetUnsigned("heat_per_work") == 10000);
    CHECK(recyclers->At(0).GetUnsigned("heat_per_work") == 123);
}
}
