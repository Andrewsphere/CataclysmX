#include "core/save_database.h"

#include <catch2/catch_test_macros.hpp>
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
    chunk.position = position;
    const TileCoord tile{position.x * chunk_size, position.z * chunk_size};
    chunk.tiles[255] = {Terrain::closed_door, 255};
    chunk.actors.push_back({1, tile, true, UINT64_MAX});
    chunk.objects.push_back({2, tile, true, UINT64_MAX - 1});
    chunk.items.push_back({3, ItemKind::backpack, LocationKind::actor, {}, 1});
    chunk.items.push_back({4, ItemKind::backpack, LocationKind::item, {}, 3});
    chunk.items.push_back({5, ItemKind::bandage, LocationKind::item, {}, 4});
    chunk.items.push_back({6, ItemKind::rock, LocationKind::object, {}, 2});
    chunk.items.push_back({7, ItemKind::rock, LocationKind::ground, tile, 0});
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
    return snapshot;
}

void Populate(const char* path) {
    SaveDatabase database;
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

TEST_CASE("Save database refuses existing files and never creates on Open", "[save][database]") {
    SaveFile file;
    SaveDatabase database;
    CHECK(database.Open(file.path.c_str()) == eastl::unexpected(SaveError::io));
    CHECK_FALSE(std::filesystem::exists(file.path));
    {
        std::ofstream empty(file.path, std::ios::binary);
        REQUIRE(empty.is_open());
        empty.close();
        REQUIRE(empty.good());
    }
    CHECK(database.Create(file.path.c_str()) == eastl::unexpected(SaveError::already_exists));
    CHECK(std::filesystem::file_size(file.path) == 0);
    CHECK(database.Open(file.path.c_str()) == eastl::unexpected(SaveError::invalid_data));
    CHECK(database.Begin() == eastl::unexpected(SaveError::not_open));
    CHECK(database.Create(file.directory.string().c_str()) == eastl::unexpected(SaveError::already_exists));
}

TEST_CASE("Save database stores nested standard JSON and round trips full width values", "[save][database]") {
    SaveFile file;
    auto chunk = SampleChunk({-1, -1});
    chunk.items.back().id = UINT64_MAX - 1;
    auto snapshot = SampleSnapshot();
    snapshot.nextId = UINT64_MAX;
    snapshot.session.playerPosition = {-16, -16};
    {
        SaveDatabase database;
        REQUIRE(database.Create(file.path.c_str()));
        CHECK(database.WriteChunk(chunk) == eastl::unexpected(SaveError::invalid_state));
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
        CHECK(payload.find("\"position\":{\"x\":-16,\"z\":-16}") != std::string::npos);
        CHECK(payload.find("\"owner\"") == std::string::npos);
        raw.Exec("UPDATE metadata SET game_version='future informational label'");
    }
    SaveDatabase database;
    auto opened = database.Open(file.path.c_str());
    REQUIRE(opened);
    CHECK(opened->seed == UINT32_MAX);
    CHECK(opened->nextId == UINT64_MAX);
    CHECK(opened->session.tick == UINT64_MAX);
    CHECK(opened->session.autosaveInterval == UINT64_MAX - 1);
    CHECK(opened->session.rngState == UINT32_MAX);
    auto loaded = database.ReadChunk({-1, -1}, opened->nextId);
    REQUIRE(loaded);
    REQUIRE(*loaded);
    CHECK((**loaded).tiles == chunk.tiles);
    REQUIRE((**loaded).actors.size() == 1);
    CHECK((**loaded).actors[0].turns == UINT64_MAX);
    CHECK((**loaded).objects[0].updates == UINT64_MAX - 1);
    REQUIRE((**loaded).items.size() == 5);
    CHECK((**loaded).items[2].owner == 4);
    CHECK((**loaded).items[2].location == LocationKind::item);
    CHECK((**loaded).items.back().id == UINT64_MAX - 1);
    auto absent = database.ReadChunk({1, 0}, opened->nextId);
    REQUIRE(absent);
    CHECK_FALSE(*absent);
}

TEST_CASE("Save database rolls back failed checkpoints including previously written chunks", "[save][database]") {
    SaveFile file;
    Populate(file.path.c_str());
    SaveDatabase database;
    REQUIRE(database.Open(file.path.c_str()));
    auto chunk = SampleChunk();
    chunk.tiles[0].damage = 41;
    REQUIRE(database.Begin());
    REQUIRE(database.WriteChunk(chunk));
    auto bad = SampleSnapshot();
    bad.nextId = 7; // ID 7 is present: only full checkpoint validation can reject it.
    REQUIRE(database.WriteSnapshot(bad));
    CHECK(database.Commit() == eastl::unexpected(SaveError::invalid_data));
    CHECK(database.Commit() == eastl::unexpected(SaveError::invalid_state));
    auto original = database.ReadChunk({0, 0}, 8);
    REQUIRE(original);
    REQUIRE(*original);
    CHECK((**original).tiles[0].damage == 0);

    REQUIRE(database.Begin());
    REQUIRE(database.WriteChunk(chunk));
    chunk.items[0].owner = 999;
    CHECK(database.WriteChunk(chunk) == eastl::unexpected(SaveError::invalid_data));
    CHECK(database.Commit() == eastl::unexpected(SaveError::invalid_state));
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
    SaveDatabase observer;
    REQUIRE(observer.Open(file.path.c_str()));
    original = observer.ReadChunk({0, 0}, 8);
    REQUIRE(original);
    REQUIRE(*original);
    CHECK((**original).tiles[0].damage == 0);
}

TEST_CASE("Save database reports writer and commit contention as busy", "[save][database]") {
    SaveFile file;
    Populate(file.path.c_str());
    SaveDatabase database;
    REQUIRE(database.Open(file.path.c_str()));
    RawDatabase lock(file.path.c_str());
    lock.Exec("BEGIN IMMEDIATE");
    CHECK(database.Begin() == eastl::unexpected(SaveError::busy));
    lock.Exec("ROLLBACK");

    lock.Exec("BEGIN; SELECT * FROM chunks");
    REQUIRE(database.Begin());
    auto chunk = SampleChunk();
    chunk.tiles[0].damage = 90;
    REQUIRE(database.WriteChunk(chunk));
    CHECK(database.Commit() == eastl::unexpected(SaveError::busy));
    lock.Exec("ROLLBACK");
    auto original = database.ReadChunk({0, 0}, 8);
    REQUIRE(original);
    REQUIRE(*original);
    CHECK((**original).tiles[0].damage == 0);
}

TEST_CASE("Save database rolls back earlier SQL writes after a SQLite failure", "[save][database]") {
    SaveFile file;
    Populate(file.path.c_str());
    SaveDatabase database;
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
    CHECK(database.WriteSnapshot(SampleSnapshot()) == eastl::unexpected(SaveError::invalid_data));
    CHECK(database.Commit() == eastl::unexpected(SaveError::invalid_state));
    auto original = database.ReadChunk({0, 0}, 8);
    REQUIRE(original);
    REQUIRE(*original);
    CHECK((**original).tiles[0].damage == 0);
    {
        RawDatabase raw(file.path.c_str());
        raw.Exec("DROP TRIGGER fail_snapshot");
    }
    SaveDatabase observer;
    auto snapshot = observer.Open(file.path.c_str());
    REQUIRE(snapshot);
    CHECK(snapshot->session.tick == UINT64_MAX);
}

TEST_CASE("Save database rejects unknown versions and incomplete or extended schemas", "[save][database]") {
    SaveFile file;
    Populate(file.path.c_str());
    {
        RawDatabase raw(file.path.c_str());
        SECTION("future version") { raw.Exec("UPDATE metadata SET format_version=2"); }
        SECTION("old version") { raw.Exec("UPDATE metadata SET format_version=0"); }
    }
    SaveDatabase database;
    CHECK(database.Open(file.path.c_str()) == eastl::unexpected(SaveError::unsupported_version));
}

TEST_CASE("Save database rejects damaged metadata and schemas", "[save][database]") {
    SaveFile file;
    Populate(file.path.c_str());
    {
        RawDatabase raw(file.path.c_str());
        SECTION("missing metadata") { raw.Exec("DELETE FROM metadata"); }
        SECTION("extra columns") { raw.Exec("ALTER TABLE chunks ADD COLUMN future TEXT"); }
        SECTION("missing world_state") { raw.Exec("DELETE FROM world_state"); }
        SECTION("unknown tables") { raw.Exec("CREATE TABLE future(value TEXT)"); }
        SECTION("malformed chunk") { raw.Exec("UPDATE chunks SET payload='{}'"); }
    }
    SaveDatabase database;
    CHECK(database.Open(file.path.c_str()) == eastl::unexpected(SaveError::invalid_data));
}

TEST_CASE("Save database strictly validates nested JSON and coordinates", "[save][database]") {
    SaveFile file;
    Populate(file.path.c_str());
    {
        RawDatabase raw(file.path.c_str());
        auto payload = raw.Text("SELECT payload FROM chunks");
        SECTION("duplicate root key") { payload.insert(1, "\"actors\":[],"); }
        SECTION("duplicate nested key") { Replace(payload, "\"kind\":\"backpack\"", "\"kind\":\"backpack\",\"kind\":\"backpack\""); }
        SECTION("JSON5 comment") { payload.insert(1, "/* comment */"); }
        SECTION("numeric ID") { Replace(payload, "\"id\":\"3\"", "\"id\":3"); }
        SECTION("overflow ID") { Replace(payload, "\"id\":\"3\"", "\"id\":\"18446744073709551616\""); }
        SECTION("noncanonical ID") { Replace(payload, "\"id\":\"3\"", "\"id\":\"03\""); }
        SECTION("zero ID") { Replace(payload, "\"id\":\"3\"", "\"id\":\"0\""); }
        SECTION("duplicate ID across entity types") { Replace(payload, "\"id\":\"3\"", "\"id\":\"2\""); }
        SECTION("unknown item") { Replace(payload, "\"kind\":\"bandage\"", "\"kind\":\"unknown\""); }
        SECTION("noncontainer children") { Replace(payload, "\"kind\":\"backpack\"", "\"kind\":\"rock\""); }
        SECTION("unknown terrain") { Replace(payload, "\"terrain\":\"floor\"", "\"terrain\":\"water\""); }
        SECTION("tile count") { Replace(payload, "{\"terrain\":\"floor\",\"damage\":0},", ""); }
        SECTION("damage overflow") { Replace(payload, "\"damage\":0", "\"damage\":256"); }
        SECTION("wrong chunk position") { Replace(payload, "\"x\":0", "\"x\":16"); }
        SECTION("position overflow") { Replace(payload, "\"x\":0", "\"x\":2147483648"); }
        SECTION("missing ground position") {
            const auto ground = payload.find("\"ground_items\"");
            REQUIRE(ground != std::string::npos);
            const auto position = payload.find(",\"position\":{\"x\":0,\"z\":0}", ground);
            REQUIRE(position != std::string::npos);
            payload.erase(position, std::string(",\"position\":{\"x\":0,\"z\":0}").size());
        }
        raw.SetText("UPDATE chunks SET payload=?1", payload);
    }
    SaveDatabase database;
    CHECK(database.Open(file.path.c_str()) == eastl::unexpected(SaveError::invalid_data));
}

TEST_CASE("Save database validates session, counters and generator version", "[save][database]") {
    SaveFile file;
    Populate(file.path.c_str());
    SaveError expected = SaveError::invalid_data;
    {
        RawDatabase raw(file.path.c_str());
        auto payload = raw.Text("SELECT payload FROM world_state");
        SECTION("unknown generator") {
            Replace(payload, "\"generation_version\":1", "\"generation_version\":2");
            expected = SaveError::unsupported_version;
        }
        SECTION("numeric counter") { Replace(payload, "\"tick\":\"18446744073709551615\"", "\"tick\":1"); }
        SECTION("invalid radius") { Replace(payload, "\"resident_radius\":2", "\"resident_radius\":9"); }
        SECTION("invalid next ID") { Replace(payload, "\"next_id\":\"8\"", "\"next_id\":\"7\""); }
        SECTION("player is an object") { Replace(payload, "\"player\":\"1\"", "\"player\":\"2\""); }
        SECTION("player position mismatch") { Replace(payload, "\"x\":0", "\"x\":1"); }
        SECTION("duplicate session key") { Replace(payload, "\"rng_state\":4294967295", "\"rng_state\":0,\"rng_state\":1"); }
        raw.SetText("UPDATE world_state SET payload=?1", payload);
    }
    SaveDatabase database;
    CHECK(database.Open(file.path.c_str()) == eastl::unexpected(expected));
}

TEST_CASE("Save database rejects cross chunk duplicate IDs on Open and Commit", "[save][database]") {
    SaveFile file;
    Populate(file.path.c_str());
    SECTION("commit scans all chunks") {
        SaveDatabase database;
        REQUIRE(database.Open(file.path.c_str()));
        REQUIRE(database.Begin());
        REQUIRE(database.WriteChunk(SampleChunk({1, 0})));
        CHECK(database.Commit() == eastl::unexpected(SaveError::invalid_data));
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
            raw.SetText("INSERT INTO chunks(x,z,payload) VALUES(1,0,?1)", payload);
        }
        SaveDatabase database;
        CHECK(database.Open(file.path.c_str()) == eastl::unexpected(SaveError::invalid_data));
    }
}

TEST_CASE("Save database bounds containment depth on encode and decode", "[save][database]") {
    SaveFile file;
    SaveDatabase database;
    REQUIRE(database.Create(file.path.c_str()));
    ChunkArchive chunk{};
    for (PersistentId id = 1; id <= 64; ++id) {
        chunk.items.push_back({id, ItemKind::backpack,
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
    chunk.items.push_back({65, ItemKind::backpack, LocationKind::item, {}, 64});
    CHECK(database.WriteChunk(chunk) == eastl::unexpected(SaveError::invalid_data));
    {
        RawDatabase raw(file.path.c_str());
        auto payload = raw.Text("SELECT payload FROM chunks");
        Replace(payload, "\"id\":\"64\",\"kind\":\"backpack\",\"contents\":[]",
            "\"id\":\"64\",\"kind\":\"backpack\",\"contents\":[{\"id\":\"65\",\"kind\":\"backpack\",\"contents\":[]}]");
        raw.SetText("UPDATE chunks SET payload=?1", payload);
    }
    CHECK(database.ReadChunk({0, 0}, 100) == eastl::unexpected(SaveError::invalid_data));
    SaveDatabase reopened;
    CHECK(reopened.Open(file.path.c_str()) == eastl::unexpected(SaveError::invalid_data));
}

TEST_CASE("Save database validates flat archive owners before encoding", "[save][database]") {
    SaveFile file;
    Populate(file.path.c_str());
    SaveDatabase database;
    REQUIRE(database.Open(file.path.c_str()));
    auto chunk = SampleChunk();
    SECTION("missing owner") { chunk.items[0].owner = 100; }
    SECTION("owner type mismatch") { chunk.items[0].owner = 2; }
    SECTION("ground owner") { chunk.items.back().owner = 1; }
    SECTION("item order") { chunk.items[0].location = LocationKind::item; chunk.items[0].owner = 4; }
    SECTION("noncontainer") { chunk.items[0].kind = ItemKind::rock; }
    SECTION("duplicate ID") { chunk.items[0].id = 1; }
    SECTION("duplicate actor position") { chunk.actors.push_back({20, {}, false, 0}); }
    SECTION("duplicate object position") { chunk.objects.push_back({20, {}, false, 0}); }
    SECTION("ground wrong chunk") { chunk.items.back().position = {16, 0}; }
    SECTION("unknown item kind") { chunk.items[0].kind = static_cast<ItemKind>(255); }
    SECTION("unknown location") { chunk.items[0].location = static_cast<LocationKind>(255); }
    SECTION("unknown terrain") { chunk.tiles[0].terrain = static_cast<Terrain>(255); }
    REQUIRE(database.Begin());
    CHECK(database.WriteChunk(chunk) == eastl::unexpected(SaveError::invalid_data));
    CHECK(database.Commit() == eastl::unexpected(SaveError::invalid_state));
}

TEST_CASE("Save database distinguishes absent chunks from corrupt payloads after opening", "[save][database]") {
    SaveFile file;
    Populate(file.path.c_str());
    SaveDatabase database;
    REQUIRE(database.Open(file.path.c_str()));
    {
        RawDatabase raw(file.path.c_str());
        raw.Exec("UPDATE chunks SET payload='null'");
    }
    CHECK(database.ReadChunk({0, 0}, 8) == eastl::unexpected(SaveError::invalid_data));
    auto absent = database.ReadChunk({2, 2}, 8);
    REQUIRE(absent);
    CHECK_FALSE(*absent);
}

TEST_CASE("Save database creation leaves snapshot initialization to the first checkpoint", "[save][database]") {
    SaveFile file;
    {
        SaveDatabase database;
        REQUIRE(database.Create(file.path.c_str()));
        REQUIRE(database.Begin());
        CHECK(database.Commit() == eastl::unexpected(SaveError::invalid_data));
        REQUIRE(database.Begin());
        REQUIRE(database.WriteSnapshot(SaveSnapshot{}));
        REQUIRE(database.Commit());
        REQUIRE(database.Begin());
        auto changed = SaveSnapshot{};
        changed.seed = 100;
        REQUIRE(database.WriteSnapshot(changed));
        // Destruction rolls back the unfinished checkpoint.
    }
    SaveDatabase reopened;
    auto snapshot = reopened.Open(file.path.c_str());
    REQUIRE(snapshot);
    CHECK(snapshot->seed == 42);
}

TEST_CASE("Save database removes abandoned creation and permits same path retry", "[save][database]") {
    SaveFile file;
    {
        SaveDatabase database;
        REQUIRE(database.Create(file.path.c_str()));
        REQUIRE(std::filesystem::exists(file.path));
        SaveDatabase observer;
        CHECK(observer.Open(file.path.c_str()) == eastl::unexpected(SaveError::invalid_data));
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
            CHECK(database.Commit() == eastl::unexpected(SaveError::invalid_data));
        }
    }
    CHECK_FALSE(std::filesystem::exists(file.path));
    CHECK_FALSE(std::filesystem::exists(file.path + "-journal"));
    Populate(file.path.c_str());
    SaveDatabase reopened;
    REQUIRE(reopened.Open(file.path.c_str()));
}

TEST_CASE("Save database removes creation after a failed first write", "[save][database]") {
    SaveFile file;
    {
        SaveDatabase database;
        REQUIRE(database.Create(file.path.c_str()));
        REQUIRE(database.Begin());
        REQUIRE(database.WriteChunk(SampleChunk()));
        auto invalid = SampleSnapshot();
        invalid.nextId = 0;
        CHECK(database.WriteSnapshot(invalid) == eastl::unexpected(SaveError::invalid_data));
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
        SaveDatabase database;
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
            CHECK(database.WriteSnapshot(invalid) == eastl::unexpected(SaveError::invalid_data));
        }
        SECTION("later commit failure") {
            invalid.nextId = 7;
            REQUIRE(database.WriteSnapshot(invalid));
            CHECK(database.Commit() == eastl::unexpected(SaveError::invalid_data));
        }
    }
    REQUIRE(std::filesystem::exists(file.path));
    SaveDatabase reopened;
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
        SaveDatabase database;
        REQUIRE(database.Create(path.c_str()));
        REQUIRE(std::filesystem::exists(nativePath));
    }
    CHECK_FALSE(std::filesystem::exists(nativePath));
    Populate(path.c_str());
    CHECK(std::filesystem::exists(nativePath));
    SaveDatabase reopened;
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
        SaveDatabase database;
        CHECK(database.Create(file.path.c_str()) == eastl::unexpected(SaveError::already_exists));
        CHECK(database.Open(file.path.c_str()) == eastl::unexpected(SaveError::invalid_data));
    }
    REQUIRE(std::filesystem::exists(file.path));
    std::ifstream existing(file.path, std::ios::binary);
    std::string content;
    std::getline(existing, content);
    CHECK(content == "external data");
}

} // namespace
