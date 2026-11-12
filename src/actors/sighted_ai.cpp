#include "actors/sighted_ai.h"

#include "actors/ai_vision.h"
#include "combat/combat_system.h"
#include "core/state_reader.h"
#include "core/world.h"
#include <EASTL/array.h>
#include <EASTL/algorithm.h>
#include <cstdlib>

#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
#error Engine must compile without exceptions
#endif

namespace cataclysmx {
namespace {

constexpr TileCoord directions[] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}, {0, 0, 1}, {0, 0, -1}};
constexpr std::uint32_t maxDiscoveryBuckets = 25 * 41;

struct SightedState {
    String profile;
    PersistentId target = 0;
    TileCoord lastPosition{};
    Tick observedTick = 0;
    Tick memoryRemaining = 0;
    Tick scanRemaining = 0;
    Tick searchRemaining = 0;
    AiBehavior behavior = AiBehavior::Idle;
    TileCoord anchor{};
    std::uint32_t bucket = 0;
    PersistentId after = 0;
    bool started = false;
    TileCoord previousPosition{};
    bool hasPrevious = false;
};

TileCoord Add(TileCoord a, TileCoord b) { return {a.x + b.x, a.z + b.z, a.y + b.y}; }
int Distance(TileCoord a, TileCoord b) {
    return std::abs(a.x - b.x) + std::abs(a.z - b.z) + std::abs(a.y - b.y);
}
Tick Decrease(Tick value, Tick elapsed) { return value > elapsed ? value - elapsed : 0; }

void Forget(SightedState& state) {
    state.target = 0;
    state.lastPosition = {};
    state.observedTick = 0;
    state.memoryRemaining = 0;
    state.searchRemaining = 0;
    state.behavior = AiBehavior::Idle;
}

struct Observation {
    PersistentId id = 0;
    ActorHandle handle{};
    TileCoord position{};
    CombatFaction faction = CombatFaction::Neutral;
};

// The decision function below receives only these value observations and local
// movement affordances. It cannot look up a target or inspect distant terrain.
struct DecisionInput {
    PersistentId self = 0;
    TileCoord position{};
    CombatFaction faction = CombatFaction::Neutral;
    bool canAttack = false;
    Tick tick = 0;
    eastl::array<bool, 6> moves{};
    eastl::array<Observation, 37> observations{}; // tracking + four neighbors + 32 candidates
    std::size_t observationCount = 0;
    bool rememberedLocationEmpty = false;
};

bool WithinRange(TileCoord origin, TileCoord target, std::uint32_t range) {
    const std::int64_t dx = std::int64_t{target.x} - origin.x;
    const std::int64_t dz = std::int64_t{target.z} - origin.z;
    const std::int64_t dy = std::int64_t{target.y} - origin.y;
    return dx * dx + dz * dz + dy * dy <= std::int64_t{range} * range;
}

bool DetectPosition(const World& world, TileCoord origin, TileCoord target,
    const AiProfile& profile, AiVisionStats& stats) {
    if (!WithinRange(origin, target, profile.sightRange)) { return false; }
    const auto band = ClassifyLight(world.GetLightLevel(target));
    const auto range = band == LightBand::Dark ? profile.darkRange
        : band == LightBand::Dim ? profile.dimRange : profile.sightRange;
    return WithinRange(origin, target, range) && cataclysmx::AiLineVisible(world, origin, target, &stats);
}

void Observe(const World& world, ActorHandle handle, const AiProfile& profile,
    DecisionInput& input, AiVisionStats& stats) {
    const auto* actor = world.GetActor(handle);
    // Resident-only actors can be observed, but never get a sensing turn here.
    if (!actor || actor->parked || actor->id == input.self) { return; }
    for (std::size_t i = 0; i < input.observationCount; ++i) {
        if (input.observations[i].id == actor->id) { return; }
    }
    if (!DetectPosition(world, input.position, actor->position, profile, stats)) { return; }
    if (input.observationCount == input.observations.size()) { std::abort(); }
    // Baseline sight recognizes allegiance only after successful detection.
    input.observations[input.observationCount++] = {actor->id, handle, actor->position, actor->combat.faction};
}

ChunkCoord DiscoveryBucket(const SightedState& state, const AiProfile& profile) {
    const int radius = static_cast<int>((profile.sightRange + chunk_size - 1) / chunk_size);
    const int width = radius * 2 + 1;
    const int horizontal = static_cast<int>(state.bucket) % (width * width);
    const int layer = static_cast<int>(state.bucket) / (width * width);
    const int dy = layer == 0 ? 0 : (layer % 2 ? (layer + 1) / 2 : -layer / 2);
    ChunkCoord position = ChunkOf(state.anchor);
    position.y += dy;
    if (horizontal == 0) { return position; }
    int index = 1;
    for (int ring = 1; ring <= radius; ++ring) {
        for (int z = -ring; z <= ring; ++z) {
            for (int x = -ring; x <= ring; ++x) {
                if (std::abs(x) != ring && std::abs(z) != ring) { continue; }
                if (index++ == horizontal) { return {position.x + x, position.z + z, position.y}; }
            }
        }
    }
    std::abort();
}

DecisionInput Sense(const World& world, ActorHandle self, SightedState& state,
    const AiProfile& profile, AiInspection& inspection) {
    DecisionInput input;
    const auto* actor = world.GetActor(self);
    if (!actor) { std::abort(); }
    input.self = actor->id;
    input.position = actor->position;
    input.faction = actor->combat.faction;
    input.canAttack = world.Combat().CanAttack(self);
    input.tick = world.GetTick();
    AiVisionStats stats;
    // Tracking and nearby interaction do not consume background discovery's
    // allowance. These checks always finish in this callback's stable world.
    if (state.target) { Observe(world, world.FindActor(state.target), profile, input, stats); }
    for (int i = 0; i < 6; ++i) {
        const auto destination = Add(input.position, directions[i]);
        const auto* tile = world.GetTile(destination);
        if (!tile) { continue; }
        const auto& terrain = world.GetContent().GetTerrain(tile->terrain);
        const auto occupant = world.ActorAt(destination);
        if (i < 4) { Observe(world, occupant, profile, input, stats); }
        // Deliberately local probes, with no identity exposed by occupancy.
        input.moves[i] = world.IsActive(destination) && terrain.passable && !terrain.openSpace && !occupant.value;
        if (i >= 4) {
            const auto* source = world.GetTile(input.position);
            input.moves[i] = input.moves[i] && terrain.stairs && source
                && world.GetContent().GetTerrain(source->terrain).stairs;
        }
    }
    if (state.scanRemaining == 0) {
        const auto radius = (profile.sightRange + chunk_size - 1) / chunk_size;
        const auto width = radius * 2 + 1;
        const auto layers = 2 * eastl::min(profile.sightRange, std::uint32_t{20}) + 1;
        const auto bucketCount = width * width * layers;
        if (!state.started || state.bucket >= bucketCount) {
            state.anchor = input.position;
            state.bucket = 0;
            state.after = 0;
            state.started = true;
        }
        // IDs, never vector indices, are continuation tokens. New lower IDs in
        // a visited bucket are considered on its next pass. Movement does not
        // restart the pass, so continual input changes cannot starve discovery.
        while (inspection.candidates < profile.candidateBudget && inspection.buckets < profile.bucketBudget) {
            ++inspection.buckets;
            const auto handle = world.NextActorInChunk(DiscoveryBucket(state, profile), state.after);
            if (handle.value) {
                const auto* candidate = world.GetActor(handle);
                if (!candidate) { std::abort(); }
                state.after = candidate->id;
                ++inspection.candidates;
                Observe(world, handle, profile, input, stats);
            } else {
                state.after = 0;
                if (++state.bucket == bucketCount) {
                    state.bucket = 0;
                    state.anchor = input.position;
                    break; // At most one full pass in an update.
                }
            }
        }
        state.scanRemaining = profile.scanIntervalTicks;
    }
    if (state.target && DetectPosition(world, input.position, state.lastPosition, profile, stats)) {
        const auto* occupant = world.GetActor(world.ActorAt(state.lastPosition));
        input.rememberedLocationEmpty = !occupant || occupant->id != state.target;
    }
    inspection.rays = stats.rays;
    inspection.cells = stats.cells;
    return input;
}

void Remember(SightedState& state, const Observation& observation, Tick tick, const AiProfile& profile) {
    state.target = observation.id;
    state.lastPosition = observation.position;
    state.observedTick = tick;
    state.memoryRemaining = profile.memoryTicks;
    state.searchRemaining = 0;
    state.behavior = AiBehavior::Pursue;
}

ActorIntent Decide(const DecisionInput& input, SightedState& state, const AiProfile& profile,
    AiInspection& inspection, std::uint32_t& rng) {
    const Observation* tracked = nullptr;
    const Observation* best = nullptr;
    for (std::size_t i = 0; i < input.observationCount; ++i) {
        const auto& observation = input.observations[i];
        if (!Hostile(input.faction, observation.faction)) { continue; }
        if (observation.id == state.target) { tracked = &observation; }
        if (!best || Distance(input.position, observation.position) < Distance(input.position, best->position)
            || (Distance(input.position, observation.position) == Distance(input.position, best->position)
                && observation.id < best->id)) { best = &observation; }
    }
    if (tracked) { Remember(state, *tracked, input.tick, profile); }
    if (best && (state.target == 0 || (best->id != state.target
        && Distance(input.position, best->position) + static_cast<int>(profile.switchAdvantage)
            < Distance(input.position, state.lastPosition)))) {
        Remember(state, *best, input.tick, profile);
        tracked = best;
    }
    inspection.targetVisible = tracked && tracked->id == state.target;
    if (inspection.targetVisible && input.canAttack && tracked->position.y == input.position.y
        && Distance(input.position, tracked->position) == 1) {
        state.behavior = AiBehavior::Attack;
        return {ActorIntentKind::Attack, {}, 100, tracked->handle};
    }
    if (state.target && state.behavior != AiBehavior::Search) {
        state.behavior = AiBehavior::Pursue;
        if (input.position == state.lastPosition && input.rememberedLocationEmpty) {
            state.behavior = AiBehavior::Search;
            state.searchRemaining = eastl::min<Tick>(profile.searchTicks, state.memoryRemaining);
        }
    }
    int selected = -1;
    if (state.target && state.behavior == AiBehavior::Pursue) {
        int bestDistance = Distance(input.position, state.lastPosition) + 2;
        // Stable per-identity tie phase prevents every creature preferring +X.
        for (int offset = 0; offset < 6; ++offset) {
            const int i = (static_cast<int>(input.self % 6) + offset) % 6;
            const auto destination = Add(input.position, directions[i]);
            if (!input.moves[i] || (state.hasPrevious && destination == state.previousPosition)) { continue; }
            const int distance = Distance(destination, state.lastPosition);
            if (distance < bestDistance) { bestDistance = distance; selected = i; }
        }
    } else {
        // One explicit draw for idle/search attention. No random numbers are
        // consumed by sensing, continuation, or target ranking.
        rng = rng * 1'664'525U + 1'013'904'223U;
        for (int offset = 0; offset < 4; ++offset) {
            const int i = (static_cast<int>(rng >> 30) + offset) % 4;
            const auto destination = Add(input.position, directions[i]);
            if (!input.moves[i] || (state.hasPrevious && destination == state.previousPosition)) { continue; }
            if (state.behavior == AiBehavior::Search && Distance(destination, state.lastPosition) > 2) { continue; }
            selected = i;
            break;
        }
    }
    // When boxed in, wait once and release the anti-backtracking preference.
    // The next decision can reverse, without alternating every action forever.
    state.hasPrevious = selected >= 0;
    if (selected < 0) { return {}; }
    state.previousPosition = input.position;
    return {ActorIntentKind::Move, directions[selected], 100};
}

DataObject PositionState(TileCoord position) {
    DataObject result;
    result.SetSigned("x", position.x);
    result.SetSigned("z", position.z);
    result.SetSigned("y", position.y);
    return result;
}

StateResult<TileCoord> ReadPosition(const DataObject& state, eastl::string_view field, Diagnostics* diagnostics) {
    const auto data = state.Get(field, DataType::Object);
    if (data) {
        const auto x = (*data)->GetSigned("x");
        const auto z = (*data)->GetSigned("z");
        const auto y = (*data)->GetSigned("y");
        if ((*data)->Size() == 3 && x && z && y && *x >= -world_coordinate_limit && *x <= world_coordinate_limit
            && *z >= -world_coordinate_limit && *z <= world_coordinate_limit && *y >= world_min_y && *y <= world_max_y) {
            return TileCoord{static_cast<int>(*x), static_cast<int>(*z), static_cast<int>(*y)};
        }
    }
    return cataclysmx::InvalidState(diagnostics, "actors.controller.state", field, "expected a valid signed {x,z,y} position");
}

StateResult<SightedState> ReadState(const DataObject& data, Diagnostics* diagnostics) {
    StateReader reader(data, diagnostics, "actors.controller.state");
    const auto version = reader.GetUnsignedOrError("version");
    SightedState state;
    const auto profile = reader.GetStringOrError("profile");
    state.target = reader.GetUnsignedOrError("target");
    state.observedTick = reader.GetUnsignedOrError("observed_tick");
    state.memoryRemaining = reader.GetUnsignedOrError("memory_remaining");
    state.scanRemaining = reader.GetUnsignedOrError("scan_remaining");
    state.searchRemaining = reader.GetUnsignedOrError("search_remaining");
    const auto behavior = reader.GetUnsignedOrError("behavior");
    const auto bucket = reader.GetUnsignedOrError("bucket");
    state.after = reader.GetUnsignedOrError("after");
    state.started = reader.GetBooleanOrError("started");
    state.hasPrevious = reader.GetBooleanOrError("has_previous");
    if (const auto result = reader.Result(); !result) { return eastl::unexpected(result.error()); }
    if (version != 1) { return reader.Invalid("version", "unsupported sighted controller version", StateError::UnsupportedVersion); }
    if (data.Size() != 15) { return reader.Invalid({}, "unknown or missing controller state field"); }
    for (std::size_t i = 0; i < profile.size(); ++i) {
        const char ch = profile[i];
        if (!((ch >= 'a' && ch <= 'z') || (i > 0 && ((ch >= '0' && ch <= '9') || ch == '_')))) {
            return reader.Invalid("profile", "invalid AI profile id");
        }
    }
    const auto last = ReadPosition(data, "last_position", diagnostics);
    const auto anchor = ReadPosition(data, "anchor", diagnostics);
    const auto previous = ReadPosition(data, "previous_position", diagnostics);
    if (!last) { return eastl::unexpected(last.error()); }
    if (!anchor) { return eastl::unexpected(anchor.error()); }
    if (!previous) { return eastl::unexpected(previous.error()); }
    if (state.memoryRemaining > 60000 || state.scanRemaining > 60000 || state.searchRemaining > state.memoryRemaining
        || behavior > static_cast<unsigned>(AiBehavior::Attack) || bucket >= maxDiscoveryBuckets) {
        return reader.Invalid({}, "invalid AI timers, behavior, or discovery cursor");
    }
    state.behavior = static_cast<AiBehavior>(behavior);
    state.bucket = static_cast<std::uint32_t>(bucket);
    state.profile.assign(profile.data(), profile.size());
    state.lastPosition = *last;
    state.anchor = *anchor;
    state.previousPosition = *previous;
    if ((state.target == 0) != (state.memoryRemaining == 0)
        || (state.target == 0) != (state.behavior == AiBehavior::Idle)
        || (state.behavior == AiBehavior::Search) != (state.searchRemaining != 0)
        || (!state.target && (state.observedTick != 0 || state.lastPosition != TileCoord{}))
        || (!state.started && (state.bucket != 0 || state.after != 0 || state.anchor != TileCoord{}))) {
        return reader.Invalid({}, "inconsistent AI memory or cursor state");
    }
    return state;
}

class SightedController final : public AiController {
public:
    explicit SightedController(SightedState state) : state_(eastl::move(state)) {}
    AiControllerKind Kind() const override { return AiControllerKind::Sighted; }
    ActorIntent ChooseAction(const ActorReadContext& context, std::uint32_t& rng) override {
        const auto& world = context.GetWorld();
        AiProfile defaults;
        const AiProfile* profile = &defaults;
        if (!state_.profile.empty()) {
            profile = world.GetContent().TryGetAiProfile(world.GetContent().FindAiProfile(state_.profile));
            if (!profile) { std::abort(); } // Validated at configuration/load boundaries.
        }
        inspection_ = {};
        const auto input = Sense(world, context.Self(), state_, *profile, inspection_);
        return Decide(input, state_, *profile, inspection_, rng);
    }
    void Elapse(Tick elapsed) override {
        state_.scanRemaining = Decrease(state_.scanRemaining, elapsed);
        state_.memoryRemaining = Decrease(state_.memoryRemaining, elapsed);
        state_.searchRemaining = Decrease(state_.searchRemaining, elapsed);
        if (state_.memoryRemaining == 0 || (state_.behavior == AiBehavior::Search && state_.searchRemaining == 0)) {
            Forget(state_);
            inspection_.targetVisible = false;
        }
    }
    AiInspection Inspect() const override {
        auto result = inspection_;
        result.behavior = state_.behavior;
        result.target = state_.target;
        result.lastKnownPosition = state_.lastPosition;
        result.observedTick = state_.observedTick;
        result.memoryRemaining = state_.memoryRemaining;
        result.scanRemaining = state_.scanRemaining;
        return result;
    }
    DataObject SaveState() const override {
        DataObject result;
        result.SetUnsigned("version", 1);
        result.SetString("profile", state_.profile);
        result.SetUnsigned("target", state_.target);
        result.Set("last_position", PositionState(state_.lastPosition));
        result.SetUnsigned("observed_tick", state_.observedTick);
        result.SetUnsigned("memory_remaining", state_.memoryRemaining);
        result.SetUnsigned("scan_remaining", state_.scanRemaining);
        result.SetUnsigned("search_remaining", state_.searchRemaining);
        result.SetUnsigned("behavior", static_cast<unsigned>(state_.behavior));
        result.Set("anchor", PositionState(state_.anchor));
        result.SetUnsigned("bucket", state_.bucket);
        result.SetUnsigned("after", state_.after);
        result.SetBoolean("started", state_.started);
        result.Set("previous_position", PositionState(state_.previousPosition));
        result.SetBoolean("has_previous", state_.hasPrevious);
        return result;
    }
private:
    SightedState state_;
    AiInspection inspection_;
};

}

StateResult<void> ValidateSightedAiState(const DataObject& state, Diagnostics* diagnostics) {
    const auto result = ReadState(state, diagnostics);
    if (!result) { return eastl::unexpected(result.error()); }
    return {};
}

StateResult<eastl::unique_ptr<AiController>> RestoreSightedAiController(const DataObject* state, Diagnostics* diagnostics) {
    SightedState restored;
    if (state) {
        auto parsed = ReadState(*state, diagnostics);
        if (!parsed) { return eastl::unexpected(parsed.error()); }
        restored = eastl::move(*parsed);
    }
    return eastl::unique_ptr<AiController>(new SightedController(eastl::move(restored)));
}

eastl::unique_ptr<AiController> MakeSightedAiController(eastl::string_view profile) {
    SightedState state;
    state.profile.assign(profile.data(), profile.size());
    return eastl::unique_ptr<AiController>(new SightedController(eastl::move(state)));
}

}
