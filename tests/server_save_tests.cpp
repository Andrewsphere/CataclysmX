#include "core/server.h"
#include "core/save_database.h"
#include "content_fixture.h"

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
    SaveDatabase database(TestContent());
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
    CHECK(a.speed == b.speed);
    CHECK(a.remainingTicks == b.remainingTicks);
    CHECK(a.pacing == b.pacing);
    CHECK(first.GetPacing() == second.GetPacing());
    CHECK(first.GetCircleResidency() == second.GetCircleResidency());
    CHECK(first.GetActiveRadius() == second.GetActiveRadius());
    CHECK(first.GetResidentRadius() == second.GetResidentRadius());
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
            CHECK(actor->speed == other->speed);
            CHECK(actor->remainingTicks == other->remainingTicks);
        }
        REQUIRE(chunk->objects.size() == right.GetChunk(position)->objects.size());
        for (ObjectHandle handle : chunk->objects) {
            const auto* object = left.GetObject(handle);
            const auto* other = right.GetObject(right.FindObject(object->id));
            REQUIRE(other);
            CHECK(object->enabled == other->enabled);
            CHECK(object->updates == other->updates);
            CHECK(object->updateInterval == other->updateInterval);
            CHECK(object->remainingTicks == other->remainingTicks);
        }
    }
    CHECK(left.CheckInvariants());
    CHECK(right.CheckInvariants());
}

TEST_CASE("Server checkpoint reopens tick RNG bubble and nested inventory for deterministic continuation",
    "[server][save][determinism]") {
    SaveFile file;
    Server original(TestContent(), 918);
    REQUIRE(original.SetBubbleRadii(1, 1));
    REQUIRE(original.Send({CommandKind::pick_up}).advanced);
    World& world = original.GetWorld();
    const auto bag = world.GetActor(original.GetPlayer())->inventory[0];
    const auto nested = Take(world.CreateItem(TestItem("backpack"), ItemLocation::Inside(bag)));
    const auto child = Take(world.CreateItem(TestItem("bandage"), ItemLocation::Inside(nested)));
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
    CHECK_FALSE(session.circleResidency);
    auto restored = Take(Server::LoadSave(TestContent(), file.path.c_str()));
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
    Server server(TestContent());
    CHECK(server.Save() == eastl::unexpected(SaveError::NotOpen));
    server.SetAutosaveInterval(2);
    REQUIRE(server.Send({}).advanced); // Unattached autosave is ignored.
    REQUIRE(server.Send({}).advanced);
    REQUIRE(server.CreateSave(file.path.c_str())); // Tick 200 starts the interval.
    REQUIRE(server.Send({}).advanced);
    CHECK(ReadSession(file).tick == 200);
    REQUIRE(server.Save()); // Manual checkpoint at 300 restarts it again.
    REQUIRE(server.Send({}).advanced);
    CHECK(ReadSession(file).tick == 300);
    const auto rejected = server.Send({CommandKind::move, 0, 0});
    CHECK_FALSE(rejected.advanced);
    CHECK_FALSE(rejected.storageError);
    CHECK(ReadSession(file).tick == 300);
    REQUIRE(server.Send({}).advanced);
    CHECK(ReadSession(file).tick == 500);
    server.SetAutosaveInterval(0);
    REQUIRE(server.Send({}).advanced);
    REQUIRE(server.Send({}).advanced);
    CHECK(ReadSession(file).tick == 500);
    REQUIRE(server.Save());
    auto restored = Take(Server::LoadSave(TestContent(), file.path.c_str()));
    CHECK(restored->GetTick() == 700);
    CHECK(restored->GetAutosaveInterval() == 0);
    restored->SetAutosaveInterval(2);
    REQUIRE(restored->Send({}).advanced);
    CHECK(ReadSession(file).tick == 700);
    REQUIRE(restored->Send({}).advanced);
    CHECK(ReadSession(file).tick == 900);
}

TEST_CASE("Circle checkpoints continue tile centered streaming deterministically", "[server][save][circle][determinism]") {
    SaveFile file;
    Server original(TestContent(), 918);
    REQUIRE(original.SetCircleResidency(0, {15}));
    REQUIRE(original.Send({CommandKind::move, 1, 0}).advanced);
    REQUIRE(original.CreateSave(file.path.c_str()));
    CHECK(ReadSession(file).circleResidency == CircleResidency{15});
    auto restored = Take(Server::LoadSave(TestContent(), file.path.c_str()));
    SameState(original, *restored);
    for (Command command : {Command{CommandKind::move, -1, 0}, Command{CommandKind::move, -1, 0},
             Command{CommandKind::move, -1, 0}, Command{}, Command{CommandKind::move, 1, 0}}) {
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
    CHECK(ReadSession(file).rngState == uninterrupted.rngState);
    REQUIRE(restored->SetBubbleRadii(0, 0));
    REQUIRE(restored->Save());
    auto square = Take(Server::LoadSave(TestContent(), file.path.c_str()));
    CHECK_FALSE(square->GetCircleResidency());
    CHECK(square->GetWorld().LoadedChunks().size() == world_layer_count);
}

TEST_CASE("Small circles preflight active square storage and restore policy on resize failure", "[server][save][streaming][circle]") {
    SaveFile file;
    Server server(TestContent());
    World& world = server.GetWorld();
    REQUIRE(world.MoveActor(server.GetPlayer(), {15, 0}));
    REQUIRE(world.SetTile({16, 0}, {TestTerrain("floor"), 0}));
    REQUIRE(server.SetCircleResidency(1, {0}));
    REQUIRE(server.CreateSave(file.path.c_str()));
    {
        RawDatabase raw(file.path.c_str());
        raw.Exec("UPDATE chunks SET payload='{}' WHERE x=2 AND z=0 AND y=0");
    }
    const auto before = world.LoadedChunks();
    const auto player = server.GetPlayer();
    const auto result = server.Send({CommandKind::move, 1, 0});
    CHECK(result.outcome == Outcome::unavailable);
    CHECK_FALSE(result.advanced);
    REQUIRE(result.storageError);
    CHECK(*result.storageError == SaveError::InvalidData);
    CHECK(world.GetActor(player)->position == TileCoord{15, 0});
    CHECK(world.LoadedChunks() == before);
    CHECK(server.SetBubbleRadii(1, 2) == eastl::unexpected(WorldError::storage_failure));
    CHECK(server.GetCircleResidency() == CircleResidency{0});
    CHECK(server.GetActiveRadius() == 1);
    CHECK(server.GetResidentRadius() == 5);
    REQUIRE(world.ReconcileChunks());
    CHECK(world.LoadedChunks() == before);
    CHECK(server.GetPlayer() == player);
    CHECK(world.CheckInvariants());
}

TEST_CASE("Within chunk circle expansion rejects corrupt resident columns before moving", "[server][save][streaming][circle]") {
    SaveFile file;
    Server server(TestContent());
    REQUIRE(server.SetCircleResidency(0, {15}));
    REQUIRE(server.CreateSave(file.path.c_str()));
    {
        RawDatabase raw(file.path.c_str());
        raw.Exec("UPDATE chunks SET payload='{}' WHERE x=1 AND z=0 AND y=0");
    }
    World& world = server.GetWorld();
    const auto before = world.LoadedChunks();
    const auto player = server.GetPlayer();
    const auto view = server.View();
    const auto result = server.Send({CommandKind::move, 1, 0});
    CHECK(result.outcome == Outcome::unavailable);
    CHECK_FALSE(result.advanced);
    REQUIRE(result.storageError);
    CHECK(*result.storageError == SaveError::InvalidData);
    CHECK(server.GetPlayer() == player);
    CHECK(world.GetActor(player)->position == TileCoord{0, 0});
    CHECK(server.GetTick() == 0);
    CHECK(server.View().cells == view.cells);
    REQUIRE(world.ReconcileChunks());
    CHECK(world.LoadedChunks() == before);
    CHECK_FALSE(world.GetChunk({1, 0}));
}

TEST_CASE("Server checkpoints retain dirty layered archives after eviction and disk reload",
    "[server][save][streaming][layers]") {
    SaveFile file;
    Server server(TestContent());
    REQUIRE(server.SetBubbleRadii(0, 0));
    World& world = server.GetWorld();
    const auto request = Take(world.RequestChunks({{10, 10}, 0}, LoadMode::resident));
    REQUIRE(world.ReconcileChunks());
    const TileCoord below{160, 160, -1};
    const TileCoord above{160, 160, 1};
    REQUIRE(world.SetTile(below, {TestTerrain("floor"), 17}));
    REQUIRE(world.SetTile(above, {TestTerrain("floor"), 29}));
    const auto lowerItem = Take(world.CreateItem(TestItem("rock"), ItemLocation::Ground(below)));
    const auto upperItem = Take(world.CreateItem(TestItem("bandage"), ItemLocation::Ground(above)));
    const PersistentId lowerId = world.GetItem(lowerItem)->id;
    const PersistentId upperId = world.GetItem(upperItem)->id;
    REQUIRE(server.CreateSave(file.path.c_str()));

    // Change both slices after the initial checkpoint, then archive them together.
    REQUIRE(world.SetTile(below, {TestTerrain("floor"), 31}));
    REQUIRE(world.SetTile(above, {TestTerrain("floor"), 43}));
    REQUIRE(world.ReleaseRequest(request));
    REQUIRE(world.ReconcileChunks());
    CHECK(world.GetChunk({10, 10, -1}) == nullptr);
    CHECK(world.GetChunk({10, 10, 1}) == nullptr);
    CHECK(world.ArchivedChunkCount() >= world_layer_count);
    REQUIRE(server.Save());
    CHECK(world.ArchivedChunkCount() == 0);

    auto restored = Take(Server::LoadSave(TestContent(), file.path.c_str()));
    World& loaded = restored->GetWorld();
    REQUIRE(loaded.RequestChunks({{10, 10}, 0}, LoadMode::resident));
    REQUIRE(loaded.ReconcileChunks());
    REQUIRE(loaded.GetTile(below));
    REQUIRE(loaded.GetTile(above));
    CHECK(loaded.GetTile(below)->damage == 31);
    CHECK(loaded.GetTile(above)->damage == 43);
    REQUIRE(loaded.GetItem(loaded.FindItem(lowerId)));
    REQUIRE(loaded.GetItem(loaded.FindItem(upperId)));
    CHECK(loaded.GetItem(loaded.FindItem(lowerId))->location == ItemLocation::Ground(below));
    CHECK(loaded.GetItem(loaded.FindItem(upperId))->location == ItemLocation::Ground(above));
    CHECK(loaded.CheckInvariants());
}

TEST_CASE("Server restores a nonsurface host and its session roster on the recorded layer",
    "[server][save][players][layers]") {
    SaveFile file;
    Server server(TestContent());
    REQUIRE(server.SetBubbleRadii(0, 0));
    int y = -1;
    SECTION("underground") { y = -1; }
    SECTION("elevated") { y = 1; }
    const TileCoord position{0, 0, y};
    REQUIRE(server.GetWorld().SetTile(position, {TestTerrain("floor"), 0}));
    REQUIRE(server.GetWorld().MoveActor(server.GetPlayer(), position));
    REQUIRE(server.CreateSave(file.path.c_str()));
    const auto session = ReadSession(file);
    CHECK(session.playerPosition == position);
    REQUIRE_FALSE(session.players.empty());
    CHECK(session.players[0].position == position);
    auto restored = Take(Server::LoadSave(TestContent(), file.path.c_str()));
    CHECK(restored->View().player_position == position);
    CHECK(restored->GetWorld().GetActor(restored->GetPlayer())->position == position);
    CHECK(restored->GetWorld().CheckInvariants());
}

TEST_CASE("Server saves partial cooldowns object timers and pacing for deterministic continuation",
    "[server][save][scheduling][determinism]") {
    SaveFile file;
    Server original(TestContent(), 918);
    REQUIRE(original.SetBubbleRadii(1, 1));
    REQUIRE(original.GetWorld().SetActorSpeed(original.GetPlayer(), 70));
    const auto object = original.GetWorld().ObjectAt({2, 0});
    const auto objectId = original.GetWorld().GetObject(object)->id;
    REQUIRE(original.GetWorld().SetObjectEnabled(object, true));
    REQUIRE(original.GetWorld().SetObjectUpdateInterval(object, 130));
    const PacingSettings settings{200, true, false};
    REQUIRE(original.SetPacing(settings));
    original.SetAutosaveInterval(3); // Public units remain turns, not ticks.
    Command wait;
    wait.waitCost = 250;
    const auto committed = original.Send(wait);
    REQUIRE(committed.advanced);
    CHECK(committed.cost == 175);
    REQUIRE(original.Update(125'000).tick == 25);
    CHECK(original.View().remainingTicks == 150);
    CHECK(original.GetWorld().GetObject(object)->remainingTicks == 105);
    REQUIRE(original.CreateSave(file.path.c_str()));
    const auto session = ReadSession(file);
    CHECK(session.tick == 25);
    CHECK(session.pacing == settings);
    CHECK(session.autosaveInterval == 3);
    auto restored = Take(Server::LoadSave(TestContent(), file.path.c_str()));
    SameState(original, *restored);
    CHECK(restored->Update(0).tick == 25); // Loading adds no elapsed/offline time.
    CHECK(restored->View().remainingTicks == 150);
    REQUIRE(restored->GetWorld().GetObject(restored->GetWorld().FindObject(objectId)));
    CHECK(restored->GetWorld().GetObject(restored->GetWorld().FindObject(objectId))->remainingTicks == 105);
    for (std::uint64_t elapsed : {100'001ULL, 24'999ULL, 625'000ULL, 349'999ULL,
             1ULL, 2'125'000ULL, 11'111ULL, 888'889ULL}) {
        CAPTURE(elapsed);
        const auto a = original.Update(elapsed);
        const auto b = restored->Update(elapsed);
        CHECK(a.tick == b.tick);
        CHECK(a.needsInput == b.needsInput);
        CHECK(a.skippedActions == b.skippedActions);
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
    CHECK(continued.pacing == uninterrupted.pacing);
}

TEST_CASE("Autosave turn intervals measure simulation ticks across speed scaled waits",
    "[server][save][autosave][scheduling]") {
    SaveFile file;
    Server server(TestContent());
    REQUIRE(server.GetWorld().SetActorSpeed(server.GetPlayer(), 50));
    server.SetAutosaveInterval(2);
    REQUIRE(server.CreateSave(file.path.c_str()));
    Command wait;
    wait.waitCost = 250;
    CHECK(server.Send(wait).tick == 125);
    CHECK(ReadSession(file).tick == 0);
    wait.waitCost = 150;
    const auto boundary = server.Send(wait);
    CHECK(boundary.tick == 200);
    CHECK(boundary.cost == 75);
    CHECK_FALSE(boundary.storageError);
    const auto session = ReadSession(file);
    CHECK(session.tick == 200);
    CHECK(session.autosaveInterval == 2);
    auto restored = Take(Server::LoadSave(TestContent(), file.path.c_str()));
    CHECK(restored->GetAutosaveInterval() == 2);
    SameState(server, *restored);
}

TEST_CASE("Loading a ready player starts fresh input grace without offline catchup",
    "[server][save][scheduling][pacing]") {
    SaveFile file;
    Server original(TestContent());
    REQUIRE(original.GetWorld().SetActorSpeed(original.GetPlayer(), 50));
    REQUIRE(original.Send({}).tick == 50); // Fresh readiness with a 50-tick budget.
    REQUIRE(original.SetPacing({200, false, false}));
    CHECK(original.Update(200'000).skippedActions == 0); // Only 50ms grace remains.
    REQUIRE(original.CreateSave(file.path.c_str()));
    auto restored = Take(Server::LoadSave(TestContent(), file.path.c_str()));
    CHECK(restored->GetPacing() == original.GetPacing());
    CHECK(restored->Update(0).tick == 50);
    CHECK(restored->Update(49'999).skippedActions == 0);
    const auto oldDeadline = original.Update(50'000);
    CHECK(oldDeadline.skippedActions == 1);
    CHECK(oldDeadline.tick == 100);
    // The restored player gets the full 250ms, not the saved 50ms remainder.
    CHECK(restored->Update(200'000).skippedActions == 0);
    CHECK(restored->GetTick() == 50);
    const auto freshDeadline = restored->Update(1);
    CHECK(freshDeadline.skippedActions == 1);
    CHECK(freshDeadline.tick == 100);
    SameState(original, *restored);
}

TEST_CASE("Failed autosave preserves successful gameplay and retries dirty state next successful turn",
    "[server][save][autosave]") {
    SaveFile file;
    Server server(TestContent());
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
        CHECK(result.tick == 300);
        REQUIRE(result.storageError);
        CHECK(*result.storageError == SaveError::Busy);
        CHECK(ReadSession(file).tick == 0);
        const auto failed = server.Send({CommandKind::move, 0, 0});
        CHECK_FALSE(failed.advanced);
        CHECK_FALSE(failed.storageError);
        // A second successful turn while locked retries immediately, before tick 600.
        const auto retry = server.Send({});
        CHECK(retry.advanced);
        REQUIRE(retry.storageError);
        CHECK(*retry.storageError == SaveError::Busy);
        lock.Exec("ROLLBACK");
    }
    const auto retry = server.Send({});
    CHECK(retry.advanced);
    CHECK_FALSE(retry.storageError);
    CHECK(ReadSession(file).tick == 500);
    auto restored = Take(Server::LoadSave(TestContent(), file.path.c_str()));
    SameState(server, *restored);
    CHECK(restored->View().inventory.size() == 2);
    REQUIRE(server.Send({}).advanced);
    REQUIRE(server.Send({}).advanced);
    CHECK(ReadSession(file).tick == 500);
    REQUIRE(server.Send({}).advanced);
    CHECK(ReadSession(file).tick == 800);
}

TEST_CASE("Streaming movement preflights the full future bubble before moving or unloading",
    "[server][save][streaming]") {
    SaveFile file;
    Server server(TestContent());
    REQUIRE(server.SetBubbleRadii(0, 1));
    World& world = server.GetWorld();
    REQUIRE(world.MoveActor(server.GetPlayer(), {15, 0}));
    REQUIRE(world.SetTile({16, 0}, {TestTerrain("floor"), 0}));
    REQUIRE(server.CreateSave(file.path.c_str()));
    const auto checkpoint = ReadSession(file);
    {
        RawDatabase raw(file.path.c_str());
        raw.Exec("UPDATE chunks SET payload='{}' WHERE x=2 AND z=0 AND y=0");
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
    CHECK_FALSE(diagnostics.Empty());
    CHECK(world.LoadedChunks() == before);
    // Failed movement released temporary demand; failed resize restored both requests.
    REQUIRE(world.ReconcileChunks());
    CHECK(world.LoadedChunks() == before);
    CHECK_FALSE(world.IsActive({16, 0}));
    {
        RawDatabase raw(file.path.c_str());
        raw.Exec("DELETE FROM chunks WHERE x=2 AND z=0 AND y=0");
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
    CHECK(world.LoadedChunks().size() == 9 * world_layer_count);
    CHECK(world.GetChunk({2, 0}));
    CHECK(world.GetChunk({-1, 0}) == nullptr);
    CHECK(world.IsActive({16, 0}));
    CHECK_FALSE(world.IsActive({15, 0}));
    CHECK(world.CheckInvariants());
}

TEST_CASE("Buffered intent is transient while its active cooldown survives loading",
    "[server][save][buffering]") {
    SaveFile file;
    Server server(TestContent());
    REQUIRE(server.SetPacing({100, true, true}));
    REQUIRE(server.Send({}).advanced);
    REQUIRE(server.Send({CommandKind::move, 1, 0}).outcome == Outcome::queued);
    CHECK(server.Update(250'000).tick == 25);
    REQUIRE(server.CreateSave(file.path.c_str()));
    auto restored = Take(Server::LoadSave(TestContent(), file.path.c_str()));
    CHECK(server.View().commandQueued);
    CHECK_FALSE(restored->View().commandQueued);
    CHECK(restored->View().remainingTicks == 75);
    const auto loaded = restored->Update(750'000);
    CHECK(loaded.tick == 100);
    CHECK(loaded.needsInput);
    CHECK_FALSE(loaded.commandResult);
    CHECK(restored->View().player_position == TileCoord{0, 0});
    const auto original = server.Update(750'000);
    REQUIRE(original.commandResult);
    CHECK(original.commandResult->outcome == Outcome::moved);
    CHECK(server.View().player_position == TileCoord{1, 0});
}

TEST_CASE("Buffered movement reports storage failures at execution and clears the intent",
    "[server][save][buffering]") {
    SaveFile file;
    Server server(TestContent());
    REQUIRE(server.SetBubbleRadii(0, 1));
    World& world = server.GetWorld();
    REQUIRE(world.MoveActor(server.GetPlayer(), {15, 0}));
    REQUIRE(world.SetTile({16, 0}, {TestTerrain("floor"), 0}));
    REQUIRE(server.SetPacing({100, true, true}));
    REQUIRE(server.Send({}).advanced);
    REQUIRE(server.CreateSave(file.path.c_str()));
    {
        RawDatabase raw(file.path.c_str());
        raw.Exec("UPDATE chunks SET payload='{}' WHERE x=2 AND z=0 AND y=0");
    }
    const auto before = world.LoadedChunks();
    const auto accepted = server.Send({CommandKind::move, 1, 0});
    CHECK(accepted.outcome == Outcome::queued);
    CHECK_FALSE(accepted.storageError);
    CHECK(accepted.tick == 0);
    const auto update = server.Update(1'000'000);
    REQUIRE(update.commandResult);
    CHECK(update.commandResult->outcome == Outcome::unavailable);
    CHECK_FALSE(update.commandResult->advanced);
    CHECK(update.commandResult->tick == 100);
    REQUIRE(update.storageError);
    REQUIRE(update.commandResult->storageError);
    CHECK(*update.storageError == SaveError::InvalidData);
    CHECK(*update.commandResult->storageError == SaveError::InvalidData);
    CHECK(update.needsInput);
    CHECK_FALSE(server.View().commandQueued);
    CHECK(server.View().player_position == TileCoord{15, 0});
    CHECK(world.LoadedChunks() == before);
    CHECK(world.GetActor(server.GetPlayer())->turns == 1);
    CHECK_FALSE(server.Update(0).commandResult);
    CHECK(world.CheckInvariants());
}

TEST_CASE("Server load rejects unsupported schema and invalid or missing session players",
    "[server][save]") {
    SaveFile file;
    {
        Server server(TestContent());
        REQUIRE(server.CreateSave(file.path.c_str()));
    }
    SECTION("Unsupported schema") {
        RawDatabase raw(file.path.c_str());
        raw.Exec("UPDATE metadata SET format_version=999");
        Diagnostics diagnostics;
        const auto result = Server::LoadSave(TestContent(), file.path.c_str(), &diagnostics);
        REQUIRE_FALSE(result);
        CHECK(result.error() == SaveError::UnsupportedVersion);
        CHECK_FALSE(diagnostics.Empty());
    }
    SECTION("Malformed session") {
        RawDatabase raw(file.path.c_str());
        raw.Exec("UPDATE world_state SET payload='{}'");
        const auto result = Server::LoadSave(TestContent(), file.path.c_str());
        REQUIRE_FALSE(result);
        CHECK(result.error() == SaveError::InvalidData);
    }
    SECTION("Parked host is not a valid server session") {
        RawDatabase raw(file.path.c_str());
        raw.Exec("UPDATE chunks SET payload=json_set(payload, '$.actors[0].parked', json('true')) "
            "WHERE x=0 AND z=0 AND y=0");
        const auto result = Server::LoadSave(TestContent(), file.path.c_str());
        REQUIRE_FALSE(result);
        CHECK(result.error() == SaveError::InvalidData);
    }
    SECTION("Valid World checkpoint without a session player") {
        SaveDatabase database(TestContent());
        auto snapshot = Take(database.Open(file.path.c_str()));
        snapshot.session.player = 0;
        snapshot.session.players.clear();
        REQUIRE(database.Begin());
        REQUIRE(database.WriteSnapshot(snapshot));
        REQUIRE(database.Commit());
        Diagnostics diagnostics;
        const auto result = Server::LoadSave(TestContent(), file.path.c_str(), &diagnostics);
        REQUIRE_FALSE(result);
        CHECK(result.error() == SaveError::InvalidData);
        CHECK_FALSE(diagnostics.Empty());
    }
}

TEST_CASE("Guest checkpoint keeps parked inventory and reconnect identity", "[server][save][multiplayer]") {
    SaveFile file;
    Server server(TestContent());
    const auto joined = server.JoinPlayer("Visitor", 0, 8844);
    REQUIRE(joined);
    REQUIRE(server.ActivatePlayer(joined->player));
    const ActorHandle guest = server.GetWorld().FindActor(joined->player);
    REQUIRE(server.GetWorld().GetActor(guest));
    const ItemHandle item = Take(server.GetWorld().CreateItem(TestItem("bandage"),
        ItemLocation::Ground(server.GetWorld().GetActor(guest)->position)));
    REQUIRE(server.GetWorld().TransferItem(item, ItemLocation::Carried(guest)));
    const PersistentId itemId = server.GetWorld().GetItem(item)->id;
    REQUIRE(server.CreateSave(file.path.c_str()));
    auto restored = Take(Server::LoadSave(TestContent(), file.path.c_str()));
    REQUIRE(restored->GetPlayers().size() == 2);
    CHECK(restored->GetPlayers()[1].state == PlayerConnectionState::Parked);
    CHECK(restored->GetPlayers()[1].id == joined->player);
    const auto resumed = restored->JoinPlayer("Visitor", 8844, 0);
    REQUIRE(resumed);
    CHECK(resumed->player == joined->player);
    REQUIRE(restored->ActivatePlayer(resumed->player));
    const WorldView view = restored->ViewForPlayer(resumed->player);
    REQUIRE(view.inventory.size() == 1);
    CHECK(view.inventory[0].id == itemId);
    CHECK(view.inventory[0].type == TestItem("bandage"));
    CHECK(restored->GetWorld().CheckInvariants());
}

TEST_CASE("Batch parking validates every player before removing support",
    "[server][save][players][layers][gravity]") {
    Server server(TestContent());
    World& world = server.GetWorld();
    const TileCoord bottom{8, 8, 0};
    const TileCoord top{8, 8, 1};
    REQUIRE(world.SetTile(bottom, {TestTerrain("floor"), 0}));
    REQUIRE(world.SetTile(top, {TestTerrain("air"), 0}));
    const ActorHandle lower = Take(world.CreateActor(bottom, true));
    const ActorHandle upper = Take(world.CreateActor(top, true));
    ActorHandle invalid{};
    WorldError error = WorldError::invalid_handle;
    SECTION("invalid handle") {}
    SECTION("nonplayer") {
        invalid = world.ActorAt({5, 5});
        REQUIRE(invalid.value != 0);
        error = WorldError::invalid_request;
    }
    const ActorHandle rejected[]{lower, invalid};
    CHECK(world.ParkActors(rejected) == eastl::unexpected(error));
    CHECK_FALSE(world.GetActor(lower)->parked);
    CHECK_FALSE(world.GetActor(upper)->parked);
    CHECK(world.GetActor(upper)->position == top);
    REQUIRE(world.ParkActors({}));
    const ActorHandle accepted[]{lower, upper, lower};
    REQUIRE(world.ParkActors(accepted));
    REQUIRE(world.ParkActors(accepted));
    CHECK(world.GetActor(lower)->parked);
    CHECK(world.GetActor(upper)->parked);
    CHECK(world.GetActor(lower)->position == bottom);
    CHECK(world.GetActor(upper)->position == top);
    CHECK(world.CheckInvariants());
}

TEST_CASE("Loading stacked active guests parks them together before gravity",
    "[server][save][multiplayer][layers][gravity]") {
    SaveFile file;
    Server original(TestContent());
    REQUIRE(original.SetBubbleRadii(0, 0));
    const auto lower = Take(original.JoinPlayer("Lower", 0, 881));
    REQUIRE(original.ActivatePlayer(lower.player));
    const auto upper = Take(original.JoinPlayer("Upper", 0, 882));
    REQUIRE(original.ActivatePlayer(upper.player));
    World& world = original.GetWorld();
    const TileCoord bottom{8, 8, 0};
    const TileCoord top{8, 8, 1};
    REQUIRE(world.SetTile(bottom, {TestTerrain("floor"), 0}));
    REQUIRE(world.SetTile(top, {TestTerrain("air"), 0}));
    REQUIRE(world.MoveActor(world.FindActor(lower.player), bottom));
    REQUIRE(world.MoveActor(world.FindActor(upper.player), top));
    REQUIRE(world.GetActor(world.FindActor(upper.player))->position == top);
    REQUIRE(original.CreateSave(file.path.c_str()));
    SECTION("lower guest first") {}
    SECTION("upper guest first") {
        SaveDatabase database(TestContent());
        auto snapshot = Take(database.Open(file.path.c_str()));
        REQUIRE(snapshot.session.players.size() == 3);
        eastl::swap(snapshot.session.players[1], snapshot.session.players[2]);
        REQUIRE(database.Begin());
        REQUIRE(database.WriteSnapshot(snapshot));
        REQUIRE(database.Commit());
    }
    auto restored = Take(Server::LoadSave(TestContent(), file.path.c_str()));
    World& loaded = restored->GetWorld();
    for (const auto& guest : {lower, upper}) {
        const Actor* actor = loaded.GetActor(loaded.FindActor(guest.player));
        REQUIRE(actor);
        CHECK(actor->parked);
        CHECK(actor->position == (guest.player == lower.player ? bottom : top));
    }
    REQUIRE(restored->GetPlayers().size() == 3);
    CHECK(restored->GetPlayers()[1].state == PlayerConnectionState::Parked);
    CHECK(restored->GetPlayers()[2].state == PlayerConnectionState::Parked);
    REQUIRE(restored->Save());
    // With the lower guest still parked, the upper guest has lost its support.
    REQUIRE(restored->JoinPlayer("Upper", upper.reconnectToken, 0));
    REQUIRE(restored->ActivatePlayer(upper.player));
    CHECK(loaded.GetActor(loaded.FindActor(upper.player))->position == bottom);
    CHECK_FALSE(loaded.GetActor(loaded.FindActor(upper.player))->parked);
    CHECK(loaded.GetActor(loaded.FindActor(lower.player))->position == bottom);
    CHECK(loaded.GetActor(loaded.FindActor(lower.player))->parked);
    CHECK(loaded.CheckInvariants());
    REQUIRE(restored->Save());
}

TEST_CASE("Loading parks guest support and refreshes the fallen host position",
    "[server][save][multiplayer][layers][gravity]") {
    SaveFile file;
    Server original(TestContent());
    REQUIRE(original.SetBubbleRadii(0, 0));
    const auto guest = Take(original.JoinPlayer("Support", 0, 883));
    REQUIRE(original.ActivatePlayer(guest.player));
    World& world = original.GetWorld();
    const TileCoord bottom{8, 8, 0};
    const TileCoord top{8, 8, 1};
    REQUIRE(world.SetTile(bottom, {TestTerrain("floor"), 0}));
    REQUIRE(world.SetTile(top, {TestTerrain("air"), 0}));
    REQUIRE(world.MoveActor(world.FindActor(guest.player), bottom));
    REQUIRE(world.MoveActor(original.GetPlayer(), top));
    REQUIRE(original.CreateSave(file.path.c_str()));
    REQUIRE(ReadSession(file).playerPosition == top);
    auto restored = Take(Server::LoadSave(TestContent(), file.path.c_str()));
    const World& loaded = restored->GetWorld();
    CHECK(loaded.GetActor(restored->GetPlayer())->position == bottom);
    CHECK(restored->View().player_position == bottom);
    REQUIRE(restored->GetPlayers().size() == 2);
    const Actor* support = loaded.GetActor(loaded.FindActor(guest.player));
    REQUIRE(support);
    CHECK(support->parked);
    CHECK(support->position == bottom);
    REQUIRE(restored->Save());
    const auto session = ReadSession(file);
    CHECK(session.playerPosition == bottom);
    CHECK(session.players[0].position == bottom);
    auto reopened = Take(Server::LoadSave(TestContent(), file.path.c_str()));
    CHECK(reopened->View().player_position == bottom);
    CHECK(reopened->GetWorld().CheckInvariants());
}

TEST_CASE("Shuffled persisted roster resolves rounds by ID with independent opportunities",
    "[server][save][multiplayer]") {
    SaveFile file;
    Server original(TestContent());
    const auto first = original.JoinPlayer("First", 0, 801);
    const auto second = original.JoinPlayer("Second", 0, 802);
    REQUIRE(first);
    REQUIRE(second);
    REQUIRE(original.CreateSave(file.path.c_str()));
    {
        SaveDatabase database(TestContent());
        auto snapshot = Take(database.Open(file.path.c_str()));
        REQUIRE(snapshot.session.players.size() == 3);
        eastl::swap(snapshot.session.players[0], snapshot.session.players[2]);
        REQUIRE(database.Begin());
        REQUIRE(database.WriteSnapshot(snapshot));
        REQUIRE(database.Commit());
    }
    auto server = Take(Server::LoadSave(TestContent(), file.path.c_str()));
    REQUIRE(server->GetPlayers().size() == 3);
    CHECK(server->GetPlayers()[0].id == server->GetHostPlayerId());
    REQUIRE(server->SetPacing({100, true, true}));
    REQUIRE(server->JoinPlayer("First", 801, 0));
    REQUIRE(server->JoinPlayer("Second", 802, 0));
    REQUIRE(server->ActivatePlayer(first->player));
    REQUIRE(server->ActivatePlayer(second->player));
    const ActorHandle older = server->GetWorld().FindActor(first->player);
    REQUIRE(server->GetWorld().ScheduleActorAction(older));
    const PersistentId host = server->GetHostPlayerId();
    REQUIRE(server->Submit(second->player, {1, 1, {}}).outcome == Outcome::queued);
    REQUIRE(server->Submit(host, {1, 1, {}}).outcome == Outcome::queued);
    const auto partial = server->Update(0);
    REQUIRE(partial.commandResults.size() == 2);
    CHECK(partial.commandResults[0].player == host);
    CHECK(partial.commandResults[1].player == second->player);
    CHECK(server->ViewForPlayer(first->player).opportunity == 1);
    CHECK(server->ViewForPlayer(second->player).opportunity == 2);
    REQUIRE(server->Submit(second->player, {2, 2, {}}).outcome == Outcome::queued);
    REQUIRE(server->Submit(first->player, {1, 1, {}}).outcome == Outcome::queued);
    REQUIRE(server->Submit(host, {2, 2, {}}).outcome == Outcome::queued);
    const auto round = server->Update(1'000'000);
    REQUIRE(round.commandResults.size() == 3);
    CHECK(round.commandResults[0].player == host);
    CHECK(round.commandResults[1].player == first->player);
    CHECK(round.commandResults[2].player == second->player);
}

TEST_CASE("Checkpoint reads administratively moved archived parked player position",
    "[server][save][multiplayer]") {
    SaveFile file;
    Server server(TestContent());
    const auto joined = server.JoinPlayer("Guest", 0, 871);
    REQUIRE(joined);
    const ActorHandle guest = server.GetWorld().FindActor(joined->player);
    const TileCoord original = server.GetWorld().GetActor(guest)->position;
    const ChunkCoord chunk = ChunkOf(original);
    const TileCoord moved{chunk.x * chunk_size
        + (original.x == chunk.x * chunk_size ? 1 : 0), original.z, original.y};
    REQUIRE(ChunkOf(original) == ChunkOf(moved));
    REQUIRE(server.GetWorld().SetTile(moved, {TestTerrain("floor"), 0}));
    REQUIRE(server.GetWorld().MoveActor(guest, moved));
    REQUIRE(server.DisconnectPlayer(joined->player));
    REQUIRE(server.SetBubbleRadii(0, 0));
    CHECK(server.GetWorld().FindPlayerPosition(joined->player) == moved);
    REQUIRE(server.CreateSave(file.path.c_str()));
    const auto session = ReadSession(file);
    REQUIRE(session.players.size() == 2);
    CHECK(session.players[1].position == moved);
    auto loaded = Take(Server::LoadSave(TestContent(), file.path.c_str()));
    REQUIRE(loaded->JoinPlayer("Guest", 871, 0));
    REQUIRE(loaded->ActivatePlayer(joined->player));
    CHECK(loaded->ViewForPlayer(joined->player).player_position == moved);
}

TEST_CASE("Parked boundary guests reload after the host enlarges residency",
    "[server][save][multiplayer][streaming][circle]") {
    bool circle = true;
    SECTION("circle policy") {}
    SECTION("legacy square policy") { circle = false; }
    for (int boundary : {world_coordinate_limit - 1, -world_coordinate_limit + 1}) {
        CAPTURE(boundary, circle);
        SaveFile file;
        Server server(TestContent());
        REQUIRE(server.SetBubbleRadii(0, 0));
        const TileCoord position{boundary, boundary};
        const auto joined = Take(server.JoinPlayer("Boundary guest", 0, 873));
        World& world = server.GetWorld();
        const auto request = Take(world.RequestChunks({ChunkOf(position), 0}, LoadMode::resident));
        REQUIRE(world.ReconcileChunks());
        REQUIRE(world.SetTile(position, {TestTerrain("floor"), 0}));
        REQUIRE(world.MoveActor(world.FindActor(joined.player), position));
        REQUIRE(server.DisconnectPlayer(joined.player));
        REQUIRE(world.ReleaseRequest(request));
        REQUIRE(world.ReconcileChunks());
        if (circle) {
            REQUIRE(server.SetCircleResidency(0, {78}));
        } else {
            REQUIRE(server.SetBubbleRadii(1, 5));
        }
        REQUIRE(server.CreateSave(file.path.c_str()));
        auto restored = Take(Server::LoadSave(TestContent(), file.path.c_str()));
        REQUIRE(restored->GetPlayers().size() == 2);
        CHECK(restored->GetPlayers()[1].state == PlayerConnectionState::Parked);
        CHECK(restored->GetWorld().FindPlayerPosition(joined.player) == position);
        CHECK(restored->GetCircleResidency() == server.GetCircleResidency());
        CHECK(restored->GetActiveRadius() == server.GetActiveRadius());
        REQUIRE(restored->Save());
        REQUIRE(restored->JoinPlayer("Boundary guest", 873, 0));
        CHECK(restored->ActivatePlayer(joined.player)
            == eastl::unexpected(SessionError::Unavailable));
        CHECK(restored->GetPlayers()[1].state == PlayerConnectionState::Joining);
        const auto* guest = restored->GetWorld().GetActor(restored->GetWorld().FindActor(joined.player));
        REQUIRE(guest);
        CHECK(guest->parked);
        CHECK(guest->position == position);
        CHECK(restored->GetWorld().CheckInvariants());
    }
}

TEST_CASE("Reconnect staging failure leaves parked guest joining without relocating",
    "[server][save][multiplayer][streaming]") {
    SaveFile file;
    Server server(TestContent());
    const auto joined = server.JoinPlayer("Guest", 0, 872);
    REQUIRE(joined);
    World& world = server.GetWorld();
    const ActorHandle guest = world.FindActor(joined->player);
    REQUIRE(world.SetTile({15, 1}, {TestTerrain("floor"), 0}));
    REQUIRE(world.SetTile({16, 0}, {TestTerrain("floor"), 0}));
    for (TileCoord wall : {TileCoord{14, 0}, TileCoord{15, 0}, TileCoord{14, 1},
             TileCoord{14, 2}, TileCoord{15, 2}}) {
        REQUIRE(world.SetTile(wall, {TestTerrain("wall"), 0}));
    }
    REQUIRE(world.MoveActor(guest, {15, 1}));
    REQUIRE(server.DisconnectPlayer(joined->player));
    REQUIRE(server.SetBubbleRadii(0, 0));
    REQUIRE(world.CreateActor({15, 1}));
    REQUIRE(server.CreateSave(file.path.c_str()));
    {
        RawDatabase raw(file.path.c_str());
        raw.Exec("UPDATE chunks SET payload='{}' WHERE x=1 AND z=0 AND y=0");
    }
    REQUIRE(server.JoinPlayer("Guest", 872, 0));
    CHECK(server.ActivatePlayer(joined->player)
        == eastl::unexpected(SessionError::StorageFailure));
    CHECK(server.GetPlayers()[1].state == PlayerConnectionState::Joining);
    CHECK(world.GetActor(world.FindActor(joined->player))->parked);
    CHECK(world.GetActor(world.FindActor(joined->player))->position == TileCoord{15, 1});
    CHECK(world.CheckInvariants());
}

}
