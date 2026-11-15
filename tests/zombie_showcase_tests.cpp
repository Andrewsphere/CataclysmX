#include "core/server.h"
#include "combat/combat_system.h"
#include "content/tileset.h"

#include <catch2/catch_test_macros.hpp>
#include <EASTL/utility.h>
#include <filesystem>

namespace {
using namespace cataclysmx;

ContentCatalog ShowcaseContent() {
    Diagnostics diagnostics;
    auto result = LoadContentPack(CATACLYSMX_ZOMBIE_SHOWCASE_PACK, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str());
    REQUIRE(result);
    REQUIRE(ValidateServerContent(*result));
    return eastl::move(*result);
}

TEST_CASE("Zombie showcase covers production zombie types with matching combat recipes", "[zombie-showcase][content]") {
    const auto showcase = ShowcaseContent();
    const auto production = LoadContentPack(CATACLYSMX_CONTENT_PACK);
    REQUIRE(production);
    CHECK(showcase.MonsterCount() == 66);
    for (std::uint32_t value = 1; value <= production->MonsterCount(); ++value) {
        const auto& original = production->GetMonster({value});
        if (original.faction != CombatFaction::Undead) { continue; }
        bool found = false;
        for (std::uint32_t display = 1; display <= showcase.MonsterCount(); ++display) {
            const auto& exhibit = showcase.GetMonster({display});
            if (exhibit.name != original.name && exhibit.name.find((original.name + " (").c_str()) != 0) { continue; }
            found = true;
            CHECK(exhibit.speed == original.speed);
            CHECK(exhibit.attackDamage == original.attackDamage);
            CHECK(exhibit.accuracy == original.accuracy);
            CHECK(exhibit.body == original.body);
            CHECK(exhibit.faction == original.faction);
        }
        INFO(original.id.c_str());
        CHECK(found);
    }
}

TEST_CASE("Zombie showcase exhibits remain stationary and harmless beside the player", "[zombie-showcase][server]") {
    const auto content = ShowcaseContent();
    Server server(content, 42, NewGameScenario::ZombieShowcase);
    Server repeated(content, 42, NewGameScenario::ZombieShowcase);
    auto& world = server.GetWorld();
    Vector<ActorHandle> actors;
    world.CollectActiveActors(actors);
    REQUIRE(actors.size() == content.MonsterCount() + 1);
    Vector<bool> seen(content.MonsterCount() + 1, false);
    Vector<TileCoord> positions;
    ActorHandle neighbor;
    for (const auto handle : actors) {
        const auto* actor = world.GetActor(handle);
        REQUIRE(actor);
        positions.push_back(actor->position);
        const auto* other = repeated.GetWorld().GetActor(repeated.GetWorld().FindActor(actor->id));
        REQUIRE(other);
        CHECK(other->position == actor->position);
        CHECK(other->monsterType == actor->monsterType);
        if (actor->player) { continue; }
        REQUIRE(actor->monsterType.value != 0);
        CHECK_FALSE(seen[actor->monsterType.value]);
        seen[actor->monsterType.value] = true;
        CHECK(world.CaptureActorController(handle).GetString("type") == eastl::string_view("none"));
        neighbor = handle;
    }
    REQUIRE(world.GetActor(neighbor) != nullptr);
    const auto adjacent = world.GetActor(server.GetPlayer())->position;
    REQUIRE(world.MoveActor(neighbor, {adjacent.x + 1, adjacent.z}));
    for (std::size_t index = 0; index < actors.size(); ++index) {
        positions[index] = world.GetActor(actors[index])->position;
    }
    const auto before = world.Combat().Describe(server.GetPlayer());
    for (unsigned turn = 0; turn < 10; ++turn) { server.Send({CommandKind::wait}); }
    for (std::size_t index = 0; index < actors.size(); ++index) {
        CHECK(world.GetActor(actors[index])->position == positions[index]);
    }
    CHECK(world.Combat().Describe(server.GetPlayer()) == before);
    for (std::size_t index = 1; index < seen.size(); ++index) { CHECK(seen[index]); }
}

TEST_CASE("Zombie showcase maps every production sprite exactly once and retains fallback art", "[zombie-showcase][tileset]") {
    const auto content = ShowcaseContent();
    const auto production = LoadContentPack(CATACLYSMX_CONTENT_PACK);
    REQUIRE(production);
    const auto path = std::filesystem::path(CATACLYSMX_CONTENT_PACK).parent_path().parent_path()
        / "assets/tiles/gen1_32x32/tileset.json5";
    Diagnostics diagnostics;
    const auto tileset = LoadTilesetFile(path.string().c_str(), content, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str());
    REQUIRE(tileset);
    const auto original = LoadTilesetFile(path.string().c_str(), *production);
    REQUIRE(original);
    Vector<bool> displayed(tileset->sprites.size(), false);
    for (std::uint32_t value = 1; value <= content.MonsterCount(); ++value) {
        const auto mapping = tileset->monsters[value];
        if (content.GetMonster({value}).id == "zombie_doctor") {
            CHECK(mapping.kind == TilesetMappingKind::Missing);
            continue;
        }
        if (content.GetMonster({value}).id == "cellar_zombie") {
            CHECK(mapping.kind == TilesetMappingKind::Visual);
            continue;
        }
        REQUIRE(mapping.kind == TilesetMappingKind::Sprite);
        CHECK_FALSE(displayed[mapping.index]);
        displayed[mapping.index] = true;
    }
    for (const auto& mapping : original->monsters) {
        if (mapping.kind == TilesetMappingKind::Missing) { continue; }
        if (mapping.kind == TilesetMappingKind::Visual) {
            for (const auto& alternative : original->visuals[mapping.index].alternatives) {
                REQUIRE(alternative.mapping.kind == TilesetMappingKind::Sprite);
                CHECK(displayed[alternative.mapping.index]);
            }
        } else {
            REQUIRE(mapping.kind == TilesetMappingKind::Sprite);
            CHECK(displayed[mapping.index]);
        }
    }
}

TEST_CASE("Zombie showcase accepts an empty undead roster", "[zombie-showcase][server]") {
    const auto content = LoadContentPack(CATACLYSMX_TERRAIN_FILE);
    REQUIRE(content);
    Server server(*content, 0, NewGameScenario::ZombieShowcase);
    Vector<ActorHandle> actors;
    server.GetWorld().CollectActiveActors(actors);
    REQUIRE(actors.size() == 1);
    CHECK(actors.front() == server.GetPlayer());
    CHECK(server.GetWorld().GetActor(server.GetPlayer())->player);
    CHECK(server.Send({CommandKind::wait}).advanced);
}
}
