#include "content/content.h"
#include "network/network.h"

#include <catch2/catch_test_macros.hpp>

namespace {
using namespace cataclysmx;

ContentResult<ContentCatalog> CityLoad(eastl::string_view definitions) {
    String document = "{format_version:1,definitions:["
        "{type:'terrain',id:'floor',name:'Floor',movement:{passable:true},blocks_sight:false,"
        "appearance:{symbol:'.',foreground:'white'}},";
    document.append(definitions.data(), definitions.size());
    document += "]}";
    const ContentSource source{"city.json5", {document.data(), document.size()}};
    return LoadContent({&source, 1});
}

String CityPattern(eastl::string_view extra, eastl::string_view palette = "'.':'floor'") {
    String result = "{type:'building_pattern',id:'house',rows:['.'],palette:{";
    result.append(palette.data(), palette.size());
    result += "},entrance:{x:0,z:0}";
    if (!extra.empty()) { result += ','; result.append(extra.data(), extra.size()); }
    result += '}';
    return result;
}

eastl::string_view CityView(const String& text) { return {text.data(), text.size()}; }

TEST_CASE("City metadata validates and defaults", "[content][city]") {
    const auto defaults = CityLoad(CityView(CityPattern("")));
    REQUIRE(defaults);
    const auto& pattern = defaults->GetBuildingPattern(defaults->FindBuildingPattern("house"));
    CHECK(pattern.era == BuildingEra::Older);
    CHECK(pattern.minSettlementSize == SettlementScale::Small);
    CHECK(pattern.occupiedFloors == 1);
    REQUIRE(pattern.spaces.size() == 1);
    CHECK(pattern.spaces[0] == PopulationSpace::Interior);
    for (const auto field : {"era:'future'", "era:3", "era:null", "min_settlement_size:'tiny'",
                             "min_settlement_size:false", "occupied_floors:0", "occupied_floors:11",
                             "occupied_floors:1.5", "occupied_floors:null", "use:'public'",
                             "era:'older',era:'newer'", "unknown_city_field:1"}) {
        INFO(field);
        CHECK_FALSE(CityLoad(CityView(CityPattern(field))));
    }
    for (const auto palette : {"'.':{terrain:'floor',space:'inside'}",
                               "'.':{terrain:'floor',space:null}",
                               "'.':{terrain:'floor',space:1}",
                               "'.':{terrain:'floor',space:'none',extra:1}"}) {
        INFO(palette);
        CHECK_FALSE(CityLoad(CityView(CityPattern("", palette))));
    }
}

TEST_CASE("City spaces expand across surface layers and basements", "[content][city]") {
    const auto content = CityLoad(R"(
      {type:'building_pattern',id:'house',family:'homes',use:'civic',era:'newer',
       min_settlement_size:'large',occupied_floors:10,rows:['.ex'],
       palette:{'.':'floor',e:{terrain:'floor',space:'exterior'},x:{terrain:'floor',space:'none'}},
       entrance:{x:0,z:0},layers:[{y:1,rows:['xe.']}],
       basements:[{id:'cellar',rows:['ex.']}]},
      {type:'building_pattern',id:'other',family:'homes',use:'civic',
       rows:['.'],palette:{'.':'floor'},entrance:{x:0,z:0}}
    )");
    REQUIRE(content);
    const auto& p = content->GetBuildingPattern(content->FindBuildingPattern("house"));
    CHECK(p.era == BuildingEra::Newer);
    CHECK(p.minSettlementSize == SettlementScale::Large);
    CHECK(p.occupiedFloors == 10);
    CHECK(p.use == BuildingUse::Civic);
    REQUIRE(p.spaces.size() == 3);
    CHECK(p.spaces[0] == PopulationSpace::Interior);
    CHECK(p.spaces[1] == PopulationSpace::Exterior);
    CHECK(p.spaces[2] == PopulationSpace::None);
    REQUIRE(p.layers.size() == 1);
    REQUIRE(p.layers[0].spaces.size() == 3);
    CHECK(p.layers[0].spaces[0] == PopulationSpace::None);
    CHECK(p.layers[0].spaces[1] == PopulationSpace::Exterior);
    CHECK(p.layers[0].spaces[2] == PopulationSpace::Interior);
    REQUIRE(p.basements.size() == 1);
    REQUIRE(p.basements[0].layer.spaces.size() == 3);
    CHECK(p.basements[0].layer.spaces[0] == PopulationSpace::Exterior);
    CHECK(p.basements[0].layer.spaces[1] == PopulationSpace::None);
    CHECK(p.basements[0].layer.spaces[2] == PopulationSpace::Interior);
}

TEST_CASE("City metadata changes content fingerprint", "[content][city]") {
    const auto baseline = CityLoad(CityView(CityPattern("")));
    REQUIRE(baseline);
    const auto fingerprint = network::ContentFingerprint(*baseline);
    for (const auto field : {"era:'newer'", "min_settlement_size:'medium'", "occupied_floors:2", "use:'civic'"}) {
        INFO(field);
        const auto changed = CityLoad(CityView(CityPattern(field)));
        REQUIRE(changed);
        CHECK(network::ContentFingerprint(*changed) != fingerprint);
    }
    const auto explicitDefault = CityLoad(CityView(CityPattern(
        "era:'older',min_settlement_size:'small',occupied_floors:1", "'.':{terrain:'floor',space:'interior'}")));
    REQUIRE(explicitDefault);
    CHECK(network::ContentFingerprint(*explicitDefault) == fingerprint);
    const auto exterior = CityLoad(CityView(CityPattern("", "'.':{terrain:'floor',space:'exterior'}")));
    REQUIRE(exterior);
    CHECK(network::ContentFingerprint(*exterior) != fingerprint);
}

TEST_CASE("Layer and basement classifications affect fingerprints independently", "[content][city]") {
    const auto base = CityLoad(R"(
      {type:'building_pattern',id:'house',rows:['.'],entrance:{x:0,z:0},
       palette:{'.':'floor',e:{terrain:'floor',space:'exterior'}},
       layers:[{y:1,rows:['.']}],basements:[{id:'cellar',rows:['.']}]})");
    const auto layer = CityLoad(R"(
      {type:'building_pattern',id:'house',rows:['.'],entrance:{x:0,z:0},
       palette:{'.':'floor',e:{terrain:'floor',space:'exterior'}},
       layers:[{y:1,rows:['e']}],basements:[{id:'cellar',rows:['.']}]})");
    const auto basement = CityLoad(R"(
      {type:'building_pattern',id:'house',rows:['.'],entrance:{x:0,z:0},
       palette:{e:{space:'exterior',terrain:'floor'},'.':'floor'},
       layers:[{y:1,rows:['.']}],basements:[{id:'cellar',rows:['e']}]})");
    const auto reordered = CityLoad(R"(
      {type:'building_pattern',id:'house',rows:['.'],entrance:{x:0,z:0},
       palette:{e:{space:'exterior',terrain:'floor'},'.':'floor'},
       layers:[{y:1,rows:['.']}],basements:[{id:'cellar',rows:['.']}]})");
    REQUIRE(base);
    REQUIRE(layer);
    REQUIRE(basement);
    REQUIRE(reordered);
    const auto fingerprint = network::ContentFingerprint(*base);
    CHECK(network::ContentFingerprint(*layer) != fingerprint);
    CHECK(network::ContentFingerprint(*basement) != fingerprint);
    CHECK(network::ContentFingerprint(*reordered) == fingerprint);
}
}
