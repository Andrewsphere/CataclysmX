#include "actors/ai_controller.h"
#include "combat/combat_system.h"
#include "content/content.h"
#include "core/save_database.h"
#include "core/world.h"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <EASTL/utility.h>
#include <sqlite3.h>

#include <chrono>
#include <filesystem>
#include <string>

namespace {
using namespace cataclysmx;

template<class T, class E>
T Take(eastl::expected<T, E> result) {
    REQUIRE(result);
    return eastl::move(*result);
}

ContentCatalog IdentityContent(bool extended = false) {
    const ContentSource sources[] = {
        {"identity", R"({format_version:1,ai_profiles:[{id:'vision'}],definitions:[
          {type:'terrain',id:'floor',name:'Floor',movement:{passable:true},blocks_sight:false,appearance:{symbol:'.',foreground:'white'}},
          {type:'terrain',id:'wall',name:'Wall',movement:{passable:false},blocks_sight:true,appearance:{symbol:'#',foreground:'white'}},
          {type:'terrain',id:'air',name:'Air',movement:{passable:true},open_space:true,blocks_sight:false,looks_like:'floor'},
          {type:'item',id:'keepsake',name:'Keepsake',mass_g:1,volume_ml:1},
          {type:'monster',id:'walker',name:'Walker',ai_profile:'vision',body:'simple',speed:125,attack_damage:17,accuracy:8700}
        ]})"},
        {"earlier", R"({format_version:1,definitions:[
          {type:'monster',id:'a_monster',name:'Earlier monster',ai_profile:'vision'}
        ]})"}
    };
    Diagnostics diagnostics;
    auto result = LoadContent({sources, extended ? 2U : 1U}, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str());
    return Take(eastl::move(result));
}

struct SaveFile {
    std::filesystem::path directory;
    std::string path;
    SaveFile() {
        std::error_code error;
        const auto parent = std::filesystem::temp_directory_path(error);
        REQUIRE_FALSE(error);
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (unsigned attempt = 0; ; ++attempt) {
            directory = parent / ("cataclysmx-monster-identity-" + std::to_string(stamp)
                + "-" + std::to_string(attempt));
            const bool created = std::filesystem::create_directory(directory, error);
            REQUIRE_FALSE(error);
            if (created) { break; }
        }
        path = (directory / "world.sqlite").string();
    }
    ~SaveFile() {
        std::error_code error;
        std::filesystem::remove_all(directory, error);
    }
};

struct RawDatabase {
    sqlite3* db = nullptr;
    explicit RawDatabase(const char* path) {
        REQUIRE(sqlite3_open_v2(path, &db, SQLITE_OPEN_READWRITE, nullptr) == SQLITE_OK);
    }
    ~RawDatabase() { sqlite3_close(db); }
    void Exec(const char* sql) {
        REQUIRE(sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK);
    }
    std::string Text(const char* sql) {
        sqlite3_stmt* statement = nullptr;
        REQUIRE(sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) == SQLITE_OK);
        REQUIRE(sqlite3_step(statement) == SQLITE_ROW);
        const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(statement, 0));
        REQUIRE(text);
        std::string result(text, static_cast<std::size_t>(sqlite3_column_bytes(statement, 0)));
        REQUIRE(sqlite3_finalize(statement) == SQLITE_OK);
        return result;
    }
};

LoadRequest Load(World& world) {
    const auto request = Take(world.RequestChunks({{0, 0}, 0}, LoadMode::active));
    REQUIRE(world.ReconcileChunks());
    return request;
}

ActorHandle Spawn(World& world) {
    REQUIRE(world.SetTile({2, 2}, {world.GetContent().FindTerrain("floor"), 0}));
    return Take(world.CreateMonster(world.GetContent().FindMonster("walker"), {2, 2}));
}

void Populate(const ContentCatalog& content, const char* path) {
    World world(content);
    Load(world);
    Spawn(world);
    REQUIRE(world.CreateSave(path, SessionState{}));
}

TEST_CASE("Monster identity is assigned only to valid monster spawns", "[monster][identity][world]") {
    const auto content = IdentityContent();
    World world(content);
    Load(world);
    const auto monster = Spawn(world);
    CHECK(world.GetActor(monster)->monsterType == content.FindMonster("walker"));
    CHECK(world.GetActor(monster)->speed == 125);
    REQUIRE(world.SetTile({3, 2}, {content.FindTerrain("floor"), 0}));
    REQUIRE(world.SetTile({4, 2}, {content.FindTerrain("floor"), 0}));
    const auto ordinary = Take(world.CreateActor({3, 2}));
    const auto player = Take(world.CreateActor({4, 2}, true));
    CHECK(world.GetActor(ordinary)->monsterType == MonsterTypeId{});
    CHECK(world.GetActor(player)->monsterType == MonsterTypeId{});
    CHECK_FALSE(world.CreateMonster({}, {5, 2}));
    CHECK_FALSE(world.CreateMonster(MonsterTypeId{UINT32_MAX}, {5, 2}));
    CHECK_FALSE(world.CreateMonster(content.FindMonster("walker"), {3, 2}));
    CHECK(world.GetActor(ordinary)->monsterType == MonsterTypeId{});
    CHECK(world.GetActor(player)->monsterType == MonsterTypeId{});
    CHECK(world.CheckInvariants());
}

TEST_CASE("Monster archive and disk reload preserve identity and changed state across catalog numbering", "[monster][identity][save]") {
    SaveFile file;
    const auto content = IdentityContent();
    const auto reordered = IdentityContent(true);
    REQUIRE(content.FindMonster("walker") != reordered.FindMonster("walker"));
    PersistentId id = 0;
    PersistentId itemId = 0;
    DataObject combat;
    DataObject controller;
    Tick delay = 0;
    {
        World world(content);
        auto request = Load(world);
        const auto monster = Spawn(world);
        id = world.GetActor(monster)->id;
        auto injured = world.GetActor(monster)->combat;
        const auto injury = ApplyInjury(injured.body, BodyPart::Locomotion, 23, injured.injury);
        REQUIRE_FALSE(injury.defeated);
        REQUIRE(world.Combat().Configure(monster, injured));
        REQUIRE(world.SetActorSpeed(monster, 73));
        // A replacement controller must survive too; identity must not reconstruct vision.
        REQUIRE(world.SetActorController(monster, AiControllerKind::Melee));
        REQUIRE(world.ScheduleActorAction(monster));
        const auto item = Take(world.CreateItem(content.FindItem("keepsake"), ItemLocation::Carried(monster)));
        itemId = world.GetItem(item)->id;
        combat = CaptureCombatState(world.GetActor(monster)->combat, content);
        controller = world.CaptureActorController(monster);
        delay = world.GetActorRemainingTicks(monster);
        REQUIRE(delay > 0);
        REQUIRE(world.ReleaseRequest(request));
        REQUIRE(world.ReconcileChunks());
        CHECK_FALSE(world.GetActor(monster));
        CHECK(world.ArchivedChunkCount() > 0);
        CHECK(world.CheckInvariants());
        request = Load(world);
        auto restored = world.FindActor(id);
        REQUIRE(world.GetActor(restored));
        CHECK(world.GetActor(restored)->monsterType == content.FindMonster("walker"));
        CHECK(world.GetActor(restored)->speed == 73);
        CHECK(CaptureCombatState(world.GetActor(restored)->combat, content) == combat);
        CHECK(world.CaptureActorController(restored) == controller);
        CHECK(world.GetActorRemainingTicks(restored) == delay);
        REQUIRE(world.GetItem(world.FindItem(itemId)));
        CHECK(world.GetItem(world.FindItem(itemId))->location == ItemLocation::Carried(restored));
        REQUIRE(world.ReleaseRequest(request));
        REQUIRE(world.ReconcileChunks());
        REQUIRE(world.CreateSave(file.path.c_str(), SessionState{}));
        // Actor archives are excluded from the clean warm cache; reload must use disk.
        CHECK(world.ArchivedChunkCount() == 0);
        Load(world);
        restored = world.FindActor(id);
        REQUIRE(world.GetActor(restored));
        CHECK(world.GetActor(restored)->monsterType == content.FindMonster("walker"));
        CHECK(CaptureCombatState(world.GetActor(restored)->combat, content) == combat);
        CHECK(world.CheckInvariants());
    }
    {
        RawDatabase raw(file.path.c_str());
        CHECK(raw.Text("SELECT format_version FROM metadata") == std::to_string(save_format_version));
        CHECK(raw.Text("SELECT json_extract(payload,'$.actors[0].monster_type') FROM chunks WHERE x=0 AND z=0 AND y=0") == "walker");
    }
    {
        SaveDatabase database(reordered);
        const auto snapshot = Take(database.Open(file.path.c_str()));
        const auto chunk = Take(database.ReadChunk({0, 0, 0}, snapshot.nextId));
        REQUIRE(chunk);
        REQUIRE(chunk->actors.size() == 1);
        CHECK(chunk->actors[0].monsterType == "walker");
        CHECK(chunk->actors[0].combat == combat);
        CHECK(chunk->actors[0].controller == controller);
        CHECK(chunk->actors[0].speed == 73);
    }
    World restored(reordered);
    REQUIRE(restored.LoadSave(file.path.c_str()));
    Load(restored);
    const auto monster = restored.FindActor(id);
    REQUIRE(restored.GetActor(monster));
    CHECK(restored.GetActor(monster)->monsterType == reordered.FindMonster("walker"));
    CHECK(restored.GetActor(monster)->speed == 73);
    CHECK(CaptureCombatState(restored.GetActor(monster)->combat, reordered) == combat);
    CHECK(restored.CaptureActorController(monster) == controller);
    CHECK(restored.GetActorRemainingTicks(monster) == delay);
    REQUIRE(restored.GetItem(restored.FindItem(itemId)));
    CHECK(restored.GetItem(restored.FindItem(itemId))->location == ItemLocation::Carried(monster));
    CHECK(restored.CheckInvariants());
}

TEST_CASE("Optional v10 monster identity accepts absent or empty fields and omits untyped output", "[monster][identity][save]") {
    const auto content = IdentityContent();
    SaveFile file;
    Populate(content, file.path.c_str());
    {
        RawDatabase raw(file.path.c_str());
        SECTION("old v10 payload") {
            raw.Exec("UPDATE chunks SET payload=json_remove(payload,'$.actors[0].monster_type')");
        }
        SECTION("explicit empty string") {
            raw.Exec("UPDATE chunks SET payload=json_set(payload,'$.actors[0].monster_type','') WHERE y=0");
        }
    }
    World world(content);
    REQUIRE(world.LoadSave(file.path.c_str()));
    Load(world);
    const auto actor = world.ActorAt({2, 2});
    REQUIRE(world.GetActor(actor));
    CHECK(world.GetActor(actor)->monsterType == MonsterTypeId{});
    REQUIRE(world.SetActorSpeed(actor, 74));
    REQUIRE(world.Save(SessionState{}));
    RawDatabase raw(file.path.c_str());
    CHECK(raw.Text("SELECT payload FROM chunks WHERE x=0 AND z=0 AND y=0").find("monster_type") == std::string::npos);
}

TEST_CASE("Malformed external monster identity fails before World publication", "[monster][identity][save]") {
    const auto content = IdentityContent();
    SaveFile file;
    Populate(content, file.path.c_str());
    {
        RawDatabase raw(file.path.c_str());
        SECTION("unknown") { raw.Exec("UPDATE chunks SET payload=json_set(payload,'$.actors[0].monster_type','missing') WHERE y=0"); }
        SECTION("numeric") { raw.Exec("UPDATE chunks SET payload=json_set(payload,'$.actors[0].monster_type',1) WHERE y=0"); }
        SECTION("null") { raw.Exec("UPDATE chunks SET payload=json_set(payload,'$.actors[0].monster_type',NULL) WHERE y=0"); }
        SECTION("boolean") { raw.Exec("UPDATE chunks SET payload=json_set(payload,'$.actors[0].monster_type',json('true')) WHERE y=0"); }
        SECTION("object") { raw.Exec("UPDATE chunks SET payload=json_set(payload,'$.actors[0].monster_type',json('{}')) WHERE y=0"); }
        SECTION("array") { raw.Exec("UPDATE chunks SET payload=json_set(payload,'$.actors[0].monster_type',json('[]')) WHERE y=0"); }
        SECTION("embedded NUL") { raw.Exec("UPDATE chunks SET payload=json_set(payload,'$.actors[0].monster_type','walker'||char(0)) WHERE y=0"); }
        SECTION("typed player") {
            raw.Exec("UPDATE chunks SET payload=json_set(payload,'$.actors[0].player',json('true'),'$.actors[0].controller',json('{\"type\":\"none\",\"state\":{}}')) WHERE y=0");
        }
        SECTION("duplicate") {
            raw.Exec("UPDATE chunks SET payload=replace(payload,'\"monster_type\":\"walker\"','\"monster_type\":\"walker\",\"monster_type\":\"walker\"') WHERE y=0");
        }
    }
    World world(content, 987);
    const auto revision = world.GetRevision();
    CHECK(world.LoadSave(file.path.c_str()) == eastl::unexpected(SaveError::InvalidData));
    CHECK_FALSE(world.HasSave());
    CHECK(world.GetSeed() == 987);
    CHECK(world.GetRevision() == revision);
    CHECK(world.LoadedChunks().empty());
    CHECK(world.CheckInvariants());
}

TEST_CASE("RAM archive writes reject unknown identity and typed players", "[monster][identity][save]") {
    const auto content = IdentityContent();
    SaveFile file;
    SaveDatabase database(content);
    REQUIRE(database.Create(file.path.c_str()));
    ChunkArchive chunk{};
    chunk.tiles.fill({content.FindTerrain("floor"), 0});
    chunk.actors.push_back({1, {2, 2}, false, 0});
    SECTION("unknown") { chunk.actors[0].monsterType = "missing"; }
    SECTION("embedded NUL") { chunk.actors[0].monsterType.assign("walker\0", 7); }
    SECTION("typed player") {
        chunk.actors[0].player = true;
        chunk.actors[0].monsterType = "walker";
    }
    REQUIRE(database.Begin());
    CHECK(database.WriteChunk(chunk) == eastl::unexpected(SaveError::InvalidData));
}

TEST_CASE("Invalid monster identity during disk materialization leaves demand uncommitted", "[monster][identity][save]") {
    const auto content = IdentityContent();
    SaveFile file;
    Populate(content, file.path.c_str());
    World world(content);
    REQUIRE(world.LoadSave(file.path.c_str()));
    REQUIRE(world.RequestChunks({{0, 0}, 0}, LoadMode::active));
    const auto revision = world.GetRevision();
    {
        RawDatabase raw(file.path.c_str());
        raw.Exec("UPDATE chunks SET payload=json_set(payload,'$.actors[0].monster_type','missing') WHERE y=0");
    }
    CHECK(world.ReconcileChunks() == eastl::unexpected(SaveError::InvalidData));
    CHECK(world.LoadedChunks().empty());
    CHECK(world.GetRevision() == revision);
    CHECK(world.CheckInvariants());
    {
        RawDatabase raw(file.path.c_str());
        raw.Exec("UPDATE chunks SET payload=json_set(payload,'$.actors[0].monster_type','walker') WHERE y=0");
    }
    REQUIRE(world.ReconcileChunks());
    const auto actor = world.ActorAt({2, 2});
    REQUIRE(world.GetActor(actor));
    CHECK(world.GetActor(actor)->monsterType == content.FindMonster("walker"));
    CHECK(world.CheckInvariants());
}

TEST_CASE("Legacy v7 through v9 saves are rejected before world publication", "[monster][identity][save]") {
    const auto content = IdentityContent();
    for (int version : {7, 8, 9}) {
        CAPTURE(version);
        SaveFile file;
        Populate(content, file.path.c_str());
        {
            RawDatabase raw(file.path.c_str());
            raw.Exec("UPDATE world_state SET payload=json_remove(payload,'$.power')");
            raw.Exec("UPDATE chunks SET payload=json_set(payload,'$.actors',json((SELECT json_group_array(json_remove(value,'$.combat','$.monster_type','$.wielded_item','$.learning','$.equipment','$.character_combat')) FROM json_each(chunks.payload,'$.actors'))))");
            raw.Exec("UPDATE world_state SET payload=json_set(payload,'$.session.players',json((SELECT json_group_array("
                "json_remove(value,'$.session')) FROM json_each(world_state.payload,'$.session.players'))))");
            if (version < 9) {
                raw.Exec("UPDATE chunks SET payload=json_set(payload,'$.actors',json((SELECT json_group_array(json_remove(value,'$.controller')) FROM json_each(chunks.payload,'$.actors'))))");
            }
            if (version == 7) { raw.Exec("UPDATE world_state SET payload=json_remove(payload,'$.session.circle_residency')"); }
            raw.Exec(("UPDATE metadata SET format_version=" + std::to_string(version)).c_str());
        }
        World rejected(content);
        CHECK(rejected.LoadSave(file.path.c_str()) == eastl::unexpected(SaveError::UnsupportedVersion));
        CHECK_FALSE(rejected.HasSave());
        CHECK(rejected.LoadedChunks().empty());
    }
}
}
