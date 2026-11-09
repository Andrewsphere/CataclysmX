#include "core/save_database.h"
#include "content_fixture.h"

#include <catch2/catch_test_macros.hpp>
#include <sqlite3.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>

namespace {
using namespace cataclysmx;

struct PerceptionSaveFile {
    std::filesystem::path directory;
    std::string path;
    PerceptionSaveFile() {
        static std::atomic<unsigned> sequence{0};
        directory = std::filesystem::temp_directory_path()
            / ("cataclysmx-perception-save-"
                + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
                + "-" + std::to_string(sequence.fetch_add(1)));
        REQUIRE(std::filesystem::create_directory(directory));
        path = (directory / "world.sqlite").string();
    }
    ~PerceptionSaveFile() {
        std::error_code error;
        std::filesystem::remove_all(directory, error);
    }
};

struct MemorySql {
    sqlite3* db = nullptr;
    explicit MemorySql(const char* path) {
        REQUIRE(sqlite3_open_v2(path, &db, SQLITE_OPEN_READWRITE, nullptr) == SQLITE_OK);
    }
    ~MemorySql() { sqlite3_close(db); }
    void Exec(const char* sql) { REQUIRE(sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK); }
    void Payload(const char* text) {
        sqlite3_stmt* statement = nullptr;
        REQUIRE(sqlite3_prepare_v2(db, "UPDATE player_memories SET payload=?1", -1, &statement, nullptr) == SQLITE_OK);
        REQUIRE(sqlite3_bind_text(statement, 1, text, -1, SQLITE_TRANSIENT) == SQLITE_OK);
        REQUIRE(sqlite3_step(statement) == SQLITE_DONE);
        REQUIRE(sqlite3_finalize(statement) == SQLITE_OK);
    }
};

ChunkArchive PerceptionChunk(const ContentCatalog& content) {
    ChunkArchive chunk{};
    chunk.tiles.fill({content.FindTerrain("floor"), 0});
    chunk.actors.push_back({UINT64_MAX - 1, {}, true, 0});
    chunk.actors.back().light = {255, 12};
    chunk.objects.push_back({1, {1, 0, 0}, false, 0});
    auto& object = chunk.objects.back();
    object.kind = ObjectKind::Lamp;
    object.facing = ObjectFacing::West;
    object.broken = true;
    object.light = {64, 3}; // Disabled emitters retain their configured source.
    return chunk;
}

SaveSnapshot PerceptionSnapshot(const ContentCatalog& content) {
    SaveSnapshot snapshot;
    snapshot.nextId = UINT64_MAX;
    snapshot.skyLight = 17;
    PlayerMemory memory;
    memory.player = UINT64_MAX - 1;
    MemoryChunk chunk;
    chunk.position = {-2, 4, -3}; // Has no corresponding durable live chunk.
    chunk.terrain[0] = content.FindTerrain("floor");
    chunk.terrain[255] = content.FindTerrain("floor");
    chunk.objects.push_back({255, {ObjectKind::Chair, ObjectFacing::South, true, true}});
    memory.chunks.push_back(chunk);
    snapshot.memories.push_back(memory);
    return snapshot; // Generic World saves have neither host nor roster.
}

void SavePerception(const char* path, const ContentCatalog& content) {
    SaveDatabase database(content);
    REQUIRE(database.Create(path));
    REQUIRE(database.Begin());
    REQUIRE(database.WriteChunk(PerceptionChunk(content)));
    REQUIRE(database.WriteSnapshot(PerceptionSnapshot(content)));
    REQUIRE(database.Commit());
}

TEST_CASE("V7 persists sources, semantic identity and independent stale memory", "[save][perception]") {
    PerceptionSaveFile file;
    const auto& content = TestContent();
    SavePerception(file.path.c_str(), content);
    SaveDatabase database(content);
    auto loaded = database.Open(file.path.c_str());
    REQUIRE(loaded);
    const auto expected = PerceptionSnapshot(content);
    CHECK(loaded->skyLight == 17);
    CHECK(loaded->memories == expected.memories);
    auto chunk = database.ReadChunk({}, loaded->nextId);
    REQUIRE(chunk);
    REQUIRE(*chunk);
    CHECK((**chunk).actors[0].light == LightEmission{255, 12});
    const auto& object = (**chunk).objects[0];
    CHECK(object.light == LightEmission{64, 3});
    CHECK(object.kind == ObjectKind::Lamp);
    CHECK(object.facing == ObjectFacing::West);
    CHECK(object.broken);
    CHECK_FALSE(object.enabled);
    auto absent = database.ReadChunk({-2, 4, -3}, loaded->nextId);
    REQUIRE(absent);
    CHECK_FALSE(*absent);

    // Live changes do not rewrite an old observation, even at the same tile.
    auto live = **chunk;
    auto memory = expected.memories[0].chunks[0];
    memory.position = {};
    loaded->memories[0].chunks.push_back(memory);
    live.objects[0].kind = ObjectKind::Table;
    live.tiles[255].terrain = TestTerrain("closed_door");
    REQUIRE(database.Begin());
    REQUIRE(database.WriteChunk(live));
    REQUIRE(database.WriteSnapshot(*loaded));
    REQUIRE(database.Commit());
    SaveDatabase observer(content);
    auto reopened = observer.Open(file.path.c_str());
    REQUIRE(reopened);
    CHECK(reopened->memories == loaded->memories);
    CHECK(reopened->memories[0].chunks[1].terrain[255] == TestTerrain("floor"));
    CHECK(reopened->memories[0].chunks[1].objects[0].appearance.kind == ObjectKind::Chair);
}

TEST_CASE("Remembered terrain resolves stable strings in reordered catalogs", "[save][perception][content]") {
    const ContentSource originalSource{"floor.json5", R"({format_version:1,definitions:[{
        type:'terrain',id:'floor',name:'floor',movement:{passable:true},blocks_sight:false,
        appearance:{symbol:'.',foreground:'white'}}]})"};
    const ContentSource extraSource{"ash.json5", R"({format_version:1,definitions:[{
        type:'terrain',id:'ash',name:'ash',movement:{passable:true},blocks_sight:false,
        appearance:{symbol:'.',foreground:'white'}}]})"};
    const ContentSource sources[] = {originalSource, extraSource};
    auto original = LoadContent({sources, 1});
    auto reordered = LoadContent({sources, 2});
    REQUIRE(original);
    REQUIRE(reordered);
    REQUIRE(original->FindTerrain("floor") != reordered->FindTerrain("floor"));
    PerceptionSaveFile file;
    SavePerception(file.path.c_str(), *original);
    SaveDatabase database(*reordered);
    auto opened = database.Open(file.path.c_str());
    REQUIRE(opened);
    CHECK(opened->memories == PerceptionSnapshot(*reordered).memories);
}

TEST_CASE("Malformed palettes runs and sparse objects are rejected", "[save][perception]") {
    PerceptionSaveFile file;
    SavePerception(file.path.c_str(), TestContent());
    {
        MemorySql raw(file.path.c_str());
        const char* payload = nullptr;
        SECTION("palette zero is unknown") { payload = R"({"palette":["floor"],"runs":[[0,256]],"objects":[]})"; }
        SECTION("all-unobserved chunk") { payload = R"({"palette":[null],"runs":[[0,256]],"objects":[]})"; }
        SECTION("duplicate terrain") { payload = R"({"palette":[null,"floor","floor"],"runs":[[1,256]],"objects":[]})"; }
        SECTION("unknown terrain") { payload = R"({"palette":[null,"removed"],"runs":[[1,256]],"objects":[]})"; }
        SECTION("palette reference") { payload = R"({"palette":[null],"runs":[[1,256]],"objects":[]})"; }
        SECTION("short coverage") { payload = R"({"palette":[null],"runs":[[0,255]],"objects":[]})"; }
        SECTION("long coverage") { payload = R"({"palette":[null],"runs":[[0,257]],"objects":[]})"; }
        SECTION("zero count") { payload = R"({"palette":[null],"runs":[[0,0],[0,256]],"objects":[]})"; }
        SECTION("negative count") { payload = R"({"palette":[null],"runs":[[0,-1]],"objects":[]})"; }
        SECTION("fraction count") { payload = R"({"palette":[null],"runs":[[0,256.0]],"objects":[]})"; }
        SECTION("duplicate field") { payload = R"({"palette":[null],"palette":[null],"runs":[[0,256]]})"; }
        SECTION("unknown field") { payload = R"({"palette":[null],"runs":[[0,256]],"objects":[],"extra":0})"; }
        SECTION("object on unseen terrain") { payload = R"({"palette":[null],"runs":[[0,256]],"objects":[{"tile":0,"kind":1,"facing":0,"active":false,"broken":false}]})"; }
        SECTION("None object") { payload = R"({"palette":[null,"floor"],"runs":[[1,256]],"objects":[{"tile":0,"kind":0,"facing":0,"active":false,"broken":false}]})"; }
        SECTION("invalid facing") { payload = R"({"palette":[null,"floor"],"runs":[[1,256]],"objects":[{"tile":0,"kind":1,"facing":4,"active":false,"broken":false}]})"; }
        SECTION("duplicate object tile") { payload = R"({"palette":[null,"floor"],"runs":[[1,256]],"objects":[{"tile":0,"kind":1,"facing":0,"active":false,"broken":false},{"tile":0,"kind":2,"facing":0,"active":false,"broken":false}]})"; }
        SECTION("unordered objects") { payload = R"({"palette":[null,"floor"],"runs":[[1,256]],"objects":[{"tile":1,"kind":1,"facing":0,"active":false,"broken":false},{"tile":0,"kind":2,"facing":0,"active":false,"broken":false}]})"; }
        REQUIRE(payload);
        raw.Payload(payload);
    }
    SaveDatabase database(TestContent());
    Diagnostics diagnostics;
    CHECK(database.Open(file.path.c_str(), &diagnostics) == eastl::unexpected(SaveError::InvalidData));
    CHECK_FALSE(diagnostics.Empty());
}

TEST_CASE("Memory SQL keys are canonical bounded and refer to player actors", "[save][perception]") {
    PerceptionSaveFile file;
    SavePerception(file.path.c_str(), TestContent());
    {
        MemorySql raw(file.path.c_str());
        SECTION("leading zeros") { raw.Exec("UPDATE player_memories SET player_id='018446744073709551614'"); }
        SECTION("overflow ID") { raw.Exec("UPDATE player_memories SET player_id='18446744073709551616'"); }
        SECTION("signed ID") { raw.Exec("UPDATE player_memories SET player_id='+1'"); }
        SECTION("missing actor") { raw.Exec("UPDATE player_memories SET player_id='2'"); }
        SECTION("object owner") { raw.Exec("UPDATE player_memories SET player_id='1'"); }
        SECTION("chunk outside world") { raw.Exec("UPDATE player_memories SET x=2147483647"); }
        SECTION("invalid layer") { raw.Exec("UPDATE player_memories SET y=-11"); }
        SECTION("fractional coordinate") { raw.Exec("UPDATE player_memories SET z=1.5"); }
    }
    SaveDatabase database(TestContent());
    CHECK(database.Open(file.path.c_str()) == eastl::unexpected(SaveError::InvalidData));
}

TEST_CASE("Memory validation and write failures roll back the entire checkpoint", "[save][perception]") {
    PerceptionSaveFile file;
    SavePerception(file.path.c_str(), TestContent());
    SaveDatabase database(TestContent());
    REQUIRE(database.Open(file.path.c_str()));
    auto snapshot = PerceptionSnapshot(TestContent());
    auto chunk = PerceptionChunk(TestContent());
    chunk.tiles[0].damage = 99;
    snapshot.skyLight = 0;
    bool failsAtCommit = false;
    SECTION("missing durable owner") { snapshot.memories[0].player = 2; failsAtCommit = true; }
    SECTION("duplicate memory owner") { snapshot.memories.push_back(snapshot.memories[0]); }
    SECTION("duplicate chunk") { snapshot.memories[0].chunks.push_back(snapshot.memories[0].chunks[0]); }
    SECTION("all-unobserved chunk") {
        snapshot.memories[0].chunks[0].terrain.fill({});
        snapshot.memories[0].chunks[0].objects.clear();
    }
    SECTION("unordered chunks") {
        auto earlier = snapshot.memories[0].chunks[0];
        earlier.position.x -= 1;
        snapshot.memories[0].chunks.push_back(earlier);
    }
    SECTION("invalid runtime terrain") { snapshot.memories[0].chunks[0].terrain[0] = TerrainId{UINT32_MAX}; }
    SECTION("known tile beyond coordinate boundary") {
        snapshot.memories[0].chunks[0].position.x = world_coordinate_limit / chunk_size;
    }
    SECTION("unseen object") { snapshot.memories[0].chunks[0].terrain[255] = {}; }
    SECTION("SQL memory failure") {
        MemorySql raw(file.path.c_str());
        raw.Exec("CREATE TRIGGER fail_memory BEFORE INSERT ON player_memories BEGIN SELECT RAISE(ABORT,'memory failure'); END");
    }
    REQUIRE(database.Begin());
    REQUIRE(database.WriteChunk(chunk));
    if (failsAtCommit) {
        REQUIRE(database.WriteSnapshot(snapshot));
        CHECK(database.Commit() == eastl::unexpected(SaveError::InvalidData));
    } else {
        CHECK(database.WriteSnapshot(snapshot) == eastl::unexpected(SaveError::InvalidData));
    }
    CHECK(database.Commit() == eastl::unexpected(SaveError::InvalidState));
    {
        MemorySql raw(file.path.c_str());
        raw.Exec("DROP TRIGGER IF EXISTS fail_memory");
    }
    SaveDatabase observer(TestContent());
    auto original = observer.Open(file.path.c_str());
    REQUIRE(original);
    CHECK(original->skyLight == 17);
    CHECK(original->memories == PerceptionSnapshot(TestContent()).memories);
    auto originalChunk = observer.ReadChunk({}, original->nextId);
    REQUIRE(originalChunk);
    REQUIRE(*originalChunk);
    CHECK((**originalChunk).tiles[0].damage == 0);
}

TEST_CASE("Duplicate memory SQL rows are rejected at commit after external damage", "[save][perception]") {
    PerceptionSaveFile file;
    SavePerception(file.path.c_str(), TestContent());
    SaveDatabase database(TestContent());
    REQUIRE(database.Open(file.path.c_str()));
    {
        MemorySql raw(file.path.c_str());
        raw.Exec("ALTER TABLE player_memories RENAME TO old_memories;"
            "CREATE TABLE player_memories(player_id TEXT,x INTEGER,z INTEGER,y INTEGER,payload TEXT);"
            "INSERT INTO player_memories SELECT * FROM old_memories;"
            "INSERT INTO player_memories SELECT * FROM old_memories;"
            "DROP TABLE old_memories;");
    }
    REQUIRE(database.Begin());
    CHECK(database.Commit() == eastl::unexpected(SaveError::InvalidData));
}

TEST_CASE("Memory owners include parked unloaded players and decode in numeric ID order", "[save][perception]") {
    PerceptionSaveFile file;
    SaveDatabase database(TestContent());
    REQUIRE(database.Create(file.path.c_str()));
    auto snapshot = PerceptionSnapshot(TestContent());
    auto chunk = PerceptionChunk(TestContent());
    auto lowMemory = snapshot.memories[0];
    lowMemory.player = 2;
    auto middleMemory = lowMemory;
    middleMemory.player = 10;
    snapshot.memories.insert(snapshot.memories.begin(), middleMemory);
    snapshot.memories.insert(snapshot.memories.begin(), lowMemory);
    ChunkArchive unloaded{};
    unloaded.position = {100, 100, 7};
    unloaded.tiles.fill({TestTerrain("floor"), 0});
    unloaded.actors.push_back({2, {1600, 1600, 7}, true, 0, 100, 0, true});
    unloaded.actors.push_back({10, {1600, 1600, 7}, true, 0, 100, 0, true});
    REQUIRE(database.Begin());
    REQUIRE(database.WriteChunk(chunk));
    REQUIRE(database.WriteChunk(unloaded));
    REQUIRE(database.WriteSnapshot(snapshot));
    REQUIRE(database.Commit());
    SaveDatabase observer(TestContent());
    auto opened = observer.Open(file.path.c_str());
    REQUIRE(opened);
    CHECK(opened->session.players.empty());
    CHECK(opened->memories == snapshot.memories);
    // Removing observations replaces the old rows rather than leaving stale owners.
    snapshot.memories.clear();
    REQUIRE(database.Begin());
    REQUIRE(database.WriteSnapshot(snapshot));
    REQUIRE(database.Commit());
    SaveDatabase emptyObserver(TestContent());
    auto empty = emptyObserver.Open(file.path.c_str());
    REQUIRE(empty);
    CHECK(empty->memories.empty());
}

TEST_CASE("Unknown remembered terrain includes player and chunk diagnostic context", "[save][perception][content]") {
    PerceptionSaveFile file;
    SavePerception(file.path.c_str(), TestContent());
    {
        MemorySql raw(file.path.c_str());
        raw.Payload(R"({"palette":[null,"removed_memory_terrain"],"runs":[[1,256]],"objects":[]})");
    }
    SaveDatabase database(TestContent());
    Diagnostics diagnostics;
    CHECK(database.Open(file.path.c_str(), &diagnostics) == eastl::unexpected(SaveError::InvalidData));
    REQUIRE(diagnostics.Frames().size() >= 2);
    CHECK(diagnostics.Frames()[0].message.find("removed_memory_terrain") != String::npos);
    REQUIRE(diagnostics.Frames()[1].location);
    CHECK(diagnostics.Frames()[1].location->path == "player_memories[18446744073709551614][-2,4,-3]");
}

TEST_CASE("Empty memory groups validate their owner before normalization", "[save][perception]") {
    PerceptionSaveFile file;
    SavePerception(file.path.c_str(), TestContent());
    SaveDatabase database(TestContent());
    REQUIRE(database.Open(file.path.c_str()));
    auto snapshot = PerceptionSnapshot(TestContent());
    snapshot.memories[0].chunks.clear();
    bool validOwner = true;
    SECTION("durable player") {}
    SECTION("missing owner") { snapshot.memories[0].player = 2; validOwner = false; }
    REQUIRE(database.Begin());
    REQUIRE(database.WriteSnapshot(snapshot));
    if (validOwner) {
        REQUIRE(database.Commit());
        SaveDatabase observer(TestContent());
        auto opened = observer.Open(file.path.c_str());
        REQUIRE(opened);
        CHECK(opened->memories.empty());
    } else {
        CHECK(database.Commit() == eastl::unexpected(SaveError::InvalidData));
    }
}

TEST_CASE("Emitter bounds and zero pairing are validated before writes", "[save][perception]") {
    PerceptionSaveFile file;
    SaveDatabase database(TestContent());
    REQUIRE(database.Create(file.path.c_str()));
    auto chunk = PerceptionChunk(TestContent());
    SECTION("actor range") { chunk.actors[0].light.range = 13; }
    SECTION("actor missing intensity") { chunk.actors[0].light.intensity = 0; }
    SECTION("actor missing range") { chunk.actors[0].light.range = 0; }
    SECTION("disabled object range") { chunk.objects[0].light.range = 13; }
    SECTION("object missing intensity") { chunk.objects[0].light.intensity = 0; }
    SECTION("None live object") { chunk.objects[0].kind = ObjectKind::None; }
    SECTION("unknown live kind") { chunk.objects[0].kind = static_cast<ObjectKind>(255); }
    SECTION("unknown live facing") { chunk.objects[0].facing = static_cast<ObjectFacing>(255); }
    REQUIRE(database.Begin());
    CHECK(database.WriteChunk(chunk) == eastl::unexpected(SaveError::InvalidData));
}

TEST_CASE("All-unobserved memory rows fail both database and world loading", "[save][perception]") {
    PerceptionSaveFile file;
    SavePerception(file.path.c_str(), TestContent());
    {
        MemorySql raw(file.path.c_str());
        raw.Payload(R"({"palette":[null],"runs":[[0,256]],"objects":[]})");
    }
    SaveDatabase database(TestContent());
    CHECK(database.Open(file.path.c_str()) == eastl::unexpected(SaveError::InvalidData));
    World world(TestContent());
    CHECK(world.LoadSave(file.path.c_str()) == eastl::unexpected(SaveError::InvalidData));
}

} // namespace
