#include "core/world.h"
#include "worldgen/worldgen.h"
#include "objects/storage_access.h"
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <string>

namespace {
using namespace cataclysmx;
template<class T, class E> T Take(eastl::expected<T, E> value) { REQUIRE(value); return eastl::move(*value); }

ContentCatalog HouseContent() {
    const ContentSource source{"house-world", R"({format_version:1,ai_profiles:[{id:'vision'}],objects:[
      {id:'cabinet',name:'Cabinet',behavior:'none',capacity_volume_ml:100,kind:'table',
        blocks_movement:true,blocks_sight:true,movable:true,light:{intensity:180,range:4}},
      {id:'fixed',name:'Fixed',behavior:'none',blocks_movement:true},
      {id:'legacy',name:'Legacy',behavior:'none'},
      {id:'press',name:'Press',behavior:'crafting_machine',capacity_volume_ml:100,machine:{
        input:'material',input_count:1,output:'material',output_count:2,
        duration_ticks:1,energy_capacity:100,energy_per_tick:1}}
    ],definitions:[
      {type:'terrain',id:'floor',name:'Floor',movement:{passable:true},blocks_sight:false,appearance:{symbol:'.',foreground:'white'}},
      {type:'terrain',id:'wall',name:'Wall',movement:{passable:false},blocks_sight:true,appearance:{symbol:'#',foreground:'white'}},
      {type:'terrain',id:'air',name:'Air',movement:{passable:true},open_space:true,blocks_sight:false,looks_like:'floor'},
      {type:'item',id:'material',name:'Material',mass_g:1,volume_ml:40},
      {type:'item',id:'bag',name:'Bag',mass_g:1,volume_ml:10,capabilities:[{type:'container',id:'storage',capacity_volume_ml:1000}]},
      {type:'monster',id:'walker',name:'Walker',ai_profile:'vision',speed:125,attack_damage:17,accuracy:8700},
      {type:'monster_group',id:'undead',entries:[{monster:'walker'}]},
      {type:'loot_pool',id:'loot',entries:[{item:'material'}]},
      {type:'building_pattern',id:'house',rows:['....','....','....','....'],palette:{'.':'floor'},entrance:{x:1,z:0},
        furniture:[{x:1,z:1,y:0,object:'cabinet',facing:'east'}],
        loot:[{x:1,z:1,y:0,pool:'loot',destination:'furniture'}],
        monsters:[{x:2,z:2,y:0,group:'undead'}]}
    ]})"};
    Diagnostics diagnostics;
    auto loaded = LoadContent({&source, 1}, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str()); REQUIRE(loaded);
    return eastl::move(*loaded);
}

struct Rig {
    ContentCatalog content = HouseContent();
    World world{content};
    LoadRequest request = Take(world.RequestChunks({{0, 0}, 1}, LoadMode::active));
    ActorHandle actor{};
    Rig() { REQUIRE(world.ReconcileChunks()); actor = Take(world.CreateActor({14, 2}, true)); }
    ObjectHandle Cabinet(TileCoord position = {15, 2}) {
        return Take(world.CreateObject(content.FindObject("cabinet"), position));
    }
    ItemHandle Material(ItemLocation location) { return Take(world.CreateItem(content.FindItem("material"), location)); }
};

TEST_CASE("Furniture storage enforces external volume and preserves nested ownership", "[house][world][storage]") {
    Rig r;
    const auto cabinet = r.Cabinet();
    const auto bag = Take(r.world.CreateItem(r.content.FindItem("bag"), ItemLocation::Stored(cabinet)));
    r.Material(ItemLocation::Inside(bag)); r.Material(ItemLocation::Inside(bag));
    CHECK(r.world.GetObjectUsedVolume(cabinet) == 10);
    const auto a = r.Material(ItemLocation::Stored(cabinet));
    const auto b = r.Material(ItemLocation::Stored(cabinet));
    CHECK(r.world.GetObjectUsedVolume(cabinet) == 90);
    const auto rejected = r.world.CreateItem(r.content.FindItem("material"), ItemLocation::Stored(cabinet));
    REQUIRE_FALSE(rejected); CHECK(rejected.error() == WorldError::capacity_exceeded);
    REQUIRE(r.world.TransferItem(a, ItemLocation::Inside(bag)));
    CHECK(r.world.GetObjectUsedVolume(cabinet) == 50);
    REQUIRE(r.world.TransferItem(a, ItemLocation::Stored(cabinet)));
    CHECK(r.world.GetObjectUsedVolume(cabinet) == 90);
    const auto outside = r.Material(ItemLocation::Carried(r.actor));
    CHECK_FALSE(r.world.TransferItem(outside, ItemLocation::Stored(cabinet)));
    CHECK(r.world.GetItem(outside)->location == ItemLocation::Carried(r.actor));
    CHECK(r.world.CanAccessItem(r.actor, b));
    CHECK(r.world.CanAccessItem(r.actor, r.world.GetItem(bag)->contents.front()));
    CHECK(r.world.BlocksMovement({15, 2})); CHECK(r.world.BlocksSight({15, 2}));
    CHECK_FALSE(r.world.CreateActor({15, 2}));
    REQUIRE(r.world.SetObjectEnabled(cabinet, false));
    CHECK(r.world.BlocksMovement({15, 2}));
    Vector<ObjectActionInfo> actions;
    r.world.QueryObjectActions(r.actor, cabinet, actions);
    REQUIRE(actions.size() == 1); CHECK(actions[0].action == ObjectAction::Collect);
    CHECK(r.world.UseObject(r.actor, cabinet, ObjectAction::Collect).outcome == ObjectUseOutcome::Succeeded);
    CHECK(r.world.GetObjectUsedVolume(cabinet) == 0);
    REQUIRE(r.world.CheckInvariants());
}

TEST_CASE("Furniture push and pull relocate both occupants atomically across chunks", "[house][world][movement]") {
    Rig r;
    const auto cabinet = r.Cabinet();
    const auto item = r.Material(ItemLocation::Stored(cabinet));
    const auto id = r.world.GetItem(item)->id;
    const auto blocker = Take(r.world.CreateActor({16, 2}));
    CHECK(r.world.QueryObjectMove(r.actor, cabinet, ObjectMoveKind::Push).outcome == ObjectMoveOutcome::Occupied);
    CHECK(Take(r.world.TryMoveObject(r.actor, cabinet, ObjectMoveKind::Push)).outcome == ObjectMoveOutcome::Occupied);
    CHECK(r.world.GetActor(r.actor)->position == TileCoord{14, 2});
    CHECK(r.world.GetObject(cabinet)->position == TileCoord{15, 2});
    REQUIRE(r.world.DestroyActor(blocker));
    const auto pushed = Take(r.world.TryMoveObject(r.actor, cabinet, ObjectMoveKind::Push));
    CHECK(pushed.outcome == ObjectMoveOutcome::Moved); CHECK(pushed.cost == ticks_per_turn);
    CHECK(r.world.ActorAt({15, 2}) == r.actor); CHECK(r.world.ObjectAt({16, 2}) == cabinet);
    CHECK(r.world.GetItem(item)->location == ItemLocation::Stored(cabinet));
    CHECK(r.world.GetItem(item)->id == id);
    CHECK(r.world.GetActorRemainingTicks(r.actor) == 0); // Server owns the single charge.
    REQUIRE(r.world.CheckInvariants());
    CHECK(Take(r.world.TryMoveObject(r.actor, cabinet, ObjectMoveKind::Pull)).outcome == ObjectMoveOutcome::Moved);
    CHECK(r.world.ActorAt({14, 2}) == r.actor); CHECK(r.world.ObjectAt({15, 2}) == cabinet);
    REQUIRE(r.world.ScheduleActorAction(r.actor));
    CHECK(Take(r.world.TryMoveObject(r.actor, cabinet, ObjectMoveKind::Push)).outcome == ObjectMoveOutcome::NotReady);
    r.world.ElapseTicks(r.world.GetActorRemainingTicks(r.actor));
    REQUIRE(r.world.SetTile({16, 2}, {r.content.FindTerrain("wall"), 0}));
    CHECK(Take(r.world.TryMoveObject(r.actor, cabinet, ObjectMoveKind::Push)).outcome == ObjectMoveOutcome::Blocked);
    CHECK_FALSE(r.world.MoveObject(cabinet, {14, 2}));
    REQUIRE(r.world.MoveObject(cabinet, {18, 2}));
    CHECK(r.world.QueryObjectMove(r.actor, cabinet, ObjectMoveKind::Push).outcome == ObjectMoveOutcome::OutOfReach);
    REQUIRE(r.world.CheckInvariants());
}

TEST_CASE("Storage reach ignores only the endpoint furniture and respects walls and corners", "[house][world][storage]") {
    Rig r;
    const auto cabinet = r.Cabinet({15, 3});
    CHECK(r.world.CanAccessObjectStorage(r.actor, cabinet));
    REQUIRE(r.world.SetTile({15, 2}, {r.content.FindTerrain("wall"), 0}));
    // The direct diagonal is blocked, but the orthogonal route through (14,3)
    // remains inside the radius-one flood area and does not cut a corner.
    CHECK(r.world.CanAccessObjectStorage(r.actor, cabinet));
    REQUIRE(r.world.SetTile({14, 3}, {r.content.FindTerrain("wall"), 0}));
    CHECK_FALSE(r.world.CanAccessObjectStorage(r.actor, cabinet));
    CHECK_FALSE(CanReachStorage(r.world, {14, 2}, cabinet, 0));
    CHECK_FALSE(CanReachStorage(r.world, {14, 2, 1}, cabinet, 8));
    REQUIRE(r.world.SetTile({15, 3}, {r.content.FindTerrain("wall"), 0}));
    CHECK_FALSE(CanReachStorage(r.world, {14, 2}, cabinet, 8));
}

TEST_CASE("Furniture blocks falling actors and administrative relocation retains suspended timers", "[house][world][movement]") {
    Rig r;
    const auto cabinet = r.Cabinet();
    REQUIRE(r.world.SetTile({15, 2, 1}, {r.content.FindTerrain("air"), 0}));
    const auto falling = Take(r.world.CreateActor({15, 2, 1}));
    CHECK(r.world.GetActor(falling)->position == TileCoord{15, 2, 1});
    REQUIRE(r.world.MoveObject(cabinet, {17, 2}));
    CHECK(r.world.GetActor(falling)->position == TileCoord{15, 2, 0});
    const auto inactive = Take(r.world.RequestChunks({{2, 0}, 0}, LoadMode::resident));
    REQUIRE(r.world.ReconcileChunks());
    REQUIRE(r.world.SetTile({32, 2}, {r.content.FindTerrain("floor"), 0}));
    const auto legacy = Take(r.world.CreateObject({18, 2}, true));
    r.world.ElapseTicks(25);
    CHECK(r.world.GetObjectRemainingTicks(legacy) == 75);
    REQUIRE(r.world.MoveObject(legacy, {32, 2}));
    r.world.ElapseTicks(50);
    CHECK(r.world.GetObjectRemainingTicks(legacy) == 75);
    REQUIRE(r.world.MoveObject(legacy, {18, 2}));
    r.world.ElapseTicks(25);
    CHECK(r.world.GetObjectRemainingTicks(legacy) == 50);
    REQUIRE(r.world.ReleaseRequest(inactive));
    REQUIRE(r.world.CheckInvariants());
}

TEST_CASE("Object movement query is side effect free and has typed mutation errors", "[house][world][movement]") {
    Rig r;
    const auto fixed = Take(r.world.CreateObject(r.content.FindObject("fixed"), {15, 2}));
    const auto revision = r.world.GetRevision();
    CHECK(r.world.QueryObjectMove(r.actor, fixed, ObjectMoveKind::Push).outcome == ObjectMoveOutcome::Immovable);
    CHECK(r.world.GetRevision() == revision);
    const auto invalid = r.world.TryMoveObject(r.actor, fixed, static_cast<ObjectMoveKind>(255));
    REQUIRE_FALSE(invalid); CHECK(invalid.error() == WorldError::invalid_request);
    const auto stale = r.world.TryMoveObject({}, fixed, ObjectMoveKind::Push);
    REQUIRE_FALSE(stale); CHECK(stale.error() == WorldError::invalid_handle);
    REQUIRE(r.world.MoveObject(fixed, {18, 2})); // Administrative move bypasses movable.
    REQUIRE(r.world.CheckInvariants());
}

TEST_CASE("Incoming request order does not affect generated persistent ID allocation", "[house][world][generation]") {
    const auto content = HouseContent();
    const auto plan = GenerateRegionPlan(content, 42, {0, 0});
    REQUIRE_FALSE(plan.buildings.empty());
    const auto first = ChunkOf(plan.buildings[0].bounds.min);
    const ChunkCoord second{first.x + 1, first.z, first.y};
    World a(content), b(content);
    Take(a.RequestChunks({first, 1}, LoadMode::active));
    Take(a.RequestChunks({second, 1}, LoadMode::active));
    Take(b.RequestChunks({second, 1}, LoadMode::active));
    Take(b.RequestChunks({first, 1}, LoadMode::active));
    REQUIRE(a.ReconcileChunks()); REQUIRE(b.ReconcileChunks());
    std::size_t count = 0;
    for (ChunkCoord chunk : a.LoadedChunks()) {
        for (auto handle : a.GetChunk(chunk)->objects) {
            const auto* object = a.GetObject(handle);
            const auto other = b.ObjectAt(object->position); REQUIRE(other.value);
            CHECK(b.GetObject(other)->id == object->id);
            REQUIRE(object->contents.size() == b.GetObject(other)->contents.size());
            for (std::size_t i = 0; i < object->contents.size(); ++i) {
                CHECK(a.GetItem(object->contents[i])->id == b.GetItem(b.GetObject(other)->contents[i])->id);
            }
            ++count;
        }
        for (auto handle : a.GetChunk(chunk)->actors) {
            const auto* actor = a.GetActor(handle);
            const auto other = b.ActorAt(actor->position); REQUIRE(other.value);
            CHECK(b.GetActor(other)->id == actor->id);
        }
    }
    CHECK(count > 0);
    REQUIRE(a.CheckInvariants()); REQUIRE(b.CheckInvariants());
}

TEST_CASE("Full machine output waits without partial creation and retries after collection", "[house][world][storage]") {
    Rig r;
    const auto press = Take(r.world.CreateObject(r.content.FindObject("press"), {15, 2}));
    r.Material(ItemLocation::Stored(press));
    r.Material(ItemLocation::Carried(r.actor));
    REQUIRE(r.world.UseObject(r.actor, press, ObjectAction::Start).outcome == ObjectUseOutcome::Succeeded);
    r.world.ElapseTicks(1);
    CHECK_FALSE(r.world.ProcessObjectEvents());
    CHECK(r.world.GetObjectUsedVolume(press) == 40);
    CHECK_FALSE(r.world.NextObjectEventIn());
    REQUIRE(r.world.UseObject(r.actor, press, ObjectAction::Collect).outcome == ObjectUseOutcome::Succeeded);
    REQUIRE(r.world.ProcessObjectEvents());
    CHECK(r.world.GetObjectUsedVolume(press) == 80);
    REQUIRE(r.world.CheckInvariants());
}

struct SaveFile {
    std::filesystem::path directory;
    std::string path;
    SaveFile() {
        std::error_code error;
        const auto parent = std::filesystem::temp_directory_path(error); REQUIRE_FALSE(error);
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (int attempt = 0; ; ++attempt) {
            directory = parent / ("cataclysmx-house-" + std::to_string(stamp) + "-" + std::to_string(attempt));
            const bool created = std::filesystem::create_directory(directory, error); REQUIRE_FALSE(error);
            if (created) { break; }
        }
        path = (directory / "world.sqlite").string();
    }
    ~SaveFile() { std::error_code error; std::filesystem::remove_all(directory, error); }
};

TEST_CASE("Generated population and empty source snapshots survive RAM and disk reload", "[house][world][save]") {
    const auto content = HouseContent();
    const auto plan = GenerateRegionPlan(content, 42, {0, 0});
    REQUIRE_FALSE(plan.buildings.empty());
    const auto& building = plan.buildings[0];
    ChunkPlacements placements;
    TileCoord furniturePosition{};
    bool found = false;
    for (int z = ChunkOf(building.bounds.min).z; z <= ChunkOf(building.bounds.max).z && !found; ++z) {
        for (int x = ChunkOf(building.bounds.min).x; x <= ChunkOf(building.bounds.max).x && !found; ++x) {
            GenerateChunkPlacements(content, 42, plan, {x, z, 0}, placements);
            if (!placements.furniture.empty()) { furniturePosition = placements.furniture.front().position; found = true; }
        }
    }
    REQUIRE(found);
    SaveFile file;
    PersistentId movedId = 0, itemId = 0;
    TileCoord destination{};
    SessionState session;
    {
        World world(content);
        const auto origin = Take(world.RequestChunks({{0, 0}, 0}, LoadMode::active));
        const auto house = Take(world.RequestChunks({ChunkOf(furniturePosition), 1}, LoadMode::active));
        REQUIRE(world.ReconcileChunks());
        const auto player = Take(world.CreateActor({0, 0}, true));
        session.player = world.GetActor(player)->id;
        const auto object = world.ObjectAt(furniturePosition); REQUIRE(object.value);
        REQUIRE(world.GetObject(object)->contents.size() == 1);
        movedId = world.GetObject(object)->id;
        const auto item = world.GetObject(object)->contents.front(); itemId = world.GetItem(item)->id;
        CHECK(world.GetObject(object)->kind == ObjectKind::Table);
        CHECK(world.GetObject(object)->enabled);
        CHECK(world.GetObject(object)->light == LightEmission{180, 4});
        // Remove every other generated entity, leaving truly empty source chunks.
        for (ChunkCoord chunk : world.LoadedChunks()) {
            const auto actors = world.GetChunk(chunk)->actors;
            for (auto actor : actors) {
                if (actor == player) { continue; }
                CHECK(world.GetActor(actor)->speed == 125);
                CHECK(world.GetActor(actor)->combat.attackDamage == 17);
                CHECK(world.GetActor(actor)->combat.accuracy == 87);
                REQUIRE(world.DestroyActor(actor));
            }
            const auto objects = world.GetChunk(chunk)->objects;
            for (auto other : objects) { if (other != object) { REQUIRE(world.DestroyObject(other)); } }
            const auto ground = world.GetChunk(chunk)->ground_items;
            for (auto groundItem : ground) { REQUIRE(world.DestroyItem(groundItem)); }
        }
        // Administrative cross-chunk movement retains typed storage and contents.
        destination = {furniturePosition.x + chunk_size, furniturePosition.z, furniturePosition.y};
        REQUIRE(world.SetTile(destination, {content.FindTerrain("floor"), 0}));
        REQUIRE(world.MoveObject(object, destination));
        REQUIRE(world.ReleaseRequest(house)); REQUIRE(world.ReconcileChunks());
        CHECK_FALSE(world.FindObject(movedId).value);
        Take(world.RequestChunks({ChunkOf(furniturePosition), 1}, LoadMode::active));
        REQUIRE(world.ReconcileChunks());
        CHECK_FALSE(world.ObjectAt(furniturePosition).value);
        const auto restored = world.FindObject(movedId); REQUIRE(restored.value);
        CHECK(world.GetObject(restored)->position == destination);
        CHECK(world.GetObject(restored)->enabled);
        CHECK(world.GetObject(restored)->light == LightEmission{180, 4});
        CHECK(world.GetItem(world.FindItem(itemId))->location == ItemLocation::Stored(restored));
        REQUIRE(world.CheckInvariants());
        REQUIRE(world.CreateSave(file.path.c_str(), session));
        REQUIRE(world.ReleaseRequest(origin));
    }
    World loaded(content);
    Take(loaded.LoadSave(file.path.c_str()));
    Take(loaded.RequestChunks({ChunkOf(furniturePosition), 1}, LoadMode::active));
    REQUIRE(loaded.ReconcileChunks());
    CHECK_FALSE(loaded.ObjectAt(furniturePosition).value);
    const auto object = loaded.FindObject(movedId); REQUIRE(object.value);
    CHECK(loaded.GetObject(object)->position == destination);
    CHECK(loaded.GetObject(object)->enabled);
    CHECK(loaded.GetObject(object)->light == LightEmission{180, 4});
    CHECK(loaded.GetObjectUsedVolume(object) == 40);
    CHECK(loaded.GetItem(loaded.FindItem(itemId))->location == ItemLocation::Stored(object));
    Vector<ActorHandle> monsters;
    loaded.CollectActiveActors(monsters);
    CHECK(monsters.empty());
    const auto newer = Take(loaded.CreateObject(content.FindObject("legacy"), furniturePosition));
    CHECK(loaded.GetObject(newer)->id > movedId); CHECK(loaded.GetObject(newer)->id > itemId);
    REQUIRE(loaded.CheckInvariants());
}
}
