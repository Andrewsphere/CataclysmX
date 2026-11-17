#include "core/world.h"
#include "combat/combat_system.h"
#include "core/server.h"
#include "core/save_database.h"
#include "content_fixture.h"
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <string>

namespace {
using namespace cataclysmx;
template<class T, class E> T Take(eastl::expected<T, E> value) { REQUIRE(value); return eastl::move(*value); }

ContentCatalog EquipmentContent() {
    const ContentSource source{"combat-equipment", R"({format_version:1,definitions:[
      {type:'terrain',id:'floor',name:'Floor',movement:{passable:true},blocks_sight:false,appearance:{symbol:'.',foreground:'white'}},
      {type:'terrain',id:'wall',name:'Wall',movement:{passable:false},blocks_sight:true,appearance:{symbol:'#',foreground:'white'}},
      {type:'terrain',id:'air',name:'Air',movement:{passable:true},open_space:true,blocks_sight:false,looks_like:'floor'},
      {type:'item',id:'rock',name:'Rock',mass_g:10,volume_ml:10},
      {type:'item',id:'bag',name:'Bag',mass_g:5,volume_ml:10,capabilities:[{type:'container',id:'storage',capacity_volume_ml:1000}]},
      {type:'item',id:'staff',name:'Staff',mass_g:20,volume_ml:40,melee:{damage:[{type:'blunt',amount:400}],grips:[{kind:'one_handed'},{kind:'two_handed'}]}},
      {type:'item',id:'pole',name:'Pole',mass_g:20,volume_ml:40,melee:{hands:2,damage:[{type:'blunt',amount:400}]}},
      {type:'item',id:'plate',name:'Plate',mass_g:1000,volume_ml:500,armor:{layer:'normal',regions:[{region:'torso',overlap_exempt:true}],rigid_regions:['torso'],wear_cost:170,remove_cost:130}},
      {type:'item',id:'coat',name:'Coat',mass_g:800,volume_ml:1000,armor:{layer:'outer',regions:[{region:'torso'}],rigid_regions:['torso']}},
      {type:'item',id:'boots',name:'Leg-covering boots',mass_g:1000,volume_ml:1000,armor:{layer:'normal',regions:[
        {region:'left_leg',encumbrance:10},{region:'right_leg',encumbrance:10},
        {region:'left_foot',encumbrance:10},{region:'right_foot',encumbrance:10}]}},
      {type:'item',id:'hood',name:'Heavy hood',mass_g:500,volume_ml:500,armor:{layer:'normal',regions:[{region:'head',encumbrance:100}]}}
    ]})"};
    Diagnostics diagnostics;
    auto loaded = LoadContent({&source, 1}, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str());
    return Take(eastl::move(loaded));
}
struct Rig {
    ContentCatalog content = EquipmentContent();
    World world{content};
    LoadRequest request = Take(world.RequestChunks({{}, 0}, LoadMode::active));
    ActorHandle actor{};
    Rig() {
        REQUIRE(world.ReconcileChunks());
        REQUIRE(world.SetTile({3, 3}, {content.FindTerrain("floor")}));
        actor = Take(world.CreateActor({3, 3}, true));
    }
    ItemHandle Item(const char* name) { return Take(world.CreateItem(content.FindItem(name), ItemLocation::Carried(actor))); }
};

TEST_CASE("Equipment keeps independent hands and counts a two-handed item once", "[combat][equipment]") {
    Rig r;
    const auto left = r.Item("rock"), right = r.Item("rock"), staff = r.Item("staff");
    const auto mass = r.world.GetCarriedMass(r.actor);
    REQUIRE(r.world.HoldItem(r.actor, left, Hand::Left, GripKind::OneHanded).outcome == InventoryOutcome::Succeeded);
    REQUIRE(r.world.HoldItem(r.actor, right, Hand::Right, GripKind::OneHanded).outcome == InventoryOutcome::Succeeded);
    CHECK(r.world.HoldItem(r.actor, staff, Hand::Left, GripKind::TwoHanded).outcome != InventoryOutcome::Succeeded);
    CHECK(r.world.GetActor(r.actor)->wieldedItem == left);
    REQUIRE(r.world.ReleaseHand(r.actor, Hand::Left).outcome == InventoryOutcome::Succeeded);
    CHECK(r.world.GetActor(r.actor)->wieldedItem == right);
    REQUIRE(r.world.ReleaseHand(r.actor, Hand::Right).outcome == InventoryOutcome::Succeeded);
    REQUIRE(r.world.HoldItem(r.actor, staff, Hand::Right, GripKind::TwoHanded).outcome == InventoryOutcome::Succeeded);
    CHECK(r.world.GetActor(r.actor)->equipment.hands[0].item == staff);
    CHECK(r.world.GetActor(r.actor)->equipment.hands[1].item == staff);
    CHECK(r.world.GetCarriedMass(r.actor) == mass);
    REQUIRE(r.world.RegripItem(r.actor, Hand::Right, GripKind::OneHanded).outcome == InventoryOutcome::Succeeded);
    CHECK_FALSE(r.world.GetActor(r.actor)->equipment.hands[0].item.value);
    REQUIRE(r.world.RegripItem(r.actor, Hand::Right, GripKind::TwoHanded).outcome == InventoryOutcome::Succeeded);
    REQUIRE(r.world.TransferItem(staff, ItemLocation::Ground({3, 3})));
    CHECK_FALSE(r.world.GetActor(r.actor)->equipment.hands[0].item.value);
    CHECK_FALSE(r.world.GetActor(r.actor)->equipment.hands[1].item.value);
    CHECK(r.world.CheckInvariants());
}

TEST_CASE("Equipment extraction and legacy replacement are atomic", "[combat][equipment]") {
    Rig r;
    const auto bag = r.Item("bag"), left = r.Item("rock"), right = r.Item("rock");
    const auto nested = Take(r.world.CreateItem(r.content.FindItem("staff"), ItemLocation::Inside(bag)));
    const auto pole = Take(r.world.CreateItem(r.content.FindItem("pole"), ItemLocation::Inside(bag)));
    REQUIRE(r.world.HoldItem(r.actor, left, Hand::Left, GripKind::OneHanded).outcome == InventoryOutcome::Succeeded);
    REQUIRE(r.world.HoldItem(r.actor, right, Hand::Right, GripKind::OneHanded).outcome == InventoryOutcome::Succeeded);
    const auto mass = r.world.GetCarriedMass(r.actor);
    CHECK(r.world.WieldItem(r.actor, pole).outcome != InventoryOutcome::Succeeded);
    CHECK(r.world.GetActor(r.actor)->equipment.hands[0].item == left);
    CHECK(r.world.GetActor(r.actor)->equipment.hands[1].item == right);
    CHECK(r.world.GetItem(pole)->location == ItemLocation::Inside(bag));
    REQUIRE(r.world.WieldItem(r.actor, nested).outcome == InventoryOutcome::Succeeded);
    CHECK(r.world.GetActor(r.actor)->equipment.hands[1].item == right);
    CHECK(r.world.GetItem(nested)->location == ItemLocation::Carried(r.actor));
    CHECK(r.world.GetItem(bag)->contents.size() == 1);
    CHECK(r.world.GetCarriedMass(r.actor) == mass);
    REQUIRE(r.world.TransferItem(nested, ItemLocation::Ground({3, 3})));
    CHECK_FALSE(r.world.GetActor(r.actor)->equipment.hands[0].item.value);
    CHECK(r.world.GetActor(r.actor)->wieldedItem == right);
    CHECK(r.world.CheckInvariants());
}

TEST_CASE("Worn rigid conflicts are layer-local and costs come from the garment", "[combat][equipment]") {
    Rig r;
    const auto first = r.Item("plate"), second = r.Item("plate"), coat = r.Item("coat");
    const auto worn = r.world.WearItem(r.actor, first);
    REQUIRE(worn.outcome == InventoryOutcome::Succeeded);
    CHECK(worn.baseCost == 170);
    CHECK(r.world.WearItem(r.actor, second).outcome != InventoryOutcome::Succeeded);
    CHECK(r.world.HoldItem(r.actor, first, Hand::Left, GripKind::OneHanded).outcome != InventoryOutcome::Succeeded);
    REQUIRE(r.world.WearItem(r.actor, coat).outcome == InventoryOutcome::Succeeded);
    CHECK(r.world.RemoveWornItem(r.actor, first).baseCost == 130);
    REQUIRE(r.world.WearItem(r.actor, second).outcome == InventoryOutcome::Succeeded);
    REQUIRE(r.world.TransferItem(second, ItemLocation::Ground({3, 3})));
    CHECK(r.world.GetActor(r.actor)->equipment.worn.size() == 1);
    CHECK(r.world.CheckInvariants());
}

TEST_CASE("Disabled hands retain occupancy but cannot regrip", "[combat][equipment]") {
    Rig r;
    const auto staff = r.Item("staff");
    REQUIRE(r.world.HoldItem(r.actor, staff, Hand::Left, GripKind::TwoHanded).outcome == InventoryOutcome::Succeeded);
    r.world.Combat().Damage(r.actor, {}, BodyPart::LeftHand, {DamageType::Blunt, 10000});
    CHECK(r.world.GetActor(r.actor)->equipment.hands[0].item == staff);
    CHECK(r.world.GetActor(r.actor)->equipment.hands[1].item == staff);
    CHECK_FALSE(r.world.CanUseWieldedItem(r.actor));
    CHECK(r.world.RegripItem(r.actor, Hand::Left, GripKind::OneHanded).outcome != InventoryOutcome::Succeeded);
    REQUIRE(r.world.ReleaseHand(r.actor, Hand::Left).outcome == InventoryOutcome::Succeeded);
    CHECK_FALSE(r.world.GetActor(r.actor)->equipment.hands[1].item.value);
    CHECK(r.world.HoldItem(r.actor, staff, Hand::Left, GripKind::OneHanded).outcome != InventoryOutcome::Succeeded);
    REQUIRE(r.world.HoldItem(r.actor, staff, Hand::Right, GripKind::OneHanded).outcome == InventoryOutcome::Succeeded);
    CHECK(r.world.CanUseWieldedItem(r.actor));
    CHECK(r.world.CheckInvariants());
}

TEST_CASE("Equipment rebinds persistent identities after chunk archival", "[combat][equipment][save]") {
    Rig r;
    const auto actorId = r.world.GetActor(r.actor)->id;
    const auto staff = r.Item("staff"), plate = r.Item("plate");
    const auto staffId = r.world.GetItem(staff)->id, plateId = r.world.GetItem(plate)->id;
    REQUIRE(r.world.HoldItem(r.actor, staff, Hand::Left, GripKind::TwoHanded).outcome == InventoryOutcome::Succeeded);
    REQUIRE(r.world.WearItem(r.actor, plate).outcome == InventoryOutcome::Succeeded);
    REQUIRE(r.world.ReleaseRequest(r.request));
    REQUIRE(r.world.ReconcileChunks());
    CHECK_FALSE(r.world.GetActor(r.actor));
    r.request = Take(r.world.RequestChunks({{}, 0}, LoadMode::active));
    REQUIRE(r.world.ReconcileChunks());
    r.actor = r.world.FindActor(actorId);
    REQUIRE(r.actor.value);
    const auto restoredStaff = r.world.FindItem(staffId);
    const auto* actor = r.world.GetActor(r.actor);
    CHECK(actor->equipment.hands[0].item == restoredStaff);
    CHECK(actor->equipment.hands[1].item == restoredStaff);
    CHECK(actor->wieldedItem == restoredStaff);
    REQUIRE(actor->equipment.worn.size() == 1);
    CHECK(actor->equipment.worn.front() == r.world.FindItem(plateId));
    CHECK(r.world.CheckInvariants());
}

TEST_CASE("Legacy paid two-handed windup restores equipment and the exact committed grip", "[combat][equipment][save]") {
    Rig r;
    const auto pole = r.Item("pole");
    const auto actorId = r.world.GetActor(r.actor)->id, poleId = r.world.GetItem(pole)->id;
    std::error_code error;
    const auto parent = std::filesystem::temp_directory_path(error); REQUIRE_FALSE(error);
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    struct SaveFile {
        std::filesystem::path directory;
        ~SaveFile() { std::error_code ignored; std::filesystem::remove_all(directory, ignored); }
    } file;
    for (int attempt = 0;; ++attempt) {
        file.directory = parent / ("cataclysmx-equipment-migration-" + std::to_string(stamp) + "-" + std::to_string(attempt));
        const bool created = std::filesystem::create_directory(file.directory, error); REQUIRE_FALSE(error);
        if (created) { break; }
    }
    const auto path = (file.directory / "world.sqlite").string();
    ChunkArchive chunk{}; chunk.tiles.fill({r.content.FindTerrain("floor")});
    SavedActor actor{actorId, {3, 3}, true, 5};
    actor.remainingTicks = 41; actor.wieldedItem = poleId;
    auto legacy = MakeCombatState(true); legacy.stamina = 31; legacy.heavyCooldown = 87;
    legacy.action = {ActorActionKind::MeleeWindup, 999, static_cast<unsigned>(CombatMove::HeavyAttack), 73};
    legacy.action.weapon = poleId;
    actor.combat = CaptureCombatState(legacy);
    chunk.actors.push_back(eastl::move(actor));
    chunk.items.push_back({poleId, r.content.FindItem("pole"), LocationKind::actor, {}, actorId, {}, r.world.CaptureItemState(pole)});
    SaveSnapshot snapshot; snapshot.nextId = poleId + 1;
    snapshot.session.tick = 17; snapshot.session.player = actorId; snapshot.session.playerPosition = {3, 3};
    snapshot.session.players.push_back({actorId, {3, 3}, "Host", 0, actorId});
    {
        SaveDatabase database{r.content};
        REQUIRE(database.Create(path.c_str())); REQUIRE(database.Begin());
        REQUIRE(database.WriteChunk(chunk)); REQUIRE(database.WriteSnapshot(snapshot)); REQUIRE(database.Commit());
    }
    World restored{r.content};
    const auto session = Take(restored.LoadSave(path.c_str()));
    Take(restored.RequestChunks({{}, 0}, LoadMode::active)); REQUIRE(restored.ReconcileChunks());
    const auto handle = restored.FindActor(actorId);
    REQUIRE(handle.value);
    const auto* value = restored.GetActor(handle);
    CHECK(value->turns == 5);
    CHECK(restored.GetTick() == 17);
    CHECK(restored.GetActorRemainingTicks(handle) == 41);
    CHECK(value->combat.stamina == 31);
    CHECK(value->combat.action.recoveryTicks == 73);
    CHECK(value->combat.action.definitionDriven);
    CHECK(value->combat.action.sources.functionalHands == HandMask::Both);
    for (unsigned hand = 0; hand < 2; ++hand) {
        CHECK(value->equipment.hands[hand].item == restored.FindItem(poleId));
        CHECK(value->equipment.hands[hand].grip == GripKind::TwoHanded);
        CHECK(value->combat.action.sources.hands[hand].required);
        CHECK(value->combat.action.sources.hands[hand].item == poleId);
        CHECK(value->combat.action.sources.hands[hand].grip == GripKind::TwoHanded);
    }
    CHECK(restored.CheckInvariants());
    REQUIRE(restored.Save(session));
    CHECK(restored.GetActorRemainingTicks(handle) == 41);
    CHECK(restored.GetActor(handle)->combat.stamina == 31);
}

TEST_CASE("Old command envelopes reject every appended combat field", "[combat][equipment][server]") {
    Command command;
    command.equipment.item = 1;
    CHECK_FALSE(ValidCommandShape(command));
    command = {}; command.combatStyle = {1}; CHECK_FALSE(ValidCommandShape(command));
    command = {}; command.ability.ability = {1}; CHECK_FALSE(ValidCommandShape(command));
    command = {}; command.art = {1}; CHECK_FALSE(ValidCommandShape(command));
    command = {}; command.kind = CommandKind::equipment; command.equipment.item = 1;
    CHECK(ValidCommandShape(command));
    command.target = 9; CHECK_FALSE(ValidCommandShape(command));
}

TEST_CASE("Worn leg encumbrance reaches NPC movement scheduling with diagonal scaling once", "[combat][equipment][movement]") {
    Rig r;
    for (int z = 0; z <= 5; ++z) for (int x = 0; x <= 5; ++x) {
        REQUIRE(r.world.SetTile({x, z}, {r.content.FindTerrain("floor")}));
    }
    const auto npc = Take(r.world.CreateActor({1, 1}));
    auto combat = MakeCombatState(true); combat.faction = CombatFaction::Undead;
    REQUIRE(r.world.Combat().Configure(npc, combat));
    REQUIRE(r.world.SetActorController(npc, AiControllerKind::Sighted));
    r.world.SetSkyLight(255);
    const auto boots = Take(r.world.CreateItem(r.content.FindItem("boots"), ItemLocation::Carried(npc)));
    const auto hood = Take(r.world.CreateItem(r.content.FindItem("hood"), ItemLocation::Carried(npc)));
    CHECK(r.world.ActorMovementBaseCost(npc) == 100);
    REQUIRE(r.world.WearItem(npc, hood).outcome == InventoryOutcome::Succeeded);
    CHECK(r.world.ActorMovementBaseCost(npc) == 100);
    REQUIRE(r.world.RemoveWornItem(npc, hood).outcome == InventoryOutcome::Succeeded);
    REQUIRE(r.world.WearItem(npc, boots).outcome == InventoryOutcome::Succeeded);
    CHECK(r.world.ActorMovementBaseCost(npc) == 110);
    REQUIRE(r.world.WearItem(npc, hood).outcome == InventoryOutcome::Succeeded);
    CHECK(r.world.ActorMovementBaseCost(npc) == 110);
    TileCoord expectedPosition;
    Tick expectedTicks = 0;
    SECTION("cardinal movement") {
        REQUIRE(r.world.MoveActor(r.actor, {3, 1}));
        expectedPosition = {2, 1}; expectedTicks = 110;
    }
    SECTION("diagonal movement") {
        expectedPosition = {2, 2}; expectedTicks = 156; // ceil(110 * 141 / 100)
    }
    std::uint32_t rng = 7;
    r.world.ProcessNpcActions(rng);
    CHECK(r.world.GetActor(npc)->position == expectedPosition);
    CHECK(r.world.GetActorRemainingTicks(npc) == expectedTicks);
    CHECK(r.world.GetActor(npc)->turns == 1);
    CHECK(r.world.CheckInvariants());
}
}
