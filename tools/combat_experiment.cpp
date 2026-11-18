#include "core/world.h"
#include "core/simulation.h"
#include "core/ratio.h"
#include "actors/movement.h"
#include "combat/combat_system.h"
#include "skills/learning_system.h"

#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <charconv>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <locale>

namespace {
using namespace cataclysmx;

struct Options {
    std::uint32_t seed = 1;
    std::uint32_t seeds = 100;
    Tick limit = 30000;
    std::uint32_t playerDelay = 0;
    std::uint32_t enemyDelay = 0;
    int skill = -1;
    eastl::string_view scenario = "duel";
    eastl::string_view loadout = "all";
    eastl::string_view style = "basic_balanced";
    eastl::string_view enemy = "cellar_zombie";
    eastl::string_view outfit = "standard";
    eastl::string_view policy = "attack";
    eastl::string_view enemyAi = "melee";
    std::uint64_t artXp = 0;
    CombatStyleId styleId{};
    MonsterTypeId enemyId{};
    const char* csv = nullptr;
    bool plainCsv = false;
    bool enemyFirst = false;
    bool listCatalog = false;
    const char* weapon = nullptr;
    const char* offhand = nullptr;
    eastl::string_view grip = "one_handed";
    Vector<const char*> wear;
    String wearIds;

    bool Cautious() const { return policy == "cautious" && scenario != "passive"; }
};

struct Loadout {
    eastl::string_view name;
    const char* left = nullptr;
    GripKind grip = GripKind::OneHanded;
    const char* right = nullptr;
    bool inDefaultSweep = true;
};
constexpr Loadout loadouts[] = {
    {"unarmed"},
    {"swordshield", "arming_sword", GripKind::OneHanded, "wooden_shield"},
    {"twohand", "arming_sword", GripKind::TwoHanded},
    {"dual", "arming_sword", GripKind::OneHanded, "arming_sword"},
    {"hammer", "claw_hammer", GripKind::OneHanded, nullptr, false},
    {"knife", "chef_knife", GripKind::OneHanded, nullptr, false},
    {"sword", "arming_sword", GripKind::OneHanded, nullptr, false},
    {"axe", "fire_axe", GripKind::TwoHanded, nullptr, false},
};

template<class T> bool Number(eastl::string_view text, T& value) {
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    return !text.empty() && parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}
void Require(bool condition, const char* operation) {
    if (!condition) { std::cerr << "Experiment failed: " << operation << '\n'; std::exit(1); }
}
template<class T, class E> T Take(eastl::expected<T, E> result, const char* operation) {
    Require(result.has_value(), operation);
    return eastl::move(*result);
}

struct SideMetrics {
    std::uint64_t strikes = 0, invalidStrikes = 0, misses = 0, dodges = 0, parries = 0, contacts = 0;
    std::uint64_t blocks = 0, armorStops = 0, counters = 0;
    std::uint64_t hpLoss = 0, maxHpLoss = 0, blockAbsorbed = 0, armorAbsorbed = 0;
    std::uint64_t commitments = 0, invalidations = 0, staminaSpent = 0;
    std::uint64_t windups = 0, wastedWindups = 0, exhaustedActions = 0;
};
struct Run {
    std::uint32_t seed = 0;
    eastl::string_view outcome = "timeout";
    Tick ticks = 0;
    unsigned kills = 0;
    unsigned finalStamina = 0;
    std::uint64_t waits = 0;
    std::uint64_t approachSteps = 0, retreatSteps = 0, fleeSteps = 0, rests = 0;
    SideMetrics outgoing, incoming;
};
struct MoveFrequency {
    CombatMoveId move{};
    std::uint64_t playerCommitted = 0, enemyCommitted = 0, playerInvalidated = 0, enemyInvalidated = 0;
};

void Collect(World& world, PersistentId player, Run& run, Vector<CombatEvent>& events,
    Vector<MoveFrequency>& frequencies, eastl::optional<Tick>& deathTick) {
    world.Combat().DrainEvents(events);
    for (const auto& event : events) {
        if (event.kind == CombatEventKind::ActorDied) {
            deathTick = event.tick;
            continue;
        }
        if (event.kind == CombatEventKind::ActionCommitted || event.kind == CombatEventKind::ActionInvalidated) {
            const bool own = event.actor == player;
            auto& side = own ? run.outgoing : run.incoming;
            if (event.kind == CombatEventKind::ActionCommitted) { ++side.commitments; side.staminaSpent += event.staminaSpent; }
            else { ++side.invalidations; }
            if (const auto* move = world.GetContent().TryGetCombatMove(event.move)) {
                if (event.kind == CombatEventKind::ActionCommitted) {
                    side.windups += move->cost.windupCost != 0;
                    side.exhaustedActions += move->requirements.exhaustedOnly;
                } else { side.wastedWindups += move->cost.windupCost != 0; }
            }
            auto found = eastl::find_if(frequencies.begin(), frequencies.end(), [&](const auto& value) { return value.move == event.move; });
            if (found == frequencies.end()) { frequencies.push_back({event.move}); found = frequencies.end() - 1; }
            if (event.kind == CombatEventKind::ActionCommitted) { ++(own ? found->playerCommitted : found->enemyCommitted); }
            else { ++(own ? found->playerInvalidated : found->enemyInvalidated); }
        }
        if (event.kind != CombatEventKind::Strike) { continue; }
        const auto& strike = event.strike;
        auto& side = strike.attacker == player ? run.outgoing : run.incoming;
        ++side.strikes;
        switch (strike.outcome) {
        case StrikeOutcome::Invalidated: ++side.invalidStrikes; break;
        case StrikeOutcome::Miss: ++side.misses; break;
        case StrikeOutcome::Dodged: ++side.dodges; break;
        case StrikeOutcome::Parried: ++side.parries; break;
        case StrikeOutcome::Contact: ++side.contacts; break;
        }
        side.blocks += strike.response == DefenseKind::Block && strike.defenseSucceeded;
        side.armorStops += strike.armorStopped;
        side.counters += strike.counter;
        side.hpLoss += strike.hpLoss;
        side.maxHpLoss += strike.maxHpLoss;
        side.blockAbsorbed += strike.blockAbsorbed;
        side.armorAbsorbed += strike.armorAbsorbed;
    }
}

constexpr TileCoord exits[] = {{0, 8}, {8, 0}, {chunk_size - 1, 8}, {8, chunk_size - 1}};

bool IsExit(TileCoord position) {
    for (const auto exit : exits) { if (position == exit) { return true; } }
    return false;
}

void PrepareArena(World& world, const ContentCatalog& content) {
    Take(world.RequestChunks({{}, 0}, LoadMode::active), "request arena");
    Require(world.ReconcileChunks().has_value(), "load arena");
    // Discard generated occupants/furniture at the setup boundary. Catalog rules
    // and monster recipes remain untouched; the controlled arena is the fixture.
    Vector<ActorHandle> actors;
    world.CollectActiveActors(actors);
    for (const auto actor : actors) { Require(world.DestroyActor(actor).has_value(), "clear generated actor"); }
    Vector<ObjectHandle> objects;
    world.CollectActiveObjects(objects);
    for (const auto object : objects) { Require(world.DestroyObject(object).has_value(), "clear generated furniture"); }
    const auto floor = content.FindTerrain("floor"), wall = content.FindTerrain("wall");
    Require(floor.value && wall.value, "floor/wall terrain definitions");
    for (int z = 0; z < chunk_size; ++z) {
        for (int x = 0; x < chunk_size; ++x) {
            const bool edge = x == 0 || z == 0 || x == chunk_size - 1 || z == chunk_size - 1;
            const bool closed = edge && !IsExit({x, z}); // Identical terrain for both player policies.
            Require(world.SetTile({x, z}, {closed ? wall : floor, 0}).has_value(), "level arena floor");
        }
    }
    world.SetSkyLight(100);
}
void Hold(World& world, ActorHandle actor, const char* item, Hand hand, GripKind grip) {
    const auto type = world.GetContent().FindItem(item);
    Require(type.value != 0, item);
    const auto handle = Take(world.CreateItem(type, ItemLocation::Carried(actor)), "create loadout item");
    Require(world.HoldItem(actor, handle, hand, grip).outcome == InventoryOutcome::Succeeded, "hold loadout item");
}
ActorHandle Enemy(World& world, TileCoord position, const Options& options) {
    const auto actor = Take(world.CreateMonster(options.enemyId, position), "create enemy");
    if (options.enemyAi == "melee") {
        Require(world.SetActorController(actor, AiControllerKind::Melee).has_value(), "set melee AI");
    }
    if (options.enemyDelay) { Require(world.ScheduleActorAction(actor, options.enemyDelay).has_value(), "enemy starting delay"); }
    return actor;
}

void Equip(World& world, ActorHandle player, const Loadout& loadout, const Options& options) {
    if (loadout.left) { Hold(world, player, loadout.left, Hand::Left, loadout.grip); }
    if (loadout.right) { Hold(world, player, loadout.right, Hand::Right, GripKind::OneHanded); }
    const auto wear = [&](const char* name) {
        const auto type = world.GetContent().FindItem(name);
        Require(type.value != 0, name);
        const auto item = Take(world.CreateItem(type, ItemLocation::Carried(player)), "create clothing");
        Require(world.WearItem(player, item).outcome == InventoryOutcome::Succeeded, name);
    };
    if (options.outfit == "work") {
        for (const char* name : {"cotton_tshirt", "denim_jeans", "rain_jacket"}) { wear(name); }
    }
    if (options.outfit != "none") {
        for (const char* name : {"leather_work_gloves", "work_boots"}) { wear(name); }
    }
    for (const auto* item : options.wear) { wear(item); }
}

void SetInitialStyle(World& world, ActorHandle player, const Options& options) {
    const auto art = world.GetContent().GetCombatStyle(options.styleId).art;
    if (art.value) {
        auto& combat = world.Combat();
        Require(combat.GrantArt(player, art, ArtGrantReason::Administration).has_value(), "grant owning art");
        // Seed the real character progress as a fixture, then rebuild the
        // production grants with the selected skills and XP already installed.
        auto& arts = world.GetActor(player)->characterCombat->arts;
        const auto entry = eastl::find_if(arts.begin(), arts.end(), [&](const auto& value) { return value.art == art; });
        Require(entry != arts.end(), "find granted art progress");
        entry->xp = options.artXp;
        Require(combat.GrantArt(player, art, ArtGrantReason::Administration).has_value(), "refresh art grants");
    }
    // Initial state, not a paid mid-fight style change.
    auto state = world.GetActor(player)->combat;
    state.styleId = options.styleId;
    Require(world.Combat().Configure(player, state).has_value(), "select initial style");
}

int Distance(TileCoord a, TileCoord b) {
    return eastl::max(std::abs(a.x - b.x), std::abs(a.z - b.z));
}

int ExitDistance(TileCoord position) {
    int distance = chunk_size;
    for (const auto exit : exits) { distance = eastl::min(distance, Distance(position, exit)); }
    return distance;
}

struct CautiousPolicy {
    bool fleeing = false;
    bool recovering = false;

    bool Act(World& world, ActorHandle player, const Vector<ActorHandle>& enemies, std::uint32_t& rng, Run& run) {
        const auto* actor = world.GetActor(player);
        const auto position = actor->position;
        const auto& body = actor->combat.body;
        for (const auto vital : {BodyPart::Head, BodyPart::Torso}) {
            fleeing |= body.parts[static_cast<std::size_t>(vital)].hp <= UnreducedMax(vital) * 4000 / ratioScale;
        }
        const auto stamina = actor->combat.stamina;
        recovering = recovering ? stamina < 45 : stamina <= 25;
        SmallVector<TileCoord, 3> threats;
        bool threatened = false;
        for (const auto enemy : enemies) {
            if (!world.IsActorAlive(enemy)) { continue; }
            const auto* other = world.GetActor(enemy);
            if (Hostile(actor->combat.faction, other->combat.faction) && world.CanPlayerSee(player, other->position)) {
                threats.push_back(other->position);
                threatened |= world.Combat().CanReach(enemy, player);
            }
        }
        const auto nearest = [&](TileCoord from) {
            int distance = chunk_size;
            for (const auto threat : threats) { distance = eastl::min(distance, Distance(from, threat)); }
            return distance;
        };
        const auto step = [&](const auto& score, bool allowExit, std::uint64_t& counter) {
            auto best = score(position);
            eastl::optional<TileCoord> direction;
            for (const auto delta : actorStepDirections) {
                if (delta.y != 0) { continue; }
                const TileCoord tile{position.x + delta.x, position.z + delta.z, position.y};
                if ((!allowExit && IsExit(tile)) || !world.IsActive(tile) || world.BlocksMovement(tile)
                    || world.ActorAt(tile).value != 0) { continue; }
                const auto candidate = score(tile);
                if (candidate < best) { best = candidate; direction = delta; }
            }
            if (!direction || !world.TryActorStep(player, *direction, true)) { return false; }
            const auto cost = ActorStepBaseCost(world.ActorMovementBaseCost(player), *direction);
            Require(world.SetActorExertion(player, ExertionKind::Moving).has_value(), "set movement exertion");
            Require(world.ScheduleActorAction(player, cost).has_value(), "schedule movement");
            ++counter;
            return true;
        };
        if (fleeing) {
            // Prioritize exit progress, breaking ties by space from visible threats.
            return step([&](TileCoord from) { return 4 * ExitDistance(from) - eastl::min(nearest(from), 3); }, true, run.fleeSteps);
        }
        const auto separation = nearest(position);
        if (recovering) {
            return threatened && step([&](TileCoord from) { return -nearest(from); }, false, run.retreatSteps);
        }
        const auto target = world.Combat().FindTarget(player);
        if (target != ActorHandle{} && world.Combat().Attack(player, target, rng).committed) { return true; }
        return !threats.empty() && separation > 1 && step(nearest, false, run.approachSteps);
    }
};

Run Experiment(const ContentCatalog& content, const Options& options, const Loadout& loadout, unsigned skill,
    std::uint32_t seed, Vector<MoveFrequency>& frequencies) {
    World world(content, seed);
    PrepareArena(world, content);
    world.Combat().SetTrainingEnabled(false);
    const unsigned totalEnemies = options.scenario == "crowd" ? 3 : options.scenario == "attrition" ? 5 : 1;
    Vector<ActorHandle> enemies;
    const auto spawnInitial = [&] {
        enemies.push_back(Enemy(world, {9, 8}, options));
        if (options.scenario == "crowd") {
            enemies.push_back(Enemy(world, {8, 9}, options));
            enemies.push_back(Enemy(world, {7, 8}, options));
        }
    };
    if (options.enemyFirst) { spawnInitial(); }
    const auto player = Take(world.CreateActor({8, 8}, true), "create controlled survivor");
    const PersistentId playerId = world.GetActor(player)->id;
    if (!options.enemyFirst) { spawnInitial(); }
    for (std::uint32_t i = 1; i <= content.SkillCount(); ++i) {
        if (content.GetSkill({i}).category == SkillCategory::Combat) {
            Require(world.Learning().SetLevel(player, {i}, static_cast<std::uint8_t>(skill)).has_value(), "set combat skill level");
        }
    }
    Equip(world, player, loadout, options);
    SetInitialStyle(world, player, options);
    if (options.playerDelay) { Require(world.ScheduleActorAction(player, options.playerDelay).has_value(), "starting player delay"); }
    Vector<CombatEvent> events;
    world.Combat().DrainEvents(events); // Setup is excluded from measured costs.
    Run run; run.seed = seed;
    CautiousPolicy cautious;
    eastl::optional<Tick> deathTick;
    std::uint32_t rng = seed;
    unsigned spawned = static_cast<unsigned>(enemies.size());
    Tick horizon = world.GetTick();
    while (world.GetTick() < options.limit) {
        // Stop at each scheduler-selected boundary so death never leaves an
        // unobserved tail of NPC turns, even without a living input barrier.
        const auto result = AdvanceUntilInput(world, rng, horizon);
        Collect(world, playerId, run, events, frequencies, deathTick);
        if (!world.IsActorAlive(player)) { run.outcome = "loss"; break; }
        run.kills = 0;
        for (const auto enemy : enemies) { run.kills += !world.IsActorAlive(enemy); }
        if (run.kills == totalEnemies) { run.outcome = "win"; break; }
        // A step reaches its tile immediately. Only count an escape after the
        // full movement delay and intervening scheduler resolutions are survived.
        if (cautious.fleeing && result.stop == SimulationStop::InputRequired && IsExit(world.GetActor(player)->position)) {
            run.outcome = "escape";
            break;
        }
        if (options.scenario == "attrition" && run.kills == spawned && spawned < totalEnemies) {
            const auto position = world.GetActor(player)->position;
            eastl::optional<TileCoord> next;
            for (const auto direction : actorStepDirections) {
                const TileCoord candidate{position.x + direction.x, position.z + direction.z, position.y};
                if (!IsExit(candidate) && !world.BlocksMovement(candidate) && world.ActorAt(candidate) == ActorHandle{}) { next = candidate; break; }
            }
            Require(next.has_value(), "free attrition spawn tile");
            enemies.push_back(Enemy(world, *next, options));
            ++spawned;
            horizon = world.GetTick();
            continue;
        }
        if (world.GetTick() == options.limit) { break; }
        Require(result.stop != SimulationStop::NoEvents, "scheduler has no combat work");
        if (result.stop != SimulationStop::InputRequired) {
            Require(result.nextEventIn != 0, "scheduler next event");
            horizon = world.GetTick() + eastl::min(result.nextEventIn, options.limit - world.GetTick());
            continue;
        }
        bool committed = false;
        if (options.Cautious()) {
            committed = cautious.Act(world, player, enemies, rng, run);
        } else if (options.scenario != "passive") {
            // Production targeting performs perception/hostility/reach checks.
            // No harness RNG draw, attack formula, or manual NPC turn ordering.
            const auto target = world.Combat().FindTarget(player);
            if (target != ActorHandle{}) { committed = world.Combat().Attack(player, target, rng).committed; }
        }
        if (!committed) {
            ++run.waits;
            if (options.Cautious()) {
                ++run.rests;
                Require(world.SetActorExertion(player, ExertionKind::Rest).has_value(), "set rest exertion");
            }
            Require(world.ScheduleActorAction(player, ticks_per_turn).has_value(), "wait for next decision");
        }
        Collect(world, playerId, run, events, frequencies, deathTick);
        horizon = world.GetTick();
    }
    world.FinalizeActorDeaths();
    Collect(world, playerId, run, events, frequencies, deathTick);
    run.kills = 0;
    for (const auto enemy : enemies) { run.kills += !world.IsActorAlive(enemy); }
    run.ticks = run.outcome == "win" || run.outcome == "loss" ? deathTick.value_or(world.GetTick()) : world.GetTick();
    if (const auto* survivor = world.GetActor(player)) { run.finalStamina = survivor->combat.stamina; }
    Require(world.CheckInvariants(), "final world invariants");
    return run;
}

class CsvWriter {
public:
    CsvWriter(std::ostream& output, bool excel) : output_(output), separator_(excel ? ';' : ',') {
        output_.imbue(std::locale::classic());
        if (excel) { output_ << "\xEF\xBB\xBF" "sep=;\r\n"; }
    }

    template<class... T> void Fields(const T&... values) { (Field(values), ...); }
    void Columns(eastl::string_view names) {
        // Static column lists contain only unquoted names.
        for (;;) {
            const auto comma = names.find(',');
            Field(names.substr(0, comma));
            if (comma == eastl::string_view::npos) { return; }
            names.remove_prefix(comma + 1);
        }
    }
    void EndRow() {
        output_ << (separator_ == ';' ? "\r\n" : "\n");
        first_ = true;
    }

private:
    void Delimit() {
        if (!first_) { output_.put(separator_); }
        first_ = false;
    }
    void Field(eastl::string_view text) {
        Delimit();
        const bool quote = text.find(separator_) != eastl::string_view::npos || text.find_first_of("\"\r\n") != eastl::string_view::npos;
        if (quote) { output_.put('"'); }
        for (const char ch : text) {
            if (ch == '"') { output_.put('"'); }
            output_.put(ch);
        }
        if (quote) { output_.put('"'); }
    }
    void Field(const char* text) { Field(eastl::string_view(text)); }
    template<class T> void Field(T value) { Delimit(); output_ << value; }

    std::ostream& output_;
    char separator_;
    bool first_ = true;
};

void ListCatalog(const ContentCatalog& content) {
    CsvWriter output(std::cout, false);
    output.Columns("kind,id,name,grip,art,handling_skill,obsolete"); output.EndRow();
    for (std::uint32_t i = 1; i <= content.MonsterCount(); ++i) {
        const auto& monster = content.GetMonster({i});
        output.Fields("enemy", monster.id.c_str(), monster.name.c_str(), "", "", "", false); output.EndRow();
    }
    for (std::uint32_t i = 1; i <= content.ItemCount(); ++i) {
        const auto& item = content.GetItem({i});
        const bool obsolete = item.flags.Contains(content.FindFlag("obsolete"));
        if (item.melee) {
            for (const auto& grip : item.melee->grips) {
                output.Fields("weapon", item.id.c_str(), item.name.c_str(),
                    grip.kind == GripKind::TwoHanded ? "two_handed" : "one_handed", "",
                    item.melee->handlingSkill.value ? content.GetSkill(item.melee->handlingSkill).id.c_str() : "",
                    obsolete); output.EndRow();
            }
        }
        if (item.armor) {
            output.Fields("armor", item.id.c_str(), item.name.c_str(), "", "", "", obsolete); output.EndRow();
        }
    }
    for (std::uint32_t i = 1; i <= content.CombatStyleCount(); ++i) {
        const auto& style = content.GetCombatStyle({i});
        output.Fields("style", style.id.c_str(), style.name.c_str(), "",
            style.art.value ? content.GetCombatArt(style.art).id.c_str() : "", "", false); output.EndRow();
    }
    Require(std::cout.good(), "write catalog output");
}

constexpr auto setupColumns = "enemy,outfit,policy,art,art_xp,weapon,grip,offhand,wear,enemy_ai";

void WriteSetup(CsvWriter& output, const Options& options, eastl::string_view art) {
    output.Fields(options.enemy, options.outfit, options.policy, art, options.artXp);
    output.Fields(options.weapon ? options.weapon : "", options.weapon ? options.grip : "",
        options.offhand ? options.offhand : "", options.wearIds.c_str(), options.enemyAi);
}

void SideHeader(CsvWriter& output, const char* prefix) {
    for (const char* field : {"strikes", "invalid_strikes", "misses", "dodges", "parries", "contacts", "blocks", "armor_stops", "counters",
        "hp_loss_100", "max_hp_loss_100", "block_absorbed_100", "armor_absorbed_100", "commitments", "invalidations", "stamina_spent",
        "windups", "wasted_windups", "exhausted_actions"}) {
        const String name = String(prefix) + '_' + field;
        output.Fields(name.c_str());
    }
}
void WriteSide(CsvWriter& output, const SideMetrics& side) {
    output.Fields(side.strikes, side.invalidStrikes, side.misses, side.dodges, side.parries, side.contacts, side.blocks,
        side.armorStops, side.counters, side.hpLoss, side.maxHpLoss, side.blockAbsorbed, side.armorAbsorbed, side.commitments,
        side.invalidations, side.staminaSpent, side.windups, side.wastedWindups, side.exhaustedActions);
}
double Rate(std::uint64_t numerator, std::uint64_t denominator) {
    return denominator ? static_cast<double>(numerator) / static_cast<double>(denominator) : 0.0;
}
std::uint64_t Percentile(Vector<std::uint64_t>& scratch, unsigned percent) {
    eastl::sort(scratch.begin(), scratch.end());
    return scratch[(scratch.size() * percent + 99) / 100 - 1];
}
void Summary(CsvWriter& output, const Vector<Run>& runs, const Options& options, eastl::string_view loadout, unsigned skill,
    eastl::string_view art) {
    std::uint64_t wins = 0, losses = 0, escapes = 0, timeouts = 0, strikes = 0, misses = 0, dodges = 0, parries = 0, blocks = 0, armorStops = 0;
    std::uint64_t commitments = 0, invalidations = 0, windups = 0, wastedWindups = 0, exhaustedActions = 0;
    std::uint64_t approachSteps = 0, retreatSteps = 0, fleeSteps = 0, rests = 0;
    SideMetrics incoming;
    Vector<std::uint64_t> ticks, damage, stamina, dealt, blocked, armored, winDamage, winMaxLoss;
    for (const auto& run : runs) {
        if (run.outcome == "win") {
            ++wins;
            winDamage.push_back(run.incoming.hpLoss);
            winMaxLoss.push_back(run.incoming.maxHpLoss);
        }
        losses += run.outcome == "loss";
        escapes += run.outcome == "escape";
        timeouts += run.outcome == "timeout";
        approachSteps += run.approachSteps; retreatSteps += run.retreatSteps; fleeSteps += run.fleeSteps; rests += run.rests;
        ticks.push_back(run.ticks); damage.push_back(run.incoming.hpLoss); stamina.push_back(run.outgoing.staminaSpent);
        dealt.push_back(run.outgoing.hpLoss); blocked.push_back(run.incoming.blockAbsorbed); armored.push_back(run.incoming.armorAbsorbed);
        incoming.strikes += run.incoming.strikes; incoming.dodges += run.incoming.dodges; incoming.parries += run.incoming.parries;
        incoming.blocks += run.incoming.blocks; incoming.armorStops += run.incoming.armorStops;
        strikes += run.outgoing.strikes; misses += run.outgoing.misses; dodges += run.outgoing.dodges; parries += run.outgoing.parries;
        blocks += run.outgoing.blocks; armorStops += run.outgoing.armorStops;
        commitments += run.outgoing.commitments; invalidations += run.outgoing.invalidations;
        windups += run.outgoing.windups; wastedWindups += run.outgoing.wastedWindups; exhaustedActions += run.outgoing.exhaustedActions;
    }
    output.Fields(options.scenario, options.style, options.seed, options.limit, options.enemyFirst, options.playerDelay, options.enemyDelay,
        loadout, skill, runs.size(), wins, losses, timeouts, Rate(wins, runs.size()), Rate(losses, runs.size()), Rate(timeouts, runs.size()),
        Percentile(ticks, 50), Percentile(ticks, 90), Percentile(damage, 50), Percentile(damage, 90), Percentile(stamina, 50),
        Percentile(stamina, 90), Rate(misses, strikes), Rate(dodges, strikes), Rate(parries, strikes), Rate(blocks, strikes),
        Rate(armorStops, strikes), Rate(invalidations, commitments), Rate(wastedWindups, windups), Rate(exhaustedActions, commitments),
        Rate(incoming.dodges, incoming.strikes), Rate(incoming.parries, incoming.strikes), Rate(incoming.blocks, incoming.strikes),
        Rate(incoming.armorStops, incoming.strikes), Percentile(dealt, 50), Percentile(dealt, 90), Percentile(blocked, 50), Percentile(blocked, 90),
        Percentile(armored, 50), Percentile(armored, 90));
    WriteSetup(output, options, art);
    output.Fields(escapes, Rate(escapes, runs.size()), approachSteps, retreatSteps, fleeSteps, rests);
    if (wins) {
        output.Fields(Percentile(winDamage, 50), Percentile(winDamage, 90), Percentile(winMaxLoss, 50), Percentile(winMaxLoss, 90));
    } else { output.Fields("", "", "", ""); }
    output.EndRow();
}
}

int main(int argc, char** argv) {
    Options options;
    bool namedLoadout = false, explicitGrip = false;
    for (int i = 1; i < argc; ++i) {
        const eastl::string_view argument(argv[i]);
        if (argument == "--enemy-first") { options.enemyFirst = true; continue; }
        if (argument == "--list-catalog") { options.listCatalog = true; continue; }
        if (argument == "--help") {
            std::cout << "cataclysmx_combat_experiment [--seeds N] [--seed N] [--scenario duel|crowd|passive|attrition]\n"
                "  [--enemy ID] [--loadout all|unarmed|swordshield|twohand|dual|hammer|knife|sword|axe]\n"
                "  [--outfit none|standard|work] [--skill 0..10] [--style ID] [--art-xp N]\n"
                "  [--weapon ITEM --grip one_handed|two_handed --offhand ITEM] [--wear ITEM ...]\n"
                "  [--list-catalog] lists loaded enemies, authored melee grips, armor and styles as CSV.\n"
                "  [--policy attack|cautious] [--enemy-ai melee|recipe] [--max-ticks N]\n"
                "  [--player-delay BASE_COST] [--enemy-delay BASE_COST] [--enemy-first]\n"
                "  [--csv PATH] [--csv-format excel|plain]\n"
                "  all runs the original four loadouts; passive always waits.\n"
                "  --weapon replaces --loadout; --wear adds garments after the outfit (use --outfit none for individual armor).\n"
                "  Art XP uses raw catalog units and requires an owning art when nonzero.\n"
                "  Cautious: recover at <=25 stamina until >=45; flee at <=40% head or torso HP.\n"
                "  Escape requires reaching an edge opening and surviving until ready again.\n"
                "  Default melee AI wanders outside attack range; recipe retains the monster's normal controller.\n"
                "  CSV files default to Excel semicolon format; stdout is always comma CSV.\n";
            return 0;
        }
        if (i + 1 == argc) { std::cerr << "Missing option value: " << argv[i] << '\n'; return 2; }
        const eastl::string_view value(argv[++i]);
        bool valid = true;
        if (argument == "--seeds") { valid = Number(value, options.seeds) && options.seeds != 0; }
        else if (argument == "--seed") { valid = Number(value, options.seed); }
        else if (argument == "--scenario") { options.scenario = value; valid = value == "duel" || value == "crowd" || value == "passive" || value == "attrition"; }
        else if (argument == "--loadout") {
            namedLoadout = true;
            options.loadout = value;
            valid = value == "all";
            for (const auto& loadout : loadouts) { valid |= value == loadout.name; }
        }
        else if (argument == "--weapon") { options.weapon = argv[i]; valid = !value.empty(); }
        else if (argument == "--offhand") { options.offhand = argv[i]; valid = !value.empty(); }
        else if (argument == "--grip") {
            explicitGrip = true; options.grip = value; valid = value == "one_handed" || value == "two_handed";
        }
        else if (argument == "--wear") { options.wear.push_back(argv[i]); valid = !value.empty(); }
        else if (argument == "--enemy") { options.enemy = value; valid = !value.empty(); }
        else if (argument == "--outfit") { options.outfit = value; valid = value == "none" || value == "standard" || value == "work"; }
        else if (argument == "--style") { options.style = value; valid = !value.empty(); }
        else if (argument == "--art-xp") { valid = Number(value, options.artXp); }
        else if (argument == "--policy") { options.policy = value; valid = value == "attack" || value == "cautious"; }
        else if (argument == "--enemy-ai") { options.enemyAi = value; valid = value == "melee" || value == "recipe"; }
        else if (argument == "--skill") { valid = Number(value, options.skill) && options.skill >= 0 && options.skill <= maximumSkillLevel; }
        else if (argument == "--max-ticks") { valid = Number(value, options.limit) && options.limit != 0; }
        else if (argument == "--player-delay") { valid = Number(value, options.playerDelay); }
        else if (argument == "--enemy-delay") { valid = Number(value, options.enemyDelay); }
        else if (argument == "--csv") { options.csv = argv[i]; valid = !value.empty(); }
        else if (argument == "--csv-format") { options.plainCsv = value == "plain"; valid = options.plainCsv || value == "excel"; }
        else { valid = false; }
        if (!valid) { std::cerr << "Invalid option/value: " << argv[i - 1] << ' ' << argv[i] << '\n'; return 2; }
    }
    if (options.seeds - 1 > UINT32_MAX - options.seed) { std::cerr << "Seed range overflows uint32\n"; return 2; }
    if ((options.weapon && namedLoadout) || (!options.weapon && (explicitGrip || options.offhand)) ||
        (options.offhand && options.grip == "two_handed")) {
        std::cerr << "Use either --loadout or --weapon; --grip/--offhand require --weapon and a free offhand\n"; return 2;
    }
    Diagnostics diagnostics;
    auto content = LoadContentPack(CATACLYSMX_CONTENT_PACK, &diagnostics);
    if (!content) { std::cerr << FormatDiagnostics(diagnostics).c_str() << '\n'; return 1; }
    if (options.listCatalog) { ListCatalog(*content); return 0; }
    for (const auto* name : options.wear) {
        const auto* item = content->TryGetItem(content->FindItem(name));
        if (!item || !item->armor) { std::cerr << "Unknown/non-armor --wear item: " << name << '\n'; return 2; }
        if (!options.wearIds.empty()) { options.wearIds += ';'; }
        options.wearIds += name;
    }
    Vector<Loadout> selectedLoadouts;
    if (options.weapon) {
        const auto grip = options.grip == "two_handed" ? GripKind::TwoHanded : GripKind::OneHanded;
        const auto validHeld = [&](const char* name, GripKind wanted) {
            const auto* item = content->TryGetItem(content->FindItem(name));
            if (!item) { return false; }
            for (const auto& supported : ItemMelee(*item).grips) { if (supported.kind == wanted) { return true; } }
            return false;
        };
        if (!validHeld(options.weapon, grip) || (options.offhand && !validHeld(options.offhand, GripKind::OneHanded))) {
            std::cerr << "Unknown held item or unsupported grip\n"; return 2;
        }
        selectedLoadouts.push_back({options.weapon, options.weapon, grip, options.offhand});
    } else {
        for (const auto& loadout : loadouts) {
            if (options.loadout == "all" ? loadout.inDefaultSweep : options.loadout == loadout.name) { selectedLoadouts.push_back(loadout); }
        }
    }
    options.enemyId = content->FindMonster(options.enemy);
    if (!options.enemyId.value) { std::cerr << "Unknown monster ID: " << options.enemy.data() << '\n'; return 2; }
    options.styleId = content->FindCombatStyle(options.style);
    if (!options.styleId.value) { std::cerr << "Unknown combat style ID: " << options.style.data() << '\n'; return 2; }
    const auto artId = content->GetCombatStyle(options.styleId).art;
    if (options.artXp && !artId.value) { std::cerr << "Nonzero --art-xp requires a style with an owning art\n"; return 2; }
    const eastl::string_view art = artId.value ? eastl::string_view(content->GetCombatArt(artId).id) : eastl::string_view("");
    std::ofstream csvFile, summaryFile;
    if (options.csv) {
        csvFile.open(options.csv, std::ios::binary);
        const String summaryPath = String(options.csv) + ".summary.csv";
        summaryFile.open(summaryPath.c_str(), std::ios::binary);
        Require(csvFile.is_open() && summaryFile.is_open(), "open CSV output");
    }
    const bool excel = options.csv && !options.plainCsv;
    CsvWriter stdoutCsv(std::cout, false), csv(csvFile, excel), summary(summaryFile, excel);
    if (options.csv) {
        csv.Columns("scenario,loadout,skill,seed,style,enemy_first,player_delay,enemy_delay,outcome,ticks,kills,final_stamina,waits");
        SideHeader(csv, "out"); SideHeader(csv, "in");
        csv.Columns(setupColumns);
        csv.Columns("first_seed,seeds,max_ticks,approach_steps,retreat_steps,flee_steps,rests,win_hp_lost_100,win_max_hp_lost_100");
        csv.EndRow();
    }
    const auto summaryHeader = [](CsvWriter& output) {
        output.Columns("scenario,style,first_seed,max_ticks,enemy_first,player_delay,enemy_delay,loadout,skill,runs,wins,losses,timeouts,win_rate,loss_rate,timeout_rate,"
            "ticks_median,ticks_p90,hp_lost_100_median,hp_lost_100_p90,stamina_spent_median,stamina_spent_p90,out_miss_rate,out_dodge_rate,out_parry_rate,out_block_rate,"
            "out_armor_stop_rate,invalidation_per_commitment,wasted_windup_rate,exhausted_action_rate,in_dodge_rate,in_parry_rate,in_block_rate,in_armor_stop_rate,"
            "hp_dealt_100_median,hp_dealt_100_p90,block_absorbed_100_median,block_absorbed_100_p90,armor_absorbed_100_median,armor_absorbed_100_p90");
        output.Columns(setupColumns);
        output.Columns("escapes,escape_rate,approach_steps_total,retreat_steps_total,flee_steps_total,rests_total,"
            "win_hp_lost_100_median,win_hp_lost_100_p90,win_max_hp_lost_100_median,win_max_hp_lost_100_p90");
        output.EndRow();
    };
    summaryHeader(stdoutCsv);
    if (options.csv) { summaryHeader(summary); }
    for (const auto& loadout : selectedLoadouts) {
        const unsigned levels[] = {0, 3, 6, 10};
        const unsigned levelCount = options.skill >= 0 ? 1 : 4;
        for (unsigned levelIndex = 0; levelIndex < levelCount; ++levelIndex) {
            const unsigned level = options.skill >= 0 ? static_cast<unsigned>(options.skill) : levels[levelIndex];
            Vector<Run> runs;
            Vector<MoveFrequency> frequencies;
            for (std::uint32_t index = 0; index < options.seeds; ++index) {
                auto run = Experiment(*content, options, loadout, level, options.seed + index, frequencies);
                if (options.csv) {
                    csv.Fields(options.scenario, loadout.name, level, run.seed, options.style, options.enemyFirst, options.playerDelay,
                        options.enemyDelay, run.outcome, run.ticks, run.kills, run.finalStamina, run.waits);
                    WriteSide(csv, run.outgoing); WriteSide(csv, run.incoming);
                    WriteSetup(csv, options, art);
                    csv.Fields(options.seed, options.seeds, options.limit, run.approachSteps, run.retreatSteps, run.fleeSteps, run.rests);
                    if (run.outcome == "win") { csv.Fields(run.incoming.hpLoss, run.incoming.maxHpLoss); }
                    else { csv.Fields("", ""); }
                    csv.EndRow();
                }
                runs.push_back(run);
            }
            Summary(stdoutCsv, runs, options, loadout.name, level, art);
            if (options.csv) { Summary(summary, runs, options, loadout.name, level, art); }
            eastl::sort(frequencies.begin(), frequencies.end(), [&](const auto& a, const auto& b) {
                if (!a.move.value || !b.move.value) { return a.move.value < b.move.value; }
                return content->GetCombatMove(a.move).id < content->GetCombatMove(b.move).id;
            });
            for (const auto& frequency : frequencies) {
                if (!frequency.move.value) { continue; }
                std::cerr << "move loadout=" << loadout.name.data() << " skill=" << level << " id=" << content->GetCombatMove(frequency.move).id.c_str()
                    << " player_committed=" << frequency.playerCommitted << " enemy_committed=" << frequency.enemyCommitted
                    << " player_invalidated=" << frequency.playerInvalidated << " enemy_invalidated=" << frequency.enemyInvalidated << '\n';
            }
        }
    }
    if (options.csv) { csvFile.close(); summaryFile.close(); Require(csvFile.good() && summaryFile.good(), "write CSV output"); }
    Require(std::cout.good(), "write summary output");
    return 0;
}
