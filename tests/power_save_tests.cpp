#include "core/save_database.h"
#include "power/power_types.h"
#include "content_fixture.h"

#include <catch2/catch_test_macros.hpp>
#include <sqlite3.h>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>

namespace {
using namespace cataclysmx;

const ContentCatalog& PowerContent() {
    static const ContentCatalog content = [] {
        const ContentSource source{"power-save", R"({format_version:1,definitions:[
          {type:'terrain',id:'floor',name:'Floor',movement:{passable:true},blocks_sight:false,
           appearance:{symbol:'.',foreground:'white'}},
          {type:'item',id:'part',name:'Part',mass_g:1,volume_ml:1}
        ],objects:[
          {id:'battery',name:'Battery station',behavior:'grid_battery'},
          {id:'solar',name:'Solar installation',behavior:'solar',solar:{peak_output_per_tick:1}},
          {id:'generator',name:'Generator',behavior:'generator',generator:{fuel_item:'part',
           fuel_capacity:6000,energy_per_fuel:2000,output_per_tick:4}},
          {id:'machine',name:'Machine',behavior:'crafting_machine',machine:{input:'part',input_count:1,
           output:'part',output_count:1,duration_ticks:100,energy_capacity:0,energy_per_tick:2,grid_powered:true}}
        ]})"};
        auto result = LoadContent({&source, 1});
        REQUIRE(result);
        return eastl::move(*result);
    }();
    return content;
}

struct PowerSaveFile {
    std::filesystem::path directory;
    std::string path;
    PowerSaveFile() {
        static std::atomic<unsigned> sequence{0};
        std::error_code error;
        directory = std::filesystem::temp_directory_path(error);
        REQUIRE_FALSE(error);
        directory /= "cataclysmx-power-save-"
            + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
            + "-" + std::to_string(sequence.fetch_add(1));
        REQUIRE(std::filesystem::create_directory(directory, error));
        path = (directory / "world.sqlite").string();
    }
    ~PowerSaveFile() {
        std::error_code error;
        std::filesystem::remove_all(directory, error);
    }
};

struct PowerRawDatabase {
    sqlite3* db = nullptr;
    explicit PowerRawDatabase(const char* path) {
        REQUIRE(sqlite3_open_v2(path, &db, SQLITE_OPEN_READWRITE, nullptr) == SQLITE_OK);
    }
    ~PowerRawDatabase() { sqlite3_close(db); }
    void Exec(const char* sql) { REQUIRE(sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK); }
    int Version() {
        sqlite3_stmt* statement = nullptr;
        REQUIRE(sqlite3_prepare_v2(db, "SELECT format_version FROM metadata", -1, &statement, nullptr) == SQLITE_OK);
        REQUIRE(sqlite3_step(statement) == SQLITE_ROW);
        const int result = sqlite3_column_int(statement, 0);
        REQUIRE(sqlite3_finalize(statement) == SQLITE_OK);
        return result;
    }
};

SaveSnapshot PowerSnapshot(bool solar = false) {
    SaveSnapshot snapshot;
    snapshot.seed = 123;
    snapshot.nextId = 5;
    snapshot.session.tick = 250;
    auto& power = snapshot.power;
    power.nextGridId = UINT64_MAX;
    power.mainsCutoffTick = SeededMainsCutoff(snapshot.seed);
    power.settings = {12, 9};
    SavedPowerGrid grid;
    grid.id = {UINT64_MAX - 1};
    grid.regions.push_back({{1600, -1600, -1}, {1615, -1585, 1}});
    grid.regions.push_back({{1616, -1600, 0}, {1620, -1585, 0}});
    grid.mainsConnected = true;
    grid.fairnessCursor = 3;
    grid.processedInterval = 200;
    grid.intervalProcessed = true;
    grid.mainsChargeInterval = 200;
    grid.mainsChargeProcessed = true;
    power.grids.push_back(grid);
    power.devices.push_back({1, {1600, -1600, 0}, PowerDeviceKind::Battery, false, 0, false});
    power.devices.push_back({2, {1601, -1600, 0}, PowerDeviceKind::Generator, true, 100, true});
    power.devices.push_back({3, {1602, -1600, 0}, PowerDeviceKind::Machine, true, 0, false});
    if (solar) {
        snapshot.nextId = 6;
        power.devices.push_back({5, {1603, -1600, 0}, PowerDeviceKind::Solar, true, 200, true});
    }
    power.generatedBindings.push_back({{1600, -1600, 0}, grid.id});
    return snapshot;
}

void PopulatePowerSave(const char* path, bool solar = false) {
    const auto snapshot = PowerSnapshot(solar);
    REQUIRE(ValidPowerState(snapshot.power, snapshot.session.tick));
    ChunkArchive chunk{};
    chunk.position = {100, -100, 0};
    chunk.tiles.fill(Tile{PowerContent().FindTerrain("floor"), 0});
    for (const auto& device : snapshot.power.devices) {
        SavedObject object{device.object, device.position, true, 0};
        switch (device.kind) {
        case PowerDeviceKind::Battery: object.type = PowerContent().FindObject("battery"); break;
        case PowerDeviceKind::Generator:
            object.type = PowerContent().FindObject("generator");
            object.state.SetUnsigned("version", 2);
            object.state.SetBoolean("run_requested", true);
            object.state.SetUnsigned("energy", 1777);
            object.state.SetUnsigned("generator_output_remaining", 137);
            object.state.SetUnsigned("generator_interval_start", 200);
            object.state.SetBoolean("generator_interval_initialized", true);
            break;
        case PowerDeviceKind::Machine:
            object.type = PowerContent().FindObject("machine");
            object.state.SetUnsigned("version", 2);
            object.state.SetBoolean("run_requested", true);
            object.state.SetBoolean("has_job", true);
            object.state.SetUnsigned("remaining_work", 73);
            object.state.SetUnsigned("funded_ticks", 23);
            break;
        case PowerDeviceKind::Solar:
            object.type = PowerContent().FindObject("solar");
            object.state.SetUnsigned("version", 1);
            object.state.SetBoolean("run_requested", true);
            object.state.SetBoolean("interval_initialized", true);
            object.state.SetUnsigned("interval_start", 200);
            object.state.SetUnsigned("output_remaining", SolarIntervalAllowance(200, 1) - 3);
            break;
        }
        chunk.objects.push_back(eastl::move(object));
    }
    SaveDatabase database(PowerContent());
    REQUIRE(database.Create(path));
    REQUIRE(database.Begin());
    REQUIRE(database.WriteChunk(chunk));
    REQUIRE(database.WriteSnapshot(snapshot));
    REQUIRE(database.Commit());
}

TEST_CASE("Power snapshot retains offscreen holds and consumed interval ledgers", "[save][power][database]") {
    PowerSaveFile file;
    PopulatePowerSave(file.path.c_str());
    const auto expected = PowerSnapshot();
    {
        SaveDatabase database(PowerContent());
        auto loaded = database.Open(file.path.c_str());
        REQUIRE(loaded);
        CHECK(loaded->power == expected.power);
        CHECK(loaded->session.tick == expected.session.tick);
        const auto chunk = database.ReadChunk({100, -100, 0}, loaded->nextId);
        REQUIRE(chunk);
        REQUIRE(*chunk);
        CHECK((**chunk).objects[1].state.GetUnsigned("energy") == 1777);
        CHECK((**chunk).objects[1].state.GetUnsigned("generator_output_remaining") == 137);
        CHECK((**chunk).objects[2].state.GetUnsigned("remaining_work") == 73);
        CHECK((**chunk).objects[2].state.GetUnsigned("funded_ticks") == 23);
        REQUIRE(database.Begin());
        auto changed = *loaded;
        changed.power.devices[1].workshopHold = false;
        changed.power.grids[0].fairnessCursor = 2;
        REQUIRE(database.WriteSnapshot(changed));
        database.Rollback();
    }
    SaveDatabase reopened(PowerContent());
    const auto loaded = reopened.Open(file.path.c_str());
    REQUIRE(loaded);
    CHECK(loaded->power == expected.power);
    PowerRawDatabase raw(file.path.c_str());
    CHECK(raw.Version() == save_format_version);
}

TEST_CASE("Legacy power checkpoint is rejected", "[save][power][migration]") {
    PowerSaveFile file;
    PopulatePowerSave(file.path.c_str());
    {
        PowerRawDatabase raw(file.path.c_str());
        raw.Exec("UPDATE world_state SET payload=json_remove(payload,'$.power'); UPDATE metadata SET format_version=14");
        SECTION("faithful v14 without grid equipment") {
            raw.Exec("UPDATE chunks SET payload=json_set(payload,'$.objects',json('[]'))");
        }
        SECTION("current equipment relabeled as legacy") {}
    }
    SaveDatabase database(PowerContent());
    CHECK(database.Open(file.path.c_str()) == eastl::unexpected(SaveError::UnsupportedVersion));
    PowerRawDatabase raw(file.path.c_str());
    CHECK(raw.Version() == 14);
}

TEST_CASE("Power decoding rejects malformed fields and invalid relationships", "[save][power][validation]") {
    PowerSaveFile file;
    PopulatePowerSave(file.path.c_str());
    PowerRawDatabase raw(file.path.c_str());
    SECTION("missing power") { raw.Exec("UPDATE world_state SET payload=json_remove(payload,'$.power')"); }
    SECTION("unknown power field") { raw.Exec("UPDATE world_state SET payload=json_set(payload,'$.power.fuel','99')"); }
    SECTION("duplicate field") {
        raw.Exec("UPDATE world_state SET payload=replace(payload,'\"next_grid_id\":','\"version\":1,\"next_grid_id\":')");
    }
    SECTION("numeric counter") { raw.Exec("UPDATE world_state SET payload=json_set(payload,'$.power.next_grid_id',5)"); }
    SECTION("noncanonical counter") { raw.Exec("UPDATE world_state SET payload=json_set(payload,'$.power.next_grid_id','05')"); }
    SECTION("overflow counter") { raw.Exec("UPDATE world_state SET payload=json_set(payload,'$.power.next_grid_id','18446744073709551616')"); }
    SECTION("unsupported envelope") { raw.Exec("UPDATE world_state SET payload=json_set(payload,'$.power.version',3)"); }
    SECTION("invalid settings") { raw.Exec("UPDATE world_state SET payload=json_set(payload,'$.power.settings.max_wiring_width',65)"); }
    SECTION("invalid cutoff") { raw.Exec("UPDATE world_state SET payload=json_set(payload,'$.power.mains_cutoff_tick','0')"); }
    SECTION("nonboolean hold") { raw.Exec("UPDATE world_state SET payload=json_set(payload,'$.power.devices[1].workshop_hold',1)"); }
    SECTION("unknown device kind") { raw.Exec("UPDATE world_state SET payload=json_set(payload,'$.power.devices[0].kind',256)"); }
    SECTION("duplicate device") { raw.Exec("UPDATE world_state SET payload=json_set(payload,'$.power.devices[1].object','1')"); }
    SECTION("dangling device") { raw.Exec("UPDATE world_state SET payload=json_set(payload,'$.power.devices[2].object','4')"); }
    SECTION("device outside next id") { raw.Exec("UPDATE world_state SET payload=json_set(payload,'$.power.devices[2].object','5')"); }
    SECTION("wrong object position") { raw.Exec("UPDATE world_state SET payload=json_set(payload,'$.power.devices[2].position.x',1603)"); }
    SECTION("wrong equipment role") { raw.Exec("UPDATE world_state SET payload=json_set(payload,'$.power.devices[0].kind',1)"); }
    SECTION("missing remote generator index") { raw.Exec("UPDATE world_state SET payload=json_remove(payload,'$.power.devices[1]')"); }
    SECTION("missing remote battery index") { raw.Exec("UPDATE world_state SET payload=json_remove(payload,'$.power.devices[0]')"); }
    SECTION("missing remote machine index") { raw.Exec("UPDATE world_state SET payload=json_remove(payload,'$.power.devices[2]')"); }
    SECTION("index points to generic object") { raw.Exec("UPDATE chunks SET payload=json_set(payload,'$.objects[0].type','')"); }
    SECTION("generator hold cleared") { raw.Exec("UPDATE world_state SET payload=json_set(payload,'$.power.devices[1].workshop_hold',json('false'))"); }
    SECTION("machine hold cleared") { raw.Exec("UPDATE world_state SET payload=json_set(payload,'$.power.devices[2].workshop_hold',json('false'))"); }
    SECTION("generator run intent changed") {
        raw.Exec("UPDATE chunks SET payload=json_set(payload,'$.objects[1].state.run_requested',json('false'),"
            "'$.objects[1].state.generator_output_remaining',0)");
    }
    SECTION("machine run intent changed") { raw.Exec("UPDATE chunks SET payload=json_set(payload,'$.objects[2].state.run_requested',json('false'))"); }
    SECTION("generator disabled behind hold") { raw.Exec("UPDATE chunks SET payload=json_set(payload,'$.objects[1].enabled',json('false'))"); }
    SECTION("machine broken behind hold") { raw.Exec("UPDATE chunks SET payload=json_set(payload,'$.objects[2].broken',json('true'))"); }
    SECTION("future generator budget interval") { raw.Exec("UPDATE chunks SET payload=json_set(payload,'$.objects[1].state.generator_interval_start',300)"); }
    SECTION("charge marker newer than source budget") {
        raw.Exec("UPDATE chunks SET payload=json_set(payload,'$.objects[1].state.generator_interval_start',0)");
    }
    SECTION("unaligned generator budget interval") { raw.Exec("UPDATE chunks SET payload=json_set(payload,'$.objects[1].state.generator_interval_start',201)"); }
    SECTION("invalid generator budget") { raw.Exec("UPDATE chunks SET payload=json_set(payload,'$.objects[1].state.generator_output_remaining',401)"); }
    SECTION("battery hold") { raw.Exec("UPDATE world_state SET payload=json_set(payload,'$.power.devices[0].workshop_hold',json('true'))"); }
    SECTION("battery charging ledger") { raw.Exec("UPDATE world_state SET payload=json_set(payload,'$.power.devices[0].charge_processed',json('true'))"); }
    SECTION("grid machine charging ledger") { raw.Exec("UPDATE world_state SET payload=json_set(payload,'$.power.devices[2].charge_processed',json('true'))"); }
    SECTION("future interval") { raw.Exec("UPDATE world_state SET payload=json_set(payload,'$.power.grids[0].processed_interval','300')"); }
    SECTION("unaligned charge ledger") { raw.Exec("UPDATE world_state SET payload=json_set(payload,'$.power.devices[1].charge_interval','201')"); }
    SECTION("uninitialized nonzero ledger") { raw.Exec("UPDATE world_state SET payload=json_set(payload,'$.power.grids[0].mains_charge_processed',json('false'))"); }
    SECTION("reversed geometry") { raw.Exec("UPDATE world_state SET payload=json_set(payload,'$.power.grids[0].regions[0].min.x',1620)"); }
    SECTION("out of bounds geometry") { raw.Exec("UPDATE world_state SET payload=json_set(payload,'$.power.grids[0].regions[0].max.y',11)"); }
    SECTION("dangling binding") { raw.Exec("UPDATE world_state SET payload=json_set(payload,'$.power.generated_bindings[0].grid','1')"); }
    SECTION("duplicate binding") { raw.Exec("UPDATE world_state SET payload=json_insert(payload,'$.power.generated_bindings[#]',json_extract(payload,'$.power.generated_bindings[0]'))"); }
    SECTION("duplicate grid") { raw.Exec("UPDATE world_state SET payload=json_insert(payload,'$.power.grids[#]',json_extract(payload,'$.power.grids[0]'))"); }
    SECTION("overlapping distinct grids") {
        raw.Exec("UPDATE world_state SET payload=json_set(payload,'$.power.grids[0].id','1','$.power.generated_bindings[0].grid','1');"
            "UPDATE world_state SET payload=json_insert(payload,'$.power.grids[#]',json_set(json_extract(payload,'$.power.grids[0]'),'$.id','2'))");
    }
    SaveDatabase database(PowerContent());
    CHECK(database.Open(file.path.c_str()) == eastl::unexpected(SaveError::InvalidData));
}

TEST_CASE("Invalid power checkpoint rolls back earlier writes", "[save][power][database]") {
    PowerSaveFile file;
    PopulatePowerSave(file.path.c_str());
    SaveDatabase database(PowerContent());
    auto loaded = database.Open(file.path.c_str());
    REQUIRE(loaded);
    REQUIRE(database.Begin());
    const auto chunk = database.ReadChunk({100, -100, 0}, loaded->nextId);
    REQUIRE(chunk);
    REQUIRE(*chunk);
    auto changedChunk = **chunk;
    changedChunk.tiles[0].damage = 41;
    REQUIRE(database.WriteChunk(changedChunk));
    auto changed = *loaded;
    changed.power.settings.maxWiringDepth = 0;
    CHECK(database.WriteSnapshot(changed) == eastl::unexpected(SaveError::InvalidData));
    CHECK(database.Commit() == eastl::unexpected(SaveError::InvalidState));
    REQUIRE(database.Begin());
    changed = *loaded;
    SECTION("dangling object") { changed.power.devices[2].object = 4; }
    SECTION("role mismatch") { changed.power.devices[0].kind = PowerDeviceKind::Generator; }
    SECTION("lost run intent hold") { changed.power.devices[1].workshopHold = false; }
    SECTION("omitted remote equipment") { changed.power.devices.erase(changed.power.devices.begin() + 1); }
    SECTION("changed archived run intent") {
        changedChunk.objects[1].state.SetBoolean("run_requested", false);
        changedChunk.objects[1].state.SetUnsigned("generator_output_remaining", 0);
    }
    SECTION("future archived generator allowance") {
        changedChunk.objects[1].state.SetUnsigned("generator_interval_start", 300);
    }
    REQUIRE(database.WriteChunk(changedChunk));
    REQUIRE(database.WriteSnapshot(changed));
    CHECK(database.Commit() == eastl::unexpected(SaveError::InvalidData));
    SaveDatabase reopened(PowerContent());
    auto restored = reopened.Open(file.path.c_str());
    REQUIRE(restored);
    CHECK(restored->power == loaded->power);
    const auto restoredChunk = reopened.ReadChunk({100, -100, 0}, restored->nextId);
    REQUIRE(restoredChunk);
    REQUIRE(*restoredChunk);
    CHECK((**restoredChunk).tiles[0].damage == 0);
}

TEST_CASE("Solar allowance and offscreen intent survive durable checkpoints", "[save][power][solar]") {
    PowerSaveFile file;
    PopulatePowerSave(file.path.c_str(), true);
    SaveDatabase database(PowerContent());
    const auto loaded = database.Open(file.path.c_str());
    REQUIRE(loaded);
    CHECK(loaded->power == PowerSnapshot(true).power);
    const auto chunk = database.ReadChunk({100, -100, 0}, loaded->nextId);
    REQUIRE(chunk);
    REQUIRE(*chunk);
    const auto& state = (**chunk).objects[3].state;
    CHECK(state.GetUnsigned("version") == 1);
    CHECK(state.GetBoolean("run_requested") == true);
    CHECK(state.GetBoolean("interval_initialized") == true);
    CHECK(state.GetUnsigned("interval_start") == 200);
    CHECK(state.GetUnsigned("output_remaining") == SolarIntervalAllowance(200, 1) - 3);
    CHECK(state.Size() == 5);
    REQUIRE(database.Begin());
    REQUIRE(database.WriteSnapshot(*loaded));
    REQUIRE(database.Commit());
}

TEST_CASE("Solar archive and index corruption is rejected before synchronization", "[save][power][solar][validation]") {
    PowerSaveFile file;
    PopulatePowerSave(file.path.c_str(), true);
    PowerRawDatabase raw(file.path.c_str());
    SECTION("legacy power cannot contain solar") {
        raw.Exec("UPDATE world_state SET payload=json_set(payload,'$.power.version',1)");
    }
    SECTION("missing index") { raw.Exec("UPDATE world_state SET payload=json_remove(payload,'$.power.devices[3]')"); }
    SECTION("wrong role") { raw.Exec("UPDATE world_state SET payload=json_set(payload,'$.power.devices[3].kind',1)"); }
    SECTION("wrong position") { raw.Exec("UPDATE world_state SET payload=json_set(payload,'$.power.devices[3].position.x',1604)"); }
    SECTION("lost hold") { raw.Exec("UPDATE world_state SET payload=json_set(payload,'$.power.devices[3].workshop_hold',json('false'))"); }
    SECTION("future interval") { raw.Exec("UPDATE chunks SET payload=json_set(payload,'$.objects[3].state.interval_start',300)"); }
    SECTION("unaligned interval") { raw.Exec("UPDATE chunks SET payload=json_set(payload,'$.objects[3].state.interval_start',201)"); }
    SECTION("excess daylight allowance") { raw.Exec("UPDATE chunks SET payload=json_set(payload,'$.objects[3].state.output_remaining',100)"); }
    SECTION("uninitialized charged source") {
        raw.Exec("UPDATE chunks SET payload=json_set(payload,'$.objects[3].state.interval_initialized',json('false'),"
            "'$.objects[3].state.interval_start',0,'$.objects[3].state.output_remaining',0)");
    }
    SECTION("charging newer than source interval") {
        raw.Exec("UPDATE chunks SET payload=json_set(payload,'$.objects[3].state.interval_start',100)");
    }
    SECTION("stopped with allowance") { raw.Exec("UPDATE chunks SET payload=json_set(payload,'$.objects[3].state.run_requested',json('false'))"); }
    SECTION("fuel is not a solar state field") { raw.Exec("UPDATE chunks SET payload=json_set(payload,'$.objects[3].state.energy',100)"); }
    SaveDatabase database(PowerContent());
    CHECK(database.Open(file.path.c_str()) == eastl::unexpected(SaveError::InvalidData));
}

TEST_CASE("Format sixteen power checkpoint is rejected", "[save][power][migration]") {
    PowerSaveFile file;
    PopulatePowerSave(file.path.c_str());
    {
        PowerRawDatabase raw(file.path.c_str());
        raw.Exec("UPDATE metadata SET format_version=16;"
            "UPDATE world_state SET payload=json_set(payload,'$.power.version',1)");
    }
    SaveDatabase database(PowerContent());
    CHECK(database.Open(file.path.c_str()) == eastl::unexpected(SaveError::UnsupportedVersion));
    PowerRawDatabase raw(file.path.c_str());
    CHECK(raw.Version() == 16);
}
}
