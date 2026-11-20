#include "core/world.h"
#include "digital/machine_system.h"
#include "objects/machine_job.h"
#include "power/power_system.h"
#include "skills/learning_system.h"

#include <catch2/catch_test_macros.hpp>
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

ContentCatalog CompletionContent() {
    const ContentSource source{"completion.json5", R"({format_version:1,definitions:[
      {type:'terrain',id:'floor',name:'Floor',movement:{passable:true},blocks_sight:false,appearance:{symbol:'.',foreground:'white'}},
      {type:'terrain',id:'wall',name:'Wall',movement:{passable:false},blocks_sight:true,appearance:{symbol:'#',foreground:'white'}},
      {type:'terrain',id:'air',name:'Air',movement:{passable:true},open_space:true,blocks_sight:false,looks_like:'floor'},
      {type:'skill',id:'computers',name:'Computers'},
      {type:'book',id:'boost',name:'Boost',study_work:1,
       benefits:[{skill:'computers',first_level:1,last_level:5,multiplier:15000}]},
      {type:'item',id:'manual',name:'Manual',mass_g:1,volume_ml:1,books:['boost']},
      {type:'item',id:'filament',name:'Filament',mass_g:10,volume_ml:15},
      {type:'item',id:'cup',name:'Cup',mass_g:8,volume_ml:20},
      {type:'item',id:'printer',name:'Printer',mass_g:100,volume_ml:100,
       digital_device:'controller',supplied_information:['supplied'],placed_object:'station'},
      {type:'information',id:'supplied',name:'Supplied',software:true,designs:['low','high','advanced']},
      {type:'machine_recipe',id:'low',name:'Low',inputs:[{item:'filament',count:1}],
       result:{item:'cup',count:1},duration:10,energy_per_tick:1,setup_work:10,setup_work_per_unit:0,
       learning:{skill:'computers',level:0,xp_per_execution:20}},
      {type:'machine_recipe',id:'high',name:'High',inputs:[{item:'filament',count:1}],
       result:{item:'cup',count:1},duration:10,energy_per_tick:1,setup_work:10,setup_work_per_unit:0,
       learning:{skill:'computers',level:1,xp_per_execution:30}},
      {type:'machine_recipe',id:'advanced',name:'Advanced',inputs:[{item:'filament',count:1}],
       result:{item:'cup',count:1},duration:10,energy_per_tick:1,setup_work:10,setup_work_per_unit:0,
       learning:{skill:'computers',level:5,xp_per_execution:10}},
      {type:'item',id:'battery',name:'Battery',mass_g:1,volume_ml:1,placed_object:'battery_box',
       capabilities:[{type:'energy_store',id:'cell',capacity_energy:1000,initial_energy:1000,
       rechargeable:true,battery_class:'cell'}]}
    ],objects:[
      {id:'station',name:'Printer',behavior:'selectable_machine',packing_item:'printer',
       capacity_volume_ml:12000,selectable_recipes:['low','high','advanced']},
      {id:'battery_box',name:'Battery box',behavior:'grid_battery'}
    ]})"};
    Diagnostics diagnostics;
    auto loaded = LoadContent({&source, 1}, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str());
    return Take(eastl::move(loaded));
}

struct SaveFile {
    std::filesystem::path directory;
    std::string path;
    SaveFile() {
        directory = std::filesystem::temp_directory_path() / ("cataclysmx-completion-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        REQUIRE(std::filesystem::create_directory(directory));
        path = (directory / "world.sqlite").string();
    }
    ~SaveFile() {
        std::error_code error;
        std::filesystem::remove_all(directory, error);
    }
};

struct ExclusiveReadBlocker {
    sqlite3* database = nullptr;
    explicit ExclusiveReadBlocker(const char* path) {
        REQUIRE(sqlite3_open(path, &database) == SQLITE_OK);
        REQUIRE(sqlite3_exec(database, "BEGIN EXCLUSIVE", nullptr, nullptr, nullptr) == SQLITE_OK);
    }
    ~ExclusiveReadBlocker() {
        sqlite3_exec(database, "ROLLBACK", nullptr, nullptr, nullptr);
        sqlite3_close(database);
    }
};

LoadRequest Load(World& world, int column, LoadMode mode = LoadMode::active) {
    const auto request = Take(world.RequestChunks({{column, 0}, 0}, mode));
    REQUIRE(world.ReconcileChunks());
    return request;
}

void Floor(World& world, TileCoord position) {
    if (const auto object = world.ObjectAt(position); object.value) { REQUIRE(world.DestroyObject(object)); }
    if (const auto actor = world.ActorAt(position); actor.value) { REQUIRE(world.DestroyActor(actor)); }
    REQUIRE(world.SetTile(position, {world.GetContent().FindTerrain("floor"), 0}));
}

MachineJobState Job(const World& world, ObjectHandle machine) {
    const auto state = world.CaptureObjectState(machine);
    const auto* job = state.Find("job");
    REQUIRE(job);
    return Take(ReadMachineJobState(*job));
}

bool HasJob(const World& world, ObjectHandle machine) {
    const auto state = world.DescribeObjectState(machine);
    REQUIRE(state);
    return state->hasJob;
}

unsigned Count(const World& world, ObjectHandle machine, ItemTypeId type) {
    unsigned count = 0;
    for (const auto item : world.GetObject(machine)->contents) {
        if (world.GetItem(item)->type == type) { ++count; }
    }
    return count;
}

// Explicit event boundaries let tests inspect completion before publication.
void Elapse(World& world, Tick ticks) {
    for (Tick i = 0; i < ticks; ++i) {
        world.Power().ProcessEvents();
        world.ElapseTicks(1);
        world.Machines().ProcessEvents();
        world.ProcessObjectEvents();
    }
}

struct Rig {
    ContentCatalog content = CompletionContent();
    World world{content};
    LoadRequest origin{};
    LoadRequest workshop{};
    ActorHandle actor{};
    ActorHandle helper{};
    ObjectHandle machine{};
    ItemHandle battery{};
    ItemCapabilityId store{};
    Rig(bool player = true) {
        origin = Load(world, 0);
        workshop = Load(world, 1);
        for (const TileCoord position : {TileCoord{2, 2}, TileCoord{18, 2}, TileCoord{18, 3},
                                       TileCoord{19, 2}, TileCoord{20, 2}, TileCoord{19, 3}}) {
            Floor(world, position);
        }
        actor = Take(world.CreateActor({18, 2}, player));
        helper = Take(world.CreateActor({18, 3}, true));
        machine = Take(world.CreateObject(content.FindObject("station"), {19, 2}));
        Take(world.Power().ExtendBounds({{19, 2}, {20, 3}}));
        const auto box = Take(world.CreateObject(content.FindObject("battery_box"), {20, 2}));
        battery = Take(world.CreateItem(content.FindItem("battery"), ItemLocation::Stored(box)));
        store = content.GetItem(content.FindItem("battery")).energyStores.front().capability;
    }
    SkillId Skill() const { return content.FindSkill("computers"); }
    std::uint32_t Energy() const { return world.GetItemEnergy(battery, store)->energy; }
    void Start(unsigned quantity = 1, const char* recipe = "low", ObjectHandle target = {}) {
        if (!target.value) { target = machine; }
        for (unsigned i = 0; i < quantity; ++i) {
            Take(world.CreateItem(content.FindItem("filament"), ItemLocation::Carried(actor)));
        }
        REQUIRE(Take(world.Machines().Start(actor, target, content.FindMachineRecipe(recipe),
            static_cast<std::uint16_t>(quantity))).outcome == MachineOutcome::Started);
    }
    void Setup(ObjectHandle target = {}) {
        if (!target.value) { target = machine; }
        Elapse(world, 10);
        REQUIRE(Job(world, target).phase == MachineJobPhase::Processing);
    }
    void CheckReward(ActorHandle recipient, unsigned xp) const {
        const auto* progress = FindSkillProgress(world.GetActor(recipient)->learning, Skill());
        REQUIRE(progress);
        CHECK(progress->xp == std::uint64_t{xp} * ratioScale);
    }
    void CheckComplete(unsigned quantity = 1) const {
        CHECK_FALSE(HasJob(world, machine));
        CHECK(Count(world, machine, content.FindItem("cup")) == quantity);
        CHECK(Count(world, machine, content.FindItem("filament")) == 0);
        REQUIRE(world.CheckInvariants());
    }
};

TEST_CASE("Ten shipped cups consume prepared originals and exactly twelve thousand battery energy",
    "[digital][completion][power]") {
    Diagnostics diagnostics;
    auto loaded = LoadContentPack(CATACLYSMX_CONTENT_PACK, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str());
    const auto content = Take(eastl::move(loaded));
    World world(content);
    Load(world, 0);
    for (int x = 2; x <= 6; ++x) { Floor(world, {x, 2}); }
    const auto actor = Take(world.CreateActor({2, 2}, true));
    const auto machine = Take(world.CreateObject(content.FindObject("home_polymer_printer_station"), {3, 2}));
    const auto batteryType = content.FindItem("workshop_battery_pack");
    const auto& batteryDefinition = content.GetItem(batteryType);
    REQUIRE(batteryDefinition.placedObject.value);
    Take(world.Power().ExtendBounds({{3, 2}, {6, 2}}));
    Vector<ItemHandle> batteries;
    for (int i = 0; i < 3; ++i) {
        const auto box = Take(world.CreateObject(batteryDefinition.placedObject, {4 + i, 2}));
        const auto battery = Take(world.CreateItem(batteryType, ItemLocation::Stored(box)));
        REQUIRE(world.SetItemEnergy(battery, batteryDefinition.energyStores.front().capability, 4000));
        batteries.push_back(battery);
    }
    const auto energy = [&]() {
        std::uint64_t total = 0;
        for (auto battery : batteries) {
            total += world.GetItemEnergy(battery, batteryDefinition.energyStores.front().capability)->energy;
        }
        return total;
    };
    Vector<PersistentId> originals;
    const auto filament = content.FindItem("polymer_filament");
    for (int i = 0; i < 51; ++i) {
        const auto item = Take(world.CreateItem(filament, ItemLocation::Carried(actor)));
        originals.push_back(world.GetItem(item)->id);
    }
    REQUIRE(Take(world.Machines().Start(actor, machine, content.FindMachineRecipe("print_plastic_cup"), 10))
        .outcome == MachineOutcome::Started);
    const auto captured = Job(world, machine);
    CHECK(captured.totalSetupWork == 400);
    CHECK(captured.duration == 12000);
    CHECK(captured.learningXp == 100);
    CHECK(captured.reservedInputs.size() == 50);
    CHECK(captured.reservedVolumeMl == 4000);
    CHECK(content.GetItem(filament).massGrams * captured.reservedInputs.size() == 500);
    Elapse(world, 399);
    CHECK(Job(world, machine).phase == MachineJobPhase::Setup);
    CHECK(energy() == 12000);
    CHECK(Count(world, machine, content.FindItem("plastic_cup")) == 0);
    Elapse(world, 1);
    CHECK(Job(world, machine).setupWork == 400 * ratioScale);
    CHECK(Job(world, machine).remainingWork == 12000);
    CHECK(energy() == 12000);
    Elapse(world, 11999);
    CHECK(HasJob(world, machine));
    CHECK(Count(world, machine, content.FindItem("plastic_cup")) == 0);
    Elapse(world, 1);
    CHECK_FALSE(HasJob(world, machine));
    CHECK(energy() == 0);
    CHECK(Count(world, machine, content.FindItem("plastic_cup")) == 10);
    for (auto id : captured.reservedInputs) { CHECK_FALSE(world.FindItem(id).value); }
    unsigned surviving = 0;
    for (auto id : originals) { surviving += world.FindItem(id).value != 0; }
    CHECK(surviving == 1);
    const auto* progress = FindSkillProgress(world.GetActor(actor)->learning, content.FindSkill("computers"));
    REQUIRE(progress);
    CHECK(progress->level == 1);
    CHECK(progress->xp == 0);
    CHECK(world.GetActor(actor)->learning.recipes.empty());
    Elapse(world, 200);
    CHECK(energy() == 0);
    CHECK(Count(world, machine, content.FindItem("plastic_cup")) == 10);
    REQUIRE(world.CheckInvariants());
}

TEST_CASE("A helper resumes setup and collects without taking the initiator reward",
    "[digital][completion]") {
    Rig r;
    r.Start();
    Elapse(r.world, 5);
    REQUIRE(r.world.MoveActor(r.actor, {2, 2}));
    Elapse(r.world, 1);
    CHECK(Job(r.world, r.machine).worker == 0);
    CHECK(Job(r.world, r.machine).remainingSetupTicks == 5);
    CHECK(r.Energy() == 1000);
    REQUIRE(Take(r.world.Machines().Use(r.helper, r.machine, MachineAction::Resume)).outcome
        == MachineOutcome::Resumed);
    CHECK(Job(r.world, r.machine).initiator == r.world.GetActor(r.actor)->id);
    CHECK(Job(r.world, r.machine).worker == r.world.GetActor(r.helper)->id);
    Elapse(r.world, 5);
    CHECK(Job(r.world, r.machine).phase == MachineJobPhase::Processing);
    CHECK(Job(r.world, r.machine).setupWork == 10 * ratioScale);
    REQUIRE(Take(r.world.Machines().Use(r.helper, r.machine, MachineAction::Stop)).outcome
        == MachineOutcome::Stopped);
    REQUIRE(Take(r.world.Machines().Use(r.helper, r.machine, MachineAction::Resume)).outcome
        == MachineOutcome::Resumed);
    Elapse(r.world, 10);
    r.CheckComplete();
    r.CheckReward(r.actor, 20);
    const auto awarded = r.world.GetActor(r.actor)->learning;
    REQUIRE(Take(r.world.Machines().Use(r.helper, r.machine, MachineAction::Collect)).outcome
        == MachineOutcome::Collected);
    Elapse(r.world, 200);
    CHECK(r.world.GetActor(r.actor)->learning == awarded);
    CHECK_FALSE(FindSkillProgress(r.world.GetActor(r.helper)->learning, r.Skill()));
    CHECK(r.Energy() == 990);
    REQUIRE(r.world.CheckInvariants());
}

TEST_CASE("Completion resolves live parked resident and archived initiators without activation",
    "[digital][completion][save]") {
    enum class Storage { Live, Parked, Resident, Archive, Disk };
    Storage storage = Storage::Live;
    SECTION("live") {}
    SECTION("parked player") { storage = Storage::Parked; }
    SECTION("resident inactive NPC") { storage = Storage::Resident; }
    SECTION("authoritative archive") { storage = Storage::Archive; }
    SECTION("disk only archive") { storage = Storage::Disk; }
    SaveFile file;
    Rig r(storage == Storage::Live || storage == Storage::Parked);
    const auto id = r.world.GetActor(r.actor)->id;
    r.Start(); r.Setup();
    REQUIRE(r.world.MoveActor(r.actor, {2, 2}));
    if (storage == Storage::Parked) { REQUIRE(r.world.ParkActors({&r.actor, 1})); }
    if (storage == Storage::Resident) { Load(r.world, 0, LoadMode::resident); }
    if (storage == Storage::Resident || storage == Storage::Archive || storage == Storage::Disk) {
        REQUIRE(r.world.ReleaseRequest(r.origin));
        REQUIRE(r.world.ReconcileChunks());
    }
    SessionState session;
    if (storage == Storage::Disk) { REQUIRE(r.world.CreateSave(file.path.c_str(), session)); }
    const auto tick = r.world.GetTick();
    Elapse(r.world, 10);
    r.CheckComplete();
    if (storage == Storage::Archive || storage == Storage::Disk) {
        CHECK_FALSE(r.world.FindActor(id).value);
        Load(r.world, 0);
    }
    const auto restored = r.world.FindActor(id);
    REQUIRE(restored.value);
    r.CheckReward(restored, 20);
    CHECK(r.world.GetTick() == tick + 10);
    CHECK_FALSE(FindSkillProgress(r.world.GetActor(r.helper)->learning, r.Skill()));
    if (r.world.HasSave()) { REQUIRE(r.world.Save(session)); }
    else { REQUIRE(r.world.CreateSave(file.path.c_str(), session)); }
    World reopened(r.content);
    Take(reopened.LoadSave(file.path.c_str()));
    Load(reopened, 0); Load(reopened, 1);
    const auto* progress = FindSkillProgress(reopened.GetActor(reopened.FindActor(id))->learning, r.Skill());
    REQUIRE(progress);
    CHECK(progress->xp == 20 * ratioScale);
    REQUIRE(reopened.CheckInvariants());
    REQUIRE(r.world.CheckInvariants());
}

TEST_CASE("Reloaded paid setup and stopped processing finish without repaying work or energy",
    "[digital][completion][save]") {
    bool setup = true;
    SECTION("unassigned paid setup slice") {}
    SECTION("stopped prepaid processing") { setup = false; }
    SaveFile file;
    Rig r;
    r.Start();
    if (setup) {
        Elapse(r.world, 5);
        REQUIRE(r.world.MoveActor(r.actor, {2, 2}));
        Elapse(r.world, 1);
    } else {
        r.Setup();
        Elapse(r.world, 4);
        REQUIRE(Take(r.world.Machines().Use(r.helper, r.machine, MachineAction::Stop)).outcome
            == MachineOutcome::Stopped);
    }
    const auto machineId = r.world.GetObject(r.machine)->id;
    const auto initiatorId = r.world.GetActor(r.actor)->id;
    const auto helperId = r.world.GetActor(r.helper)->id;
    const auto batteryId = r.world.GetItem(r.battery)->id;
    const auto captured = r.world.CaptureObjectState(r.machine);
    const auto before = Job(r.world, r.machine);
    CHECK(before.remainingSetupTicks == (setup ? 5 : 0));
    CHECK(before.fundedTicks == (setup ? 0 : 6));
    CHECK(r.Energy() == (setup ? 1000 : 990));
    REQUIRE(r.world.CreateSave(file.path.c_str(), {}));
    World restored(r.content);
    Take(restored.LoadSave(file.path.c_str()));
    Load(restored, 1);
    REQUIRE(restored.CheckInvariants());
    const auto machine = restored.FindObject(machineId);
    const auto helper = restored.FindActor(helperId);
    const auto battery = restored.FindItem(batteryId);
    REQUIRE(machine.value);
    REQUIRE(helper.value);
    REQUIRE(battery.value);
    CHECK(restored.CaptureObjectState(machine) == captured);
    REQUIRE(Take(restored.Machines().Use(helper, machine, MachineAction::Resume)).outcome
        == MachineOutcome::Resumed);
    Elapse(restored, setup ? 15 : 6);
    CHECK_FALSE(HasJob(restored, machine));
    CHECK(Count(restored, machine, r.content.FindItem("cup")) == 1);
    CHECK(restored.GetItemEnergy(battery, r.store)->energy == 990);
    for (auto original : before.reservedInputs) { CHECK_FALSE(restored.FindItem(original).value); }
    Load(restored, 0);
    const auto recipient = restored.FindActor(initiatorId);
    REQUIRE(recipient.value);
    const auto* progress = FindSkillProgress(restored.GetActor(recipient)->learning, r.Skill());
    REQUIRE(progress);
    CHECK(progress->xp == 20 * ratioScale);
    CHECK_FALSE(FindSkillProgress(restored.GetActor(helper)->learning, r.Skill()));
    REQUIRE(restored.Save({}));
    REQUIRE(restored.CheckInvariants());
}

TEST_CASE("Destroyed initiators do not prevent products or reward a replacement",
    "[digital][completion][save]") {
    SaveFile file;
    Rig r(false);
    const auto id = r.world.GetActor(r.actor)->id;
    r.Start(); r.Setup();
    REQUIRE(r.world.CreateSave(file.path.c_str(), {}));
    REQUIRE(r.world.DestroyActor(r.actor));
    const auto replacement = Take(r.world.CreateActor({18, 2}, false));
    CHECK(r.world.GetActor(replacement)->id != id);
    Elapse(r.world, 10);
    r.CheckComplete();
    CHECK_FALSE(r.world.FindActor(id).value);
    CHECK_FALSE(FindSkillProgress(r.world.GetActor(replacement)->learning, r.Skill()));
    CHECK_FALSE(FindSkillProgress(r.world.GetActor(r.helper)->learning, r.Skill()));
    REQUIRE(r.world.Save({}));
    REQUIRE(r.world.CheckInvariants());
}

TEST_CASE("Archived recipient read failures retain originals and retry publication exactly once",
    "[digital][completion][save][retry]") {
    SaveFile file;
    Rig r(false);
    const auto id = r.world.GetActor(r.actor)->id;
    const auto machineId = r.world.GetObject(r.machine)->id;
    r.Start(); r.Setup();
    const auto originals = Job(r.world, r.machine).reservedInputs;
    REQUIRE(r.world.MoveActor(r.actor, {2, 2}));
    REQUIRE(r.world.ReleaseRequest(r.origin));
    REQUIRE(r.world.ReconcileChunks());
    REQUIRE(r.world.CreateSave(file.path.c_str(), {}));
    const auto turns = r.world.GetActor(r.helper)->turns;
    {
        ExclusiveReadBlocker blocker(file.path.c_str());
        Elapse(r.world, 10);
        const auto pending = Job(r.world, r.machine);
        CHECK(pending.waitingError == SaveError::Busy);
        CHECK(pending.remainingWork == 0);
        CHECK(pending.fundedTicks == 0);
        CHECK(pending.pendingWork == 0);
        CHECK(pending.completionRetryTicks == machineCompletionRetryTicks);
        CHECK(pending.reservedInputs == originals);
        for (auto original : originals) {
            const auto item = r.world.FindItem(original);
            REQUIRE(item.value);
            CHECK(r.world.GetItem(item)->location == ItemLocation::Stored(r.machine));
        }
        CHECK(Count(r.world, r.machine, r.content.FindItem("cup")) == 0);
        CHECK_FALSE(r.world.FindActor(id).value);
        CHECK(r.Energy() == 990);
        REQUIRE(r.world.CheckInvariants());
    }
    // Persist the failed, fully paid completion rather than merely retrying RAM.
    REQUIRE(r.world.Save({}));
    World restored(r.content);
    Take(restored.LoadSave(file.path.c_str()));
    Load(restored, 1);
    REQUIRE(restored.CheckInvariants());
    const auto machine = restored.FindObject(machineId);
    REQUIRE(machine.value);
    CHECK(Job(restored, machine).remainingWork == 0);
    CHECK(Job(restored, machine).reservedInputs == originals);
    Elapse(restored, machineCompletionRetryTicks - 1);
    CHECK(HasJob(restored, machine));
    Elapse(restored, 1);
    CHECK_FALSE(HasJob(restored, machine));
    CHECK(Count(restored, machine, r.content.FindItem("cup")) == 1);
    for (auto original : originals) { CHECK_FALSE(restored.FindItem(original).value); }
    CHECK_FALSE(restored.FindActor(id).value);
    const auto battery = restored.FindItem(r.world.GetItem(r.battery)->id);
    REQUIRE(battery.value);
    CHECK(restored.GetItemEnergy(battery, r.store)->energy == 990);
    Elapse(restored, 200);
    CHECK(Count(restored, machine, r.content.FindItem("cup")) == 1);
    CHECK(restored.GetItemEnergy(battery, r.store)->energy == 990);
    CHECK(restored.GetActor(restored.FindActor(r.world.GetActor(r.helper)->id))->turns == turns);
    Load(restored, 0);
    const auto* progress = FindSkillProgress(restored.GetActor(restored.FindActor(id))->learning, r.Skill());
    REQUIRE(progress);
    CHECK(progress->xp == 20 * ratioScale);
    REQUIRE(restored.Save({}));
    REQUIRE(restored.CheckInvariants());
}

TEST_CASE("Same tick completion uses persistent machine order rather than reused slots",
    "[digital][completion][determinism]") {
    Rig r;
    const auto spare = r.machine;
    const auto low = Take(r.world.CreateObject(r.content.FindObject("station"), {19, 3}));
    REQUIRE(r.world.DestroyObject(spare));
    const auto high = Take(r.world.CreateObject(r.content.FindObject("station"), {19, 2}));
    REQUIRE(r.world.GetObject(low)->id < r.world.GetObject(high)->id);
    Take(r.world.Learning().Train(r.actor, {r.Skill(), 0, ratioScale}, SkillActivity::Craft, 90 * ratioScale));
    r.Start(1, "low", low); r.Setup(low);
    REQUIRE(Take(r.world.Machines().Use(r.actor, low, MachineAction::Stop)).outcome == MachineOutcome::Stopped);
    r.Start(1, "high", high); r.Setup(high);
    REQUIRE(Take(r.world.Machines().Use(r.actor, low, MachineAction::Resume)).outcome == MachineOutcome::Resumed);
    Elapse(r.world, 10);
    CHECK_FALSE(HasJob(r.world, low));
    CHECK_FALSE(HasJob(r.world, high));
    const auto* progress = FindSkillProgress(r.world.GetActor(r.actor)->learning, r.Skill());
    REQUIRE(progress);
    CHECK(progress->level == 1);
    // Low first clips 10 surplus at its ceiling, then high supplies all 30.
    // Reversing completion order would leave only 20 XP at level one.
    CHECK(progress->xp == 30 * ratioScale);
    CHECK(Count(r.world, low, r.content.FindItem("cup")) == 1);
    CHECK(Count(r.world, high, r.content.FindItem("cup")) == 1);
    CHECK(r.Energy() == 980);
    REQUIRE(r.world.CheckInvariants());
}

TEST_CASE("Split commissioned quantities preserve additive completion learning",
    "[digital][completion][skills]") {
    Rig batch, split;
    batch.Start(4); batch.Setup(); Elapse(batch.world, 40); batch.CheckComplete(4);
    for (int i = 0; i < 2; ++i) {
        split.Start(2); split.Setup(); Elapse(split.world, 20);
        REQUIRE(Take(split.world.Machines().Use(split.helper, split.machine, MachineAction::Collect)).outcome
            == MachineOutcome::Collected);
    }
    CHECK(batch.world.GetActor(batch.actor)->learning == split.world.GetActor(split.actor)->learning);
    batch.CheckReward(batch.actor, 80);
    split.CheckReward(split.actor, 80);
    CHECK(batch.Energy() == split.Energy());
    REQUIRE(split.world.CheckInvariants());
}

TEST_CASE("Selectable processing holds an unattended workshop until durable completion",
    "[digital][completion][power][residency]") {
    Rig r;
    const auto machineId = r.world.GetObject(r.machine)->id;
    const auto actorId = r.world.GetActor(r.actor)->id;
    r.Start(); r.Setup();
    REQUIRE(r.world.SetActorParked(r.actor, true));
    REQUIRE(r.world.SetActorParked(r.helper, true));
    REQUIRE(r.world.ReleaseRequest(r.origin));
    REQUIRE(r.world.ReleaseRequest(r.workshop));
    REQUIRE(r.world.ReconcileChunks());
    CHECK(r.world.IsActive({19, 2}));
    REQUIRE(r.world.FindObject(machineId).value);
    Elapse(r.world, 10);
    r.CheckComplete();
    CHECK(r.Energy() == 990);
    r.CheckReward(r.actor, 20);
    REQUIRE(r.world.ReconcileChunks());
    CHECK_FALSE(r.world.FindObject(machineId).value);
    SaveFile file;
    REQUIRE(r.world.CreateSave(file.path.c_str(), {}));
    World restored(r.content);
    Take(restored.LoadSave(file.path.c_str()));
    Load(restored, 1);
    const auto machine = restored.FindObject(machineId);
    REQUIRE(machine.value);
    CHECK_FALSE(HasJob(restored, machine));
    CHECK(Count(restored, machine, r.content.FindItem("cup")) == 1);
    const auto* progress = FindSkillProgress(restored.GetActor(restored.FindActor(actorId))->learning, r.Skill());
    REQUIRE(progress);
    CHECK(progress->xp == 20 * ratioScale);
    REQUIRE(restored.CheckInvariants());
}

TEST_CASE("Destroying a commissioned printer drops the original reserved inputs without reward",
    "[digital][completion][lifecycle]") {
    Rig r;
    r.Start();
    const auto originals = Job(r.world, r.machine).reservedInputs;
    const auto equipment = r.world.GetObject(r.machine)->equipment;
    REQUIRE(r.world.DestroyObject(r.machine));
    CHECK_FALSE(r.world.Machines().Busy(r.actor));
    for (auto id : originals) {
        const auto item = r.world.FindItem(id);
        REQUIRE(item.value);
        CHECK(r.world.GetItem(item)->location == ItemLocation::Ground({19, 2}));
        CHECK_FALSE(r.world.IsReservedItem(item));
    }
    CHECK(r.world.GetItem(equipment)->location == ItemLocation::Ground({19, 2}));
    CHECK_FALSE(FindSkillProgress(r.world.GetActor(r.actor)->learning, r.Skill()));
    REQUIRE(r.world.CheckInvariants());
}

TEST_CASE("Completion uses current boosts and advanced eighty percent practice accounting",
    "[digital][completion][skills]") {
    SECTION("boost acquired after commissioning applies at completion") {
        Rig r;
        r.Start(); r.Setup();
        Take(r.world.CreateItem(r.content.FindItem("manual"), ItemLocation::Carried(r.actor)));
        REQUIRE(Take(r.world.Learning().Study(r.actor, r.content.FindBook("boost"))).outcome == StudyOutcome::Started);
        r.world.ElapseTicks(1);
        REQUIRE(r.world.Learning().ProcessEvents());
        Elapse(r.world, 10);
        r.CheckComplete();
        r.CheckReward(r.actor, 30);
    }
    SECTION("advanced completion discounts once and pairs practice with existing theory") {
        Rig r;
        REQUIRE(r.world.Learning().SetLevel(r.actor, r.Skill(), 5));
        Take(r.world.Learning().Train(r.actor, {r.Skill(), 5, ratioScale},
            SkillActivity::Disassembly, 3 * ratioScale));
        const auto before = r.world.GetActor(r.actor)->learning;
        auto expected = before;
        AwardSkillXp(expected, r.content, r.Skill(), 5, SkillActivity::Craft, 8 * ratioScale);
        r.Start(1, "advanced"); r.Setup(); Elapse(r.world, 10);
        r.CheckComplete();
        CHECK(r.world.GetActor(r.actor)->learning == expected);
        const auto* progress = FindSkillProgress(r.world.GetActor(r.actor)->learning, r.Skill());
        REQUIRE(progress);
        CHECK(progress->level == 5);
        CHECK(progress->xp == 3 * ratioScale);
        CHECK(progress->theory == 0);
        CHECK(progress->practice == 5 * ratioScale);
    }
}
}
