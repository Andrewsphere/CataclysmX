#include "core/save_database.h"
#include "core/item_state.h"
#include "items/item_snapshot.h"
#include "objects/object_behavior.h"
#include "core/ratio.h"
#include "crafting/crafting_state.h"
#include "skills/skill_state.h"
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <string>

namespace {
using namespace cataclysmx;
ContentCatalog Content() {
    const ContentSource source{"digital-save", R"({format_version:1,definitions:[
      {type:'terrain',id:'floor',name:'Floor',movement:{passable:true},blocks_sight:false,
       appearance:{symbol:'.',foreground:'white'}},
      {type:'skill',id:'computers',name:'Computers'},
      {type:'book',id:'manual',name:'Manual',study_work:100,
       benefits:[{skill:'computers',first_level:1,last_level:1,direct_xp:10}]},
      {type:'item',id:'filament',name:'Filament',mass_g:10,volume_ml:15},
      {type:'item',id:'cup',name:'Cup',mass_g:40,volume_ml:400},
      {type:'item',id:'usb',name:'USB',mass_g:1,volume_ml:1,digital_device:'storage'},
      {type:'item',id:'printer',name:'Printer',mass_g:8000,volume_ml:65000,
       digital_device:'controller',supplied_information:['supplied'],placed_object:'station'},
      {type:'information',id:'supplied',name:'Supplied',designs:['print_cup'],software:true},
      {type:'machine_recipe',id:'print_cup',inputs:[{item:'filament',count:5}],
       result:{item:'cup',count:1},duration:1200,energy_per_tick:1,
       learning:{skill:'computers',level:0,xp_per_execution:10}},
      {type:'machine_recipe',id:'other',inputs:[{item:'filament',count:5}],
       result:{item:'cup',count:1},duration:1200,energy_per_tick:1,
       learning:{skill:'computers',level:0,xp_per_execution:10}},
      {type:'recipe',id:'manual_craft',inputs:[],result:{item:'cup',count:1},work:220}
    ],objects:[{id:'station',name:'Printer',behavior:'selectable_machine',
       packing_item:'printer',capacity_volume_ml:800,selectable_recipes:['print_cup']}]
    })"};
    Diagnostics diagnostics;
    auto loaded = LoadContent({&source, 1}, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str());
    REQUIRE(loaded);
    return eastl::move(*loaded);
}
struct File {
    std::filesystem::path directory;
    std::string path;
    File() {
        directory = std::filesystem::temp_directory_path() / ("cataclysmx-digital-save-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        REQUIRE(std::filesystem::create_directory(directory));
        path = (directory / "world.sqlite").string();
    }
    ~File() { std::error_code error; std::filesystem::remove_all(directory, error); }
};
MachineJobState Job(PersistentId base = 10) {
    MachineJobState s;
    s.recipeName = "print_cup"; s.outputName = "cup";
    s.inputs.push_back({"filament", 5});
    for (PersistentId i = 2; i < 7; ++i) { s.reservedInputs.push_back(base + i); }
    s.reservedVolumeMl = 400;
    s.outputCount = 1; s.duration = 1200; s.remainingWork = 1200; s.energyPerTick = 1;
    s.totalSetupWork = 220; s.pendingWork = 50 * ratioScale; s.remainingSetupTicks = 50;
    s.initiator = 1; // Historical identity need not remain alive or loaded.
    s.learningSkillName = "computers"; s.learningXp = 10;
    return s;
}
void StoreJob(ChunkArchive& chunk, const MachineJobState& job) {
    chunk.objects[0].state.SetUnsigned("version", 1);
    chunk.objects[0].state.SetBoolean("has_job", true);
    chunk.objects[0].state.Set("job", WriteMachineJobState(job));
}
ChunkArchive Chunk(const ContentCatalog& content, PersistentId base = 10, int x = 0) {
    ChunkArchive chunk{};
    chunk.position = {x, 0, 0};
    chunk.tiles.fill({content.FindTerrain("floor"), 0});
    SavedObject object{base, {x * chunk_size, 0, 0}, true, 0};
    object.type = content.FindObject("station"); object.equipment = base + 1;
    chunk.objects.push_back(object);
    StoreJob(chunk, Job(base));
    const auto& definition = content.GetItem(content.FindItem("printer"));
    SavedItem equipment{base + 1, content.FindItem("printer"), LocationKind::object, {}, base};
    WriteItemState(content, definition, MakeItemState(definition), equipment.state);
    chunk.items.push_back(eastl::move(equipment));
    for (PersistentId i = 2; i < 7; ++i) {
        chunk.items.push_back({base + i, content.FindItem("filament"), LocationKind::object, {}, base});
    }
    return chunk;
}
void Transfer(SavedItem& item, PersistentId worker) {
    auto* library = item.state.Find("information_library");
    library->Set("packages", DataObject::List());
    DataObject transfer;
    transfer.SetUnsigned("worker", worker);
    transfer.SetUnsigned("source_item", 2); // Source has disappeared; never re-created.
    transfer.SetUnsigned("source_object", 0);
    transfer.SetUnsigned("host_item", 0);
    transfer.SetString("package", "supplied");
    transfer.SetUnsigned("remaining_ticks", 50);
    library->Set("transfer", eastl::move(transfer));
}
SaveSnapshot Snapshot(const Vector<ChunkArchive>& chunks) {
    SaveSnapshot snapshot;
    snapshot.nextId = 1000;
    for (const auto& chunk : chunks) {
        const auto& object = chunk.objects[0];
        snapshot.power.devices.push_back({object.id, object.position, PowerDeviceKind::Machine, false});
    }
    return snapshot;
}
TEST_CASE("Digital disk roundtrip preserves empty libraries paid setup and original reservations", "[digital][save]") {
    const auto content = Content();
    auto chunk = Chunk(content);
    SECTION("deleted supplied library stays empty") {
        chunk.items[0].state.Find("information_library")->Set("packages", DataObject::List());
    }
    SECTION("supplied library stays present") {}
    SECTION("paid copy retains vanished source identity without restoring source") {
        Transfer(chunk.items[0], 1);
    }
    SECTION("stopped prepaid processing retains claim") {
        auto job = Job();
        job.phase = MachineJobPhase::Processing;
        job.setupWork = std::uint64_t{job.totalSetupWork} * ratioScale;
        job.pendingWork = 0; job.remainingSetupTicks = 0;
        job.fundedTicks = 75;
        StoreJob(chunk, job);
    }
    REQUIRE(ValidateItemSnapshot(content, chunk));
    File file;
    {
        SaveDatabase database(content);
        REQUIRE(database.Create(file.path.c_str()));
        REQUIRE(database.Begin());
        Diagnostics diagnostics;
        const auto written = database.WriteChunk(chunk, &diagnostics);
        INFO(FormatDiagnostics(diagnostics).c_str()); REQUIRE(written);
        REQUIRE(database.WriteSnapshot(Snapshot({chunk})));
        REQUIRE(database.Commit(&diagnostics));
    }
    SaveDatabase loaded(content);
    REQUIRE(loaded.Open(file.path.c_str()));
    auto archive = loaded.ReadChunk(chunk.position, 1000);
    REQUIRE(archive); REQUIRE(*archive);
    CHECK((**archive).items[0].state == chunk.items[0].state);
    CHECK((**archive).objects[0].state == chunk.objects[0].state);
    const auto job = ReadMachineJobState(*(**archive).objects[0].state.Find("job"));
    REQUIRE(job);
    CHECK(job->worker == 0);
    const auto original = ReadMachineJobState(*chunk.objects[0].state.Find("job"));
    REQUIRE(original);
    CHECK(job->remainingSetupTicks == original->remainingSetupTicks);
    CHECK(job->fundedTicks == original->fundedTicks);
    CHECK(job->reservedVolumeMl == 400);
    CHECK(job->reservedInputs == Job().reservedInputs);
}
TEST_CASE("Machine snapshots reject forged reservation ownership and insufficient claims", "[digital][save]") {
    const auto content = Content();
    auto chunk = Chunk(content);
    auto job = Job();
    SECTION("backing equipment reserved") { job.reservedInputs[0] = 11; }
    SECTION("missing original") { chunk.items.pop_back(); }
    SECTION("foreign original") { chunk.items[1].location = LocationKind::ground; chunk.items[1].owner = 0; }
    SECTION("wrong captured type") { chunk.items[1].type = content.FindItem("cup"); }
    SECTION("nonleaf original") { chunk.items.push_back({90, content.FindItem("filament"), LocationKind::item, {}, 12}); }
    SECTION("unsupported recipe identity") { job.recipeName = "other"; }
    SECTION("output no longer fits claim") { job.reservedVolumeMl = 399; }
    SECTION("input no longer fits claim") { job.inputs[0].count = 5; job.outputName = "filament"; job.reservedVolumeMl = 74; }
    SECTION("public contents consume reserved output space while stopped") {
        job.phase = MachineJobPhase::Processing;
        job.setupWork = std::uint64_t{job.totalSetupWork} * ratioScale;
        job.pendingWork = 0; job.remainingSetupTicks = 0; job.fundedTicks = 100;
        chunk.items.push_back({90, content.FindItem("cup"), LocationKind::object, {}, 10});
        chunk.items.push_back({91, content.FindItem("filament"), LocationKind::object, {}, 10});
    }
    StoreJob(chunk, job);
    CHECK_FALSE(ValidateItemSnapshot(content, chunk));
}
TEST_CASE("Digital snapshot library parsing is strict and does not require vanished sources", "[digital][save]") {
    const auto content = Content();
    auto chunk = Chunk(content);
    Transfer(chunk.items[0], 1);
    REQUIRE(ValidateItemSnapshot(content, chunk));
    auto* transfer = chunk.items[0].state.Find("information_library")->Find("transfer");
    SECTION("worker required") { transfer->SetUnsigned("worker", 0); }
    SECTION("source shape") { transfer->SetUnsigned("source_object", 3); }
    SECTION("unknown package") { transfer->SetString("package", "unknown"); }
    SECTION("duration exceeds possible original payment") { transfer->SetUnsigned("remaining_ticks", std::uint64_t{UINT32_MAX} + 1); }
    SECTION("unknown library field") { chunk.items[0].state.Find("information_library")->SetBoolean("invented", true); }
    SECTION("same worker also setting up machine") { auto job = Job(); job.worker = 1; StoreJob(chunk, job); }
    CHECK_FALSE(ValidateItemSnapshot(content, chunk));
}
TEST_CASE("Global saved worker claims conflict across machine setup and information copying", "[digital][save]") {
    const auto content = Content();
    auto first = Chunk(content);
    auto job = Job(); job.worker = 1; StoreJob(first, job);
    auto second = Chunk(content, 20, 1);
    SECTION("two machine setups") { auto other = Job(20); other.worker = 1; StoreJob(second, other); }
    SECTION("setup and transfer") { Transfer(second.items[0], 1); }
    SECTION("setup and manual crafting") {
        SavedItem item{90, content.UnfinishedCraftItem(), LocationKind::ground, {chunk_size, 0, 0}, 0};
        CraftingJobState craft;
        craft.recipeName = "manual_craft";
        craft.totalWork = 220; craft.worker = 1; craft.status = CraftingStatus::Working;
        craft.pendingWork = 50 * ratioScale; craft.remaining = 50;
        item.state.Set("crafting_job", WriteCraftingState(craft));
        second.items.push_back(eastl::move(item));
    }
    SECTION("setup and study") {
        SavedActor actor{1, {chunk_size + 1, 0, 0}, false, 0};
        actor.remainingTicks = 50;
        LearningState learning;
        const auto book = content.FindBook("manual");
        learning.books.push_back({book, 0, false});
        learning.study.book = book; learning.study.active = true;
        learning.study.remaining = 50; learning.study.pendingWork = 50 * ratioScale;
        actor.learning = CaptureLearningState(learning, content);
        second.actors.push_back(eastl::move(actor));
    }
    SECTION("cross chunk worker deadline differs") {
        SavedActor actor{1, {chunk_size + 1, 0, 0}, false, 0};
        actor.remainingTicks = 49;
        second.actors.push_back(eastl::move(actor));
    }
    REQUIRE(ValidateItemSnapshot(content, first));
    REQUIRE(ValidateItemSnapshot(content, second));
    File file;
    SaveDatabase database(content);
    REQUIRE(database.Create(file.path.c_str()));
    REQUIRE(database.Begin());
    REQUIRE(database.WriteChunk(first));
    REQUIRE(database.WriteChunk(second));
    REQUIRE(database.WriteSnapshot(Snapshot({first, second})));
    const auto committed = database.Commit();
    REQUIRE_FALSE(committed);
    CHECK(committed.error() == SaveError::InvalidData);
}
}
