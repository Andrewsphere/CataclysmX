#include "core/world.h"
#include "objects/storage_access.h"
#include <catch2/catch_test_macros.hpp>

namespace {
using namespace cataclysmx;
template<class T> T Take(WorldResult<T> value) { REQUIRE(value); return eastl::move(*value); }
ContentCatalog StorageContent() {
    const ContentSource source{"house-crafting", R"({format_version:1,objects:[
      {id:'cabinet',name:'Cabinet',behavior:'none',capacity_volume_ml:100,blocks_movement:true,movable:true},
      {id:'internal',name:'Internal buffer',behavior:'none'}
    ],definitions:[
      {type:'terrain',id:'floor',name:'Floor',movement:{passable:true},blocks_sight:false,appearance:{symbol:'.',foreground:'white'}},
      {type:'terrain',id:'wall',name:'Wall',movement:{passable:false},blocks_sight:true,appearance:{symbol:'#',foreground:'white'}},
      {type:'terrain',id:'air',name:'Air',movement:{passable:true},open_space:true,blocks_sight:false,looks_like:'floor'},
      {type:'item',id:'material',name:'Material',mass_g:1,volume_ml:40},
      {type:'item',id:'product',name:'Product',mass_g:1,volume_ml:10},
      {type:'item',id:'tool',name:'Tool',mass_g:1,volume_ml:10},
      {type:'item',id:'bag',name:'Bag',mass_g:1,volume_ml:10,capabilities:[{type:'container',id:'storage',capacity_volume_ml:1000}]},
      {type:'recipe',id:'make',inputs:[{item:'material',count:1},{item:'tool',count:1,retained:true}],result:{item:'product',count:1},work:250}
    ]})"};
    Diagnostics diagnostics;
    auto result = LoadContent({&source, 1}, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str()); REQUIRE(result); return eastl::move(*result);
}
struct Rig {
    ContentCatalog content = StorageContent();
    World world{content};
    ActorHandle actor{};
    ObjectHandle cabinet{};
    ItemHandle material{}, tool{};
    Rig(TileCoord storage = {4, 2}, const char* type = "cabinet") {
        Take(world.RequestChunks({{0, 0}, 0}, LoadMode::active)); REQUIRE(world.ReconcileChunks());
        actor = Take(world.CreateActor({2, 2}, true));
        cabinet = Take(world.CreateObject(content.FindObject(type), storage));
        const auto bag = Take(world.CreateItem(content.FindItem("bag"), ItemLocation::Stored(cabinet)));
        material = Take(world.CreateItem(content.FindItem("material"), ItemLocation::Inside(bag)));
        tool = Take(world.CreateItem(content.FindItem("tool"), ItemLocation::Stored(cabinet)));
        REQUIRE(world.SetCraftingSettings({3, 100}));
    }
    CraftingResult Start() { return Take(world.StartCrafting(actor, content.FindCraftingRecipe("make"))); }
};

TEST_CASE("Crafting consumes nested cabinet ingredients through blocked storage endpoints", "[house][crafting]") {
    Rig r;
    CHECK_FALSE(r.world.CanAccessItem(r.actor, r.material)); // Ordinary access stays immediate.
    const auto started = r.Start(); REQUIRE(started.outcome == CraftingOutcome::Started);
    CHECK(r.world.GetItem(r.material)->location == ItemLocation::Inside(started.unfinished));
    CHECK(r.world.GetItem(r.tool)->location == ItemLocation::Stored(r.cabinet));
    REQUIRE(r.world.MoveObject(r.cabinet, {10, 2}));
    r.world.ElapseTicks(100); REQUIRE(r.world.ProcessCraftingEvents());
    REQUIRE(r.world.InspectCrafting(started.unfinished));
    CHECK(r.world.InspectCrafting(started.unfinished)->status == CraftingStatus::MissingTools);
    CHECK(r.world.IsReservedItem(r.material));
    REQUIRE(r.world.MoveObject(r.cabinet, {3, 2}));
    CHECK(Take(r.world.ResumeCrafting(r.actor, started.unfinished)).outcome == CraftingOutcome::Resumed);
    REQUIRE(r.world.CheckInvariants());
}

TEST_CASE("Crafting storage reach cannot cross walls furniture or diagonal corners", "[house][crafting]") {
    SECTION("Wall under storage is never ignored") {
        Rig r;
        REQUIRE(r.world.SetTile({4, 2}, {r.content.FindTerrain("wall"), 0}));
        CHECK(r.Start().outcome == CraftingOutcome::MissingTools);
    }
    SECTION("Diagonal access cannot cut two blocked sides") {
        Rig r({3, 3});
        for (int z = 0; z <= 5; ++z) { for (int x = 0; x <= 5; ++x) {
            if (TileCoord{x, z} != TileCoord{2, 2} && TileCoord{x, z} != TileCoord{3, 3}) {
                REQUIRE(r.world.SetTile({x, z}, {r.content.FindTerrain("wall"), 0}));
            }
        } }
        CHECK(r.Start().outcome == CraftingOutcome::MissingTools);
    }
    SECTION("A cabinet is an endpoint and cannot provide passage to ground items") {
        Rig r({3, 2});
        REQUIRE(r.world.TransferItem(r.material, ItemLocation::Ground({4, 2})));
        for (int z = 0; z <= 5; ++z) {
            if (z != 2) { REQUIRE(r.world.SetTile({3, z}, {r.content.FindTerrain("wall"), 0})); }
        }
        CHECK(r.Start().outcome == CraftingOutcome::MissingInputs);
    }
    SECTION("Legacy internal contents are not exposed as storage") {
        Rig r({3, 2}, "internal");
        CHECK(r.Start().outcome == CraftingOutcome::MissingTools);
    }
    SECTION("Ground items at a blocked cabinet endpoint are not storage") {
        Rig r({3, 2});
        REQUIRE(r.world.TransferItem(r.material, ItemLocation::Ground({3, 2})));
        CHECK(r.Start().outcome == CraftingOutcome::MissingInputs);
    }
}

TEST_CASE("Unfinished crafts use dynamic external volume in furniture capacity", "[house][crafting][storage]") {
    Rig r({3, 2});
    const auto started = r.Start(); REQUIRE(started.outcome == CraftingOutcome::Started);
    CHECK(r.world.GetItemVolume(started.unfinished) == 40);
    REQUIRE(r.world.TransferItem(started.unfinished, ItemLocation::Stored(r.cabinet)));
    CHECK(r.world.GetObjectUsedVolume(r.cabinet) == 60); // Bag 10, tool 10, reserved material 40.
    Take(r.world.CreateItem(r.content.FindItem("material"), ItemLocation::Stored(r.cabinet)));
    CHECK(r.world.GetObjectUsedVolume(r.cabinet) == 100);
    const auto rejected = r.world.CreateItem(r.content.FindItem("material"), ItemLocation::Stored(r.cabinet));
    REQUIRE_FALSE(rejected); CHECK(rejected.error() == WorldError::capacity_exceeded);
    const auto bag = r.world.GetObject(r.cabinet)->contents.front();
    REQUIRE(r.world.TransferItem(started.unfinished, ItemLocation::Inside(bag)));
    CHECK(r.world.GetObjectUsedVolume(r.cabinet) == 60);
    REQUIRE(r.world.TransferItem(started.unfinished, ItemLocation::Stored(r.cabinet)));
    CHECK(r.world.GetObjectUsedVolume(r.cabinet) == 100);
    CHECK(r.world.CanAccessItem(r.actor, started.unfinished));
    CHECK_FALSE(r.world.CanAccessItem(r.actor, r.material));
    REQUIRE(r.world.CheckInvariants());
}
}
