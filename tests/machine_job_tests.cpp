#include "objects/machine_job.h"
#include "objects/object_behavior.h"
#include "core/ratio.h"
#include <catch2/catch_test_macros.hpp>
#include <limits>

namespace {
using namespace cataclysmx;
MachineJobState Job() {
    MachineJobState s;
    s.recipeName = "print_cup";
    s.outputName = "cup";
    s.quantity = 2;
    s.inputs.push_back({"filament", 4});
    s.reservedInputs = {11, 12, 13, 14};
    s.reservedVolumeMl = 800;
    s.outputCount = 2;
    s.duration = 2400;
    s.remainingWork = s.duration;
    s.energyPerTick = 1;
    s.totalSetupWork = 240;
    s.initiator = 1;
    s.worker = 2;
    s.learningSkillName = "computers";
    s.learningXp = 20;
    return s;
}
ObjectDefinition Printer() {
    ObjectDefinition d;
    d.id = "printer";
    d.name = "Printer";
    d.behavior = ObjectBehaviorKind::SelectableMachine;
    d.packingItem = {1};
    d.capacityVolumeMl = 12000;
    d.selectableRecipes.push_back({1});
    return d;
}
void Processing(MachineJobState& s) {
    s.phase = MachineJobPhase::Processing;
    s.setupWork = std::uint64_t{s.totalSetupWork} * ratioScale;
    s.worker = 0;
    s.runRequested = true;
}
TEST_CASE("Machine capture round trips paid setup and original ownership", "[machine-job][save]") {
    auto s = Job();
    s.setupWork = 20 * ratioScale;
    s.pendingWork = 80 * ratioScale;
    s.remainingSetupTicks = 53;
    s.pauseRequested = true;
    s.timingSpeed = 123;
    s.timingRemainder = 17;
    s.softwareName = "controller";
    s.skills.push_back({"computers", 1});
    REQUIRE(ValidMachineJobState(s));
    const auto payload = WriteMachineJobState(s);
    const auto parsed = ReadMachineJobState(payload);
    REQUIRE(parsed);
    CHECK(WriteMachineJobState(*parsed) == payload);
    CHECK(parsed->worker == 2);
    CHECK(parsed->initiator == 1);
    CHECK(parsed->reservedInputs == s.reservedInputs);
    CHECK(parsed->reservedVolumeMl == 800);
}
TEST_CASE("Captured machine jobs reject impossible reservations and paid timing", "[machine-job][save]") {
    auto s = Job();
    SECTION("expanded input limit") { s.inputs[0].count = 256; }
    SECTION("expanded output limit") { s.outputCount = 256; }
    SECTION("duplicate original") { s.reservedInputs[2] = s.reservedInputs[1]; }
    SECTION("missing original") { s.reservedInputs.pop_back(); }
    SECTION("duplicate consumed type") { s.inputs.push_back(s.inputs[0]); }
    SECTION("fractional batch") { s.outputCount = 3; }
    SECTION("setup cannot request grid work") { s.runRequested = true; }
    SECTION("setup cannot hold grid funding") { s.fundedTicks = 1; }
    SECTION("paid work exceeds slice") { s.pendingWork = 100 * ratioScale + 1; }
    SECTION("paid time exceeds slice") { s.remainingSetupTicks = 101; }
    SECTION("paid work exceeds goal") {
        s.setupWork = 239 * ratioScale;
        s.pendingWork = 2 * ratioScale;
    }
    SECTION("invalid timing remainder") { s.timingRemainder = s.timingSpeed; }
    SECTION("unknown phase") { s.phase = static_cast<MachineJobPhase>(2); }
    SECTION("completion error before completion") { s.waitingError = SaveError::Io; }
    SECTION("energy budget overflow") { s.duration = UINT64_MAX - 1; s.energyPerTick = 2; }
    CHECK_FALSE(ValidMachineJobState(s));
    CHECK_FALSE(ReadMachineJobState(WriteMachineJobState(s)));
}
TEST_CASE("Setup paid slices survive due boundaries and worker loss", "[machine-job][save]") {
    auto s = Job();
    SECTION("due paid work") {
        s.pendingWork = 100 * ratioScale;
        s.remainingSetupTicks = 0;
    }
    SECTION("unassigned paid work") {
        s.pendingWork = 100 * ratioScale;
        s.remainingSetupTicks = 50;
        s.worker = 0;
        s.pauseRequested = true;
    }
    SECTION("fractional slice without whole fixed point work") {
        s.timingSpeed = UINT32_MAX;
        s.timingRemainder = 100 * ticks_per_turn * ratioScale;
        s.pendingWork = 0;
        s.remainingSetupTicks = 100;
        s.worker = 0;
    }
    SECTION("upper bound includes previous fractional residue") {
        s.timingSpeed = 123;
        s.pendingWork = (100ull * ticks_per_turn * ratioScale + 122) / 123;
        s.remainingSetupTicks = 100;
    }
    REQUIRE(ValidMachineJobState(s));
    const auto saved = WriteMachineJobState(s);
    const auto loaded = ReadMachineJobState(saved);
    REQUIRE(loaded);
    CHECK(WriteMachineJobState(*loaded) == saved);
}
TEST_CASE("Machine job reader rejects malformed payload without narrowing values", "[machine-job][save]") {
    auto payload = WriteMachineJobState(Job());
    SECTION("unknown field") { payload.SetUnsigned("unknown", 0); }
    SECTION("oversized rate") { payload.SetUnsigned("energy_per_tick", std::uint64_t{UINT32_MAX} + 1); }
    SECTION("oversized phase") { payload.SetUnsigned("phase", 256); }
    SECTION("wrong intent type") { payload.SetUnsigned("run_requested", 0); }
    SECTION("unsupported version") { payload.SetUnsigned("version", 2); }
    SECTION("invalid error") { payload.SetUnsigned("waiting_error", 999); }
    SECTION("missing fields") { payload = DataObject{}; }
    CHECK_FALSE(ReadMachineJobState(payload));
}
TEST_CASE("Selectable behavior leaves setup to actor scheduling and freezes prepaid processing", "[machine-job][object-behavior]") {
    const auto definition = Printer();
    auto created = MakeObjectBehavior(definition);
    REQUIRE(created);
    auto& behavior = **created;
    CHECK_FALSE(behavior.MachineJob());
    CHECK_FALSE(behavior.NextEventIn());
    CHECK(behavior.Describe().gridPowered);
    CHECK_FALSE(behavior.SetEnergy(10));
    auto s = Job();
    s.pendingWork = ratioScale * 100;
    s.remainingSetupTicks = 50;
    REQUIRE(behavior.SetMachineJob(s));
    CHECK_FALSE(behavior.NextEventIn());
    CHECK_FALSE(behavior.FundGridWork(100));
    behavior.Elapse(1000);
    CHECK(behavior.MachineJob()->remainingSetupTicks == 50);
    CHECK(behavior.MachineJob()->pendingWork == ratioScale * 100);
    s = Job();
    Processing(s);
    REQUIRE(behavior.SetMachineJob(s));
    REQUIRE(behavior.FundGridWork(100));
    CHECK_FALSE(behavior.FundGridWork(1));
    REQUIRE(behavior.NextEventIn());
    CHECK(*behavior.NextEventIn() == 100);
    behavior.Elapse(40);
    CHECK(behavior.MachineJob()->remainingWork == 2360);
    behavior.MutableMachineJob()->runRequested = false;
    behavior.Elapse(1000);
    CHECK_FALSE(behavior.NextEventIn());
    CHECK(behavior.MachineJob()->fundedTicks == 60);
    const auto saved = behavior.SaveState();
    auto loaded = MakeObjectBehavior(definition, &saved);
    REQUIRE(loaded);
    CHECK((*loaded)->SaveState() == saved);
    (*loaded)->MutableMachineJob()->runRequested = true;
    (*loaded)->Elapse(std::numeric_limits<Tick>::max());
    CHECK((*loaded)->MachineJob()->remainingWork == 2300);
    CHECK((*loaded)->MachineJob()->fundedTicks == 0);
    CHECK_FALSE((*loaded)->NextEventIn());
    (*loaded)->ClearMachineJob();
    CHECK_FALSE((*loaded)->MachineJob());
}
TEST_CASE("Completion retry survives save and counts down without consuming more power", "[machine-job][save]") {
    const auto definition = Printer();
    auto created = MakeObjectBehavior(definition);
    REQUIRE(created);
    auto s = Job();
    Processing(s);
    s.remainingWork = 0;
    s.waitingError = SaveError::Io;
    s.completionRetryTicks = machineCompletionRetryTicks;
    REQUIRE((*created)->SetMachineJob(s));
    CHECK_FALSE((*created)->FundGridWork(1));
    REQUIRE((*created)->NextEventIn());
    CHECK(*(*created)->NextEventIn() == 100);
    CHECK((*created)->Describe().runRequested);
    (*created)->Elapse(30);
    const auto saved = (*created)->SaveState();
    auto loaded = MakeObjectBehavior(definition, &saved);
    REQUIRE(loaded);
    CHECK(*(*loaded)->NextEventIn() == 70);
    CHECK((*loaded)->MachineJob()->waitingError == SaveError::Io);
    (*loaded)->Elapse(1000);
    CHECK(*(*loaded)->NextEventIn() == 0);
    CHECK((*loaded)->MachineJob()->remainingWork == 0);
}
}
