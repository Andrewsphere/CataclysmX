#include "combat/combat_persistence.h"
#include "combat/combat_types.h"
#include "combat/combat_system.h"
#include "core/save_database.h"
#include "content_fixture.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_message.hpp>
#include <sqlite3.h>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <initializer_list>
#include <string>

namespace {
using namespace cataclysmx;

DataObject Equipment(PersistentId left, GripKind leftGrip, PersistentId right, GripKind rightGrip,
    std::initializer_list<PersistentId> worn = {}) {
    DataObject d; d.SetUnsigned("version", 1); DataObject hands = DataObject::List();
    for (const auto& h : {SavedHeldItem{left, leftGrip}, SavedHeldItem{right, rightGrip}}) {
        DataObject v; v.SetUnsigned("item", h.item); v.SetUnsigned("grip", static_cast<unsigned>(h.grip)); hands.Append(eastl::move(v));
    }
    DataObject list = DataObject::List(); for (auto id : worn) { list.Append(DataObject::Unsigned(id)); }
    d.Set("hands", eastl::move(hands)); d.Set("worn", eastl::move(list)); return d;
}
const ContentCatalog& ExpandedContent() {
    static const auto content = [] {
        Diagnostics diagnostics; auto loaded = LoadContentPack(CATACLYSMX_CONTENT_PACK, &diagnostics);
        INFO(FormatDiagnostics(diagnostics).c_str()); REQUIRE(loaded);
        REQUIRE(loaded->FindCombatArt("boxing").value); REQUIRE(loaded->FindAbility("focus").value);
        REQUIRE(loaded->FindBook("boxing_manual_text").value);
        return eastl::move(*loaded);
    }();
    return content;
}
CombatState BoundState(const ContentCatalog& content = TestContent()) {
    auto s = MakeCombatState(true); s.styleId = content.FindCombatStyle("basic_balanced");
    REQUIRE(s.styleId.value); return s;
}
CombatState AbilityState(const ContentCatalog& c, const char* name) {
    auto state = BoundState(c); auto& a = state.action; a.ability = c.FindAbility(name);
    REQUIRE(a.ability.value); const auto& ability = c.GetAbility(a.ability);
    a.kind = ability.cost.windupCost ? ActorActionKind::AbilityWindup : ActorActionKind::Recovery;
    a.definitionDriven = true; a.committedStyle = state.styleId; a.recoveryTicks = 73;
    a.abilitySource = {AbilityGrantSourceKind::Innate, {}}; a.trainingWork = ability.training.work;
    a.abilityTarget.kind = ability.targeting;
    if (ability.targeting == AbilityTargetKind::Tile) { a.abilityTarget.tile = {1, 0, 0}; }
    if (ability.execution == AbilityExecution::Sweep) {
        a.moveId = ability.sweepMove; a.sources.primaryKind = AttackSourceKind::HeldItem;
        a.sources.hands[0] = {71, GripKind::OneHanded, true}; a.sources.functionalHands = HandMask::Left;
    }
    REQUIRE(ValidateCombatState(state, c)); return state;
}
CombatState MeleeState(const ContentCatalog& c) {
    auto state = BoundState(c); auto& a = state.action;
    a.kind = ActorActionKind::MeleeWindup; a.definitionDriven = true; a.target = UINT64_MAX;
    a.moveId = c.FindCombatMove("basic_heavy"); a.committedStyle = state.styleId; a.recoveryTicks = 73;
    a.weapon = 71; a.sources.primaryKind = AttackSourceKind::HeldItem;
    a.sources.hands[0] = {71, GripKind::OneHanded, true}; a.sources.functionalHands = HandMask::Left;
    REQUIRE(ValidateCombatState(state, c)); return state;
}
struct SaveFile {
    std::filesystem::path directory;
    std::string path;
    SaveFile() {
        static std::atomic<unsigned> sequence{0};
        directory = std::filesystem::temp_directory_path() / ("cataclysmx-combat-persistence-"
            + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(sequence++));
        REQUIRE(std::filesystem::create_directory(directory)); path = (directory / "world.sqlite").string();
    }
    ~SaveFile() { std::error_code error; std::filesystem::remove_all(directory, error); }
};
struct RawDatabase {
    sqlite3* db = nullptr;
    explicit RawDatabase(const char* path) { REQUIRE(sqlite3_open(path, &db) == SQLITE_OK); }
    ~RawDatabase() { sqlite3_close(db); }
    void Exec(const char* sql) { REQUIRE(sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK); }
    void LegacyCombat(const DataObject& combat) {
        const auto json = EncodeDataObject(combat); sqlite3_stmt* statement = nullptr;
        REQUIRE(sqlite3_prepare_v2(db, "UPDATE chunks SET payload=json_remove(json_set(payload,'$.actors[0].combat',json(?1)),'$.actors[0].equipment','$.actors[0].character_combat')", -1, &statement, nullptr) == SQLITE_OK);
        REQUIRE(sqlite3_bind_text(statement, 1, json.c_str(), static_cast<int>(json.size()), SQLITE_TRANSIENT) == SQLITE_OK);
        REQUIRE(sqlite3_step(statement) == SQLITE_DONE); REQUIRE(sqlite3_finalize(statement) == SQLITE_OK);
        Exec("UPDATE world_state SET payload=json_remove(payload,'$.power','$.session.players[0].session')");
        Exec("UPDATE metadata SET format_version=13");
    }
};
ChunkArchive Chunk(const ContentCatalog& content = TestContent()) {
    ChunkArchive chunk{}; chunk.tiles.fill({content.FindTerrain("floor"), 0});
    chunk.actors.push_back({1, {}, true, 0}); return chunk;
}
SaveSnapshot Snapshot() {
    SaveSnapshot s; s.nextId = 100; s.session.player = 1;
    SavedPlayer p; p.actor = 1; p.name = "Host"; s.session.players.push_back(p); return s;
}

TEST_CASE("Equipment codec enforces two-hand occupancy and ordered exclusive worn identities", "[combat][save]") {
    const auto d = Equipment(8, GripKind::TwoHanded, 8, GripKind::TwoHanded, {19, 11});
    const auto s = ReadEquipment(d); REQUIRE(s);
    CHECK(s->hands[0].item == 8); CHECK(s->hands[1].item == 8);
    REQUIRE(s->worn.size() == 2); CHECK(s->worn[0] == 19); CHECK(s->worn[1] == 11);
    CHECK(ReadEquipment(DataObject{}));
    CHECK_FALSE(ReadEquipment(Equipment(8, GripKind::OneHanded, 8, GripKind::OneHanded)));
    CHECK_FALSE(ReadEquipment(Equipment(8, GripKind::TwoHanded, 9, GripKind::OneHanded)));
    CHECK_FALSE(ReadEquipment(Equipment(0, GripKind::TwoHanded, 0, GripKind::OneHanded)));
    CHECK_FALSE(ReadEquipment(Equipment(8, GripKind::OneHanded, 0, GripKind::OneHanded, {8})));
    CHECK_FALSE(ReadEquipment(Equipment(0, GripKind::OneHanded, 0, GripKind::OneHanded, {4, 4})));
    auto extra = d; extra.SetUnsigned("extra", 0); CHECK_FALSE(ReadEquipment(extra));
}

TEST_CASE("Legacy combat migration preserves paid windup recovery cooldowns and accuracy", "[combat][save]") {
    const auto& c = TestContent(); auto old = MakeCombatState(true);
    old.style = CombatStyle::Defensive; old.stamina = 43; old.staminaElapsed = 7;
    old.accuracy = 81; old.heavyCooldown = 91; old.reactionCooldown = 23;
    old.action = {ActorActionKind::MeleeWindup, UINT64_MAX, static_cast<unsigned>(CombatMove::HeavyAttack), 167, 15, 72};
    const auto legacy = CaptureCombatState(old); auto migrated = ReadCombatState(legacy, c); REQUIRE(migrated);
    CHECK(migrated->styleId == c.FindCombatStyle("basic_defensive")); CHECK(migrated->stamina == 43);
    CHECK(migrated->staminaRegenerationRemainder == 7000); CHECK(migrated->staminaElapsed == 0);
    CHECK(migrated->legacyAccuracyOverride); CHECK(migrated->intrinsicMissOverride == 1900);
    CHECK(migrated->action.definitionDriven); CHECK(migrated->action.moveId == c.FindCombatMove("basic_heavy"));
    CHECK(migrated->action.sources.hands[0].item == 72); CHECK(migrated->action.recoveryTicks == 167);
    CHECK(migrated->action.target == UINT64_MAX); CHECK(migrated->action.ratingPenalty100 == 1500);
    REQUIRE(migrated->moveCooldowns.size() == 2); CHECK(migrated->moveCooldowns[0].remaining == 91);
    const auto roundtrip = ReadCombatState(CaptureCombatState(*migrated, c), c); REQUIRE(roundtrip); CHECK(*roundtrip == *migrated);
    auto v1 = legacy; v1.SetUnsigned("version", 1);
    DataObject action;
    for (auto field : {"kind", "target", "move", "recoveryTicks", "accuracyPenalty"}) { action.Set(field, *legacy.Find("action")->Find(field)); }
    v1.Set("action", eastl::move(action)); auto first = ReadCombatState(v1, c); REQUIRE(first); CHECK(first->action.sources.hands[0].item == 0);
}

TEST_CASE("Expanded combat rejects unknown definitions and nested schema drift", "[combat][save]") {
    const auto& c = TestContent(); auto d = CaptureCombatState(BoundState(), c);
    SECTION("unknown style") { d.SetString("style", "missing_style"); }
    SECTION("numeric style identity") { d.SetUnsigned("style", 1); }
    SECTION("unknown pending move") { d.Find("action")->SetString("moveId", "missing_move"); }
    SECTION("unknown ability") { d.Find("action")->SetString("ability", "missing_ability"); }
    SECTION("unknown art") { d.Find("action")->SetString("trainingArt", "missing_art"); }
    SECTION("unknown source fields") { d.Find("action")->Find("sources")->SetUnsigned("extra", 1); }
    SECTION("reach overflow") { d.Find("reach")->SetUnsigned("regions", UINT64_MAX); }
    SECTION("duplicate cooldown") {
        DataObject list = DataObject::List(), cd; cd.SetString("move", "basic_heavy"); cd.SetUnsigned("remaining", 4);
        list.Append(cd); list.Append(cd); d.Set("moveCooldowns", eastl::move(list));
    }
    SECTION("unknown version") { d.SetUnsigned("version", 4); }
    Diagnostics diagnostics; CHECK_FALSE(ReadCombatState(d, c, &diagnostics)); CHECK_FALSE(diagnostics.Empty());
}

TEST_CASE("Character combat preserves art experience independent grants cooldowns and funded manual progress", "[combat][save]") {
    const auto& c = ExpandedContent();
    const auto art = c.FindCombatArt("boxing"); const auto ability = c.FindAbility("focus");
    CharacterCombatState s; s.arts.push_back({art, UINT64_MAX});
    s.abilityGrants.push_back({ability, {AbilityGrantSourceKind::Art, art}});
    s.abilityGrants.push_back({ability, {AbilityGrantSourceKind::Innate, {}}});
    s.abilityCooldowns.push_back({ability, {AbilityGrantSourceKind::Art, art}, 73});
    s.abilityCooldowns.push_back({ability, {AbilityGrantSourceKind::Innate, {}}, 11});
    ManualAttemptProgress m; m.book = c.FindBook("boxing_manual_text"); m.art = art; m.remaining = 235; m.totalDuration = 900;
    m.successChance = 3500; m.active = true; m.fundedTicks = 35; m.pauseRequested = true; s.manuals.push_back(m);
    const auto d = CaptureCharacterCombat(&s, c); auto decoded = ReadCharacterCombat(d, c); REQUIRE(decoded); REQUIRE(*decoded);
    CHECK(**decoded == s); CHECK(EqualCharacterCombat(decoded->get(), &s));
    const auto clone = CloneCharacterCombat(&s); CHECK(EqualCharacterCombat(clone.get(), &s)); CHECK(clone.get() != &s);
    auto null = ReadCharacterCombat(DataObject{}, c); REQUIRE(null); CHECK_FALSE(*null);
    CharacterCombatState empty; auto present = ReadCharacterCombat(CaptureCharacterCombat(&empty, c), c);
    REQUIRE(present); REQUIRE(*present); CHECK_FALSE(EqualCharacterCombat(present->get(), nullptr));
    auto bad = d; bad.SetString("arts", "not a list"); CHECK_FALSE(ReadCharacterCombat(bad, c));
}

TEST_CASE("Combat envelope preserves ability source target exertion effects reach and corpse identity", "[combat][save]") {
    const auto& c = ExpandedContent(); auto s = BoundState(c);
    s.stamina = 26; s.staminaRegenerationRemainder = 8157; s.inexhaustible = true;
    s.authoredTrainingRanks = true; s.offensiveTrainingRank = 7; s.defensiveTrainingRank = 8;
    s.attackRating100 = -117; s.defenseRating100 = 255; s.reach = {2, ReachHeight::Upper, detailedContactMask};
    s.focusRemaining = 97; s.focusRatingBonus100 = 150; s.staggerRemaining = 4;
    s.combatLockRemaining = 127; s.exertion = ExertionKind::Combat;
    s.moveCooldowns.push_back({c.FindCombatMove("basic_heavy"), 84}); s.openings.push_back({UINT64_MAX, 24});
    SECTION("pending ability") {
        auto& a = s.action; a.kind = ActorActionKind::AbilityWindup; a.definitionDriven = true;
        a.ability = c.FindAbility("leap"); a.abilitySource = {AbilityGrantSourceKind::Innate, {}};
        a.abilityTarget.kind = c.GetAbility(a.ability).targeting;
        if (a.abilityTarget.kind == AbilityTargetKind::Actor) { a.abilityTarget.actor = UINT64_MAX; }
        if (a.abilityTarget.kind == AbilityTargetKind::Tile) { a.abilityTarget.tile = {1, 2, 0}; }
        a.committedStyle = s.styleId; a.recoveryTicks = 57; a.trainingWork = 3799;
        if (c.GetAbility(a.ability).execution == AbilityExecution::Sweep) { a.moveId = c.GetAbility(a.ability).sweepMove; }
    }
    SECTION("finalized corpse") {
        s.body.parts[static_cast<unsigned>(BodyPart::Head)].hp = 0; s.life = ActorLifeState::Dead; s.corpse = 319;
    }
    SECTION("awaiting corpse finalization") {
        s.body.parts[static_cast<unsigned>(BodyPart::Torso)].hp = 0; s.life = ActorLifeState::Dead;
    }
    REQUIRE(ValidateCombatState(s, c)); const auto d = CaptureCombatState(s, c); auto decoded = ReadCombatState(d, c);
    REQUIRE(decoded); CHECK(*decoded == s);
    auto json = DecodeDataObject(EncodeDataObject(d)); REQUIRE(json); auto fromJson = ReadCombatState(*json, c); REQUIRE(fromJson); CHECK(*fromJson == s);
}

TEST_CASE("Format 13 combat checkpoint is rejected", "[combat][save][database]") {
    const auto& c = TestContent(); SaveFile file; auto chunk = Chunk(); auto snapshot = Snapshot();
    auto old = MakeCombatState(true); old.stamina = 31; old.heavyCooldown = 87;
    old.action = {ActorActionKind::MeleeWindup, 99, static_cast<unsigned>(CombatMove::HeavyAttack), 73};
    chunk.actors[0].remainingTicks = 41; chunk.actors[0].combat = CaptureCombatState(old);
    {
        SaveDatabase db(c); REQUIRE(db.Create(file.path.c_str())); REQUIRE(db.Begin()); REQUIRE(db.WriteChunk(chunk));
        REQUIRE(db.WriteSnapshot(snapshot)); REQUIRE(db.Commit());
    }
    { RawDatabase raw(file.path.c_str()); raw.LegacyCombat(chunk.actors[0].combat); }
    SaveDatabase db(c);
    CHECK(db.Open(file.path.c_str()) == eastl::unexpected(SaveError::UnsupportedVersion));
}

TEST_CASE("Session roster preserves stable session across replacement and rejects duplicate sessions", "[combat][save][database]") {
    SaveFile file; auto chunk = Chunk(); auto snapshot = Snapshot();
    chunk.actors[0].id = 7; snapshot.session.player = 7; snapshot.session.players[0].actor = 7; snapshot.session.players[0].session = 1;
    SaveDatabase db(TestContent()); REQUIRE(db.Create(file.path.c_str())); REQUIRE(db.Begin()); REQUIRE(db.WriteChunk(chunk));
    REQUIRE(db.WriteSnapshot(snapshot)); REQUIRE(db.Commit());
    SaveDatabase reader(TestContent()); auto loaded = reader.Open(file.path.c_str()); REQUIRE(loaded);
    CHECK(loaded->session.player == 7); CHECK(loaded->session.players[0].actor == 7); CHECK(loaded->session.players[0].session == 1);
    auto guest = snapshot.session.players[0]; guest.actor = 8; guest.name = "Guest"; guest.reconnectToken = 19;
    snapshot.session.players.push_back(guest);
    REQUIRE(db.Begin()); CHECK_FALSE(db.WriteSnapshot(snapshot));
}

TEST_CASE("Mixed expanded actor payload survives database reload and invalid equipment ownership fails", "[combat][save][database]") {
    const auto& c = ExpandedContent(); const auto art = c.FindCombatArt("boxing"); const auto abilityId = c.FindAbility("focus");
    SaveFile file; auto chunk = Chunk(c); auto snapshot = Snapshot(); auto state = BoundState(c);
    state.stamina = 42; state.focusRemaining = 89; state.focusRatingBonus100 = 137;
    state.moveCooldowns.push_back({c.FindCombatMove("basic_heavy"), 97});
    CharacterCombatState character; character.arts.push_back({art, 347159});
    character.abilityGrants.push_back({abilityId, {AbilityGrantSourceKind::Art, art}});
    character.abilityCooldowns.push_back({abilityId, {AbilityGrantSourceKind::Innate, {}}, 113});
    ManualAttemptProgress manual; manual.book = c.FindBook("boxing_manual_text"); manual.art = art; manual.remaining = 247;
    manual.totalDuration = 501; manual.successChance = 4100; manual.active = true; manual.fundedTicks = 47;
    manual.pauseRequested = true; character.manuals.push_back(manual);
    auto& actor = chunk.actors[0]; actor.remainingTicks = 47;
    SECTION("funded manual with a pending pause") {}
    SECTION("ability windup with an art grant source") {
        character.manuals.clear();
        auto& action = state.action; action.kind = ActorActionKind::AbilityWindup; action.definitionDriven = true;
        action.ability = c.FindAbility("leap"); action.abilitySource = {AbilityGrantSourceKind::Art, art};
        action.trainingArt = art; // UseAbility snapshots the art of its selected grant.
        character.abilityGrants.push_back({action.ability, action.abilitySource});
        action.committedStyle = state.styleId; action.recoveryTicks = 79; action.trainingWork = 3571;
        const auto& ability = c.GetAbility(action.ability); action.abilityTarget.kind = ability.targeting;
        if (ability.targeting == AbilityTargetKind::Actor) { action.abilityTarget.actor = 77; }
        if (ability.targeting == AbilityTargetKind::Tile) { action.abilityTarget.tile = {1, 0, 0}; }
        if (ability.execution == AbilityExecution::Sweep) { action.moveId = ability.sweepMove; }
    }
    SECTION("melee recovery keeps paired sources without reconstructing them") {
        character.manuals.clear();
        auto& action = state.action; action.kind = ActorActionKind::Recovery; action.definitionDriven = true;
        action.moveId = c.FindCombatMove("basic_paired"); action.committedStyle = state.styleId;
        action.target = 99; action.recoveryTicks = 71; action.ratingPenalty100 = 170;
        action.trainingWork = 9971; action.weapon = 3;
        action.sources.primaryHand = Hand::Right; action.sources.paired = true;
        action.sources.primaryKind = AttackSourceKind::HeldItem; action.sources.secondaryKind = AttackSourceKind::HeldItem;
        action.sources.hands[0] = {2, GripKind::OneHanded, true}; action.sources.hands[1] = {3, GripKind::OneHanded, true};
        action.sources.functionalHands = HandMask::Both;
    }
    REQUIRE(ValidateCombatState(state, c));
    actor.combat = CaptureCombatState(state, c); actor.characterCombat = CaptureCharacterCombat(&character, c);
    actor.wieldedItem = 2; actor.equipment = Equipment(2, GripKind::OneHanded, 3, GripKind::OneHanded);
    chunk.items.push_back({2, c.FindItem("rock"), LocationKind::actor, {}, 1});
    chunk.items.push_back({3, c.FindItem("rock"), LocationKind::actor, {}, 1});
    {
        SaveDatabase db(c); REQUIRE(db.Create(file.path.c_str())); REQUIRE(db.Begin()); REQUIRE(db.WriteChunk(chunk));
        REQUIRE(db.WriteSnapshot(snapshot)); REQUIRE(db.Commit());
    }
    SaveDatabase db(c); auto loaded = db.Open(file.path.c_str()); REQUIRE(loaded);
    auto restored = db.ReadChunk({}, 100); REQUIRE(restored); REQUIRE(*restored);
    const auto& saved = (**restored).actors[0]; CHECK(saved.remainingTicks == 47);
    CHECK(saved.combat == actor.combat); CHECK(saved.characterCombat == actor.characterCombat); CHECK(saved.equipment == actor.equipment);
    auto decoded = ReadCharacterCombat(saved.characterCombat, c); REQUIRE(decoded); CHECK(EqualCharacterCombat(decoded->get(), &character));
    chunk.items[1].location = LocationKind::ground; chunk.items[1].owner = 0;
    REQUIRE(db.Begin()); CHECK_FALSE(db.WriteChunk(chunk));
}

TEST_CASE("Database retains dead historical actors alongside replacement characters", "[combat][save][database]") {
    const auto& c = TestContent(); SaveFile file; auto chunk = Chunk(); auto snapshot = Snapshot();
    auto dead = BoundState(); dead.body.parts[static_cast<unsigned>(BodyPart::Torso)].hp = 0;
    dead.life = ActorLifeState::Dead;
    SECTION("corpse creation still pending") {}
    SECTION("corpse identity already finalized") {
        dead.corpse = 3; chunk.items.push_back({3, TestItem("backpack"), LocationKind::ground, {}, 0});
    }
    chunk.actors[0].combat = CaptureCombatState(dead, c);
    chunk.actors.push_back({7, {}, true, 0}); // The dead actor no longer occupies this cell.
    snapshot.session.player = 7; snapshot.session.players[0].actor = 7; snapshot.session.players[0].session = 1;
    {
        SaveDatabase db(c); REQUIRE(db.Create(file.path.c_str())); REQUIRE(db.Begin()); REQUIRE(db.WriteChunk(chunk));
        REQUIRE(db.WriteSnapshot(snapshot)); REQUIRE(db.Commit());
    }
    SaveDatabase db(c); auto loaded = db.Open(file.path.c_str()); REQUIRE(loaded);
    auto restored = db.ReadChunk({}, 100); REQUIRE(restored); REQUIRE(*restored);
    REQUIRE((**restored).actors.size() == 2); auto state = ReadCombatState((**restored).actors[0].combat, c); REQUIRE(state);
    CHECK(state->life == ActorLifeState::Dead); CHECK(state->corpse == dead.corpse); CHECK(state->action == ActorActionState{});
    CHECK(loaded->session.players[0].session == 1); CHECK(loaded->session.players[0].actor == 7);
}

TEST_CASE("Parked terminal hosts survive full database validation with explicit and implicit rosters", "[combat][save][database]") {
    const auto& c = TestContent(); SaveFile file; auto chunk = Chunk(); auto snapshot = Snapshot();
    auto dead = BoundState(); dead.body.parts[static_cast<unsigned>(BodyPart::Head)].hp = 0;
    REQUIRE(ValidateBody(dead.body, dead.injury)); dead.life = ActorLifeState::Dead;
    chunk.actors[0].combat = CaptureCombatState(dead, c); chunk.actors[0].parked = true;
    SECTION("explicit roster") { snapshot.session.players[0].session = 19; }
    SECTION("implicit host roster") { snapshot.session.players.clear(); }
    {
        SaveDatabase db(c); REQUIRE(db.Create(file.path.c_str())); REQUIRE(db.Begin());
        REQUIRE(db.WriteChunk(chunk)); REQUIRE(db.WriteSnapshot(snapshot)); REQUIRE(db.Commit());
    }
    {
        SaveDatabase observer(c); const auto current = observer.Open(file.path.c_str()); REQUIRE(current);
        CHECK(current->session.player == 1);
        if (!snapshot.session.players.empty()) {
            REQUIRE(current->session.players.size() == 1); CHECK(current->session.players[0].session == 19);
        }
    }
    // Old-format checkpoints are rejected in every roster representation too.
    auto legacy = MakeCombatState(true); legacy.body.parts[static_cast<unsigned>(BodyPart::Head)].hp = 0;
    legacy.action = {ActorActionKind::MeleeWindup, 99, static_cast<unsigned>(CombatMove::HeavyAttack), 73};
    REQUIRE(ValidateBody(legacy.body, legacy.injury));
    {
        RawDatabase raw(file.path.c_str()); raw.LegacyCombat(CaptureCombatState(legacy));
        raw.Exec("UPDATE chunks SET payload=json_set(payload,'$.actors[0].remaining_ticks','39')");
    }
    SaveDatabase db(c);
    CHECK(db.Open(file.path.c_str()) == eastl::unexpected(SaveError::UnsupportedVersion));
}

TEST_CASE("Full combat reads preserve exact pending source commitments and windup flags", "[combat][save]") {
    const auto& c = ExpandedContent(); auto state = BoundState(c); auto& a = state.action;
    state.styleId = c.FindCombatStyle("weapon_guard");
    a.kind = ActorActionKind::MeleeWindup; a.definitionDriven = true; a.target = UINT64_MAX;
    a.moveId = c.FindCombatMove("weapon_drive"); a.committedStyle = state.styleId;
    a.trainingArt = c.FindCombatArt("weapon_forms"); a.recoveryTicks = 193; a.ratingPenalty100 = 170;
    a.trainingWork = 6387; a.weapon = 71;
    a.sources.primaryKind = AttackSourceKind::HeldItem;
    a.sources.hands[0] = {71, GripKind::TwoHanded, true}; a.sources.hands[1] = a.sources.hands[0];
    a.sources.functionalHands = HandMask::Both;
    REQUIRE(ValidateCombatState(state, c));
    const auto encoded = CaptureCombatState(state, c); const auto json = DecodeDataObject(EncodeDataObject(encoded)); REQUIRE(json);
    auto read = ReadCombatState(*json, c); REQUIRE(read); CHECK(*read == state);
    CHECK(read->action.sources == a.sources); CHECK_FALSE(read->action.counter); CHECK(read->action.definitionDriven);
    CHECK(CaptureCombatState(*read, c) == encoded);
}

TEST_CASE("Ability persistence rejects contradictory strike sources and art relationships", "[combat][save]") {
    const auto& c = ExpandedContent();
    auto state = AbilityState(c, "sweep"); auto data = CaptureCombatState(state, c);
    SECTION("sweep cannot substitute another existing strike move") {
        state.action.moveId = c.FindCombatMove("basic_weapon"); data.Find("action")->SetString("moveId", "basic_weapon");
    }
    SECTION("focus cannot carry a strike move") {
        state = AbilityState(c, "focus"); data = CaptureCombatState(state, c);
        state.action.moveId = c.FindCombatMove("basic_weapon"); data.Find("action")->SetString("moveId", "basic_weapon");
    }
    SECTION("leap cannot carry committed hands") {
        state = AbilityState(c, "leap"); data = CaptureCombatState(state, c);
        const auto melee = MeleeState(c); state.action.sources = melee.action.sources;
        data.Find("action")->Set("sources", *CaptureCombatState(melee, c).Find("action")->Find("sources"));
    }
    SECTION("innate ability has no training art") {
        state.action.trainingArt = c.FindCombatArt("boxing"); data.Find("action")->SetString("trainingArt", "boxing");
    }
    SECTION("art source must actually grant this ability") {
        state.action.abilitySource = {AbilityGrantSourceKind::Art, c.FindCombatArt("boxing")};
        state.action.trainingArt = state.action.abilitySource.art;
        data.Find("action")->Find("abilitySource")->SetUnsigned("kind", static_cast<unsigned>(AbilityGrantSourceKind::Art));
        data.Find("action")->Find("abilitySource")->SetString("art", "boxing"); data.Find("action")->SetString("trainingArt", "boxing");
    }
    SECTION("melee training art must match committed style") {
        state = MeleeState(c); data = CaptureCombatState(state, c);
        state.action.trainingArt = c.FindCombatArt("boxing"); data.Find("action")->SetString("trainingArt", "boxing");
    }
    SECTION("incoming responses cannot become saved offensive windups") {
        state = MeleeState(c); data = CaptureCombatState(state, c);
        state.action.moveId = c.FindCombatMove("basic_parry"); data.Find("action")->SetString("moveId", "basic_parry");
    }
    SECTION("counter flag must match move trigger") {
        state = MeleeState(c); data = CaptureCombatState(state, c);
        state.action.counter = true; data.Find("action")->SetBoolean("counter", true);
    }
    CHECK_FALSE(ValidateCombatState(state, c)); const auto read = ReadCombatState(data, c);
    CHECK(read == eastl::unexpected(StateError::InvalidData));
}

TEST_CASE("Ability training follows grant art independently of the current committed style", "[combat][save]") {
    const auto& c = ExpandedContent(); auto state = AbilityState(c, "leap");
    state.action.committedStyle = c.FindCombatStyle("weapon_guard");
    state.action.abilitySource = {AbilityGrantSourceKind::Art, c.FindCombatArt("boxing")};
    state.action.trainingArt = state.action.abilitySource.art;
    REQUIRE(ValidateCombatState(state, c)); const auto read = ReadCombatState(CaptureCombatState(state, c), c);
    REQUIRE(read); CHECK(*read == state); // Grant loss while pending remains an execution-time refusal.
}

TEST_CASE("Saved counter commitments retain their matching trigger and stale opponent", "[combat][save]") {
    const auto& c = ExpandedContent(); auto state = MeleeState(c);
    state.action.kind = ActorActionKind::Recovery; state.action.moveId = c.FindCombatMove("basic_counter");
    state.action.counter = true;
    REQUIRE(ValidateCombatState(state, c)); const auto read = ReadCombatState(CaptureCombatState(state, c), c);
    REQUIRE(read); CHECK(read->action.counter); CHECK(read->action.target == UINT64_MAX);
}

TEST_CASE("Character persistence checks funded manual pairs and independent cooldown references", "[combat][save]") {
    const auto& c = ExpandedContent(); CharacterCombatState state;
    const auto boxing = c.FindCombatArt("boxing"), weapons = c.FindCombatArt("weapon_forms");
    state.arts.push_back({boxing, 70000});
    state.abilityGrants.push_back({c.FindAbility("focus"), {AbilityGrantSourceKind::Art, boxing}});
    // This cooldown survives removal of the weapon-art grant and learned art.
    state.abilityCooldowns.push_back({c.FindAbility("sweep"), {AbilityGrantSourceKind::Art, weapons}, 43});
    ManualAttemptProgress manual; manual.book = c.FindBook("boxing_manual_text"); manual.art = boxing;
    manual.totalDuration = 500; manual.remaining = 230; manual.fundedTicks = 30; manual.active = true;
    state.manuals.push_back(manual);
    const auto valid = CaptureCharacterCombat(&state, c); REQUIRE(ReadCharacterCombat(valid, c));
    auto data = valid;
    SECTION("unlearned art cannot own a saved grant") {
        auto grants = DataObject::List(); DataObject grant; grant.SetString("ability", "focus");
        DataObject source; source.SetUnsigned("kind", 0); source.SetString("art", "weapon_forms");
        grant.Set("source", eastl::move(source)); grants.Append(eastl::move(grant)); data.Set("grants", eastl::move(grants));
    }
    SECTION("cooldown source cannot refer to an art that never grants the ability") {
        auto cooldowns = DataObject::List(); auto cooldown = valid.Find("cooldowns")->At(0);
        cooldown.Find("source")->SetString("art", "boxing"); cooldowns.Append(eastl::move(cooldown)); data.Set("cooldowns", eastl::move(cooldowns));
    }
    SECTION("book and art must form an authored manual") {
        auto manuals = DataObject::List(); auto attempt = valid.Find("manuals")->At(0);
        attempt.SetString("art", "weapon_forms"); manuals.Append(eastl::move(attempt)); data.Set("manuals", eastl::move(manuals));
    }
    SECTION("funded slice cannot exceed the actual funding quantum") {
        auto manuals = DataObject::List(); auto attempt = valid.Find("manuals")->At(0);
        attempt.SetUnsigned("fundedTicks", 101); manuals.Append(eastl::move(attempt)); data.Set("manuals", eastl::move(manuals));
    }
    CHECK(ReadCharacterCombat(data, c) == eastl::unexpected(StateError::InvalidData));
}

TEST_CASE("Legacy recovery checkpoints are rejected before payload validation", "[combat][save][database]") {
    const auto& c = TestContent(); SaveFile file; auto chunk = Chunk(); auto snapshot = Snapshot();
    {
        SaveDatabase db(c); REQUIRE(db.Create(file.path.c_str())); REQUIRE(db.Begin());
        REQUIRE(db.WriteChunk(chunk)); REQUIRE(db.WriteSnapshot(snapshot)); REQUIRE(db.Commit());
    }
    auto old = MakeCombatState(true); old.stamina = 31; old.action.kind = ActorActionKind::Recovery;
    SECTION("paid recovery") {}
    SECTION("windup plus recovery overflows") {
        old.action = {ActorActionKind::MeleeWindup, 99, static_cast<unsigned>(CombatMove::HeavyAttack), UINT64_MAX};
    }
    {
        RawDatabase raw(file.path.c_str()); raw.LegacyCombat(CaptureCombatState(old));
        raw.Exec("UPDATE chunks SET payload=json_set(payload,'$.actors[0].remaining_ticks','114')");
    }
    SaveDatabase db(c);
    CHECK(db.Open(file.path.c_str()) == eastl::unexpected(SaveError::UnsupportedVersion));
}

TEST_CASE("A runtime committed Sweep survives actual world save and reload", "[combat][save][database][abilities]") {
    auto content = ExpandedContent(); const auto art = content.FindCombatArt("weapon_forms");
    // Remove acquisition gates in this owned fixture before constructing World;
    // leave ability, move, binding, and runtime commitment definitions intact.
    auto& definition = const_cast<CombatArtDefinition&>(content.GetCombatArt(art));
    for (auto& grant : definition.abilities) { grant.unlock = {}; }
    SaveFile file; PersistentId actorId = 0, itemId = 0;
    DataObject combat, character;
    {
        World world(content); REQUIRE(world.RequestChunks({{}, 0}, LoadMode::active)); REQUIRE(world.ReconcileChunks());
        REQUIRE(world.SetTile({3, 3}, {content.FindTerrain("floor"), 0}));
        const auto created = world.CreateActor({3, 3}, true); REQUIRE(created); const auto actor = *created;
        actorId = world.GetActor(actor)->id;
        const auto carried = world.CreateItem(content.FindItem("rock"), ItemLocation::Carried(actor)); REQUIRE(carried);
        itemId = world.GetItem(*carried)->id;
        REQUIRE(world.HoldItem(actor, *carried, Hand::Left, GripKind::OneHanded).outcome == InventoryOutcome::Succeeded);
        REQUIRE(world.Combat().GrantArt(actor, art, ArtGrantReason::Administration));
        const AbilityRequest request{content.FindAbility("sweep"), {AbilityGrantSourceKind::Art, art},
            {AbilityTargetKind::CardinalDirection, 0, {}, CardinalDirection::East}};
        std::uint32_t rng = 17; const auto result = world.Combat().UseAbility(actor, request, rng);
        REQUIRE(result.committed); REQUIRE(result.pending); world.ElapseTicks(13);
        const auto& state = world.GetActor(actor)->combat;
        REQUIRE(ValidateCombatState(state, content));
        CHECK(state.action.moveId == content.GetAbility(request.ability).sweepMove);
        CHECK(state.action.trainingArt == art); CHECK(state.action.sources.hands[0].item == itemId);
        CHECK(state.action.sources.hands[0].required); CHECK(state.action.weapon == 0);
        combat = CaptureCombatState(state, content);
        character = CaptureCharacterCombat(world.GetActor(actor)->characterCombat.get(), content);
        SessionState session; session.player = actorId; session.playerPosition = {3, 3};
        REQUIRE(world.CreateSave(file.path.c_str(), session));
    }
    World restored(content); REQUIRE(restored.LoadSave(file.path.c_str()));
    REQUIRE(restored.RequestChunks({{}, 0}, LoadMode::active)); REQUIRE(restored.ReconcileChunks());
    const auto actor = restored.FindActor(actorId); REQUIRE(actor.value);
    CHECK(CaptureCombatState(restored.GetActor(actor)->combat, content) == combat);
    CHECK(CaptureCharacterCombat(restored.GetActor(actor)->characterCombat.get(), content) == character);
    CHECK(restored.GetActor(actor)->combat.action.kind == ActorActionKind::AbilityWindup);
    REQUIRE(restored.GetItem(restored.FindItem(itemId)));
}
}
