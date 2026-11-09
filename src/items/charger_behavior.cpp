#include "items/item_registry.h"
#include "items/item_snapshot.h"
#include "core/world_archive.h"
#include <EASTL/algorithm.h>
#include <EASTL/shared_ptr.h>
#include <cstdlib>

namespace cataclysmx {
namespace {
struct ChargerConfig {
    ItemCapabilityId source{};
    String batteryClass;
    std::uint32_t energyPerTurn = 0;
    std::uint32_t actionCost = 100;
};
bool ValidName(eastl::string_view name) {
    if (name.empty() || name[0] < 'a' || name[0] > 'z') { return false; }
    for (char c : name) {
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) { return false; }
    }
    return true;
}
ItemCapabilityId BatteryStore(const ItemDefinition& item, const ChargerConfig& config) {
    if (item.energyStores.size() != 1) { return {}; }
    for (const auto& capability : item.capabilities) { if (capability.type == "charger") { return {}; } }
    const auto& store = item.energyStores.front();
    return store.config.rechargeable && store.config.batteryClass == config.batteryClass ? store.capability : ItemCapabilityId{};
}
ItemUseReason Reason(const char* code, const char* key, const char* fallback) { return {code, {key, fallback}}; }
ItemUseReason Missing() { return Reason("missing_battery", "item.refusal.3", "Install a battery first."); }
ItemUseReason Incompatible() { return Reason("incompatible_battery", "item.refusal.4", "That battery is incompatible."); }
ItemUseReason Occupied() { return Reason("occupied", "item.refusal.5", "The battery slot is occupied."); }
ItemUseReason NoPower() { return Reason("no_power", "item.refusal.6", "The device has no power."); }
ItemUseReason Full() { return Reason("full", "item.refusal.7", "The battery is already full."); }
ItemUseReason NoCapacity() { return Reason("no_capacity", "item.refusal.10", "There is not enough capacity."); }
ItemUseResult Refuse(ItemUseReason reason) { return {ItemUseOutcome::Refused, 0, eastl::move(reason)}; }
ItemUseReason TransferReason(WorldError error) {
    if (error == WorldError::capacity_exceeded) { return NoCapacity(); }
    if (error == WorldError::occupied) { return Occupied(); }
    return Incompatible();
}
ItemBehaviorSchema MakeSchema(std::uint32_t cost) {
    ItemBehaviorSchema schema;
    schema.hasActivity = true;
    schema.actions = {
        {"install", ItemActionArgument::Item, ItemActionCategory::ManageContents, cost, {"item.action.2", "Install battery"}},
        {"remove", ItemActionArgument::None, ItemActionCategory::ManageContents, cost, {"item.action.3", "Remove battery"}},
        {"start", ItemActionArgument::None, ItemActionCategory::Operate, cost, {"item.action.0", "Start charging"}},
        {"stop", ItemActionArgument::None, ItemActionCategory::Operate, cost, {"item.action.1", "Stop charging"}}
    };
    return schema;
}
struct ChargeLink {
    ItemHandle target{};
    ItemCapabilityId store{};
    std::uint32_t source = 0;
    std::uint32_t room = 0;
};
class ChargerBehavior final : public ItemBehavior {
public:
    ChargerBehavior(ItemCapabilityId self, const ChargerConfig& config, const ItemBehaviorSchema& schema,
        bool enabled, std::uint32_t remainder)
        : ItemBehavior(self), config_(config), schema_(schema), enabled_(enabled), remainder_(remainder) {}
    ChargeLink Link(const ItemReadContext& context) const {
        ChargeLink result;
        const auto source = context.Energy(context.Self(), config_.source);
        if (source) { result.source = source->energy; }
        result.target = context.Installed(context.Self(), Capability());
        const auto* target = context.GetWorld().GetItem(result.target);
        if (!target) { return result; }
        result.store = BatteryStore(context.GetWorld().GetContent().GetItem(target->type), config_);
        const auto energy = context.Energy(result.target, result.store);
        if (energy) { result.room = energy->capacity - energy->energy; }
        return result;
    }
    ItemUseReason StartReason(const ChargeLink& link) const {
        if (!link.target.value) { return Missing(); }
        if (!link.store.value) { return Incompatible(); }
        if (!link.room) { return Full(); }
        if (!link.source) { return NoPower(); }
        return {};
    }
    void QueryActions(const ItemReadContext& context, ActorHandle actor, Vector<ItemActionInfo>& output) const override {
        const auto link = Link(context);
        const auto append = [&](const char* id, ItemUseReason reason) {
            for (const auto& action : schema_.actions) {
                if (action.id != id) { continue; }
                output.push_back({Capability(), action.id, action.argument,
                    reason.code.empty() ? ItemUseOutcome::Succeeded : ItemUseOutcome::Refused,
                    action.baseCost, action.category, action.label, eastl::move(reason)});
            }
        };
        append(enabled_ ? "stop" : "start", enabled_ ? ItemUseReason{} : StartReason(link));
        if (link.target.value) {
            append("remove", context.CheckTransfer(link.target, ItemLocation::Carried(actor)) ? ItemUseReason{} : NoCapacity());
        } else { append("install", {}); }
    }
    ItemUseResult Use(ItemMutationContext& context, ActorHandle actor, const ItemUseRequest& request) override {
        const auto link = Link(context.Read());
        if (request.action == "install") {
            if (link.target.value) { return Refuse(Occupied()); }
            if (!context.Read().GetWorld().CanAccessItem(actor, request.otherItem)) { return {ItemUseOutcome::Inaccessible, 0, {}}; }
            const auto result = context.TransferItem(request.otherItem, ItemLocation::Inside(context.Read().Self(), Capability()));
            if (!result) { return Refuse(TransferReason(result.error())); }
        } else if (request.action == "remove") {
            if (!link.target.value) { return Refuse(Missing()); }
            const auto result = context.TransferItem(link.target, ItemLocation::Carried(actor));
            if (!result) { return Refuse(TransferReason(result.error())); }
        } else if (request.action == "start") {
            if (enabled_) { return {}; }
            const auto reason = StartReason(link);
            if (!reason.code.empty()) { return Refuse(reason); }
            enabled_ = true; remainder_ = 0;
        } else if (request.action == "stop") {
            if (!enabled_) { return {}; }
            Stop();
        } else { return {}; }
        return {ItemUseOutcome::Succeeded, config_.actionCost, {}};
    }
    eastl::optional<Tick> NextEventIn(const ItemReadContext& context) const override {
        if (!enabled_) { return eastl::nullopt; }
        const auto link = Link(context);
        if (!StartReason(link).code.empty()) { return Tick{0}; }
        const std::uint64_t numerator = static_cast<std::uint64_t>(eastl::min(link.source, link.room)) * ticks_per_turn - remainder_;
        return (numerator + config_.energyPerTurn - 1) / config_.energyPerTurn;
    }
    void Elapse(ItemMutationContext& context, Tick elapsed) override {
        if (!enabled_) { return; }
        const auto link = Link(context.Read());
        const Tick deadline = *NextEventIn(context.Read());
        if (!deadline) { Stop(); return; }
        const Tick bounded = eastl::min(elapsed, deadline);
        const std::uint64_t numerator = bounded * config_.energyPerTurn + remainder_;
        const auto amount = static_cast<std::uint32_t>(eastl::min<std::uint64_t>(
            numerator / ticks_per_turn, eastl::min(link.source, link.room)));
        const auto result = context.TransferEnergy({context.Read().Self(), config_.source}, {link.target, link.store}, amount);
        if (!result) { std::abort(); }
        remainder_ = static_cast<std::uint32_t>(numerator % ticks_per_turn);
        if (elapsed >= deadline) { Stop(); }
    }
    void ProcessEvent(ItemMutationContext& context) override {
        if (enabled_ && !StartReason(Link(context.Read())).code.empty()) { Stop(); }
    }
    void OnChange(ItemMutationContext& context, const ItemChange& change) override {
        if (!enabled_) { return; }
        // A move of the device (or its ancestor) preserves its intact connection.
        if ((change.kind == ItemChangeKind::ContentsChanged && change.item == context.Read().Self() && change.capability == Capability())
            || !StartReason(Link(context.Read())).code.empty()) { Stop(); }
    }
    DataObject SaveState() const override {
        DataObject result;
        result.SetUnsigned("version", 1); result.SetBoolean("enabled", enabled_); result.SetUnsigned("remainder", remainder_);
        return result;
    }
    ItemBehaviorView Describe(const ItemReadContext&) const override { return {Capability(), enabled_, 0, {}}; }
private:
    const ChargerConfig& config_;
    const ItemBehaviorSchema& schema_;
    bool enabled_ = false;
    std::uint32_t remainder_ = 0;
    void Stop() { enabled_ = false; remainder_ = 0; }
};
class ChargerDefinition final : public ItemBehaviorDefinition {
public:
    explicit ChargerDefinition(ChargerConfig config) : ItemBehaviorDefinition(MakeSchema(config.actionCost)), config_(eastl::move(config)) {}
    ContentResult<void> ValidateComposition(const ItemDefinition& item, ItemCapabilityId self, Diagnostics* diagnostics) const override {
        bool valid = true;
        for (const auto& store : item.energyStores) { if (!store.config.batteryClass.empty()) { valid = false; } }
        for (const auto& capability : item.capabilities) {
            if (capability.type != "charger" || FindItemCapability(item, capability.id) == self) { continue; }
            const auto source = capability.config.GetString("power_source");
            if (source && FindItemCapability(item, *source) == config_.source) { valid = false; }
        }
        if (!valid) {
            if (diagnostics) { diagnostics->Add("Chargers cannot share sources or be battery-class items"); }
            return eastl::unexpected(ContentError::InvalidData);
        }
        return {};
    }
    StateResult<eastl::unique_ptr<ItemBehavior>> Create(ItemCapabilityId self, const DataObject* archive, Diagnostics* diagnostics) const override {
        bool enabled = false;
        std::uint32_t remainder = 0;
        if (archive) {
            const auto version = archive->GetUnsigned("version");
            if (version && *version != 1) {
                if (diagnostics) { diagnostics->Add("Unsupported charger state version"); }
                return eastl::unexpected(StateError::UnsupportedVersion);
            }
            const auto e = archive->GetBoolean("enabled"); const auto r = archive->GetUnsigned("remainder");
            if (archive->Type() != DataType::Object || !version || !e || !r || archive->Size() != 3
                || *r >= ticks_per_turn || (!*e && *r != 0)) {
                if (diagnostics) { diagnostics->Add("Invalid charger state"); }
                return eastl::unexpected(StateError::InvalidData);
            }
            enabled = *e; remainder = static_cast<std::uint32_t>(*r);
        }
        return eastl::unique_ptr<ItemBehavior>(new ChargerBehavior(self, config_, Schema(), enabled, remainder));
    }
    bool AcceptsInstallation(const ItemDefinition& child) const override { return BatteryStore(child, config_).value != 0; }
    StateResult<void> ValidateSnapshot(const ItemSnapshotContext& context, PersistentId self,
        ItemCapabilityId capability, Diagnostics* diagnostics) const override {
        const auto* state = context.State(self, capability);
        if (!state) { return eastl::unexpected(StateError::InvalidData); }
        auto parsed = Create(capability, state, diagnostics);
        if (!parsed) { return eastl::unexpected(parsed.error()); }
        if (!*state->GetBoolean("enabled")) { return {}; }
        const auto source = context.Energy(self, config_.source);
        const auto* target = context.Installed(self, capability);
        const auto store = target ? BatteryStore(context.Definition(target->id), config_) : ItemCapabilityId{};
        const auto energy = target ? context.Energy(target->id, store) : eastl::optional<ItemEnergyView>{};
        if (!source || !source->energy || !energy || energy->energy >= energy->capacity) {
            if (diagnostics) { diagnostics->Add("Enabled charger requires power and an installed non-full battery"); }
            return eastl::unexpected(StateError::InvalidData);
        }
        return {};
    }
private:
    const ChargerConfig config_;
};
}
ContentResult<ParsedItemBehavior> ParseChargerBehavior(const DataObject& input, const ItemDefinition& item,
    ItemCapabilityId, Diagnostics* diagnostics) {
    const auto fail = [diagnostics]() -> eastl::unexpected<ContentError> {
        if (diagnostics) { diagnostics->Add("Invalid charger definition"); }
        return eastl::unexpected(ContentError::InvalidData);
    };
    if (input.Type() != DataType::Object) { return fail(); }
    for (std::size_t i = 0; i < input.Size(); ++i) {
        const auto& key = input.Name(i);
        if (key != "id" && key != "type" && key != "power_source" && key != "battery_class" && key != "energy_per_turn" && key != "action_cost") { return fail(); }
    }
    const auto source = input.GetString("power_source"); const auto batteryClass = input.GetString("battery_class");
    const auto rate = input.GetUnsigned("energy_per_turn");
    const auto cost = input.Find("action_cost") ? input.GetUnsigned("action_cost") : DataResult<std::uint64_t>{100};
    const auto id = input.GetString("id"); const auto type = input.GetString("type");
    if (!id || !ValidName(*id) || !type || *type != "charger" || !source || !batteryClass
        || !ValidName(*batteryClass) || !rate || !*rate || *rate > UINT32_MAX || !cost || !*cost || *cost > UINT32_MAX) { return fail(); }
    const auto sourceId = FindItemCapability(item, *source);
    if (!ItemEnergyStore(item, sourceId)) { return fail(); }
    ChargerConfig config{sourceId, String(batteryClass->data(), batteryClass->size()), static_cast<std::uint32_t>(*rate), static_cast<std::uint32_t>(*cost)};
    DataObject normalized;
    normalized.SetString("power_source", *source); normalized.SetString("battery_class", *batteryClass);
    normalized.SetUnsigned("energy_per_turn", *rate); normalized.SetUnsigned("action_cost", *cost);
    return ParsedItemBehavior{eastl::allocate_shared<ChargerDefinition>(FatalAllocator{}, eastl::move(config)), eastl::move(normalized)};
}
}
