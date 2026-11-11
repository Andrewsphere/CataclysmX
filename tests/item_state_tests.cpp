#include "core/data_object.h"
#include "core/item_state.h"
#include "core/state_reader.h"
#include "content/content.h"
#include "items/item_registry.h"
#include <catch2/catch_test_macros.hpp>

namespace {
using namespace cataclysmx;

ItemDefinition Device(bool reordered = false) {
    String json = R"({"format_version":1,"definitions":[{"type":"item","id":"device","name":"Device","mass_g":1,"volume_ml":1,"capabilities":[
        {"id":"charger","type":"charger","power_source":"power","battery_class":"aa","energy_per_turn":3},
        {"id":")";
    json += reordered ? "aaa" : "container";
    json += R"(","type":"container","capacity_volume_ml":1000},
        {"id":"power","type":"energy_store","capacity_energy":100,"initial_energy":73,"rechargeable":false},
        {"id":"timer","type":"door_controller","delay_ticks":300}]}]})";
    const ContentSource source{"state-test", {json.data(), json.size()}};
    ItemBehaviorRegistry registry;
    RegisterBuiltinItemBehaviors(registry);
    Diagnostics diagnostics;
    auto catalog = LoadContent({&source, 1}, registry, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str());
    REQUIRE(catalog);
    return catalog->GetItem(catalog->FindItem("device"));
}
DataObject Snapshot(const ItemDefinition& definition, const ItemRuntime& state) {
    DataObject result;
    WriteItemState(definition, state, result);
    return result;
}
DataObject Without(const DataObject& input, eastl::string_view name) {
    DataObject result;
    for (std::size_t i = 0; i < input.Size(); ++i) {
        if (input.Name(i) != name) { result.Set(input.Name(i), input.At(i)); }
    }
    return result;
}

TEST_CASE("Owned structured values preserve exact types and nested JSON", "[item-state][data-object]") {
    DataObject root;
    root.SetBoolean("enabled", true);
    root.SetUnsigned("maximum", UINT64_MAX);
    root.SetSigned("positive", INT64_MAX);
    root.SetSigned("negative", INT64_MIN);
    root.SetSigned("zero", 0);
    root.SetString("text", eastl::string_view("a\0b", 3));
    DataObject list = DataObject::List();
    list.Append(DataObject::Unsigned(1));
    DataObject nested;
    nested.SetString("$i64", "literal user field");
    nested.SetString("$object", "also literal");
    list.Append(nested);
    root.Set("children", list);
    const auto text = EncodeDataObject(root);
    const auto decoded = DecodeDataObject(text);
    REQUIRE(decoded);
    CHECK(*decoded == root);
    CHECK(EncodeDataObject(*decoded) == text);
    CHECK(root.GetUnsigned("absent") == eastl::unexpected(DataError::Absent));
    CHECK(root.GetUnsigned("positive") == eastl::unexpected(DataError::WrongType));
    CHECK(root.GetSigned("maximum") == eastl::unexpected(DataError::WrongType));
    DataObject copy = root;
    copy.SetUnsigned("maximum", 4);
    CHECK(*root.GetUnsigned("maximum") == UINT64_MAX);
    CHECK(copy.Size() == root.Size());
}

TEST_CASE("Structured JSON decoding rejects duplicates and excessive depth without a result", "[item-state][data-object]") {
    for (const char* text : {R"({"x":1,"x":2})", R"({"a":[{"x":true,"x":false}]})",
        R"({"n":18446744073709551616})", R"({"n":1.5})", R"({"n":null})", R"({"$i64":"01"})"}) {
        Diagnostics diagnostics;
        const auto result = DecodeDataObject(text, &diagnostics);
        CHECK_FALSE(result.has_value());
        CHECK_FALSE(diagnostics.Empty());
    }
    String text(65, '['); text += "true"; text.append(65, ']');
    auto result = DecodeDataObject(text);
    REQUIRE_FALSE(result);
    CHECK(result.error() == DataError::DepthLimit);
    text = String(64, '['); text += "true"; text.append(64, ']');
    CHECK(DecodeDataObject(text).has_value());
}

TEST_CASE("Capability state uses stable names and stores only mutable state", "[item-state]") {
    auto definition = Device();
    auto state = MakeItemState(definition);
    auto initial = Snapshot(definition, state);
    REQUIRE(initial.Size() == 3);
    CHECK(*initial.Find("power")->GetUnsigned("energy") == 73);
    initial.Find("charger")->SetBoolean("enabled", true);
    initial.Find("charger")->SetUnsigned("remainder", 99);
    initial.Find("timer")->SetBoolean("armed", true);
    initial.Find("timer")->Find("target")->SetSigned("x", -1);
    initial.Find("timer")->Find("target")->SetSigned("z", 2);
    initial.Find("timer")->Find("target")->SetSigned("y", -3);
    auto modified = ReadItemState(definition, initial);
    REQUIRE(modified);
    state = eastl::move(*modified);
    DataObject saved;
    saved.SetString("stale", "removed by complete write");
    WriteItemState(definition, state, saved);
    CHECK(saved.Size() == 3);
    CHECK(saved.Find("container") == nullptr);
    REQUIRE(saved.Find("power"));
    CHECK(saved.Find("power")->Size() == 2);
    auto decoded = ReadItemState(definition, saved);
    REQUIRE(decoded);
    CHECK(Snapshot(definition, *decoded) == Snapshot(definition, state));
    DataObject reversed;
    for (std::size_t i = saved.Size(); i-- > 0;) { reversed.Set(saved.Name(i), saved.At(i)); }
    auto reordered = ReadItemState(definition, reversed);
    REQUIRE(reordered);
    CHECK(Snapshot(definition, *reordered) == Snapshot(definition, state));
    definition = Device(true); // Moving the stateless name changes charger ID without changing saved names.
    decoded = ReadItemState(definition, saved);
    REQUIRE(decoded);
    CHECK(FindItemCapability(definition, "charger") == ItemCapabilityId{2});
    const auto remapped = Snapshot(definition, *decoded);
    CHECK(*remapped.Find("power")->GetUnsigned("energy") == 73);
    CHECK(*remapped.Find("charger")->GetUnsigned("remainder") == 99);
}

TEST_CASE("State validation rejects missing extra duplicate and mismatched entries", "[item-state]") {
    const auto definition = Device();
    auto state = Snapshot(definition, MakeItemState(definition));
    SECTION("missing") { state = Without(state, "timer"); }
    SECTION("extra") { state.Set("unknown", DataObject{}); }
    SECTION("duplicate shape under wrong identity") { state.Set("duplicate", *state.Find("charger")); }
    SECTION("wrong capability shape") { state.Set("charger", *state.Find("power")); }
    SECTION("energy exceeds current capacity") { state.Find("power")->SetUnsigned("energy", 101); }
    SECTION("fraction overflow") { state.Find("charger")->SetBoolean("enabled", true); state.Find("charger")->SetUnsigned("remainder", 100); }
    SECTION("disabled fraction") { state.Find("charger")->SetUnsigned("remainder", 1); }
    SECTION("disarmed nonzero delay") { state.Find("timer")->SetUnsigned("remaining_ticks", 1); }
    SECTION("disarmed nonzero target") { state.Find("timer")->Find("target")->SetSigned("x", 1); }
    SECTION("armed delay exceeds config") { state.Find("timer")->SetBoolean("armed", true); state.Find("timer")->SetUnsigned("remaining_ticks", 301); }
    SECTION("target out of bounds") { state.Find("timer")->SetBoolean("armed", true); state.Find("timer")->Find("target")->SetSigned("x", world_coordinate_limit + 1); }
    SECTION("target below world") { state.Find("timer")->SetBoolean("armed", true); state.Find("timer")->Find("target")->SetSigned("y", world_min_y - 1); }
    SECTION("target above world") { state.Find("timer")->SetBoolean("armed", true); state.Find("timer")->Find("target")->SetSigned("y", world_max_y + 1); }
    SECTION("missing target elevation") { *state.Find("timer")->Find("target") = Without(*state.Find("timer")->Find("target"), "y"); }
    SECTION("unsigned target elevation") { state.Find("timer")->Find("target")->SetUnsigned("y", 0); }
    CHECK_FALSE(ReadItemState(definition, state).has_value());
}

TEST_CASE("State readers reject malformed data rather than initializing defaults", "[item-state]") {
    const auto definition = Device();
    DataObject saved;
    WriteItemState(definition, MakeItemState(definition), saved);
    StateError expected = StateError::InvalidData;
    SECTION("absent capability") { saved = DataObject{}; }
    SECTION("wrong type") { saved.Find("power")->SetString("energy", "73"); }
    SECTION("unknown capability") { saved.Set("unknown", DataObject{}); }
    SECTION("stateless capability") { saved.Set("container", DataObject{}); }
    SECTION("unknown field") { saved.Find("power")->SetBoolean("extra", true); }
    SECTION("current capacity policy rejects rather than clamps") { saved.Find("power")->SetUnsigned("energy", 101); }
    SECTION("unsupported version") { saved.Find("power")->SetUnsigned("version", 2); expected = StateError::UnsupportedVersion; }
    SECTION("old two-dimensional door state") { saved.Find("timer")->SetUnsigned("version", 1); expected = StateError::UnsupportedVersion; }
    Diagnostics diagnostics;
    auto result = ReadItemState(definition, saved, &diagnostics);
    REQUIRE_FALSE(result);
    CHECK(result.error() == expected);
    REQUIRE_FALSE(diagnostics.Empty());
    REQUIRE(diagnostics.Frames().back().location);
    CHECK(diagnostics.Frames().back().location->path.find("device.state") != String::npos);
    ItemDefinition generic;
    CHECK(Snapshot(generic, MakeItemState(generic)) == DataObject{});
    CHECK(ReadItemState(generic, DataObject{}).has_value());
}

TEST_CASE("Shared state reads default only absent fields and preserve false zero and empty", "[state-reader]") {
    DataObject input;
    input.SetUnsigned("number", 0);
    input.SetBoolean("flag", false);
    input.SetString("text", "");
    StateReader reader(input);
    CHECK(reader.GetUnsignedOrDefault("missing_number", 23) == 23);
    CHECK(reader.GetBooleanOrDefault("missing_flag", true));
    CHECK(reader.GetStringOrDefault("missing_text", "fallback") == "fallback");
    CHECK(reader.GetUnsignedOrDefault("number", 23) == 0);
    CHECK_FALSE(reader.GetBooleanOrDefault("flag", true));
    CHECK(reader.GetStringOrDefault("text", "fallback").empty());
    CHECK(reader.GetUnsignedOrError("number") == 0);
    CHECK_FALSE(reader.GetBooleanOrError("flag"));
    CHECK(reader.GetStringOrError("text").empty());
    CHECK(reader.Result());
}

TEST_CASE("Shared state reads retain the first qualified error before commit", "[state-reader]") {
    DataObject input;
    input.SetSigned("number", 0);
    input.SetUnsigned("flag", 0);
    input.SetBoolean("text", false);
    Diagnostics diagnostics;
    StateReader reader(input, &diagnostics, "device.state.power");
    std::uint64_t committed = 71;
    const auto number = reader.GetUnsignedOrDefault("number", 42);
    reader.GetBooleanOrDefault("flag", false);
    reader.GetStringOrDefault("text", "fallback");
    reader.GetUnsignedOrError("missing");
    const auto result = reader.Result();
    if (result) { committed = number; }
    REQUIRE_FALSE(result);
    CHECK(result.error() == StateError::InvalidData);
    CHECK(committed == 71);
    REQUIRE(diagnostics.Frames().size() == 1);
    REQUIRE(diagnostics.Frames().front().location);
    CHECK(diagnostics.Frames().front().location->path == "device.state.power.number");
    CHECK(reader.Invalid("version", "unsupported", StateError::UnsupportedVersion).error()
        == StateError::InvalidData);
    CHECK(diagnostics.Frames().size() == 1);
}

TEST_CASE("Each optional scalar rejects a present wrong type", "[state-reader]") {
    DataObject input;
    input.Set("value", DataObject::List());
    Diagnostics diagnostics;
    StateReader reader(input, &diagnostics, "saved.state");
    SECTION("unsigned") { reader.GetUnsignedOrDefault("value", 99); }
    SECTION("boolean") { reader.GetBooleanOrDefault("value", true); }
    SECTION("string") { reader.GetStringOrDefault("value", "fallback"); }
    REQUIRE_FALSE(reader.Result());
    CHECK(reader.Result().error() == StateError::InvalidData);
    REQUIRE(diagnostics.Frames().size() == 1);
    REQUIRE(diagnostics.Frames().front().location);
    CHECK(diagnostics.Frames().front().location->path == "saved.state.value");
}

TEST_CASE("Shared state reads require roots and required members", "[state-reader]") {
    DataObject input;
    const DataObject* root = &input;
    String expectedPath = "saved.state.value";
    SECTION("required unsigned is missing") {}
    SECTION("required boolean is missing") { expectedPath = "saved.state.flag"; }
    SECTION("required string is missing") { expectedPath = "saved.state.text"; }
    SECTION("required object is missing") { expectedPath = "saved.state.child"; }
    SECTION("required object has wrong type") {
        input.SetBoolean("child", false);
        expectedPath = "saved.state.child";
    }
    SECTION("root is absent") { root = nullptr; expectedPath = "saved.state"; }
    SECTION("root is wrong type") { input = DataObject::List(); expectedPath = "saved.state"; }
    Diagnostics diagnostics;
    StateReader reader(root, &diagnostics, "saved.state");
    if (expectedPath == "saved.state.flag") { reader.GetBooleanOrError("flag"); }
    else if (expectedPath == "saved.state.text") { reader.GetStringOrError("text"); }
    else if (expectedPath == "saved.state.child") { reader.GetObjectOrError("child"); }
    else { reader.GetUnsignedOrError("value"); }
    REQUIRE_FALSE(reader.Result());
    REQUIRE(diagnostics.Frames().size() == 1);
    REQUIRE(diagnostics.Frames().front().location);
    CHECK(diagnostics.Frames().front().location->path == expectedPath);
    StateReader quiet(root);
    quiet.GetUnsignedOrError("value");
    CHECK_FALSE(quiet.Result());
}

TEST_CASE("Shared semantic diagnostics preserve version status and qualified paths", "[state-reader]") {
    const DataObject input;
    Diagnostics diagnostics;
    StateReader reader(input, &diagnostics, "saved.state");
    REQUIRE(reader.Result());
    reader.Invalid("version", "unsupported state version", StateError::UnsupportedVersion);
    REQUIRE_FALSE(reader.Result());
    CHECK(reader.Result().error() == StateError::UnsupportedVersion);
    REQUIRE(diagnostics.Frames().size() == 1);
    REQUIRE(diagnostics.Frames().front().location);
    CHECK(diagnostics.Frames().front().location->path == "saved.state.version");
}

TEST_CASE("Item action IDs are bounded local ASCII identities", "[item-state]") {
    CHECK(ValidItemActionId("a"));
    CHECK(ValidItemActionId("start_2"));
    CHECK(ValidItemActionId(String(64, 'a')));
    CHECK_FALSE(ValidItemActionId(String(65, 'a')));
    for (const char* id : {"", "1start", "_start", "Start", "start-stop", "start.stop", "start stop", "\xc3\xa9"}) {
        CHECK_FALSE(ValidItemActionId(id));
    }
    CHECK_FALSE(ValidItemActionId(eastl::string_view("a\0b", 3)));
}
}
