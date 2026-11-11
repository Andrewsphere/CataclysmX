#include "core/server.h"
#include "core/simulation.h"
#include "content_fixture.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>

namespace {
using namespace cataclysmx;

const ViewCell& Cell(const WorldView& view, int x, int z) {
    const int index = (z - view.origin.z) * view.width + x - view.origin.x;
    REQUIRE(index >= 0);
    REQUIRE(static_cast<std::size_t>(index) < view.cells.size());
    return view.cells[static_cast<std::size_t>(index)];
}

TEST_CASE("Views expose current appearance without teaching memory or using viewport as sight range",
    "[server][perception][memory]") {
    Server server(TestContent());
    World& world = server.GetWorld();
    const auto player = server.GetHostPlayerId();
    const auto object = world.ObjectAt({2, 0});
    REQUIRE(world.SetObjectAppearance(object, ObjectKind::Chair));
    const auto memoryBefore = world.GetPlayerMemories();
    const auto narrow = server.View(2);
    const auto wide = server.View(12);
    CHECK(Cell(narrow, 2, 0) == Cell(wide, 2, 0));
    CHECK(Cell(narrow, 2, 0).objectAppearance.kind == ObjectKind::Chair);
    CHECK(world.GetPlayerMemories() == memoryBefore);
    CHECK(world.GetRememberedTile(player, {2, 0}).object.kind == ObjectKind::Generic);

    REQUIRE(server.Send({}).advanced);
    CHECK(world.GetRememberedTile(player, {2, 0}).object.kind == ObjectKind::Chair);
    REQUIRE(world.SetTile({1, 0}, {TestTerrain("wall"), 0}));
    REQUIRE(world.SetObjectAppearance(object, ObjectKind::Table));
    const auto hidden = server.View();
    const auto& cell = Cell(hidden, 2, 0);
    CHECK(cell.terrainVisibility == Visibility::Remembered);
    CHECK(cell.overlayVisibility == Visibility::Remembered);
    CHECK(cell.objectAppearance.kind == ObjectKind::Chair);
    CHECK(cell.object == 0);
    CHECK(cell.actor == 0);
    CHECK(cell.ground_items == 0);
    CHECK(cell.terrainLight == LightBand::Dark);
    CHECK(cell.overlayLight == LightBand::Dark);

    REQUIRE(world.DestroyObject(object));
    REQUIRE(world.SetTile({1, 0}, {TestTerrain("floor"), 0}));
    REQUIRE(server.Send({}).advanced);
    CHECK(world.GetRememberedTile(player, {2, 0}).object.kind == ObjectKind::None);
}

TEST_CASE("Command replay learns the same map with different snapshot cadence",
    "[server][perception][determinism]") {
    Server sampled(TestContent());
    Server headless(TestContent());
    const Command commands[] = {{CommandKind::move, 0, -1}, {},
        {CommandKind::move, 1, 0}, {CommandKind::move, 0, 1},
        {CommandKind::interact, 1, 0}, {CommandKind::move, -1, 0}};
    for (const auto& command : commands) {
        sampled.View(0);
        sampled.View(12);
        CHECK(sampled.Send(command).outcome == headless.Send(command).outcome);
        sampled.View(3);
        CHECK(sampled.GetWorld().GetPlayerMemories() == headless.GetWorld().GetPlayerMemories());
    }
    CHECK(sampled.View().cells == headless.View().cells);
    CHECK(sampled.GetTick() == headless.GetTick());
}

TEST_CASE("Server snapshots reuse maps while World edits and metadata remain authoritative",
    "[server][perception][cache]") {
    Server server(TestContent());
    World& world = server.GetWorld();
    const auto before = server.View(8);
    const auto memories = world.GetPlayerMemories();
    server.ResetViewStats();
    world.ResetPerceptionStats();
    const auto input = server.GetInputContext(server.GetHostPlayerId());
    CHECK(input.opportunity == before.opportunity);
    CHECK(input.lastInputSequence == before.lastInputSequence);
    CHECK(input.revision == before.revision);
    CHECK(server.GetViewStats().requests == 0);
    CHECK(server.View(8).cells == before.cells);
    CHECK(server.GetViewStats().constructions == 0);
    CHECK(world.GetPerceptionStats().geometryRebuilds == 0);

    REQUIRE(world.SetActorSpeed(server.GetPlayer(), 125));
    const auto status = server.View(8);
    CHECK(status.speed == 125);
    CHECK(status.revision > before.revision);
    CHECK(status.cells == before.cells);
    CHECK(server.GetViewStats().mapConstructions == 0);

    const auto object = world.ObjectAt({2, 0});
    REQUIRE(world.SetObjectAppearance(object, ObjectKind::Chair));
    const auto changed = server.View(8);
    CHECK(Cell(changed, 2, 0).objectAppearance.kind == ObjectKind::Chair);
    CHECK(server.GetViewStats().mapConstructions == 1);
    CHECK(world.GetPerceptionStats().geometryRebuilds == 0);
    CHECK(world.GetPerceptionStats().observationScans == 0);
    CHECK(world.GetPlayerMemories() == memories);

    const auto npc = world.CreateActor({0, 1});
    REQUIRE(npc);
    REQUIRE(world.MoveActor(*npc, {1, 1}));
    const auto overlay = server.View(8);
    CHECK(Cell(overlay, 1, 1).actor == world.GetActor(*npc)->id);
    CHECK(world.GetPerceptionStats().geometryRebuilds == 0);
    CHECK(world.GetPerceptionStats().observationScans == 0);
    REQUIRE(world.SetTile({1, 0}, {TestTerrain("wall"), 0}));
    const auto hidden = server.View(8);
    CHECK(Cell(hidden, 2, 0).overlayVisibility == Visibility::Remembered);
    CHECK(world.GetPerceptionStats().geometryRebuilds == 1);
    CHECK(world.GetPlayerMemories() == memories);
}

TEST_CASE("Timed world effects teach observations inside a still-busy action",
    "[server][perception][scheduling]") {
    Server server(TestContent());
    World& world = server.GetWorld();
    const auto object = world.ObjectAt({2, 0});
    REQUIRE(world.SetObjectAppearance(object, ObjectKind::Table));
    REQUIRE(world.SetTile({1, 0}, {TestTerrain("closed_door"), 0}));
    const auto device = world.CreateItem(TestItem("door_controller"), ItemLocation::Ground({0, 0}));
    REQUIRE(device);
    const auto capability = FindItemCapability(TestContent().GetItem(TestItem("door_controller")), "timer");
    REQUIRE(world.UseItem(server.GetPlayer(), *device,
        {capability, "arm", {}, 1, 0}).outcome == ItemUseOutcome::Succeeded);
    REQUIRE(world.ScheduleActorAction(server.GetPlayer(), 500));
    Tick tick = 0;
    std::uint32_t rng = 42;
    const auto partial = AdvanceUntilInput(world, tick, rng, 350);
    CHECK(partial.stop == SimulationStop::LimitReached);
    CHECK(world.GetActorRemainingTicks(server.GetPlayer()) == 150);
    CHECK(world.GetRememberedTile(server.GetHostPlayerId(), {2, 0}).object.kind == ObjectKind::Table);
    // Close it before the input barrier; no rendering was requested while open.
    REQUIRE(world.SetTile({1, 0}, {TestTerrain("closed_door"), 0}));
    CHECK(AdvanceUntilInput(world, tick, rng, 500).stop == SimulationStop::InputRequired);
    CHECK(world.GetRememberedTile(server.GetHostPlayerId(), {2, 0}).object.kind == ObjectKind::Table);
}

TEST_CASE("A visible open column reveals lower terrain past a neighboring platform",
    "[server][perception][3d]") {
    Server server(TestContent());
    World& world = server.GetWorld();
    REQUIRE(world.SetTile({0, 0, 1}, {TestTerrain("floor"), 0}));
    REQUIRE(world.SetTile({1, 0, 1}, {TestTerrain("air"), 0}));
    REQUIRE(world.SetTile({2, 0, 1}, {TestTerrain("air"), 0}));
    REQUIRE(world.MoveActor(server.GetPlayer(), {0, 0, 1}));
    const auto support = world.CreateActor({2, 0, 0});
    REQUIRE(support);
    const auto upper = world.CreateActor({2, 0, 1});
    REQUIRE(upper);
    world.UpdatePlayerObservations();
    REQUIRE(Cell(server.View(), 2, 0).terrainDepth == 1);
    REQUIRE(world.SetTile({1, 0, 1}, {TestTerrain("floor"), 0}));
    REQUIRE(world.SetTile({2, 0, 0}, {TestTerrain("grass"), 73}));
    const auto projected = server.View();
    const auto& cell = Cell(projected, 2, 0);
    CHECK(cell.terrainVisibility == Visibility::Visible);
    CHECK(cell.tile == Tile{TestTerrain("grass"), 73});
    CHECK(cell.terrainDepth == 1);
    CHECK(cell.overlayVisibility == Visibility::Visible);
    CHECK(cell.actor == world.GetActor(*upper)->id);
    CHECK(cell.overlayDepth == 0);
    CHECK(world.GetRememberedTile(server.GetHostPlayerId(), {2, 0, 0}).terrain == TestTerrain("floor"));
}

TEST_CASE("Balcony columns reveal lit grass and actors through dark air until a roof",
    "[server][perception][columns][memory]") {
    Server server(TestContent());
    World& world = server.GetWorld();
    for (int y = 1; y <= 3; ++y) {
        for (int x = 0; x <= 2; ++x) {
            REQUIRE(world.SetTile({x, 0, y}, {TestTerrain("air"), 0}));
        }
    }
    REQUIRE(world.SetTile({0, 0, 3}, {TestTerrain("floor"), 0}));
    REQUIRE(world.SetTile({1, 0, 3}, {TestTerrain("floor"), 0}));
    REQUIRE(world.SetTile({2, 0, 0}, {TestTerrain("grass"), 0}));
    REQUIRE(world.MoveActor(server.GetPlayer(), {0, 0, 3}));
    const auto zombie = world.CreateActor({2, 0, 0});
    REQUIRE(zombie);
    REQUIRE(world.SetActorLight(*zombie, {64, 1}));
    world.SetSkyLight(0);
    REQUIRE(world.GetLightLevel({2, 0, 3}) == 0);
    const auto before = world.GetPlayerMemories();
    const auto view = server.View(64);
    REQUIRE(view.width == 129);
    const auto& cell = Cell(view, 2, 0);
    CHECK(cell.terrainVisibility == Visibility::Visible);
    CHECK(cell.tile.terrain == TestTerrain("grass"));
    CHECK(cell.terrainDepth == 3);
    CHECK(cell.overlayVisibility == Visibility::Visible);
    CHECK(cell.actor == world.GetActor(*zombie)->id);
    CHECK(cell.overlayDepth == 3);
    CHECK(world.GetPlayerMemories() == before);
    world.UpdatePlayerObservations();
    CHECK(world.GetRememberedTile(server.GetHostPlayerId(), {2, 0, 0}).terrain == TestTerrain("grass"));
    REQUIRE(world.SetTile({2, 0, 2}, {TestTerrain("floor"), 0}));
    CHECK_FALSE(world.CanPlayerSee(server.GetPlayer(), {2, 0, 0}));
    CHECK(Cell(server.View(64), 2, 0).actor == 0);
}

TEST_CASE("Distant optical edits and lights leave unrelated observer caches warm",
    "[perception][cache][lighting]") {
    World world(TestContent());
    for (int chunkX : {0, 4}) {
        REQUIRE(world.RequestChunks({{chunkX, 0}, 0}, LoadMode::active));
    }
    REQUIRE(world.ReconcileChunks());
    VisionProfile vision;
    vision.range = 6;
    REQUIRE(world.SetVisionProfile(vision));
    for (int chunkX : {0, 4}) {
        for (int z = 0; z < chunk_size; ++z) {
            for (int x = chunkX * chunk_size; x < (chunkX + 1) * chunk_size; ++x) {
                for (int y = 0; y <= world_max_y; ++y) {
                    REQUIRE(world.SetTile({x, z, y}, {TestTerrain(y == 0 ? "floor" : "air"), 0}));
                }
            }
        }
    }
    const auto first = world.CreateActor({4, 4, 0}, true);
    const auto second = world.CreateActor({68, 4, 0}, true);
    REQUIRE(first);
    REQUIRE(second);
    REQUIRE(world.CanPlayerSee(*first, {6, 4, 0}));
    REQUIRE(world.CanPlayerSee(*second, {70, 4, 0}));
    world.ResetPerceptionStats();
    REQUIRE(world.SetTile({69, 4, 0}, {TestTerrain("wall"), 0}));
    CHECK(world.CanPlayerSee(*first, {6, 4, 0}));
    CHECK(world.GetPerceptionStats().geometryRebuilds == 0);
    CHECK(world.GetPerceptionStats().filterRebuilds == 0);
    CHECK_FALSE(world.CanPlayerSee(*second, {70, 4, 0}));
    CHECK(world.GetPerceptionStats().geometryRebuilds == 1);
    CHECK(world.GetPerceptionStats().filterRebuilds == 1);
    const auto lamp = world.CreateObject({70, 5, 0}, true);
    REQUIRE(lamp);
    REQUIRE(world.SetObjectLight(*lamp, {192, 2}));
    world.ResetPerceptionStats();
    CHECK(world.CanPlayerSee(*first, {6, 4, 0}));
    CHECK(world.GetPerceptionStats().geometryRebuilds == 0);
    CHECK(world.GetPerceptionStats().filterRebuilds == 0);
    CHECK(world.CanPlayerSee(*second, {68, 4, 0}));
    CHECK(world.GetPerceptionStats().geometryRebuilds == 0);
    CHECK(world.GetPerceptionStats().filterRebuilds == 1);
}

TEST_CASE("Upper-plane lamp spill invalidates cached receiving visibility without revealing upward",
    "[server][perception][cache][lighting][memory]") {
    Server server(TestContent());
    World& world = server.GetWorld();
    const auto observer = server.GetPlayer();
    const auto observerId = server.GetHostPlayerId();
    const TileCoord target{8, 0, 0};
    const TileCoord source{8, 0, 1};
    for (int x = 0; x <= 8; ++x) {
        REQUIRE(world.SetTile({x, 0, 0}, {TestTerrain("floor"), 0}));
        REQUIRE(world.SetTile({x, 0, 1}, {TestTerrain("floor"), 0}));
    }
    REQUIRE(world.SetTile(target, {TestTerrain("stairs"), 0}));
    REQUIRE(world.SetTile(source, {TestTerrain("stairs"), 0}));
    const auto lamp = world.CreateObject(source, true);
    REQUIRE(lamp);
    REQUIRE(world.SetObjectLight(*lamp, {255, 1}));
    world.SetSkyLight(0);
    VisionProfile vision;
    vision.range = 12;
    vision.dimRange = 12;
    REQUIRE(world.SetVisionProfile(vision));
    REQUIRE(world.GetLightLevel(target) == 63); // One vertical spill hop, not direct light.
    REQUIRE(world.GetLightLevel({0, 0, 0}) == 0);
    REQUIRE(world.CanPlayerSee(observer, target));
    REQUIRE_FALSE(world.CanPlayerSee(observer, source));
    world.UpdatePlayerObservations();
    REQUIRE(world.GetRememberedTile(observerId, target).terrain == TestTerrain("stairs"));
    const auto memories = world.GetPlayerMemories();
    REQUIRE(Cell(server.View(12), target.x, target.z).terrainVisibility == Visibility::Visible);
    const auto receivingInput = world.GetLightingInputRevision(ChunkOf(target));
    auto sourceInput = world.GetLightingInputRevision(ChunkOf(source));

    for (const bool enabled : {false, true}) {
        CAPTURE(enabled);
        world.ResetPerceptionStats();
        REQUIRE(world.SetObjectEnabled(*lamp, enabled));
        CHECK(world.GetLightingInputRevision(ChunkOf(target)) == receivingInput);
        const auto nextSourceInput = world.GetLightingInputRevision(ChunkOf(source));
        CHECK(nextSourceInput > sourceInput);
        // Resolve the pending light separately: observer invalidation must survive
        // until a later perception/view query, despite unchanged receiving inputs.
        REQUIRE(world.GetLightLevel(target) == (enabled ? 63 : 0));
        CHECK(world.GetPerceptionStats().filterRebuilds == 0);
        CHECK(world.CanPlayerSee(observer, target) == enabled);
        CHECK_FALSE(world.CanPlayerSee(observer, source));
        const auto view = server.View(12);
        const auto& cell = Cell(view, target.x, target.z);
        CHECK(cell.terrainVisibility == (enabled ? Visibility::Visible : Visibility::Remembered));
        CHECK(cell.tile.terrain == TestTerrain("stairs"));
        CHECK(cell.terrainDepth == 0);
        CHECK(cell.terrainLight == (enabled ? LightBand::Dim : LightBand::Dark));
        CHECK(cell.object == 0); // The lamp's own plane is never projected upward.
        CHECK(world.GetPerceptionStats().geometryRebuilds == 0);
        CHECK(world.GetPerceptionStats().filterRebuilds == 1);
        CHECK(world.GetPerceptionStats().lightingRebuilds == 1);
        CHECK(world.GetPerceptionStats().observationScans == 0);
        CHECK(world.GetPlayerMemories() == memories);
        sourceInput = nextSourceInput;
    }
}

TEST_CASE("Hidden geometry changes cannot alter the depth of remembered projection",
    "[server][perception][3d][memory]") {
    Server server(TestContent());
    World& world = server.GetWorld();
    REQUIRE(world.SetTile({0, 0, 1}, {TestTerrain("floor"), 0}));
    REQUIRE(world.SetTile({1, 0, 1}, {TestTerrain("air"), 0}));
    REQUIRE(world.SetTile({2, 0, 1}, {TestTerrain("air"), 0}));
    REQUIRE(world.MoveActor(server.GetPlayer(), {0, 0, 1}));
    world.UpdatePlayerObservations();
    REQUIRE(world.SetTile({1, 0, 1}, {TestTerrain("wall"), 0}));
    const auto before = server.View();
    REQUIRE(Cell(before, 2, 0).terrainVisibility == Visibility::Remembered);
    REQUIRE(Cell(before, 2, 0).terrainDepth == 1);
    REQUIRE(world.SetTile({2, 0, 1}, {TestTerrain("floor"), 0}));
    CHECK(Cell(server.View(), 2, 0) == Cell(before, 2, 0));
}

TEST_CASE("Remembered upper furniture cannot hide a visible lower actor",
    "[server][perception][3d][memory]") {
    Server server(TestContent());
    World& world = server.GetWorld();
    REQUIRE(world.SetTile({0, 0, 1}, {TestTerrain("floor"), 0}));
    for (int x = 1; x <= 8; ++x) {
        REQUIRE(world.SetTile({x, 0, 0}, {TestTerrain("floor"), 0}));
        REQUIRE(world.SetTile({x, 0, 1}, {TestTerrain("air"), 0}));
    }
    REQUIRE(world.MoveActor(server.GetPlayer(), {0, 0, 1}));
    const auto chair = world.CreateObject({8, 0, 1});
    REQUIRE(chair);
    REQUIRE(world.SetObjectAppearance(*chair, ObjectKind::Chair));
    world.UpdatePlayerObservations();
    REQUIRE(world.GetRememberedTile(server.GetHostPlayerId(), {8, 0, 1}).object.kind == ObjectKind::Chair);
    REQUIRE(world.DestroyObject(*chair));
    const auto lower = world.CreateActor({8, 0, 0});
    REQUIRE(lower);
    REQUIRE(world.SetActorLight(*lower, {64, 1}));
    world.SetSkyLight(0);
    REQUIRE(ClassifyLight(world.GetLightLevel({8, 0, 1})) == LightBand::Dim);
    REQUIRE(ClassifyLight(world.GetLightLevel({8, 0, 0})) == LightBand::Ordinary);
    const auto view = server.View(12);
    const auto& cell = Cell(view, 8, 0);
    CHECK(cell.overlayVisibility == Visibility::Visible);
    CHECK(cell.actor == world.GetActor(*lower)->id);
    CHECK(cell.overlayDepth == 1);
    CHECK(cell.object == 0);
    CHECK(cell.objectAppearance == ObjectAppearance{});
    CHECK(world.GetRememberedTile(server.GetHostPlayerId(), {8, 0, 1}).object.kind == ObjectKind::Chair);
}

TEST_CASE("Lit lower terrain remains visible through previously unknown dark air",
    "[server][perception][3d][lighting]") {
    Server server(TestContent());
    World& world = server.GetWorld();
    REQUIRE(world.SetTile({16, 0, 1}, {TestTerrain("floor"), 0}));
    for (int x = 17; x <= 24; ++x) {
        REQUIRE(world.SetTile({x, 0, 0}, {TestTerrain("floor"), 0}));
        REQUIRE(world.SetTile({x, 0, 1}, {TestTerrain("air"), 0}));
    }
    REQUIRE(world.MoveActor(server.GetPlayer(), {16, 0, 1}));
    const auto lower = world.CreateActor({24, 0, 0});
    REQUIRE(lower);
    REQUIRE(world.SetActorLight(*lower, {64, 1}));
    world.SetSkyLight(0);
    REQUIRE(world.GetRememberedTile(server.GetHostPlayerId(), {24, 0, 1}).terrain == TerrainId{});
    const auto view = server.View(12);
    const auto& cell = Cell(view, 24, 0);
    CHECK(cell.terrainVisibility == Visibility::Visible);
    CHECK(cell.terrainDepth == 1);
    CHECK(cell.tile.terrain == TestTerrain("floor"));
    CHECK(cell.actor == world.GetActor(*lower)->id);
    CHECK(cell.overlayVisibility == Visibility::Visible);
    CHECK(world.GetRememberedTile(server.GetHostPlayerId(), {24, 0, 1}).terrain == TerrainId{});
}

TEST_CASE("Joining and parked views reveal only previously learned map knowledge",
    "[server][perception][multiplayer]") {
    Server server(TestContent());
    const auto joined = server.JoinPlayer("Observer", 0, 12345);
    REQUIRE(joined);
    const auto initial = server.ViewForPlayer(joined->player);
    for (const auto& cell : initial.cells) { CHECK(cell == ViewCell{}); }
    REQUIRE(server.ActivatePlayer(joined->player));
    REQUIRE(server.DisconnectPlayer(joined->player));
    World& world = server.GetWorld();
    const auto guestBefore = world.GetRememberedTile(joined->player, {2, 0});
    REQUIRE(world.SetObjectAppearance(world.ObjectAt({2, 0}), ObjectKind::Table));
    world.UpdatePlayerObservations();
    CHECK(world.GetRememberedTile(joined->player, {2, 0}) == guestBefore);
    const auto parked = server.ViewForPlayer(joined->player);
    CHECK(parked.groundItems.empty());
    for (const auto& cell : parked.cells) {
        CHECK(cell.terrainVisibility != Visibility::Visible);
        CHECK(cell.overlayVisibility != Visibility::Visible);
        CHECK(cell.actor == 0);
        CHECK(cell.object == 0);
        CHECK(cell.ground_items == 0);
    }
}

TEST_CASE("Each same-tick player action observes transiently open rooms for all participants",
    "[server][perception][multiplayer][scheduling]") {
    Server server(TestContent());
    World& world = server.GetWorld();
    const auto guest = server.JoinPlayer("Guest", 0, 12345);
    REQUIRE(guest);
    REQUIRE(server.ActivatePlayer(guest->player));
    REQUIRE(world.MoveActor(world.FindActor(guest->player), {1, 1}));
    REQUIRE(world.SetTile({1, 0}, {TestTerrain("closed_door"), 0}));
    REQUIRE(world.SetObjectAppearance(world.ObjectAt({2, 0}), ObjectKind::Chair));
    const auto host = server.GetHostPlayerId();
    CHECK(world.GetRememberedTile(host, {2, 0}).object.kind == ObjectKind::Generic);
    REQUIRE(server.Submit(host, {1, server.View(0).opportunity,
        {CommandKind::interact, 1, 0}}).outcome == Outcome::queued);
    REQUIRE(server.Submit(guest->player, {1, server.ViewForPlayer(guest->player, 0).opportunity,
        {CommandKind::interact, 0, -1}}).outcome == Outcome::queued);
    const auto update = server.Update(0);
    REQUIRE(update.commandResults.size() == 2);
    CHECK(update.commandResults[0].result.outcome == Outcome::opened);
    CHECK(update.commandResults[1].result.outcome == Outcome::closed);
    CHECK(update.commandResults[0].result.tick == update.commandResults[1].result.tick);
    CHECK(world.GetTile({1, 0})->terrain == TestTerrain("closed_door"));
    CHECK(world.GetRememberedTile(host, {2, 0}).object.kind == ObjectKind::Chair);
    CHECK(world.GetRememberedTile(guest->player, {2, 0}).object.kind == ObjectKind::Chair);
}

TEST_CASE("Server checkpoints restore stale personal memories and lighting without observing anew",
    "[server][perception][save]") {
    struct SaveFile {
        std::filesystem::path directory = std::filesystem::temp_directory_path()
            / ("cataclysmx-memory-session-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        SaveFile() { REQUIRE(std::filesystem::create_directory(directory)); }
        ~SaveFile() { std::error_code error; std::filesystem::remove_all(directory, error); }
    } file;
    Server server(TestContent());
    World& world = server.GetWorld();
    const auto object = world.ObjectAt({2, 0});
    REQUIRE(world.SetObjectAppearance(object, ObjectKind::Chair, ObjectFacing::West, true));
    REQUIRE(server.Send({}).advanced);
    const auto guest = server.JoinPlayer("Guest", 0, 12345);
    REQUIRE(guest);
    REQUIRE(server.ActivatePlayer(guest->player));
    REQUIRE(server.DisconnectPlayer(guest->player));
    REQUIRE(world.SetTile({1, 0}, {TestTerrain("wall"), 0}));
    REQUIRE(world.SetObjectAppearance(object, ObjectKind::Table));
    REQUIRE(world.SetObjectLight(object, {60, 10}));
    REQUIRE(world.SetObjectEnabled(object, true));
    REQUIRE(world.SetActorLight(server.GetPlayer(), {192, 1}));
    world.SetSkyLight(0);
    const auto memories = world.GetPlayerMemories();
    const auto path = (file.directory / "session.sqlite").string();
    REQUIRE(server.CreateSave(path.c_str()));
    CHECK(world.GetPlayerMemories() == memories);
    auto loaded = Server::LoadSave(TestContent(), path.c_str());
    REQUIRE(loaded);
    World& restored = (*loaded)->GetWorld();
    CHECK(restored.GetPlayerMemories() == memories);
    CHECK(restored.GetSkyLight() == 0);
    CHECK(restored.GetActor((*loaded)->GetPlayer())->light == LightEmission{192, 1});
    const auto* live = restored.GetObject(restored.ObjectAt({2, 0}));
    REQUIRE(live);
    CHECK(live->kind == ObjectKind::Table);
    CHECK(live->light == LightEmission{60, 10});
    const auto view = (*loaded)->View();
    CHECK(Cell(view, 2, 0).overlayVisibility == Visibility::Remembered);
    CHECK(Cell(view, 2, 0).objectAppearance.kind == ObjectKind::Chair);
    CHECK(Cell(view, 2, 0).objectAppearance.facing == ObjectFacing::West);
    CHECK(Cell(view, 2, 0).objectAppearance.broken);
    CHECK(restored.GetActor(restored.FindActor(guest->player))->parked);
    const auto rejoined = (*loaded)->JoinPlayer("Guest", 12345, 0);
    REQUIRE(rejoined);
    CHECK(rejoined->player == guest->player);
    CHECK(restored.GetPlayerMemories() == memories);
}

}
