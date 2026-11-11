#include "content/content.h"
#include "network/network.h"

#include <catch2/catch_test_macros.hpp>

namespace {
using namespace cataclysmx;
using namespace cataclysmx::network;

constexpr ContentSource items{"items.json5", R"({format_version:1,definitions:[
    {type:'item',id:'scrap',name:'Scrap',mass_g:100,volume_ml:100},
    {type:'item',id:'pipe',name:'Pipe',mass_g:300,volume_ml:200}
]})"};

std::uint64_t Fingerprint(eastl::string_view objects) {
    String text = "{format_version:1,objects:[";
    text.append(objects.data(), objects.size());
    text += "]}";
    const ContentSource sources[] = {{"objects.json5", {text.data(), text.size()}}, items};
    Diagnostics diagnostics;
    auto content = LoadContent({sources, 2}, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str());
    REQUIRE(content);
    return ContentFingerprint(*content);
}

TEST_CASE("Object fingerprints include machine configuration", "[network][object][fingerprint]") {
    constexpr const char* baseline = R"({id:'press',name:'Press',behavior:'crafting_machine',machine:{
        input:'scrap',input_count:3,output:'pipe',output_count:1,
        duration_ticks:300,energy_capacity:3000,energy_per_tick:2}})";
    const auto original = Fingerprint(baseline);
    const char* changes[] = {
        "{id:'other',name:'Press',behavior:'crafting_machine',machine:{input:'scrap',input_count:3,output:'pipe',output_count:1,duration_ticks:300,energy_capacity:3000,energy_per_tick:2}}",
        "{id:'press',name:'Other',behavior:'crafting_machine',machine:{input:'scrap',input_count:3,output:'pipe',output_count:1,duration_ticks:300,energy_capacity:3000,energy_per_tick:2}}",
        "{id:'press',name:'Press',behavior:'none'}",
        "{id:'press',name:'Press',behavior:'crafting_machine',machine:{input:'pipe',input_count:3,output:'pipe',output_count:1,duration_ticks:300,energy_capacity:3000,energy_per_tick:2}}",
        "{id:'press',name:'Press',behavior:'crafting_machine',machine:{input:'scrap',input_count:4,output:'pipe',output_count:1,duration_ticks:300,energy_capacity:3000,energy_per_tick:2}}",
        "{id:'press',name:'Press',behavior:'crafting_machine',machine:{input:'scrap',input_count:3,output:'scrap',output_count:1,duration_ticks:300,energy_capacity:3000,energy_per_tick:2}}",
        "{id:'press',name:'Press',behavior:'crafting_machine',machine:{input:'scrap',input_count:3,output:'pipe',output_count:2,duration_ticks:300,energy_capacity:3000,energy_per_tick:2}}",
        "{id:'press',name:'Press',behavior:'crafting_machine',machine:{input:'scrap',input_count:3,output:'pipe',output_count:1,duration_ticks:301,energy_capacity:3000,energy_per_tick:2}}",
        "{id:'press',name:'Press',behavior:'crafting_machine',machine:{input:'scrap',input_count:3,output:'pipe',output_count:1,duration_ticks:300,energy_capacity:3001,energy_per_tick:2}}",
        "{id:'press',name:'Press',behavior:'crafting_machine',machine:{input:'scrap',input_count:3,output:'pipe',output_count:1,duration_ticks:300,energy_capacity:3000,energy_per_tick:3}}"
    };
    for (const auto* change : changes) {
        CAPTURE(change);
        CHECK(Fingerprint(change) != original);
    }
    CHECK(Fingerprint("") != original);
}

TEST_CASE("Object fingerprints are stable across source ordering and empty objects", "[network][object][fingerprint]") {
    const ContentSource first{"first", "{format_version:1,objects:[{id:'z',name:'Z',behavior:'none'}]}"};
    const ContentSource second{"second", "{format_version:1,objects:[{id:'a',name:'A',behavior:'none'}]}"};
    const ContentSource forward[] = {items, first, second};
    const ContentSource reverse[] = {second, first, items};
    const auto a = LoadContent({forward, 3});
    const auto b = LoadContent({reverse, 3});
    REQUIRE(a); REQUIRE(b);
    CHECK(ContentFingerprint(*a) == ContentFingerprint(*b));
    const auto noObjects = LoadContent({&items, 1});
    REQUIRE(noObjects);
    CHECK(ContentFingerprint(*noObjects) == Fingerprint(""));
    CHECK(ContentFingerprint(*a) != ContentFingerprint(*noObjects));
}
}
