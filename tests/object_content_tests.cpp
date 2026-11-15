#include "content/content.h"

#include <catch2/catch_test_macros.hpp>

#include <initializer_list>

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

TEST_CASE("Furniture movement costs have bounded integer content values", "[content][object-content][grab]") {
    const auto loaded = LoadObjects(R"(
        {id:'chair',name:'Chair',behavior:'none',movable:true},
        {id:'table',name:'Table',behavior:'none',movable:true,move_cost:200},
        {id:'heavy',name:'Heavy',behavior:'none',movable:true,move_cost:1000000}
    )");
    REQUIRE(loaded);
    CHECK(loaded->GetObject(loaded->FindObject("chair")).moveCost == 100);
    CHECK(loaded->GetObject(loaded->FindObject("table")).moveCost == 200);
    CHECK(loaded->GetObject(loaded->FindObject("heavy")).moveCost == 1000000);
    for (const auto cost : {"0", "-1", "1.5", "1000001", "true", "'200'"}) {
        String text = "{id:'table',name:'Table',behavior:'none',movable:true,move_cost:";
        text += cost; text += "}";
        CHECK_FALSE(LoadObjects(text));
    }
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

TEST_CASE("Capacity-bearing machines require a complete output batch to fit empty storage", "[content][object-content]") {
    constexpr ContentSource machineItems{"machine-items.json5", R"({format_version:1,definitions:[
        {type:'item',id:'input',name:'Input',mass_g:1,volume_ml:1},
        {type:'item',id:'output',name:'Output',mass_g:1,volume_ml:40}
    ]})"};
    struct Case { const char* capacity; bool valid; };
    for (const auto& test : {Case{"capacity_volume_ml:100,", false},
                             Case{"capacity_volume_ml:119,", false},
                             Case{"capacity_volume_ml:120,", true},
                             Case{"capacity_volume_ml:121,", true}, Case{"", true}}) {
        INFO(test.capacity);
        String text = "{format_version:1,objects:[{id:'press',name:'Press',";
        text += test.capacity;
        text += R"(behavior:'crafting_machine',machine:{input:'input',input_count:1,
            output:'output',output_count:3,duration_ticks:1,energy_capacity:2,energy_per_tick:1}}]})";
        const ContentSource sources[] = {{"machine.json5", {text.data(), text.size()}}, machineItems};
        Diagnostics diagnostics;
        const auto result = LoadContent({sources, 2}, &diagnostics);
        REQUIRE(result.has_value() == test.valid);
        if (test.valid) {
            CHECK(diagnostics.Empty());
            CHECK(result->GetObject({1}).machine.outputCount == 3);
        } else {
            CHECK(result.error() == ContentError::InvalidData);
            REQUIRE_FALSE(diagnostics.Empty());
            const auto& frame = diagnostics.Frames().front();
            REQUIRE(frame.location.has_value());
            CHECK(frame.location->file == "machine.json5");
            CHECK(frame.location->path == "objects[0].machine.output_count");
            CHECK(frame.message == "machine output batch exceeds object storage capacity");
            CHECK_FALSE(LoadContent({sources, 2}).has_value());
        }
    }
    // The batch volume exceeds UINT32_MAX even though each factor fits uint32.
    constexpr ContentSource hugeOutput{"huge-output.json5", R"({format_version:1,definitions:[
        {type:'item',id:'input',name:'Input',mass_g:1,volume_ml:1},
        {type:'item',id:'output',name:'Output',mass_g:1,volume_ml:2147483648}
    ]})"};
    constexpr ContentSource hugeMachine{"huge-machine.json5", R"({format_version:1,objects:[
        {id:'press',name:'Press',behavior:'crafting_machine',capacity_volume_ml:4294967295,
         machine:{input:'input',input_count:1,output:'output',output_count:2,
                  duration_ticks:1,energy_capacity:2,energy_per_tick:1}}
    ]})"};
    const ContentSource hugeSources[] = {hugeMachine, hugeOutput};
    CHECK_FALSE(LoadContent({hugeSources, 2}).has_value());
}

TEST_CASE("Object light requires both intensity and range to be zero or both positive", "[content][object-content]") {
    for (const auto light : {"{intensity:0,range:0}", "{intensity:1,range:1}"}) {
        String object = "{id:'lamp',name:'Lamp',behavior:'none',light:";
        object += light;
        object += '}';
        const auto loaded = LoadObjects({object.data(), object.size()});
        REQUIRE(loaded.has_value());
        CHECK(ValidObjectDefinition(loaded->GetObject({1})));
    }
    for (const auto light : {"{intensity:0,range:1}", "{intensity:1,range:0}"}) {
        String object = "{id:'lamp',name:'Lamp',behavior:'none',light:";
        object += light;
        object += '}';
        Diagnostics diagnostics;
        const auto result = LoadObjects({object.data(), object.size()}, &diagnostics);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error() == ContentError::InvalidData);
        REQUIRE_FALSE(diagnostics.Empty());
        REQUIRE(diagnostics.Frames().front().location.has_value());
        CHECK(diagnostics.Frames().front().location->file == "objects.json5");
        CHECK(diagnostics.Frames().front().location->path == "objects[0].light");
        CHECK_FALSE(LoadObjects({object.data(), object.size()}).has_value());
    }
    ObjectDefinition definition;
    definition.light = {0, 1};
    CHECK_FALSE(ValidObjectDefinition(definition));
    definition.light = {1, 0};
    CHECK_FALSE(ValidObjectDefinition(definition));
}

TEST_CASE("Furniture properties are optional typed and independent of behavior", "[content][object-content]") {
    const auto loaded = LoadObjects(R"(
        {id:'cabinet',name:'Cabinet',behavior:'none',capacity_volume_ml:4294967295,
         kind:'table',blocks_movement:true,blocks_sight:true,movable:true},
        {id:'legacy',name:'Legacy',behavior:'none'}
    )");
    REQUIRE(loaded.has_value());
    const auto& cabinet = loaded->GetObject(loaded->FindObject("cabinet"));
    CHECK(cabinet.capacityVolumeMl == UINT32_MAX);
    CHECK(cabinet.kind == ObjectKind::Table);
    CHECK(cabinet.blocksMovement);
    CHECK(cabinet.blocksSight);
    CHECK(cabinet.movable);
    CHECK(ValidObjectDefinition(cabinet));
    const auto& legacy = loaded->GetObject(loaded->FindObject("legacy"));
    CHECK_FALSE(legacy.capacityVolumeMl.has_value());
    CHECK(legacy.kind == ObjectKind::Generic);
    CHECK_FALSE(legacy.blocksMovement);
    CHECK_FALSE(legacy.blocksSight);
    CHECK_FALSE(legacy.movable);
    for (const auto field : {"capacity_volume_ml:0", "capacity_volume_ml:4294967296",
                            "capacity_volume_ml:null", "kind:'none'", "kind:'bed'", "kind:0",
                            "blocks_movement:1", "blocks_sight:'true'", "movable:null",
                            "capacity_volume_ml:1,capacity_volume_ml:2", "kind:'chair',kind:'table'"}) {
        String object = "{id:'bad',name:'Bad',behavior:'none',";
        object += field;
        object += '}';
        CHECK_FALSE(LoadObjects({object.data(), object.size()}).has_value());
    }
    ObjectDefinition invalid = cabinet;
    invalid.capacityVolumeMl = 0;
    CHECK_FALSE(ValidObjectDefinition(invalid));
    invalid = cabinet;
    invalid.kind = ObjectKind::None;
    CHECK_FALSE(ValidObjectDefinition(invalid));
}

}
