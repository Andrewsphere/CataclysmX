#include "core/save_database.h"
#include "crafting/crafting_state.h"
#include "objects/object_behavior.h"

#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>

namespace {
using namespace cataclysmx;

struct HouseSaveFile {
    std::filesystem::path directory;
    std::string path;
    HouseSaveFile() {
        static std::atomic<unsigned> sequence{0};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        directory = std::filesystem::temp_directory_path()
            / ("cataclysmx-house-" + std::to_string(stamp) + "-" + std::to_string(sequence.fetch_add(1)));
        REQUIRE(std::filesystem::create_directory(directory));
        path = (directory / "world.sqlite").string();
    }
    ~HouseSaveFile() { std::error_code error; std::filesystem::remove_all(directory, error); }
};

ContentCatalog StorageContent() {
    constexpr ContentSource source{"house-save", R"({format_version:1,definitions:[
      {type:'terrain',id:'floor',name:'Floor',movement:{passable:true},blocks_sight:false,appearance:{symbol:'.',foreground:'white'}},
      {type:'item',id:'material',name:'Material',mass_g:1,volume_ml:100},
      {type:'item',id:'tiny',name:'Tiny',mass_g:1,volume_ml:1},
      {type:'item',id:'bag',name:'Bag',mass_g:1,volume_ml:10,capabilities:[{type:'container',id:'storage',capacity_volume_ml:1000}]},
      {type:'recipe',id:'make',inputs:[{item:'material',count:1}],result:{item:'tiny',count:1},work:10}
    ],objects:[
      {id:'small',name:'Small',behavior:'none',capacity_volume_ml:10,blocks_movement:true},
      {id:'large',name:'Large',behavior:'none',capacity_volume_ml:100,blocks_movement:true},
      {id:'legacy',name:'Legacy press',behavior:'crafting_machine',machine:{input:'material',input_count:1,output:'tiny',output_count:1,duration_ticks:100,energy_capacity:1000,energy_per_tick:1}}
    ]})"};
    Diagnostics diagnostics;
    auto result = LoadContent({&source, 1}, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str());
    REQUIRE(result);
    return eastl::move(*result);
}

ChunkArchive StorageChunk(const ContentCatalog& content, const char* type) {
    ChunkArchive chunk{};
    chunk.tiles.fill({content.FindTerrain("floor"), 0});
    SavedObject object{1, {2, 2, 0}, true, 0};
    object.type = content.FindObject(type);
    chunk.objects.push_back(eastl::move(object));
    return chunk;
}

void Commit(SaveDatabase& database, const ChunkArchive& chunk, std::uint32_t version = 4) {
    REQUIRE(database.Begin());
    REQUIRE(database.WriteChunk(chunk));
    SaveSnapshot snapshot;
    snapshot.nextId = 100;
    snapshot.generationVersion = version;
    REQUIRE(database.WriteSnapshot(snapshot));
    REQUIRE(database.Commit());
}

TEST_CASE("Disk furniture capacity counts direct external volumes in nested trees", "[house][save]") {
    const auto content = StorageContent();
    HouseSaveFile file;
    SaveDatabase database(content);
    REQUIRE(database.Create(file.path.c_str()));
    auto chunk = StorageChunk(content, "small");
    chunk.items.push_back({2, content.FindItem("bag"), LocationKind::object, {}, 1});
    chunk.items.push_back({3, content.FindItem("bag"), LocationKind::item, {}, 2});
    chunk.items.push_back({4, content.FindItem("material"), LocationKind::item, {}, 3});
    Commit(database, chunk);
    auto loaded = database.ReadChunk({}, 100);
    REQUIRE(loaded);
    REQUIRE(*loaded);
    CHECK((**loaded).items.size() == 3);
    chunk.items.push_back({5, content.FindItem("tiny"), LocationKind::object, {}, 1});
    REQUIRE(database.Begin());
    CHECK(database.WriteChunk(chunk) == eastl::unexpected(SaveError::InvalidData));
    // Failed write rolls back and leaves the prior valid snapshot intact.
    loaded = database.ReadChunk({}, 100);
    REQUIRE(loaded);
    REQUIRE(*loaded);
    CHECK((**loaded).items.size() == 3);
}

TEST_CASE("Disk furniture capacity includes dynamic unfinished craft volume", "[house][save][crafting]") {
    const auto content = StorageContent();
    HouseSaveFile file;
    SaveDatabase database(content);
    REQUIRE(database.Create(file.path.c_str()));
    auto chunk = StorageChunk(content, "large");
    CraftingJobState job;
    job.recipeName = "make";
    job.craftingRecipe = content.FindCraftingRecipe("make");
    job.totalWork = 10;
    SavedItem unfinished{2, content.UnfinishedCraftItem(), LocationKind::object, {}, 1};
    unfinished.state.Set("crafting_job", WriteCraftingState(job));
    chunk.items.push_back(eastl::move(unfinished));
    chunk.items.push_back({3, content.FindItem("material"), LocationKind::item, {}, 2});
    Commit(database, chunk);
    chunk.objects[0].type = content.FindObject("small");
    REQUIRE(database.Begin());
    CHECK(database.WriteChunk(chunk) == eastl::unexpected(SaveError::InvalidData));
    // The same on-disk item tree is rejected if current content's capacity shrinks.
    auto narrower = StorageContent();
    auto& definition = const_cast<ObjectDefinition&>(narrower.GetObject(narrower.FindObject("large")));
    definition.capacityVolumeMl = 10;
    SaveDatabase reader(narrower);
    CHECK(reader.Open(file.path.c_str()) == eastl::unexpected(SaveError::InvalidData));
}

TEST_CASE("Disk validation rejects blocking furniture actor overlap but permits parked players", "[house][save]") {
    const auto content = StorageContent();
    HouseSaveFile file;
    SaveDatabase database(content);
    REQUIRE(database.Create(file.path.c_str()));
    auto chunk = StorageChunk(content, "small");
    chunk.actors.push_back({2, {2, 2, 0}, true, 0});
    REQUIRE(database.Begin());
    CHECK(database.WriteChunk(chunk) == eastl::unexpected(SaveError::InvalidData));
    chunk.actors[0].parked = true;
    Commit(database, chunk);
    auto loaded = database.ReadChunk({}, 100);
    REQUIRE(loaded);
    REQUIRE(*loaded);
    CHECK((**loaded).actors[0].parked);
}

TEST_CASE("Disk snapshots accept generation versions one through four", "[house][save]") {
    const auto content = StorageContent();
    for (std::uint32_t version = 1; version <= 4; ++version) {
        CAPTURE(version);
        HouseSaveFile file;
        {
            SaveDatabase database(content);
            REQUIRE(database.Create(file.path.c_str()));
            Commit(database, StorageChunk(content, "small"), version);
        }
        SaveDatabase reader(content);
        const auto snapshot = reader.Open(file.path.c_str());
        REQUIRE(snapshot);
        CHECK(snapshot->generationVersion == version);
    }
}

TEST_CASE("Legacy objects and machines retain internal contents without capacity", "[house][save]") {
    const auto content = StorageContent();
    for (bool machine : {false, true}) {
        CAPTURE(machine);
        HouseSaveFile file;
        SaveDatabase database(content);
        REQUIRE(database.Create(file.path.c_str()));
        auto chunk = StorageChunk(content, "legacy");
        auto& object = chunk.objects[0];
        if (machine) {
            auto behavior = MakeObjectBehavior(content.GetObject(object.type));
            REQUIRE(behavior);
            REQUIRE(*behavior);
            object.state = (*behavior)->SaveState();
        } else {
            object.type = {};
        }
        for (PersistentId id = 2; id < 20; ++id) {
            chunk.items.push_back({id, content.FindItem("material"), LocationKind::object, {}, 1});
        }
        Commit(database, chunk);
        auto loaded = database.ReadChunk({}, 100);
        REQUIRE(loaded);
        REQUIRE(*loaded);
        CHECK((**loaded).items.size() == 18);
    }
}
}
