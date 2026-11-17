#include "combat/combat_system.h"
#include "core/server.h"
#include "core/simulation.h"
#include "core/save_database.h"
#include "crafting/crafting_state.h"
#include "content_fixture.h"
#include "combat_runtime_fixture.h"
#include <catch2/catch_test_macros.hpp>
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <string>

namespace {
using namespace cataclysmx;
template<class T, class E> T Take(eastl::expected<T, E> value) { REQUIRE(value); return eastl::move(*value); }
struct SaveFile {
    std::filesystem::path directory;
    std::string path;
    SaveFile() {
        std::error_code error;
        const auto root = std::filesystem::temp_directory_path(error); REQUIRE_FALSE(error);
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (int attempt = 0;; ++attempt) {
            directory = root / ("cataclysmx-combat-death-" + std::to_string(stamp) + "-" + std::to_string(attempt));
            const bool created = std::filesystem::create_directory(directory, error); REQUIRE_FALSE(error);
            if (created) { break; }
        }
        path = (directory / "world.sqlite").string();
    }
    ~SaveFile() { std::error_code error; std::filesystem::remove_all(directory, error); }
};

ContentCatalog LethalCombatContent(std::uint32_t windup) {
    auto content = CombatRuntimeTestContent();
    auto& move = const_cast<CombatMoveDefinition&>(content.GetCombatMove(content.FindCombatMove("basic_natural")));
    move.cost = {windup, 100, 0};
    move.strikes[0].contacts = {{ContactRegion::Torso, 1}};
    move.strikes[0].damage = {{DamageType::Blunt, 10000}};
    move.strikes[0].defenses = DefenseMask::None;
    return content;
}

ActorHandle LethalZombie(World& world, TileCoord position) {
    REQUIRE(world.SetTile(position, {world.GetContent().FindTerrain("floor")}));
    const auto zombie = Take(world.CreateActor(position));
    auto state = MakeCombatState(false);
    state.faction = CombatFaction::Undead;
    REQUIRE(world.Combat().Configure(zombie, state));
    REQUIRE(world.SetActorController(zombie, AiControllerKind::Melee));
    return zombie;
}

TEST_CASE("Simulation stops at the last player's lethal NPC strike", "[combat][death][scheduling]") {
    for (const std::uint32_t windup : {0u, 80u}) {
        CAPTURE(windup);
        const auto content = LethalCombatContent(windup);
        World world{content};
        Take(world.RequestChunks({{}, 0}, LoadMode::active));
        REQUIRE(world.ReconcileChunks());
        for (int x = 3; x <= 5; ++x) { REQUIRE(world.SetTile({x, 3}, {content.FindTerrain("floor")})); }
        const auto player = Take(world.CreateActor({3, 3}, true));
        REQUIRE(world.SetTile({3, 4}, {content.FindTerrain("floor")}));
        const auto parked = Take(world.CreateActor({3, 4}, true));
        REQUIRE(world.SetActorParked(parked, true));
        const auto zombie = LethalZombie(world, {4, 3});
        const auto object = Take(world.CreateObject({5, 3}, true));
        REQUIRE(world.ScheduleActorAction(player, 300));
        std::uint32_t rng = 7;
        Tick mirror = 0;
        // A finite ceiling makes the old runaway reproducible without hanging
        // the test. Uncapped server updates use the maximum representable tick.
        const auto result = AdvanceUntilInput(world, mirror, rng, 1000);
        REQUIRE_FALSE(world.IsActorAlive(player));
        CHECK(world.GetTick() == windup);
        CHECK(mirror == windup);
        CHECK(result.stop == SimulationStop::NoActivePlayers);
        CHECK(result.readyPlayers.empty());
        CHECK(world.GetActor(player)->combat.corpse != 0);
        CHECK(world.GetActor(zombie)->turns == 1);
        CHECK(world.GetObject(object)->updates == 0);
        CHECK(world.IsActorAlive(parked));
        CHECK(world.CheckInvariants());
    }
}

TEST_CASE("Lethal combat returns server control in every pacing mode", "[combat][death][scheduling][server]") {
    for (const std::uint32_t windup : {0u, 80u}) {
        for (const bool capped : {false, true}) {
            for (const bool pause : {false, true}) {
                CAPTURE(windup, capped, pause);
                const auto content = LethalCombatContent(windup);
                Server server{content, 7};
                auto& world = server.GetWorld();
                REQUIRE(world.DestroyActor(world.ActorAt({5, 5})));
                const auto zombie = LethalZombie(world, {1, 0});
                const auto player = server.GetPlayer();
                const auto character = world.GetActor(player)->id;
                const auto item = Take(world.CreateItem(content.FindItem("rock"), ItemLocation::Carried(player)));
                REQUIRE(server.SetPacing({100, capped, pause}));
                Command wait;
                wait.waitCost = 300;
                REQUIRE(server.Send(wait).advanced);
                if (windup && capped) { CHECK(server.Send(wait).outcome == Outcome::queued); }
                const auto update = server.Update(10'000'000);
                if (windup && capped) {
                    REQUIRE(update.commandResult);
                    CHECK(update.commandResult->outcome == Outcome::character_dead);
                }
                REQUIRE_FALSE(world.IsActorAlive(player));
                CHECK(server.GetTick() == windup);
                CHECK(world.GetActor(zombie)->turns == 1);
                const auto corpseId = world.GetActor(player)->combat.corpse;
                REQUIRE(corpseId != 0);
                CHECK(world.GetItem(item)->location == ItemLocation::Inside(world.FindItem(corpseId)));
                const auto view = server.View(0);
                CHECK(view.combat.life == ActorLifeState::Dead);
                unsigned deaths = 0;
                for (const auto& event : view.combatEvents) {
                    if (event.kind == CombatEventKind::ActorDied && event.actor == character) { ++deaths; }
                }
                CHECK(deaths == 1);
                CHECK(server.Update(10'000'000).tick == windup);
                CHECK(server.Send(wait).outcome == Outcome::character_dead);
                Command replace;
                replace.kind = CommandKind::new_character;
                CHECK(server.Send(replace).outcome == Outcome::character_created);
                CHECK(world.IsActorAlive(server.GetPlayer()));
                CHECK(world.GetActor(player)->combat.corpse == corpseId);
                CHECK(world.CheckInvariants());
            }
        }
    }
}

TEST_CASE("A surviving multiplayer character keeps advancing after another dies", "[combat][death][scheduling][server]") {
    const auto content = LethalCombatContent(80);
    Server server{content, 7};
    auto& world = server.GetWorld();
    REQUIRE(world.DestroyActor(world.ActorAt({5, 5})));
    const auto guest = Take(server.JoinPlayer("Survivor", 0, 123));
    REQUIRE(server.ActivatePlayer(guest.player));
    const auto survivor = world.FindActor(guest.player);
    REQUIRE(world.SetTile({0, 3}, {content.FindTerrain("floor")}));
    REQUIRE(world.MoveActor(survivor, {0, 3}));
    LethalZombie(world, {1, 0});
    Command wait;
    wait.waitCost = 100;
    const auto context = server.GetInputContext(guest.player);
    CHECK(server.Submit(guest.player, {context.lastInputSequence + 1, context.opportunity, wait}).outcome == Outcome::queued);
    wait.waitCost = 300;
    REQUIRE(server.Send(wait).outcome == Outcome::queued);
    const auto update = server.Update(0);
    REQUIRE(update.commandResults.size() == 2);
    CHECK(update.commandResults[0].result.advanced);
    CHECK(update.commandResults[1].result.advanced);
    CHECK_FALSE(world.IsActorAlive(server.GetPlayer()));
    CHECK(world.IsActorAlive(survivor));
    CHECK(server.GetTick() == 100);
    CHECK(world.GetActorRemainingTicks(survivor) == 0);
    CHECK(server.Update(1'000'000).tick == 100);
    CHECK(world.CheckInvariants());
}

TEST_CASE("Single-player advance returns after a lethal delayed attack", "[combat][death][scheduling]") {
    const auto content = LethalCombatContent(80);
    World world{content};
    Take(world.RequestChunks({{}, 0}, LoadMode::active));
    REQUIRE(world.ReconcileChunks());
    REQUIRE(world.SetTile({3, 3}, {content.FindTerrain("floor")}));
    const auto player = Take(world.CreateActor({3, 3}, true));
    LethalZombie(world, {4, 3});
    std::uint32_t rng = 7;
    Advance(world, player, rng);
    CHECK_FALSE(world.IsActorAlive(player));
    CHECK(world.GetTick() == 80);
    CHECK(world.GetActor(player)->combat.corpse != 0);
    CHECK(world.CheckInvariants());
}

TEST_CASE("Death removes participation immediately and finalizes possessions once", "[combat][death]") {
    World world{TestContent()};
    Take(world.RequestChunks({{}, 0}, LoadMode::active));
    REQUIRE(world.ReconcileChunks());
    REQUIRE(world.SetTile({3, 3}, {TestTerrain("floor")}));
    const auto actor = Take(world.CreateActor({3, 3}, true));
    const auto bag = Take(world.CreateItem(TestContent().FindItem("backpack"), ItemLocation::Carried(actor)));
    const auto child = Take(world.CreateItem(TestContent().FindItem("bandage"), ItemLocation::Inside(bag)));
    const auto held = Take(world.CreateItem(TestContent().FindItem("rock"), ItemLocation::Carried(actor)));
    REQUIRE(world.HoldItem(actor, held, Hand::Left, GripKind::OneHanded).outcome == InventoryOutcome::Succeeded);
    world.Combat().Damage(actor, {}, BodyPart::Torso, {DamageType::Blunt, 10000});
    REQUIRE(world.GetActor(actor));
    CHECK_FALSE(world.IsActorAlive(actor));
    CHECK_FALSE(world.ActorAt({3, 3}).value);
    CHECK_FALSE(world.ScheduleActorAction(actor));
    Vector<ActorHandle> ready;
    world.CollectReadyPlayers(ready); CHECK(ready.empty());
    world.FinalizeActorDeaths();
    const auto corpseId = world.GetActor(actor)->combat.corpse;
    REQUIRE(corpseId != 0);
    const auto corpse = world.FindItem(corpseId);
    CHECK(world.GetItem(corpse)->type == TestContent().CorpseItem());
    CHECK(world.GetItem(bag)->location == ItemLocation::Inside(corpse));
    CHECK(world.GetItem(child)->location == ItemLocation::Inside(bag));
    CHECK(world.GetItem(held)->location == ItemLocation::Inside(corpse));
    CHECK(world.GetActor(actor)->inventory.empty());
    CHECK_FALSE(world.GetActor(actor)->wieldedItem.value);
    CHECK(world.GetActor(actor)->parked);
    world.MarkActorDead(actor); world.FinalizeActorDeaths();
    CHECK(world.GetActor(actor)->combat.corpse == corpseId);
    CHECK(world.CountItemsAt({3, 3}) == 1);
    CHECK_FALSE(world.NextActorEventIn());
    CHECK(world.CheckInvariants());
}

TEST_CASE("NPC death retires the actor only after its corpse owns the inventory", "[combat][death]") {
    World world{TestContent()};
    Take(world.RequestChunks({{}, 0}, LoadMode::active)); REQUIRE(world.ReconcileChunks());
    REQUIRE(world.SetTile({3, 3}, {TestTerrain("floor")}));
    const auto npc = Take(world.CreateActor({3, 3}));
    const auto id = world.GetActor(npc)->id;
    const auto item = Take(world.CreateItem(TestContent().FindItem("rock"), ItemLocation::Carried(npc)));
    const auto part = world.GetActor(npc)->combat.body.kind == BodyKind::Simple ? BodyPart::Core : BodyPart::Torso;
    world.Combat().Damage(npc, {}, part, {DamageType::Blunt, 10000});
    CHECK_FALSE(world.IsActorAlive(npc));
    world.FinalizeActorDeaths();
    CHECK_FALSE(world.FindActor(id).value);
    REQUIRE(world.GetItem(item));
    CHECK(world.GetItem(item)->location.kind == LocationKind::item);
    CHECK(world.GetItem(world.GetItem(item)->location.item)->type == TestContent().CorpseItem());
    CHECK(world.CheckInvariants());
}

TEST_CASE("Dead sessions idle and create a fresh character without changing session identity", "[combat][death][server]") {
    Server server{TestContent(), 7};
    auto& world = server.GetWorld();
    const auto session = server.GetHostPlayerId();
    const auto original = server.GetPlayer();
    const auto character = world.GetActor(original)->id;
    world.Combat().Damage(original, {}, BodyPart::Torso, {DamageType::Blunt, 10000});
    const auto tick = server.GetTick();
    server.Update(1000000);
    CHECK(server.GetTick() == tick);
    CHECK(server.View(0).characterId == character);
    CHECK(server.GetPlayers().front().life == ActorLifeState::Dead);
    const auto context = server.GetInputContext(session);
    Command command; command.kind = CommandKind::new_character;
    const auto result = server.Submit(session, {context.lastInputSequence + 1, context.opportunity, command});
    CHECK(result.outcome == Outcome::character_created);
    CHECK(server.GetHostPlayerId() == session);
    CHECK(server.GetPlayer() != original);
    CHECK(world.IsActorAlive(server.GetPlayer()));
    CHECK(world.GetActor(server.GetPlayer())->learning.skills.empty());
    CHECK(world.GetActor(server.GetPlayer())->inventory.empty());
    CHECK(world.GetActor(original)->combat.corpse != 0);
    CHECK(server.GetInputContext(session).opportunity == context.opportunity + 1);
    CHECK(server.Submit(session, {context.lastInputSequence + 1, context.opportunity, command}).outcome == Outcome::stale_input);
    CHECK_FALSE(server.CreateFreshCharacter(session));
    CHECK(world.CheckInvariants());
}

TEST_CASE("A parked dead host and its replacement survive checkpoints without another corpse", "[combat][death][save]") {
    SaveFile file;
    Server server{TestContent(), 7};
    const auto session = server.GetHostPlayerId();
    const auto deadId = server.GetWorld().GetActor(server.GetPlayer())->id;
    server.GetWorld().Combat().Damage(server.GetPlayer(), {}, BodyPart::Head, {DamageType::Blunt, 10000});
    server.Update(0);
    const auto corpse = server.GetWorld().GetActor(server.GetPlayer())->combat.corpse;
    REQUIRE(corpse);
    REQUIRE(server.CreateSave(file.path.c_str()));
    auto loaded = Take(Server::LoadSave(TestContent(), file.path.c_str()));
    CHECK(loaded->GetHostPlayerId() == session);
    CHECK_FALSE(loaded->GetWorld().IsActorAlive(loaded->GetPlayer()));
    CHECK(loaded->GetWorld().GetActor(loaded->GetPlayer())->combat.corpse == corpse);
    const auto replacement = Take(loaded->CreateFreshCharacter(session));
    CHECK(replacement != deadId);
    REQUIRE(loaded->Save());
    auto reloaded = Take(Server::LoadSave(TestContent(), file.path.c_str()));
    CHECK(reloaded->GetHostPlayerId() == session);
    CHECK(reloaded->GetWorld().GetActor(reloaded->GetPlayer())->id == replacement);
    CHECK(reloaded->GetWorld().IsActorAlive(reloaded->GetPlayer()));
    const auto old = reloaded->GetWorld().FindActor(deadId);
    REQUIRE(old.value);
    CHECK(reloaded->GetWorld().GetActor(old)->combat.corpse == corpse);
    reloaded->GetWorld().FinalizeActorDeaths();
    CHECK(reloaded->GetWorld().FindItem(corpse).value);
    CHECK(reloaded->GetWorld().CheckInvariants());
}

TEST_CASE("Oversized corpse possessions preserve roots and nested items through save and archival", "[combat][death][save]") {
    const ContentSource source{"oversized-corpse", R"({format_version:1,definitions:[
      {type:'terrain',id:'floor',name:'Floor',movement:{passable:true},blocks_sight:false,appearance:{symbol:'.',foreground:'white'}},
      {type:'terrain',id:'wall',name:'Wall',movement:{passable:false},blocks_sight:true,appearance:{symbol:'#',foreground:'white'}},
      {type:'terrain',id:'air',name:'Air',movement:{passable:true},open_space:true,blocks_sight:false,looks_like:'floor'},
      {type:'item',id:'bulk',name:'Bulky possession',mass_g:1,volume_ml:3000000000},
      {type:'item',id:'pack',name:'Small pack',mass_g:1,volume_ml:20,capabilities:[{type:'container',id:'storage',capacity_volume_ml:100}]},
      {type:'item',id:'pebble',name:'Pebble',mass_g:1,volume_ml:10}
    ]})"};
    Diagnostics diagnostics;
    auto loadedContent = LoadContent({&source, 1}, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str());
    const auto content = Take(eastl::move(loadedContent));
    SaveFile file;
    PersistentId actorId = 0, corpseId = 0, firstId = 0, secondId = 0, packId = 0, childId = 0;
    {
        World world{content};
        Take(world.RequestChunks({{}, 0}, LoadMode::active));
        REQUIRE(world.ReconcileChunks());
        REQUIRE(world.SetTile({3, 3}, {content.FindTerrain("floor")}));
        const auto actor = Take(world.CreateActor({3, 3}, true));
        actorId = world.GetActor(actor)->id;
        const auto first = Take(world.CreateItem(content.FindItem("bulk"), ItemLocation::Carried(actor)));
        const auto second = Take(world.CreateItem(content.FindItem("bulk"), ItemLocation::Carried(actor)));
        const auto pack = Take(world.CreateItem(content.FindItem("pack"), ItemLocation::Carried(actor)));
        const auto child = Take(world.CreateItem(content.FindItem("pebble"), ItemLocation::Inside(pack)));
        firstId = world.GetItem(first)->id; secondId = world.GetItem(second)->id;
        packId = world.GetItem(pack)->id; childId = world.GetItem(child)->id;
        CHECK_FALSE(world.TransferItem(first, ItemLocation::Inside(pack)));
        REQUIRE(world.HoldItem(actor, first, Hand::Right, GripKind::OneHanded).outcome == InventoryOutcome::Succeeded);
        world.Combat().Damage(actor, {}, BodyPart::Torso, {DamageType::Blunt, 10000});
        world.FinalizeActorDeaths();
        corpseId = world.GetActor(actor)->combat.corpse;
        REQUIRE(corpseId);
        CHECK(world.GetContainerUsedVolume(world.FindItem(corpseId)) == 6'000'000'020ULL);
        CHECK(world.GetActor(actor)->equipment == EquipmentState{});
        CHECK(world.CheckInvariants());
        SessionState session;
        session.player = actorId; session.playerPosition = world.GetActor(actor)->position;
        REQUIRE(world.CreateSave(file.path.c_str(), session, &diagnostics));
    }
    World restored{content};
    const auto session = Take(restored.LoadSave(file.path.c_str(), &diagnostics));
    auto request = Take(restored.RequestChunks({{}, 0}, LoadMode::active));
    REQUIRE(restored.ReconcileChunks(&diagnostics));
    const auto checkPossessions = [&] {
        const auto actor = restored.FindActor(actorId);
        REQUIRE(actor.value);
        CHECK_FALSE(restored.IsActorAlive(actor));
        CHECK(restored.GetActor(actor)->combat.corpse == corpseId);
        const auto corpse = restored.FindItem(corpseId);
        REQUIRE(corpse.value);
        CHECK(restored.GetContainerUsedVolume(corpse) == 6'000'000'020ULL);
        CHECK(restored.GetItem(corpse)->contents.size() == 3);
        CHECK(restored.GetItem(restored.FindItem(firstId))->location == ItemLocation::Inside(corpse));
        CHECK(restored.GetItem(restored.FindItem(secondId))->location == ItemLocation::Inside(corpse));
        CHECK(restored.GetItem(restored.FindItem(packId))->location == ItemLocation::Inside(corpse));
        CHECK(restored.GetItem(restored.FindItem(childId))->location == ItemLocation::Inside(restored.FindItem(packId)));
        CHECK(restored.CountItemsAt({3, 3}) == 1);
        CHECK(restored.CheckInvariants());
    };
    checkPossessions();
    REQUIRE(restored.Save(session, &diagnostics));
    REQUIRE(restored.ReleaseRequest(request));
    REQUIRE(restored.ReconcileChunks(&diagnostics));
    request = Take(restored.RequestChunks({{}, 0}, LoadMode::active));
    REQUIRE(restored.ReconcileChunks(&diagnostics));
    restored.FinalizeActorDeaths();
    checkPossessions();
}

TEST_CASE("Legacy zero-head worker releases its paid crafting slice for a fresh character", "[combat][death][crafting][save]") {
    const ContentSource source{"legacy-dead-worker", R"({format_version:1,definitions:[
      {type:'terrain',id:'floor',name:'Floor',movement:{passable:true},blocks_sight:false,appearance:{symbol:'.',foreground:'white'}},
      {type:'terrain',id:'wall',name:'Wall',movement:{passable:false},blocks_sight:true,appearance:{symbol:'#',foreground:'white'}},
      {type:'terrain',id:'air',name:'Air',movement:{passable:true},open_space:true,blocks_sight:false,looks_like:'floor'},
      {type:'tool_trait',id:'cut',name:'Cut'},
      {type:'item',id:'material',name:'Material',mass_g:10,volume_ml:20},
      {type:'item',id:'product',name:'Product',mass_g:5,volume_ml:10},
      {type:'item',id:'tool',name:'Powered cutter',mass_g:10,volume_ml:10,tool_traits:[{id:'cut',tier:1}],
        crafting_power:{store:'power',energy_per_work:10000},
        capabilities:[{type:'energy_store',id:'power',capacity_energy:1000,initial_energy:1000,rechargeable:true}]},
      {type:'recipe',id:'make',inputs:[{item:'material',count:1}],tool_traits:[{id:'cut',tier:1}],result:{item:'product',count:1},work:250}
    ]})"};
    Diagnostics diagnostics;
    auto loadedContent = LoadContent({&source, 1}, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str());
    const auto content = Take(eastl::move(loadedContent));
    const auto power = FindItemCapability(content.GetItem(content.FindItem("tool")), "power");
    const auto readJob = [](const World& world, ItemHandle item) {
        const auto data = world.CaptureItemState(item);
        const auto* payload = data.Find("crafting_job"); REQUIRE(payload);
        return Take(ReadCraftingState(*payload));
    };
    SaveFile file;
    PersistentId originalId = 0, jobId = 0, materialId = 0, toolId = 0;
    CraftingJobState paid;
    {
        World world{content};
        Take(world.RequestChunks({{}, 0}, LoadMode::active)); REQUIRE(world.ReconcileChunks());
        REQUIRE(world.SetTile({3, 3}, {content.FindTerrain("floor")}));
        REQUIRE(world.SetCraftingSettings({1, 100}));
        const auto actor = Take(world.CreateActor({3, 3}, true));
        originalId = world.GetActor(actor)->id;
        const auto material = Take(world.CreateItem(content.FindItem("material"), ItemLocation::Carried(actor)));
        const auto tool = Take(world.CreateItem(content.FindItem("tool"), ItemLocation::Carried(actor)));
        materialId = world.GetItem(material)->id; toolId = world.GetItem(tool)->id;
        const auto started = Take(world.StartCrafting(actor, content.FindCraftingRecipe("make")));
        REQUIRE(started.outcome == CraftingOutcome::Started);
        jobId = world.GetItem(started.unfinished)->id;
        world.ElapseTicks(100); REQUIRE(world.ProcessCraftingEvents());
        world.ElapseTicks(30);
        paid = readJob(world, started.unfinished);
        CHECK(paid.worker == originalId);
        CHECK(paid.work == 100ULL * ratioScale);
        CHECK(paid.pendingWork == 100ULL * ratioScale);
        CHECK(paid.remaining == 70);
        REQUIRE(world.GetItemEnergy(tool, power)->energy == 800);
        SessionState session;
        session.player = originalId; session.playerPosition = {3, 3};
        session.activeRadius = 0; session.residentRadius = 0;
        session.players.push_back({originalId, {3, 3}, "Host", 0, originalId});
        REQUIRE(world.CreateSave(file.path.c_str(), session));
    }
    // Reproduce a real pre-expansion checkpoint: life was not explicit, the
    // actor's funded deadline and job assignment existed independently of HP.
    {
        struct Database {
            sqlite3* value = nullptr;
            ~Database() { if (value) { sqlite3_close(value); } }
        } database;
        REQUIRE(sqlite3_open(file.path.c_str(), &database.value) == SQLITE_OK);
        auto legacy = MakeCombatState(true);
        legacy.body.parts[static_cast<unsigned>(BodyPart::Head)].hp = 0;
        REQUIRE(ValidateCombatState(legacy));
        const auto legacyData = CaptureCombatState(legacy);
        REQUIRE(legacyData.GetUnsigned("version") == 2);
        const auto migrated = Take(ReadCombatState(legacyData, content));
        REQUIRE(migrated.life == ActorLifeState::Dead);
        const auto json = EncodeDataObject(legacyData);
        struct Statement {
            sqlite3_stmt* value = nullptr;
            ~Statement() { if (value) { sqlite3_finalize(value); } }
        } statement;
        REQUIRE(sqlite3_prepare_v2(database.value,
            "UPDATE chunks SET payload=json_remove(json_set(payload,'$.actors[0].combat',json(?1)),'$.actors[0].equipment','$.actors[0].character_combat') WHERE json_extract(payload,'$.actors[0].id')=?2",
            -1, &statement.value, nullptr) == SQLITE_OK);
        REQUIRE(sqlite3_bind_text(statement.value, 1, json.c_str(), static_cast<int>(json.size()), SQLITE_TRANSIENT) == SQLITE_OK);
        // Persistent counters are decimal JSON strings, not JSON numbers.
        const auto actorIdText = std::to_string(originalId);
        REQUIRE(sqlite3_bind_text(statement.value, 2, actorIdText.c_str(), static_cast<int>(actorIdText.size()), SQLITE_TRANSIENT) == SQLITE_OK);
        REQUIRE(sqlite3_step(statement.value) == SQLITE_DONE);
        REQUIRE(sqlite3_changes(database.value) == 1);
        REQUIRE(sqlite3_exec(database.value,
            "UPDATE world_state SET payload=json_remove(payload,'$.session.players[0].session')",
            nullptr, nullptr, nullptr) == SQLITE_OK);
        REQUIRE(sqlite3_exec(database.value, "UPDATE metadata SET format_version=13", nullptr, nullptr, nullptr) == SQLITE_OK);
    }
    auto server = Take(Server::LoadSave(content, file.path.c_str(), &diagnostics));
    auto& world = server->GetWorld();
    const auto original = world.FindActor(originalId);
    REQUIRE(original.value);
    CHECK_FALSE(world.IsActorAlive(original));
    CHECK(world.GetActor(original)->parked);
    REQUIRE(world.GetActor(original)->combat.corpse);
    const auto corpseId = world.GetActor(original)->combat.corpse;
    const auto corpse = world.FindItem(corpseId);
    auto job = world.FindItem(jobId);
    REQUIRE(job.value);
    CHECK(world.GetItem(job)->location == ItemLocation::Inside(corpse));
    CHECK(world.GetItem(world.FindItem(materialId))->location == ItemLocation::Inside(job));
    CHECK(world.GetItem(world.FindItem(toolId))->location == ItemLocation::Inside(corpse));
    const auto released = readJob(world, job);
    CHECK(released.worker == 0);
    CHECK(released.status == CraftingStatus::WorkerUnavailable);
    CHECK(released.work == paid.work);
    CHECK(released.pendingWork == paid.pendingWork);
    CHECK(released.remaining == paid.remaining);
    CHECK(released.energyRemainder == paid.energyRemainder);
    CHECK(released.timingRemainder == paid.timingRemainder);
    CHECK(world.GetItemEnergy(world.FindItem(toolId), power)->energy == 800);
    CHECK(world.GetTick() == 130);
    server->Update(1'000'000);
    CHECK(world.GetTick() == 130);
    CHECK(readJob(world, job).remaining == paid.remaining);
    CHECK(world.CheckInvariants());
    REQUIRE(server->Save());
    server.reset();
    // The unassigned paid remainder is durable, not just a transient fix made
    // during legacy loading. Reopening must neither finish nor charge it.
    server = Take(Server::LoadSave(content, file.path.c_str(), &diagnostics));
    auto& resumedWorld = server->GetWorld();
    CHECK(resumedWorld.GetActor(resumedWorld.FindActor(originalId))->combat.corpse == corpseId);
    job = resumedWorld.FindItem(jobId);
    REQUIRE(job.value);
    CHECK(readJob(resumedWorld, job).worker == 0);
    CHECK(readJob(resumedWorld, job).remaining == 70);
    CHECK(Take(server->CreateFreshCharacter(originalId)) != originalId);
    const auto fresh = server->GetPlayer();
    const auto resumed = Take(resumedWorld.ResumeCrafting(fresh, job));
    REQUIRE(resumed.outcome == CraftingOutcome::Resumed);
    CHECK(resumedWorld.GetActorRemainingTicks(fresh) == 70);
    CHECK(readJob(resumedWorld, job).pendingWork == paid.pendingWork);
    CHECK(resumedWorld.GetItemEnergy(resumedWorld.FindItem(toolId), power)->energy == 800);
    resumedWorld.ElapseTicks(70); REQUIRE(resumedWorld.ProcessCraftingEvents());
    REQUIRE(resumedWorld.InspectCrafting(job));
    CHECK(resumedWorld.InspectCrafting(job)->completedWork == 200);
    CHECK(resumedWorld.InspectCrafting(job)->fundedTicksRemaining == 50);
    CHECK(resumedWorld.GetItemEnergy(resumedWorld.FindItem(toolId), power)->energy == 750);
    resumedWorld.ElapseTicks(50); REQUIRE(resumedWorld.ProcessCraftingEvents());
    CHECK_FALSE(resumedWorld.FindItem(jobId).value);
    CHECK_FALSE(resumedWorld.FindItem(materialId).value);
    CHECK(resumedWorld.GetItemEnergy(resumedWorld.FindItem(toolId), power)->energy == 750);
    unsigned products = 0;
    for (const auto item : resumedWorld.GetActor(fresh)->inventory) {
        if (resumedWorld.GetItem(item)->type == content.FindItem("product")) { ++products; }
    }
    CHECK(products == 1);
    CHECK(resumedWorld.GetTick() == 250);
    CHECK_FALSE(resumedWorld.ProcessCraftingEvents());
    CHECK(resumedWorld.CheckInvariants());
}
}
