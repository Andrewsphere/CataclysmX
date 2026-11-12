#include "core/save_database.h"
#include "content_fixture.h"
#include "actors/ai_controller.h"
#include "objects/object_behavior.h"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <EASTL/utility.h>
#include <sqlite3.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace {
using namespace cataclysmx;

struct SaveFile {
    std::filesystem::path directory;
    std::string path;
    SaveFile() {
        static std::atomic<unsigned> sequence{0};
        std::error_code error;
        directory = std::filesystem::temp_directory_path(error);
        REQUIRE_FALSE(error);
        directory /= "cataclysmx-save-test-"
            + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
            + "-" + std::to_string(sequence.fetch_add(1));
        REQUIRE(std::filesystem::create_directory(directory, error));
        REQUIRE_FALSE(error);
        path = (directory / "world.sqlite").string();
    }
    ~SaveFile() {
        std::error_code error;
        std::filesystem::remove_all(directory, error);
    }
};

struct RawDatabase {
    sqlite3* db = nullptr;
    explicit RawDatabase(const char* path) {
        REQUIRE(sqlite3_open_v2(path, &db, SQLITE_OPEN_READWRITE, nullptr) == SQLITE_OK);
    }
    ~RawDatabase() { sqlite3_close(db); }
    void Exec(const char* sql) { REQUIRE(sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK); }
    std::string Text(const char* sql) {
        sqlite3_stmt* statement = nullptr;
        REQUIRE(sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) == SQLITE_OK);
        REQUIRE(sqlite3_step(statement) == SQLITE_ROW);
        const char* value = reinterpret_cast<const char*>(sqlite3_column_text(statement, 0));
        REQUIRE(value);
        std::string result(value, static_cast<std::size_t>(sqlite3_column_bytes(statement, 0)));
        REQUIRE(sqlite3_finalize(statement) == SQLITE_OK);
        return result;
    }
    void SetText(const char* sql, const std::string& text) {
        sqlite3_stmt* statement = nullptr;
        REQUIRE(sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) == SQLITE_OK);
        REQUIRE(sqlite3_bind_text(statement, 1, text.data(), static_cast<int>(text.size()), SQLITE_TRANSIENT) == SQLITE_OK);
        REQUIRE(sqlite3_step(statement) == SQLITE_DONE);
        REQUIRE(sqlite3_finalize(statement) == SQLITE_OK);
    }
};

ChunkArchive SampleChunk(ChunkCoord position = {0, 0}) {
    ChunkArchive chunk{};
    chunk.tiles.fill(Tile{TestTerrain("floor"), 0});
    chunk.position = position;
    const TileCoord tile{position.x * chunk_size, position.z * chunk_size, position.y};
    chunk.tiles[255] = {TestTerrain("closed_door"), 255};
    chunk.actors.push_back({1, tile, true, UINT64_MAX, 173, UINT64_MAX});
    chunk.objects.push_back({2, tile, true, UINT64_MAX - 1, 217, 176});
    chunk.items.push_back({3, TestItem("backpack"), LocationKind::actor, {}, 1});
    chunk.items.push_back({4, TestItem("backpack"), LocationKind::item, {}, 3});
    chunk.items.push_back({5, TestItem("bandage"), LocationKind::item, {}, 4});
    chunk.items.push_back({6, TestItem("rock"), LocationKind::object, {}, 2});
    chunk.items.push_back({7, TestItem("rock"), LocationKind::ground, tile, 0});
    return chunk;
}

SaveSnapshot SampleSnapshot() {
    SaveSnapshot snapshot;
    snapshot.seed = UINT32_MAX;
    snapshot.nextId = 8;
    snapshot.session.player = 1;
    snapshot.session.tick = UINT64_MAX;
    snapshot.session.rngState = UINT32_MAX;
    snapshot.session.autosaveInterval = UINT64_MAX - 1;
    snapshot.session.pacing = {37, true, false};
    return snapshot;
}

void Populate(const char* path) {
    SaveDatabase database(TestContent());
    REQUIRE(database.Create(path));
    REQUIRE(database.Begin());
    REQUIRE(database.WriteChunk(SampleChunk()));
    REQUIRE(database.WriteSnapshot(SampleSnapshot()));
    REQUIRE(database.Commit());
}

void Replace(std::string& text, const char* before, const char* after) {
    const auto position = text.find(before);
    REQUIRE(position != std::string::npos);
    text.replace(position, std::char_traits<char>::length(before), after);
}

void StripV9ChunkFields(RawDatabase& raw) {
    raw.Exec("UPDATE chunks SET payload=json_set(payload,"
        "'$.actors',json((SELECT json_group_array(json_remove(value,'$.controller','$.combat')) FROM json_each(chunks.payload,'$.actors'))),"
        "'$.objects',json((SELECT json_group_array(json_remove(value,'$.type','$.state')) FROM json_each(chunks.payload,'$.objects'))))");
}

void PopulateV7(const char* path) {
    {
        SaveDatabase database(TestContent());
        REQUIRE(database.Create(path));
        auto snapshot = SampleSnapshot();
        MemoryChunk memory{};
        memory.terrain[0] = TestTerrain("floor");
        snapshot.memories.push_back(PlayerMemory{1, {memory}});
        REQUIRE(database.Begin());
        REQUIRE(database.WriteChunk(SampleChunk()));
        REQUIRE(database.WriteSnapshot(snapshot));
        REQUIRE(database.Commit());
    }
    RawDatabase raw(path);
    auto payload = raw.Text("SELECT payload FROM world_state");
    Replace(payload, ",\"circle_residency\":null", "");
    Replace(payload, "\"active_radius\":1", "\"active_radius\":0");
    Replace(payload, "\"resident_radius\":5", "\"resident_radius\":2");
    raw.SetText("UPDATE world_state SET payload=?1", payload);
    StripV9ChunkFields(raw);
    raw.Exec("UPDATE metadata SET format_version=7");
}

ContentCatalog SaveTestCatalog(eastl::span<const ContentSource> sources) {
    Diagnostics diagnostics;
    auto loaded = LoadContent(sources, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str());
    REQUIRE(loaded);
    return eastl::move(*loaded);
}

TEST_CASE("Save terrain names survive catalog renumbering and missing content fails with context", "[save][database][content]") {
    const ContentSource sources[] = {
        {"floor.json5", R"({format_version:1, definitions:[{
            type:'terrain', id:'floor', name:'floor', movement:{passable:true}, blocks_sight:false,
            appearance:{symbol:'.', foreground:'white'}
        }]})"},
        {"moss.json5", R"({format_version:1, definitions:[{
            type:'terrain', id:'moss', name:'soft moss', movement:{passable:true}, blocks_sight:false,
            appearance:{symbol:',', foreground:'green'}
        }]})"},
        {"ash.json5", R"({format_version:1, definitions:[{
            type:'terrain', id:'ash', name:'ash', movement:{passable:true}, blocks_sight:false,
            appearance:{symbol:'.', foreground:'dark_gray'}
        }]})"}
    };
    const auto originalContent = SaveTestCatalog({sources, 2});
    const auto reorderedContent = SaveTestCatalog({sources, 3});
    const auto moss = originalContent.FindTerrain("moss");
    const auto reorderedMoss = reorderedContent.FindTerrain("moss");
    REQUIRE(moss != TerrainId{});
    REQUIRE(reorderedMoss != TerrainId{});
    REQUIRE(moss != reorderedMoss); // Adding an earlier sorted ID changes runtime numbering.
    REQUIRE(originalContent.FindTerrain("floor") != reorderedContent.FindTerrain("floor"));
    SaveFile file;
    ChunkArchive chunk{};
    chunk.position = {-3, 2};
    chunk.tiles.fill(Tile{originalContent.FindTerrain("floor"), 0});
    chunk.tiles[37] = {moss, 23};
    {
        SaveDatabase database(originalContent);
        REQUIRE(database.Create(file.path.c_str()));
        REQUIRE(database.Begin());
        REQUIRE(database.WriteChunk(chunk));
        REQUIRE(database.WriteSnapshot(SaveSnapshot{}));
        REQUIRE(database.Commit());
    }
    {
        RawDatabase raw(file.path.c_str());
        CHECK(raw.Text("SELECT format_version FROM metadata") == "10");
        const auto payload = raw.Text("SELECT payload FROM chunks");
        CHECK(payload.find("\"terrain\":\"moss\"") != std::string::npos);
        CHECK(payload.find("\"terrain\":\"floor\"") != std::string::npos);
    }
    SECTION("reload resolves every tile using the supplied catalog") {
        SaveDatabase database(reorderedContent);
        const auto snapshot = database.Open(file.path.c_str());
        REQUIRE(snapshot);
        const auto loaded = database.ReadChunk(chunk.position, snapshot->nextId);
        REQUIRE(loaded);
        REQUIRE(*loaded);
        for (std::size_t index = 0; index < chunk.tiles.size(); ++index) {
            CHECK((**loaded).tiles[index].terrain == (index == 37
                ? reorderedMoss : reorderedContent.FindTerrain("floor")));
            CHECK((**loaded).tiles[index].damage == chunk.tiles[index].damage);
        }
        REQUIRE(database.Begin());
        REQUIRE(database.WriteChunk(**loaded));
        REQUIRE(database.Commit());
    }
    SECTION("missing terrain preserves the diagnostic and identifies chunk and tile") {
        const auto missingContent = SaveTestCatalog({sources, 1});
        SaveDatabase database(missingContent);
        Diagnostics diagnostics;
        diagnostics.Add("Existing caller context");
        CHECK(database.Open(file.path.c_str(), &diagnostics) == eastl::unexpected(SaveError::InvalidData));
        REQUIRE(diagnostics.Frames().size() >= 3);
        CHECK(diagnostics.Frames()[0].message == "Existing caller context");
        const auto& terrainFrame = diagnostics.Frames()[1];
        CHECK(terrainFrame.message.find("moss") != String::npos);
        REQUIRE(terrainFrame.location);
        CHECK(terrainFrame.location->path == "chunks[-3,2,0].tiles[37].terrain");
        CHECK(diagnostics.Frames().back().message == "Opening save: validating saved world");
        CHECK(database.Begin() == eastl::unexpected(SaveError::NotOpen));
    }
}

TEST_CASE("Save database validates terrain again on chunk reads and checkpoint commits", "[save][database][content]") {
    SaveFile file;
    Populate(file.path.c_str());
    SaveDatabase database(TestContent());
    REQUIRE(database.Open(file.path.c_str()));
    {
        RawDatabase raw(file.path.c_str());
        auto payload = raw.Text("SELECT payload FROM chunks");
        Replace(payload, "\"terrain\":\"floor\"", "\"terrain\":\"removed_terrain\"");
        raw.SetText("UPDATE chunks SET payload=?1", payload);
    }
    Diagnostics diagnostics;
    SECTION("reading a previously valid row") {
        CHECK(database.ReadChunk({0, 0}, 8, &diagnostics) == eastl::unexpected(SaveError::InvalidData));
    }
    SECTION("committing scans unchanged chunks") {
        REQUIRE(database.Begin());
        auto snapshot = SampleSnapshot();
        snapshot.session.tick = 12;
        REQUIRE(database.WriteSnapshot(snapshot));
        CHECK(database.Commit(&diagnostics) == eastl::unexpected(SaveError::InvalidData));
        CHECK(database.Commit() == eastl::unexpected(SaveError::InvalidState));
        RawDatabase raw(file.path.c_str());
        CHECK(raw.Text("SELECT payload FROM world_state").find("\"tick\":\"18446744073709551615\"")
            != std::string::npos);
    }
    REQUIRE_FALSE(diagnostics.Empty());
    CHECK(diagnostics.Frames()[0].message.find("removed_terrain") != String::npos);
    REQUIRE(diagnostics.Frames()[0].location);
    CHECK(diagnostics.Frames()[0].location->path == "chunks[0,0,0].tiles[0].terrain");
}

TEST_CASE("Save database refuses existing files and never creates on Open", "[save][database]") {
    SaveFile file;
    SaveDatabase database(TestContent());
    CHECK(database.Open(file.path.c_str()) == eastl::unexpected(SaveError::Io));
    CHECK_FALSE(std::filesystem::exists(file.path));
    {
        std::ofstream empty(file.path, std::ios::binary);
        REQUIRE(empty.is_open());
        empty.close();
        REQUIRE(empty.good());
    }
    CHECK(database.Create(file.path.c_str()) == eastl::unexpected(SaveError::AlreadyExists));
    CHECK(std::filesystem::file_size(file.path) == 0);
    CHECK(database.Open(file.path.c_str()) == eastl::unexpected(SaveError::InvalidData));
    CHECK(database.Begin() == eastl::unexpected(SaveError::NotOpen));
    CHECK(database.Create(file.directory.string().c_str()) == eastl::unexpected(SaveError::AlreadyExists));
}

TEST_CASE("Save database stores nested standard JSON and round trips full width values", "[save][database]") {
    SaveFile file;
    auto chunk = SampleChunk({-1, -1});
    chunk.items.back().id = UINT64_MAX - 1;
    auto snapshot = SampleSnapshot();
    snapshot.nextId = UINT64_MAX;
    snapshot.session.playerPosition = {-16, -16};
    {
        SaveDatabase database(TestContent());
        REQUIRE(database.Create(file.path.c_str()));
        CHECK(database.WriteChunk(chunk) == eastl::unexpected(SaveError::InvalidState));
        REQUIRE(database.Begin());
        REQUIRE(database.WriteChunk(chunk));
        REQUIRE(database.WriteSnapshot(snapshot));
        REQUIRE(database.Commit());
    }
    {
        RawDatabase raw(file.path.c_str());
        const auto payload = raw.Text("SELECT payload FROM chunks");
        CHECK(payload.find("\"inventory\":[{\"id\":\"3\"") != std::string::npos);
        CHECK(payload.find("\"contents\":[{\"id\":\"4\"") != std::string::npos);
        CHECK(payload.find("\"ground_items\":[{\"id\":\"18446744073709551614\"") != std::string::npos);
        CHECK(payload.find("\"carry_capacity_g\":\"100000\"") != std::string::npos);
        CHECK(payload.find("\"type\":\"backpack\",\"state\":{},\"installation\":\"\"") != std::string::npos);
        CHECK(payload.find("\"position\":{\"x\":-16,\"z\":-16,\"y\":0}") != std::string::npos);
        CHECK(payload.find("\"owner\"") == std::string::npos);
        raw.Exec("UPDATE metadata SET game_version='future informational label'");
    }
    SaveDatabase database(TestContent());
    auto opened = database.Open(file.path.c_str());
    REQUIRE(opened);
    CHECK(opened->seed == UINT32_MAX);
    CHECK(opened->nextId == UINT64_MAX);
    CHECK(opened->session.tick == UINT64_MAX);
    CHECK(opened->session.autosaveInterval == UINT64_MAX - 1);
    CHECK(opened->session.rngState == UINT32_MAX);
    CHECK(opened->session.pacing == snapshot.session.pacing);
    auto loaded = database.ReadChunk({-1, -1}, opened->nextId);
    REQUIRE(loaded);
    REQUIRE(*loaded);
    CHECK((**loaded).tiles == chunk.tiles);
    REQUIRE((**loaded).actors.size() == 1);
    CHECK((**loaded).actors[0].turns == UINT64_MAX);
    CHECK((**loaded).actors[0].speed == 173);
    CHECK((**loaded).actors[0].remainingTicks == UINT64_MAX);
    CHECK((**loaded).actors[0].carryCapacityGrams == 100000);
    CHECK((**loaded).objects[0].updates == UINT64_MAX - 1);
    CHECK((**loaded).objects[0].updateInterval == 217);
    CHECK((**loaded).objects[0].remainingTicks == 176);
    REQUIRE((**loaded).items.size() == 5);
    CHECK((**loaded).items[2].owner == 4);
    CHECK((**loaded).items[2].location == LocationKind::item);
    CHECK((**loaded).items.back().id == UINT64_MAX - 1);
    auto absent = database.ReadChunk({1, 0}, opened->nextId);
    REQUIRE(absent);
    CHECK_FALSE(*absent);
}

TEST_CASE("Save database keeps same XZ slices independent through updates and reload", "[save][database][layers]") {
    SaveFile file;
    auto snapshot = SampleSnapshot();
    snapshot.session.playerPosition = {-16, 32, world_min_y};
    snapshot.session.players.push_back({1, snapshot.session.playerPosition, "host", 0});
    {
        SaveDatabase database(TestContent());
        REQUIRE(database.Create(file.path.c_str()));
        REQUIRE(database.Begin());
        REQUIRE(database.WriteChunk(SampleChunk({-1, 2, world_min_y})));
        for (int y : {0, world_max_y}) {
            ChunkArchive chunk{};
            chunk.position = {-1, 2, y};
            chunk.tiles.fill({TestTerrain("floor"), static_cast<std::uint8_t>(y + 20)});
            REQUIRE(database.WriteChunk(chunk));
        }
        REQUIRE(database.WriteSnapshot(snapshot));
        REQUIRE(database.Commit());
        REQUIRE(database.Begin());
        auto lower = SampleChunk({-1, 2, world_min_y});
        lower.tiles[0].damage = 77;
        REQUIRE(database.WriteChunk(lower));
        REQUIRE(database.Commit());
    }
    SaveDatabase database(TestContent());
    auto opened = database.Open(file.path.c_str());
    REQUIRE(opened);
    CHECK(opened->session.playerPosition == snapshot.session.playerPosition);
    CHECK(opened->session.players[0].position == snapshot.session.playerPosition);
    for (int y : {world_min_y, 0, world_max_y}) {
        auto loaded = database.ReadChunk({-1, 2, y}, snapshot.nextId);
        REQUIRE(loaded);
        REQUIRE(*loaded);
        const auto& chunk = **loaded;
        CHECK(chunk.position == ChunkCoord{-1, 2, y});
        CHECK(chunk.tiles[0].damage == (y == world_min_y ? 77 : y + 20));
        if (y == world_min_y) {
            CHECK(chunk.actors[0].position.y == y);
            CHECK(chunk.objects[0].position.y == y);
            CHECK(chunk.items.back().position.y == y);
        }
    }
    auto absent = database.ReadChunk({-1, 2, 1}, snapshot.nextId);
    REQUIRE(absent);
    CHECK_FALSE(*absent);
    CHECK(database.ReadChunk({-1, 2, -11}, snapshot.nextId)
        == eastl::unexpected(SaveError::InvalidData));
}

TEST_CASE("Save database supports generation versions one through three", "[save][database][layers]") {
    SaveFile file;
    SaveDatabase database(TestContent());
    REQUIRE(database.Create(file.path.c_str()));
    for (std::uint32_t version : {1U, 2U, 3U}) {
        SaveSnapshot snapshot;
        snapshot.generationVersion = version;
        REQUIRE(database.Begin());
        REQUIRE(database.WriteSnapshot(snapshot));
        REQUIRE(database.Commit());
        SaveDatabase observer(TestContent());
        auto opened = observer.Open(file.path.c_str());
        REQUIRE(opened);
        CHECK(opened->generationVersion == version);
    }
}

TEST_CASE("Save database rejects corrupt SQL Y coordinates", "[save][database][layers]") {
    SaveFile file;
    Populate(file.path.c_str());
    {
        RawDatabase raw(file.path.c_str());
        SECTION("below minimum") { raw.Exec("UPDATE chunks SET y=-11"); }
        SECTION("above maximum") { raw.Exec("UPDATE chunks SET y=11"); }
        SECTION("overflow") { raw.Exec("UPDATE chunks SET y=4294967296"); }
        SECTION("fraction") { raw.Exec("UPDATE chunks SET y=0.5"); }
        SECTION("text") { raw.Exec("UPDATE chunks SET y='surface'"); }
    }
    SaveDatabase database(TestContent());
    CHECK(database.Open(file.path.c_str()) == eastl::unexpected(SaveError::InvalidData));
}

TEST_CASE("Save database commit rejects duplicate complete slice coordinates", "[save][database][layers]") {
    SaveFile file;
    Populate(file.path.c_str());
    SaveDatabase database(TestContent());
    REQUIRE(database.Open(file.path.c_str()));
    {
        // Simulate external damage after Open to exercise the row validator too.
        RawDatabase raw(file.path.c_str());
        raw.Exec("ALTER TABLE chunks RENAME TO original_chunks;"
            "CREATE TABLE chunks(x INTEGER,z INTEGER,y INTEGER,payload TEXT);"
            "INSERT INTO chunks SELECT * FROM original_chunks;"
            "INSERT INTO chunks SELECT * FROM original_chunks;"
            "DROP TABLE original_chunks;");
    }
    REQUIRE(database.Begin());
    CHECK(database.Commit() == eastl::unexpected(SaveError::InvalidData));
    CHECK(database.Commit() == eastl::unexpected(SaveError::InvalidState));
}

TEST_CASE("Save database rolls back failed checkpoints including previously written chunks", "[save][database]") {
    SaveFile file;
    Populate(file.path.c_str());
    SaveDatabase database(TestContent());
    REQUIRE(database.Open(file.path.c_str()));
    auto chunk = SampleChunk();
    chunk.tiles[0].damage = 41;
    REQUIRE(database.Begin());
    REQUIRE(database.WriteChunk(chunk));
    auto bad = SampleSnapshot();
    bad.nextId = 7; // ID 7 is present: only full checkpoint validation can reject it.
    REQUIRE(database.WriteSnapshot(bad));
    CHECK(database.Commit() == eastl::unexpected(SaveError::InvalidData));
    CHECK(database.Commit() == eastl::unexpected(SaveError::InvalidState));
    auto original = database.ReadChunk({0, 0}, 8);
    REQUIRE(original);
    REQUIRE(*original);
    CHECK((**original).tiles[0].damage == 0);

    REQUIRE(database.Begin());
    REQUIRE(database.WriteChunk(chunk));
    chunk.items[0].owner = 999;
    CHECK(database.WriteChunk(chunk) == eastl::unexpected(SaveError::InvalidData));
    CHECK(database.Commit() == eastl::unexpected(SaveError::InvalidState));
    original = database.ReadChunk({0, 0}, 8);
    REQUIRE(original);
    REQUIRE(*original);
    CHECK((**original).tiles[0].damage == 0);

    REQUIRE(database.Begin());
    chunk = SampleChunk();
    chunk.tiles[0].damage = 52;
    REQUIRE(database.WriteChunk(chunk));
    database.Rollback();
    database.Rollback();
    SaveDatabase observer(TestContent());
    REQUIRE(observer.Open(file.path.c_str()));
    original = observer.ReadChunk({0, 0}, 8);
    REQUIRE(original);
    REQUIRE(*original);
    CHECK((**original).tiles[0].damage == 0);
}

TEST_CASE("Save database reports writer and commit contention as busy", "[save][database]") {
    SaveFile file;
    Populate(file.path.c_str());
    SaveDatabase database(TestContent());
    REQUIRE(database.Open(file.path.c_str()));
    RawDatabase lock(file.path.c_str());
    lock.Exec("BEGIN IMMEDIATE");
    CHECK(database.Begin() == eastl::unexpected(SaveError::Busy));
    lock.Exec("ROLLBACK");

    lock.Exec("BEGIN; SELECT * FROM chunks");
    REQUIRE(database.Begin());
    auto chunk = SampleChunk();
    chunk.tiles[0].damage = 90;
    REQUIRE(database.WriteChunk(chunk));
    CHECK(database.Commit() == eastl::unexpected(SaveError::Busy));
    lock.Exec("ROLLBACK");
    auto original = database.ReadChunk({0, 0}, 8);
    REQUIRE(original);
    REQUIRE(*original);
    CHECK((**original).tiles[0].damage == 0);
}

TEST_CASE("Save database rolls back earlier SQL writes after a SQLite failure", "[save][database]") {
    SaveFile file;
    Populate(file.path.c_str());
    SaveDatabase database(TestContent());
    REQUIRE(database.Open(file.path.c_str()));
    {
        RawDatabase raw(file.path.c_str());
        // Inject a database-level failure after opening, so this exercises the
        // SQLite error path rather than rejecting an unknown schema on Open.
        raw.Exec("CREATE TRIGGER fail_snapshot BEFORE UPDATE ON world_state "
            "BEGIN SELECT RAISE(ABORT,'injected checkpoint failure'); END");
    }
    REQUIRE(database.Begin());
    auto chunk = SampleChunk();
    chunk.tiles[0].damage = 73;
    REQUIRE(database.WriteChunk(chunk));
    CHECK(database.WriteSnapshot(SampleSnapshot()) == eastl::unexpected(SaveError::InvalidData));
    CHECK(database.Commit() == eastl::unexpected(SaveError::InvalidState));
    auto original = database.ReadChunk({0, 0}, 8);
    REQUIRE(original);
    REQUIRE(*original);
    CHECK((**original).tiles[0].damage == 0);
    {
        RawDatabase raw(file.path.c_str());
        raw.Exec("DROP TRIGGER fail_snapshot");
    }
    SaveDatabase observer(TestContent());
    auto snapshot = observer.Open(file.path.c_str());
    REQUIRE(snapshot);
    CHECK(snapshot->session.tick == UINT64_MAX);
}

TEST_CASE("Save database rejects unknown versions and incomplete or extended schemas", "[save][database]") {
    SaveFile file;
    Populate(file.path.c_str());
    {
        RawDatabase raw(file.path.c_str());
        SECTION("future version") { raw.Exec("UPDATE metadata SET format_version=11"); }
        SECTION("previous v6 format") { raw.Exec("UPDATE metadata SET format_version=6"); }
        SECTION("previous v5 format") { raw.Exec("UPDATE metadata SET format_version=5"); }
        SECTION("previous v4 format") { raw.Exec("UPDATE metadata SET format_version=4"); }
        SECTION("previous v3 format") { raw.Exec("UPDATE metadata SET format_version=3"); }
        SECTION("command count ticks version") { raw.Exec("UPDATE metadata SET format_version=2"); }
        SECTION("old version without migration") { raw.Exec("UPDATE metadata SET format_version=1"); }
    }
    SaveDatabase database(TestContent());
    CHECK(database.Open(file.path.c_str()) == eastl::unexpected(SaveError::UnsupportedVersion));
}

TEST_CASE("Save database rejects damaged metadata and schemas", "[save][database]") {
    SaveFile file;
    Populate(file.path.c_str());
    {
        RawDatabase raw(file.path.c_str());
        SECTION("missing metadata") { raw.Exec("DELETE FROM metadata"); }
        SECTION("extra columns") { raw.Exec("ALTER TABLE chunks ADD COLUMN future TEXT"); }
        SECTION("v5 chunks schema with current metadata") {
            raw.Exec("ALTER TABLE chunks RENAME TO old_chunks;"
                "CREATE TABLE chunks(x INTEGER NOT NULL,z INTEGER NOT NULL,payload TEXT NOT NULL,PRIMARY KEY(x,z));"
                "INSERT INTO chunks SELECT x,z,payload FROM old_chunks; DROP TABLE old_chunks;");
        }
        SECTION("missing world_state") { raw.Exec("DELETE FROM world_state"); }
        SECTION("missing player memories table") { raw.Exec("DROP TABLE player_memories"); }
        SECTION("extended player memories table") { raw.Exec("ALTER TABLE player_memories ADD COLUMN future TEXT"); }
        SECTION("unknown tables") { raw.Exec("CREATE TABLE future(value TEXT)"); }
        SECTION("malformed chunk") { raw.Exec("UPDATE chunks SET payload='{}'"); }
    }
    SaveDatabase database(TestContent());
    CHECK(database.Open(file.path.c_str()) == eastl::unexpected(SaveError::InvalidData));
}

TEST_CASE("Save database strictly validates nested JSON and coordinates", "[save][database]") {
    SaveFile file;
    Populate(file.path.c_str());
    {
        RawDatabase raw(file.path.c_str());
        auto payload = raw.Text("SELECT payload FROM chunks");
        SECTION("duplicate root key") { payload.insert(1, "\"actors\":[],"); }
        SECTION("duplicate nested key") { Replace(payload, "\"type\":\"backpack\"", "\"type\":\"backpack\",\"type\":\"backpack\""); }
        SECTION("JSON5 comment") { payload.insert(1, "/* comment */"); }
        SECTION("numeric ID") { Replace(payload, "\"id\":\"3\"", "\"id\":3"); }
        SECTION("overflow ID") { Replace(payload, "\"id\":\"3\"", "\"id\":\"18446744073709551616\""); }
        SECTION("noncanonical ID") { Replace(payload, "\"id\":\"3\"", "\"id\":\"03\""); }
        SECTION("zero ID") { Replace(payload, "\"id\":\"3\"", "\"id\":\"0\""); }
        SECTION("duplicate ID across entity types") { Replace(payload, "\"id\":\"3\"", "\"id\":\"2\""); }
        SECTION("unknown item") { Replace(payload, "\"type\":\"bandage\"", "\"type\":\"unknown\""); }
        SECTION("noncontainer children") { Replace(payload, "\"type\":\"backpack\"", "\"type\":\"rock\""); }
        SECTION("unknown terrain") { Replace(payload, "\"terrain\":\"floor\"", "\"terrain\":\"water\""); }
        SECTION("embedded NUL terrain") { Replace(payload, "\"terrain\":\"floor\"", "\"terrain\":\"floor\\u0000wall\""); }
        SECTION("numeric terrain") { Replace(payload, "\"terrain\":\"floor\"", "\"terrain\":1"); }
        SECTION("tile count") { Replace(payload, "{\"terrain\":\"floor\",\"damage\":0},", ""); }
        SECTION("damage overflow") { Replace(payload, "\"damage\":0", "\"damage\":256"); }
        SECTION("wrong chunk position") { Replace(payload, "\"x\":0", "\"x\":16"); }
        SECTION("position overflow") { Replace(payload, "\"x\":0", "\"x\":2147483648"); }
        SECTION("missing Y") { Replace(payload, ",\"y\":0", ""); }
        SECTION("duplicate Y") { Replace(payload, "\"y\":0", "\"y\":0,\"y\":0"); }
        SECTION("Y below world") { Replace(payload, "\"y\":0", "\"y\":-11"); }
        SECTION("Y above world") { Replace(payload, "\"y\":0", "\"y\":11"); }
        SECTION("Y overflow") { Replace(payload, "\"y\":0", "\"y\":4294967296"); }
        SECTION("wrong layer") { Replace(payload, "\"y\":0", "\"y\":1"); }
        SECTION("zero actor speed") { Replace(payload, "\"speed\":173", "\"speed\":0"); }
        SECTION("negative actor speed") { Replace(payload, "\"speed\":173", "\"speed\":-1"); }
        SECTION("overflow actor speed") { Replace(payload, "\"speed\":173", "\"speed\":4294967296"); }
        SECTION("missing actor speed") { Replace(payload, "\"speed\":173,", ""); }
        SECTION("actor light missing") { Replace(payload, "\"light\":{\"intensity\":0,\"range\":0},", ""); }
        SECTION("actor light intensity overflow") { Replace(payload, "\"intensity\":0", "\"intensity\":256"); }
        SECTION("actor light range overflow") { Replace(payload, "\"range\":0", "\"range\":13"); }
        SECTION("actor light unpaired zero") { Replace(payload, "\"intensity\":0", "\"intensity\":1"); }
        SECTION("actor light duplicate field") { Replace(payload, "\"range\":0", "\"range\":0,\"range\":0"); }
        SECTION("actor light unknown field") { Replace(payload, "\"range\":0", "\"range\":0,\"extra\":0"); }
        SECTION("object None kind") { Replace(payload, "\"kind\":1", "\"kind\":0"); }
        SECTION("object unknown kind") { Replace(payload, "\"kind\":1", "\"kind\":5"); }
        SECTION("object invalid facing") { Replace(payload, "\"facing\":0", "\"facing\":4"); }
        SECTION("object broken not boolean") { Replace(payload, "\"broken\":false", "\"broken\":0"); }
        SECTION("numeric actor carry capacity") { Replace(payload, "\"carry_capacity_g\":\"100000\"", "\"carry_capacity_g\":100000"); }
        SECTION("missing parked flag") { Replace(payload, "\"parked\":false,", ""); }
        SECTION("numeric parked flag") { Replace(payload, "\"parked\":false", "\"parked\":0"); }
        SECTION("nonplayer parked") {
            Replace(payload, "\"player\":true", "\"player\":false");
            Replace(payload, "\"parked\":false", "\"parked\":true");
        }
        SECTION("numeric actor delay") { Replace(payload, "\"remaining_ticks\":\"18446744073709551615\"", "\"remaining_ticks\":1"); }
        SECTION("overflow actor delay") { Replace(payload, "\"remaining_ticks\":\"18446744073709551615\"", "\"remaining_ticks\":\"18446744073709551616\""); }
        SECTION("negative actor delay") { Replace(payload, "\"remaining_ticks\":\"18446744073709551615\"", "\"remaining_ticks\":\"-1\""); }
        SECTION("noncanonical actor delay") { Replace(payload, "\"remaining_ticks\":\"18446744073709551615\"", "\"remaining_ticks\":\"01\""); }
        SECTION("zero object interval") { Replace(payload, "\"update_interval\":217", "\"update_interval\":0"); }
        SECTION("overflow object interval") { Replace(payload, "\"update_interval\":217", "\"update_interval\":4294967296"); }
        SECTION("object delay exceeds interval") { Replace(payload, "\"remaining_ticks\":\"176\"", "\"remaining_ticks\":\"218\""); }
        SECTION("numeric object delay") { Replace(payload, "\"remaining_ticks\":\"176\"", "\"remaining_ticks\":176"); }
        SECTION("missing object delay") { Replace(payload, "\"remaining_ticks\":\"176\",", ""); }
        SECTION("missing ground position") {
            const auto ground = payload.find("\"ground_items\"");
            REQUIRE(ground != std::string::npos);
            const auto position = payload.find(",\"position\":{\"x\":0,\"z\":0,\"y\":0}", ground);
            REQUIRE(position != std::string::npos);
            payload.erase(position, std::string(",\"position\":{\"x\":0,\"z\":0,\"y\":0}").size());
        }
        raw.SetText("UPDATE chunks SET payload=?1", payload);
    }
    SaveDatabase database(TestContent());
    CHECK(database.Open(file.path.c_str()) == eastl::unexpected(SaveError::InvalidData));
}

TEST_CASE("Save database validates session, counters and generator version", "[save][database]") {
    SaveFile file;
    Populate(file.path.c_str());
    SaveError expected = SaveError::InvalidData;
    {
        RawDatabase raw(file.path.c_str());
        auto payload = raw.Text("SELECT payload FROM world_state");
        SECTION("unknown generator") {
            Replace(payload, "\"generation_version\":3", "\"generation_version\":4");
            expected = SaveError::UnsupportedVersion;
        }
        SECTION("numeric counter") { Replace(payload, "\"tick\":\"18446744073709551615\"", "\"tick\":1"); }
        SECTION("missing sky light") { Replace(payload, "\"sky_light\":192,", ""); }
        SECTION("sky light overflow") { Replace(payload, "\"sky_light\":192", "\"sky_light\":256"); }
        SECTION("negative sky light") { Replace(payload, "\"sky_light\":192", "\"sky_light\":-1"); }
        SECTION("duplicate sky light") { Replace(payload, "\"sky_light\":192", "\"sky_light\":192,\"sky_light\":192"); }
        SECTION("invalid radius") { Replace(payload, "\"resident_radius\":5", "\"resident_radius\":9"); }
        SECTION("negative active radius") { Replace(payload, "\"active_radius\":1", "\"active_radius\":-1"); }
        SECTION("resident smaller than active") { Replace(payload, "\"resident_radius\":5", "\"resident_radius\":0"); }
        SECTION("missing circle residency") { Replace(payload, ",\"circle_residency\":null", ""); }
        SECTION("duplicate circle residency") { Replace(payload, "\"circle_residency\":null", "\"circle_residency\":null,\"circle_residency\":null"); }
        SECTION("unknown session field") { Replace(payload, "\"circle_residency\":null", "\"circle_residency\":null,\"future\":0"); }
        SECTION("numeric circle residency") { Replace(payload, "\"circle_residency\":null", "\"circle_residency\":78"); }
        SECTION("array circle residency") { Replace(payload, "\"circle_residency\":null", "\"circle_residency\":[]"); }
        SECTION("missing circle radius") { Replace(payload, "\"circle_residency\":null", "\"circle_residency\":{}"); }
        SECTION("negative circle radius") { Replace(payload, "\"circle_residency\":null", "\"circle_residency\":{\"radius_tiles\":-1}"); }
        SECTION("excessive circle radius") { Replace(payload, "\"circle_residency\":null", "\"circle_residency\":{\"radius_tiles\":129}"); }
        SECTION("overflow circle radius") { Replace(payload, "\"circle_residency\":null", "\"circle_residency\":{\"radius_tiles\":18446744073709551615}"); }
        SECTION("fractional circle radius") { Replace(payload, "\"circle_residency\":null", "\"circle_residency\":{\"radius_tiles\":1.5}"); }
        SECTION("string circle radius") { Replace(payload, "\"circle_residency\":null", "\"circle_residency\":{\"radius_tiles\":\"78\"}"); }
        SECTION("boolean circle radius") { Replace(payload, "\"circle_residency\":null", "\"circle_residency\":{\"radius_tiles\":true}"); }
        SECTION("null circle radius") { Replace(payload, "\"circle_residency\":null", "\"circle_residency\":{\"radius_tiles\":null}"); }
        SECTION("duplicate circle radius") { Replace(payload, "\"circle_residency\":null", "\"circle_residency\":{\"radius_tiles\":78,\"radius_tiles\":78}"); }
        SECTION("unknown circle field") { Replace(payload, "\"circle_residency\":null", "\"circle_residency\":{\"radius_tiles\":78,\"future\":0}"); }
        SECTION("invalid next ID") { Replace(payload, "\"next_id\":\"8\"", "\"next_id\":\"7\""); }
        SECTION("player is an object") { Replace(payload, "\"player\":\"1\"", "\"player\":\"2\""); }
        SECTION("player position mismatch") { Replace(payload, "\"x\":0", "\"x\":1"); }
        SECTION("missing session Y") { Replace(payload, ",\"y\":0", ""); }
        SECTION("session Y out of range") { Replace(payload, "\"y\":0", "\"y\":-11"); }
        SECTION("player layer mismatch") { Replace(payload, "\"y\":0", "\"y\":1"); }
        SECTION("duplicate session key") { Replace(payload, "\"rng_state\":4294967295", "\"rng_state\":0,\"rng_state\":1"); }
        SECTION("zero pacing rate") { Replace(payload, "\"ticks_per_second\":37", "\"ticks_per_second\":0"); }
        SECTION("overflow pacing rate") { Replace(payload, "\"ticks_per_second\":37", "\"ticks_per_second\":4294967296"); }
        SECTION("negative pacing rate") { Replace(payload, "\"ticks_per_second\":37", "\"ticks_per_second\":-1"); }
        SECTION("numeric rate limited") { Replace(payload, "\"rate_limited\":true", "\"rate_limited\":1"); }
        SECTION("numeric pause when idle") { Replace(payload, "\"pause_when_idle\":false", "\"pause_when_idle\":0"); }
        SECTION("missing pacing field") { Replace(payload, "\"ticks_per_second\":37,", ""); }
        SECTION("missing roster") { Replace(payload, ",\"players\":[]", ""); }
        SECTION("duplicate pacing field") { Replace(payload, "\"ticks_per_second\":37", "\"ticks_per_second\":37,\"ticks_per_second\":37"); }
        raw.SetText("UPDATE world_state SET payload=?1", payload);
    }
    SaveDatabase database(TestContent());
    CHECK(database.Open(file.path.c_str()) == eastl::unexpected(expected));
}

TEST_CASE("Save format ten round trips square and tile circle residency", "[save][database][residency]") {
    SaveFile file;
    auto snapshot = SampleSnapshot();
    SECTION("square") {}
    SECTION("zero circle radius") { snapshot.session.circleResidency = CircleResidency{0}; }
    SECTION("default circle radius") { snapshot.session.circleResidency = CircleResidency{}; }
    SECTION("maximum circle radius") { snapshot.session.circleResidency = CircleResidency{128}; }
    snapshot.session.activeRadius = 0;
    snapshot.session.residentRadius = 2;
    {
        SaveDatabase database(TestContent());
        REQUIRE(database.Create(file.path.c_str()));
        REQUIRE(database.Begin());
        REQUIRE(database.WriteChunk(SampleChunk()));
        REQUIRE(database.WriteSnapshot(snapshot));
        REQUIRE(database.Commit());
    }
    {
        RawDatabase raw(file.path.c_str());
        CHECK(raw.Text("SELECT format_version FROM metadata") == "10");
        const auto payload = raw.Text("SELECT payload FROM world_state");
        CHECK(payload.find("\"circle_residency\":") != std::string::npos);
        if (!snapshot.session.circleResidency) {
            CHECK(payload.find("\"circle_residency\":null") != std::string::npos);
        }
    }
    SaveDatabase database(TestContent());
    auto loaded = database.Open(file.path.c_str());
    REQUIRE(loaded);
    CHECK(loaded->session.circleResidency == snapshot.session.circleResidency);
    CHECK(loaded->session.activeRadius == 0);
    CHECK(loaded->session.residentRadius == 2);
}

TEST_CASE("Circle snapshots round trip at world boundaries independently of retained square residency", "[save][database][residency]") {
    SaveFile file;
    auto snapshot = SampleSnapshot();
    const int edge = world_coordinate_limit - 1;
    SECTION("positive X") { snapshot.session.playerPosition = {edge, 0}; }
    SECTION("negative X") { snapshot.session.playerPosition = {-edge, 0}; }
    SECTION("positive Z") { snapshot.session.playerPosition = {0, edge}; }
    SECTION("negative Z") { snapshot.session.playerPosition = {0, -edge}; }
    snapshot.session.activeRadius = 0;
    snapshot.session.residentRadius = 5;
    snapshot.session.circleResidency = CircleResidency{0};
    auto chunk = SampleChunk(ChunkOf(snapshot.session.playerPosition));
    chunk.actors[0].position = snapshot.session.playerPosition;
    {
        SaveDatabase database(TestContent());
        REQUIRE(database.Create(file.path.c_str()));
        REQUIRE(database.Begin());
        REQUIRE(database.WriteChunk(chunk));
        REQUIRE(database.WriteSnapshot(snapshot));
        REQUIRE(database.Commit());
    }
    SaveDatabase database(TestContent());
    auto loaded = database.Open(file.path.c_str());
    REQUIRE(loaded);
    CHECK(loaded->session.playerPosition == snapshot.session.playerPosition);
    CHECK(loaded->session.circleResidency == snapshot.session.circleResidency);
    CHECK(loaded->session.activeRadius == 0);
    CHECK(loaded->session.residentRadius == 5);

    // A circle cannot hide an active square or tile circle crossing world bounds.
    auto invalid = *loaded;
    invalid.session.activeRadius = 2;
    REQUIRE(database.Begin());
    CHECK(database.WriteSnapshot(invalid) == eastl::unexpected(SaveError::InvalidData));
    invalid = *loaded;
    invalid.session.circleResidency = CircleResidency{2};
    REQUIRE(database.Begin());
    CHECK(database.WriteSnapshot(invalid) == eastl::unexpected(SaveError::InvalidData));
    invalid = *loaded;
    invalid.session.circleResidency.reset();
    REQUIRE(database.Begin());
    CHECK(database.WriteSnapshot(invalid) == eastl::unexpected(SaveError::InvalidData));

    // Apply the same invalid policies directly to disk to exercise strict decoding.
    RawDatabase raw(file.path.c_str());
    const auto original = raw.Text("SELECT payload FROM world_state");
    auto payload = original;
    Replace(payload, "\"active_radius\":0", "\"active_radius\":2");
    raw.SetText("UPDATE world_state SET payload=?1", payload);
    SaveDatabase activeObserver(TestContent());
    CHECK(activeObserver.Open(file.path.c_str()) == eastl::unexpected(SaveError::InvalidData));
    payload = original;
    Replace(payload, "\"radius_tiles\":0", "\"radius_tiles\":2");
    raw.SetText("UPDATE world_state SET payload=?1", payload);
    SaveDatabase circleObserver(TestContent());
    CHECK(circleObserver.Open(file.path.c_str()) == eastl::unexpected(SaveError::InvalidData));
}

TEST_CASE("Save database rejects invalid residency before encoding", "[save][database][residency]") {
    SaveFile file;
    Populate(file.path.c_str());
    SaveDatabase database(TestContent());
    REQUIRE(database.Open(file.path.c_str()));
    auto snapshot = SampleSnapshot();
    snapshot.session.circleResidency = CircleResidency{};
    SECTION("negative circle") { snapshot.session.circleResidency->radiusTiles = -1; }
    SECTION("excessive circle") { snapshot.session.circleResidency->radiusTiles = 129; }
    SECTION("negative square active radius") { snapshot.session.activeRadius = -1; }
    SECTION("excessive square resident radius") { snapshot.session.residentRadius = 9; }
    SECTION("square radius ordering") { snapshot.session.residentRadius = 0; }
    REQUIRE(database.Begin());
    CHECK(database.WriteSnapshot(snapshot) == eastl::unexpected(SaveError::InvalidData));
    CHECK(database.Commit() == eastl::unexpected(SaveError::InvalidState));
}

TEST_CASE("V9 requires strict controller envelopes and generic object state", "[save][database][actors][objects]") {
    SaveFile file;
    Populate(file.path.c_str());
    SaveError expected = SaveError::InvalidData;
    {
        RawDatabase raw(file.path.c_str());
        auto payload = raw.Text("SELECT payload FROM chunks");
        SECTION("missing controller") { Replace(payload, "\"controller\":{\"type\":\"none\",\"state\":{}},", ""); }
        SECTION("empty controller sentinel forbidden on disk") { Replace(payload, "\"controller\":{\"type\":\"none\",\"state\":{}}", "\"controller\":{}"); }
        SECTION("unknown controller") { Replace(payload, "\"type\":\"none\"", "\"type\":\"unknown\""); }
        SECTION("duplicate envelope field") { Replace(payload, "\"type\":\"none\"", "\"type\":\"none\",\"type\":\"none\""); }
        SECTION("missing controller state") { Replace(payload, "\"type\":\"none\",\"state\":{}", "\"type\":\"none\""); }
        SECTION("none with state") { Replace(payload, "\"type\":\"none\",\"state\":{}", "\"type\":\"none\",\"state\":{\"version\":1}"); }
        SECTION("player cannot wander") { Replace(payload, "\"type\":\"none\",\"state\":{}", "\"type\":\"wander\",\"state\":{\"version\":1}"); }
        SECTION("wander missing state version") { Replace(payload, "\"type\":\"none\"", "\"type\":\"wander\""); }
        SECTION("future controller state") {
            Replace(payload, "\"type\":\"none\",\"state\":{}", "\"type\":\"wander\",\"state\":{\"version\":2}");
            expected = SaveError::UnsupportedVersion;
        }
        SECTION("missing object type") { Replace(payload, "\"type\":\"\",", ""); }
        SECTION("numeric object type") { Replace(payload, "\"type\":\"\"", "\"type\":0"); }
        SECTION("unknown object type") { Replace(payload, "\"type\":\"\"", "\"type\":\"absent\""); }
        SECTION("missing object state") { Replace(payload, "\"type\":\"\",\"state\":{}", "\"type\":\"\""); }
        SECTION("generic nonempty state") { Replace(payload, "\"type\":\"\",\"state\":{}", "\"type\":\"\",\"state\":{\"version\":1}"); }
        SECTION("generic list state") { Replace(payload, "\"type\":\"\",\"state\":{}", "\"type\":\"\",\"state\":[]"); }
        raw.SetText("UPDATE chunks SET payload=?1", payload);
    }
    SaveDatabase database(TestContent());
    CHECK(database.Open(file.path.c_str()) == eastl::unexpected(expected));
}

TEST_CASE("V9 normalizes source controller defaults and enforces player policy before writing", "[save][database][actors]") {
    SaveFile file;
    SaveDatabase database(TestContent());
    REQUIRE(database.Create(file.path.c_str()));
    auto chunk = SampleChunk();
    chunk.actors.push_back({8, {1, 0}, false, 0});
    auto snapshot = SampleSnapshot();
    snapshot.nextId = 9;
    bool npcWanders = true;
    SECTION("omitted envelopes become player None and NPC Wander") {}
    SECTION("NPC can explicitly have no controller") {
        chunk.actors[1].controller = CaptureAiController(nullptr);
        npcWanders = false;
    }
    SECTION("player controller is rejected") {
        auto controller = MakeAiController(AiControllerKind::Wander);
        REQUIRE(controller);
        chunk.actors[0].controller = CaptureAiController(controller->get());
        REQUIRE(database.Begin());
        CHECK(database.WriteChunk(chunk) == eastl::unexpected(SaveError::InvalidData));
        CHECK(database.Commit() == eastl::unexpected(SaveError::InvalidState));
        return;
    }
    REQUIRE(database.Begin());
    REQUIRE(database.WriteChunk(chunk));
    REQUIRE(database.WriteSnapshot(snapshot));
    REQUIRE(database.Commit());
    auto loaded = database.ReadChunk({}, snapshot.nextId);
    REQUIRE(loaded);
    REQUIRE(*loaded);
    CHECK((**loaded).actors[0].controller == CaptureAiController(nullptr));
    auto npc = ReadAiController((**loaded).actors[1].controller);
    REQUIRE(npc);
    CHECK(static_cast<bool>(*npc) == npcWanders);
}

TEST_CASE("V9 typed object state resolves stable names across catalog renumbering", "[save][database][objects]") {
    const ContentSource sources[] = {
        {"base", R"({format_version:1,definitions:[
            {type:'terrain',id:'floor',name:'Floor',movement:{passable:true},blocks_sight:false,appearance:{symbol:'.',foreground:'white'}},
            {type:'item',id:'scrap',name:'Scrap',mass_g:1,volume_ml:1},
            {type:'item',id:'pipe',name:'Pipe',mass_g:1,volume_ml:1}]})"},
        {"objects", R"({format_version:1,objects:[{id:'press',name:'Press',behavior:'crafting_machine',machine:{
            input:'scrap',input_count:2,output:'pipe',output_count:1,duration_ticks:100,energy_capacity:300,energy_per_tick:2}}]})"},
        {"extra", R"({format_version:1,objects:[{id:'decoration',name:'Decoration',behavior:'none'}]})"}
    };
    const auto original = SaveTestCatalog({sources, 2});
    const auto expanded = SaveTestCatalog({sources, 3});
    REQUIRE(original.FindObject("press") != expanded.FindObject("press"));
    SaveFile file;
    ChunkArchive chunk{};
    chunk.tiles.fill({original.FindTerrain("floor"), 0});
    SavedObject object{1, {}, true, 19};
    object.type = original.FindObject("press");
    auto behavior = MakeObjectBehavior(*original.TryGetObject(object.type));
    REQUIRE(behavior);
    REQUIRE(*behavior);
    REQUIRE((*behavior)->SetEnergy(137));
    object.state = (*behavior)->SaveState();
    chunk.objects.push_back(object);
    {
        SaveDatabase database(original);
        REQUIRE(database.Create(file.path.c_str()));
        REQUIRE(database.Begin());
        REQUIRE(database.WriteChunk(chunk));
        SaveSnapshot snapshot;
        snapshot.nextId = 2;
        REQUIRE(database.WriteSnapshot(snapshot));
        REQUIRE(database.Commit());
    }
    SECTION("round trip") {
        RawDatabase raw(file.path.c_str());
        CHECK(raw.Text("SELECT payload FROM chunks").find("\"type\":\"press\"") != std::string::npos);
        SaveDatabase database(expanded);
        REQUIRE(database.Open(file.path.c_str()));
        auto loaded = database.ReadChunk({}, 2);
        REQUIRE(loaded);
        REQUIRE(*loaded);
        CHECK((**loaded).objects[0].type == expanded.FindObject("press"));
        CHECK((**loaded).objects[0].state == object.state);
    }
    SECTION("factory rejects missing required state rather than defaulting") {
        RawDatabase raw(file.path.c_str());
        raw.Exec("UPDATE chunks SET payload=json_set(payload,'$.objects[0].state',json('{}'))");
        SaveDatabase database(expanded);
        CHECK(database.Open(file.path.c_str()) == eastl::unexpected(SaveError::InvalidData));
    }
    SECTION("factory preserves unsupported state version") {
        RawDatabase raw(file.path.c_str());
        raw.Exec("UPDATE chunks SET payload=json_set(payload,'$.objects[0].state.version',2)");
        SaveDatabase database(expanded);
        CHECK(database.Open(file.path.c_str()) == eastl::unexpected(SaveError::UnsupportedVersion));
    }
    SECTION("missing object definition fails") {
        const auto missing = SaveTestCatalog({sources, 1});
        SaveDatabase database(missing);
        CHECK(database.Open(file.path.c_str()) == eastl::unexpected(SaveError::InvalidData));
    }
    SECTION("write validation uses factory too") {
        SaveDatabase database(original);
        REQUIRE(database.Open(file.path.c_str()));
        chunk.objects[0].state = DataObject{};
        REQUIRE(database.Begin());
        CHECK(database.WriteChunk(chunk) == eastl::unexpected(SaveError::InvalidData));
    }
}

TEST_CASE("Opening genuine v7 preserves square residency until the first committed checkpoint", "[save][database][migration]") {
    SaveFile file;
    PopulateV7(file.path.c_str());
    std::string originalSnapshot;
    std::string originalChunk;
    std::string originalMemory;
    {
        RawDatabase raw(file.path.c_str());
        originalSnapshot = raw.Text("SELECT payload FROM world_state");
        originalChunk = raw.Text("SELECT payload FROM chunks");
        originalMemory = raw.Text("SELECT payload FROM player_memories");
    }
    SaveDatabase database(TestContent());
    auto loaded = database.Open(file.path.c_str());
    REQUIRE(loaded);
    CHECK_FALSE(loaded->session.circleResidency);
    CHECK(loaded->session.activeRadius == 0);
    CHECK(loaded->session.residentRadius == 2);
    {
        RawDatabase raw(file.path.c_str());
        CHECK(raw.Text("SELECT format_version FROM metadata") == "7");
        CHECK(raw.Text("SELECT payload FROM world_state") == originalSnapshot);
    }
    REQUIRE(database.Begin());
    SECTION("snapshot checkpoint") {
        loaded->session.circleResidency = CircleResidency{78};
        REQUIRE(database.WriteSnapshot(*loaded));
    }
    SECTION("chunk-only checkpoint") { REQUIRE(database.WriteChunk(SampleChunk())); }
    SECTION("empty checkpoint") {}
    REQUIRE(database.Commit());
    {
        RawDatabase raw(file.path.c_str());
        CHECK(raw.Text("SELECT format_version FROM metadata") == "10");
        CHECK(raw.Text("SELECT payload FROM world_state").find("\"circle_residency\":") != std::string::npos);
        CHECK(raw.Text("SELECT payload FROM chunks") != originalChunk);
        CHECK(raw.Text("SELECT payload FROM chunks").find("\"controller\":") != std::string::npos);
        CHECK(raw.Text("SELECT payload FROM player_memories") == originalMemory);
    }
    SaveDatabase observer(TestContent());
    auto upgraded = observer.Open(file.path.c_str());
    REQUIRE(upgraded);
    CHECK(upgraded->session.circleResidency == loaded->session.circleResidency);
    CHECK(upgraded->session.activeRadius == 0);
    CHECK(upgraded->session.residentRadius == 2);
    // The original connection must use the new decoder after its successful commit.
    REQUIRE(database.Begin());
    REQUIRE(database.Commit());
}

TEST_CASE("Failed v7 migration keeps durable version and permits retry on the same connection", "[save][database][migration]") {
    SaveFile file;
    PopulateV7(file.path.c_str());
    SaveDatabase database(TestContent());
    auto loaded = database.Open(file.path.c_str());
    REQUIRE(loaded);
    std::string originalSnapshot;
    std::string originalChunk;
    std::string originalMemory;
    {
        RawDatabase raw(file.path.c_str());
        originalSnapshot = raw.Text("SELECT payload FROM world_state");
        originalChunk = raw.Text("SELECT payload FROM chunks");
        originalMemory = raw.Text("SELECT payload FROM player_memories");
    }
    REQUIRE(database.Begin());
    auto chunk = SampleChunk();
    chunk.tiles[0].damage = 73;
    REQUIRE(database.WriteChunk(chunk));
    SECTION("explicit rollback after new snapshot") {
        loaded->session.circleResidency = CircleResidency{78};
        REQUIRE(database.WriteSnapshot(*loaded));
        database.Rollback();
    }
    SECTION("snapshot encode failure") {
        loaded->session.circleResidency = CircleResidency{-1};
        CHECK(database.WriteSnapshot(*loaded) == eastl::unexpected(SaveError::InvalidData));
    }
    SECTION("checkpoint validation failure after new snapshot") {
        loaded->nextId = 7;
        REQUIRE(database.WriteSnapshot(*loaded));
        CHECK(database.Commit() == eastl::unexpected(SaveError::InvalidData));
    }
    SECTION("migration snapshot SQL failure") {
        // Inject after Open; schema validation intentionally rejects saved triggers.
        // This connection already owns the writer lock, so install before retrying.
        database.Rollback();
        {
            RawDatabase raw(file.path.c_str());
            raw.Exec("CREATE TRIGGER fail_migration BEFORE UPDATE ON world_state "
                "BEGIN SELECT RAISE(ABORT,'injected migration failure'); END");
        }
        CHECK(database.Begin() == eastl::unexpected(SaveError::InvalidData));
        RawDatabase raw(file.path.c_str());
        raw.Exec("DROP TRIGGER fail_migration");
    }
    SECTION("migration chunk SQL failure") {
        database.Rollback();
        {
            RawDatabase raw(file.path.c_str());
            raw.Exec("CREATE TRIGGER fail_migration BEFORE UPDATE ON chunks "
                "BEGIN SELECT RAISE(ABORT,'injected chunk migration failure'); END");
        }
        CHECK(database.Begin() == eastl::unexpected(SaveError::InvalidData));
        RawDatabase raw(file.path.c_str());
        raw.Exec("DROP TRIGGER fail_migration");
    }
    SECTION("migration metadata SQL failure") {
        database.Rollback();
        {
            RawDatabase raw(file.path.c_str());
            raw.Exec("CREATE TRIGGER fail_migration BEFORE UPDATE ON metadata "
                "BEGIN SELECT RAISE(ABORT,'injected migration failure'); END");
        }
        CHECK(database.Begin() == eastl::unexpected(SaveError::InvalidData));
        RawDatabase raw(file.path.c_str());
        raw.Exec("DROP TRIGGER fail_migration");
    }
    SECTION("busy commit after migration SQL") {
        RawDatabase reader(file.path.c_str());
        reader.Exec("BEGIN; SELECT * FROM world_state");
        REQUIRE(database.WriteSnapshot(*loaded));
        CHECK(database.Commit() == eastl::unexpected(SaveError::Busy));
        reader.Exec("ROLLBACK");
    }
    CHECK(database.Commit() == eastl::unexpected(SaveError::InvalidState));
    {
        RawDatabase raw(file.path.c_str());
        CHECK(raw.Text("SELECT format_version FROM metadata") == "7");
        CHECK(raw.Text("SELECT payload FROM world_state") == originalSnapshot);
        CHECK(raw.Text("SELECT payload FROM chunks") == originalChunk);
        CHECK(raw.Text("SELECT payload FROM player_memories") == originalMemory);
        SaveDatabase observer(TestContent());
        auto durable = observer.Open(file.path.c_str());
        REQUIRE(durable);
        CHECK_FALSE(durable->session.circleResidency);
    }
    // No WriteSnapshot here: this also detects leaked transaction codec state.
    REQUIRE(database.Begin());
    REQUIRE(database.WriteChunk(chunk));
    REQUIRE(database.Commit());
    SaveDatabase observer(TestContent());
    auto upgraded = observer.Open(file.path.c_str());
    REQUIRE(upgraded);
    CHECK_FALSE(upgraded->session.circleResidency);
    RawDatabase raw(file.path.c_str());
    CHECK(raw.Text("SELECT format_version FROM metadata") == "10");
}

TEST_CASE("Legacy checkpoints rewrite every chunk before accepting new v10 writes and roll back atomically", "[save][database][migration]") {
    int version = 7;
    SECTION("version seven") { version = 7; }
    SECTION("version eight") { version = 8; }
    SaveFile file;
    {
        SaveDatabase database(TestContent());
        REQUIRE(database.Create(file.path.c_str()));
        REQUIRE(database.Begin());
        REQUIRE(database.WriteChunk(SampleChunk()));
        ChunkArchive remote{};
        remote.position = {3, -2, 1};
        remote.tiles.fill({TestTerrain("floor"), 0});
        remote.actors.push_back({8, {48, -32, 1}, false, 17});
        remote.objects.push_back({9, {48, -32, 1}, false, 23});
        REQUIRE(database.WriteChunk(remote));
        auto snapshot = SampleSnapshot();
        snapshot.nextId = 10;
        snapshot.generationVersion = 2;
        if (version == 8) { snapshot.session.circleResidency = CircleResidency{78}; }
        REQUIRE(database.WriteSnapshot(snapshot));
        REQUIRE(database.Commit());
    }
    std::string originalChunks;
    std::string originalSnapshot;
    {
        RawDatabase raw(file.path.c_str());
        StripV9ChunkFields(raw);
        if (version == 7) {
            raw.Exec("UPDATE world_state SET payload=json_remove(payload,'$.session.circle_residency');"
                "UPDATE metadata SET format_version=7");
        } else { raw.Exec("UPDATE metadata SET format_version=8"); }
        originalChunks = raw.Text("SELECT group_concat(payload,'|') FROM (SELECT payload FROM chunks ORDER BY x,z,y)");
        originalSnapshot = raw.Text("SELECT payload FROM world_state");
    }
    SaveDatabase database(TestContent());
    auto snapshot = database.Open(file.path.c_str());
    REQUIRE(snapshot);
    CHECK(snapshot->generationVersion == 2);
    auto remote = database.ReadChunk({3, -2, 1}, 10);
    REQUIRE(remote);
    REQUIRE(*remote);
    auto npc = ReadAiController((**remote).actors[0].controller);
    REQUIRE(npc);
    REQUIRE(*npc);
    CHECK((*npc)->Kind() == AiControllerKind::Wander);
    CHECK((**remote).objects[0].type == ObjectTypeId{});
    CHECK((**remote).objects[0].state == DataObject{});
    CHECK((**remote).actors[0].combat == CaptureCombatState(MakeCombatState(false)));

    // A failed checkpoint must undo migration of untouched remote rows too.
    REQUIRE(database.Begin());
    auto local = database.ReadChunk({}, 10);
    REQUIRE(local);
    REQUIRE(*local);
    CHECK((**local).actors[0].controller == CaptureAiController(nullptr));
    CHECK((**local).actors[0].combat == CaptureCombatState(MakeCombatState(true)));
    REQUIRE(database.WriteChunk(SampleChunk()));
    auto bad = *snapshot;
    bad.nextId = 9; // Remote ID 9 remains present.
    REQUIRE(database.WriteSnapshot(bad));
    CHECK(database.Commit() == eastl::unexpected(SaveError::InvalidData));
    {
        RawDatabase raw(file.path.c_str());
        CHECK(raw.Text("SELECT format_version FROM metadata") == std::to_string(version));
        CHECK(raw.Text("SELECT group_concat(payload,'|') FROM (SELECT payload FROM chunks ORDER BY x,z,y)") == originalChunks);
        CHECK(raw.Text("SELECT payload FROM world_state") == originalSnapshot);
    }
    // Explicit rollback also resets the effective decoder, permitting old reads.
    REQUIRE(database.Begin());
    database.Rollback();
    remote = database.ReadChunk({3, -2, 1}, 10);
    REQUIRE(remote);
    REQUIRE(*remote);

    REQUIRE(database.Begin());
    auto replacement = SampleChunk();
    replacement.tiles[0].damage = 67;
    REQUIRE(database.WriteChunk(replacement)); // New v10 row must not use legacy decoder at commit.
    REQUIRE(database.Commit());
    {
        RawDatabase raw(file.path.c_str());
        CHECK(raw.Text("SELECT format_version FROM metadata") == "10");
        CHECK(raw.Text("SELECT count(*) FROM chunks WHERE json_type(payload,'$.actors[0].controller')='object'") == "2");
        CHECK(raw.Text("SELECT count(*) FROM chunks WHERE json_type(payload,'$.objects[0].type')='text' AND json_type(payload,'$.objects[0].state')='object'") == "2");
    }
    SaveDatabase observer(TestContent());
    auto upgraded = observer.Open(file.path.c_str());
    REQUIRE(upgraded);
    CHECK(upgraded->generationVersion == 2);
    CHECK(upgraded->session.circleResidency == snapshot->session.circleResidency);
    remote = observer.ReadChunk({3, -2, 1}, 10);
    REQUIRE(remote);
    REQUIRE(*remote);
    CHECK((**remote).actors[0].turns == 17);
    CHECK((**remote).objects[0].updates == 23);
}

TEST_CASE("Version seven rejects new session fields and retains strict session validation", "[save][database][migration]") {
    SaveFile file;
    PopulateV7(file.path.c_str());
    {
        RawDatabase raw(file.path.c_str());
        auto payload = raw.Text("SELECT payload FROM world_state");
        SECTION("new field with null") { Replace(payload, "\"players\":[]", "\"players\":[],\"circle_residency\":null"); }
        SECTION("new field with circle") { Replace(payload, "\"players\":[]", "\"players\":[],\"circle_residency\":{\"radius_tiles\":78}"); }
        SECTION("unknown field") { Replace(payload, "\"players\":[]", "\"players\":[],\"future\":0"); }
        SECTION("duplicate field") { Replace(payload, "\"players\":[]", "\"players\":[],\"players\":[]"); }
        SECTION("invalid square radius") { Replace(payload, "\"resident_radius\":2", "\"resident_radius\":9"); }
        SECTION("v8 metadata with v7 payload") { raw.Exec("UPDATE metadata SET format_version=8"); }
        raw.SetText("UPDATE world_state SET payload=?1", payload);
    }
    SaveDatabase database(TestContent());
    CHECK(database.Open(file.path.c_str()) == eastl::unexpected(SaveError::InvalidData));
}

TEST_CASE("Checkpoint rejects metadata version changes after Open", "[save][database][migration]") {
    SaveFile file;
    PopulateV7(file.path.c_str());
    SaveDatabase database(TestContent());
    REQUIRE(database.Open(file.path.c_str()));
    {
        RawDatabase raw(file.path.c_str());
        raw.Exec("UPDATE metadata SET format_version=8");
    }
    CHECK(database.Begin() == eastl::unexpected(SaveError::InvalidData));
    CHECK(database.Commit() == eastl::unexpected(SaveError::InvalidState));
}

TEST_CASE("Save database rejects cross chunk duplicate IDs on Open and Commit", "[save][database]") {
    SaveFile file;
    Populate(file.path.c_str());
    SECTION("commit scans all chunks") {
        SaveDatabase database(TestContent());
        REQUIRE(database.Open(file.path.c_str()));
        REQUIRE(database.Begin());
        REQUIRE(database.WriteChunk(SampleChunk({1, 0})));
        CHECK(database.Commit() == eastl::unexpected(SaveError::InvalidData));
        auto absent = database.ReadChunk({1, 0}, 8);
        REQUIRE(absent);
        CHECK_FALSE(*absent);
    }
    SECTION("open scans nonresident disk chunks") {
        {
            RawDatabase raw(file.path.c_str());
            auto payload = raw.Text("SELECT payload FROM chunks");
            // All three root positions move to the second chunk, retaining IDs.
            std::size_t offset = 0;
            while ((offset = payload.find("\"x\":0", offset)) != std::string::npos) {
                payload.replace(offset, 5, "\"x\":16");
                offset += 6;
            }
            raw.SetText("INSERT INTO chunks(x,z,y,payload) VALUES(1,0,0,?1)", payload);
        }
        SaveDatabase database(TestContent());
        CHECK(database.Open(file.path.c_str()) == eastl::unexpected(SaveError::InvalidData));
    }
}

TEST_CASE("Save database bounds containment depth on encode and decode", "[save][database]") {
    SaveFile file;
    SaveDatabase database(TestContent());
    REQUIRE(database.Create(file.path.c_str()));
    ChunkArchive chunk{};
    chunk.tiles.fill(Tile{TestTerrain("floor"), 0});
    for (PersistentId id = 1; id <= 64; ++id) {
        chunk.items.push_back({id, TestItem("backpack"),
            id == 1 ? LocationKind::ground : LocationKind::item, {}, id - 1});
    }
    SaveSnapshot snapshot;
    snapshot.nextId = 100;
    REQUIRE(database.Begin());
    REQUIRE(database.WriteChunk(chunk));
    REQUIRE(database.WriteSnapshot(snapshot));
    REQUIRE(database.Commit());
    auto loaded = database.ReadChunk({0, 0}, 100);
    REQUIRE(loaded);
    REQUIRE(*loaded);
    CHECK((**loaded).items.size() == 64);
    REQUIRE(database.Begin());
    chunk.items.push_back({65, TestItem("backpack"), LocationKind::item, {}, 64});
    CHECK(database.WriteChunk(chunk) == eastl::unexpected(SaveError::InvalidData));
    {
        RawDatabase raw(file.path.c_str());
        auto payload = raw.Text("SELECT payload FROM chunks");
        Replace(payload, "\"id\":\"64\",\"type\":\"backpack\",\"state\":{},\"installation\":\"\",\"contents\":[]",
            "\"id\":\"64\",\"type\":\"backpack\",\"state\":{},\"installation\":\"\",\"contents\":[{\"id\":\"65\",\"type\":\"backpack\",\"state\":{},\"installation\":\"\",\"contents\":[]}]");
        raw.SetText("UPDATE chunks SET payload=?1", payload);
    }
    CHECK(database.ReadChunk({0, 0}, 100) == eastl::unexpected(SaveError::InvalidData));
    SaveDatabase reopened(TestContent());
    CHECK(reopened.Open(file.path.c_str()) == eastl::unexpected(SaveError::InvalidData));
}

TEST_CASE("Save database validates flat archive owners before encoding", "[save][database]") {
    SaveFile file;
    Populate(file.path.c_str());
    SaveDatabase database(TestContent());
    REQUIRE(database.Open(file.path.c_str()));
    auto chunk = SampleChunk();
    SECTION("missing owner") { chunk.items[0].owner = 100; }
    SECTION("owner type mismatch") { chunk.items[0].owner = 2; }
    SECTION("ground owner") { chunk.items.back().owner = 1; }
    SECTION("item order") { chunk.items[0].location = LocationKind::item; chunk.items[0].owner = 4; }
    SECTION("noncontainer") { chunk.items[0].type = TestItem("rock"); }
    SECTION("duplicate ID") { chunk.items[0].id = 1; }
    SECTION("duplicate actor position") { chunk.actors.push_back({20, {}, false, 0}); }
    SECTION("duplicate object position") { chunk.objects.push_back({20, {}, false, 0}); }
    SECTION("ground wrong chunk") { chunk.items.back().position = {16, 0}; }
    SECTION("ground wrong layer") { chunk.items.back().position.y = 1; }
    SECTION("actor wrong layer") { chunk.actors[0].position.y = 1; }
    SECTION("object wrong layer") { chunk.objects[0].position.y = 1; }
    SECTION("chunk Y out of range") { chunk.position.y = 11; }
    SECTION("unknown item type") { chunk.items[0].type = ItemTypeId{UINT32_MAX}; }
    SECTION("unknown location") { chunk.items[0].location = static_cast<LocationKind>(255); }
    SECTION("unknown terrain") { chunk.tiles[0].terrain = TerrainId{UINT32_MAX}; }
    SECTION("invalid zero terrain") { chunk.tiles[0] = Tile{}; }
    SECTION("zero actor speed") { chunk.actors[0].speed = 0; }
    SECTION("zero object interval") { chunk.objects[0].updateInterval = 0; }
    SECTION("object delay exceeds interval") { chunk.objects[0].remainingTicks = 218; }
    REQUIRE(database.Begin());
    CHECK(database.WriteChunk(chunk) == eastl::unexpected(SaveError::InvalidData));
    CHECK(database.Commit() == eastl::unexpected(SaveError::InvalidState));
}

TEST_CASE("Save database rejects zero pacing rate before encoding", "[save][database][timing]") {
    SaveFile file;
    Populate(file.path.c_str());
    SaveDatabase database(TestContent());
    REQUIRE(database.Open(file.path.c_str()));
    REQUIRE(database.Begin());
    auto snapshot = SampleSnapshot();
    snapshot.session.pacing.ticksPerSecond = 0;
    CHECK(database.WriteSnapshot(snapshot) == eastl::unexpected(SaveError::InvalidData));
    CHECK(database.Commit() == eastl::unexpected(SaveError::InvalidState));
}

TEST_CASE("Save database validates player rosters against every chunk", "[save][database][players]") {
    SaveFile file;
    SaveDatabase database(TestContent());
    REQUIRE(database.Create(file.path.c_str()));
    auto host = SampleChunk();
    auto guest = SampleChunk({0, 0, 1});
    guest.actors.clear();
    guest.objects.clear();
    guest.items.clear();
    guest.actors.push_back({8, {0, 0, 1}, true, 7, 100, 42, true});
    guest.items.push_back({9, TestItem("backpack"), LocationKind::actor, {}, 8});
    guest.items.push_back({10, TestItem("rock"), LocationKind::item, {}, 9});
    auto snapshot = SampleSnapshot();
    snapshot.nextId = 11;
    snapshot.session.players.push_back({1, {0, 0}, "host", 0});
    snapshot.session.players.push_back({8, {0, 0, 1}, "guest", UINT64_MAX});
    REQUIRE(database.Begin());
    REQUIRE(database.WriteChunk(host));
    REQUIRE(database.WriteChunk(guest));
    REQUIRE(database.WriteSnapshot(snapshot));
    REQUIRE(database.Commit());
    SaveDatabase observer(TestContent());
    auto opened = observer.Open(file.path.c_str());
    REQUIRE(opened);
    REQUIRE(opened->session.players.size() == 2);
    CHECK(opened->session.players[1].reconnectToken == UINT64_MAX);
    CHECK(opened->session.players[1].position == TileCoord{0, 0, 1});
    auto restored = observer.ReadChunk({0, 0, 1}, 11);
    REQUIRE(restored);
    REQUIRE(*restored);
    CHECK((**restored).actors[0].parked);
    CHECK((**restored).items[1].owner == 9);

    bool checkCommit = false;
    SECTION("zero guest token") { snapshot.session.players[1].reconnectToken = 0; }
    SECTION("duplicate guest token") {
        snapshot.nextId = 12;
        snapshot.session.players.push_back({11, {17, 0}, "third", UINT64_MAX});
    }
    SECTION("duplicate actor") { snapshot.session.players[1].actor = 1; }
    SECTION("missing actor") { snapshot.session.players[1].actor = 7; checkCommit = true; }
    SECTION("position mismatch") { snapshot.session.players[1].position = {17, 0}; checkCommit = true; }
    SECTION("layer mismatch") { snapshot.session.players[1].position.y = 0; checkCommit = true; }
    SECTION("roster Y below world") { snapshot.session.players[1].position.y = -11; }
    SECTION("roster Y above world") { snapshot.session.players[1].position.y = 11; }
    SECTION("guest omitted from roster") { snapshot.session.players.pop_back(); checkCommit = true; }
    SECTION("implicit host cannot omit guest") { snapshot.session.players.clear(); checkCommit = true; }
    SECTION("empty name") { snapshot.session.players[1].name.clear(); }
    SECTION("oversized name") { snapshot.session.players[1].name = String(33, 'x'); }
    REQUIRE(observer.Begin());
    if (checkCommit) {
        REQUIRE(observer.WriteSnapshot(snapshot));
        CHECK(observer.Commit() == eastl::unexpected(SaveError::InvalidData));
    } else {
        CHECK(observer.WriteSnapshot(snapshot) == eastl::unexpected(SaveError::InvalidData));
    }
}

TEST_CASE("Opening a save rejects a parked host even with a matching roster", "[save][database][players]") {
    SaveFile file;
    Populate(file.path.c_str());
    {
        RawDatabase raw(file.path.c_str());
        auto payload = raw.Text("SELECT payload FROM chunks");
        Replace(payload, "\"player\":true,\"parked\":false", "\"player\":true,\"parked\":true");
        raw.SetText("UPDATE chunks SET payload=?1", payload);
    }
    SaveDatabase database(TestContent());
    CHECK(database.Open(file.path.c_str()) == eastl::unexpected(SaveError::InvalidData));
}

TEST_CASE("Save database rejects corrupt roster JSON on open", "[save][database][players]") {
    SaveFile file;
    Populate(file.path.c_str());
    {
        RawDatabase raw(file.path.c_str());
        auto payload = raw.Text("SELECT payload FROM world_state");
        SECTION("missing roster key") { Replace(payload, ",\"players\":[]", ""); }
        SECTION("unknown roster key") { Replace(payload, "\"players\":[]", "\"players\":[],\"extra\":0"); }
        SECTION("invalid roster type") { Replace(payload, "\"players\":[]", "\"players\":{}"); }
        SECTION("roster missing actor") {
            Replace(payload, "\"players\":[]",
                "\"players\":[{\"actor\":\"99\",\"position\":{\"x\":0,\"z\":0,\"y\":0},\"name\":\"guest\",\"reconnect_token\":\"1\"}]");
        }
        raw.SetText("UPDATE world_state SET payload=?1", payload);
    }
    SaveDatabase database(TestContent());
    CHECK(database.Open(file.path.c_str()) == eastl::unexpected(SaveError::InvalidData));
}

TEST_CASE("Save database distinguishes absent chunks from corrupt payloads after opening", "[save][database]") {
    SaveFile file;
    Populate(file.path.c_str());
    SaveDatabase database(TestContent());
    REQUIRE(database.Open(file.path.c_str()));
    {
        RawDatabase raw(file.path.c_str());
        raw.Exec("UPDATE chunks SET payload='null'");
    }
    CHECK(database.ReadChunk({0, 0}, 8) == eastl::unexpected(SaveError::InvalidData));
    auto absent = database.ReadChunk({2, 2}, 8);
    REQUIRE(absent);
    CHECK_FALSE(*absent);
}

TEST_CASE("Save database creation leaves snapshot initialization to the first checkpoint", "[save][database]") {
    SaveFile file;
    {
        SaveDatabase database(TestContent());
        REQUIRE(database.Create(file.path.c_str()));
        REQUIRE(database.Begin());
        CHECK(database.Commit() == eastl::unexpected(SaveError::InvalidData));
        REQUIRE(database.Begin());
        REQUIRE(database.WriteSnapshot(SaveSnapshot{}));
        REQUIRE(database.Commit());
        REQUIRE(database.Begin());
        auto changed = SaveSnapshot{};
        changed.seed = 100;
        REQUIRE(database.WriteSnapshot(changed));
        // Destruction rolls back the unfinished checkpoint.
    }
    SaveDatabase reopened(TestContent());
    auto snapshot = reopened.Open(file.path.c_str());
    REQUIRE(snapshot);
    CHECK(snapshot->seed == 42);
}

TEST_CASE("Save database removes abandoned creation and permits same path retry", "[save][database]") {
    SaveFile file;
    {
        SaveDatabase database(TestContent());
        REQUIRE(database.Create(file.path.c_str()));
        REQUIRE(std::filesystem::exists(file.path));
        SaveDatabase observer(TestContent());
        CHECK(observer.Open(file.path.c_str()) == eastl::unexpected(SaveError::InvalidData));
        CHECK(std::filesystem::exists(file.path));
        SECTION("schema only") {}
        SECTION("unfinished first checkpoint") {
            REQUIRE(database.Begin());
            REQUIRE(database.WriteChunk(SampleChunk()));
            REQUIRE(database.WriteSnapshot(SampleSnapshot()));
        }
        SECTION("failed first commit") {
            REQUIRE(database.Begin());
            REQUIRE(database.WriteChunk(SampleChunk()));
            CHECK(database.Commit() == eastl::unexpected(SaveError::InvalidData));
        }
    }
    CHECK_FALSE(std::filesystem::exists(file.path));
    CHECK_FALSE(std::filesystem::exists(file.path + "-journal"));
    Populate(file.path.c_str());
    SaveDatabase reopened(TestContent());
    REQUIRE(reopened.Open(file.path.c_str()));
}

TEST_CASE("Save database removes creation after a failed first write", "[save][database]") {
    SaveFile file;
    {
        SaveDatabase database(TestContent());
        REQUIRE(database.Create(file.path.c_str()));
        REQUIRE(database.Begin());
        REQUIRE(database.WriteChunk(SampleChunk()));
        auto invalid = SampleSnapshot();
        invalid.nextId = 0;
        CHECK(database.WriteSnapshot(invalid) == eastl::unexpected(SaveError::InvalidData));
        // A failure rolls back but keeps the creator usable until destruction.
        CHECK(std::filesystem::exists(file.path));
        REQUIRE(database.Begin());
        database.Rollback();
    }
    CHECK_FALSE(std::filesystem::exists(file.path));
    Populate(file.path.c_str());
    CHECK(std::filesystem::exists(file.path));
}

TEST_CASE("Save database retains its first committed checkpoint after later failures", "[save][database]") {
    SaveFile file;
    {
        SaveDatabase database(TestContent());
        REQUIRE(database.Create(file.path.c_str()));
        REQUIRE(database.Begin());
        REQUIRE(database.WriteChunk(SampleChunk()));
        REQUIRE(database.WriteSnapshot(SampleSnapshot()));
        REQUIRE(database.Commit());
        REQUIRE(database.Begin());
        auto changed = SampleChunk();
        changed.tiles[0].damage = 99;
        REQUIRE(database.WriteChunk(changed));
        auto invalid = SampleSnapshot();
        SECTION("later write failure") {
            invalid.nextId = 0;
            CHECK(database.WriteSnapshot(invalid) == eastl::unexpected(SaveError::InvalidData));
        }
        SECTION("later commit failure") {
            invalid.nextId = 7;
            REQUIRE(database.WriteSnapshot(invalid));
            CHECK(database.Commit() == eastl::unexpected(SaveError::InvalidData));
        }
    }
    REQUIRE(std::filesystem::exists(file.path));
    SaveDatabase reopened(TestContent());
    auto snapshot = reopened.Open(file.path.c_str());
    REQUIRE(snapshot);
    CHECK(snapshot->nextId == 8);
    auto chunk = reopened.ReadChunk({0, 0}, snapshot->nextId);
    REQUIRE(chunk);
    REQUIRE(*chunk);
    CHECK((**chunk).tiles[0].damage == 0);
}

TEST_CASE("Save database cleans up UTF-8 creation paths", "[save][database]") {
    SaveFile file;
    const auto nativePath = file.directory / std::filesystem::path(u8"world-\u00e9.sqlite");
    const auto utf8Path = nativePath.u8string();
    const std::string path(reinterpret_cast<const char*>(utf8Path.data()), utf8Path.size());
    {
        SaveDatabase database(TestContent());
        REQUIRE(database.Create(path.c_str()));
        REQUIRE(std::filesystem::exists(nativePath));
    }
    CHECK_FALSE(std::filesystem::exists(nativePath));
    Populate(path.c_str());
    CHECK(std::filesystem::exists(nativePath));
    SaveDatabase reopened(TestContent());
    REQUIRE(reopened.Open(path.c_str()));
}

TEST_CASE("Save database never removes a preexisting file after failed creation or opening", "[save][database]") {
    SaveFile file;
    {
        std::ofstream existing(file.path, std::ios::binary);
        REQUIRE(existing.is_open());
        existing << "external data";
        existing.close();
        REQUIRE(existing.good());
    }
    {
        SaveDatabase database(TestContent());
        CHECK(database.Create(file.path.c_str()) == eastl::unexpected(SaveError::AlreadyExists));
        CHECK(database.Open(file.path.c_str()) == eastl::unexpected(SaveError::InvalidData));
    }
    REQUIRE(std::filesystem::exists(file.path));
    std::ifstream existing(file.path, std::ios::binary);
    std::string content;
    std::getline(existing, content);
    CHECK(content == "external data");
}

TEST_CASE("V10 combat state round trips injuries protection and stale pending targets", "[save][database][combat]") {
    SaveFile file;
    auto chunk = SampleChunk();
    auto combat = MakeCombatState(true);
    combat.body.parts[0] = {9876, 0, 0, 17, 0, false};
    combat.body.parts[2] = {0, 123, 456, 0, 0, true};
    combat.body.parts[3] = {9877, 123, 0, 0, 789, false};
    combat.injury.mendedHp = 60;
    combat.style = CombatStyle::Aggressive;
    combat.faction = CombatFaction::Undead;
    combat.action = {ActorActionKind::MeleeWindup, UINT64_MAX,
        static_cast<std::uint32_t>(CombatMove::HeavyAttack), UINT64_MAX, 30};
    SECTION("heavy windup") {}
    SECTION("aiming") { combat.action = {ActorActionKind::Aiming, UINT64_MAX}; }
    combat.stamina = 42;
    combat.staminaElapsed = 9;
    combat.reactionCooldown = UINT64_MAX;
    combat.heavyCooldown = UINT64_MAX - 1;
    combat.protection[2][3] = UINT32_MAX;
    chunk.actors[0].combat = CaptureCombatState(combat);
    {
        SaveDatabase database(TestContent());
        REQUIRE(database.Create(file.path.c_str()));
        REQUIRE(database.Begin());
        REQUIRE(database.WriteChunk(chunk));
        REQUIRE(database.WriteSnapshot(SampleSnapshot()));
        REQUIRE(database.Commit());
    }
    SaveDatabase database(TestContent());
    REQUIRE(database.Open(file.path.c_str()));
    auto loaded = database.ReadChunk({}, 8);
    REQUIRE(loaded);
    REQUIRE(*loaded);
    const auto restored = ReadCombatState((**loaded).actors[0].combat);
    REQUIRE(restored);
    CHECK(*restored == combat);
    REQUIRE(database.Begin());
    REQUIRE(database.Commit());
}

TEST_CASE("V10 requires strict valid combat envelopes on disk and before writing", "[save][database][combat]") {
    SaveFile file;
    Populate(file.path.c_str());
    SaveError expected = SaveError::InvalidData;
    {
        RawDatabase raw(file.path.c_str());
        SECTION("missing combat") { raw.Exec("UPDATE chunks SET payload=json_remove(payload,'$.actors[0].combat')"); }
        SECTION("empty combat") { raw.Exec("UPDATE chunks SET payload=json_set(payload,'$.actors[0].combat',json('{}'))"); }
        SECTION("combat list") { raw.Exec("UPDATE chunks SET payload=json_set(payload,'$.actors[0].combat',json('[]'))"); }
        SECTION("unknown nested field") { raw.Exec("UPDATE chunks SET payload=json_set(payload,'$.actors[0].combat.action.extra',0)"); }
        SECTION("missing nested field") { raw.Exec("UPDATE chunks SET payload=json_remove(payload,'$.actors[0].combat.profile.regeneration')"); }
        SECTION("overflow before cast") { raw.Exec("UPDATE chunks SET payload=json_set(payload,'$.actors[0].combat.accuracy',256)"); }
        SECTION("invalid body") { raw.Exec("UPDATE chunks SET payload=json_set(payload,'$.actors[0].combat.body.parts[0].hp',1)"); }
        SECTION("future combat version") {
            raw.Exec("UPDATE chunks SET payload=json_set(payload,'$.actors[0].combat.version',2)");
            expected = SaveError::UnsupportedVersion;
        }
        SECTION("duplicate combat version") {
            auto payload = raw.Text("SELECT payload FROM chunks");
            Replace(payload, "\"combat\":{\"version\":1", "\"combat\":{\"version\":1,\"version\":1");
            raw.SetText("UPDATE chunks SET payload=?1", payload);
        }
        SECTION("invalid in-memory write") {
            SaveDatabase database(TestContent());
            REQUIRE(database.Open(file.path.c_str()));
            auto chunk = SampleChunk();
            chunk.actors[0].combat.SetUnsigned("stamina", 101);
            REQUIRE(database.Begin());
            CHECK(database.WriteChunk(chunk) == eastl::unexpected(SaveError::InvalidData));
            CHECK(database.Commit() == eastl::unexpected(SaveError::InvalidState));
            return;
        }
    }
    SaveDatabase database(TestContent());
    CHECK(database.Open(file.path.c_str()) == eastl::unexpected(expected));
}

TEST_CASE("V9 migration synthesizes combat defaults and preserves existing actor state", "[save][database][combat][migration]") {
    SaveFile file;
    auto chunk = SampleChunk();
    chunk.actors.push_back({8, {1, 0}, false, 77, 137, 456});
    auto controller = MakeAiController(AiControllerKind::Wander);
    REQUIRE(controller);
    chunk.actors[1].controller = CaptureAiController(controller->get());
    chunk.actors[1].light = {4, 2};
    auto snapshot = SampleSnapshot();
    snapshot.nextId = 9;
    {
        SaveDatabase database(TestContent());
        REQUIRE(database.Create(file.path.c_str()));
        REQUIRE(database.Begin());
        REQUIRE(database.WriteChunk(chunk));
        REQUIRE(database.WriteSnapshot(snapshot));
        REQUIRE(database.Commit());
    }
    std::string original;
    {
        RawDatabase raw(file.path.c_str());
        raw.Exec("UPDATE chunks SET payload=json_set(payload,'$.actors',json((SELECT json_group_array(json_remove(value,'$.combat')) FROM json_each(chunks.payload,'$.actors'))));"
            "UPDATE metadata SET format_version=9");
        original = raw.Text("SELECT payload FROM chunks");
    }
    SaveDatabase database(TestContent());
    REQUIRE(database.Open(file.path.c_str()));
    auto loaded = database.ReadChunk({}, 9);
    REQUIRE(loaded);
    REQUIRE(*loaded);
    CHECK((**loaded).actors[0].combat == CaptureCombatState(MakeCombatState(true)));
    CHECK((**loaded).actors[1].combat == CaptureCombatState(MakeCombatState(false)));
    CHECK((**loaded).actors[1].controller == chunk.actors[1].controller);
    CHECK((**loaded).actors[1].light == chunk.actors[1].light);
    CHECK((**loaded).actors[1].turns == 77);
    CHECK((**loaded).actors[1].speed == 137);
    CHECK((**loaded).actors[1].remainingTicks == 456);
    {
        RawDatabase raw(file.path.c_str());
        CHECK(raw.Text("SELECT format_version FROM metadata") == "9");
        CHECK(raw.Text("SELECT payload FROM chunks") == original);
    }
    REQUIRE(database.Begin());
    SECTION("rollback retains legacy format") {
        database.Rollback();
        RawDatabase raw(file.path.c_str());
        CHECK(raw.Text("SELECT format_version FROM metadata") == "9");
        CHECK(raw.Text("SELECT payload FROM chunks") == original);
        REQUIRE(database.Begin());
    }
    SECTION("empty checkpoint") {}
    REQUIRE(database.Commit());
    RawDatabase raw(file.path.c_str());
    CHECK(raw.Text("SELECT format_version FROM metadata") == "10");
    CHECK(raw.Text("SELECT payload FROM chunks").find("\"combat\":") != std::string::npos);
    SaveDatabase observer(TestContent());
    REQUIRE(observer.Open(file.path.c_str()));
    const auto upgraded = observer.ReadChunk({}, 9);
    REQUIRE(upgraded);
    REQUIRE(*upgraded);
    CHECK((**upgraded).actors[1].combat == (**loaded).actors[1].combat);
    CHECK((**upgraded).actors[1].controller == chunk.actors[1].controller);
}

} // namespace
