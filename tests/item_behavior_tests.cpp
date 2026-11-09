#include "client/localization.h"
#include "content/content.h"
#include "core/item_state.h"
#include "core/save_database.h"
#include "core/server.h"
#include "core/simulation.h"
#include "items/item_registry.h"
#include "items/item_snapshot.h"
#include "network/network.h"
#include "network/wire.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <EASTL/shared_ptr.h>
#include <EASTL/utility.h>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <limits>
#include <string>
#include <thread>
#include <sqlite3.h>

namespace {
using namespace cataclysmx;
using namespace cataclysmx::network;

// Entirely test-owned extension: no production action enum, dispatch, or registry entry.
struct CounterConfig {
    Tick duration = 51;
    std::uint32_t cost = 17;
    String lifecycle = "ordinary";
};

ItemUseReason AlreadyStarted() {
    return {"counter_already_started", {"test.counter.unknown.reason", "The counter is already running."}};
}

ItemBehaviorSchema CounterSchema(const CounterConfig& config) {
    ItemBehaviorSchema schema;
    schema.hasActivity = true;
    schema.countdown = true;
    schema.maximumDelay = config.duration;
    schema.implementationVersion = 1;
    schema.actions = {
        {"erase_self", ItemActionArgument::None, ItemActionCategory::Operate, config.cost,
            {"test.counter.unknown.erase", "Erase counter"}},
        {"start", ItemActionArgument::None, ItemActionCategory::Operate, config.cost,
            {"test.counter.unknown.start", "Start counter"}},
        {"stop", ItemActionArgument::None, ItemActionCategory::Operate, config.cost,
            {"test.counter.unknown.stop", "Stop counter"}}
    };
    return schema;
}

// Tile damage is an observable callback log that survives destruction of the
// behavior itself. No global probe, entity pointer, or unsaved handle is needed.
constexpr TileCoord callbackLog{8, 8};
void RecordCallback(ItemMutationContext& context) {
    const Tile* tile = context.Read().GetWorld().GetTile(callbackLog);
    REQUIRE(tile);
    Tile changed = *tile;
    REQUIRE(changed.damage < 255);
    ++changed.damage;
    REQUIRE(context.SetTile(callbackLog, changed));
}

class CounterBehavior final : public ItemBehavior {
public:
    CounterBehavior(ItemCapabilityId self, CounterConfig config, bool enabled, Tick remaining,
        std::uint64_t count, String note, DataObject history)
        : ItemBehavior(self), config_(eastl::move(config)), enabled_(enabled), remaining_(remaining),
          count_(count), note_(eastl::move(note)), history_(eastl::move(history)) {}

    void QueryActions(const ItemReadContext&, ActorHandle, Vector<ItemActionInfo>& output) const override {
        for (const auto& action : CounterSchema(config_).actions) {
            const auto reason = action.id == "start" && enabled_ ? AlreadyStarted() : ItemUseReason{};
            output.push_back({Capability(), action.id, action.argument,
                reason.code.empty() ? ItemUseOutcome::Succeeded : ItemUseOutcome::Refused,
                action.baseCost, action.category, action.label, reason});
        }
    }
    ItemUseResult Use(ItemMutationContext& context, ActorHandle, const ItemUseRequest& request) override {
        if (request.action == "start") {
            if (enabled_) { return {ItemUseOutcome::Refused, 0, AlreadyStarted()}; }
            enabled_ = true;
            remaining_ = config_.duration;
        } else if (request.action == "stop") {
            enabled_ = false;
            remaining_ = 0;
        } else if (request.action == "erase_self") {
            RequestSelf(context);
        } else { return {}; }
        return {ItemUseOutcome::Succeeded, config_.cost, {}};
    }
    eastl::optional<Tick> NextEventIn(const ItemReadContext&) const override {
        return enabled_ ? eastl::optional<Tick>{remaining_} : eastl::nullopt;
    }
    void Elapse(ItemMutationContext& context, Tick elapsed) override {
        if (config_.lifecycle == "observe_elapse") { RecordCallback(context); }
        if (config_.lifecycle == "elapse_self") { RequestSelf(context); }
        if (config_.lifecycle == "elapse_sibling") { RequestVictim(context); }
        remaining_ = elapsed >= remaining_ ? 0 : remaining_ - elapsed;
    }
    void ProcessEvent(ItemMutationContext& context) override {
        REQUIRE(enabled_);
        REQUIRE(remaining_ == 0);
        RecordCallback(context);
        enabled_ = false;
        ++count_;
        note_ = "completed";
        history_.Append(DataObject::Unsigned(count_));
        if (config_.lifecycle == "event_energy") {
            const auto& read = context.Read();
            const auto& world = read.GetWorld();
            const auto* item = world.GetItem(read.Self());
            REQUIRE(item);
            const auto power = FindItemCapability(world.GetContent().GetItem(item->type), "power");
            REQUIRE(power.value);
            REQUIRE(context.SetEnergy({read.Self(), power}, 1999));
        }
        if (config_.lifecycle == "event_self") { RequestSelf(context); }
        if (config_.lifecycle == "event_sibling") { RequestVictim(context); }
    }
    void OnChange(ItemMutationContext& context, const ItemChange& change) override {
        if (config_.lifecycle == "energy_listener" && change.kind == ItemChangeKind::EnergyChanged
            && change.item == context.Read().Self()) {
            enabled_ = true;
            remaining_ = 0;
            note_ = "energy notification";
        }
        if (enabled_ && config_.lifecycle == "move_self" && change.kind == ItemChangeKind::Moved
            && change.item == context.Read().Self()) {
            RecordCallback(context);
            RequestSelf(context);
        }
    }
    DataObject SaveState() const override {
        DataObject state;
        state.SetUnsigned("version", 1);
        state.SetBoolean("enabled", enabled_);
        state.SetUnsigned("remaining_ticks", remaining_);
        state.SetUnsigned("count", count_);
        state.SetString("note", note_);
        state.Set("history", history_);
        return state;
    }
    ItemBehaviorView Describe(const ItemReadContext&) const override {
        return {Capability(), enabled_, remaining_, {"test.counter.unknown.status", "Counter status"}};
    }
private:
    CounterConfig config_;
    bool enabled_ = false;
    Tick remaining_ = 0;
    std::uint64_t count_ = 0;
    String note_;
    DataObject history_;

    void RequestSelf(ItemMutationContext& context) {
        const auto self = context.Read().Self();
        context.RequestDestroy(self);
        context.RequestDestroy(self); // Requests are idempotent within a callback.
        REQUIRE(context.Read().GetWorld().GetItem(self)); // Still alive until callback returns.
        note_ = "destruction requested"; // Exercise callback-local lifetime after the request.
    }
    void RequestVictim(ItemMutationContext& context) {
        const auto& world = context.Read().GetWorld();
        const auto victims = world.ItemsAt({1, 1});
        for (const auto handle : victims) {
            const auto* item = world.GetItem(handle);
            if (item && world.GetContent().GetItem(item->type).id == "victim") {
                context.RequestDestroy(handle);
            }
        }
    }
};

class CounterDefinition final : public ItemBehaviorDefinition {
public:
    explicit CounterDefinition(CounterConfig config)
        : ItemBehaviorDefinition(CounterSchema(config)), config_(eastl::move(config)) {}

    StateResult<eastl::unique_ptr<ItemBehavior>> Create(ItemCapabilityId self,
        const DataObject* archive, Diagnostics* diagnostics) const override {
        bool enabled = false;
        Tick remaining = 0;
        std::uint64_t count = 0;
        String note = "new counter";
        DataObject history = DataObject::List();
        if (archive) {
            const auto version = archive->GetUnsigned("version");
            if (version && *version != 1) {
                if (diagnostics) { diagnostics->Add("Unsupported counter state version"); }
                return eastl::unexpected(StateError::UnsupportedVersion);
            }
            const auto e = archive->GetBoolean("enabled");
            const auto r = archive->GetUnsigned("remaining_ticks");
            const auto c = archive->GetUnsigned("count");
            const auto n = archive->GetString("note");
            const auto h = archive->Get("history", DataType::List);
            bool valid = archive->Type() == DataType::Object && archive->Size() == 6
                && version && e && r && c && n && h;
            if (valid) {
                valid = *r <= config_.duration && (*e || *r == 0) && n->size() <= 64
                    && (*h)->Size() <= 64 && *c == (*h)->Size();
                for (std::size_t i = 0; valid && i < (*h)->Size(); ++i) {
                    valid = (*h)->At(i).Type() == DataType::Unsigned
                        && (*h)->At(i).UnsignedValue() == i + 1;
                }
            }
            if (!valid) {
                if (diagnostics) { diagnostics->Add("Invalid counter state"); }
                return eastl::unexpected(StateError::InvalidData);
            }
            enabled = *e; remaining = *r; count = *c;
            note.assign(n->data(), n->size()); history = **h;
        }
        return eastl::unique_ptr<ItemBehavior>(new CounterBehavior(self, config_, enabled,
            remaining, count, eastl::move(note), eastl::move(history)));
    }
    StateResult<void> ValidateSnapshot(const ItemSnapshotContext& context, PersistentId self,
        ItemCapabilityId capability, Diagnostics* diagnostics) const override {
        const auto* state = context.State(self, capability);
        if (!state) { return eastl::unexpected(StateError::InvalidData); }
        auto parsed = Create(capability, state, diagnostics);
        if (!parsed) { return eastl::unexpected(parsed.error()); }
        // This rule deliberately depends on the archive graph, not local state.
        // Create accepts the same armed state regardless of its owner's location.
        if (config_.lifecycle == "ground_only" && *state->GetBoolean("enabled")) {
            const auto* item = context.FindItem(self);
            if (!item || item->location != LocationKind::ground) {
                if (diagnostics) { diagnostics->Add("Armed test counter requires a direct ground root"); }
                return eastl::unexpected(StateError::InvalidData);
            }
        }
        return {};
    }
private:
    CounterConfig config_;
};

ContentResult<ParsedItemBehavior> ParseCounter(const DataObject& input,
    const ItemDefinition&, ItemCapabilityId, Diagnostics* diagnostics) {
    const auto duration = input.GetUnsigned("duration_ticks");
    const auto cost = input.GetUnsigned("action_cost");
    const auto lifecycle = input.GetString("lifecycle");
    if (!duration || !*duration || *duration > 10000 || !cost || !*cost || *cost > UINT32_MAX
        || !lifecycle) {
        if (diagnostics) { diagnostics->Add("Invalid test counter configuration"); }
        return eastl::unexpected(ContentError::InvalidData);
    }
    CounterConfig config{*duration, static_cast<std::uint32_t>(*cost),
        String(lifecycle->data(), lifecycle->size())};
    DataObject stable;
    stable.SetUnsigned("duration_ticks", config.duration);
    stable.SetUnsigned("action_cost", config.cost);
    stable.SetString("lifecycle", config.lifecycle);
    return ParsedItemBehavior{eastl::allocate_shared<CounterDefinition>(FatalAllocator{}, config), eastl::move(stable)};
}

constexpr char behaviorContent[] = R"({format_version:1,definitions:[
    {type:'terrain',id:'floor',name:'Floor',movement:{passable:true},blocks_sight:false,
      appearance:{symbol:'.',foreground:'white'}},
    {type:'terrain',id:'wall',name:'Wall',movement:{passable:false},blocks_sight:true,
      appearance:{symbol:'#',foreground:'white'}},
    {type:'terrain',id:'closed_door',name:'Closed door',movement:{passable:false},blocks_sight:true,
      opens_to:'open_door',appearance:{symbol:'+',foreground:'white'}},
    {type:'terrain',id:'open_door',name:'Open door',movement:{passable:true},blocks_sight:false,
      closes_to:'closed_door',appearance:{symbol:"'",foreground:'white'}},
    {type:'item',id:'rock',name:'Rock',mass_g:1,volume_ml:1},
    {type:'item',id:'bandage',name:'Bandage',mass_g:1,volume_ml:1},
    {type:'item',id:'backpack',name:'Backpack',mass_g:1,volume_ml:1,capabilities:[
      {id:'storage',type:'container',capacity_volume_ml:10000}]},
    {type:'item',id:'battery',name:'Battery',mass_g:1,volume_ml:1,capabilities:[
      {id:'cell',type:'energy_store',capacity_energy:1000,initial_energy:0,rechargeable:true,battery_class:'aa'}]},
    {type:'item',id:'device',name:'Composite device',mass_g:1,volume_ml:1,capabilities:[
      {id:'power',type:'energy_store',capacity_energy:2000,initial_energy:2000,rechargeable:false},
      {id:'storage',type:'container',capacity_volume_ml:1000},
      {id:'charger',type:'charger',power_source:'power',battery_class:'aa',energy_per_turn:3},
      {id:'timer',type:'door_controller',delay_ticks:300},
      {id:'counter',type:'counter',duration_ticks:51,action_cost:17,lifecycle:'ordinary'},
      {id:'counter_other',type:'counter',duration_ticks:79,action_cost:23,lifecycle:'ordinary'}]},
    {type:'item',id:'killer',name:'Lifecycle counter',mass_g:1,volume_ml:1,capabilities:[
      {id:'a_killer',type:'counter',duration_ticks:51,action_cost:17,lifecycle:'event_self'},
      {id:'storage',type:'container',capacity_volume_ml:1000},
      {id:'z_observer',type:'counter',duration_ticks:51,action_cost:17,lifecycle:'ordinary'}]},
    {type:'item',id:'victim',name:'Victim counter',mass_g:1,volume_ml:1,capabilities:[
      {id:'counter',type:'counter',duration_ticks:51,action_cost:17,lifecycle:'ordinary'}]}
]})";

ContentCatalog BehaviorCatalog(eastl::string_view text = behaviorContent) {
    ItemBehaviorRegistry registry;
    RegisterBuiltinItemBehaviors(registry);
    REQUIRE(registry.Add("counter", ParseCounter));
    const ContentSource source{"test-behavior.json5", text};
    Diagnostics diagnostics;
    auto loaded = LoadContent({&source, 1}, registry, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str());
    REQUIRE(loaded);
    return eastl::move(*loaded); // Registry, source text and diagnostics are not retained.
}

String Replace(String text, eastl::string_view from, eastl::string_view to) {
    const auto position = text.find(from.data(), 0, from.size());
    REQUIRE(position != String::npos);
    text.replace(position, from.size(), to.data(), to.size());
    return text;
}

template<class T> T Take(WorldResult<T> result) {
    REQUIRE(result);
    return eastl::move(*result);
}
LoadRequest Load(World& world) {
    const auto request = Take(world.RequestChunks({{0, 0}, 0}, LoadMode::active));
    REQUIRE(world.ReconcileChunks());
    REQUIRE(world.SetTile({1, 1}, {world.GetContent().FindTerrain("floor"), 0}));
    REQUIRE(world.SetTile(callbackLog, {world.GetContent().FindTerrain("floor"), 0}));
    return request;
}
ItemCapabilityId Capability(const World& world, ItemHandle item, const char* name) {
    REQUIRE(world.GetItem(item));
    const auto result = FindItemCapability(world.GetContent().GetItem(world.GetItem(item)->type), name);
    REQUIRE(result.value);
    return result;
}
ItemUseResult Use(World& world, ActorHandle actor, ItemHandle item, const char* capability,
    const char* action, ItemHandle other = {}, int dx = 0, int dz = 0) {
    return world.UseItem(actor, item, {Capability(world, item, capability), action, other, dx, dz});
}
void Start(World& world, ActorHandle actor, ItemHandle item, const char* capability) {
    REQUIRE(Use(world, actor, item, capability, "start").outcome == ItemUseOutcome::Succeeded);
}
Command UseCommand(const World& world, ItemHandle item, const char* capability, const char* action) {
    Command command;
    command.kind = CommandKind::use_item;
    command.item = world.GetItem(item)->id;
    command.capability = Capability(world, item, capability);
    command.itemAction = action;
    return command;
}
DataObject State(const World& world, ItemHandle item, const char* capability) {
    const auto snapshot = world.CaptureItemState(item);
    const auto* state = snapshot.Find(capability);
    REQUIRE(state);
    return *state;
}
std::uint64_t Unsigned(const DataObject& object, const char* field) {
    const auto value = object.GetUnsigned(field);
    REQUIRE(value);
    return *value;
}
void Advance(World& world, Tick elapsed) {
    world.ElapseTicks(elapsed);
    world.ProcessItemEvents();
}
void RemoveNpcs(World& world) {
    for (const auto handle : world.ActiveActors()) {
        if (!world.GetActor(handle)->player) { REQUIRE(world.DestroyActor(handle)); }
    }
}

struct CompositeRig {
    ContentCatalog content = BehaviorCatalog();
    World world{content};
    LoadRequest request = Load(world);
    ActorHandle actor = Take(world.CreateActor({1, 1}, true));
    ItemHandle device = Take(world.CreateItem(content.FindItem("device"), ItemLocation::Ground({1, 1})));
    ItemHandle battery = Take(world.CreateItem(content.FindItem("battery"), ItemLocation::Carried(actor)));
    CompositeRig() {
        REQUIRE(world.SetTile({2, 1}, {content.FindTerrain("closed_door"), 7}));
        REQUIRE(Use(world, actor, device, "charger", "install", battery).outcome == ItemUseOutcome::Succeeded);
        Start(world, actor, device, "charger");
        Start(world, actor, device, "counter");
        REQUIRE(Use(world, actor, device, "timer", "arm", {}, 1, 0).outcome == ItemUseOutcome::Succeeded);
    }
};

struct TemporarySave {
    std::filesystem::path directory;
    std::string path;
    TemporarySave() {
        static std::atomic<unsigned> sequence{0};
        directory = std::filesystem::temp_directory_path() / ("cataclysmx-behavior-"
            + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
            + "-" + std::to_string(sequence.fetch_add(1)));
        REQUIRE(std::filesystem::create_directory(directory));
        path = (directory / "world.sqlite").string();
    }
    ~TemporarySave() { std::error_code error; std::filesystem::remove_all(directory, error); }
};

ContentCatalog EnergyCascadeCatalog() {
    auto text = Replace(behaviorContent,
        "id:'counter',type:'counter',duration_ticks:51,action_cost:17,lifecycle:'ordinary'",
        "id:'counter',type:'counter',duration_ticks:151,action_cost:17,lifecycle:'energy_listener'");
    text = Replace(eastl::move(text),
        "id:'counter_other',type:'counter',duration_ticks:79,action_cost:23,lifecycle:'ordinary'",
        "id:'counter_other',type:'counter',duration_ticks:51,action_cost:23,lifecycle:'event_energy'");
    return BehaviorCatalog(text);
}

struct EnergyCascadeRig {
    ContentCatalog content = EnergyCascadeCatalog();
    World world{content};
    LoadRequest request = Load(world);
    ActorHandle actor = Take(world.CreateActor({1, 1}, true));
    ItemHandle device = Take(world.CreateItem(content.FindItem("device"), ItemLocation::Ground({1, 1})));

    explicit EnergyCascadeRig(bool listenerInitiallyScheduled) {
        REQUIRE(Capability(world, device, "counter").value < Capability(world, device, "counter_other").value);
        if (listenerInitiallyScheduled) { Start(world, actor, device, "counter"); }
        Start(world, actor, device, "counter_other");
        world.ElapseTicks(51); // B is due; A is absent or will be visited first with a positive delay.
        REQUIRE(Unsigned(State(world, device, "counter"), "remaining_ticks") == (listenerInitiallyScheduled ? 100 : 0));
        REQUIRE(State(world, device, "counter").GetBoolean("enabled") == listenerInitiallyScheduled);
        REQUIRE(Unsigned(State(world, device, "counter_other"), "remaining_ticks") == 0);
        REQUIRE(Unsigned(State(world, device, "counter"), "count") == 0);
        REQUIRE(Unsigned(State(world, device, "counter_other"), "count") == 0);
        REQUIRE(world.GetTile(callbackLog)->damage == 0);
    }

    void CheckDrained() const {
        const auto listener = State(world, device, "counter");
        const auto trigger = State(world, device, "counter_other");
        CHECK(Unsigned(listener, "count") == 1);
        CHECK(Unsigned(trigger, "count") == 1);
        CHECK(listener.GetBoolean("enabled") == false);
        CHECK(trigger.GetBoolean("enabled") == false);
        CHECK(listener.GetString("note") == eastl::string_view("completed"));
        REQUIRE(listener.Find("history"));
        REQUIRE(trigger.Find("history"));
        REQUIRE(listener.Find("history")->Size() == 1);
        REQUIRE(trigger.Find("history")->Size() == 1);
        CHECK(listener.Find("history")->At(0) == DataObject::Unsigned(1));
        CHECK(trigger.Find("history")->At(0) == DataObject::Unsigned(1));
        CHECK(Unsigned(State(world, device, "power"), "energy") == 1999);
        CHECK(world.GetTile(callbackLog)->damage == 2);
        CHECK_FALSE(world.NextItemEventIn());
        CHECK(world.ActiveItemCount() == 0);
        CHECK(world.CheckInvariants());
    }
};

TEST_CASE("ProcessItemEvents drains earlier callbacks made due by a later energy notification", "[item-behavior][scheduling]") {
    const bool initiallyScheduled = GENERATE(false, true);
    CAPTURE(initiallyScheduled);
    EnergyCascadeRig rig(initiallyScheduled);
    // A's OnChange is queued by B's SetEnergy inside ProcessEvent. One call must
    // reach the fixed point even when A wasn't in the captured scheduled list.
    rig.world.ProcessItemEvents();
    rig.CheckDrained();
    const auto completed = rig.world.CaptureItemState(rig.device);
    rig.world.ProcessItemEvents();
    CHECK(rig.world.CaptureItemState(rig.device) == completed);
    CHECK(rig.world.GetTile(callbackLog)->damage == 2);
}

TEST_CASE("Simulation drains notification scheduled events at the same tick before input or limits", "[item-behavior][scheduling][simulation]") {
    const bool initiallyScheduled = GENERATE(false, true);
    const bool readyPlayer = GENERATE(false, true);
    CAPTURE(initiallyScheduled, readyPlayer);
    EnergyCascadeRig rig(initiallyScheduled);
    if (!readyPlayer) { REQUIRE(rig.world.ScheduleActorAction(rig.actor, 1000)); }
    Tick tick = 51;
    std::uint32_t rng = 123;
    const auto result = AdvanceUntilInput(rig.world, tick, rng, 51);
    CHECK(tick == 51);
    CHECK(rng == 123);
    CHECK(result.stop == (readyPlayer ? SimulationStop::InputRequired : SimulationStop::LimitReached));
    if (readyPlayer) {
        REQUIRE(result.readyPlayers.size() == 1);
        CHECK(result.readyPlayers[0] == rig.actor);
    } else {
        CHECK(result.readyPlayers.empty());
        CHECK(result.nextEventIn == 1000);
    }
    rig.CheckDrained();
    const auto completed = rig.world.CaptureItemState(rig.device);
    CHECK(AdvanceUntilInput(rig.world, tick, rng, 51).stop == result.stop);
    CHECK(tick == 51);
    CHECK(rig.world.CaptureItemState(rig.device) == completed);
    CHECK(rig.world.GetTile(callbackLog)->damage == 2);
}

TEST_CASE("Explicit behavior definitions own configuration beyond registry lifetime", "[item-behavior][content]") {
    const auto content = BehaviorCatalog();
    const auto copy = content;
    const auto independent = BehaviorCatalog();
    const auto type = content.FindItem("device");
    CHECK(copy.GetItem(type) == content.GetItem(type));
    CHECK(independent.GetItem(type) == content.GetItem(type));
    CHECK(ContentFingerprint(copy) == ContentFingerprint(content));
    CHECK(ContentFingerprint(independent) == ContentFingerprint(content));
    const auto changed = BehaviorCatalog(Replace(behaviorContent, "duration_ticks:51", "duration_ticks:52"));
    CHECK_FALSE(changed.GetItem(type) == content.GetItem(type));
    CHECK(ContentFingerprint(changed) != ContentFingerprint(content));
    const auto& definition = copy.GetItem(type);
    auto initialized = MakeItemState(definition);
    DataObject archive;
    WriteItemState(definition, initialized, archive);
    REQUIRE(archive.Find("counter"));
    CHECK(Unsigned(*archive.Find("counter"), "version") == 1);
    CHECK(archive.Find("counter")->GetBoolean("enabled") == false);
    auto recreated = ReadItemState(definition, archive);
    REQUIRE(recreated);
    DataObject roundtrip;
    WriteItemState(definition, *recreated, roundtrip);
    CHECK(roundtrip == archive);
    ItemBehaviorRegistry builtins;
    RegisterBuiltinItemBehaviors(builtins);
    CHECK(builtins.Find("counter") == nullptr);
}

TEST_CASE("Multiple virtual states on one item remain independent with sparse activity", "[item-behavior][world]") {
    CompositeRig rig;
    auto& world = rig.world;
    for (int i = 0; i < 64; ++i) {
        Take(world.CreateItem(rig.content.FindItem("device"), ItemLocation::Ground({1, 1})));
    }
    CHECK(world.ActiveItemCount() == 1); // Three scheduled behaviors, one active item, 64 passive devices.
    CHECK(world.NextItemEventIn() == eastl::optional<Tick>{51});
    const auto before = world.CaptureItemState(rig.device);
    Start(world, rig.actor, rig.device, "counter_other");
    const auto after = world.CaptureItemState(rig.device);
    CHECK(*before.Find("charger") == *after.Find("charger"));
    CHECK(*before.Find("power") == *after.Find("power"));
    CHECK(*before.Find("timer") == *after.Find("timer"));
    CHECK(*before.Find("counter") == *after.Find("counter"));
    CHECK_FALSE(*before.Find("counter_other") == *after.Find("counter_other"));
    Advance(world, 17);
    CHECK(Unsigned(State(world, rig.device, "counter"), "remaining_ticks") == 34);
    CHECK(Unsigned(State(world, rig.device, "counter_other"), "remaining_ticks") == 62);
    CHECK(Unsigned(State(world, rig.device, "timer"), "remaining_ticks") == 283);
    CHECK(Unsigned(State(world, rig.device, "charger"), "remainder") == 51);
    const auto presentation = world.DescribeItem(rig.actor, rig.device);
    CHECK(presentation.energy.size() == 1);
    CHECK(presentation.activities.size() == 4);
    Localization localization;
    bool found = false;
    for (const auto& action : presentation.actions) {
        if (action.capability == Capability(world, rig.device, "counter") && action.action == "start") {
            found = true;
            CHECK(action.argument == ItemActionArgument::None);
            CHECK(action.category == ItemActionCategory::Operate);
            CHECK(action.baseCost == 17);
            CHECK(action.label.key == "test.counter.unknown.start");
            CHECK(action.label.fallback == "Start counter");
            CHECK(localization.ItemActionText(action) == "Start counter");
            CHECK(action.availability == ItemUseOutcome::Refused);
            CHECK(action.reason == AlreadyStarted());
            CHECK(localization.ItemRefusal(action.availability, action.reason) == "The counter is already running.");
        }
    }
    REQUIRE(found);
    const auto custom = State(world, rig.device, "counter");
    REQUIRE(Use(world, rig.actor, rig.device, "charger", "stop").outcome == ItemUseOutcome::Succeeded);
    CHECK(State(world, rig.device, "counter") == custom);
    REQUIRE(Use(world, rig.actor, rig.device, "timer", "disarm").outcome == ItemUseOutcome::Succeeded);
    REQUIRE(Use(world, rig.actor, rig.device, "counter_other", "stop").outcome == ItemUseOutcome::Succeeded);
    CHECK(world.ActiveItemCount() == 1);
    Advance(world, 34);
    CHECK(Unsigned(State(world, rig.device, "counter"), "count") == 1);
    CHECK(world.ActiveItemCount() == 0);
    CHECK_FALSE(world.NextItemEventIn());
    CHECK(world.CheckInvariants());
}

TEST_CASE("Custom countdown and built in charging are partition invariant", "[item-behavior][world]") {
    CompositeRig whole;
    CompositeRig partitioned;
    Start(whole.world, whole.actor, whole.device, "counter_other");
    Start(partitioned.world, partitioned.actor, partitioned.device, "counter_other");
    Advance(whole.world, 137);
    for (int i = 0; i < 137; ++i) { Advance(partitioned.world, 1); }
    CHECK(whole.world.CaptureItemState(whole.device) == partitioned.world.CaptureItemState(partitioned.device));
    CHECK(whole.world.CaptureItemState(whole.battery) == partitioned.world.CaptureItemState(partitioned.battery));
    CHECK(Unsigned(State(whole.world, whole.device, "counter"), "count") == 1);
    CHECK(Unsigned(State(whole.world, whole.device, "counter_other"), "count") == 1);
    CHECK(Unsigned(State(whole.world, whole.device, "charger"), "remainder") == 11);
    CHECK(Unsigned(State(whole.world, whole.battery, "cell"), "energy") == 4);
    CHECK(Unsigned(State(whole.world, whole.device, "power"), "energy") == 1996);
    CHECK(whole.world.GetTile(callbackLog)->damage == 2);
    CHECK(partitioned.world.GetTile(callbackLog)->damage == 2);
    CHECK(whole.world.CheckInvariants());
    CHECK(partitioned.world.CheckInvariants());
}

TEST_CASE("Custom behavior clocks freeze for resident chunks and parked owners", "[item-behavior][world]") {
    CompositeRig rig;
    Advance(rig.world, 17);
    REQUIRE(rig.world.UpdateRequest(rig.request, {{0, 0}, 0}, LoadMode::resident));
    REQUIRE(rig.world.ReconcileChunks());
    const auto frozen = rig.world.CaptureItemState(rig.device);
    Advance(rig.world, std::numeric_limits<Tick>::max());
    CHECK(rig.world.CaptureItemState(rig.device) == frozen);
    CHECK_FALSE(rig.world.NextItemEventIn());
    REQUIRE(rig.world.UpdateRequest(rig.request, {{0, 0}, 0}, LoadMode::active));
    REQUIRE(rig.world.ReconcileChunks());
    CHECK(rig.world.NextItemEventIn() == eastl::optional<Tick>{34});
    REQUIRE(rig.world.TransferItem(rig.device, ItemLocation::Carried(rig.actor)));
    const auto carried = rig.world.CaptureItemState(rig.device); // Moving the owner cancels only the door attempt.
    REQUIRE(rig.world.SetActorParked(rig.actor, true));
    Advance(rig.world, 10000);
    CHECK(rig.world.CaptureItemState(rig.device) == carried);
    CHECK(rig.world.ActiveItemCount() == 1);
    CHECK_FALSE(rig.world.NextItemEventIn());
    REQUIRE(rig.world.SetActorParked(rig.actor, false));
    Advance(rig.world, 34);
    CHECK(Unsigned(State(rig.world, rig.device, "counter"), "count") == 1);
    CHECK(Unsigned(State(rig.world, rig.device, "charger"), "remainder") == 53);
    CHECK(rig.world.CheckInvariants());
}

TEST_CASE("Unload reload reconstructs custom state and scheduling with fresh handles", "[item-behavior][world][save]") {
    CompositeRig rig;
    Advance(rig.world, 17);
    const auto id = rig.world.GetItem(rig.device)->id;
    const auto batteryId = rig.world.GetItem(rig.battery)->id;
    const auto frozen = rig.world.CaptureItemState(rig.device);
    for (int cycle = 0; cycle < 2; ++cycle) {
        const auto old = rig.device;
        REQUIRE(rig.world.ReleaseRequest(rig.request));
        REQUIRE(rig.world.ReconcileChunks());
        CHECK(rig.world.GetItem(old) == nullptr);
        CHECK(rig.world.ActiveItemCount() == 0);
        Advance(rig.world, 10000);
        rig.request = Load(rig.world);
        rig.device = rig.world.FindItem(id);
        rig.battery = rig.world.FindItem(batteryId);
        REQUIRE(rig.world.GetItem(rig.device));
        CHECK(rig.device != old);
        CHECK(rig.world.CaptureItemState(rig.device) == frozen);
        CHECK(rig.world.NextItemEventIn() == eastl::optional<Tick>{34});
        CHECK(rig.world.ActiveItemCount() == 1);
        CHECK(rig.world.CheckInvariants());
    }
    Advance(rig.world, 34);
    CHECK(Unsigned(State(rig.world, rig.device, "counter"), "count") == 1);
}

TEST_CASE("Server routes custom string actions and charges their configured cost", "[item-behavior][server]") {
    const auto content = BehaviorCatalog();
    REQUIRE(ValidateServerContent(content));
    Server server(content);
    auto& world = server.GetWorld();
    RemoveNpcs(world);
    const auto device = Take(world.CreateItem(content.FindItem("device"), ItemLocation::Carried(server.GetPlayer())));
    const auto start = UseCommand(world, device, "counter", "start");
    REQUIRE(ValidCommandShape(start));
    const auto used = server.Send(start);
    CHECK(used.outcome == Outcome::used_item);
    CHECK(used.itemOutcome == ItemUseOutcome::Succeeded);
    CHECK(used.cost == 17);
    CHECK(used.advanced);
    CHECK(server.GetTick() == 17);
    CHECK(Unsigned(State(world, device, "counter"), "remaining_ticks") == 34);
    const auto before = world.CaptureItemState(device);
    const auto refused = server.Send(start);
    CHECK(refused.outcome == Outcome::item_use_failed);
    CHECK(refused.itemOutcome == ItemUseOutcome::Refused);
    CHECK(refused.itemReason == AlreadyStarted());
    CHECK_FALSE(refused.advanced);
    CHECK(refused.cost == 0);
    CHECK(server.GetTick() == 17);
    CHECK(world.CaptureItemState(device) == before);
    CHECK(server.Send(UseCommand(world, device, "counter", "stop")).cost == 17);
    CHECK(world.ActiveItemCount() == 0);
    CHECK(world.CheckInvariants());
}

TEST_CASE("Server save resumes custom typed state alongside charge fraction and timer", "[item-behavior][server][save]") {
    const auto content = BehaviorCatalog();
    TemporarySave file;
    Server original(content);
    auto& world = original.GetWorld();
    RemoveNpcs(world);
    const auto actor = original.GetPlayer();
    const auto position = world.GetActor(actor)->position;
    const TileCoord target{position.x + 1, position.z};
    const auto device = Take(world.CreateItem(content.FindItem("device"), ItemLocation::Ground(position)));
    const auto battery = Take(world.CreateItem(content.FindItem("battery"), ItemLocation::Carried(actor)));
    const auto id = world.GetItem(device)->id;
    const auto batteryId = world.GetItem(battery)->id;
    REQUIRE(world.SetTile(target, {content.FindTerrain("closed_door"), 7}));
    REQUIRE(Use(world, actor, device, "charger", "install", battery).outcome == ItemUseOutcome::Succeeded);
    Start(world, actor, device, "charger");
    Start(world, actor, device, "counter");
    REQUIRE(Use(world, actor, device, "timer", "arm", {}, 1, 0).outcome == ItemUseOutcome::Succeeded);
    REQUIRE(original.SetPacing({100, true, true}));
    REQUIRE(original.Send({}).advanced);
    CHECK(original.Update(170000).tick == 17);
    const auto saved = world.CaptureItemState(device);
    CHECK(Unsigned(*saved.Find("counter"), "remaining_ticks") == 34);
    CHECK(Unsigned(*saved.Find("charger"), "remainder") == 51);
    CHECK(Unsigned(*saved.Find("timer"), "remaining_ticks") == 283);
    REQUIRE(original.CreateSave(file.path.c_str()));
    auto loaded = Server::LoadSave(content, file.path.c_str());
    REQUIRE(loaded);
    auto& restored = **loaded;
    auto& fresh = restored.GetWorld();
    CHECK(restored.GetTick() == 17);
    CHECK(fresh.CaptureItemState(fresh.FindItem(id)) == saved);
    CHECK(fresh.ActiveItemCount() == 1);
    original.Update(830000);
    restored.Update(830000);
    REQUIRE(original.SetPacing({100, false, true}));
    REQUIRE(restored.SetPacing({100, false, true}));
    Command wait;
    wait.waitCost = 250;
    REQUIRE(original.Send(wait).advanced);
    REQUIRE(restored.Send(wait).advanced);
    CHECK(restored.GetTick() == original.GetTick());
    CHECK(fresh.CaptureItemState(fresh.FindItem(id)) == world.CaptureItemState(device));
    CHECK(fresh.CaptureItemState(fresh.FindItem(batteryId)) == world.CaptureItemState(battery));
    const auto custom = State(fresh, fresh.FindItem(id), "counter");
    CHECK(custom.GetString("note") == eastl::string_view("completed"));
    REQUIRE(custom.Find("history"));
    CHECK(*custom.Find("history") == [] { auto list = DataObject::List(); list.Append(DataObject::Unsigned(1)); return list; }());
    CHECK(restored.View().groundItems == original.View().groundItems);
    CHECK(fresh.GetTile(target)->terrain == content.FindTerrain("open_door"));
    CHECK(fresh.GetTile(target)->damage == 7);
    CHECK(fresh.CheckInvariants());
}

ChunkArchive CounterArchive(const ContentCatalog& content) {
    ChunkArchive chunk{};
    chunk.tiles.fill({content.FindTerrain("floor"), 0});
    SavedItem item{};
    item.id = 1;
    item.type = content.FindItem("device");
    item.location = LocationKind::ground;
    WriteItemState(content.GetItem(item.type), MakeItemState(content.GetItem(item.type)), item.state);
    chunk.items.push_back(eastl::move(item));
    return chunk;
}

ContentCatalog GroundCounterCatalog() {
    return BehaviorCatalog(Replace(behaviorContent,
        "id:'counter',type:'counter',duration_ticks:51,action_cost:17,lifecycle:'ordinary'",
        "id:'counter',type:'counter',duration_ticks:51,action_cost:17,lifecycle:'ground_only'"));
}

ChunkArchive NestedCounterArchive(const ContentCatalog& content, bool armed) {
    auto chunk = CounterArchive(content);
    auto& device = chunk.items[0];
    device.id = 2;
    device.location = LocationKind::item;
    device.owner = 1;
    auto* counter = device.state.Find("counter");
    REQUIRE(counter);
    counter->SetBoolean("enabled", armed);
    counter->SetUnsigned("remaining_ticks", armed ? 51 : 0);
    SavedItem pack{};
    pack.id = 1;
    pack.type = content.FindItem("backpack");
    pack.location = LocationKind::ground;
    WriteItemState(content.GetItem(pack.type), MakeItemState(content.GetItem(pack.type)), pack.state);
    chunk.items.insert(chunk.items.begin(), eastl::move(pack));
    return chunk;
}

TEST_CASE("Relational snapshot hook rejects locally valid armed counters inside a container", "[item-behavior][save]") {
    const auto content = GroundCounterCatalog();
    const auto invalid = NestedCounterArchive(content, true);
    const auto& device = invalid.items[1];
    const auto& definition = content.GetItem(device.type);
    const auto capability = FindItemCapability(definition, "counter");
    const auto* behavior = ItemBehaviorConfig(definition, capability);
    REQUIRE(behavior);
    const auto* local = device.state.Find("counter");
    REQUIRE(local);
    Diagnostics localDiagnostics;
    REQUIRE(behavior->Create(capability, local, &localDiagnostics));
    REQUIRE(ReadItemState(definition, device.state, &localDiagnostics));
    CHECK(localDiagnostics.Empty());

    Diagnostics hookDiagnostics;
    const ItemSnapshotContext context(content, invalid);
    CHECK(behavior->ValidateSnapshot(context, device.id, capability, &hookDiagnostics)
        == eastl::unexpected(StateError::InvalidData));
    CHECK(FormatDiagnostics(hookDiagnostics).find("Armed test counter requires a direct ground root") != String::npos);
    Diagnostics snapshotDiagnostics;
    CHECK(ValidateItemSnapshot(content, invalid, &snapshotDiagnostics) == eastl::unexpected(StateError::InvalidData));
    CHECK(FormatDiagnostics(snapshotDiagnostics).find("Armed test counter requires a direct ground root") != String::npos);

    // Identical local bytes pass on the ground; the same container relation also
    // passes when disarmed. Neither graph shape nor local parsing causes failure.
    auto valid = invalid;
    valid.items[1].location = LocationKind::ground;
    valid.items[1].owner = 0;
    CHECK(valid.items[1].state == invalid.items[1].state);
    REQUIRE(ValidateItemSnapshot(content, valid));
    REQUIRE(ValidateItemSnapshot(content, NestedCounterArchive(content, false)));

    TemporarySave file;
    SaveDatabase database(content);
    REQUIRE(database.Create(file.path.c_str()));
    REQUIRE(database.Begin());
    REQUIRE(database.WriteChunk(valid));
    SaveSnapshot snapshot;
    snapshot.nextId = 3;
    REQUIRE(database.WriteSnapshot(snapshot));
    REQUIRE(database.Commit());
    REQUIRE(database.Begin());
    Diagnostics writeDiagnostics;
    CHECK(database.WriteChunk(invalid, &writeDiagnostics) == eastl::unexpected(SaveError::InvalidData));
    CHECK(FormatDiagnostics(writeDiagnostics).find("Armed test counter requires a direct ground root") != String::npos);
    CHECK(database.Commit() == eastl::unexpected(SaveError::InvalidState));
    const auto retained = database.ReadChunk({}, 3);
    REQUIRE(retained);
    REQUIRE(*retained);
    REQUIRE((**retained).items.size() == 2);
    CHECK((**retained).items[1].location == LocationKind::ground);
    CHECK((**retained).items[1].state == valid.items[1].state);
}

TEST_CASE("Save open and world reconciliation invoke relational behavior validation after local parsing succeeds", "[item-behavior][save][world]") {
    const auto content = GroundCounterCatalog();
    const auto nested = NestedCounterArchive(content, false);
    REQUIRE(ValidateItemSnapshot(content, nested));
    TemporarySave file;
    {
        SaveDatabase database(content);
        REQUIRE(database.Create(file.path.c_str()));
        REQUIRE(database.Begin());
        REQUIRE(database.WriteChunk(nested));
        SaveSnapshot snapshot;
        snapshot.nextId = 3;
        REQUIRE(database.WriteSnapshot(snapshot));
        REQUIRE(database.Commit());
    }
    {
        SaveDatabase database(content);
        const auto opened = database.Open(file.path.c_str());
        REQUIRE(opened);
        const auto loaded = database.ReadChunk({}, opened->nextId);
        REQUIRE(loaded);
        REQUIRE(*loaded);
        REQUIRE((**loaded).items.size() == 2);
        CHECK((**loaded).items[1].state == nested.items[1].state);
    }
    // Open a world while the file is valid, but leave the chunk unmaterialized.
    // Corrupting afterward exercises ReadChunk at the reconciliation boundary
    // independently of SaveDatabase::Open's eager whole-save validation.
    World pendingWorld(content);
    REQUIRE(pendingWorld.LoadSave(file.path.c_str()));
    const auto request = Take(pendingWorld.RequestChunks({{0, 0}, 0}, LoadMode::active));
    CHECK(pendingWorld.LoadedChunks().empty());

    // Arm only the nested counter, leaving its owner, physical metadata, all
    // built-in state, and the SQL schema intact. A due (zero-delay) armed state
    // is intentionally locally valid, but violates our extension's ground rule.
    auto locallyValid = nested.items[1].state;
    locallyValid.Find("counter")->SetBoolean("enabled", true);
    REQUIRE(ReadItemState(content.GetItem(nested.items[1].type), locallyValid));
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(file.path.c_str(), &db, SQLITE_OPEN_READWRITE, nullptr) == SQLITE_OK);
    sqlite3_stmt* statement = nullptr;
    REQUIRE(sqlite3_prepare_v2(db, "SELECT payload FROM chunks", -1, &statement, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_step(statement) == SQLITE_ROW);
    String payload(reinterpret_cast<const char*>(sqlite3_column_text(statement, 0)));
    REQUIRE(sqlite3_finalize(statement) == SQLITE_OK);
    payload = Replace(eastl::move(payload), "\"counter\":{\"version\":1,\"enabled\":false",
        "\"counter\":{\"version\":1,\"enabled\":true");
    REQUIRE(sqlite3_prepare_v2(db, "UPDATE chunks SET payload=?1", -1, &statement, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_bind_text(statement, 1, payload.data(), static_cast<int>(payload.size()), SQLITE_TRANSIENT) == SQLITE_OK);
    REQUIRE(sqlite3_step(statement) == SQLITE_DONE);
    REQUIRE(sqlite3_finalize(statement) == SQLITE_OK);
    REQUIRE(sqlite3_close(db) == SQLITE_OK);

    Diagnostics reconcileDiagnostics;
    CHECK(pendingWorld.ReconcileChunks(&reconcileDiagnostics) == eastl::unexpected(SaveError::InvalidData));
    CHECK(FormatDiagnostics(reconcileDiagnostics).find("Armed test counter requires a direct ground root") != String::npos);
    CHECK(pendingWorld.LoadedChunks().empty());
    CHECK(pendingWorld.GetItem(pendingWorld.FindItem(2)) == nullptr);
    CHECK(pendingWorld.ActiveItemCount() == 0);
    CHECK_FALSE(pendingWorld.NextItemEventIn());
    CHECK(pendingWorld.CheckInvariants());
    REQUIRE(pendingWorld.ReleaseRequest(request));

    Diagnostics diagnostics;
    SaveDatabase rejected(content);
    const auto opened = rejected.Open(file.path.c_str(), &diagnostics);
    REQUIRE_FALSE(opened);
    CHECK(opened.error() == SaveError::InvalidData);
    CHECK(FormatDiagnostics(diagnostics).find("Armed test counter requires a direct ground root") != String::npos);
    CHECK(rejected.Begin() == eastl::unexpected(SaveError::NotOpen));
}

TEST_CASE("Custom state parser rejects malformed snapshots through every validation boundary", "[item-behavior][save]") {
    const auto content = BehaviorCatalog();
    auto chunk = CounterArchive(content);
    auto* counter = chunk.items[0].state.Find("counter");
    REQUIRE(counter);
    StateError expected = StateError::InvalidData;
    SECTION("boolean is not a string") { counter->SetString("enabled", "false"); }
    SECTION("ticks are unsigned") { counter->SetSigned("remaining_ticks", 0); }
    SECTION("count is unsigned") { counter->SetString("count", "0"); }
    SECTION("duration bounded by definition") { counter->SetBoolean("enabled", true); counter->SetUnsigned("remaining_ticks", 52); }
    SECTION("inactive state cannot retain time") { counter->SetUnsigned("remaining_ticks", 1); }
    SECTION("unsupported version") { counter->SetUnsigned("version", 2); expected = StateError::UnsupportedVersion; }
    SECTION("typed extension string") { counter->SetUnsigned("note", 0); }
    SECTION("typed extension list") { counter->SetString("history", "[]"); }
    SECTION("list element types") { auto list = DataObject::List(); list.Append(DataObject::Text("1")); counter->Set("history", list); counter->SetUnsigned("count", 1); }
    SECTION("list and count agree") { counter->SetUnsigned("count", 1); }
    SECTION("extra field") { counter->SetBoolean("extra", true); }
    SECTION("missing state never initializes") { chunk.items[0].state.Set("counter", DataObject{}); }
    Diagnostics diagnostics;
    const auto& definition = content.GetItem(chunk.items[0].type);
    auto parsed = ReadItemState(definition, chunk.items[0].state, &diagnostics);
    REQUIRE_FALSE(parsed);
    CHECK(parsed.error() == expected);
    CHECK_FALSE(diagnostics.Empty());
    CHECK(ValidateItemSnapshot(content, chunk) == eastl::unexpected(expected));
    TemporarySave file;
    SaveDatabase database(content);
    REQUIRE(database.Create(file.path.c_str()));
    REQUIRE(database.Begin());
    const auto saveError = expected == StateError::UnsupportedVersion ? SaveError::UnsupportedVersion : SaveError::InvalidData;
    CHECK(database.WriteChunk(chunk) == eastl::unexpected(saveError));
    CHECK(database.Commit() == eastl::unexpected(SaveError::InvalidState));
}

TEST_CASE("Durable custom behavior payload is validated again on open", "[item-behavior][save]") {
    const auto content = BehaviorCatalog();
    TemporarySave file;
    {
        SaveDatabase database(content);
        REQUIRE(database.Create(file.path.c_str()));
        REQUIRE(database.Begin());
        REQUIRE(database.WriteChunk(CounterArchive(content)));
        SaveSnapshot snapshot;
        snapshot.nextId = 2;
        REQUIRE(database.WriteSnapshot(snapshot));
        REQUIRE(database.Commit());
    }
    {
        SaveDatabase database(content);
        const auto opened = database.Open(file.path.c_str());
        REQUIRE(opened);
        const auto chunk = database.ReadChunk({}, opened->nextId);
        REQUIRE(chunk);
        REQUIRE(*chunk);
        CHECK((**chunk).items[0].state == CounterArchive(content).items[0].state);
    }
    // Mutate a dynamically written fixture, keeping SQL/schema/other state valid.
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(file.path.c_str(), &db, SQLITE_OPEN_READWRITE, nullptr) == SQLITE_OK);
    sqlite3_stmt* statement = nullptr;
    REQUIRE(sqlite3_prepare_v2(db, "SELECT payload FROM chunks", -1, &statement, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_step(statement) == SQLITE_ROW);
    String payload(reinterpret_cast<const char*>(sqlite3_column_text(statement, 0)));
    REQUIRE(sqlite3_finalize(statement) == SQLITE_OK);
    payload = Replace(eastl::move(payload), "\"note\":\"new counter\"", "\"note\":42");
    REQUIRE(sqlite3_prepare_v2(db, "UPDATE chunks SET payload=?1", -1, &statement, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_bind_text(statement, 1, payload.data(), static_cast<int>(payload.size()), SQLITE_TRANSIENT) == SQLITE_OK);
    REQUIRE(sqlite3_step(statement) == SQLITE_DONE);
    REQUIRE(sqlite3_finalize(statement) == SQLITE_OK);
    REQUIRE(sqlite3_close(db) == SQLITE_OK);
    Diagnostics diagnostics;
    SaveDatabase malformed(content);
    const auto opened = malformed.Open(file.path.c_str(), &diagnostics);
    REQUIRE_FALSE(opened);
    CHECK(opened.error() == SaveError::InvalidData);
    CHECK_FALSE(diagnostics.Empty());
}

TEST_CASE("Deferred self destruction skips later behavior callbacks on the same item", "[item-behavior][lifecycle]") {
    String text = behaviorContent;
    bool duringElapse = false;
    SECTION("event callback") {}
    SECTION("elapse callback") {
        duringElapse = true;
        text = Replace(eastl::move(text), "lifecycle:'event_self'", "lifecycle:'elapse_self'");
        text = Replace(eastl::move(text), "id:'z_observer',type:'counter',duration_ticks:51,action_cost:17,lifecycle:'ordinary'",
            "id:'z_observer',type:'counter',duration_ticks:51,action_cost:17,lifecycle:'observe_elapse'");
    }
    const auto content = BehaviorCatalog(text);
    World world(content);
    Load(world);
    const auto actor = Take(world.CreateActor({1, 1}, true));
    const auto killer = Take(world.CreateItem(content.FindItem("killer"), ItemLocation::Ground({1, 1})));
    const auto survivor = Take(world.CreateItem(content.FindItem("device"), ItemLocation::Ground({1, 1})));
    Start(world, actor, killer, "a_killer");
    Start(world, actor, killer, "z_observer");
    Start(world, actor, survivor, "counter");
    Advance(world, duringElapse ? 1 : 51);
    CHECK(world.GetItem(killer) == nullptr);
    CHECK(world.GetTile(callbackLog)->damage == (duringElapse ? 0 : 2)); // Killer event + surviving item only.
    CHECK(Unsigned(State(world, survivor, "counter"), "count") == (duringElapse ? 0 : 1));
    CHECK(world.ActiveItemCount() == (duringElapse ? 1 : 0));
    CHECK(world.CheckInvariants());
}

TEST_CASE("Deferred sibling destruction invalidates pending scheduled callbacks safely", "[item-behavior][lifecycle]") {
    String text = behaviorContent;
    bool duringElapse = false;
    SECTION("event callback") { text = Replace(eastl::move(text), "lifecycle:'event_self'", "lifecycle:'event_sibling'"); }
    SECTION("elapse callback") {
        duringElapse = true;
        text = Replace(eastl::move(text), "lifecycle:'event_self'", "lifecycle:'elapse_sibling'");
        text = Replace(eastl::move(text), "id:'counter',type:'counter',duration_ticks:51,action_cost:17,lifecycle:'ordinary'}]}\n]",
            "id:'counter',type:'counter',duration_ticks:51,action_cost:17,lifecycle:'observe_elapse'}]}\n]");
    }
    const auto content = BehaviorCatalog(text);
    World world(content);
    Load(world);
    const auto actor = Take(world.CreateActor({1, 1}, true));
    // Persistent-ID order puts killer before victim in the pending callback list.
    const auto killer = Take(world.CreateItem(content.FindItem("killer"), ItemLocation::Ground({1, 1})));
    const auto victim = Take(world.CreateItem(content.FindItem("victim"), ItemLocation::Ground({1, 1})));
    const auto survivor = Take(world.CreateItem(content.FindItem("device"), ItemLocation::Ground({1, 1})));
    Start(world, actor, killer, "a_killer");
    Start(world, actor, victim, "counter");
    Start(world, actor, survivor, "counter");
    Advance(world, duringElapse ? 1 : 51);
    CHECK(world.GetItem(victim) == nullptr);
    REQUIRE(world.GetItem(killer));
    CHECK(world.GetTile(callbackLog)->damage == (duringElapse ? 0 : 2));
    CHECK(Unsigned(State(world, survivor, "counter"), "count") == (duringElapse ? 0 : 1));
    CHECK(world.CheckInvariants());
}

TEST_CASE("Use callback can request its own destruction with other activities pending", "[item-behavior][lifecycle]") {
    CompositeRig rig;
    const auto survivor = Take(rig.world.CreateItem(rig.content.FindItem("device"), ItemLocation::Ground({1, 1})));
    Start(rig.world, rig.actor, survivor, "counter");
    const auto result = Use(rig.world, rig.actor, rig.device, "counter", "erase_self");
    CHECK(result.outcome == ItemUseOutcome::Succeeded);
    CHECK(result.baseCost == 17);
    CHECK(rig.world.GetItem(rig.device) == nullptr);
    CHECK(rig.world.GetItem(rig.battery) == nullptr);
    Advance(rig.world, 51);
    CHECK(Unsigned(State(rig.world, survivor, "counter"), "count") == 1);
    CHECK(rig.world.ActiveItemCount() == 0);
    CHECK(rig.world.CheckInvariants());
}

TEST_CASE("Transfer OnChange may destroy the moved owner and descendants without retained borrows", "[item-behavior][lifecycle]") {
    const auto content = BehaviorCatalog(Replace(behaviorContent, "lifecycle:'event_self'", "lifecycle:'move_self'"));
    World world(content);
    Load(world);
    const auto actor = Take(world.CreateActor({1, 1}, true));
    const auto pack = Take(world.CreateItem(content.FindItem("backpack"), ItemLocation::Ground({1, 1})));
    const auto killer = Take(world.CreateItem(content.FindItem("killer"), ItemLocation::Inside(pack)));
    const auto descendant = Take(world.CreateItem(content.FindItem("rock"), ItemLocation::Inside(killer)));
    const auto observer = Take(world.CreateItem(content.FindItem("device"), ItemLocation::Ground({1, 1})));
    Start(world, actor, killer, "a_killer");
    Start(world, actor, killer, "z_observer");
    Start(world, actor, observer, "counter");
    SECTION("move owner directly") {
        REQUIRE(world.TransferItem(killer, ItemLocation::Carried(actor)));
        CHECK(world.GetItem(pack)->contents.empty());
    }
    SECTION("move ancestor with descendant notifications pending") {
        REQUIRE(world.TransferItem(pack, ItemLocation::Carried(actor)));
        CHECK(world.GetItem(pack)->location == ItemLocation::Carried(actor));
        CHECK(world.GetItem(pack)->contents.empty());
    }
    CHECK(world.GetItem(killer) == nullptr);
    CHECK(world.GetItem(descendant) == nullptr);
    CHECK(world.GetTile(callbackLog)->damage == 1);
    CHECK(world.ActiveItemCount() == 1);
    Advance(world, 51);
    CHECK(Unsigned(State(world, observer, "counter"), "count") == 1);
    CHECK(world.CheckInvariants());
}

TEST_CASE("Moving ordinary child does not cancel unrelated parent activities", "[item-behavior][lifecycle]") {
    CompositeRig rig;
    Advance(rig.world, 17);
    const auto before = rig.world.CaptureItemState(rig.device);
    const auto child = Take(rig.world.CreateItem(rig.content.FindItem("rock"), ItemLocation::Carried(rig.actor)));
    REQUIRE(rig.world.TransferItem(child, ItemLocation::Inside(rig.device)));
    CHECK(rig.world.CaptureItemState(rig.device) == before);
    REQUIRE(rig.world.TransferItem(child, ItemLocation::Carried(rig.actor)));
    CHECK(rig.world.CaptureItemState(rig.device) == before);
    Advance(rig.world, 34);
    CHECK(Unsigned(State(rig.world, rig.device, "counter"), "count") == 1);
    CHECK(Unsigned(State(rig.world, rig.device, "timer"), "remaining_ticks") == 249);
    CHECK(State(rig.world, rig.device, "charger").GetBoolean("enabled") == true);
    CHECK(rig.world.CheckInvariants());
}

TEST_CASE("Wire roundtrips custom actions arbitrary reasons and catalog metadata", "[item-behavior][network][codec]") {
    const auto content = BehaviorCatalog();
    Server server(content);
    const auto device = Take(server.GetWorld().CreateItem(content.FindItem("device"), ItemLocation::Carried(server.GetPlayer())));
    const auto command = UseCommand(server.GetWorld(), device, "counter", "erase_self");
    wire::Writer commandWriter;
    commandWriter.CommandValue(command);
    wire::Reader commandReader{{commandWriter.bytes.data(), commandWriter.bytes.size()}};
    Command decoded;
    REQUIRE(commandReader.CommandValue(decoded));
    CHECK(commandReader.Done());
    CHECK(decoded.itemAction == "erase_self");
    CHECK(decoded.capability == command.capability);
    CHECK(decoded.item == command.item);
    PlayerCommandResult receipt;
    receipt.player = 7; receipt.sequence = 3; receipt.opportunity = 2;
    receipt.result.outcome = Outcome::item_use_failed;
    receipt.result.itemOutcome = ItemUseOutcome::Refused;
    receipt.result.itemReason = {"test_arbitrary_reason_42", {"test.unknown.wire.reason", "Counter is paused: try later."}};
    wire::Writer resultWriter;
    resultWriter.Result(receipt);
    wire::Reader resultReader{{resultWriter.bytes.data(), resultWriter.bytes.size()}};
    PlayerCommandResult result;
    REQUIRE(resultReader.Result(result));
    CHECK(resultReader.Done());
    CHECK(result.result.itemOutcome == ItemUseOutcome::Refused);
    CHECK(result.result.itemReason == receipt.result.itemReason);
    const auto view = server.View();
    REQUIRE(wire::ValidItemDefinitions(view, content));
    wire::Writer viewWriter;
    REQUIRE(viewWriter.View(view));
    wire::Reader viewReader{{viewWriter.bytes.data(), viewWriter.bytes.size()}};
    WorldView roundtrip;
    REQUIRE(viewReader.View(roundtrip));
    CHECK(viewReader.Done());
    CHECK(roundtrip.inventory == view.inventory);
    REQUIRE(wire::ValidItemDefinitions(roundtrip, content));
    bool changed = false;
    for (auto& entry : roundtrip.inventory) {
        if (entry.id != command.item) { continue; }
        for (auto& action : entry.actions) {
            if (action.capability == command.capability && action.action == "start") {
                ++action.baseCost;
                changed = true;
            }
        }
    }
    REQUIRE(changed);
    CHECK_FALSE(wire::ValidItemDefinitions(roundtrip, content));
}

template<class Predicate> bool Poll(LanHost& host, LanClient& client, Predicate predicate) {
    for (int i = 0; i < 500; ++i) {
        host.Pump(10000);
        client.Pump(10000);
        if (predicate()) { return true; }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

TEST_CASE("LAN host executes guest custom behavior command with the same explicit catalog", "[item-behavior][network][loopback]") {
    const auto content = BehaviorCatalog();
    auto runtime = NetworkRuntime::Create();
    REQUIRE(runtime);
    Server server(content);
    RemoveNpcs(server.GetWorld());
    LanHost host(**runtime, server, content);
    REQUIRE(host.Open({0, 0, "Custom behavior"}));
    LanClient client(**runtime, content);
    REQUIRE(client.Connect({"127.0.0.1", host.GetPort(), "Counter guest", 0}));
    REQUIRE(Poll(host, client, [&] { return client.GetState() == ConnectionState::Active; }));
    auto& world = server.GetWorld();
    const auto guest = world.FindActor(client.View().playerId);
    REQUIRE(world.GetActor(guest));
    const auto device = Take(world.CreateItem(content.FindItem("device"), ItemLocation::Carried(guest)));
    const auto command = UseCommand(world, device, "counter", "start");
    const auto sequence = client.Submit(command);
    REQUIRE(sequence);
    host.QueueLocal({CommandKind::wait});
    bool received = false;
    REQUIRE(Poll(host, client, [&] {
        for (const auto& receipt : client.TakeResults()) {
            // Admission and final execution both produce receipts for this input.
            if (receipt.sequence != *sequence || receipt.result.outcome == Outcome::queued) { continue; }
            CHECK(receipt.result.outcome == Outcome::used_item);
            CHECK(receipt.result.itemOutcome == ItemUseOutcome::Succeeded);
            CHECK(receipt.result.cost == 17);
            CHECK(receipt.result.advanced);
            received = true;
        }
        return received;
    }));
    CHECK(world.GetItem(device)->location == ItemLocation::Carried(guest));
    CHECK(State(world, device, "counter").GetBoolean("enabled") == true);
    CHECK(Unsigned(State(world, device, "counter"), "remaining_ticks") <= 51);
    CHECK(client.GetState() == ConnectionState::Active);
    CHECK(wire::ValidItemDefinitions(client.View(), content));
    CHECK(world.CheckInvariants());
}
}
