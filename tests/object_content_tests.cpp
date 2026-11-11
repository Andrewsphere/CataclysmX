#include "content/content.h"

#include <catch2/catch_test_macros.hpp>

namespace {
using namespace cataclysmx;

constexpr ContentSource items{"items.json5", R"({format_version:1,definitions:[
    {type:'item',id:'scrap',name:'scrap',mass_g:100,volume_ml:100},
    {type:'item',id:'pipe',name:'pipe',mass_g:300,volume_ml:200}
]})"};

ContentResult<ContentCatalog> LoadObjects(eastl::string_view objects, Diagnostics* diagnostics = nullptr) {
    String text = "{format_version:1,objects:[";
    text.append(objects.data(), objects.size());
    text += "]}";
    const ContentSource sources[] = {{"objects.json5", {text.data(), text.size()}}, items};
    return LoadContent({sources, 2}, diagnostics);
}

String Machine(eastl::string_view fields) {
    String text = "{id:'press',name:'Press',behavior:'crafting_machine',machine:{";
    text.append(fields.data(), fields.size());
    text += "}}";
    return text;
}

TEST_CASE("Objects resolve later item sources into typed definitions", "[content][object-content]") {
    const auto loaded = LoadObjects(R"(
        {id:'press',name:'Press',behavior:'crafting_machine',machine:{
            input:'scrap',input_count:3,output:'pipe',output_count:1,
            duration_ticks:300,energy_capacity:3000,energy_per_tick:2}},
        {id:'decoration',name:'Decoration',behavior:'none'}
    )");
    REQUIRE(loaded.has_value());
    CHECK(loaded->ObjectCount() == 2);
    CHECK(loaded->FindObject("decoration") == ObjectTypeId{1});
    CHECK(loaded->FindObject("press") == ObjectTypeId{2});
    CHECK(loaded->FindObject("missing") == ObjectTypeId{});
    CHECK(loaded->TryGetObject({}) == nullptr);
    CHECK(loaded->TryGetObject({3}) == nullptr);
    const auto& definition = loaded->GetObject(loaded->FindObject("press"));
    CHECK(definition.behavior == ObjectBehaviorKind::CraftingMachine);
    CHECK(definition.machine.input == loaded->FindItem("scrap"));
    CHECK(definition.machine.output == loaded->FindItem("pipe"));
    CHECK(definition.machine.duration == 300);
    CHECK(definition.machine.energyCapacity == 3000);
    CHECK(definition.machine.energyPerTick == 2);
    const auto old = LoadContent({&items, 1});
    REQUIRE(old.has_value());
    CHECK(old->ObjectCount() == 0);
}

TEST_CASE("Object IDs are lexical across sources and globally unique", "[content][object-content]") {
    const ContentSource sources[] = {
        {"z", "{format_version:1,objects:[{id:'z',name:'Z',behavior:'none'}]}"},
        {"a", "{format_version:1,objects:[{id:'a',name:'A',behavior:'none'}]}"}
    };
    const ContentSource reversed[] = {sources[1], sources[0]};
    const auto first = LoadContent({sources, 2});
    const auto second = LoadContent({reversed, 2});
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    CHECK(first->FindObject("a") == ObjectTypeId{1});
    CHECK(first->FindObject("z") == second->FindObject("z"));
    CHECK_FALSE(LoadObjects("{id:'scrap',name:'Collision',behavior:'none'}").has_value());
    CHECK_FALSE(LoadObjects(R"({id:'x',name:'X',behavior:'none'},{id:'x',name:'Y',behavior:'none'})").has_value());
}

TEST_CASE("Machine content rejects bad references bounds and schema fields", "[content][object-content]") {
    const eastl::string_view invalidConfigs[] = {
        "input:'absent',input_count:1,output:'pipe',output_count:1,duration_ticks:1,energy_capacity:2,energy_per_tick:1",
        "input:'scrap',input_count:1,output:'absent',output_count:1,duration_ticks:1,energy_capacity:2,energy_per_tick:1",
        "input:'scrap',input_count:0,output:'pipe',output_count:1,duration_ticks:1,energy_capacity:2,energy_per_tick:1",
        "input:'scrap',input_count:1025,output:'pipe',output_count:1,duration_ticks:1,energy_capacity:2,energy_per_tick:1",
        "input:'scrap',input_count:1,output:'pipe',output_count:0,duration_ticks:1,energy_capacity:2,energy_per_tick:1",
        "input:'scrap',input_count:1,output:'pipe',output_count:1025,duration_ticks:1,energy_capacity:2,energy_per_tick:1",
        "input:'scrap',input_count:1,output:'pipe',output_count:1,duration_ticks:0,energy_capacity:2,energy_per_tick:1",
        "input:'scrap',input_count:1,output:'pipe',output_count:1,duration_ticks:4294967296,energy_capacity:2,energy_per_tick:1",
        "input:'scrap',input_count:1,output:'pipe',output_count:1,duration_ticks:1,energy_capacity:2,energy_per_tick:0",
        "input:'scrap',input_count:1,output:'pipe',output_count:1,duration_ticks:1,energy_capacity:1,energy_per_tick:2",
        "input:'scrap',input_count:1,output:'pipe',output_count:1,duration_ticks:1,energy_capacity:9223372036854775808,energy_per_tick:1",
        "input:'scrap',input_count:1,output:'pipe',output_count:1,duration_ticks:1.5,energy_capacity:2,energy_per_tick:1",
        "input:'scrap',input_count:1,output:'pipe',output_count:1,duration_ticks:1,energy_capacity:2,energy_per_tick:1,extra:0",
        "input:'scrap',input_count:1,input_count:2,output:'pipe',output_count:1,duration_ticks:1,energy_capacity:2,energy_per_tick:1"
    };
    for (const auto config : invalidConfigs) {
        const auto object = Machine(config);
        Diagnostics diagnostics;
        const auto result = LoadObjects({object.data(), object.size()}, &diagnostics);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error() == ContentError::InvalidData);
        REQUIRE_FALSE(diagnostics.Empty());
        REQUIRE(diagnostics.Frames().front().location.has_value());
        CHECK(diagnostics.Frames().front().location->file == "objects.json5");
        CHECK_FALSE(LoadObjects({object.data(), object.size()}).has_value());
    }
    CHECK_FALSE(LoadObjects("{id:'x',name:'X',behavior:'unknown'}").has_value());
    CHECK_FALSE(LoadObjects("{id:'x',name:'X',behavior:'none',machine:{}}").has_value());
    CHECK_FALSE(LoadObjects("{id:'x',name:'X',behavior:'none',extra:1}").has_value());
}

TEST_CASE("Machine content accepts maximum bounded integers", "[content][object-content]") {
    const auto loaded = LoadObjects(R"({id:'press',name:'Press',behavior:'crafting_machine',machine:{
        input:'scrap',input_count:1024,output:'pipe',output_count:1024,duration_ticks:4294967295,
        energy_capacity:9223372036854775807,energy_per_tick:4294967295}})");
    REQUIRE(loaded.has_value());
    CHECK(ValidObjectDefinition(loaded->GetObject({1})));
}

}
