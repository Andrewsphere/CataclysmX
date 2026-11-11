#include "actors/ai_controller.h"
#include "core/world.h"
#include "content_fixture.h"
#include <EASTL/array.h>
#include <EASTL/type_traits.h>
#include <catch2/catch_test_macros.hpp>

namespace {
using namespace cataclysmx;

DataObject WanderState() {
    DataObject state;
    state.SetUnsigned("version", 1);
    return state;
}

TEST_CASE("Actors preserve inspection defaults and move their owned values", "[actors][controller]") {
    CHECK_FALSE(eastl::is_copy_constructible<Actor>::value);
    CHECK_FALSE(eastl::is_copy_assignable<Actor>::value);
    CHECK(eastl::is_nothrow_move_constructible<Actor>::value);
    CHECK(eastl::is_nothrow_move_assignable<Actor>::value);
    Actor actor(17, TileCoord{2, 3, -2}, false);
    CHECK(actor.id == 17);
    CHECK(actor.position == TileCoord{2, 3, -2});
    CHECK_FALSE(actor.player);
    CHECK(actor.turns == 0);
    CHECK(actor.speed == 100);
    CHECK(actor.actionDeadline.Remaining(0) == 0);
    CHECK_FALSE(actor.parked);
    CHECK(actor.carryCapacityGrams == 100000);
    CHECK(actor.light == LightEmission{});
    REQUIRE(actor.inventory.empty());
    actor.inventory.push_back(ItemHandle{9});
    Actor moved(eastl::move(actor));
    CHECK(moved.id == 17);
    REQUIRE(moved.inventory.size() == 1);
    CHECK(moved.inventory.front() == ItemHandle{9});
    Actor player(18, {}, true);
    CHECK(player.player);
    player = eastl::move(moved);
    CHECK(player.id == 17);
    CHECK_FALSE(player.player);
    REQUIRE(player.inventory.size() == 1);
    CHECK(player.inventory.front() == ItemHandle{9});
}

TEST_CASE("Wander preserves legacy draws and returns moves even without loaded destinations", "[actors][controller]") {
    World world(TestContent());
    const ActorHandle self{123};
    const ActorReadContext context(world, self);
    CHECK(&context.GetWorld() == &world);
    CHECK(context.Self() == self);
    const auto revision = world.GetRevision();
    auto controller = MakeAiController(AiControllerKind::Wander);
    REQUIRE(controller);
    REQUIRE(*controller);
    CHECK((*controller)->Kind() == AiControllerKind::Wander);
    std::uint32_t rng = 42;
    constexpr eastl::array<std::uint32_t, 9> expectedRng = {
        1083814273U, 378494188U, 2479403867U, 955863294U, 1613448261U,
        110225632U, 1921058495U, 508781842U, 3753001289U
    };
    constexpr TileCoord expectedDirections[] = {
        {0, 1}, {1, 0}, {-1, 0}, {1, 0}, {0, 1},
        {1, 0}, {0, 1}, {1, 0}, {0, -1}
    };
    for (std::size_t i = 0; i < expectedRng.size(); ++i) {
        const auto intent = (*controller)->ChooseAction(context, rng);
        CHECK(rng == expectedRng[i]);
        CHECK(intent.kind == ActorIntentKind::Move);
        CHECK(intent.direction == expectedDirections[i]);
        CHECK(intent.baseCost == 100);
        CHECK(world.GetTile(intent.direction) == nullptr);
    }
    CHECK(world.GetRevision() == revision);
    Vector<ActorHandle> actors;
    world.CollectActiveActors(actors);
    CHECK(actors.empty());
    CHECK(world.CheckInvariants());
}

TEST_CASE("Controller snapshots round trip and resume the same logical decisions", "[actors][controller]") {
    World world(TestContent());
    const ActorReadContext context(world, {});
    auto original = MakeAiController(AiControllerKind::Wander);
    REQUIRE(original);
    std::uint32_t rng = UINT32_MAX;
    (*original)->ChooseAction(context, rng);
    const auto envelope = CaptureAiController(original->get());
    REQUIRE(envelope.Size() == 2);
    CHECK(*envelope.GetString("type") == "wander");
    REQUIRE(envelope.Find("state"));
    CHECK(*envelope.Find("state") == WanderState());
    auto restored = ReadAiController(envelope);
    REQUIRE(restored);
    REQUIRE(*restored);
    CHECK(CaptureAiController(restored->get()) == envelope);
    std::uint32_t restoredRng = rng;
    for (int i = 0; i < 32; ++i) {
        CHECK((*original)->ChooseAction(context, rng) == (*restored)->ChooseAction(context, restoredRng));
        CHECK(rng == restoredRng);
    }
    CHECK(CaptureAiController(original->get()) == envelope);

    auto none = MakeAiController(AiControllerKind::None);
    REQUIRE(none);
    CHECK_FALSE(*none);
    const auto noneEnvelope = CaptureAiController(nullptr);
    CHECK(*noneEnvelope.GetString("type") == "none");
    REQUIRE(noneEnvelope.Find("state"));
    CHECK(*noneEnvelope.Find("state") == DataObject{});
    auto restoredNone = ReadAiController(noneEnvelope);
    REQUIRE(restoredNone);
    CHECK_FALSE(*restoredNone);
}

TEST_CASE("Controller payload validation is strict and typed", "[actors][controller]") {
    DataObject state = WanderState();
    StateError expected = StateError::InvalidData;
    SECTION("missing version") { state = DataObject{}; }
    SECTION("signed version") { state.SetSigned("version", 1); }
    SECTION("text version") { state.SetString("version", "1"); }
    SECTION("boolean version") { state.SetBoolean("version", true); }
    SECTION("extra field") { state.SetUnsigned("extra", 0); }
    SECTION("wrong root") { state = DataObject::List(); }
    SECTION("future version") { state.SetUnsigned("version", 2); expected = StateError::UnsupportedVersion; }
    SECTION("zero version") { state.SetUnsigned("version", 0); expected = StateError::UnsupportedVersion; }
    Diagnostics diagnostics;
    auto valid = ValidateAiControllerState(AiControllerKind::Wander, state, &diagnostics);
    REQUIRE_FALSE(valid);
    CHECK(valid.error() == expected);
    REQUIRE_FALSE(diagnostics.Empty());
    REQUIRE(diagnostics.Frames().back().location);
    CHECK(diagnostics.Frames().back().location->path.find("actors.controller.state") == 0);
    const auto controller = MakeAiController(AiControllerKind::Wander, &state);
    REQUIRE_FALSE(controller);
    CHECK(controller.error() == expected);
}

TEST_CASE("None and controller identities have no permissive fallback", "[actors][controller]") {
    const DataObject empty;
    CHECK(ValidateAiControllerState(AiControllerKind::None, empty));
    CHECK(MakeAiController(AiControllerKind::None, &empty));
    const DataObject nonempty = WanderState();
    CHECK_FALSE(MakeAiController(AiControllerKind::None, &nonempty));
    const DataObject list = DataObject::List();
    CHECK_FALSE(MakeAiController(AiControllerKind::None, &list));
    const auto invalidKind = static_cast<AiControllerKind>(255);
    CHECK_FALSE(MakeAiController(invalidKind));
    CHECK_FALSE(ValidateAiControllerState(invalidKind, empty));
    CHECK(AiControllerKindName(invalidKind).empty());
    for (const auto kind : {AiControllerKind::None, AiControllerKind::Wander}) {
        CHECK(ReadAiControllerKind(AiControllerKindName(kind)) == kind);
    }
    for (const auto name : {"", "Wander", "unknown", "wander "}) {
        CHECK(ReadAiControllerKind(name) == eastl::unexpected(StateError::InvalidData));
    }
    CHECK_FALSE(ReadAiControllerKind(eastl::string_view("wander\0", 7)));
}

TEST_CASE("Controller required reads retain exact diagnostic paths", "[actors][controller]") {
    DataObject envelope;
    envelope.SetString("type", "wander");
    envelope.Set("state", WanderState());
    String expectedPath;
    StateError expected = StateError::InvalidData;
    SECTION("missing version") { *envelope.Find("state") = DataObject{}; expectedPath = "actors.controller.state.version"; }
    SECTION("wrong version type") { envelope.Find("state")->SetSigned("version", 1); expectedPath = "actors.controller.state.version"; }
    SECTION("unsupported version") {
        envelope.Find("state")->SetUnsigned("version", 2);
        expectedPath = "actors.controller.state.version";
        expected = StateError::UnsupportedVersion;
    }
    SECTION("missing state") { envelope = DataObject{}; envelope.SetString("type", "wander"); expectedPath = "actors.controller.state"; }
    SECTION("wrong state root") { envelope.Set("state", DataObject::List()); expectedPath = "actors.controller.state"; }
    SECTION("wrong type field") { envelope.SetBoolean("type", false); expectedPath = "actors.controller.type"; }
    SECTION("unknown kind") { envelope.SetString("type", "future"); expectedPath = "actors.controller.type"; }
    SECTION("empty kind is not missing") { envelope.SetString("type", ""); expectedPath = "actors.controller.type"; }
    SECTION("wrong envelope root") { envelope = DataObject::List(); expectedPath = "actors.controller"; }
    Diagnostics diagnostics;
    const auto restored = ReadAiController(envelope, &diagnostics);
    REQUIRE_FALSE(restored);
    CHECK(restored.error() == expected);
    REQUIRE(diagnostics.Frames().size() == 1);
    REQUIRE(diagnostics.Frames().front().location);
    CHECK(diagnostics.Frames().front().location->path == expectedPath);
}

TEST_CASE("Controller envelopes require exactly type and state", "[actors][controller]") {
    DataObject envelope;
    envelope.SetString("type", "wander");
    envelope.Set("state", WanderState());
    SECTION("unknown type") { envelope.SetString("type", "future"); }
    SECTION("wrong type field") { envelope.SetUnsigned("type", 1); }
    SECTION("wrong state type") { envelope.Set("state", DataObject::List()); }
    SECTION("extra field") { envelope.SetBoolean("extra", true); }
    SECTION("missing type") { envelope = DataObject{}; envelope.Set("state", WanderState()); }
    SECTION("missing state") { envelope = DataObject{}; envelope.SetString("type", "wander"); }
    SECTION("missing state with same field count") {
        envelope = DataObject{};
        envelope.SetString("type", "wander");
        envelope.Set("other", WanderState());
    }
    SECTION("wrong root") { envelope = DataObject::Text("wander"); }
    SECTION("none cannot contain wander state") { envelope.SetString("type", "none"); }
    Diagnostics diagnostics;
    auto result = ReadAiController(envelope, &diagnostics);
    REQUIRE_FALSE(result);
    CHECK(result.error() == StateError::InvalidData);
    CHECK_FALSE(diagnostics.Empty());
}

}
