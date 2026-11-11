#include "objects/object_behavior.h"
#include "objects/interactive_object.h"

#include <catch2/catch_test_macros.hpp>
#include <EASTL/utility.h>
#include <limits>

namespace {
using namespace cataclysmx;

ObjectDefinition Definition() {
    return {"press", "Press", ObjectBehaviorKind::CraftingMachine, {{1}, 2, {2}, 3, 10, 100, 3}};
}

DataObject State(std::uint64_t energy, bool hasJob, bool running, Tick remaining) {
    DataObject state;
    state.SetUnsigned("version", 1);
    state.SetUnsigned("energy", energy);
    state.SetBoolean("has_job", hasJob);
    state.SetBoolean("running", running);
    state.SetUnsigned("remaining_work", remaining);
    return state;
}

TEST_CASE("New typed objects have full energy and no scheduled job", "[object-behavior]") {
    const auto definition = Definition();
    auto created = MakeObjectBehavior(definition);
    REQUIRE(created.has_value());
    REQUIRE(*created != nullptr);
    const auto& behavior = **created;
    CHECK_FALSE(behavior.NextEventIn().has_value());
    const auto view = behavior.Describe();
    CHECK(view.energy == 100);
    CHECK(view.energyCapacity == 100);
    CHECK(view.duration == 10);
    CHECK(view.output == ItemTypeId{2});
    CHECK(view.outputCount == 3);
    CHECK_FALSE(view.hasJob);
    CHECK_FALSE(view.running);
    CHECK(ValidateObjectBehaviorState(definition, behavior.SaveState()).has_value());
}

TEST_CASE("Machine work clamps at whole-tick energy boundary and preserves paused jobs", "[object-behavior]") {
    const auto definition = Definition();
    const auto state = State(14, true, true, 10);
    auto created = MakeObjectBehavior(definition, &state);
    REQUIRE(created.has_value());
    auto& behavior = **created;
    REQUIRE(behavior.NextEventIn().has_value());
    CHECK(*behavior.NextEventIn() == 4);
    behavior.Elapse(std::numeric_limits<Tick>::max());
    const auto paused = behavior.Describe();
    CHECK(paused.energy == 2);
    CHECK(paused.remainingWork == 6);
    CHECK(paused.hasJob);
    CHECK_FALSE(paused.running);
    CHECK_FALSE(behavior.NextEventIn().has_value());
    behavior.Elapse(100);
    CHECK(behavior.Describe().remainingWork == 6);
    REQUIRE(behavior.SetEnergy(100).has_value());
    CHECK_FALSE(behavior.Describe().running);
    const auto saved = behavior.SaveState();
    auto restored = MakeObjectBehavior(definition, &saved);
    REQUIRE(restored.has_value());
    CHECK((*restored)->SaveState() == saved);
    CHECK_FALSE(behavior.SetEnergy(101).has_value());
    CHECK(behavior.Describe().energy == 100);
}

TEST_CASE("Machine completion remains due until processed including at zero power", "[object-behavior]") {
    const auto definition = Definition();
    const auto state = State(12, true, true, 4);
    auto created = MakeObjectBehavior(definition, &state);
    REQUIRE(created.has_value());
    auto& behavior = **created;
    behavior.Elapse(100);
    CHECK(behavior.Describe().energy == 0);
    CHECK(behavior.Describe().hasJob);
    CHECK(behavior.Describe().remainingWork == 0);
    REQUIRE(behavior.NextEventIn().has_value());
    CHECK(*behavior.NextEventIn() == 0);
    const auto saved = behavior.SaveState();
    REQUIRE(ValidateObjectBehaviorState(definition, saved).has_value());
    auto restored = MakeObjectBehavior(definition, &saved);
    REQUIRE(restored.has_value());
    CHECK((*restored)->NextEventIn() == eastl::optional<Tick>{0});
    behavior.Elapse(100);
    CHECK(behavior.SaveState() == saved);
}

TEST_CASE("Energy edits pause incomplete work without discarding its progress", "[object-behavior]") {
    const auto definition = Definition();
    const auto state = State(100, true, true, 10);
    auto created = MakeObjectBehavior(definition, &state);
    REQUIRE(created.has_value());
    auto& behavior = **created;
    behavior.Elapse(2);
    CHECK(behavior.Describe().remainingWork == 8);
    REQUIRE(behavior.SetEnergy(2).has_value());
    CHECK_FALSE(behavior.Describe().running);
    CHECK(behavior.Describe().hasJob);
    CHECK(behavior.Describe().remainingWork == 8);
    CHECK_FALSE(behavior.NextEventIn().has_value());
    const auto saved = behavior.SaveState();
    CHECK(ValidateObjectBehaviorState(definition, saved).has_value());
    REQUIRE(behavior.SetEnergy(3).has_value());
    CHECK_FALSE(behavior.Describe().running);
}

TEST_CASE("Machine state validation rejects unknown versions fields and inconsistent state", "[object-behavior]") {
    const auto definition = Definition();
    DataObject invalid[] = {
        State(101, false, false, 0), State(10, false, true, 0),
        State(10, false, false, 1), State(10, true, false, 11),
        State(2, true, true, 1), DataObject::List(), State(10, true, true, 1),
        State(10, true, true, 1), State(10, true, true, 1), State(10, true, true, 1)
    };
    invalid[6].SetUnsigned("version", 2);
    invalid[7].SetBoolean("extra", false);
    invalid[8].SetUnsigned("has_job", 1);
    invalid[9].SetSigned("remaining_work", 1);
    for (const auto& state : invalid) {
        Diagnostics diagnostics;
        CHECK_FALSE(ValidateObjectBehaviorState(definition, state, &diagnostics).has_value());
        CHECK_FALSE(diagnostics.Empty());
        CHECK_FALSE(MakeObjectBehavior(definition, &state).has_value());
    }
    CHECK(ValidateObjectBehaviorState(definition, invalid[6]).error() == StateError::UnsupportedVersion);
    const auto pausedDue = State(0, true, false, 0);
    CHECK(ValidateObjectBehaviorState(definition, pausedDue).has_value());
    const auto paused = State(100, true, false, 5);
    CHECK(ValidateObjectBehaviorState(definition, paused).has_value());
}

TEST_CASE("Saved machines default safely without restoring a free charge or job", "[object-behavior]") {
    const auto definition = Definition();
    DataObject saved;
    saved.SetUnsigned("version", 1);
    SECTION("all optional fields omitted") {}
    SECTION("explicit zero and false") { saved = State(0, false, false, 0); }
    SECTION("job marker omitted") {
        saved.SetBoolean("running", false);
        saved.SetUnsigned("remaining_work", 0);
    }
    CHECK(ValidateObjectBehaviorState(definition, saved));
    auto restored = MakeObjectBehavior(definition, &saved);
    REQUIRE(restored);
    REQUIRE(*restored);
    const auto view = (*restored)->Describe();
    CHECK(view.energy == 0);
    CHECK_FALSE(view.hasJob);
    CHECK_FALSE(view.running);
    CHECK(view.remainingWork == 0);
    CHECK_FALSE((*restored)->NextEventIn());
    CHECK((*restored)->SaveState() == State(0, false, false, 0));
    auto fresh = MakeObjectBehavior(definition);
    REQUIRE(fresh);
    CHECK((*fresh)->Describe().energy == definition.machine.energyCapacity);
}

TEST_CASE("Saved jobs require explicit remaining work and default to paused unpowered state", "[object-behavior]") {
    const auto definition = Definition();
    DataObject saved;
    saved.SetUnsigned("version", 1);
    saved.SetBoolean("has_job", true);
    SECTION("missing work cannot become free completion") {
        Diagnostics diagnostics;
        const auto result = ValidateObjectBehaviorState(definition, saved, &diagnostics);
        REQUIRE_FALSE(result);
        CHECK(result.error() == StateError::InvalidData);
        REQUIRE(diagnostics.Frames().size() == 1);
        REQUIRE(diagnostics.Frames().front().location);
        CHECK(diagnostics.Frames().front().location->path == "remaining_work");
        CHECK_FALSE(MakeObjectBehavior(definition, &saved));
    }
    SECTION("incomplete work stays paused") {
        saved.SetUnsigned("remaining_work", 5);
        REQUIRE(ValidateObjectBehaviorState(definition, saved));
        auto restored = MakeObjectBehavior(definition, &saved);
        REQUIRE(restored);
        CHECK((*restored)->SaveState() == State(0, true, false, 5));
        CHECK_FALSE((*restored)->NextEventIn());
    }
    SECTION("explicit completed work is still due") {
        saved.SetUnsigned("remaining_work", 0);
        REQUIRE(ValidateObjectBehaviorState(definition, saved));
        auto restored = MakeObjectBehavior(definition, &saved);
        REQUIRE(restored);
        CHECK((*restored)->NextEventIn() == eastl::optional<Tick>{0});
    }
}

TEST_CASE("Optional machine fields reject explicit malformed values consistently", "[object-behavior]") {
    const auto definition = Definition();
    DataObject saved;
    saved.SetUnsigned("version", 1);
    String field;
    StateError expected = StateError::InvalidData;
    SECTION("signed energy") { field = "energy"; saved.SetSigned(field, 0); }
    SECTION("string energy") { field = "energy"; saved.SetString(field, "0"); }
    SECTION("unsigned job flag") { field = "has_job"; saved.SetUnsigned(field, 0); }
    SECTION("string running flag") { field = "running"; saved.SetString(field, "false"); }
    SECTION("signed idle remaining work") { field = "remaining_work"; saved.SetSigned(field, 0); }
    SECTION("missing required version") { field = "version"; saved = DataObject{}; }
    SECTION("wrong version type") { field = "version"; saved.SetBoolean(field, true); }
    SECTION("unsupported version") {
        field = "version"; saved.SetUnsigned(field, 2); expected = StateError::UnsupportedVersion;
    }
    SECTION("unsupported schema is rejected before interpreting its fields") {
        field = "version"; saved.SetUnsigned(field, 2); expected = StateError::UnsupportedVersion;
        saved.SetString("energy", "future representation");
        saved.SetBoolean("has_job", true);
    }
    SECTION("unknown field") { field = "extra"; saved.SetBoolean(field, false); }
    SECTION("running without a job") { field = "running"; saved.SetBoolean(field, true); }
    SECTION("work without a job") { field = "running"; saved.SetUnsigned("remaining_work", 1); }
    const auto original = saved;
    Diagnostics diagnostics;
    const auto validated = ValidateObjectBehaviorState(definition, saved, &diagnostics);
    REQUIRE_FALSE(validated);
    CHECK(validated.error() == expected);
    REQUIRE(diagnostics.Frames().size() == 1);
    REQUIRE(diagnostics.Frames().front().location);
    CHECK(diagnostics.Frames().front().location->path == field);
    Diagnostics factoryDiagnostics;
    const auto created = MakeObjectBehavior(definition, &saved, &factoryDiagnostics);
    REQUIRE_FALSE(created);
    CHECK(created.error() == expected);
    CHECK(FormatDiagnostics(factoryDiagnostics) == FormatDiagnostics(diagnostics));
    CHECK(saved == original);
}

TEST_CASE("Failed saved machine construction leaves an existing machine intact", "[object-behavior]") {
    const auto definition = Definition();
    auto current = MakeObjectBehavior(definition);
    REQUIRE(current);
    const auto before = (*current)->SaveState();
    DataObject malformed;
    malformed.SetUnsigned("version", 1);
    malformed.SetUnsigned("energy", 12);
    malformed.SetBoolean("has_job", true);
    auto replacement = MakeObjectBehavior(definition, &malformed);
    if (replacement) { current = eastl::move(replacement); }
    CHECK_FALSE(replacement);
    CHECK((*current)->SaveState() == before);
}

TEST_CASE("Large elapsed work and energy products stay bounded", "[object-behavior]") {
    auto definition = Definition();
    definition.machine.duration = std::numeric_limits<std::uint32_t>::max();
    definition.machine.energyPerTick = std::numeric_limits<std::uint32_t>::max();
    definition.machine.energyCapacity = std::numeric_limits<std::uint64_t>::max() / 2;
    const auto state = State(definition.machine.energyCapacity, true, true, definition.machine.duration);
    auto created = MakeObjectBehavior(definition, &state);
    REQUIRE(created.has_value());
    auto& behavior = **created;
    const Tick boundary = definition.machine.energyCapacity / definition.machine.energyPerTick;
    CHECK(behavior.NextEventIn() == eastl::optional<Tick>{boundary});
    behavior.Elapse(std::numeric_limits<Tick>::max());
    CHECK(behavior.Describe().remainingWork == definition.machine.duration - boundary);
    CHECK(behavior.Describe().energy == definition.machine.energyCapacity % definition.machine.energyPerTick);
    CHECK_FALSE(behavior.Describe().running);
}

TEST_CASE("Generic objects retain move-only timer compatibility without a behavior", "[object-behavior]") {
    const ObjectDefinition generic{"generic", "Generic", ObjectBehaviorKind::None, {}};
    const DataObject empty;
    auto created = MakeObjectBehavior(generic, &empty);
    REQUIRE(created.has_value());
    CHECK(*created == nullptr);
    CHECK_FALSE(ValidateObjectBehaviorState(generic, State(0, false, false, 0)).has_value());
    InteractiveObject object(42, {1, 2, 3}, true);
    CHECK(object.type == ObjectTypeId{});
    CHECK(object.updateInterval == 100);
    CHECK(object.updateDeadline.Remaining(0) == 100);
    InteractiveObject moved(eastl::move(object));
    CHECK(moved.id == 42);
    CHECK(moved.position == TileCoord{1, 2, 3});
    CHECK(moved.enabled);
}

}
