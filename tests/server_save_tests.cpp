#include "core/server.h"
#include "core/save_database.h"

#include <catch2/catch_test_macros.hpp>
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

struct SaveFile {
    std::filesystem::path directory;
    std::string path;

    SaveFile() {
        std::error_code error;
        const auto parent = std::filesystem::temp_directory_path(error);
        REQUIRE_FALSE(error);
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (int attempt = 0; ; ++attempt) {
            directory = parent / ("cataclysmx-server-save-" + std::to_string(stamp)
                + "-" + std::to_string(attempt));
            const bool created = std::filesystem::create_directory(directory, error);
            REQUIRE_FALSE(error);
            if (created) {
                break;
            }
        }
        path = (directory / "world.sqlite").string();
    }

    ~SaveFile() {
        std::error_code error;
        std::filesystem::remove_all(directory, error);
    }
};

struct RawDatabase {
    sqlite3* database = nullptr;

    explicit RawDatabase(const char* path) {
        REQUIRE(sqlite3_open_v2(path, &database, SQLITE_OPEN_READWRITE, nullptr) == SQLITE_OK);
    }
    ~RawDatabase() { sqlite3_close(database); }
    void Exec(const char* sql) {
        REQUIRE(sqlite3_exec(database, sql, nullptr, nullptr, nullptr) == SQLITE_OK);
    }
};

SessionState ReadSession(const SaveFile& file) {
    SaveDatabase database;
    return Take(database.Open(file.path.c_str())).session;
}

void SameState(const Server& first, const Server& second) {
    const auto a = first.View();
    const auto b = second.View();
    CHECK(a.tick == b.tick);
    CHECK(a.origin == b.origin);
    CHECK(a.player_position == b.player_position);
    CHECK(a.cells == b.cells);
    CHECK(a.inventory == b.inventory);
    const World& left = first.GetWorld();
    const World& right = second.GetWorld();
    REQUIRE(left.LoadedChunks() == right.LoadedChunks());
    for (ChunkCoord position : left.LoadedChunks()) {
        const Chunk* chunk = left.GetChunk(position);
        REQUIRE(chunk->tiles == right.GetChunk(position)->tiles);
        CHECK(chunk->active == right.GetChunk(position)->active);
        REQUIRE(chunk->actors.size() == right.GetChunk(position)->actors.size());
        for (ActorHandle handle : chunk->actors) {
            const Actor* actor = left.GetActor(handle);
            const Actor* other = right.GetActor(right.FindActor(actor->id));
            REQUIRE(other);
            CHECK(actor->position == other->position);
            CHECK(actor->turns == other->turns);
        }
        for (ObjectHandle handle : chunk->objects) {
            const auto* object = left.GetObject(handle);
            const auto* other = right.GetObject(right.FindObject(object->id));
            REQUIRE(other);
            CHECK(object->enabled == other->enabled);
            CHECK(object->updates == other->updates);
        }
    }
    CHECK(left.CheckInvariants());
    CHECK(right.CheckInvariants());
}

TEST_CASE("Server checkpoint reopens tick RNG bubble and nested inventory for deterministic continuation",
    "[server][save][determinism]") {
    SaveFile file;
    Server original(918);
    REQUIRE(original.SetBubbleRadii(1, 1));
    REQUIRE(original.Send({CommandKind::pick_up}).advanced);
    World& world = original.GetWorld();
    const auto bag = world.GetActor(original.GetPlayer())->inventory[0];
    const auto nested = Take(world.CreateItem(ItemKind::backpack, ItemLocation::Inside(bag)));
    const auto child = Take(world.CreateItem(ItemKind::bandage, ItemLocation::Inside(nested)));
    const auto bagId = world.GetItem(bag)->id;
    const auto nestedId = world.GetItem(nested)->id;
    const auto childId = world.GetItem(child)->id;
    const auto playerId = world.GetActor(original.GetPlayer())->id;
    original.SetAutosaveInterval(1000);
    for (int turn = 0; turn < 17; ++turn) {
        REQUIRE(original.Send({}).advanced);
    }
    // An administrative resident request is deliberately absent from session state.
    const auto admin = Take(world.RequestChunks({{10, 10}, 0}, LoadMode::resident));
    REQUIRE(world.ReconcileChunks());
    REQUIRE(original.CreateSave(file.path.c_str()));
    const auto session = ReadSession(file);
    CHECK(session.tick == original.GetTick());
    CHECK(session.player == playerId);
    CHECK(session.activeRadius == 1);
    CHECK(session.residentRadius == 1);
    auto restored = Take(Server::LoadSave(file.path.c_str()));
    CHECK(restored->GetAutosaveInterval() == 1000);
    CHECK(restored->GetWorld().GetChunk({10, 10}) == nullptr);
    REQUIRE(world.ReleaseRequest(admin));
    REQUIRE(world.ReconcileChunks());
    SameState(original, *restored);
    const World& loaded = restored->GetWorld();
    REQUIRE(loaded.GetActor(restored->GetPlayer()));
    CHECK(loaded.GetActor(restored->GetPlayer())->id == playerId);
    const auto loadedBag = loaded.FindItem(bagId);
    const auto loadedNested = loaded.FindItem(nestedId);
    const auto loadedChild = loaded.FindItem(childId);
    REQUIRE(loaded.GetItem(loadedBag));
    REQUIRE(loaded.GetItem(loadedNested));
    REQUIRE(loaded.GetItem(loadedChild));
    CHECK(loaded.GetItem(loadedBag)->location == ItemLocation::Carried(restored->GetPlayer()));
    CHECK(loaded.GetItem(loadedNested)->location == ItemLocation::Inside(loadedBag));
    CHECK(loaded.GetItem(loadedChild)->location == ItemLocation::Inside(loadedNested));
    for (int turn = 0; turn < 48; ++turn) {
        const Command command = turn == 3 ? Command{CommandKind::drop}
            : turn == 4 ? Command{CommandKind::pick_up} : Command{};
        const auto a = original.Send(command);
        const auto b = restored->Send(command);
        CHECK(a.outcome == b.outcome);
        CHECK(a.advanced == b.advanced);
        CHECK(a.tick == b.tick);
        CHECK_FALSE(a.storageError);
        CHECK_FALSE(b.storageError);
        SameState(original, *restored);
    }
    REQUIRE(original.Save());
    const auto uninterrupted = ReadSession(file);
    REQUIRE(restored->Save());
    const auto continued = ReadSession(file);
    CHECK(continued.tick == uninterrupted.tick);
    CHECK(continued.rngState == uninterrupted.rngState);
}

TEST_CASE("Server manual checkpoints reset autosave cadence and rejected commands never save",
    "[server][save][autosave]") {
    SaveFile file;
    Server server;
    CHECK(server.Save() == eastl::unexpected(SaveError::NotOpen));
    server.SetAutosaveInterval(2);
    REQUIRE(server.Send({}).advanced); // Unattached autosave is ignored.
    REQUIRE(server.Send({}).advanced);
    REQUIRE(server.CreateSave(file.path.c_str())); // Tick 2 starts the interval.
    REQUIRE(server.Send({}).advanced);
    CHECK(ReadSession(file).tick == 2);
    REQUIRE(server.Save()); // Manual checkpoint at 3 restarts it again.
    REQUIRE(server.Send({}).advanced);
    CHECK(ReadSession(file).tick == 3);
    const auto rejected = server.Send({CommandKind::move, 0, 0});
    CHECK_FALSE(rejected.advanced);
    CHECK_FALSE(rejected.storageError);
    CHECK(ReadSession(file).tick == 3);
    REQUIRE(server.Send({}).advanced);
    CHECK(ReadSession(file).tick == 5);
    server.SetAutosaveInterval(0);
    REQUIRE(server.Send({}).advanced);
    REQUIRE(server.Send({}).advanced);
    CHECK(ReadSession(file).tick == 5);
    REQUIRE(server.Save());
    auto restored = Take(Server::LoadSave(file.path.c_str()));
    CHECK(restored->GetTick() == 7);
    CHECK(restored->GetAutosaveInterval() == 0);
    restored->SetAutosaveInterval(2);
    REQUIRE(restored->Send({}).advanced);
    CHECK(ReadSession(file).tick == 7);
    REQUIRE(restored->Send({}).advanced);
    CHECK(ReadSession(file).tick == 9);
}

TEST_CASE("Failed autosave preserves successful gameplay and retries dirty state next successful turn",
    "[server][save][autosave]") {
    SaveFile file;
    Server server;
    server.SetAutosaveInterval(3);
    REQUIRE(server.CreateSave(file.path.c_str()));
    REQUIRE(server.Send({}).advanced);
    REQUIRE(server.Send({}).advanced);
    {
        RawDatabase lock(file.path.c_str());
        lock.Exec("BEGIN IMMEDIATE");
        const auto result = server.Send({CommandKind::pick_up});
        CHECK(result.outcome == Outcome::picked_up);
        CHECK(result.advanced);
        CHECK(result.tick == 3);
        REQUIRE(result.storageError);
        CHECK(*result.storageError == SaveError::Busy);
        CHECK(ReadSession(file).tick == 0);
        const auto failed = server.Send({CommandKind::move, 0, 0});
        CHECK_FALSE(failed.advanced);
        CHECK_FALSE(failed.storageError);
        // A second successful turn while locked retries immediately, not at tick 6.
        const auto retry = server.Send({});
        CHECK(retry.advanced);
        REQUIRE(retry.storageError);
        CHECK(*retry.storageError == SaveError::Busy);
        lock.Exec("ROLLBACK");
    }
    const auto retry = server.Send({});
    CHECK(retry.advanced);
    CHECK_FALSE(retry.storageError);
    CHECK(ReadSession(file).tick == 5);
    auto restored = Take(Server::LoadSave(file.path.c_str()));
    SameState(server, *restored);
    CHECK(restored->View().inventory.size() == 1);
    REQUIRE(server.Send({}).advanced);
    REQUIRE(server.Send({}).advanced);
    CHECK(ReadSession(file).tick == 5);
    REQUIRE(server.Send({}).advanced);
    CHECK(ReadSession(file).tick == 8);
}

TEST_CASE("Streaming movement preflights the full future bubble before moving or unloading",
    "[server][save][streaming]") {
    SaveFile file;
    Server server;
    REQUIRE(server.SetBubbleRadii(0, 1));
    World& world = server.GetWorld();
    REQUIRE(world.MoveActor(server.GetPlayer(), {15, 0}));
    REQUIRE(world.SetTile({16, 0}, {}));
    REQUIRE(server.CreateSave(file.path.c_str()));
    const auto checkpoint = ReadSession(file);
    {
        RawDatabase raw(file.path.c_str());
        raw.Exec("UPDATE chunks SET payload='{}' WHERE x=2 AND z=0");
    }
    const auto player = server.GetPlayer();
    const auto before = world.LoadedChunks();
    const auto view = server.View();
    REQUIRE(world.GetTile({16, 0})); // Destination itself is already resident.
    const auto result = server.Send({CommandKind::move, 1, 0});
    CHECK(result.outcome == Outcome::unavailable);
    CHECK_FALSE(result.advanced);
    CHECK(result.tick == 0);
    REQUIRE(result.storageError);
    CHECK(*result.storageError == SaveError::InvalidData);
    CHECK(server.GetPlayer() == player);
    CHECK(world.GetActor(player)->position == TileCoord{15, 0});
    CHECK(world.GetActor(player)->turns == 0);
    CHECK(world.LoadedChunks() == before);
    CHECK(server.View().cells == view.cells);
    CHECK(world.IsActive({15, 0}));
    CHECK_FALSE(world.IsActive({16, 0}));
    CHECK(world.CheckInvariants());
    Diagnostics diagnostics;
    CHECK(server.SetBubbleRadii(1, 2, &diagnostics)
        == eastl::unexpected(WorldError::storage_failure));
    CHECK_FALSE(diagnostics.message.empty());
    CHECK(world.LoadedChunks() == before);
    // Failed movement released temporary demand; failed resize restored both requests.
    REQUIRE(world.ReconcileChunks());
    CHECK(world.LoadedChunks() == before);
    CHECK_FALSE(world.IsActive({16, 0}));
    {
        RawDatabase raw(file.path.c_str());
        raw.Exec("DELETE FROM chunks WHERE x=2 AND z=0");
    }
    REQUIRE(server.Save());
    CHECK(ReadSession(file).rngState == checkpoint.rngState);
    CHECK(ReadSession(file).activeRadius == 0);
    CHECK(ReadSession(file).residentRadius == 1);
    const auto moved = server.Send({CommandKind::move, 1, 0});
    CHECK(moved.outcome == Outcome::moved);
    CHECK(moved.advanced);
    CHECK_FALSE(moved.storageError);
    CHECK(server.GetPlayer() == player);
    CHECK(world.GetActor(player)->position == TileCoord{16, 0});
    CHECK(world.LoadedChunks().size() == 9);
    CHECK(world.GetChunk({2, 0}));
    CHECK(world.GetChunk({-1, 0}) == nullptr);
    CHECK(world.IsActive({16, 0}));
    CHECK_FALSE(world.IsActive({15, 0}));
    CHECK(world.CheckInvariants());
}

TEST_CASE("Server load rejects unsupported schema and invalid or missing session players",
    "[server][save]") {
    SaveFile file;
    {
        Server server;
        REQUIRE(server.CreateSave(file.path.c_str()));
    }
    SECTION("Unsupported schema") {
        RawDatabase raw(file.path.c_str());
        raw.Exec("UPDATE metadata SET format_version=999");
        Diagnostics diagnostics;
        const auto result = Server::LoadSave(file.path.c_str(), &diagnostics);
        REQUIRE_FALSE(result);
        CHECK(result.error() == SaveError::UnsupportedVersion);
        CHECK_FALSE(diagnostics.message.empty());
    }
    SECTION("Malformed session") {
        RawDatabase raw(file.path.c_str());
        raw.Exec("UPDATE world_state SET payload='{}'");
        const auto result = Server::LoadSave(file.path.c_str());
        REQUIRE_FALSE(result);
        CHECK(result.error() == SaveError::InvalidData);
    }
    SECTION("Valid World checkpoint without a session player") {
        SaveDatabase database;
        auto snapshot = Take(database.Open(file.path.c_str()));
        snapshot.session.player = 0;
        REQUIRE(database.Begin());
        REQUIRE(database.WriteSnapshot(snapshot));
        REQUIRE(database.Commit());
        Diagnostics diagnostics;
        const auto result = Server::LoadSave(file.path.c_str(), &diagnostics);
        REQUIRE_FALSE(result);
        CHECK(result.error() == SaveError::InvalidData);
        CHECK_FALSE(diagnostics.message.empty());
    }
}

}
