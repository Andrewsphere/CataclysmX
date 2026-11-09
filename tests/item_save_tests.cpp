#include "core/save_database.h"
#include "core/item_state.h"
#include "content/content.h"
#include "items/item_snapshot.h"
#include <catch2/catch_test_macros.hpp>
#include <sqlite3.h>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>

namespace {
using namespace cataclysmx;
struct ItemSaveFile {
    std::filesystem::path directory;
    std::string path;
    ItemSaveFile() {
        static std::atomic<unsigned> sequence{0};
        directory = std::filesystem::temp_directory_path() / ("cataclysmx-item-save-"
            + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
            + "-" + std::to_string(sequence.fetch_add(1)));
        REQUIRE(std::filesystem::create_directory(directory));
        path = (directory / "world.sqlite").string();
    }
    ~ItemSaveFile() { std::error_code error; std::filesystem::remove_all(directory, error); }
};

ContentCatalog Catalog(bool expanded = false) {
    String text = R"({format_version:1,definitions:[
        {type:'terrain',id:'floor',name:'floor',movement:{passable:true},blocks_sight:false,
          appearance:{symbol:'.',foreground:'white'}},
        {type:'item',id:'battery',name:'battery',mass_g:20,volume_ml:10,capabilities:[
          {type:'energy_store',id:'cell',capacity_energy:100,initial_energy:17,rechargeable:true,battery_class:'aa'}]},
        {type:'item',id:'device',name:'device',mass_g:100,volume_ml:50,capabilities:[)";
    if (expanded) { text += "{type:'container',id:'aaa',capacity_volume_ml:1000},"; }
    // Intentionally reverse source order: catalog order is lexical.
    if (expanded) {
        text += R"(
          {type:'charger',id:'charger',power_source:'power',battery_class:'aa',energy_per_turn:3},
          {type:'energy_store',id:'power',capacity_energy:1000,initial_energy:800,rechargeable:false},
          {type:'door_controller',id:'timer',delay_ticks:300}]},)";
    } else {
        text += R"(
          {type:'door_controller',id:'timer',delay_ticks:300},
          {type:'energy_store',id:'power',capacity_energy:1000,initial_energy:800,rechargeable:false},
          {type:'charger',id:'charger',power_source:'power',battery_class:'aa',energy_per_turn:3}]},)";
    }
    text += R"(
        {type:'item',id:'bag',name:'bag',mass_g:5,volume_ml:10,capabilities:[
          {type:'container',id:'storage',capacity_volume_ml:1000}]}
    )";
    if (expanded) { text += ",{type:'item',id:'aardvark',name:'extra',mass_g:0,volume_ml:1}"; }
    text += "]}";
    ContentSource source{"item-save.json5", text};
    Diagnostics diagnostics;
    auto result = LoadContent({&source, 1}, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str());
    REQUIRE(result);
    return eastl::move(*result);
}

SavedItem Item(const ContentCatalog& content, const char* type, PersistentId id,
    LocationKind location, PersistentId owner = 0) {
    SavedItem item{};
    item.id = id; item.type = content.FindItem(type); item.location = location; item.owner = owner;
    const auto& definition = content.GetItem(item.type);
    WriteItemState(definition, MakeItemState(definition), item.state);
    return item;
}
ChunkArchive Sample(const ContentCatalog& content) {
    ChunkArchive chunk{};
    chunk.tiles.fill({content.FindTerrain("floor"), 0});
    SavedActor actor{1, {}, true, 0}; actor.carryCapacityGrams = UINT64_MAX;
    chunk.actors.push_back(actor);
    chunk.items.push_back(Item(content, "bag", 2, LocationKind::actor, 1));
    chunk.items.push_back(Item(content, "device", 3, LocationKind::ground));
    const auto& definition = content.GetItem(chunk.items[1].type);
    auto* charger = chunk.items[1].state.Find("charger");
    charger->SetBoolean("enabled", true); charger->SetUnsigned("remainder", 87);
    auto* timer = chunk.items[1].state.Find("timer");
    timer->SetBoolean("armed", true); timer->SetUnsigned("remaining_ticks", 123);
    timer->Find("target")->SetSigned("x", 1);
    chunk.items.push_back(Item(content, "battery", 4, LocationKind::item, 3));
    chunk.items.back().installation = FindItemCapability(definition, "charger");
    return chunk;
}
void Populate(const ContentCatalog& content, const char* path) {
    SaveDatabase database(content);
    REQUIRE(database.Create(path)); REQUIRE(database.Begin());
    REQUIRE(database.WriteChunk(Sample(content)));
    SaveSnapshot snapshot; snapshot.nextId = 10;
    REQUIRE(database.WriteSnapshot(snapshot)); REQUIRE(database.Commit());
}
void WritePayload(const char* path, const std::string& text) {
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(path, &db, SQLITE_OPEN_READWRITE, nullptr) == SQLITE_OK);
    sqlite3_stmt* statement = nullptr;
    REQUIRE(sqlite3_prepare_v2(db, "UPDATE chunks SET payload=?1", -1, &statement, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_bind_text(statement, 1, text.data(), static_cast<int>(text.size()), SQLITE_TRANSIENT) == SQLITE_OK);
    REQUIRE(sqlite3_step(statement) == SQLITE_DONE);
    REQUIRE(sqlite3_finalize(statement) == SQLITE_OK); REQUIRE(sqlite3_close(db) == SQLITE_OK);
}
void Mutate(const char* path, const char* before, const char* after) {
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(path, &db, SQLITE_OPEN_READWRITE, nullptr) == SQLITE_OK);
    sqlite3_stmt* statement = nullptr;
    REQUIRE(sqlite3_prepare_v2(db, "SELECT payload FROM chunks", -1, &statement, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_step(statement) == SQLITE_ROW);
    std::string text(reinterpret_cast<const char*>(sqlite3_column_text(statement, 0)));
    REQUIRE(sqlite3_finalize(statement) == SQLITE_OK);
    const auto position = text.find(before); REQUIRE(position != std::string::npos);
    text.replace(position, std::char_traits<char>::length(before), after);
    REQUIRE(sqlite3_close(db) == SQLITE_OK);
    WritePayload(path, text);
}

TEST_CASE("Pre-refactor V5 item payload loads without rewriting its state", "[save][item-save]") {
    const auto original = Catalog();
    const auto expanded = Catalog(true);
    ItemSaveFile file;
    Populate(original, file.path.c_str()); // Only provision the SQL schema and world metadata.
    // Frozen pre-refactor Sample payload. No current item writer or value codec
    // constructs these fields: signed targets use the original $i64 tags, while
    // versions, energy and timer/fraction values are unsigned JSON numbers.
    std::string payload = R"({"tiles":[)";
    for (unsigned i = 0; i < 256; ++i) {
        if (i) { payload += ','; }
        payload += R"({"terrain":"floor","damage":0})";
    }
    payload += R"(],"actors":[{"id":"1","position":{"x":0,"z":0},"player":true,"parked":false,"turns":"0","speed":100,"remaining_ticks":"0","carry_capacity_g":"18446744073709551615","inventory":[{"id":"2","type":"bag","state":{},"installation":"","contents":[]}]}],"objects":[],"ground_items":[{"id":"3","type":"device","state":{"charger":{"version":1,"enabled":true,"remainder":87},"power":{"version":1,"energy":800},"timer":{"version":1,"armed":true,"remaining_ticks":123,"target":{"x":{"$i64":"1"},"z":{"$i64":"0"}}}},"installation":"","contents":[{"id":"4","type":"battery","state":{"cell":{"version":1,"energy":17}},"installation":"charger","contents":[]}],"position":{"x":0,"z":0}}]})";
    WritePayload(file.path.c_str(), payload);
    SaveDatabase database(expanded);
    auto opened = database.Open(file.path.c_str()); REQUIRE(opened);
    auto loaded = database.ReadChunk({}, opened->nextId); REQUIRE(loaded); REQUIRE(*loaded);
    const auto expected = Sample(expanded);
    REQUIRE((**loaded).items.size() == expected.items.size());
    for (std::size_t i = 0; i < expected.items.size(); ++i) {
        const auto& actual = (**loaded).items[i];
        CHECK(actual.id == expected.items[i].id);
        CHECK(actual.type == expected.items[i].type);
        CHECK(actual.owner == expected.items[i].owner);
        CHECK(actual.location == expected.items[i].location);
        CHECK(actual.installation == expected.items[i].installation);
        REQUIRE(actual.state.Size() == expected.items[i].state.Size());
        for (std::size_t j = 0; j < expected.items[i].state.Size(); ++j) {
            const auto* state = actual.state.Find(expected.items[i].state.Name(j));
            REQUIRE(state);
            CHECK(*state == expected.items[i].state.At(j));
        }
    }
    CHECK((**loaded).actors[0].carryCapacityGrams == UINT64_MAX);
}

TEST_CASE("Item saves retain charge fractions timers capacity and stable catalog identities", "[save][item-save]") {
    const auto original = Catalog(); const auto expanded = Catalog(true);
    REQUIRE(original.FindItem("device") != expanded.FindItem("device"));
    REQUIRE(FindItemCapability(original.GetItem(original.FindItem("device")), "charger")
        != FindItemCapability(expanded.GetItem(expanded.FindItem("device")), "charger"));
    ItemSaveFile file; Populate(original, file.path.c_str());
    SaveDatabase database(expanded);
    const auto opened = database.Open(file.path.c_str()); REQUIRE(opened);
    const auto loaded = database.ReadChunk({}, opened->nextId); REQUIRE(loaded); REQUIRE(*loaded);
    const auto& chunk = **loaded;
    REQUIRE(chunk.items.size() == 3);
    CHECK(chunk.actors[0].carryCapacityGrams == UINT64_MAX);
    const auto& device = chunk.items[1];
    CHECK(device.type == expanded.FindItem("device"));
    const auto& definition = expanded.GetItem(device.type);
    const auto charger = FindItemCapability(definition, "charger");
    CHECK(device.state.Find("charger")->GetBoolean("enabled") == true);
    CHECK(device.state.Find("charger")->GetUnsigned("remainder") == 87);
    const auto* timer = device.state.Find("timer");
    CHECK(timer->GetBoolean("armed") == true);
    CHECK(timer->GetUnsigned("remaining_ticks") == 123);
    CHECK(timer->Find("target")->GetSigned("x") == 1);
    CHECK(timer->Find("target")->GetSigned("z") == 0);
    CHECK(chunk.items[2].installation == charger);
    CHECK(chunk.items[2].owner == device.id);
    REQUIRE(database.Begin()); REQUIRE(database.WriteChunk(chunk)); REQUIRE(database.Commit());
}

TEST_CASE("Malformed saved item state type and installation produce contextual errors", "[save][item-save]") {
    const auto content = Catalog(); ItemSaveFile file; Populate(content, file.path.c_str());
    SaveError expected = SaveError::InvalidData;
    SECTION("unknown type") { Mutate(file.path.c_str(), "\"type\":\"battery\"", "\"type\":\"missing\""); }
    SECTION("missing state") { Mutate(file.path.c_str(), "\"state\":{}", "\"state\":null"); }
    SECTION("missing capability") { Mutate(file.path.c_str(), "\"cell\":{\"version\":1,\"energy\":17}", "\"other\":{\"version\":1,\"energy\":17}"); }
    SECTION("absent saved state does not initialize") { Mutate(file.path.c_str(), "\"cell\":{\"version\":1,\"energy\":17}", ""); }
    SECTION("wrong energy type") { Mutate(file.path.c_str(), "\"energy\":17", "\"energy\":\"17\""); }
    SECTION("energy above current capacity") { Mutate(file.path.c_str(), "\"energy\":17", "\"energy\":101"); }
    SECTION("unsupported state version") { Mutate(file.path.c_str(), "\"cell\":{\"version\":1", "\"cell\":{\"version\":2"); expected = SaveError::UnsupportedVersion; }
    SECTION("unsupported behavior version") { Mutate(file.path.c_str(), "\"charger\":{\"version\":1", "\"charger\":{\"version\":2"); expected = SaveError::UnsupportedVersion; }
    SECTION("unsigned target is not signed state") { Mutate(file.path.c_str(), "\"x\":{\"$i64\":\"1\"}", "\"x\":1"); }
    SECTION("extra state entry") { Mutate(file.path.c_str(), "\"state\":{}", "\"state\":{\"storage\":{\"version\":1}}"); }
    SECTION("duplicate nested field") { Mutate(file.path.c_str(), "\"energy\":17", "\"energy\":17,\"energy\":18"); }
    SECTION("fraction out of range") { Mutate(file.path.c_str(), "\"remainder\":87", "\"remainder\":100"); }
    SECTION("disabled fraction nonzero") { Mutate(file.path.c_str(), "\"enabled\":true", "\"enabled\":false"); }
    SECTION("unknown parent capability") { Mutate(file.path.c_str(), "\"installation\":\"charger\"", "\"installation\":\"missing\""); }
    SECTION("parent noncharger capability") { Mutate(file.path.c_str(), "\"installation\":\"charger\"", "\"installation\":\"power\""); }
    SECTION("root installation") { Mutate(file.path.c_str(), "\"installation\":\"\"", "\"installation\":\"charger\""); }
    SECTION("actor capacity requires decimal string") { Mutate(file.path.c_str(), "\"carry_capacity_g\":\"18446744073709551615\"", "\"carry_capacity_g\":18446744073709551615"); }
    SECTION("actor capacity exceeded") { Mutate(file.path.c_str(), "\"carry_capacity_g\":\"18446744073709551615\"", "\"carry_capacity_g\":\"0\""); }
    Diagnostics diagnostics; SaveDatabase database(content);
    const auto opened = database.Open(file.path.c_str(), &diagnostics);
    REQUIRE_FALSE(opened); CHECK(opened.error() == expected);
    CHECK_FALSE(diagnostics.Empty());
    CHECK(FormatDiagnostics(diagnostics).find("chunk") != String::npos);
    CHECK(database.Begin() == eastl::unexpected(SaveError::NotOpen));
}

TEST_CASE("Item save validation rejects invalid ownership and rolls back the checkpoint", "[save][item-save]") {
    const auto content = Catalog(); ItemSaveFile file; Populate(content, file.path.c_str());
    SaveDatabase database(content); REQUIRE(database.Open(file.path.c_str()));
    auto chunk = Sample(content);
    SECTION("duplicate mount") { auto battery = chunk.items.back(); battery.id = 5; chunk.items.push_back(battery); }
    SECTION("ordinary contents require container") { chunk.items.back().installation = {}; }
    SECTION("battery must be rechargeable compatible passive") { chunk.items.back().type = content.FindItem("bag"); chunk.items.back().state = DataObject{}; }
    SECTION("armed controller in inventory") { chunk.items[1].location = LocationKind::actor; chunk.items[1].owner = 1; }
    SECTION("armed target must be cardinal") {
        chunk.items[1].state.Find("timer")->Find("target")->SetSigned("z", 1);
    }
    SECTION("enabled charger cannot have full battery") { chunk.items.back().state.Find("cell")->SetUnsigned("energy", 100); }
    SECTION("enabled charger needs source") {
        chunk.items[1].state.Find("power")->SetUnsigned("energy", 0);
    }
    SECTION("ordinary direct child volume exceeds container") {
        chunk.items.back().installation = {}; chunk.items.back().owner = 2;
        chunk.items[1].state.Find("charger")->SetBoolean("enabled", false);
        chunk.items[1].state.Find("charger")->SetUnsigned("remainder", 0);
        for (PersistentId id = 10; id < 110; ++id) {
            chunk.items.push_back(Item(content, "battery", id, LocationKind::item, 2));
        }
    }
    SECTION("descendant mass counts against actor capacity") {
        chunk.actors[0].carryCapacityGrams = 24;
        chunk.items.back().installation = {}; chunk.items.back().owner = 2;
        chunk.items[1].state.Find("charger")->SetBoolean("enabled", false);
        chunk.items[1].state.Find("charger")->SetUnsigned("remainder", 0);
    }
    REQUIRE(database.Begin());
    Diagnostics diagnostics;
    CHECK(database.WriteChunk(chunk, &diagnostics) == eastl::unexpected(SaveError::InvalidData));
    CHECK(database.Commit() == eastl::unexpected(SaveError::InvalidState));
    auto original = database.ReadChunk({}, 10); REQUIRE(original); REQUIRE(*original);
    CHECK((**original).items.size() == 3);
}

TEST_CASE("Shared item snapshot validation rejects noncanonical archives defensively", "[save][item-save]") {
    const auto content = Catalog();
    auto chunk = Sample(content);
    SECTION("invalid child type") { chunk.items.back().type = {}; }
    SECTION("missing parent") { chunk.items.back().owner = 999; }
    SECTION("self ownership cycle") { chunk.items.back().owner = chunk.items.back().id; }
    SECTION("two item ownership cycle") {
        chunk.items[1].location = LocationKind::item;
        chunk.items[1].owner = chunk.items.back().id;
    }
    SECTION("parent follows child") { eastl::swap(chunk.items[1], chunk.items[2]); }
    SECTION("invalid location kind") { chunk.items.back().location = static_cast<LocationKind>(255); }
    SECTION("extra state even on stateless container") { chunk.items[0].state.SetUnsigned("storage", 1); }
    SECTION("wrong behavior payload type") { chunk.items[1].state.SetUnsigned("charger", 1); }
    SECTION("shared energy exceeds capacity") { chunk.items.back().state.Find("cell")->SetUnsigned("energy", 101); }
    Diagnostics diagnostics;
    CHECK(ValidateItemSnapshot(content, chunk, &diagnostics) == eastl::unexpected(StateError::InvalidData));
    CHECK_FALSE(diagnostics.Empty());
}

TEST_CASE("Shared snapshot state errors preserve unsupported versions", "[save][item-save]") {
    const auto content = Catalog();
    auto chunk = Sample(content);
    SECTION("common energy") { chunk.items.back().state.Find("cell")->SetUnsigned("version", 2); }
    SECTION("behavior factory") { chunk.items[1].state.Find("timer")->SetUnsigned("version", 2); }
    CHECK(ValidateItemSnapshot(content, chunk) == eastl::unexpected(StateError::UnsupportedVersion));
    ItemSaveFile file;
    SaveDatabase database(content);
    REQUIRE(database.Create(file.path.c_str()));
    REQUIRE(database.Begin());
    CHECK(database.WriteChunk(chunk) == eastl::unexpected(SaveError::UnsupportedVersion));
    CHECK(database.Commit() == eastl::unexpected(SaveError::InvalidState));
}

TEST_CASE("Shared item snapshot depth includes the root item", "[save][item-save]") {
    const auto content = Catalog();
    ChunkArchive chunk{};
    chunk.items.push_back(Item(content, "bag", 1, LocationKind::ground));
    for (PersistentId id = 2; id <= 64; ++id) {
        chunk.items.push_back(Item(content, "bag", id, LocationKind::item, id - 1));
    }
    REQUIRE(ValidateItemSnapshot(content, chunk));
    chunk.items.push_back(Item(content, "bag", 65, LocationKind::item, 64));
    CHECK(ValidateItemSnapshot(content, chunk) == eastl::unexpected(StateError::InvalidData));
}
}
