#include "core/world.h"
#include "core/save_database.h"
#include "crafting/crafting_state.h"
#include "combat/combat_system.h"
#include "client/power_view.h"
#include "network/wire.h"
#include "power/power_system.h"
#include "content_fixture.h"
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <string>
#include <sqlite3.h>

namespace {
using namespace cataclysmx;
namespace wire = cataclysmx::network::wire;
template<class T, class E> T Take(eastl::expected<T, E> value) { REQUIRE(value); return eastl::move(*value); }

String WorkshopSource() {
    return R"({format_version:1,definitions:[
      {type:'terrain',id:'floor',name:'Floor',movement:{passable:true},blocks_sight:false,appearance:{symbol:'.',foreground:'white'}},
      {type:'terrain',id:'wall',name:'Wall',movement:{passable:false},blocks_sight:true,appearance:{symbol:'#',foreground:'white'}},
      {type:'terrain',id:'air',name:'Air',movement:{passable:true},open_space:true,blocks_sight:false,appearance:{symbol:' ',foreground:'white'}},
      {type:'tool_trait',id:'anvil',name:'Anvil'},{type:'tool_trait',id:'hammer',name:'Hammer'},
      {type:'item',id:'kit',name:'Anvil kit',mass_g:5000,volume_ml:5000,placed_object:'anvil_station',
       capabilities:[{type:'energy_store',id:'power',capacity_energy:100,initial_energy:80,rechargeable:true}]},
      {type:'item',id:'part',name:'Part',mass_g:10,volume_ml:10},
      {type:'item',id:'tool',name:'Tool',mass_g:10,volume_ml:10,tool_traits:[{id:'hammer',tier:1}],
       crafting_power:{store:'power',energy_per_work:10000},
       capabilities:[{type:'energy_store',id:'power',capacity_energy:1000,initial_energy:1000,rechargeable:true}]},
      {type:'recipe',id:'make',inputs:[{item:'part',count:1}],tool_traits:[{id:'anvil',tier:1}],
       result:{item:'part',count:1},work:250},
      {type:'recipe',id:'high',inputs:[{item:'part',count:1}],tool_traits:[{id:'anvil',tier:2}],
       result:{item:'part',count:1},work:250},
      {type:'recipe',id:'mixed',inputs:[{item:'part',count:1}],tool_traits:[{id:'anvil',tier:1},{id:'hammer',tier:1}],
       result:{item:'part',count:1},work:250}
    ],objects:[
      {id:'anvil_station',name:'Anvil',behavior:'none',packing_item:'kit',tool_traits:[{id:'anvil',tier:1}],
       capacity_volume_ml:10,blocks_movement:true,movable:true},
      {id:'fixture',behavior:'none',name:'Fixed anvil',tool_traits:[{id:'anvil',tier:2}],blocks_movement:true},
      {id:'multi',behavior:'none',name:'Multi station',tool_traits:[{id:'anvil',tier:1},{id:'hammer',tier:1}]}
    ]})";
}
ContentCatalog WorkshopContent(const String& text = WorkshopSource()) {
    const ContentSource source{"workshop-test", {text.data(), text.size()}};
    Diagnostics diagnostics; auto loaded = LoadContent({&source, 1}, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str()); REQUIRE(loaded); return eastl::move(*loaded);
}
String PoweredWorkshopSource() {
    auto text = WorkshopSource();
    text.insert(text.find("packing_item:'kit'"), "crafting_power:{store:'grid',energy_per_work:10000},");
    text.insert(text.find("placed_object:'anvil_station'"), "crafting_power:{store:'power',energy_per_work:10000},");
    text.insert(text.find("{type:'item',id:'part'"), R"(
      {type:'item',id:'battery',name:'Battery',mass_g:1,volume_ml:1,placed_object:'battery_box',
       capabilities:[{type:'energy_store',id:'cell',capacity_energy:1000,initial_energy:1000,rechargeable:true,battery_class:'cell'}]},
    )");
    text.insert(text.find("{id:'anvil_station'"), "{id:'battery_box',name:'Battery box',behavior:'grid_battery'},");
    return text;
}
struct Rig {
    ContentCatalog content;
    World world{content};
    LoadRequest request{};
    ActorHandle actor{}, other{};
    explicit Rig(const String& text = WorkshopSource()) : content(WorkshopContent(text)) {
        request = Take(world.RequestChunks({{0, 0}, 0}, LoadMode::active)); REQUIRE(world.ReconcileChunks());
        for (int z = 0; z < 8; ++z) for (int x = 0; x < 8; ++x) {
            REQUIRE(world.SetTile({x, z}, {content.FindTerrain("floor")}));
        }
        actor = Take(world.CreateActor({2, 2}, true)); other = Take(world.CreateActor({4, 2}, true));
        Add("part", ItemLocation::Carried(actor)); Add("part", ItemLocation::Carried(other));
    }
    ItemHandle Add(const char* type, ItemLocation location) { return Take(world.CreateItem(content.FindItem(type), location)); }
    ObjectHandle Station(const char* type = "anvil_station", TileCoord position = {3, 2}) {
        return Take(world.CreateObject(content.FindObject(type), position));
    }
    CraftingResult Start(ActorHandle worker, const char* recipe = "make") {
        return Take(world.StartCrafting(worker, content.FindCraftingRecipe(recipe)));
    }
    void Step(Tick ticks) { world.ElapseTicks(ticks); world.ProcessCraftingEvents(); }
    ItemHandle Supply(std::uint32_t energy = 1000) {
        Take(world.Power().ExtendBounds({{3, 2}, {3, 3}}));
        const auto box = Station("battery_box", {3, 3});
        const auto battery = Add("battery", ItemLocation::Stored(box));
        const auto store = FindItemCapability(content.GetItem(world.GetItem(battery)->type), "cell");
        REQUIRE(world.SetItemEnergy(battery, store, energy)); world.Power().SyncObject(box);
        return battery;
    }
    std::uint32_t Energy(ItemHandle item, const char* name = "cell") const {
        return world.GetItemEnergy(item, FindItemCapability(content.GetItem(world.GetItem(item)->type), name))->energy;
    }
    CraftingJobState Job(ItemHandle item) const {
        const auto state = world.CaptureItemState(item);
        const auto* payload = state.Find("crafting_job"); REQUIRE(payload);
        return Take(ReadCraftingState(*payload));
    }
};
struct SaveFile {
    std::filesystem::path directory = std::filesystem::temp_directory_path()
        / ("cataclysmx-workshop-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::string path;
    SaveFile() { REQUIRE(std::filesystem::create_directory(directory)); path = (directory / "world.sqlite").string(); }
    ~SaveFile() { std::error_code error; std::filesystem::remove_all(directory, error); }
};
void Sql(const std::string& path, const char* command) {
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(path.c_str(), &db) == SQLITE_OK);
    const auto result = sqlite3_exec(db, command, nullptr, nullptr, nullptr);
    sqlite3_close(db); REQUIRE(result == SQLITE_OK);
}

TEST_CASE("Station content validates unique portable mappings and fingerprints traits", "[workshop][content]") {
    auto text = WorkshopSource();
    const auto replace = [&](eastl::string_view from, eastl::string_view to) {
        const auto index = text.find(from.data(), 0, from.size()); REQUIRE(index != String::npos);
        text.replace(index, from.size(), to.data(), to.size());
    };
    SECTION("unresolved packing item") { replace("packing_item:'kit'", "packing_item:'missing'"); }
    SECTION("packing item points elsewhere") { replace("placed_object:'anvil_station'", "placed_object:'fixture'"); }
    SECTION("packing items cannot supply carried station traits") {
        text.insert(text.find("placed_object:'anvil_station'"), "tool_traits:[{id:'anvil',tier:1}],");
    }
    SECTION("unknown trait") { replace("packing_item:'kit',tool_traits:[{id:'anvil',tier:1}]",
        "packing_item:'kit',tool_traits:[{id:'unknown',tier:1}]"); }
    SECTION("valid traits affect content identity") {
        const auto original = WorkshopContent(text);
        replace("name:'Fixed anvil',tool_traits:[{id:'anvil',tier:2}]",
            "name:'Fixed anvil',tool_traits:[{id:'anvil',tier:3}]");
        const auto changed = WorkshopContent(text);
        CHECK(network::ContentFingerprint(original) != network::ContentFingerprint(changed));
        return;
    }
    const ContentSource source{"invalid-workshop", {text.data(), text.size()}};
    CHECK_FALSE(LoadContent({&source, 1}));
}

TEST_CASE("Station traits require placement tier reach and enabled equipment", "[workshop][crafting]") {
    Rig r;
    const auto kit = r.Add("kit", ItemLocation::Carried(r.actor));
    CHECK(r.Start(r.actor).outcome == CraftingOutcome::MissingTools);
    const auto placed = Take(r.world.PlaceStation(r.actor, kit, {3, 2}));
    REQUIRE(placed.outcome == StationActionOutcome::Placed); CHECK(placed.cost == 400);
    const auto station = r.world.ObjectAt({3, 2});
    CHECK(r.Start(r.actor, "high").outcome == CraftingOutcome::MissingTools);
    SECTION("blocking furniture is a valid endpoint without walk-through") {
        const auto job = r.Start(r.actor); REQUIRE(job.outcome == CraftingOutcome::Started);
        CHECK(r.world.StationWorker(station) == r.world.GetActor(r.actor)->id);
    }
    SECTION("disabled") {
        REQUIRE(r.world.SetObjectEnabled(station, false));
        CHECK(r.Start(r.actor).outcome == CraftingOutcome::MissingTools);
    }
    SECTION("broken") {
        REQUIRE(r.world.SetObjectAppearance(station, ObjectKind::Generic, ObjectFacing::North, true));
        CHECK(r.Start(r.actor).outcome == CraftingOutcome::MissingTools);
    }
    SECTION("wall and diagonal corner") {
        REQUIRE(r.world.MoveObject(station, {3, 3}));
        REQUIRE(r.world.SetTile({3, 2}, {r.content.FindTerrain("wall")}));
        REQUIRE(r.world.SetTile({2, 3}, {r.content.FindTerrain("wall")}));
        CHECK(r.Start(r.actor).outcome == CraftingOutcome::MissingTools);
        REQUIRE(r.world.SetTile({3, 2}, {r.content.FindTerrain("floor")}));
        CHECK(r.Start(r.actor).outcome == CraftingOutcome::Started);
    }
    REQUIRE(r.world.CheckInvariants());
}

TEST_CASE("Station contention reserves nothing and pause releases at the paid boundary", "[workshop][crafting]") {
    Rig r; const auto station = r.Station();
    const auto first = r.Start(r.actor); REQUIRE(first.outcome == CraftingOutcome::Started);
    const auto inventory = r.world.GetActor(r.other)->inventory;
    CHECK(r.Start(r.other).outcome == CraftingOutcome::StationBusy);
    CHECK(r.world.GetActor(r.other)->inventory == inventory);
    CHECK(r.world.GetActorRemainingTicks(r.other) == 0);
    CHECK(r.world.QueryPackStation(r.other, station) == StationActionOutcome::StationBusy);
    CHECK(r.world.QueryObjectMove(r.other, station, ObjectMoveKind::Push).outcome == ObjectMoveOutcome::Occupied);
    Take(r.world.PauseCrafting(r.actor, first.unfinished)); r.Step(99);
    CHECK(r.world.StationWorker(station) == r.world.GetActor(r.actor)->id);
    r.Step(1); CHECK(r.world.StationWorker(station) == 0);
    const auto second = r.Start(r.other); REQUIRE(second.outcome == CraftingOutcome::Started);
    CHECK(Take(r.world.ResumeCrafting(r.actor, first.unfinished)).outcome == CraftingOutcome::StationBusy);
    Take(r.world.CancelCrafting(r.other, second.unfinished));
    CHECK(r.world.StationWorker(station) == 0);
    CHECK(Take(r.world.ResumeCrafting(r.actor, first.unfinished)).outcome == CraftingOutcome::Resumed);
    r.Step(100); r.Step(50);
    CHECK_FALSE(r.world.GetItem(first.unfinished)); CHECK(r.world.StationWorker(station) == 0);
    REQUIRE(r.world.CheckInvariants());
}

TEST_CASE("Station selection skips busy providers and claims multitrait furniture once", "[workshop][crafting]") {
    Rig r; const auto first = r.Station("multi");
    const auto started = r.Start(r.actor, "mixed"); REQUIRE(started.outcome == CraftingOutcome::Started);
    CHECK(r.world.InspectCrafting(started.unfinished)->stations.Size() == 1);
    const auto second = r.Station("fixture", {4, 3});
    const auto other = r.Start(r.other); REQUIRE(other.outcome == CraftingOutcome::Started);
    CHECK(r.world.StationWorker(first) == r.world.GetActor(r.actor)->id);
    CHECK(r.world.StationWorker(second) == r.world.GetActor(r.other)->id);
    REQUIRE(r.world.CheckInvariants());
}

TEST_CASE("Failed station funding spends no inputs or local power and claims nothing", "[workshop][crafting]") {
    Rig r; const auto station = r.Station();
    const auto tool = r.Add("tool", ItemLocation::Carried(r.actor));
    const auto power = FindItemCapability(r.content.GetItem(r.world.GetItem(tool)->type), "power");
    REQUIRE(r.world.SetItemEnergy(tool, power, 99));
    const auto inventory = r.world.GetActor(r.actor)->inventory;
    CHECK(r.Start(r.actor, "mixed").outcome == CraftingOutcome::InsufficientEnergy);
    CHECK(r.world.GetActor(r.actor)->inventory == inventory);
    CHECK(r.world.GetItemEnergy(tool, power)->energy == 99);
    CHECK(r.world.StationWorker(station) == 0);
    CHECK(r.Start(r.other).outcome == CraftingOutcome::Started);
    REQUIRE(r.world.CheckInvariants());
}

TEST_CASE("Station loss releases claims but preserves funded work", "[workshop][crafting]") {
    Rig r; const auto station = r.Station();
    const auto job = r.Start(r.actor).unfinished; REQUIRE(job.value);
    r.Step(30);
    SECTION("administrative move") { REQUIRE(r.world.MoveObject(station, {7, 7})); }
    SECTION("disabled") { REQUIRE(r.world.SetObjectEnabled(station, false)); }
    SECTION("destroyed") { REQUIRE(r.world.DestroyObject(station)); }
    SECTION("worker parked") {
        REQUIRE(r.world.SetActorParked(r.actor, true));
        CHECK(r.world.StationWorker(station) == 0);
        const auto other = r.Start(r.other); REQUIRE(other.outcome == CraftingOutcome::Started);
        REQUIRE(r.world.SetActorParked(r.actor, false));
        r.Step(70);
        CHECK(r.world.InspectCrafting(job)->completedWork == 100);
        CHECK(r.world.InspectCrafting(job)->status == CraftingStatus::StationBusy);
        REQUIRE(r.world.CheckInvariants()); return;
    }
    SECTION("worker dies") {
        r.world.Combat().Damage(r.actor, {}, BodyPart::Torso, {DamageType::Blunt, 10000});
        r.world.FinalizeActorDeaths();
        CHECK(r.world.StationWorker(station) == 0);
        const auto state = r.world.InspectCrafting(job); REQUIRE(state);
        CHECK(state->status == CraftingStatus::WorkerUnavailable);
        CHECK(state->stations.Empty()); CHECK(state->fundedTicksRemaining == 70);
        CHECK(r.Start(r.other).outcome == CraftingOutcome::Started);
        REQUIRE(r.world.CheckInvariants()); return;
    }
    CHECK(r.world.StationWorker(station) == 0);
    CHECK(r.world.InspectCrafting(job)->fundedTicksRemaining == 70);
    r.Step(70);
    CHECK(r.world.InspectCrafting(job)->completedWork == 100);
    CHECK(r.world.InspectCrafting(job)->status == CraftingStatus::MissingTools);
    REQUIRE(r.world.CheckInvariants());
}

TEST_CASE("Portable stations protect original equipment and recover its exact state", "[workshop][world]") {
    Rig r; const auto kit = r.Add("kit", ItemLocation::Carried(r.actor));
    const auto id = r.world.GetItem(kit)->id;
    const auto power = FindItemCapability(r.content.GetItem(r.world.GetItem(kit)->type), "power");
    REQUIRE(r.world.SetItemEnergy(kit, power, 17));
    const auto state = r.world.CaptureItemState(kit);
    REQUIRE(Take(r.world.PlaceStation(r.actor, kit, {3, 2})).outcome == StationActionOutcome::Placed);
    const auto station = r.world.ObjectAt({3, 2});
    CHECK(r.world.GetObject(station)->equipment == kit); CHECK(r.world.IsReservedItem(kit));
    CHECK_FALSE(r.world.CanAccessItem(r.actor, kit));
    CHECK_FALSE(r.world.TransferItem(kit, ItemLocation::Carried(r.actor))); CHECK_FALSE(r.world.DestroyItem(kit));
    CHECK(r.world.GetObjectUsedVolume(station) == 0);
    const auto stored = r.Add("part", ItemLocation::Stored(station));
    CHECK(r.world.GetObjectUsedVolume(station) == 10);
    CHECK(r.world.QueryPackStation(r.actor, station) == StationActionOutcome::NotEmpty);
    REQUIRE(r.world.TransferItem(stored, ItemLocation::Ground({2, 2})));
    SECTION("pack into inventory") {
        CHECK(Take(r.world.PackStation(r.actor, station)).outcome == StationActionOutcome::Packed);
        CHECK(r.world.GetItem(kit)->location == ItemLocation::Carried(r.actor));
    }
    SECTION("pack with ground fallback") {
        REQUIRE(r.world.SetActorCarryCapacity(r.actor, 10));
        CHECK(Take(r.world.PackStation(r.actor, station)).outcome == StationActionOutcome::Packed);
        CHECK(r.world.GetItem(kit)->location == ItemLocation::Ground({3, 2}));
    }
    SECTION("destruction drops exactly once") {
        REQUIRE(r.world.DestroyObject(station));
        CHECK(r.world.GetItem(kit)->location == ItemLocation::Ground({3, 2}));
        CHECK(r.world.CountItemsAt({3, 2}) == 1);
    }
    CHECK_FALSE(r.world.GetObject(station)); CHECK(r.world.GetItem(kit)->id == id);
    CHECK(r.world.CaptureItemState(kit) == state); CHECK_FALSE(r.world.IsReservedItem(kit));
    REQUIRE(r.world.CheckInvariants());
}

TEST_CASE("Station claims and original equipment survive disk and release across unload", "[workshop][save]") {
    SaveFile file; Rig r;
    const auto station = r.Station(); const auto stationId = r.world.GetObject(station)->id;
    const auto kitId = r.world.GetItem(r.world.GetObject(station)->equipment)->id;
    const auto job = r.Start(r.actor).unfinished; REQUIRE(job.value);
    const auto jobId = r.world.GetItem(job)->id, actorId = r.world.GetActor(r.actor)->id;
    r.Step(30);
    SessionState session;
    Diagnostics diagnostics; const auto saved = r.world.CreateSave(file.path.c_str(), session, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str()); REQUIRE(saved);
    World restored(r.content); Take(restored.LoadSave(file.path.c_str()));
    auto request = Take(restored.RequestChunks({{0, 0}, 0}, LoadMode::active)); REQUIRE(restored.ReconcileChunks());
    CHECK(restored.StationWorker(restored.FindObject(stationId)) == actorId);
    CHECK(restored.GetObject(restored.FindObject(stationId))->equipment == restored.FindItem(kitId));
    CHECK(restored.InspectCrafting(restored.FindItem(jobId))->fundedTicksRemaining == 70);
    REQUIRE(restored.ReleaseRequest(request)); REQUIRE(restored.ReconcileChunks());
    request = Take(restored.RequestChunks({{0, 0}, 0}, LoadMode::active)); REQUIRE(restored.ReconcileChunks());
    CHECK(restored.StationWorker(restored.FindObject(stationId)) == 0);
    CHECK(restored.InspectCrafting(restored.FindItem(jobId))->fundedTicksRemaining == 70);
    restored.ElapseTicks(70); restored.ProcessCraftingEvents();
    CHECK(restored.StationWorker(restored.FindObject(stationId)) == actorId);
    Take(restored.CancelCrafting(restored.FindActor(actorId), restored.FindItem(jobId)));
    CHECK(Take(restored.PackStation(restored.FindActor(actorId), restored.FindObject(stationId))).outcome == StationActionOutcome::Packed);
    REQUIRE(restored.FindItem(kitId).value); REQUIRE(restored.CheckInvariants());
}

TEST_CASE("Workshop saves reject invalid equipment and station claims", "[workshop][save]") {
    SaveFile file; Rig r; r.Station();
    REQUIRE(r.Start(r.actor).outcome == CraftingOutcome::Started);
    const auto second = r.Station("fixture", {4, 3});
    REQUIRE(r.Start(r.other).outcome == CraftingOutcome::Started);
    SessionState session;
    Diagnostics diagnostics; const auto saved = r.world.CreateSave(file.path.c_str(), session, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str()); REQUIRE(saved);
    SECTION("missing backing identity") {
        Sql(file.path, "UPDATE chunks SET payload=json_set(payload,'$.objects[0].equipment','0') WHERE json_array_length(json_extract(payload,'$.objects'))>0");
    }
    SECTION("nonexistent station claim") {
        const auto id = r.world.GetObject(r.world.ObjectAt({3, 2}))->id;
        const std::string command = "UPDATE chunks SET payload=replace(payload,'\"stations\":[" + std::to_string(id)
            + "]','\"stations\":[999999]')";
        Sql(file.path, command.c_str());
    }
    SECTION("duplicate station claim") {
        const auto firstId = r.world.GetObject(r.world.ObjectAt({3, 2}))->id;
        const auto secondId = r.world.GetObject(second)->id;
        const std::string command = "UPDATE chunks SET payload=replace(payload,'\"stations\":[" + std::to_string(secondId)
            + "]','\"stations\":[" + std::to_string(firstId) + "]')";
        Sql(file.path, command.c_str());
    }
    SECTION("disabled claimed station") {
        Sql(file.path, "UPDATE chunks SET payload=json_set(payload,'$.objects[0].enabled',json('false')) WHERE json_array_length(json_extract(payload,'$.objects'))>0");
    }
    SaveDatabase db(r.content); CHECK_FALSE(db.Open(file.path.c_str()));
}

TEST_CASE("Station bindings release when only the provider or wrapper becomes inactive", "[workshop][save]") {
    Rig r;
    const auto remote = Take(r.world.RequestChunks({{1, 0}, 0}, LoadMode::active)); REQUIRE(r.world.ReconcileChunks());
    REQUIRE(r.world.SetTile({16, 2}, {r.content.FindTerrain("floor")}));
    ObjectHandle station{};
    SECTION("provider alone becomes resident") {
        REQUIRE(r.world.SetTile({14, 2}, {r.content.FindTerrain("floor")}));
        REQUIRE(r.world.SetTile({15, 2}, {r.content.FindTerrain("floor")}));
        REQUIRE(r.world.MoveActor(r.actor, {14, 2})); REQUIRE(r.world.SetCraftingSettings({2, 100}));
        station = r.Station("fixture", {16, 2});
        const auto job = r.Start(r.actor).unfinished; REQUIRE(job.value);
        r.Step(30);
        REQUIRE(r.world.UpdateRequest(remote, {{1, 0}, 0}, LoadMode::resident)); REQUIRE(r.world.ReconcileChunks());
        CHECK(r.world.InspectCrafting(job)->stations.Empty());
        CHECK(r.world.StationWorker(station) == 0);
        r.Step(70); CHECK(r.world.InspectCrafting(job)->completedWork == 100);
        CHECK(r.world.InspectCrafting(job)->status == CraftingStatus::MissingTools);
    }
    SECTION("wrapper alone unloads") {
        station = r.Station();
        const auto job = r.Start(r.actor).unfinished; REQUIRE(job.value);
        const auto id = r.world.GetItem(job)->id;
        r.Step(30); REQUIRE(r.world.TransferItem(job, ItemLocation::Ground({16, 2})));
        REQUIRE(r.world.ReleaseRequest(remote)); REQUIRE(r.world.ReconcileChunks());
        CHECK(r.world.StationWorker(station) == 0);
        Take(r.world.RequestChunks({{1, 0}, 0}, LoadMode::active)); REQUIRE(r.world.ReconcileChunks());
        const auto restored = r.world.FindItem(id); REQUIRE(restored.value);
        CHECK(r.world.InspectCrafting(restored)->stations.Empty());
        CHECK(r.world.InspectCrafting(restored)->fundedTicksRemaining == 70);
    }
    REQUIRE(r.world.CheckInvariants());
}

TEST_CASE("Workshop legacy saves retain paid work without inventing claims", "[workshop][save]") {
    SaveFile file; Rig r; r.Station("fixture");
    const auto job = r.Start(r.actor).unfinished; REQUIRE(job.value);
    r.Step(30);
    REQUIRE(r.world.CreateSave(file.path.c_str(), {}));
    Sql(file.path, R"(UPDATE chunks SET payload=json_remove(payload,
      '$.objects[0].equipment','$.actors[0].inventory[0].state.crafting_job.stations',
      '$.actors[0].inventory[0].state.crafting_job.heat_remainder')
      WHERE json_array_length(json_extract(payload,'$.objects'))>0;
      UPDATE chunks SET payload=replace(payload,'"version":4,"kind"','"version":2,"kind"');
      UPDATE metadata SET format_version=15;)");
    SaveDatabase db(r.content);
    const auto snapshot = Take(db.Open(file.path.c_str()));
    const auto chunk = Take(db.ReadChunk({0, 0}, snapshot.nextId)); REQUIRE(chunk);
    bool found = false;
    for (const auto& item : chunk->items) {
        if (item.type != r.content.UnfinishedCraftItem()) { continue; }
        const auto state = Take(ReadCraftingState(*item.state.Find("crafting_job")));
        CHECK(state.stations.Empty()); CHECK(state.remaining == 70); CHECK(state.pendingWork == 1000000);
        found = true;
    }
    CHECK(found);
    REQUIRE(db.Begin()); REQUIRE(db.Commit());
}

TEST_CASE("Workshop job payload validates bindings and migrates version two", "[workshop][save]") {
    CraftingJobState state; state.recipeName = "make"; state.totalWork = 250;
    state.worker = 1; state.pendingWork = 1000000; state.remaining = 70; state.status = CraftingStatus::Working;
    REQUIRE(state.stations.PushBack(7)); REQUIRE(state.stations.PushBack(9));
    const auto payload = WriteCraftingState(state);
    CHECK(Take(ReadCraftingState(payload)).stations == state.stations);
    auto legacy = DataObject{};
    for (std::size_t i = 0; i < payload.Size(); ++i) {
        if (payload.Name(i) != "stations" && payload.Name(i) != "heat_remainder") {
            legacy.Set(payload.Name(i), payload.At(i));
        }
    }
    legacy.SetUnsigned("version", 2);
    CHECK(Take(ReadCraftingState(legacy)).stations.Empty());
    auto bad = payload;
    SECTION("duplicate station") { state.stations[1] = 7; bad = WriteCraftingState(state); }
    SECTION("unordered station") { state.stations[0] = 10; bad = WriteCraftingState(state); }
    SECTION("zero station") { state.stations[0] = 0; bad = WriteCraftingState(state); }
    SECTION("unassigned paid remainder") {
        state.status = CraftingStatus::WorkerUnavailable; state.worker = 0; bad = WriteCraftingState(state);
    }
    SECTION("wrong binding type") { bad.SetUnsigned("stations", 7); }
    CHECK_FALSE(ReadCraftingState(bad));
}

TEST_CASE("Workshop shared controls server commands and LAN views round trip", "[workshop][network][ui]") {
    auto content = TestContent();
    const auto type = content.FindObject("scrap_press");
    auto& stationDefinition = const_cast<ObjectDefinition&>(content.GetObject(type));
    stationDefinition.behavior = ObjectBehaviorKind::None; stationDefinition.machine = {};
    stationDefinition.packingItem = content.FindItem("rock");
    auto& packingDefinition = const_cast<ItemDefinition&>(content.GetItem(stationDefinition.packingItem));
    packingDefinition.placedObject = type;
    REQUIRE(ValidateServerContent(content)); Server server(content);
    auto& world = server.GetWorld(); const auto actor = server.GetPlayer();
    const auto position = world.GetActor(actor)->position;
    const TileCoord destination{position.x + 1, position.z, position.y};
    if (const auto existing = world.ObjectAt(destination); existing.value) REQUIRE(world.DestroyObject(existing));
    REQUIRE(world.SetTile(destination, {content.FindTerrain("floor")}));
    const auto kit = Take(world.CreateItem(stationDefinition.packingItem, ItemLocation::Carried(actor)));
    auto view = server.View(1); PowerWorkshop workshop; workshop.Open(view);
    workshop.Handle({"p"}, view, content); workshop.Handle({"Right"}, view, content);
    const auto command = workshop.Handle({"Enter"}, view, content); REQUIRE(command);
    CHECK(command->kind == CommandKind::place_station); CHECK(command->item == world.GetItem(kit)->id);
    wire::Writer writer; REQUIRE(writer.CommandValue(*command));
    Command decoded; wire::Reader reader{{writer.bytes.data(), writer.bytes.size()}};
    REQUIRE(reader.CommandValue(decoded)); REQUIRE(reader.Done()); CHECK(decoded.position == destination);
    auto invalid = *command; invalid.otherItem = 17; CHECK_FALSE(ValidCommandShape(invalid));
    const auto placed = server.Send(decoded); REQUIRE(placed.outcome == Outcome::station_changed);
    CHECK(placed.stationOutcome == StationActionOutcome::Placed);
    CHECK(Localization{}.ResultText(placed) == "Workshop station placed");
    PlayerCommandResult result{server.GetHostPlayerId(), 1, 1, placed};
    wire::Writer resultWriter; REQUIRE(resultWriter.Result(result));
    PlayerCommandResult resultCopy; wire::Reader resultReader{{resultWriter.bytes.data(), resultWriter.bytes.size()}};
    REQUIRE(resultReader.Result(resultCopy)); REQUIRE(resultReader.Done());
    CHECK(resultCopy.result.stationOutcome == placed.stationOutcome);
    view = server.View(1); REQUIRE(view.stations.size() == 1);
    CHECK(view.stations[0].packing == StationActionOutcome::Packed);
    wire::Writer viewWriter; REQUIRE(viewWriter.View(view));
    WorldView viewCopy; wire::Reader viewReader{{viewWriter.bytes.data(), viewWriter.bytes.size()}};
    REQUIRE(viewReader.View(viewCopy)); REQUIRE(viewReader.Done());
    CHECK(viewCopy.stations == view.stations); REQUIRE(wire::ValidItemDefinitions(viewCopy, content));
    auto claimedView = view;
    CraftingEntry job; job.id = 999999; job.craftingRecipe = {1};
    job.totalWork = content.GetCraftingRecipe(job.craftingRecipe).work;
    job.status = CraftingStatus::Working; job.worker = view.characterId; job.fundedTicksRemaining = 10;
    REQUIRE(job.stations.PushBack(view.stations[0].id));
    claimedView.craftingJobs.push_back(job);
    claimedView.stations[0].worker = job.worker;
    claimedView.stations[0].packing = StationActionOutcome::StationBusy;
    wire::Writer claimWriter; REQUIRE(claimWriter.View(claimedView));
    WorldView claimCopy; wire::Reader claimReader{{claimWriter.bytes.data(), claimWriter.bytes.size()}};
    REQUIRE(claimReader.View(claimCopy)); REQUIRE(claimReader.Done());
    CHECK(claimCopy.craftingJobs == claimedView.craftingJobs);
    CHECK(claimCopy.stations == claimedView.stations);
    auto invalidView = claimedView; invalidView.stations.push_back(invalidView.stations[0]);
    wire::Writer duplicate; CHECK_FALSE(duplicate.View(invalidView));
    invalidView = claimedView; invalidView.stations[0].broken = true;
    wire::Writer broken; CHECK_FALSE(broken.View(invalidView));
    invalidView = claimedView; invalidView.craftingJobs[0].worker = 0; invalidView.craftingJobs[0].status = CraftingStatus::Paused;
    wire::Writer orphan; CHECK_FALSE(orphan.View(invalidView));
    Vector<String> lines; workshop.Describe(viewCopy, content, lines);
    bool shown = false; for (const auto& line : lines) shown |= line.find("can pack") != String::npos;
    CHECK(shown);
    const auto pack = workshop.Handle({"r"}, viewCopy, content); REQUIRE(pack);
    CHECK(pack->kind == CommandKind::pack_station); REQUIRE(ValidCommandShape(*pack));
    CHECK(server.Send(*pack).stationOutcome == StationActionOutcome::Packed);
    CHECK(world.GetItem(kit)->location == ItemLocation::Carried(actor));
    REQUIRE(world.CheckInvariants());
}

TEST_CASE("Powered portable stations retain occupancy and never debit their backing tool", "[workshop][crafting][power]") {
    Rig r(PoweredWorkshopSource()); const auto station = r.Station();
    const auto kit = r.world.GetObject(station)->equipment;
    const auto battery = r.Supply();
    const auto job = r.Start(r.actor); REQUIRE(job.outcome == CraftingOutcome::Started);
    CHECK(r.Energy(battery) == 900); CHECK(r.Energy(kit, "power") == 80);
    CHECK(r.Start(r.other).outcome == CraftingOutcome::StationBusy);
    CHECK(r.world.QueryPackStation(r.actor, station) == StationActionOutcome::Unavailable);
    CHECK(r.world.QueryPackStation(r.other, station) == StationActionOutcome::StationBusy);
    REQUIRE(Take(r.world.PauseCrafting(r.actor, job.unfinished)).outcome == CraftingOutcome::PauseRequested);
    r.Step(99); CHECK(r.world.StationWorker(station) != 0);
    r.Step(1); CHECK(r.world.StationWorker(station) == 0);
    CHECK(r.Energy(battery) == 900);
    REQUIRE(Take(r.world.PackStation(r.actor, station)).outcome == StationActionOutcome::Packed);
    CHECK(r.world.GetItem(kit)->location == ItemLocation::Carried(r.actor));
    CHECK(r.Energy(kit, "power") == 80);
    CHECK(Take(r.world.ResumeCrafting(r.actor, job.unfinished)).outcome == CraftingOutcome::MissingTools);
    REQUIRE(r.world.CheckInvariants());
}

TEST_CASE("Powered station paid work survives worker loss without repaying or reclaiming", "[workshop][crafting][power]") {
    Rig r(PoweredWorkshopSource()); const auto station = r.Station(); const auto battery = r.Supply();
    const auto job = r.Start(r.actor); REQUIRE(job.outcome == CraftingOutcome::Started);
    r.Step(30);
    r.world.Combat().Damage(r.actor, {}, BodyPart::Torso, {DamageType::Blunt, 10000});
    r.world.FinalizeActorDeaths();
    CHECK(r.world.StationWorker(station) == 0); CHECK(r.Energy(battery) == 900);
    CHECK(r.world.InspectCrafting(job.unfinished)->fundedTicksRemaining == 70);
    REQUIRE(r.world.TransferItem(job.unfinished, ItemLocation::Carried(r.other)));
    REQUIRE(Take(r.world.ResumeCrafting(r.other, job.unfinished)).outcome == CraftingOutcome::Resumed);
    CHECK(r.world.StationWorker(station) == 0); CHECK(r.Energy(battery) == 900);
    REQUIRE(Take(r.world.PauseCrafting(r.other, job.unfinished)).outcome == CraftingOutcome::PauseRequested);
    r.Step(70);
    CHECK(r.world.InspectCrafting(job.unfinished)->completedWork == 100);
    CHECK(r.Energy(battery) == 900);
    REQUIRE(Take(r.world.ResumeCrafting(r.other, job.unfinished)).outcome == CraftingOutcome::Resumed);
    CHECK(r.Energy(battery) == 800);
    CHECK(r.world.StationWorker(station) == r.world.GetActor(r.other)->id);
    REQUIRE(r.world.CheckInvariants());
}

TEST_CASE("Powered station fractional debt survives pause speed change replacement and disk", "[workshop][crafting][power][save]") {
    SaveFile file; Rig r(PoweredWorkshopSource()); const auto station = r.Station(); const auto battery = r.Supply(1);
    REQUIRE(r.world.SetCraftingSettings({1, 1})); REQUIRE(r.world.SetActorSpeed(r.actor, 200));
    const auto job = r.Start(r.actor); REQUIRE(job.outcome == CraftingOutcome::Started);
    REQUIRE(Take(r.world.PauseCrafting(r.actor, job.unfinished)).outcome == CraftingOutcome::PauseRequested);
    r.Step(1);
    CHECK(r.Job(job.unfinished).energyRemainder == 50000000); CHECK(r.Energy(battery) == 1);
    const auto actorId = r.world.GetActor(r.actor)->id, jobId = r.world.GetItem(job.unfinished)->id;
    const auto batteryId = r.world.GetItem(battery)->id;
    REQUIRE(r.world.DestroyObject(station)); r.Station();
    REQUIRE(r.world.CreateSave(file.path.c_str(), {}));
    World restored(r.content); Take(restored.LoadSave(file.path.c_str()));
    Take(restored.RequestChunks({{0, 0}, 0}, LoadMode::active)); REQUIRE(restored.ReconcileChunks());
    const auto worker = restored.FindActor(actorId);
    const auto unfinished = restored.FindItem(jobId);
    CHECK(restored.InspectCrafting(unfinished)->status == CraftingStatus::Paused);
    REQUIRE(restored.SetCraftingSettings({1, 1})); REQUIRE(restored.SetActorSpeed(worker, 100));
    REQUIRE(Take(restored.ResumeCrafting(worker, unfinished)).outcome == CraftingOutcome::Resumed);
    const auto cell = FindItemCapability(r.content.GetItem(r.content.FindItem("battery")), "cell");
    CHECK(restored.GetItemEnergy(restored.FindItem(batteryId), cell)->energy == 0);
    const auto state = restored.CaptureItemState(unfinished);
    CHECK(Take(ReadCraftingState(*state.Find("crafting_job"))).energyRemainder == 50000000);
    // The next failed preflight cannot discard this carried half-unit balance.
    restored.ElapseTicks(1); restored.ProcessCraftingEvents();
    CHECK(restored.InspectCrafting(unfinished)->status == CraftingStatus::InsufficientEnergy);
    const auto failed = restored.CaptureItemState(unfinished);
    CHECK(Take(ReadCraftingState(*failed.Find("crafting_job"))).energyRemainder == 50000000);
    REQUIRE(restored.CheckInvariants());
}

TEST_CASE("A fixture powered soldering station completes existing connector production", "[workshop][crafting][power]") {
    Diagnostics diagnostics;
    auto loaded = LoadContentPack(CATACLYSMX_CONTENT_PACK, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str()); REQUIRE(loaded);
    auto content = eastl::move(*loaded);
    const auto type = content.FindObject("scrap_press");
    auto& definition = const_cast<ObjectDefinition&>(content.GetObject(type));
    definition.behavior = ObjectBehaviorKind::None; definition.machine = {};
    definition.toolTraits = {};
    REQUIRE(definition.toolTraits.PushBack({content.FindToolTrait("soldering"), 1}));
    definition.craftingEnergyPerWork = 10000;
    REQUIRE(ValidObjectDefinition(definition));
    World world(content);
    Take(world.RequestChunks({{0, 0}, 0}, LoadMode::active)); REQUIRE(world.ReconcileChunks());
    for (int z = 1; z <= 4; ++z) for (int x = 1; x <= 4; ++x) {
        const TileCoord position{x, z};
        if (const auto object = world.ObjectAt(position); object.value) { REQUIRE(world.DestroyObject(object)); }
        if (const auto actor = world.ActorAt(position); actor.value) { REQUIRE(world.DestroyActor(actor)); }
        REQUIRE(world.SetTile(position, {content.FindTerrain("floor")}));
    }
    const auto actor = Take(world.CreateActor({2, 2}, true));
    const auto station = Take(world.CreateObject(type, {3, 2}));
    Take(world.Power().ExtendBounds({{3, 2}, {3, 3}}));
    const auto box = Take(world.CreateObject(content.FindObject("grid_battery_station"), {3, 3}));
    const auto battery = Take(world.CreateItem(content.FindItem("workshop_battery_pack"), ItemLocation::Stored(box)));
    const auto power = FindItemCapability(content.GetItem(content.FindItem("workshop_battery_pack")), "power");
    REQUIRE(world.SetItemEnergy(battery, power, 100)); world.Power().SyncObject(box);
    Take(world.CreateItem(content.FindItem("copper_wire"), ItemLocation::Carried(actor)));
    Take(world.CreateItem(content.FindItem("solder_wire"), ItemLocation::Carried(actor)));
    const auto job = Take(world.StartCrafting(actor, content.FindCraftingRecipe("solder_electrical_connectors")));
    REQUIRE(job.outcome == CraftingOutcome::Started); CHECK(world.GetItemEnergy(battery, power)->energy == 0);
    world.ElapseTicks(100); world.ProcessCraftingEvents();
    CHECK(world.InspectCrafting(job.unfinished)->completedWork == 100);
    CHECK(world.InspectCrafting(job.unfinished)->status == CraftingStatus::InsufficientEnergy);
    CHECK(world.StationWorker(station) == 0);
    REQUIRE(world.SetItemEnergy(battery, power, 100));
    REQUIRE(Take(world.ResumeCrafting(actor, job.unfinished)).outcome == CraftingOutcome::Resumed);
    world.ElapseTicks(100); world.ProcessCraftingEvents();
    CHECK_FALSE(world.GetItem(job.unfinished)); CHECK(world.GetItemEnergy(battery, power)->energy == 0);
    unsigned connectors = 0;
    for (auto item : world.GetActor(actor)->inventory) {
        connectors += world.GetItem(item)->type == content.FindItem("electrical_connector");
    }
    CHECK(connectors == 10); REQUIRE(world.CheckInvariants());
}
}
