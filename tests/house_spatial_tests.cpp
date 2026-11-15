#include "actors/ai_vision.h"
#include "core/lighting.h"
#include "core/perception.h"
#include "core/world.h"

#include <catch2/catch_test_macros.hpp>

namespace {
using namespace cataclysmx;

ContentCatalog SpatialContent() {
    constexpr ContentSource source{"house-spatial", R"({format_version:1,definitions:[
      {type:'terrain',id:'floor',name:'Floor',movement:{passable:true},blocks_sight:false,appearance:{symbol:'.',foreground:'white'}},
      {type:'terrain',id:'wall',name:'Wall',movement:{passable:false},blocks_sight:true,appearance:{symbol:'#',foreground:'white'}},
      {type:'terrain',id:'air',name:'Air',movement:{passable:true},open_space:true,blocks_sight:false,appearance:{symbol:' ',foreground:'white'}}
    ],objects:[
      {id:'chair',name:'Chair',behavior:'none',kind:'chair',blocks_movement:true,movable:true},
      {id:'cabinet',name:'Cabinet',behavior:'none',capacity_volume_ml:1000,blocks_movement:true,blocks_sight:true,movable:true}
    ]})"};
    Diagnostics diagnostics;
    auto content = LoadContent({&source, 1}, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str());
    REQUIRE(content);
    return eastl::move(*content);
}

void Prepare(World& world) {
    REQUIRE(world.RequestChunks({{0, 0}, 0}, LoadMode::active));
    REQUIRE(world.ReconcileChunks());
    world.SetSkyLight(0);
    for (int y = -1; y <= 1; ++y) {
        for (int z = 0; z < chunk_size; ++z) {
            for (int x = 0; x < chunk_size; ++x) {
                REQUIRE(world.SetTile({x, z, y}, {world.GetContent().FindTerrain("floor"), 0}));
            }
        }
    }
}

TEST_CASE("Chairs block actor movement without becoming opaque", "[house][spatial]") {
    const auto content = SpatialContent();
    World world(content);
    Prepare(world);
    const auto actor = world.CreateActor({4, 8, 0}, true);
    REQUIRE(actor);
    const auto chair = world.CreateObject(content.FindObject("chair"), {5, 8, 0});
    REQUIRE(chair);
    CHECK(world.BlocksMovement({5, 8, 0}));
    CHECK_FALSE(world.BlocksSight({5, 8, 0}));
    CHECK_FALSE(world.TryActorStep(*actor, {1, 0, 0}));
    CHECK(world.GetActor(*actor)->position == TileCoord{4, 8, 0});
    CHECK(LineVisible(world, {4, 8, 0}, {6, 8, 0}));
    CHECK(AiLineVisible(world, {4, 8, 0}, {6, 8, 0}));
    REQUIRE(world.DestroyObject(*chair));
    CHECK(world.TryActorStep(*actor, {1, 0, 0}));
}

TEST_CASE("Cabinet movement refreshes player AI and lighting occlusion", "[house][spatial][lighting]") {
    const auto content = SpatialContent();
    World world(content);
    Prepare(world);
    for (int z = 0; z < chunk_size; ++z) {
        if (z != 8) { REQUIRE(world.SetTile({7, z, 0}, {content.FindTerrain("wall"), 0})); }
    }
    const TileCoord observer{4, 8, 0};
    const TileCoord target{10, 8, 0};
    const auto lamp = world.CreateObject(observer, true);
    REQUIRE(lamp);
    REQUIRE(world.SetObjectLight(*lamp, {255, 12}));
    CHECK(LineVisible(world, observer, target));
    CHECK(world.GetLightLevel(target) > 63);
    const auto cabinet = world.CreateObject(content.FindObject("cabinet"), {7, 8, 0});
    REQUIRE(cabinet);
    OpticalField optics;
    optics.Rebuild(world);
    VisibilityMask mask;
    PerceptionStats stats;
    BuildVisibilityMask(world, observer, 12, mask, stats, &optics);
    CHECK_FALSE(mask.Contains(target));
    CHECK_FALSE(LineVisible(world, observer, target));
    CHECK_FALSE(AiLineVisible(world, observer, target));
    CHECK(world.GetLightLevel(target) == 0);
    REQUIRE(world.SetObjectEnabled(*cabinet, false));
    CHECK(world.BlocksMovement({7, 8, 0}));
    CHECK(world.BlocksSight({7, 8, 0}));
    CHECK(world.GetLightLevel(target) == 0);
    REQUIRE(world.MoveObject(*cabinet, {6, 6, 0}));
    const ChunkCoord changed[] = {{0, 0, 0}};
    optics.Refresh(world, {changed, 1});
    BuildVisibilityMask(world, observer, 12, mask, stats, &optics);
    CHECK(mask.Contains(target));
    CHECK(LineVisible(world, observer, target));
    CHECK(AiLineVisible(world, observer, target));
    CHECK(world.GetLightLevel(target) > 63);
}
}
