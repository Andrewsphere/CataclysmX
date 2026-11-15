#include "network/network.h"
#include "network/wire.h"

#include <catch2/catch_test_macros.hpp>

namespace {
using namespace cataclysmx;
using namespace cataclysmx::network;

TEST_CASE("Push and pull interaction codecs retain stale target guards", "[house][network][codec]") {
    for (auto kind : {InteractionKind::PushObject, InteractionKind::PullObject}) {
        Command command;
        command.kind = CommandKind::interact_action;
        command.interaction = {{4, 5, -1}, 77, {}, kind};
        wire::Writer writer;
        REQUIRE(writer.CommandValue(command));
        Command decoded;
        wire::Reader reader{{writer.bytes.data(), writer.bytes.size()}};
        REQUIRE(reader.CommandValue(decoded));
        CHECK(reader.Done());
        CHECK(decoded.interaction == command.interaction);
        for (std::size_t size = 0; size < writer.bytes.size(); ++size) {
            wire::Reader truncated{{writer.bytes.data(), size}};
            CHECK_FALSE(truncated.CommandValue(decoded));
        }
        command.interaction.terrain = {1};
        CHECK_FALSE(wire::Writer{}.CommandValue(command));
        command.interaction.terrain = {};
        command.interaction.action = ObjectAction::Collect;
        CHECK_FALSE(wire::Writer{}.CommandValue(command));
        command.interaction.action = ObjectAction::Start;
        command.interaction.object = 0;
        CHECK_FALSE(wire::Writer{}.CommandValue(command));
    }
}

TEST_CASE("Object moved receipts require a time charged successful action", "[house][network][codec]") {
    PlayerCommandResult receipt{1, 2, 3, {Outcome::object_moved, true, 100, {}, 100}};
    wire::Writer writer;
    REQUIRE(writer.Result(receipt));
    wire::Reader reader{{writer.bytes.data(), writer.bytes.size()}};
    PlayerCommandResult decoded;
    REQUIRE(reader.Result(decoded));
    CHECK(reader.Done());
    CHECK(decoded.result.outcome == Outcome::object_moved);
    CHECK(decoded.result.advanced);
    CHECK(decoded.result.cost == 100);
    receipt.result.cost = 0;
    CHECK_FALSE(wire::Writer{}.Result(receipt));
    receipt.result.cost = 100;
    receipt.result.advanced = false;
    CHECK_FALSE(wire::Writer{}.Result(receipt));
    // Result payload starts after three uint64 IDs and the outcome byte.
    writer.bytes[25] = 0;
    wire::Reader malformed{{writer.bytes.data(), writer.bytes.size()}};
    CHECK_FALSE(malformed.Result(decoded));
}

ContentCatalog FingerprintContent(bool reverse = false) {
    const ContentSource definitions{"definitions", R"({format_version:1,definitions:[
      {type:'terrain',id:'floor',name:'Floor',movement:{passable:true},blocks_sight:false,appearance:{symbol:'.',foreground:'white'}},
      {type:'terrain',id:'wall',name:'Wall',movement:{passable:false},blocks_sight:true,appearance:{symbol:'#',foreground:'white'}},
      {type:'item',id:'scrap',name:'Scrap',mass_g:1,volume_ml:10},
      {type:'building_pattern',id:'house',rows:['.'],palette:{'.':'floor'},entrance:{x:0,z:0}},
      {type:'loot_pool',id:'loot',entries:[{item:'scrap',weight:1}]},
      {type:'monster',id:'zombie',name:'Zombie',ai_profile:'vision'},
      {type:'monster_group',id:'undead',entries:[{monster:'zombie',weight:1}]}
    ],objects:[
      {id:'cabinet',name:'Cabinet',behavior:'none',capacity_volume_ml:1000,light:{intensity:80,range:4}}
    ]})"};
    const ContentSource profiles{"profiles", R"({format_version:1,ai_profiles:[{id:'vision'}],definitions:[
      {type:'loot_pool',id:'other_loot',entries:[{pool:'loot'}]},
      {type:'monster',id:'other_monster',name:'Other monster',ai_profile:'vision'},
      {type:'monster_group',id:'other_group',entries:[{monster:'zombie'}]}
    ],objects:[{id:'other_object',name:'Other',behavior:'none'}]})"};
    const ContentSource sources[] = {reverse ? profiles : definitions, reverse ? definitions : profiles};
    Diagnostics diagnostics;
    auto result = LoadContent({sources, 2}, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str());
    REQUIRE(result);
    return eastl::move(*result);
}

template<class T>
void CheckField(ContentCatalog& content, T& field, T changed) {
    const auto before = ContentFingerprint(content);
    T original = field;
    field = eastl::move(changed);
    CHECK(ContentFingerprint(content) != before);
    field = eastl::move(original);
}

TEST_CASE("House fingerprints include furniture and complete spawn recipes", "[house][network][fingerprint]") {
    auto content = FingerprintContent();
    const auto reordered = FingerprintContent(true);
    CHECK(ContentFingerprint(content) == ContentFingerprint(reordered));
    // Mutate owned catalog values only to isolate hashing from parser validation.
    auto& object = const_cast<ObjectDefinition&>(content.GetObject(content.FindObject("cabinet")));
    CheckField(content, object.capacityVolumeMl, eastl::optional<std::uint32_t>{});
    CheckField(content, object.capacityVolumeMl, eastl::optional<std::uint32_t>{1001});
    CheckField(content, object.kind, ObjectKind::Chair);
    CheckField(content, object.blocksMovement, true);
    CheckField(content, object.blocksSight, true);
    CheckField(content, object.movable, true);
    CHECK(object.light == LightEmission{80, 4});
    CHECK(content.GetObject(content.FindObject("other_object")).light == LightEmission{});
    CheckField(content, object.light.intensity, std::uint8_t{81});
    CheckField(content, object.light.range, std::uint8_t{5});
    CheckField(content, object.light, LightEmission{});
    auto& pool = const_cast<LootPoolDefinition&>(content.GetLootPool(content.FindLootPool("loot")));
    auto& entry = pool.entries[0];
    CheckField(content, entry.kind, LootEntryKind::Pool);
    CheckField(content, entry.item, ItemTypeId{});
    CheckField(content, entry.pool, LootPoolId{1});
    CheckField(content, entry.weight, std::uint32_t{2});
    CheckField(content, entry.count.min, std::uint32_t{2});
    CheckField(content, entry.count.max, std::uint32_t{2});
    auto& monster = const_cast<MonsterDefinition&>(content.GetMonster(content.FindMonster("zombie")));
    CheckField(content, monster.name, String{"Other"});
    CheckField(content, monster.aiProfile, AiProfileId{});
    CheckField(content, monster.body, BodyKind::Detailed);
    CheckField(content, monster.faction, CombatFaction::Neutral);
    CheckField(content, monster.speed, std::uint32_t{101});
    CheckField(content, monster.attackDamage, std::uint32_t{13});
    CheckField(content, monster.accuracy, std::uint32_t{9100});
    auto& group = const_cast<MonsterGroupDefinition&>(content.GetMonsterGroup(content.FindMonsterGroup("undead")));
    CheckField(content, group.entries[0].monster, MonsterTypeId{});
    CheckField(content, group.entries[0].weight, std::uint32_t{2});
    auto& profile = const_cast<AiProfile&>(content.GetAiProfile(content.FindAiProfile("vision")));
    CheckField(content, profile.sightRange, std::uint32_t{13});
}

void CheckPopulation(ContentCatalog& content, PatternPopulation& population) {
    population.furniture.push_back({{0, 0, 0}, content.FindObject("cabinet")});
    population.loot.push_back({{0, 0, 0}, content.FindLootPool("loot")});
    population.monsters.push_back({{0, 0, 0}, content.FindMonsterGroup("undead")});
    auto& furniture = population.furniture[0];
    CheckField(content, furniture.position, TileCoord{1, 2, -1});
    CheckField(content, furniture.type, ObjectTypeId{});
    CheckField(content, furniture.facing, ObjectFacing::East);
    auto& loot = population.loot[0];
    CheckField(content, loot.position, TileCoord{1, 2, -1});
    CheckField(content, loot.pool, LootPoolId{});
    CheckField(content, loot.chance, std::uint32_t{9999});
    CheckField(content, loot.rolls, std::uint32_t{2});
    CheckField(content, loot.destination, LootDestination::Furniture);
    auto& monster = population.monsters[0];
    CheckField(content, monster.position, TileCoord{1, 2, -1});
    CheckField(content, monster.group, MonsterGroupId{});
    CheckField(content, monster.chance, std::uint32_t{9999});
}

TEST_CASE("House fingerprints include layers common population and basement variants", "[house][network][fingerprint]") {
    auto content = FingerprintContent();
    auto& pattern = const_cast<BuildingPattern&>(content.GetBuildingPattern(content.FindBuildingPattern("house")));
    const auto floor = content.FindTerrain("floor");
    const auto wall = content.FindTerrain("wall");
    CheckField(content, pattern.site, BuildingSite::Landmark);
    CheckField(content, pattern.layers, Vector<PatternLayer>{{1, {floor}}});
    pattern.layers.push_back({1, {floor}});
    CheckField(content, pattern.layers[0].y, 2);
    CheckField(content, pattern.layers[0].tiles[0], wall);
    CheckPopulation(content, pattern.population);
    CheckField(content, pattern.basements, Vector<BasementVariant>{{"utility", 1, {-1, {floor}}, {}}});
    pattern.basements.push_back({"utility", 1, {-1, {floor}}, {}});
    auto& basement = pattern.basements[0];
    CheckField(content, basement.id, String{"infested"});
    CheckField(content, basement.weight, std::uint32_t{2});
    CheckField(content, basement.layer.y, -2);
    CheckField(content, basement.layer.tiles[0], wall);
    CheckPopulation(content, basement.population);
}
}
