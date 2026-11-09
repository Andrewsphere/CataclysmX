#include "core/server.h"
#include "content_fixture.h"

#include <catch2/catch_test_macros.hpp>
#include <EASTL/utility.h>

namespace {
using namespace cataclysmx;

ContentCatalog CustomContent() {
    const ContentSource source{"integration.json5", R"({format_version:1, definitions:[
        {type:'terrain',id:'air',name:'Air',movement:{passable:true},blocks_sight:false,open_space:true,
         appearance:{symbol:' ',foreground:'white'}},
        {type:'terrain', id:'floor', name:'Floor', movement:{passable:true}, blocks_sight:false,
         appearance:{symbol:'.',foreground:'white'}},
        {type:'terrain', id:'wall', name:'Wall', movement:{passable:false}, blocks_sight:true,
         appearance:{symbol:'#',foreground:'white'}},
        {type:'terrain', id:'closed_door', name:'Gate', movement:{passable:false}, blocks_sight:true,
         appearance:{symbol:'+',foreground:'brown'}, opens_to:'gate_open'},
        {type:'terrain', id:'gate_open', name:'Open gate', movement:{passable:true}, blocks_sight:false,
         appearance:{symbol:'/',foreground:'brown'}, closes_to:'closed_door'},
        {type:'terrain', id:'glass', name:'Glass', movement:{passable:false}, blocks_sight:false,
         appearance:{symbol:'#',foreground:'cyan'}},
        {type:'terrain', id:'fog', name:'Fog', movement:{passable:true}, blocks_sight:true,
         appearance:{symbol:'%',foreground:'light_gray'}},
        {type:'item',id:'rock',name:'rock',mass_g:500,volume_ml:250},
        {type:'item',id:'backpack',name:'backpack',mass_g:1000,volume_ml:2000,
         capabilities:[{type:'container',id:'storage',capacity_volume_ml:20000}]},
        {type:'item',id:'bandage',name:'bandage',mass_g:20,volume_ml:50}
    ]})"};
    auto result = LoadContent({&source, 1});
    REQUIRE(result.has_value());
    return eastl::move(*result);
}

bool Visible(const WorldView& view, TileCoord position) {
    return view.cells[static_cast<std::size_t>((position.z - view.origin.z) * view.width
        + position.x - view.origin.x)].visible;
}

TEST_CASE("Catalog properties independently drive movement and visibility", "[content][server]") {
    const auto content = CustomContent();
    REQUIRE(ValidateServerContent(content));
    Server server(content);
    World& world = server.GetWorld();
    REQUIRE(world.SetTile({1, 0}, {content.FindTerrain("glass"), 0}));
    CHECK(Visible(server.View(), {2, 0}));
    const auto blocked = server.Send({CommandKind::move, 1, 0});
    CHECK(blocked.outcome == Outcome::blocked);
    CHECK_FALSE(blocked.advanced);
    REQUIRE(world.SetTile({1, 0}, {content.FindTerrain("fog"), 0}));
    CHECK_FALSE(Visible(server.View(), {2, 0}));
    CHECK(server.Send({CommandKind::move, 1, 0}).outcome == Outcome::moved);
    CHECK(world.CheckInvariants());
}

TEST_CASE("Terrain transitions resolve to arbitrary catalog IDs and preserve tile state", "[content][server]") {
    const auto content = CustomContent();
    CHECK(content.FindTerrain("open_door") == TerrainId{});
    Server server(content);
    World& world = server.GetWorld();
    REQUIRE(world.SetTile({1, 0}, {content.FindTerrain("closed_door"), 23}));
    CHECK(server.Send({CommandKind::interact, 1, 0}).outcome == Outcome::opened);
    CHECK(*world.GetTile({1, 0}) == Tile{content.FindTerrain("gate_open"), 23});
    CHECK(server.Send({CommandKind::interact, 1, 0}).outcome == Outcome::closed);
    CHECK(*world.GetTile({1, 0}) == Tile{content.FindTerrain("closed_door"), 23});
    CHECK(world.CheckInvariants());
}

TEST_CASE("Invalid terrain IDs are rejected without changing a world tile", "[content][world]") {
    World world(TestContent());
    REQUIRE(world.RequestChunks({{0, 0}, 0}, LoadMode::active));
    REQUIRE(world.ReconcileChunks());
    const Tile original = *world.GetTile({0, 0});
    CHECK(world.SetTile({0, 0}, {}) == eastl::unexpected(WorldError::invalid_request));
    CHECK(world.SetTile({0, 0}, {TerrainId{UINT32_MAX}, 0})
        == eastl::unexpected(WorldError::invalid_request));
    CHECK(*world.GetTile({0, 0}) == original);
    CHECK(world.CheckInvariants());
}

TEST_CASE("Application content requirements produce recoverable diagnostics", "[content][server]") {
    ContentCatalog empty;
    Diagnostics diagnostics;
    CHECK(ValidateServerContent(empty, &diagnostics) == eastl::unexpected(ContentError::InvalidData));
    CHECK(FormatDiagnostics(diagnostics).find("floor") != String::npos);
    const ContentSource source{"blocked.json5", R"({format_version:1,definitions:[
        {type:'terrain',id:'floor',name:'Floor',movement:{passable:false},blocks_sight:false,
         appearance:{symbol:'.',foreground:'white'}},
        {type:'terrain',id:'wall',name:'Wall',movement:{passable:false},blocks_sight:true,looks_like:'floor'},
        {type:'terrain',id:'closed_door',name:'Door',movement:{passable:false},blocks_sight:true,looks_like:'floor'}
    ]})"};
    auto content = LoadContent({&source, 1});
    REQUIRE(content);
    diagnostics.Clear();
    CHECK(ValidateServerContent(*content, &diagnostics) == eastl::unexpected(ContentError::InvalidData));
    CHECK(FormatDiagnostics(diagnostics).find("passable") != String::npos);
}
}
