#include "client/ai_debug.h"
#include "actors/ai_controller.h"
#include "combat/combat_system.h"
#include "content_fixture.h"
#include <catch2/catch_test_macros.hpp>

namespace {
using namespace cataclysmx;

const TerminalAppearance& Cell(const AiDebugView& view, TileCoord position) {
    const auto& image = view.image;
    return image.cells[(position.z - image.bounds.min.z) * image.width + position.x - image.bounds.min.x];
}

bool HasDetail(const AiDebugView& view, const char* text) {
    for (const auto& line : view.details) { if (line.find(text) != String::npos) { return true; } }
    return false;
}

TEST_CASE("AI debug capture retains decision evidence without sensing or world mutation", "[client][ai-debug]") {
    World world(TestContent());
    REQUIRE(world.RequestChunks({{}, 0}, LoadMode::active));
    REQUIRE(world.ReconcileChunks());
    for (int z = 0; z < chunk_size; ++z) {
        for (int x = 0; x < chunk_size; ++x) {
            REQUIRE(world.SetTile({x, z, world_max_y}, {TestTerrain("floor"), 0}));
        }
    }
    world.SetSkyLight(255);
    const TileCoord origin{4, 4, world_max_y};
    const TileCoord goal{9, 2, world_max_y};
    const auto npc = world.CreateActor(origin);
    REQUIRE(npc);
    auto combat = MakeCombatState(false);
    combat.faction = CombatFaction::Undead;
    REQUIRE(world.Combat().Configure(*npc, combat));
    REQUIRE(world.SetActorController(*npc, AiControllerKind::Sighted));
    REQUIRE(world.CreateActor(goal, true));
    REQUIRE(world.SetTile({4, 3, world_max_y}, {TestTerrain("wall"), 0}));
    std::uint32_t rng = 71;
    world.ProcessNpcActions(rng);
    const auto position = world.GetActor(*npc)->position;
    REQUIRE(position == TileCoord{5, 3, world_max_y});
    const auto state = world.CaptureActorController(*npc);
    const auto memories = world.GetPlayerMemories();
    const auto chunks = world.LoadedChunks();
    const auto revision = world.GetRevision();
    const auto tick = world.GetTick();
    const auto rngBefore = rng;
    AiDebugView view;
    CaptureAiDebugView(world, position, view);
    CHECK(view.actor == world.GetActor(*npc)->id);
    CHECK(view.cursor == position);
    CHECK(view.tick == tick);
    CHECK(HasDetail(view, "Controller: sighted"));
    CHECK(HasDetail(view, "Pursue -> Move"));
    CHECK(HasDetail(view, "NE: eligible | distance2=17 | CHOSEN"));
    CHECK(HasDetail(view, "N: impassable"));
    CHECK(Cell(view, position).background == RgbColor{150, 125, 10});
    CHECK(Cell(view, origin).background == RgbColor{30, 60, 150});
    CHECK(Cell(view, goal).background == RgbColor{105, 35, 130});
    CHECK(Cell(view, {4, 3, world_max_y}).background == RgbColor{110, 25, 25});
    CaptureAiDebugView(world, position, view);
    CHECK(world.CaptureActorController(*npc) == state);
    CHECK(world.GetPlayerMemories() == memories);
    CHECK(world.LoadedChunks() == chunks);
    CHECK(world.GetRevision() == revision);
    CHECK(world.GetTick() == tick);
    CHECK(rng == rngBefore);
    const auto retained = view;
    REQUIRE(world.DestroyActor(*npc));
    CaptureAiDebugView(world, position, view);
    CHECK(view.actor == 0);
    CHECK(HasDetail(view, "No actor here"));
    CHECK(HasDetail(retained, "CHOSEN"));
    CHECK(Cell(retained, position).symbol == "Z");
}

TEST_CASE("AI debug inspection of absent and edge tiles never acquires residency", "[client][ai-debug]") {
    World world(TestContent());
    AiDebugView view;
    CaptureAiDebugView(world, {world_coordinate_limit + 1, -world_coordinate_limit - 1, world_max_y + 1}, view);
    CHECK(view.cursor == TileCoord{world_coordinate_limit, -world_coordinate_limit, world_max_y});
    CHECK(view.actor == 0);
    CHECK(view.image.cells.size() == 17 * 17);
    CHECK(HasDetail(view, "not resident"));
    CHECK(world.LoadedChunks().empty());
    CHECK(world.GetTick() == 0);
}

}
