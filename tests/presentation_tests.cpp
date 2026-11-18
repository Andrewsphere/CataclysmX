#include "core/presentation.h"
#include "core/ratio.h"
#include "combat/combat_system.h"
#include "client/tile_inspection.h"
#include "content_fixture.h"
#include <catch2/catch_test_macros.hpp>

namespace {
using namespace cataclysmx;
TEST_CASE("Enemy tile health follows injury, recovery and visibility in gameplay views", "[presentation][enemy-health]") {
    Server server(TestContent());
    auto& world = server.GetWorld();
    const TileCoord position{1, 0};
    const auto enemy = world.CreateActor(position);
    REQUIRE(enemy);
    auto healthy = world.GetActor(*enemy)->combat;
    healthy.faction = CombatFaction::Undead;
    REQUIRE(world.Combat().Configure(*enemy, healthy));
    const auto before = server.View(3);
    CHECK(before.enemyHealth.empty());
    const auto mapRevision = world.GetMapRevision();
    REQUIRE(world.Combat().Damage(*enemy, server.GetPlayer(), BodyPart::AttackApparatus,
        {DamageType::Blunt, 6000}).outcome == AttackOutcome::Hit);
    CHECK(world.GetMapRevision() == mapRevision);
    const auto injured = server.View(3);
    REQUIRE(injured.enemyHealth.size() == 1);
    const auto& health = injured.enemyHealth.front();
    REQUIRE(health.cellIndex < injured.cells.size());
    CHECK(injured.cells[health.cellIndex].actor == world.GetActor(*enemy)->id);
    CHECK(health.bodyKind == BodyKind::Simple);
    CHECK(health.partRatios[static_cast<unsigned>(BodyPart::AttackApparatus)] == 0);
    CHECK(health.partRatios[static_cast<unsigned>(BodyPart::Core)] == ratioScale);
    CHECK(health.partRatios[static_cast<unsigned>(BodyPart::Locomotion)] == ratioScale);
    PlayerInspection inspection;
    REQUIRE(CapturePlayerInspection(server, server.GetHostPlayerId(), 1, position, 3, inspection));
    CHECK(inspection.view.enemyHealth.empty());
    CHECK(inspection.details.combat.parts[static_cast<unsigned>(BodyPart::AttackApparatus)].hp == 0);

    const auto normalVision = world.GetVisionProfile();
    REQUIRE(world.SetVisionProfile({0, 0, 0, normalVision.glareThreshold, 0}));
    CHECK(server.View(3).enemyHealth.empty());
    REQUIRE(world.SetVisionProfile(normalVision));
    CHECK(server.View(3).enemyHealth == injured.enemyHealth);

    REQUIRE(world.Combat().Configure(*enemy, healthy));
    CHECK(server.View(3).enemyHealth.empty());
    auto friendly = healthy;
    friendly.faction = CombatFaction::Neutral;
    REQUIRE(world.Combat().Configure(*enemy, friendly));
    REQUIRE(world.Combat().Damage(*enemy, server.GetPlayer(), BodyPart::AttackApparatus,
        {DamageType::Blunt, 6000}).outcome == AttackOutcome::Hit);
    CHECK(server.View(3).enemyHealth.empty());

    REQUIRE(world.Combat().Configure(*enemy, healthy));
    REQUIRE(world.Combat().Damage(*enemy, server.GetPlayer(), BodyPart::Core,
        {DamageType::Blunt, 10000}).outcome == AttackOutcome::Defeated);
    CHECK(server.View(3).enemyHealth.empty());
}

TEST_CASE("Repeated presentation capture reuses buffers and clears obsolete visuals", "[presentation][inspection]") {
    Server server(TestContent()); const auto view = server.View(8);
    PlayerPresentation presentation;
    presentation.mapItems.reserve(view.cells.size());
    presentation.interactions.reserve(maxPresentationInteractions);
    const auto* mapBuffer = presentation.mapItems.data();
    const auto* interactionBuffer = presentation.interactions.data();
    REQUIRE(CapturePlayerPresentation(server.GetWorld(), view, presentation));
    CHECK(presentation.mapItems.data() == mapBuffer);
    CHECK(presentation.interactions.data() == interactionBuffer);
    const auto expected = presentation.mapItems;
    REQUIRE(CapturePlayerPresentation(server.GetWorld(), view, presentation));
    CHECK(presentation.mapItems == expected); CHECK(presentation.mapItems.data() == mapBuffer);
    CHECK(presentation.interactions.data() == interactionBuffer);
    auto hidden = view;
    for (auto& cell : hidden.cells) cell = {};
    CaptureMapItems(server.GetWorld(), hidden, presentation.mapItems);
    CHECK(presentation.mapItems.data() == mapBuffer);
    REQUIRE(presentation.mapItems.size() == view.cells.size());
    for (const auto& item : presentation.mapItems) CHECK(item == MapItemVisual{});
}
TEST_CASE("Typed inspection preserves local formatting and read only world state", "[presentation][inspection]") {
    Server server(TestContent());
    const auto view = server.View(8);
    const auto revision = server.GetRevision();
    for (int z = 0; z < view.height; ++z) for (int x = 0; x < view.width; ++x) {
        const TileCoord cursor{view.origin.x + x, view.origin.z + z, view.origin.y};
        TileInspection details;
        REQUIRE(CaptureTileInspection(server.GetWorld(), view, cursor, details));
        Vector<String> local, remote;
        DescribeTile(server.GetWorld(), view, cursor, local);
        DescribeTile(TestContent(), view, cursor, details, remote);
        CHECK(local == remote);
        const auto& cell = view.cells[static_cast<std::size_t>(z * view.width + x)];
        if (cell.overlayVisibility != Visibility::Visible) {
            CHECK(details.actor == 0); CHECK(details.object == 0); CHECK(details.items.empty());
        }
    }
    CHECK(server.GetRevision() == revision);
    CHECK(server.View(8).cells == view.cells);
    TileInspection outside;
    REQUIRE(CaptureTileInspection(server.GetWorld(), view, {view.origin.x - 1, view.origin.z, view.origin.y}, outside));
    CHECK(outside.items.empty()); CHECK(outside.actor == 0); CHECK(outside.object == 0);
}
TEST_CASE("Inspection observes memories without exposing subsequently hidden contents", "[presentation][inspection]") {
    Server server(TestContent()); auto& world = server.GetWorld();
    REQUIRE(world.SetObjectAppearance(world.ObjectAt({2,0}), ObjectKind::Chair));
    REQUIRE(server.Send({}).advanced);
    REQUIRE(world.SetTile({1,0}, {TestTerrain("wall"),0}));
    REQUIRE(world.CreateItem(TestItem("bandage"), ItemLocation::Ground({2,0})));
    const auto memories = world.GetPlayerMemories();
    const auto chunks = world.LoadedChunks();
    PlayerInspection result;
    REQUIRE(CapturePlayerInspection(server, server.GetHostPlayerId(), 1, {2,0}, 8, result));
    CHECK(result.details.actor == 0); CHECK(result.details.object == 0); CHECK(result.details.items.empty());
    const auto index = static_cast<std::size_t>((result.cursor.z - result.view.origin.z) * result.view.width + result.cursor.x - result.view.origin.x);
    CHECK(result.mapItems[index].item == 0);
    Vector<String> lines;
    DescribeTile(TestContent(), result.view, result.cursor, result.details, lines);
    bool remembered = false;
    for (const auto& line : lines) {
        remembered |= line.find("remembered") != String::npos;
        CHECK(line.find("bandage") == String::npos);
    }
    CHECK(remembered); CHECK(world.GetPlayerMemories() == memories);
    CHECK(world.LoadedChunks() == chunks);
}
}
