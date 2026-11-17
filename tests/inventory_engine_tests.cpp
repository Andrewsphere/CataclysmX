#include "core/server.h"
#include "combat/combat_system.h"
#include "network/wire.h"
#include "content_fixture.h"
#include "combat_runtime_fixture.h"
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <string>

namespace {
using namespace cataclysmx;
template<class T, class E> T Take(eastl::expected<T, E> value) { REQUIRE(value); return eastl::move(*value); }

ContentCatalog InventoryContent(bool unarmedHeavy = false) {
    const ContentSource source{"inventory-engine", R"({format_version:1,objects:[
      {id:'cabinet',name:'Cabinet',behavior:'none',capacity_volume_ml:100,blocks_movement:true}
    ],definitions:[
      {type:'terrain',id:'floor',name:'Floor',movement:{passable:true},blocks_sight:false,appearance:{symbol:'.',foreground:'white'}},
      {type:'terrain',id:'wall',name:'Wall',movement:{passable:false},blocks_sight:true,appearance:{symbol:'#',foreground:'white'}},
      {type:'terrain',id:'air',name:'Air',movement:{passable:true},open_space:true,blocks_sight:false,looks_like:'floor'},
      {type:'item',id:'rock',name:'Rock',mass_g:10,volume_ml:60},
      {type:'item',id:'bag',name:'Bag',mass_g:5,volume_ml:10,capabilities:[{type:'container',id:'storage',capacity_volume_ml:1000}]},
      {type:'item',id:'weapon',name:'Weapon',mass_g:20,volume_ml:40,
        melee:{hands:2,attack_cost:150,damage:[{type:'blunt',amount:450},{type:'slashing',amount:450}]}}
    ]})"};
    Diagnostics diagnostics;
    auto content = LoadContent({&source, 1}, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str());
    auto result = Take(eastl::move(content));
    ConfigureCombatRuntimeTestContent(result);
    const auto edit = [&](const char* name) -> CombatMoveDefinition& {
        return const_cast<CombatMoveDefinition&>(result.GetCombatMove(result.FindCombatMove(name)));
    };
    // Test inventory sources and committed identity independently of repertoire
    // weights, defensive responses, intrinsic misses, and learning discounts.
    auto& fist = edit("basic_fist");
    fist.requirements.primary.freeHands = HandMask::Both;
    fist.training.work = 0;
    auto& weapon = edit("basic_weapon");
    weapon.cost = {0, 100, 0}; weapon.cooldown = 0;
    weapon.strikes[0].intrinsicMiss = 0;
    weapon.strikes[0].damageRatio = ratioScale;
    weapon.training.work = 0;
    auto& heavy = edit("basic_heavy");
    heavy.requirements.primary = unarmedHeavy
        ? SourceRequirement{AttackSourceKind::EmptyHand, SourceHand::Both}
        : SourceRequirement{AttackSourceKind::HeldItem};
    heavy.training.work = 0;
    for (const auto* name : {"basic_balanced", "basic_aggressive", "basic_defensive"}) {
        auto& style = const_cast<CombatStyleDefinition&>(result.GetCombatStyle(result.FindCombatStyle(name)));
        style.moves.clear();
        if (eastl::string_view(name) == "basic_aggressive") {
            style.moves.push_back({result.FindCombatMove("basic_heavy"), 1});
        } else {
            style.moves.push_back({result.FindCombatMove("basic_fist"), 1});
            style.moves.push_back({result.FindCombatMove("basic_weapon"), 1});
        }
    }
    return result;
}

struct Rig {
    ContentCatalog content;
    World world{content};
    LoadRequest request = Take(world.RequestChunks({{}, 0}, LoadMode::active));
    ActorHandle actor{}, target{};
    std::uint32_t rng = 7;
    explicit Rig(bool unarmedHeavy = false) : content(InventoryContent(unarmedHeavy)) {
        REQUIRE(world.ReconcileChunks());
        for (int z = 1; z <= 6; ++z) for (int x = 1; x <= 6; ++x)
            REQUIRE(world.SetTile({x, z}, {content.FindTerrain("floor")}));
        actor = Take(world.CreateActor({3, 3}, true));
        target = Take(world.CreateActor({4, 3}));
        auto state = MakeCombatState(true);
        state.accuracy = 100; state.attackDamage = 17; state.injury.regeneration = false;
        REQUIRE(world.Combat().Configure(actor, state));
        state.faction = CombatFaction::Undead;
        REQUIRE(world.Combat().Configure(target, state));
        world.SetSkyLight(255);
    }
    ItemHandle Item(const char* name, ItemLocation location) { return Take(world.CreateItem(content.FindItem(name), location)); }
    void Advance() { world.ElapseTicks(world.GetActorRemainingTicks(actor)); world.Combat().ProcessEvents(rng); }
};

TEST_CASE("Wield extracts, swaps and unwields without duplicating ownership or mass", "[inventory][engine]") {
    Rig r;
    const auto bag = r.Item("bag", ItemLocation::Carried(r.actor));
    const auto rock = r.Item("rock", ItemLocation::Inside(bag));
    const auto weapon = r.Item("weapon", ItemLocation::Carried(r.actor));
    const auto mass = r.world.GetCarriedMass(r.actor);
    CHECK(r.world.WieldItem(r.actor, rock).baseCost == 100);
    CHECK(r.world.GetItem(rock)->location == ItemLocation::Carried(r.actor));
    CHECK(r.world.GetItem(bag)->contents.empty());
    CHECK(r.world.GetCarriedMass(r.actor) == mass);
    CHECK(r.world.WieldItem(r.actor, weapon).outcome == InventoryOutcome::Succeeded);
    CHECK(r.world.GetItem(rock)->location == ItemLocation::Carried(r.actor));
    CHECK(r.world.WieldItem(r.actor).baseCost == 100);
    CHECK_FALSE(r.world.GetActor(r.actor)->wieldedItem.value);
    CHECK(r.world.GetCarriedMass(r.actor) == mass);
    CHECK(r.world.CheckInvariants());
}

TEST_CASE("Weapon contact replaces unarmed damage and aggregates fractional components", "[inventory][combat]") {
    Rig r;
    const auto unarmed = r.world.Combat().Attack(r.actor, r.target, r.rng);
    CHECK(unarmed.injury == 17); CHECK(unarmed.cost == 100);
    r.Advance();
    const auto rock = r.Item("rock", ItemLocation::Carried(r.actor));
    REQUIRE(r.world.WieldItem(r.actor, rock).outcome == InventoryOutcome::Succeeded);
    const auto improvised = r.world.Combat().Attack(r.actor, r.target, r.rng);
    CHECK(improvised.injury == 4); CHECK(improvised.cost == 100);
    r.Advance();
    const auto weapon = r.Item("weapon", ItemLocation::Carried(r.actor));
    REQUIRE(r.world.WieldItem(r.actor, weapon).outcome == InventoryOutcome::Succeeded);
    const auto armed = r.world.Combat().Attack(r.actor, r.target, r.rng);
    CHECK(armed.injury == 9); CHECK(armed.cost == 150);
    r.Advance();
    REQUIRE(r.world.Combat().SetStyle(r.actor, CombatStyle::Aggressive));
    CHECK(r.world.Combat().Attack(r.actor, r.target, r.rng).cost == 120);
    CHECK(r.world.GetActor(r.actor)->combat.action.recoveryTicks == 180);
    r.Advance();
    CHECK(r.world.GetActor(r.actor)->combatFeedback.result.injury == 18);
}

TEST_CASE("Weapon disability retains equipment but removal cancels its pending strike", "[inventory][combat]") {
    Rig r;
    const auto weapon = r.Item("weapon", ItemLocation::Carried(r.actor));
    REQUIRE(r.world.WieldItem(r.actor, weapon).outcome == InventoryOutcome::Succeeded);
    REQUIRE(r.world.Combat().SetStyle(r.actor, CombatStyle::Aggressive));
    REQUIRE(r.world.Combat().Attack(r.actor, r.target, r.rng).outcome == AttackOutcome::Windup);
    CHECK(r.world.GetActor(r.actor)->combat.action.weapon == r.world.GetItem(weapon)->id);
    for (const auto& hand : r.world.GetActor(r.actor)->combat.action.sources.hands) {
        CHECK(hand.required);
        CHECK(hand.item == r.world.GetItem(weapon)->id);
        CHECK(hand.grip == GripKind::TwoHanded);
    }
    SECTION("injury") {
        r.world.Combat().Damage(r.actor, r.target, BodyPart::LeftHand, {DamageType::Blunt, 100000, 0});
        CHECK(r.world.GetActor(r.actor)->wieldedItem == weapon);
        CHECK_FALSE(r.world.CanUseWieldedItem(r.actor));
        CHECK(r.world.GetActor(r.actor)->combat.action.kind == ActorActionKind::DisruptionRecovery);
        r.Advance();
        CHECK(r.world.WieldItem(r.actor).outcome == InventoryOutcome::Succeeded);
        CHECK(r.world.WieldItem(r.actor, weapon).outcome == InventoryOutcome::NeedsHands);
        const auto bag = r.Item("bag", ItemLocation::Carried(r.actor));
        REQUIRE(r.world.WieldItem(r.actor, bag).outcome == InventoryOutcome::Succeeded);
        REQUIRE(r.world.TransferItem(weapon, ItemLocation::Inside(bag)));
        const auto equipment = r.world.GetActor(r.actor)->equipment;
        const auto mass = r.world.GetCarriedMass(r.actor);
        CHECK(r.world.WieldItem(r.actor, weapon).outcome == InventoryOutcome::NeedsHands);
        CHECK(r.world.GetActor(r.actor)->equipment == equipment);
        CHECK(r.world.GetActor(r.actor)->wieldedItem == bag);
        CHECK(r.world.GetItem(weapon)->location == ItemLocation::Inside(bag));
        CHECK(r.world.GetCarriedMass(r.actor) == mass);
        CHECK(r.world.GetActorRemainingTicks(r.actor) == 0);
    }
    SECTION("drop") {
        REQUIRE(r.world.TransferItem(weapon, ItemLocation::Ground({3, 3})));
        CHECK_FALSE(r.world.GetActor(r.actor)->wieldedItem.value);
        CHECK(r.world.GetActor(r.actor)->combat.action.kind == ActorActionKind::DisruptionRecovery);
        CHECK(r.world.GetActor(r.actor)->combat.action.weapon == 0);
        r.Advance();
        CHECK_FALSE(r.world.GetActor(r.actor)->combatFeedback.result.contact);
    }
    SECTION("destroy and reuse slot") {
        REQUIRE(r.world.DestroyItem(weapon));
        const auto replacement = r.Item("weapon", ItemLocation::Carried(r.actor));
        CHECK(replacement != weapon);
        CHECK_FALSE(r.world.GetItem(weapon));
        CHECK_FALSE(r.world.GetActor(r.actor)->wieldedItem.value);
    }
    CHECK(r.world.CheckInvariants());
}

TEST_CASE("Exact transfers validate nearby roots, ancestry, capacity and stale sources atomically", "[inventory][transfer]") {
    Rig r;
    const auto cabinet = Take(r.world.CreateObject(r.content.FindObject("cabinet"), {3, 4}));
    const ItemStorage object{ItemStorageKind::Object, {}, r.world.GetObject(cabinet)->id};
    const ItemStorage floor{ItemStorageKind::Ground, {2, 3}};
    const auto rock = r.Item("rock", ItemLocation::Ground(floor.position));
    const auto id = r.world.GetItem(rock)->id;
    CHECK_FALSE(r.world.CanAccessItem(r.actor, rock)); // Legacy item use stays current-tile.
    CHECK(r.world.CanAccessStorage(r.actor, floor));
    REQUIRE(r.world.TransferInventoryItem(r.actor, id, floor, object).baseCost == 100);
    CHECK(r.world.TransferInventoryItem(r.actor, id, floor, {}).outcome == InventoryOutcome::InvalidItem);
    CHECK(r.world.TransferInventoryItem(r.actor, id, object, object).outcome == InventoryOutcome::AlreadyThere);
    const auto second = r.Item("rock", ItemLocation::Carried(r.actor));
    CHECK(r.world.TransferInventoryItem(r.actor, r.world.GetItem(second)->id, {}, object).outcome == InventoryOutcome::NoCapacity);
    CHECK(r.world.GetItem(second)->location == ItemLocation::Carried(r.actor));
    const auto bag = r.Item("bag", ItemLocation::Carried(r.actor));
    const ItemStorage container{ItemStorageKind::Container, {}, r.world.GetItem(bag)->id};
    CHECK(r.world.TransferInventoryItem(r.actor, id, object, container).baseCost == 100);
    CHECK(r.world.TransferInventoryItem(r.actor, container.id, {}, container).baseCost == 0);
    const auto stolen = r.Item("rock", ItemLocation::Carried(r.target));
    CHECK(r.world.TransferInventoryItem(r.actor, r.world.GetItem(stolen)->id, {}, floor).outcome == InventoryOutcome::Inaccessible);
    CHECK(r.world.GetActorRemainingTicks(r.actor) == 0);
    CHECK(r.world.CheckInvariants());
}

TEST_CASE("Unarmed windup survives removal of an unrelated carried item", "[inventory][combat]") {
    Rig r{true};
    const auto rock = r.Item("rock", ItemLocation::Carried(r.actor));
    REQUIRE(r.world.Combat().SetStyle(r.actor, CombatStyle::Aggressive));
    REQUIRE(r.world.Combat().Attack(r.actor, r.target, r.rng).outcome == AttackOutcome::Windup);
    const auto action = r.world.GetActor(r.actor)->combat.action;
    REQUIRE(action.weapon == 0);
    REQUIRE(r.world.DestroyItem(rock));
    CHECK(r.world.GetActor(r.actor)->combat.action == action);
    r.Advance();
    CHECK(r.world.GetActor(r.actor)->combatFeedback.result.injury == 34);
}

TEST_CASE("Mixed contacts sum before rounding and safely exceed uint32 raw damage", "[inventory][combat]") {
    Rig r;
    const DamagePacket fractions[] = {{DamageType::Blunt, 50, 0}, {DamageType::Slashing, 50, 0}};
    CHECK(r.world.Combat().DamageContact(r.target, r.actor, BodyPart::Torso, {fractions, 2}).injury == 1);
    const DamagePacket large[] = {{DamageType::Blunt, UINT32_MAX, 0}, {DamageType::Slashing, UINT32_MAX, 0}};
    CHECK(r.world.Combat().DamageContact(r.target, r.actor, BodyPart::Torso, {large, 2}).injury
        == (std::uint64_t{UINT32_MAX} * 2) / body_hp_scale);
}

TEST_CASE("Inventory success schedules precisely one speed-scaled action", "[inventory][server]") {
    Server server(TestContent());
    REQUIRE(server.SetPacing({100, true, true}));
    auto& world = server.GetWorld();
    const auto actor = server.GetPlayer();
    REQUIRE(world.SetActorSpeed(actor, 125));
    const auto item = Take(world.CreateItem(TestItem("rock"), ItemLocation::Carried(actor)));
    const auto turns = world.GetActor(actor)->turns;
    Command command; command.kind = CommandKind::wield_item; command.item = world.GetItem(item)->id;
    const auto result = server.Send(command);
    REQUIRE(result.outcome == Outcome::wielded);
    CHECK(result.cost == 125); CHECK(result.advanced);
    CHECK(world.GetActorRemainingTicks(actor) == 125);
    CHECK(world.GetActor(actor)->turns == turns + 1);
    CHECK(world.WieldItem(actor).outcome == InventoryOutcome::Busy);
    CHECK(world.GetActor(actor)->wieldedItem == item);
    CHECK(world.GetActor(actor)->turns == turns + 1);
    server.Update(1'250'000);
    REQUIRE(world.GetActorRemainingTicks(actor) == 0);
    Command transfer; transfer.kind = CommandKind::transfer_item; transfer.item = command.item;
    transfer.destination = {ItemStorageKind::Ground, world.GetActor(actor)->position};
    const auto moved = server.Send(transfer);
    REQUIRE(moved.outcome == Outcome::transferred);
    CHECK(moved.cost == 125); CHECK(moved.advanced);
    CHECK(world.GetActorRemainingTicks(actor) == 125);
    CHECK(world.GetActor(actor)->turns == turns + 2);
    CHECK_FALSE(world.GetActor(actor)->wieldedItem.value);
    Command invalid; invalid.kind = CommandKind::wield_item;
    invalid.target = 1; CHECK_FALSE(ValidCommandShape(invalid));
    invalid = {}; invalid.kind = CommandKind::wield_item;
    invalid.capability = {1}; CHECK_FALSE(ValidCommandShape(invalid));
    invalid = {}; invalid.kind = CommandKind::wield_item;
    invalid.waitCost = 1; CHECK_FALSE(ValidCommandShape(invalid));
}

TEST_CASE("Authoritative inventory commands and snapshots roundtrip with exact storage", "[inventory][network]") {
    Server server(TestContent());
    auto& world = server.GetWorld();
    const auto actor = server.GetPlayer();
    const auto item = Take(world.CreateItem(TestItem("rock"), ItemLocation::Carried(actor)));
    Command wield; wield.kind = CommandKind::wield_item; wield.item = world.GetItem(item)->id;
    network::wire::Writer commandWriter;
    REQUIRE(commandWriter.CommandValue(wield));
    network::wire::Reader commandReader{{commandWriter.bytes.data(), commandWriter.bytes.size()}};
    Command decoded; REQUIRE(commandReader.CommandValue(decoded)); REQUIRE(commandReader.Done());
    const auto receipt = server.Send(decoded);
    REQUIRE(receipt.outcome == Outcome::wielded); CHECK(receipt.cost == 100);
    network::wire::Writer receiptWriter;
    REQUIRE(receiptWriter.Result({world.GetActor(actor)->id, 1, 1, receipt}));
    network::wire::Reader receiptReader{{receiptWriter.bytes.data(), receiptWriter.bytes.size()}};
    PlayerCommandResult result; REQUIRE(receiptReader.Result(result)); CHECK(receiptReader.Done());
    const auto view = server.View(2);
    CHECK(view.wieldedItem == wield.item);
    unsigned floors = 0;
    for (const auto& storage : view.itemStorages) if (storage.storage.kind == ItemStorageKind::Ground) {
        ++floors; CHECK_FALSE(storage.massLimitGrams); CHECK_FALSE(storage.volumeLimitMl);
    }
    CHECK(floors == 9);
    network::wire::Writer writer; REQUIRE(writer.View(view));
    network::wire::Reader reader{{writer.bytes.data(), writer.bytes.size()}};
    WorldView received; REQUIRE(reader.View(received)); REQUIRE(reader.Done());
    CHECK(received.inventory == view.inventory); CHECK(received.nearbyItems == view.nearbyItems);
    CHECK(received.itemStorages == view.itemStorages); CHECK(received.wieldedItem == wield.item);
    CHECK(network::wire::ValidItemDefinitions(received, TestContent()));
    auto fabricated = view;
    for (auto& storage : fabricated.itemStorages) {
        if (storage.storage.kind == ItemStorageKind::Ground) { storage.volumeLimitMl = UINT64_MAX; break; }
    }
    network::wire::Writer rejected; CHECK_FALSE(rejected.View(fabricated));
    Command transfer; transfer.kind = CommandKind::transfer_item; transfer.item = wield.item;
    transfer.destination = {ItemStorageKind::Ground, world.GetActor(actor)->position};
    network::wire::Writer transferWriter; REQUIRE(transferWriter.CommandValue(transfer));
    network::wire::Reader transferReader{{transferWriter.bytes.data(), transferWriter.bytes.size()}};
    REQUIRE(transferReader.CommandValue(decoded)); CHECK(decoded.destination == transfer.destination);
    transfer.otherItem = 1; CHECK_FALSE(ValidCommandShape(transfer));
    wield.source.id = 1; CHECK_FALSE(ValidCommandShape(wield));
    Command wait; wait.destination = transfer.destination; CHECK_FALSE(ValidCommandShape(wait));
}

TEST_CASE("Refused authoritative transfers consume no ticks and preserve ownership", "[inventory][server]") {
    Server server(TestContent());
    auto& world = server.GetWorld();
    const auto actor = server.GetPlayer();
    const auto position = world.GetActor(actor)->position;
    const auto item = Take(world.CreateItem(TestItem("rock"), ItemLocation::Ground(position)));
    REQUIRE(world.SetActorCarryCapacity(actor, world.GetCarriedMass(actor)));
    Command transfer; transfer.kind = CommandKind::transfer_item; transfer.item = world.GetItem(item)->id;
    transfer.source = {ItemStorageKind::Ground, position};
    const auto tick = server.GetTick();
    const auto refused = server.Send(transfer);
    CHECK(refused.outcome == Outcome::inventory_refused);
    CHECK(refused.inventoryOutcome == InventoryOutcome::NoCapacity);
    CHECK_FALSE(refused.advanced); CHECK(refused.cost == 0); CHECK(server.GetTick() == tick);
    CHECK(world.GetItem(item)->location == ItemLocation::Ground(position));
    transfer.destination = transfer.source;
    CHECK(server.Send(transfer).inventoryOutcome == InventoryOutcome::AlreadyThere);
    CHECK(server.GetTick() == tick);
}

TEST_CASE("Nearby furniture metadata survives a radius-zero snapshot and wire roundtrip", "[inventory][network]") {
    auto content = LoadContentPack(CATACLYSMX_CONTENT_PACK); REQUIRE(content);
    Server server(*content);
    auto& world = server.GetWorld();
    const TileCoord position{1, 0, 0};
    const auto cabinet = Take(world.CreateObject(content->FindObject("kitchen_cabinet"), position));
    const auto id = world.GetObject(cabinet)->id;
    const auto view = server.View(0);
    REQUIRE(view.cells.size() == 1);
    const ItemStorageView* found = nullptr;
    for (const auto& storage : view.itemStorages) {
        if (storage.storage == ItemStorage{ItemStorageKind::Object, {}, id}) { found = &storage; }
    }
    REQUIRE(found); CHECK(found->accessible); CHECK(found->objectPosition == position);
    network::wire::Writer writer; REQUIRE(writer.View(view));
    network::wire::Reader reader{{writer.bytes.data(), writer.bytes.size()}};
    WorldView decoded; REQUIRE(reader.View(decoded)); REQUIRE(reader.Done());
    CHECK(decoded.itemStorages == view.itemStorages);
    for (auto& storage : decoded.itemStorages) {
        if (storage.storage.kind == ItemStorageKind::Object) { storage.objectPosition.x += 10; }
    }
    network::wire::Writer invalid; CHECK_FALSE(invalid.View(decoded));
}

TEST_CASE("Archive restoration rebinds equipment and preserves pending weapon identity", "[inventory][save]") {
    const auto directory = std::filesystem::temp_directory_path() / ("cataclysmx-inventory-"
        + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    REQUIRE(std::filesystem::create_directory(directory));
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code error; std::filesystem::remove_all(path, error); } } cleanup{directory};
    Rig r;
    const auto weapon = r.Item("weapon", ItemLocation::Carried(r.actor));
    const auto actorId = r.world.GetActor(r.actor)->id;
    const auto weaponId = r.world.GetItem(weapon)->id;
    REQUIRE(r.world.WieldItem(r.actor, weapon).outcome == InventoryOutcome::Succeeded);
    REQUIRE(r.world.Combat().SetStyle(r.actor, CombatStyle::Aggressive));
    REQUIRE(r.world.Combat().Attack(r.actor, r.target, r.rng).outcome == AttackOutcome::Windup);
    const auto committed = r.world.GetActor(r.actor)->combat.action;
    const auto remaining = r.world.GetActorRemainingTicks(r.actor);
    const auto stamina = r.world.GetActor(r.actor)->combat.stamina;
    REQUIRE(r.world.ReleaseRequest(r.request)); REQUIRE(r.world.ReconcileChunks());
    CHECK_FALSE(r.world.GetItem(weapon));
    r.request = Take(r.world.RequestChunks({{}, 0}, LoadMode::active)); REQUIRE(r.world.ReconcileChunks());
    r.actor = r.world.FindActor(actorId);
    const auto restored = r.world.FindItem(weaponId);
    CHECK(restored != weapon); CHECK(r.world.GetActor(r.actor)->wieldedItem == restored);
    CHECK(r.world.GetActor(r.actor)->combat.action.weapon == weaponId);
    CHECK(r.world.GetActor(r.actor)->combat.action == committed);
    CHECK(r.world.GetActorRemainingTicks(r.actor) == remaining);
    CHECK(r.world.GetActor(r.actor)->combat.stamina == stamina);
    CHECK(r.world.CheckInvariants());

    const auto path = (directory / "world.sqlite").string();
    SessionState session; session.player = actorId; session.playerPosition = {3, 3};
    REQUIRE(r.world.CreateSave(path.c_str(), session));
    World loaded(r.content);
    REQUIRE(loaded.LoadSave(path.c_str()));
    Take(loaded.RequestChunks({{}, 0}, LoadMode::active)); REQUIRE(loaded.ReconcileChunks());
    const auto* actor = loaded.GetActor(loaded.FindActor(actorId)); REQUIRE(actor);
    CHECK(actor->wieldedItem == loaded.FindItem(weaponId));
    CHECK(actor->combat.action.weapon == weaponId);
    CHECK(actor->combat.action == committed);
    CHECK(loaded.GetActorRemainingTicks(loaded.FindActor(actorId)) == remaining);
    CHECK(actor->combat.stamina == stamina);
    CHECK(loaded.CheckInvariants());
    const auto restoredActor = loaded.FindActor(actorId);
    loaded.ElapseTicks(loaded.GetActorRemainingTicks(restoredActor));
    loaded.Combat().ProcessEvents(r.rng);
    CHECK(loaded.GetActor(restoredActor)->combatFeedback.result.injury == 18);
}
}
