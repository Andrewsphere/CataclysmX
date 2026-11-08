#include "core/world.h"
#include "core/save_database.h"

#include <catch2/catch_test_macros.hpp>
#include <EASTL/numeric_limits.h>
#include <EASTL/utility.h>
#include <sqlite3.h>

#include <chrono>
#include <filesystem>
#include <string>

namespace {

using namespace cataclysmx;

template<class T, class Error>
T Take(eastl::expected<T, Error> result) {
    REQUIRE(result.has_value());
    return eastl::move(*result);
}

struct SaveDirectory {
    std::filesystem::path directory;
    std::string database;

    SaveDirectory() {
        std::error_code error;
        const auto parent = std::filesystem::temp_directory_path(error);
        REQUIRE_FALSE(error);
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (int attempt = 0; ; ++attempt) {
            directory = parent / ("cataclysmx-world-save-" + std::to_string(stamp)
                + "-" + std::to_string(attempt));
            const bool created = std::filesystem::create_directory(directory, error);
            REQUIRE_FALSE(error);
            if (created) {
                break;
            }
        }
        database = (directory / "world.sqlite").string();
    }

    ~SaveDirectory() {
        std::error_code error;
        std::filesystem::remove_all(directory, error);
    }
};

LoadRequest Load(World& world, ChunkCoord position = {0, 0}) {
    const auto request = Take(world.RequestChunks({position, 0}, LoadMode::active));
    REQUIRE(world.ReconcileChunks());
    REQUIRE(world.CheckInvariants());
    return request;
}

void CheckSession(const SessionState& actual, const SessionState& expected) {
    CHECK(actual.tick == expected.tick);
    CHECK(actual.rngState == expected.rngState);
    CHECK(actual.player == expected.player);
    CHECK(actual.playerPosition == expected.playerPosition);
    CHECK(actual.activeRadius == expected.activeRadius);
    CHECK(actual.residentRadius == expected.residentRadius);
    CHECK(actual.autosaveInterval == expected.autosaveInterval);
}

TEST_CASE("World save round trips every tile and nested inventory values", "[world][save]") {
    SaveDirectory files;
    SessionState session;
    session.tick = 1234;
    session.rngState = 0xfedcba98U;
    session.autosaveInterval = 97;
    eastl::array<Tile, chunk_size * chunk_size> tiles;
    PersistentId actorId;
    PersistentId objectId;
    Vector<PersistentId> itemIds;
    {
        World world(0xf0123456U);
        Load(world);
        for (int index = 0; index < chunk_size * chunk_size; ++index) {
            tiles[index] = {static_cast<Terrain>(index % 4), static_cast<std::uint8_t>(index)};
            REQUIRE(world.SetTile({index % chunk_size, index / chunk_size}, tiles[index]));
        }
        const auto actor = Take(world.CreateActor({0, 0}, true));
        const auto object = Take(world.CreateObject({2, 2}, true));
        const auto bag = Take(world.CreateItem(ItemKind::backpack, ItemLocation::Carried(actor)));
        const auto nested = Take(world.CreateItem(ItemKind::backpack, ItemLocation::Inside(bag)));
        const auto child = Take(world.CreateItem(ItemKind::rock, ItemLocation::Inside(nested)));
        const auto sibling = Take(world.CreateItem(ItemKind::bandage, ItemLocation::Inside(bag)));
        const auto carried = Take(world.CreateItem(ItemKind::rock, ItemLocation::Carried(actor)));
        const auto stored = Take(world.CreateItem(ItemKind::backpack, ItemLocation::Stored(object)));
        const auto storedChild = Take(world.CreateItem(ItemKind::bandage, ItemLocation::Inside(stored)));
        const auto ground = Take(world.CreateItem(ItemKind::backpack, ItemLocation::Ground({3, 3})));
        const auto groundChild = Take(world.CreateItem(ItemKind::rock, ItemLocation::Inside(ground)));
        REQUIRE(world.TransferItem(nested, ItemLocation::Ground({1, 1})));
        REQUIRE(world.TransferItem(nested, ItemLocation::Inside(bag)));
        for (ItemHandle handle : Vector<ItemHandle>{bag, nested, child, sibling, carried,
                stored, storedChild, ground, groundChild}) {
            itemIds.push_back(world.GetItem(handle)->id);
        }
        actorId = world.GetActor(actor)->id;
        objectId = world.GetObject(object)->id;
        session.player = actorId;
        REQUIRE(world.RecordActorTurn(actor));
        REQUIRE(world.RecordActorTurn(actor));
        REQUIRE(world.RecordObjectUpdate(object));
        REQUIRE(world.SetObjectEnabled(object, false));
        REQUIRE(world.CreateSave(files.database.c_str(), session));
        CHECK(world.HasSave());
        CHECK(world.GetActor(actor)->id == actorId); // Checkpoint keeps live keys.
        CHECK(world.GetItem(child)->id == itemIds[2]);
    }
    World restored(999);
    CheckSession(Take(restored.LoadSave(files.database.c_str())), session);
    CHECK(restored.LoadedChunks().empty());
    Load(restored);
    CHECK(restored.GetChunk({0, 0})->tiles == tiles);
    const auto actor = restored.FindActor(actorId);
    const auto object = restored.FindObject(objectId);
    REQUIRE(restored.GetActor(actor));
    REQUIRE(restored.GetObject(object));
    CHECK(restored.GetActor(actor)->player);
    CHECK(restored.GetActor(actor)->turns == 2);
    CHECK(restored.GetActor(actor)->position == TileCoord{0, 0});
    CHECK(restored.GetObject(object)->position == TileCoord{2, 2});
    CHECK_FALSE(restored.GetObject(object)->enabled);
    CHECK(restored.GetObject(object)->updates == 1);
    Vector<ItemHandle> items;
    for (PersistentId id : itemIds) {
        const auto handle = restored.FindItem(id);
        REQUIRE(restored.GetItem(handle));
        items.push_back(handle);
    }
    CHECK(restored.GetActor(actor)->inventory == Vector<ItemHandle>{items[0], items[4]});
    CHECK(restored.GetItem(items[0])->location == ItemLocation::Carried(actor));
    CHECK(restored.GetItem(items[0])->contents == Vector<ItemHandle>{items[3], items[1]});
    CHECK(restored.GetItem(items[1])->location == ItemLocation::Inside(items[0]));
    CHECK(restored.GetItem(items[1])->contents == Vector<ItemHandle>{items[2]});
    CHECK(restored.GetItem(items[2])->location == ItemLocation::Inside(items[1]));
    CHECK(restored.GetItem(items[2])->kind == ItemKind::rock);
    CHECK(restored.GetItem(items[3])->kind == ItemKind::bandage);
    CHECK(restored.GetObject(object)->contents == Vector<ItemHandle>{items[5]});
    CHECK(restored.GetItem(items[5])->contents == Vector<ItemHandle>{items[6]});
    CHECK(restored.GetItem(items[5])->location == ItemLocation::Stored(object));
    CHECK(restored.GetItem(items[6])->location == ItemLocation::Inside(items[5]));
    CHECK(restored.ItemsAt({3, 3}) == Vector<ItemHandle>{items[7]});
    CHECK(restored.GetItem(items[7])->contents == Vector<ItemHandle>{items[8]});
    CHECK(restored.GetItem(items[8])->location == ItemLocation::Inside(items[7]));
    CHECK(restored.CheckInvariants());
    const auto next = Take(restored.CreateItem(ItemKind::rock, ItemLocation::Ground({0, 0})));
    CHECK(restored.GetItem(next)->id > itemIds.back());

    // Missing rows generate from the installed seed, rather than the constructor seed.
    World reference(0xf0123456U);
    Load(reference, {10, -10});
    Load(restored, {10, -10});
    CHECK(restored.GetChunk({10, -10})->tiles == reference.GetChunk({10, -10})->tiles);
}

TEST_CASE("World retains unloaded archives until checkpoint then reloads from disk", "[world][save]") {
    SaveDirectory files;
    World world;
    const auto request = Load(world);
    const auto actor = Take(world.CreateActor({1, 1}, true));
    const auto bag = Take(world.CreateItem(ItemKind::backpack, ItemLocation::Carried(actor)));
    const auto child = Take(world.CreateItem(ItemKind::rock, ItemLocation::Inside(bag)));
    const auto actorId = world.GetActor(actor)->id;
    const auto childId = world.GetItem(child)->id;
    SessionState session;
    session.player = actorId;
    session.playerPosition = {1, 1};
    REQUIRE(world.CreateSave(files.database.c_str(), session));
    REQUIRE(world.SetTile({3, 3}, {Terrain::closed_door, 219}));
    REQUIRE(world.RecordActorTurn(actor));
    REQUIRE(world.ReleaseRequest(request));
    REQUIRE(world.ReconcileChunks());
    CHECK(world.ArchivedChunkCount() == 1);
    CHECK(world.GetActor(actor) == nullptr);
    CHECK(world.GetItem(child) == nullptr);
    auto invalid = session;
    invalid.playerPosition = {2, 1};
    CHECK(world.Save(invalid) == eastl::unexpected(SaveError::InvalidData));
    CHECK(world.ArchivedChunkCount() == 1);
    {
        SaveDatabase competing;
        const auto snapshot = Take(competing.Open(files.database.c_str()));
        REQUIRE(competing.Begin());
        REQUIRE(competing.WriteSnapshot(snapshot)); // Hold an actual SQLite writer lock.
        CHECK(world.Save(session) == eastl::unexpected(SaveError::Busy));
        CHECK(world.ArchivedChunkCount() == 1);
        CHECK(world.LoadedChunks().empty());
        CHECK(world.GetActor(actor) == nullptr);
        CHECK(world.CheckInvariants());
        competing.Rollback();
    }
    REQUIRE(world.Save(session));
    CHECK(world.ArchivedChunkCount() == 0);
    CHECK(world.CheckInvariants());
    Load(world);
    const auto fresh = world.FindActor(actorId);
    REQUIRE(world.GetActor(fresh));
    CHECK(fresh != actor);
    CHECK(world.GetActor(fresh)->turns == 1);
    REQUIRE(world.GetItem(world.FindItem(childId)));
    CHECK(world.FindItem(childId) != child);
    CHECK(*world.GetTile({3, 3}) == Tile{Terrain::closed_door, 219});
    REQUIRE(world.Save(session)); // Clean disk-restored chunks need no rewrite.
    CHECK(world.GetActor(fresh));
}

TEST_CASE("World checkpoints both sides of cross chunk moves and ownership changes", "[world][save]") {
    SaveDirectory files;
    PersistentId actorId;
    PersistentId objectId;
    PersistentId bagId;
    PersistentId childId;
    {
        World world;
        const auto origin = Load(world);
        Load(world, {1, 0});
        const auto actor = Take(world.CreateActor({15, 1}));
        const auto object = Take(world.CreateObject({1, 2}));
        const auto bag = Take(world.CreateItem(ItemKind::backpack, ItemLocation::Stored(object)));
        const auto nested = Take(world.CreateItem(ItemKind::backpack, ItemLocation::Inside(bag)));
        const auto child = Take(world.CreateItem(ItemKind::bandage, ItemLocation::Inside(nested)));
        actorId = world.GetActor(actor)->id;
        objectId = world.GetObject(object)->id;
        bagId = world.GetItem(bag)->id;
        childId = world.GetItem(child)->id;
        REQUIRE(world.CreateSave(files.database.c_str(), {}));
        REQUIRE(world.MoveActor(actor, {16, 1}));
        REQUIRE(world.TransferItem(bag, ItemLocation::Carried(actor)));
        REQUIRE(world.Save({}));
        // Transfer a descendant whose source is resolved through two containers.
        REQUIRE(world.TransferItem(child, ItemLocation::Stored(object)));
        REQUIRE(world.RecordActorTurn(actor));
        REQUIRE(world.SetObjectEnabled(object, true));
        REQUIRE(world.RecordObjectUpdate(object));
        REQUIRE(world.ReleaseRequest(origin));
        REQUIRE(world.ReconcileChunks());
        CHECK(world.ArchivedChunkCount() == 1);
        REQUIRE(world.Save({}));
        CHECK(world.ArchivedChunkCount() == 0);
        CHECK(world.GetActor(actor));
    }
    World world;
    Take(world.LoadSave(files.database.c_str()));
    Load(world);
    Load(world, {1, 0});
    const auto actor = world.FindActor(actorId);
    const auto object = world.FindObject(objectId);
    const auto bag = world.FindItem(bagId);
    const auto child = world.FindItem(childId);
    REQUIRE(world.GetActor(actor));
    REQUIRE(world.GetObject(object));
    REQUIRE(world.GetItem(bag));
    REQUIRE(world.GetItem(child));
    CHECK(world.GetChunk({0, 0})->actors.empty());
    CHECK(world.GetActor(actor)->position == TileCoord{16, 1});
    CHECK(world.GetActor(actor)->turns == 1);
    CHECK(world.GetActor(actor)->inventory == Vector<ItemHandle>{bag});
    CHECK(world.GetItem(bag)->location == ItemLocation::Carried(actor));
    CHECK(world.GetItem(child)->location == ItemLocation::Stored(object));
    CHECK(world.GetObject(object)->contents == Vector<ItemHandle>{child});
    CHECK(world.GetObject(object)->enabled);
    CHECK(world.GetObject(object)->updates == 1);
    CHECK(world.CheckInvariants());
}

TEST_CASE("World save rejects invalid attachment state and session without mutation", "[world][save]") {
    SaveDirectory files;
    World world;
    CHECK(world.Save({}) == eastl::unexpected(SaveError::NotOpen));
    const auto request = Load(world);
    const auto actor = Take(world.CreateActor({0, 0}, true));
    const auto id = world.GetActor(actor)->id;
    SessionState session;
    session.player = id;
    session.playerPosition = {1, 0};
    CHECK(world.CreateSave(files.database.c_str(), session) == eastl::unexpected(SaveError::InvalidData));
    CHECK_FALSE(world.HasSave());
    session.playerPosition = {0, 0};
    REQUIRE(world.CreateSave(files.database.c_str(), session));
    CHECK(world.CreateSave(files.database.c_str(), session) == eastl::unexpected(SaveError::InvalidState));
    CHECK(world.LoadSave(files.database.c_str()) == eastl::unexpected(SaveError::InvalidState));
    CHECK(world.GetActor(actor)->id == id);
    World requested;
    Take(requested.RequestChunks({{0, 0}, 0}, LoadMode::active));
    CHECK(requested.LoadSave(files.database.c_str()) == eastl::unexpected(SaveError::InvalidState));
    CHECK_FALSE(requested.HasSave());
    World pristine;
    CHECK_FALSE(pristine.LoadSave((files.directory / "missing.sqlite").string().c_str()));
    CHECK_FALSE(pristine.HasSave());
    CheckSession(Take(pristine.LoadSave(files.database.c_str())), session);
    CHECK(world.SetTile({0, 0}, {static_cast<Terrain>(255), 0})
        == eastl::unexpected(WorldError::invalid_request));
    CHECK(*world.GetTile({0, 0}) == Tile{});
    REQUIRE(world.ReleaseRequest(request));
    REQUIRE(world.ReconcileChunks());
    REQUIRE(world.Save(session)); // Player may be archived or already evicted to disk.
    REQUIRE(world.Save(session));
}

TEST_CASE("Dirty live chunks persist edits and deletion after a clean checkpoint", "[world][save]") {
    SaveDirectory files;
    PersistentId actorId;
    PersistentId objectId;
    PersistentId itemId;
    {
        World world;
        Load(world);
        Load(world, {1, 0});
        const auto actor = Take(world.CreateActor({1, 1}));
        const auto object = Take(world.CreateObject({17, 1}));
        const auto item = Take(world.CreateItem(ItemKind::rock, ItemLocation::Ground({17, 2})));
        actorId = world.GetActor(actor)->id;
        objectId = world.GetObject(object)->id;
        itemId = world.GetItem(item)->id;
        REQUIRE(world.CreateSave(files.database.c_str(), {}));
        REQUIRE(world.SetTile({2, 2}, {Terrain::open_door, 37}));
        REQUIRE(world.DestroyItem(item));
        REQUIRE(world.Save({}));
        // Inspect through a separate World so failure to mark either chunk is visible.
        World checkpoint;
        Take(checkpoint.LoadSave(files.database.c_str()));
        Load(checkpoint);
        Load(checkpoint, {1, 0});
        CHECK(*checkpoint.GetTile({2, 2}) == Tile{Terrain::open_door, 37});
        CHECK(checkpoint.FindItem(itemId) == ItemHandle{});
        REQUIRE(world.DestroyActor(actor));
        REQUIRE(world.DestroyObject(object));
        REQUIRE(world.Save({}));
    }
    World restored;
    Take(restored.LoadSave(files.database.c_str()));
    Load(restored);
    Load(restored, {1, 0});
    CHECK(restored.FindActor(actorId) == ActorHandle{});
    CHECK(restored.FindObject(objectId) == ObjectHandle{});
    CHECK(restored.FindItem(itemId) == ItemHandle{});
    CHECK(restored.CheckInvariants());
}

// Fixtures must pass global checkpoint validation. Corruption is injected only
// after opening, to exercise streaming's own validation of incoming disk data.
void WriteFixture(const char* path, const Vector<ChunkArchive>& chunks, PersistentId nextId = 10) {
    SaveDatabase database;
    REQUIRE(database.Create(path));
    REQUIRE(database.Begin());
    for (const auto& chunk : chunks) {
        REQUIRE(database.WriteChunk(chunk));
    }
    SaveSnapshot snapshot;
    snapshot.nextId = nextId;
    REQUIRE(database.WriteSnapshot(snapshot));
    REQUIRE(database.Commit());
}

void Corrupt(const char* path, const char* sql) {
    sqlite3* database = nullptr;
    REQUIRE(sqlite3_open_v2(path, &database, SQLITE_OPEN_READWRITE, nullptr) == SQLITE_OK);
    const int result = sqlite3_exec(database, sql, nullptr, nullptr, nullptr);
    CHECK(sqlite3_close(database) == SQLITE_OK);
    REQUIRE(result == SQLITE_OK);
}

ChunkArchive ActorChunk(ChunkCoord position, PersistentId id) {
    ChunkArchive chunk{};
    chunk.position = position;
    chunk.actors.push_back({id, {position.x * chunk_size, position.z * chunk_size}, false, 0});
    return chunk;
}

TEST_CASE("Failed reconciliation preserves live keys activity and loaded set", "[world][save][streaming]") {
    SaveDirectory files;
    WriteFixture(files.database.c_str(), {ActorChunk({0, 0}, 1), ActorChunk({1, 0}, 2)});
    World world;
    Take(world.LoadSave(files.database.c_str()));
    Corrupt(files.database.c_str(),
        "UPDATE chunks SET payload=replace(payload,'\"id\":\"2\"','\"id\":\"1\"') WHERE x=1 AND z=0");
    const auto request = Load(world);
    const auto actor = world.FindActor(1);
    REQUIRE(world.GetActor(actor));
    REQUIRE(world.UpdateRequest(request, {{1, 0}, 0}, LoadMode::resident));
    Diagnostics diagnostics;
    CHECK(world.ReconcileChunks(&diagnostics) == eastl::unexpected(SaveError::InvalidData));
    CHECK_FALSE(diagnostics.message.empty());
    CHECK(world.LoadedChunks() == Vector<ChunkCoord>{{0, 0}});
    CHECK(world.IsActive({0, 0}));
    CHECK(world.GetActor(actor));
    CHECK(world.FindActor(1) == actor);
    CHECK(world.ArchivedChunkCount() == 0);
    CHECK(world.CheckInvariants());
    // Retry still reads the invalid row rather than silently generating terrain.
    CHECK(world.ReconcileChunks() == eastl::unexpected(SaveError::InvalidData));
    REQUIRE(world.UpdateRequest(request, {{0, 0}, 0}, LoadMode::resident));
    REQUIRE(world.ReconcileChunks());
    CHECK_FALSE(world.IsActive({0, 0}));
    CHECK(world.FindActor(1) == actor);
}

TEST_CASE("Reconciliation validates all staged disk chunks before activity changes", "[world][save][streaming]") {
    SaveDirectory files;
    WriteFixture(files.database.c_str(), {ActorChunk({0, 0}, 1),
        ActorChunk({1, 0}, 2), ActorChunk({2, 0}, 3)});
    World world;
    Take(world.LoadSave(files.database.c_str()));
    Corrupt(files.database.c_str(),
        "UPDATE chunks SET payload=replace(payload,'\"id\":\"3\"','\"id\":\"2\"') WHERE x=2 AND z=0");
    const auto origin = Load(world);
    const auto actor = world.FindActor(1);
    REQUIRE(world.UpdateRequest(origin, {{0, 0}, 0}, LoadMode::resident));
    Take(world.RequestChunks({{1, 0}, 0}, LoadMode::active));
    Take(world.RequestChunks({{2, 0}, 0}, LoadMode::active));
    CHECK(world.ReconcileChunks() == eastl::unexpected(SaveError::InvalidData));
    CHECK(world.LoadedChunks() == Vector<ChunkCoord>{{0, 0}});
    CHECK(world.IsActive({0, 0}));
    CHECK(world.FindActor(1) == actor);
    CHECK(world.FindActor(2) == ActorHandle{});
    CHECK(world.ArchivedChunkCount() == 0);
    CHECK(world.CheckInvariants());
}

TEST_CASE("Incoming disk chunks cannot alias IDs in pending RAM archives", "[world][save][streaming]") {
    SaveDirectory files;
    WriteFixture(files.database.c_str(), {ActorChunk({0, 0}, 1), ActorChunk({1, 0}, 2)});
    World world;
    Take(world.LoadSave(files.database.c_str()));
    Corrupt(files.database.c_str(),
        "UPDATE chunks SET payload=replace(payload,'\"id\":\"2\"','\"id\":\"1\"') WHERE x=1 AND z=0");
    const auto request = Load(world);
    REQUIRE(world.ReleaseRequest(request));
    REQUIRE(world.ReconcileChunks());
    REQUIRE(world.ArchivedChunkCount() == 1);
    Take(world.RequestChunks({{1, 0}, 0}, LoadMode::active));
    CHECK(world.ReconcileChunks() == eastl::unexpected(SaveError::InvalidData));
    CHECK(world.LoadedChunks().empty());
    CHECK(world.ArchivedChunkCount() == 1);
    CHECK(world.CheckInvariants());
}

TEST_CASE("World saves preserve unsigned persistent IDs up to exhaustion", "[world][save]") {
    SaveDirectory files;
    constexpr auto maximum = eastl::numeric_limits<PersistentId>::max();
    WriteFixture(files.database.c_str(), {ActorChunk({0, 0}, maximum - 2)}, maximum - 1);
    PersistentId issued;
    {
        World world;
        Take(world.LoadSave(files.database.c_str()));
        Load(world);
        REQUIRE(world.GetActor(world.FindActor(maximum - 2)));
        const auto item = Take(world.CreateItem(ItemKind::rock, ItemLocation::Ground({1, 0})));
        issued = world.GetItem(item)->id;
        CHECK(issued == maximum - 1);
        REQUIRE(world.Save({}));
        CHECK(world.CheckInvariants());
    }
    World restored;
    Take(restored.LoadSave(files.database.c_str()));
    Load(restored);
    REQUIRE(restored.GetItem(restored.FindItem(issued)));
    CHECK(restored.CheckInvariants());
}

}
