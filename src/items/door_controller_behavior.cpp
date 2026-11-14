#include "items/item_registry.h"
#include "items/item_snapshot.h"
#include "core/world_archive.h"
#include <EASTL/algorithm.h>
#include <EASTL/shared_ptr.h>
#include <cstdlib>

namespace cataclysmx {
namespace {
struct DoorConfig { Tick delay = 0; std::uint32_t cost = 100; };
bool ValidName(eastl::string_view name) {
    if (name.empty() || name[0] < 'a' || name[0] > 'z') { return false; }
    for (char c : name) {
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) { return false; }
    }
    return true;
}
ItemBehaviorSchema MakeSchema(DoorConfig config) {
    ItemBehaviorSchema schema;
    schema.implementationVersion = 2;
    schema.hasActivity = true; schema.countdown = true; schema.maximumDelay = config.delay;
    schema.actions = {
        {"arm", ItemActionArgument::Direction, ItemActionCategory::Operate, config.cost, {"item.action.4", "Arm door controller"}},
        {"disarm", ItemActionArgument::None, ItemActionCategory::Operate, config.cost, {"item.action.5", "Disarm door controller"}}
    };
    return schema;
}
ItemUseReason NotDeployed() { return {"not_deployed", {"item.refusal.8", "Deploy the controller on the ground before arming it."}}; }
ItemUseReason InvalidTarget() { return {"invalid_target", {"item.refusal.9", "There is no suitable door in that direction."}}; }
bool Deployed(const ItemReadContext& context, ActorHandle actor) {
    const auto* item = context.GetWorld().GetItem(context.Self());
    const auto* owner = context.GetWorld().GetActor(actor);
    return item && owner && item->location.kind == LocationKind::ground && item->location.position == owner->position;
}
bool Adjacent(TileCoord a, TileCoord b) {
    return std::abs(a.x - b.x) + std::abs(a.z - b.z) + std::abs(a.y - b.y) == 1;
}
bool Reachable(const World& world, TileCoord from, TileCoord target) {
    if (!Adjacent(from, target)) { return false; }
    if (from.y == target.y) { return true; }
    const Tile* upper = world.GetTile(from.y > target.y ? from : target);
    const Tile* lower = world.GetTile(from.y > target.y ? target : from);
    if (!upper || !lower) { return false; }
    const auto& upperTerrain = world.GetContent().GetTerrain(upper->terrain);
    const auto& lowerTerrain = world.GetContent().GetTerrain(lower->terrain);
    return upperTerrain.openSpace || (upperTerrain.stairs && lowerTerrain.stairs);
}
class DoorBehavior final : public ItemBehavior {
public:
    DoorBehavior(ItemCapabilityId self, const DoorConfig& config, const ItemBehaviorSchema& schema,
        bool armed, Tick remaining, TileCoord target)
        : ItemBehavior(self), config_(config), schema_(schema), armed_(armed), remaining_(remaining), target_(target) {}
    void QueryActions(const ItemReadContext& context, ActorHandle actor, ItemActionList& output) const override {
        const auto& action = schema_.actions[armed_ ? 1 : 0];
        const auto reason = armed_ || Deployed(context, actor) ? ItemUseReason{} : NotDeployed();
        output.push_back({Capability(), action.id, action.argument,
            reason.code.empty() ? ItemUseOutcome::Succeeded : ItemUseOutcome::Refused,
            action.baseCost, action.category, action.label, reason});
    }
    ItemUseResult Use(ItemMutationContext& context, ActorHandle actor, const ItemUseRequest& request) override {
        if (request.action == "disarm") {
            if (!armed_) { return {}; }
            Clear();
        } else if (request.action == "arm") {
            if (armed_) { return {}; }
            if (!Deployed(context.Read(), actor)) { return {ItemUseOutcome::Refused, 0, NotDeployed()}; }
            if (!((request.dy == 0 && request.dx == 0 && (request.dz == 1 || request.dz == -1))
                || (request.dy == 0 && request.dz == 0 && (request.dx == 1 || request.dx == -1))
                || (request.dx == 0 && request.dz == 0 && (request.dy == 1 || request.dy == -1)))) {
                return {ItemUseOutcome::Refused, 0, InvalidTarget()};
            }
            const auto& world = context.Read().GetWorld();
            const auto position = world.GetItem(context.Read().Self())->location.position;
            const TileCoord target{position.x + request.dx, position.z + request.dz, position.y + request.dy};
            const auto* tile = world.GetTile(target);
            if (!world.IsActive(target) || !tile || !Reachable(world, position, target)
                || !world.GetContent().GetTerrain(tile->terrain).opensTo.value) {
                return {ItemUseOutcome::Refused, 0, InvalidTarget()};
            }
            armed_ = true; remaining_ = config_.delay; target_ = target;
        } else { return {}; }
        return {ItemUseOutcome::Succeeded, config_.cost, {}};
    }
    eastl::optional<Tick> NextEventIn(const ItemReadContext&) const override {
        return armed_ ? eastl::optional<Tick>{remaining_} : eastl::nullopt;
    }
    void Elapse(ItemMutationContext&, Tick elapsed) override { remaining_ -= eastl::min(remaining_, elapsed); }
    void ProcessEvent(ItemMutationContext& context) override {
        if (!armed_ || remaining_) { return; }
        const auto& world = context.Read().GetWorld();
        const auto* item = world.GetItem(context.Read().Self());
        const auto* tile = world.GetTile(target_);
        if (item && item->location.kind == LocationKind::ground && world.IsActive(target_) && tile
            && Reachable(world, item->location.position, target_)) {
            const auto opensTo = world.GetContent().GetTerrain(tile->terrain).opensTo;
            if (opensTo.value) {
                Tile changed = *tile; changed.terrain = opensTo;
                if (!context.SetTile(target_, changed)) { std::abort(); }
            }
        }
        Clear();
    }
    void OnChange(ItemMutationContext& context, const ItemChange& change) override {
        if (change.kind == ItemChangeKind::Moved && change.item == context.Read().Self()) { Clear(); }
    }
    DataObject SaveState() const override {
        DataObject result;
        result.SetUnsigned("version", 2); result.SetBoolean("armed", armed_); result.SetUnsigned("remaining_ticks", remaining_);
        DataObject target; target.SetSigned("x", target_.x); target.SetSigned("z", target_.z); target.SetSigned("y", target_.y);
        result.Set("target", eastl::move(target));
        return result;
    }
    ItemBehaviorView Describe(const ItemReadContext&) const override { return {Capability(), armed_, remaining_, {}}; }
private:
    const DoorConfig& config_;
    const ItemBehaviorSchema& schema_;
    bool armed_ = false;
    Tick remaining_ = 0;
    TileCoord target_{};
    void Clear() { armed_ = false; remaining_ = 0; target_ = {}; }
};
class DoorDefinition final : public ItemBehaviorDefinition {
public:
    explicit DoorDefinition(DoorConfig config) : ItemBehaviorDefinition(MakeSchema(config)), config_(config) {}
    StateResult<eastl::unique_ptr<ItemBehavior>> Create(ItemCapabilityId self, const DataObject* archive, Diagnostics* diagnostics) const override {
        bool armed = false; Tick remaining = 0; TileCoord target{};
        if (archive) {
            const auto version = archive->GetUnsigned("version");
            if (version && *version != 2) {
                if (diagnostics) { diagnostics->Add("Unsupported door controller state version"); }
                return eastl::unexpected(StateError::UnsupportedVersion);
            }
            const auto a = archive->GetBoolean("armed"); const auto r = archive->GetUnsigned("remaining_ticks");
            const auto t = archive->Get("target", DataType::Object);
            bool valid = archive->Type() == DataType::Object && version && a && r && t && archive->Size() == 4;
            if (valid) {
                const auto x = (*t)->GetSigned("x"); const auto z = (*t)->GetSigned("z");
                const auto y = (*t)->GetSigned("y");
                valid = x && z && y && (*t)->Size() == 3 && *x >= -world_coordinate_limit && *x <= world_coordinate_limit
                    && *z >= -world_coordinate_limit && *z <= world_coordinate_limit
                    && *y >= world_min_y && *y <= world_max_y && *r <= config_.delay;
                if (valid) { armed = *a; remaining = *r; target = {static_cast<int>(*x), static_cast<int>(*z), static_cast<int>(*y)}; }
                valid = valid && (armed || (!remaining && target == TileCoord{}));
            }
            if (!valid) {
                if (diagnostics) { diagnostics->Add("Invalid door controller state"); }
                return eastl::unexpected(StateError::InvalidData);
            }
        }
        return eastl::unique_ptr<ItemBehavior>(new DoorBehavior(self, config_, Schema(), armed, remaining, target));
    }
    StateResult<void> ValidateSnapshot(const ItemSnapshotContext& context, PersistentId self,
        ItemCapabilityId capability, Diagnostics* diagnostics) const override {
        const auto* state = context.State(self, capability);
        if (!state) { return eastl::unexpected(StateError::InvalidData); }
        const auto parsed = Create(capability, state, diagnostics);
        if (!parsed) { return eastl::unexpected(parsed.error()); }
        if (!*state->GetBoolean("armed")) { return {}; }
        const auto* item = context.FindItem(self);
        const auto* target = state->Find("target");
        const TileCoord position{static_cast<int>(*target->GetSigned("x")), static_cast<int>(*target->GetSigned("z")),
            static_cast<int>(*target->GetSigned("y"))};
        if (!item || item->location != LocationKind::ground
            || !Adjacent(position, item->position)) {
            if (diagnostics) { diagnostics->Add("Armed door controller must be deployed beside its target"); }
            return eastl::unexpected(StateError::InvalidData);
        }
        return {};
    }
private:
    const DoorConfig config_;
};
}
ContentResult<ParsedItemBehavior> ParseDoorControllerBehavior(const DataObject& input, const ItemDefinition&,
    ItemCapabilityId, Diagnostics* diagnostics) {
    const auto fail = [diagnostics]() -> eastl::unexpected<ContentError> {
        if (diagnostics) { diagnostics->Add("Invalid door controller definition"); }
        return eastl::unexpected(ContentError::InvalidData);
    };
    if (input.Type() != DataType::Object) { return fail(); }
    for (std::size_t i = 0; i < input.Size(); ++i) {
        const auto& key = input.Name(i);
        if (key != "id" && key != "type" && key != "delay_ticks" && key != "action_cost") { return fail(); }
    }
    const auto id = input.GetString("id"); const auto type = input.GetString("type");
    const auto delay = input.GetUnsigned("delay_ticks");
    const auto cost = input.Find("action_cost") ? input.GetUnsigned("action_cost") : DataResult<std::uint64_t>{100};
    if (!id || !ValidName(*id) || !type || *type != "door_controller" || !delay || !*delay || *delay > UINT32_MAX
        || !cost || !*cost || *cost > UINT32_MAX) { return fail(); }
    DataObject normalized;
    normalized.SetUnsigned("delay_ticks", *delay); normalized.SetUnsigned("action_cost", *cost);
    return ParsedItemBehavior{eastl::allocate_shared<DoorDefinition>(FatalAllocator{}, DoorConfig{*delay, static_cast<std::uint32_t>(*cost)}), eastl::move(normalized)};
}
}
