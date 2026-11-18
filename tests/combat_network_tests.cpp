#include "network/network.h"
#include "network/wire.h"
#include "combat/combat_system.h"
#include "content_fixture.h"

#include <catch2/catch_test_macros.hpp>
#include <initializer_list>
#include <chrono>
#include <thread>

namespace {
using namespace cataclysmx;
using namespace cataclysmx::network;

void CheckCommand(const Command& command) {
    wire::Writer w; REQUIRE(w.CommandValue(command));
    wire::Reader r{{w.bytes.data(), w.bytes.size()}}; Command decoded;
    REQUIRE(r.CommandValue(decoded)); REQUIRE(r.Done());
    wire::Writer again; REQUIRE(again.CommandValue(decoded)); CHECK(again.bytes == w.bytes);
    for (std::size_t size = 0; size < w.bytes.size(); ++size) {
        wire::Reader truncated{{w.bytes.data(), size}}; CHECK_FALSE(truncated.CommandValue(decoded));
    }
    // Appended art is unused in every command except study_art.
    if (command.kind != CommandKind::study_art) {
        w.bytes.back() = 1;
        wire::Reader bad{{w.bytes.data(), w.bytes.size()}}; CHECK_FALSE(bad.CommandValue(decoded));
    }
}

TEST_CASE("Protocol 21 combat commands roundtrip every operation and target branch", "[network][combat21][codec]") {
    for (auto operation : {EquipmentOperation::Hold, EquipmentOperation::Release, EquipmentOperation::Regrip,
        EquipmentOperation::Wear, EquipmentOperation::Remove}) {
        for (auto hand : {Hand::Left, Hand::Right}) for (auto grip : {GripKind::OneHanded, GripKind::TwoHanded}) {
            Command c; c.kind = CommandKind::equipment; c.equipment = {operation, 0, hand, grip};
            if (operation == EquipmentOperation::Hold || operation == EquipmentOperation::Wear || operation == EquipmentOperation::Remove) c.equipment.item = 91;
            if (ValidCommandShape(c)) CheckCommand(c);
            else { wire::Writer w; CHECK_FALSE(w.CommandValue(c)); }
        }
    }
    for (auto kind : {CommandKind::select_combat_style, CommandKind::study_art, CommandKind::pause_art_study, CommandKind::new_character}) {
        Command c; c.kind = kind;
        if (kind == CommandKind::select_combat_style) c.combatStyle = {42};
        if (kind == CommandKind::study_art) { c.book = {3}; c.art = {7}; }
        CheckCommand(c);
    }
    for (auto source : {AbilityGrantSourceKind::Art, AbilityGrantSourceKind::Innate}) {
        for (auto target : {AbilityTargetKind::Self, AbilityTargetKind::Actor, AbilityTargetKind::Tile, AbilityTargetKind::CardinalDirection}) {
            Command c; c.kind = CommandKind::use_ability; c.ability.ability = {2};
            c.ability.source = {source, source == AbilityGrantSourceKind::Art ? CombatArtId{7} : CombatArtId{}};
            c.ability.target.kind = target;
            if (target == AbilityTargetKind::Actor) c.ability.target.actor = 444;
            if (target == AbilityTargetKind::Tile) c.ability.target.tile = {42, -7, 0};
            if (target == AbilityTargetKind::CardinalDirection) c.ability.target.direction = CardinalDirection::West;
            CheckCommand(c);
        }
    }
}

TEST_CASE("Protocol 21 rejects invalid combat enums sources and unused legacy fields", "[network][combat21][codec]") {
    Command c; c.kind = CommandKind::use_ability; c.ability.ability = {1}; c.ability.source = {AbilityGrantSourceKind::Innate, {}};
    SECTION("grant enum") { c.ability.source.kind = static_cast<AbilityGrantSourceKind>(255); }
    SECTION("innate art") { c.ability.source.art = {3}; }
    SECTION("missing art source") { c.ability.source.kind = AbilityGrantSourceKind::Art; }
    SECTION("target enum") { c.ability.target.kind = static_cast<AbilityTargetKind>(255); }
    SECTION("direction enum") { c.ability.target.direction = static_cast<CardinalDirection>(255); }
    SECTION("unused actor") { c.ability.target.actor = 99; }
    SECTION("unused tile") { c.ability.target.tile.x = 1; }
    SECTION("unused direction") { c.ability.target.direction = CardinalDirection::East; }
    SECTION("unused command field") { c.item = 4; }
    SECTION("legacy unused target") { c = {}; c.target = 9; }
    SECTION("legacy unused direction") { c = {}; c.dx = 1; }
    SECTION("legacy unused item") { c = {}; c.item = 9; }
    SECTION("legacy unused capability") { c = {}; c.capability = {1}; }
    SECTION("legacy unused item action") { c = {}; c.itemAction = "activate"; }
    SECTION("legacy unused other item") { c = {}; c.otherItem = 9; }
    SECTION("legacy unused style") { c = {}; c.style = CombatStyle::Defensive; }
    SECTION("legacy unused recipe") { c = {}; c.recipe = {1}; }
    SECTION("legacy unused book") { c = {}; c.book = {1}; }
    SECTION("legacy unused storage") { c = {}; c.source = {ItemStorageKind::Container, {}, 9}; }
    SECTION("legacy unused equipment") { c = {}; c.equipment.item = 9; }
    SECTION("legacy unused catalog style") { c = {}; c.combatStyle = {1}; }
    SECTION("legacy unused ability") { c = {}; c.ability.ability = {1}; }
    SECTION("legacy unused art") { c = {}; c.art = {1}; }
    SECTION("nonwait cost") { c = {}; c.kind = CommandKind::attack; c.waitCost = 1; }
    SECTION("legacy unused interaction") { c = {}; c.interaction.object = 9; }
    SECTION("equipment enum") { c = {}; c.kind = CommandKind::equipment; c.equipment.operation = static_cast<EquipmentOperation>(255); }
    CHECK_FALSE(ValidCommandShape(c));
    wire::Writer w; CHECK_FALSE(w.CommandValue(c));
}

TEST_CASE("Protocol 21 decoder rejects corrupt appended enums and unused fields", "[network][combat21][codec]") {
    Command c; wire::Writer w; REQUIRE(w.CommandValue(c));
    // All appended bytes have canonical zero values for wait; corrupting any
    // single byte must be rejected, including enum values that are in range.
    constexpr std::size_t appendedSize = 50;
    REQUIRE(w.bytes.size() >= appendedSize);
    for (std::size_t i = w.bytes.size() - appendedSize; i < w.bytes.size(); ++i) {
        auto bytes = w.bytes; bytes[i] = 255;
        wire::Reader r{{bytes.data(), bytes.size()}}; Command decoded; INFO(i); CHECK_FALSE(r.CommandValue(decoded));
    }
}

TEST_CASE("Protocol 21 ability requests cannot manufacture an authoritative grant", "[network][combat21][server]") {
    const auto& content = TestContent(); REQUIRE(content.AbilityCount() != 0);
    Server server(content);
    Command request; request.kind = CommandKind::use_ability;
    request.ability.ability = {1}; request.ability.source = {AbilityGrantSourceKind::Innate, {}};
    REQUIRE(ValidCommandShape(request));
    wire::Writer w; REQUIRE(w.CommandValue(request));
    wire::Reader r{{w.bytes.data(), w.bytes.size()}}; Command decoded; REQUIRE(r.CommandValue(decoded));
    const auto result = server.Send(decoded);
    CHECK(result.outcome == Outcome::combat_refused); CHECK(result.combatReason == CombatRefusal::MissingGrant);
    CHECK_FALSE(result.advanced); CHECK(result.cost == 0);
}

WorldView CombatSnapshot() {
    WorldView v; v.width = 1; v.height = 1; v.cells.push_back({}); v.playerId = 1; v.characterId = 77;
    PlayerStatus player; player.id = 1; player.name = "Survivor"; player.character = 77; player.life = ActorLifeState::Dead;
    v.players.push_back(player);
    v.combat.life = ActorLifeState::Dead; v.combat.styleId = {3}; v.combat.action = ActorActionKind::AbilityWindup;
    v.combat.inexhaustible = true; v.combat.staggerRemaining = 7; v.combat.focusRemaining = 12;
    auto& attack = v.combat.feedback.result;
    attack.moveId = {2}; attack.reason = CombatRefusal::MissingCapability;
    attack.strikeCount = 3; attack.resolvedStrikes = 2; attack.totalHpLoss = 456;
    v.combatInfo.combat = v.combat;
    v.combatInfo.equipment.hands[1] = {88, GripKind::OneHanded, true};
    v.combatInfo.equipment.worn.push_back(89);
    v.combatInfo.equipment.encumbrance[static_cast<unsigned>(ContactRegion::Eyes)] = 17;
    v.combatInfo.moves.push_back({{2}, {CombatRefusal::Cooldown, {10, 20, 3}}, 99, true});
    v.combatInfo.abilities.push_back({{3}, {AbilityGrantSourceKind::Art, {4}}, {CombatRefusal::Locked, {40, 50, 6}}, 70, AbilityTargetKind::Tile});
    v.combatInfo.abilities.push_back({{3}, {AbilityGrantSourceKind::Innate, {}}, {}, 0, AbilityTargetKind::Tile});
    v.combatInfo.arts.push_back({{4}, 123456});
    v.combatInfo.manuals.push_back({{5}, {4}, CombatRefusal::Busy, 7500, 200, 123, true});
    InventoryEntry held; held.id = 88; held.type = {1}; held.heldHands = HandMask::Right; v.inventory.push_back(held);
    InventoryEntry worn; worn.id = 89; worn.type = {1}; worn.worn = true; v.inventory.push_back(worn);
    CombatEvent e; e.tick = 900; e.actor = 77; e.opponent = 66; e.move = {2}; e.ability = {3}; e.art = {4};
    e.reason = CombatRefusal::Dead; e.cost = 100; e.staminaSpent = 9; e.learnedArt = true;
    e.strike = {77, 66, {2}, StrikeOutcome::Contact, CombatRefusal::None, DefenseKind::Block, {6}, true, true, true, true,
        ContactRegion::Eyes, BodyPart::Head, 123, 234, 1000, 643, 543, 100, DisruptionResponse::Canceled};
    e.serial = 41;
    v.combatEvents.push_back(e);
    e.serial = 43; e.kind = CombatEventKind::ActorDied; v.combatEvents.push_back(e);
    return v;
}

TEST_CASE("Protocol 21 views preserve equipment contacts study and death metrics", "[network][combat21][codec]") {
    auto v = CombatSnapshot(); wire::Writer w; REQUIRE(w.View(v));
    wire::Reader r{{w.bytes.data(), w.bytes.size()}}; WorldView decoded;
    REQUIRE(r.View(decoded)); CHECK(r.Done()); CHECK(decoded.characterId == 77); CHECK(decoded.playerId == 1);
    CHECK(decoded.players[0].character == 77); CHECK(decoded.players[0].life == ActorLifeState::Dead);
    CHECK(decoded.combat == v.combat); CHECK(decoded.combatInfo == v.combatInfo);
    CHECK(decoded.combatEvents == v.combatEvents); CHECK(decoded.inventory == v.inventory);
    CHECK(decoded.combatEvents[0].serial == 41); CHECK(decoded.combatEvents[1].serial == 43);
    for (std::size_t size = 0; size < w.bytes.size(); ++size) {
        wire::Reader truncated{{w.bytes.data(), size}}; CHECK_FALSE(truncated.View(decoded));
    }
    // learnedArt precedes the final serial and the empty enemy-health count.
    w.bytes[w.bytes.size() - sizeof(std::uint16_t) - sizeof(std::uint64_t) - sizeof(std::uint8_t)] = 2;
    wire::Reader bad{{w.bytes.data(), w.bytes.size()}}; CHECK_FALSE(bad.View(decoded));
}

TEST_CASE("Protocol 21 rejects malformed combat view values", "[network][combat21][codec]") {
    auto v = CombatSnapshot();
    SECTION("life") { v.combat.life = static_cast<ActorLifeState>(2); }
    SECTION("strikes") { v.combat.feedback.result.resolvedStrikes = 4; }
    SECTION("source") { v.combatInfo.abilities[0].source.art = {}; }
    SECTION("duplicate ability source") { v.combatInfo.abilities.push_back(v.combatInfo.abilities[0]); }
    SECTION("cost") { v.combatInfo.moves[0].availability.cost.recoveryCost = 0; }
    SECTION("ratio") { v.combatInfo.manuals[0].successChance = ratioScale + 1; }
    SECTION("progress") { v.combatInfo.manuals[0].remaining = 201; }
    SECTION("event enum") { v.combatEvents[0].strike.region = static_cast<ContactRegion>(255); }
    SECTION("zero history serial") { v.combatEvents[0].serial = 0; }
    SECTION("duplicate history serial") { v.combatEvents[1].serial = v.combatEvents[0].serial; }
    SECTION("descending history serial") { v.combatEvents[1].serial = v.combatEvents[0].serial - 1; }
    SECTION("held mask") { v.inventory[0].heldHands = static_cast<HandMask>(4); }
    SECTION("worn and held") { v.inventory[0].worn = true; }
    wire::Writer w; CHECK_FALSE(w.View(v));
}

TEST_CASE("Protocol 21 history serials reject replayed or reordered records", "[network][combat21][codec]") {
    auto v = CombatSnapshot();
    // Serials order the presentation history independently of event ticks.
    v.combatEvents[0].tick = 900; v.combatEvents[1].tick = 900;
    wire::Writer w; REQUIRE(w.View(v));
    const auto serialOffset = w.bytes.size() - sizeof(std::uint16_t) - sizeof(std::uint64_t);
    for (const auto serial : {std::uint64_t{0}, std::uint64_t{40}, std::uint64_t{41}}) {
        wire::Writer replacement; replacement.U64(serial);
        auto bytes = w.bytes;
        for (std::size_t i = 0; i < replacement.bytes.size(); ++i) bytes[serialOffset + i] = replacement.bytes[i];
        wire::Reader r{{bytes.data(), bytes.size()}}; WorldView decoded;
        INFO(serial); CHECK_FALSE(r.View(decoded));
    }
    v.combatEvents[1].serial = UINT64_MAX;
    wire::Writer maximum; REQUIRE(maximum.View(v));
    wire::Reader r{{maximum.bytes.data(), maximum.bytes.size()}}; WorldView decoded;
    REQUIRE(r.View(decoded)); CHECK(r.Done()); CHECK(decoded.combatEvents[1].serial == UINT64_MAX);
    // Raw runner events keep their zero default; only published histories
    // require an assigned serial. An empty history is also valid.
    CHECK(CombatEvent{}.serial == 0);
    v.combatEvents.clear(); wire::Writer empty; REQUIRE(empty.View(v));
}

TEST_CASE("Protocol 21 receipts preserve new outcomes and refusal codes", "[network][combat21][codec]") {
    for (auto outcome : {Outcome::equipment_changed, Outcome::ability_used, Outcome::combat_refused,
        Outcome::art_study_started, Outcome::art_study_paused, Outcome::character_dead, Outcome::character_created}) {
        PlayerCommandResult receipt{1, 2, 3, {outcome}}; receipt.result.combatReason = CombatRefusal::MissingGrant;
        wire::Writer w; REQUIRE(w.Result(receipt)); wire::Reader r{{w.bytes.data(), w.bytes.size()}}; PlayerCommandResult decoded;
        REQUIRE(r.Result(decoded)); CHECK(r.Done()); CHECK(decoded.result.outcome == outcome);
        CHECK(decoded.result.combatReason == CombatRefusal::MissingGrant);
        w.bytes.back() = 255; wire::Reader bad{{w.bytes.data(), w.bytes.size()}}; CHECK_FALSE(bad.Result(decoded));
    }
}

TEST_CASE("Protocol 21 validates combat catalog IDs and fingerprints weighted content", "[network][combat21]") {
    const ContentSource source{"combat-network.json5", "{format_version:1,definitions:[]}"};
    auto loaded = LoadContent({&source, 1}); REQUIRE(loaded); auto& content = *loaded;
    WorldView v; v.combat.styleId = content.FindCombatStyle("basic_balanced");
    v.combatInfo.moves.push_back({content.FindCombatMove("basic_fist")});
    CHECK(wire::ValidCombatDefinitions(v, content));
    v.combatInfo.moves[0].move = {UINT32_MAX}; CHECK_FALSE(wire::ValidCombatDefinitions(v, content));
    v.combatInfo = {}; v.combatEvents.push_back({}); v.combatEvents.back().strike.responseMove = {UINT32_MAX};
    v.combatEvents.back().serial = 1;
    CHECK_FALSE(wire::ValidCombatDefinitions(v, content));
    const auto original = ContentFingerprint(content);
    auto& style = const_cast<CombatStyleDefinition&>(content.GetCombatStyle(content.FindCombatStyle("basic_balanced")));
    ++style.moves[0].weight; CHECK(ContentFingerprint(content) != original);
    --style.moves[0].weight; CHECK(ContentFingerprint(content) == original);
    auto& move = const_cast<CombatMoveDefinition&>(content.GetCombatMove(content.FindCombatMove("basic_fist")));
    ++move.strikes[0].damageRatio; CHECK(ContentFingerprint(content) != original);
}

TEST_CASE("Protocol 21 replaces a remote dead character while keeping its session", "[network][combat21][lifecycle]") {
    const auto& content = TestContent(); Server server(content, 7);
    auto runtime = NetworkRuntime::Create(); REQUIRE(runtime);
    LanHost host(**runtime, server, content); REQUIRE(host.Open({0, 0, "Combat lifecycle"}));
    LanClient client(**runtime, content);
    REQUIRE(client.Connect({"127.0.0.1", host.GetPort(), "Guest", 0}));
    const auto pumpUntil = [&](const auto& done) {
        for (int i = 0; i < 2000 && !done(); ++i) {
            const auto update = host.Pump(1000); REQUIRE_FALSE(update.networkError);
            client.Pump(1000); REQUIRE(client.GetState() != ConnectionState::Rejected);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        REQUIRE(done());
    };
    pumpUntil([&] { return client.HasView(); });
    const auto session = client.View().playerId;
    const auto oldCharacter = client.View().characterId;
    const auto token = client.GetReconnectToken();
    REQUIRE(oldCharacter != 0);
    auto& world = server.GetWorld();
    const auto actor = world.FindActor(oldCharacter); REQUIRE(actor.value);
    world.Combat().Damage(actor, {}, BodyPart::Torso, {DamageType::Blunt, 10000});
    pumpUntil([&] { return client.View().combat.life == ActorLifeState::Dead; });
    Command fresh; fresh.kind = CommandKind::new_character;
    const auto sequence = client.Submit(fresh); REQUIRE(sequence);
    pumpUntil([&] { return client.View().characterId != oldCharacter; });
    CHECK(client.View().playerId == session); CHECK(client.GetReconnectToken() == token);
    CHECK(client.View().characterId != session); CHECK(client.View().combat.life == ActorLifeState::Alive);
    bool created = false;
    for (const auto& receipt : client.TakeResults()) {
        if (receipt.sequence == *sequence && receipt.result.outcome == Outcome::character_created) {
            CHECK(receipt.player == session); created = true;
        }
    }
    CHECK(created);
}

TEST_CASE("Protocol 21 fingerprints include armor contacts and grip scaling", "[network][combat21]") {
    const ContentSource source{"equipment-network.json5", R"({format_version:1,definitions:[
        {type:'item',id:'gear',name:'Gear',mass_g:1,volume_ml:1,
         melee:{damage:[{type:'blunt',amount:100}],grips:[{kind:'one_handed'}]},
         armor:{regions:[{region:'torso',coverage:9000,encumbrance:1}],protection:{blunt:100}}}
    ]})"};
    auto loaded = LoadContent({&source, 1}); REQUIRE(loaded);
    auto& content = *loaded;
    auto& gear = const_cast<ItemDefinition&>(content.GetItem(content.FindItem("gear")));
    const auto original = ContentFingerprint(content);
    SECTION("coverage") { ++gear.armor->regions[0].coverage; }
    SECTION("encumbrance") { ++gear.armor->regions[0].encumbrance; }
    SECTION("protection") { ++gear.armor->protection[0]; }
    SECTION("grip damage") { ++gear.melee->grips[0].damageRatio; }
    SECTION("grip stamina") { ++gear.melee->grips[0].staminaRatio; }
    SECTION("grip cost") { ++gear.melee->grips[0].costRatio; }
    CHECK(ContentFingerprint(content) != original);
}
}
