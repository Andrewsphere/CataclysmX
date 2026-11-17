#include "core/world.h"
#include "core/simulation.h"
#include "combat/combat_system.h"
#include "skills/learning_system.h"

#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <charconv>
#include <cstdlib>
#include <fstream>
#include <iostream>

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
    const char* csv = nullptr;
    bool enemyFirst = false;
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
    const char* loadout = nullptr;
    unsigned skill = 0;
    const char* outcome = "timeout";
    Tick ticks = 0;
    unsigned kills = 0;
    unsigned finalStamina = 0;
    std::uint64_t waits = 0;
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
            Require(world.SetTile({x, z}, {edge ? wall : floor, 0}).has_value(), "level arena floor");
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
    const auto type = world.GetContent().FindMonster("cellar_zombie");
    Require(type.value != 0, "cellar_zombie definition");
    const auto actor = Take(world.CreateMonster(type, position), "create zombie");
    Require(world.SetActorController(actor, AiControllerKind::Melee).has_value(), "set melee AI");
    if (options.enemyDelay) { Require(world.ScheduleActorAction(actor, options.enemyDelay).has_value(), "enemy starting delay"); }
    return actor;
}

Run Experiment(const ContentCatalog& content, const Options& options, const char* loadout, unsigned skill,
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
    if (eastl::string_view(loadout) != "unarmed") {
        Hold(world, player, "arming_sword", Hand::Left, eastl::string_view(loadout) == "twohand" ? GripKind::TwoHanded : GripKind::OneHanded);
        if (eastl::string_view(loadout) == "swordshield") { Hold(world, player, "wooden_shield", Hand::Right, GripKind::OneHanded); }
        if (eastl::string_view(loadout) == "dual") { Hold(world, player, "arming_sword", Hand::Right, GripKind::OneHanded); }
    }
    // Same clothing in every comparison; coverage and encumbrance are production rules.
    for (const char* name : {"leather_work_gloves", "work_boots"}) {
        const auto item = Take(world.CreateItem(content.FindItem(name), ItemLocation::Carried(player)), "create clothing");
        Require(world.WearItem(player, item).outcome == InventoryOutcome::Succeeded, "wear clothing");
    }
    const auto style = options.style == "basic_aggressive" ? CombatStyle::Aggressive
        : options.style == "basic_defensive" ? CombatStyle::Defensive : CombatStyle::Balanced;
    Require(world.Combat().SetStyle(player, style).has_value(), "select experiment style");
    if (options.playerDelay) { Require(world.ScheduleActorAction(player, options.playerDelay).has_value(), "starting player delay"); }
    Vector<CombatEvent> events;
    world.Combat().DrainEvents(events); // Setup is excluded from measured costs.
    Run run; run.seed = seed; run.loadout = loadout; run.skill = skill;
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
        if (options.scenario == "attrition" && run.kills == spawned && spawned < totalEnemies) {
            const auto position = world.GetActor(player)->position;
            eastl::optional<TileCoord> next;
            for (const auto direction : actorStepDirections) {
                const TileCoord candidate{position.x + direction.x, position.z + direction.z, position.y};
                if (!world.BlocksMovement(candidate) && world.ActorAt(candidate) == ActorHandle{}) { next = candidate; break; }
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
        if (options.scenario != "passive") {
            // Production targeting performs perception/hostility/reach checks.
            // No harness RNG draw, attack formula, or manual NPC turn ordering.
            const auto target = world.Combat().FindTarget(player);
            if (target != ActorHandle{}) { committed = world.Combat().Attack(player, target, rng).committed; }
        }
        if (!committed) {
            ++run.waits;
            Require(world.ScheduleActorAction(player, ticks_per_turn).has_value(), "wait for next decision");
        }
        Collect(world, playerId, run, events, frequencies, deathTick);
        horizon = world.GetTick();
    }
    world.FinalizeActorDeaths();
    Collect(world, playerId, run, events, frequencies, deathTick);
    run.kills = 0;
    for (const auto enemy : enemies) { run.kills += !world.IsActorAlive(enemy); }
    run.ticks = eastl::string_view(run.outcome) == "timeout" ? world.GetTick() : deathTick.value_or(world.GetTick());
    if (const auto* survivor = world.GetActor(player)) { run.finalStamina = survivor->combat.stamina; }
    Require(world.CheckInvariants(), "final world invariants");
    return run;
}

void SideHeader(std::ostream& output, const char* prefix) {
    for (const char* field : {"strikes", "invalid_strikes", "misses", "dodges", "parries", "contacts", "blocks", "armor_stops", "counters",
        "hp_loss_100", "max_hp_loss_100", "block_absorbed_100", "armor_absorbed_100", "commitments", "invalidations", "stamina_spent",
        "windups", "wasted_windups", "exhausted_actions"}) {
        output << ',' << prefix << '_' << field;
    }
}
void WriteSide(std::ostream& output, const SideMetrics& side) {
    output << ',' << side.strikes << ',' << side.invalidStrikes << ',' << side.misses << ',' << side.dodges << ',' << side.parries
        << ',' << side.contacts << ',' << side.blocks << ',' << side.armorStops << ',' << side.counters << ',' << side.hpLoss
        << ',' << side.maxHpLoss << ',' << side.blockAbsorbed << ',' << side.armorAbsorbed << ',' << side.commitments
        << ',' << side.invalidations << ',' << side.staminaSpent << ',' << side.windups << ',' << side.wastedWindups << ',' << side.exhaustedActions;
}
double Rate(std::uint64_t numerator, std::uint64_t denominator) {
    return denominator ? static_cast<double>(numerator) / static_cast<double>(denominator) : 0.0;
}
std::uint64_t Percentile(Vector<std::uint64_t>& scratch, unsigned percent) {
    eastl::sort(scratch.begin(), scratch.end());
    return scratch[(scratch.size() * percent + 99) / 100 - 1];
}
void Summary(std::ostream& output, const Vector<Run>& runs, const Options& options, const char* loadout, unsigned skill) {
    std::uint64_t wins = 0, losses = 0, timeouts = 0, strikes = 0, misses = 0, dodges = 0, parries = 0, blocks = 0, armorStops = 0;
    std::uint64_t commitments = 0, invalidations = 0, windups = 0, wastedWindups = 0, exhaustedActions = 0;
    SideMetrics incoming;
    Vector<std::uint64_t> ticks, damage, stamina, dealt, blocked, armored;
    for (const auto& run : runs) {
        wins += eastl::string_view(run.outcome) == "win";
        losses += eastl::string_view(run.outcome) == "loss";
        timeouts += eastl::string_view(run.outcome) == "timeout";
        ticks.push_back(run.ticks); damage.push_back(run.incoming.hpLoss); stamina.push_back(run.outgoing.staminaSpent);
        dealt.push_back(run.outgoing.hpLoss); blocked.push_back(run.incoming.blockAbsorbed); armored.push_back(run.incoming.armorAbsorbed);
        incoming.strikes += run.incoming.strikes; incoming.dodges += run.incoming.dodges; incoming.parries += run.incoming.parries;
        incoming.blocks += run.incoming.blocks; incoming.armorStops += run.incoming.armorStops;
        strikes += run.outgoing.strikes; misses += run.outgoing.misses; dodges += run.outgoing.dodges; parries += run.outgoing.parries;
        blocks += run.outgoing.blocks; armorStops += run.outgoing.armorStops;
        commitments += run.outgoing.commitments; invalidations += run.outgoing.invalidations;
        windups += run.outgoing.windups; wastedWindups += run.outgoing.wastedWindups; exhaustedActions += run.outgoing.exhaustedActions;
    }
    output << options.scenario.data() << ',' << options.style.data() << ',' << options.seed << ',' << options.limit
        << ',' << options.enemyFirst << ',' << options.playerDelay << ',' << options.enemyDelay
        << ',' << loadout << ',' << skill << ',' << runs.size() << ',' << wins << ',' << losses << ',' << timeouts
        << ',' << Rate(wins, runs.size()) << ',' << Rate(losses, runs.size()) << ',' << Rate(timeouts, runs.size())
        << ',' << Percentile(ticks, 50) << ',' << Percentile(ticks, 90) << ',' << Percentile(damage, 50) << ',' << Percentile(damage, 90)
        << ',' << Percentile(stamina, 50) << ',' << Percentile(stamina, 90) << ',' << Rate(misses, strikes) << ',' << Rate(dodges, strikes)
        << ',' << Rate(parries, strikes) << ',' << Rate(blocks, strikes) << ',' << Rate(armorStops, strikes) << ',' << Rate(invalidations, commitments)
        << ',' << Rate(wastedWindups, windups) << ',' << Rate(exhaustedActions, commitments)
        << ',' << Rate(incoming.dodges, incoming.strikes) << ',' << Rate(incoming.parries, incoming.strikes)
        << ',' << Rate(incoming.blocks, incoming.strikes) << ',' << Rate(incoming.armorStops, incoming.strikes)
        << ',' << Percentile(dealt, 50) << ',' << Percentile(dealt, 90) << ',' << Percentile(blocked, 50) << ',' << Percentile(blocked, 90)
        << ',' << Percentile(armored, 50) << ',' << Percentile(armored, 90) << '\n';
}
}

int main(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const eastl::string_view argument(argv[i]);
        if (argument == "--enemy-first") { options.enemyFirst = true; continue; }
        if (argument == "--help") {
            std::cout << "cataclysmx_combat_experiment [--seeds N] [--seed N] [--scenario duel|crowd|passive|attrition]\n"
                "  [--loadout all|unarmed|swordshield|twohand|dual] [--skill 0..10]\n"
                "  [--style basic_balanced|basic_aggressive|basic_defensive] [--max-ticks N]\n"
                "  [--player-delay BASE_COST] [--enemy-delay BASE_COST] [--enemy-first] [--csv PATH]\n";
            return 0;
        }
        if (i + 1 == argc) { std::cerr << "Missing option value: " << argv[i] << '\n'; return 2; }
        const eastl::string_view value(argv[++i]);
        bool valid = true;
        if (argument == "--seeds") { valid = Number(value, options.seeds) && options.seeds != 0; }
        else if (argument == "--seed") { valid = Number(value, options.seed); }
        else if (argument == "--scenario") { options.scenario = value; valid = value == "duel" || value == "crowd" || value == "passive" || value == "attrition"; }
        else if (argument == "--loadout") { options.loadout = value; valid = value == "all" || value == "unarmed" || value == "swordshield" || value == "twohand" || value == "dual"; }
        else if (argument == "--style") { options.style = value; valid = value == "basic_balanced" || value == "basic_aggressive" || value == "basic_defensive"; }
        else if (argument == "--skill") { valid = Number(value, options.skill) && options.skill >= 0 && options.skill <= maximumSkillLevel; }
        else if (argument == "--max-ticks") { valid = Number(value, options.limit) && options.limit != 0; }
        else if (argument == "--player-delay") { valid = Number(value, options.playerDelay); }
        else if (argument == "--enemy-delay") { valid = Number(value, options.enemyDelay); }
        else if (argument == "--csv") { options.csv = argv[i]; }
        else { valid = false; }
        if (!valid) { std::cerr << "Invalid option/value: " << argv[i - 1] << ' ' << argv[i] << '\n'; return 2; }
    }
    if (options.seeds - 1 > UINT32_MAX - options.seed) { std::cerr << "Seed range overflows uint32\n"; return 2; }
    Diagnostics diagnostics;
    auto content = LoadContentPack(CATACLYSMX_CONTENT_PACK, &diagnostics);
    if (!content) { std::cerr << FormatDiagnostics(diagnostics).c_str() << '\n'; return 1; }
    std::ofstream csv, summary;
    if (options.csv) {
        csv.open(options.csv);
        const String summaryPath = String(options.csv) + ".summary.csv";
        summary.open(summaryPath.c_str());
        Require(csv.is_open() && summary.is_open(), "open CSV output");
        csv << "scenario,loadout,skill,seed,style,enemy_first,player_delay,enemy_delay,outcome,ticks,kills,final_stamina,waits";
        SideHeader(csv, "out"); SideHeader(csv, "in"); csv << '\n';
    }
    const char* summaryHeader = "scenario,style,first_seed,max_ticks,enemy_first,player_delay,enemy_delay,loadout,skill,runs,wins,losses,timeouts,win_rate,loss_rate,timeout_rate,ticks_median,ticks_p90,hp_lost_100_median,hp_lost_100_p90,stamina_spent_median,stamina_spent_p90,out_miss_rate,out_dodge_rate,out_parry_rate,out_block_rate,out_armor_stop_rate,invalidation_per_commitment,wasted_windup_rate,exhausted_action_rate,in_dodge_rate,in_parry_rate,in_block_rate,in_armor_stop_rate,hp_dealt_100_median,hp_dealt_100_p90,block_absorbed_100_median,block_absorbed_100_p90,armor_absorbed_100_median,armor_absorbed_100_p90\n";
    std::cout << summaryHeader;
    if (summary.is_open()) { summary << summaryHeader; }
    for (const char* loadout : {"unarmed", "swordshield", "twohand", "dual"}) {
        if (options.loadout != "all" && options.loadout != loadout) { continue; }
        const unsigned levels[] = {0, 3, 6, 10};
        const unsigned levelCount = options.skill >= 0 ? 1 : 4;
        for (unsigned levelIndex = 0; levelIndex < levelCount; ++levelIndex) {
            const unsigned level = options.skill >= 0 ? static_cast<unsigned>(options.skill) : levels[levelIndex];
            Vector<Run> runs;
            Vector<MoveFrequency> frequencies;
            for (std::uint32_t index = 0; index < options.seeds; ++index) {
                auto run = Experiment(*content, options, loadout, level, options.seed + index, frequencies);
                if (csv.is_open()) {
                    csv << options.scenario.data() << ',' << loadout << ',' << level << ',' << run.seed << ',' << options.style.data()
                        << ',' << options.enemyFirst << ',' << options.playerDelay << ',' << options.enemyDelay << ',' << run.outcome
                        << ',' << run.ticks << ',' << run.kills << ',' << run.finalStamina << ',' << run.waits;
                    WriteSide(csv, run.outgoing); WriteSide(csv, run.incoming); csv << '\n';
                }
                runs.push_back(run);
            }
            Summary(std::cout, runs, options, loadout, level);
            if (summary.is_open()) { Summary(summary, runs, options, loadout, level); }
            eastl::sort(frequencies.begin(), frequencies.end(), [&](const auto& a, const auto& b) {
                if (!a.move.value || !b.move.value) { return a.move.value < b.move.value; }
                return content->GetCombatMove(a.move).id < content->GetCombatMove(b.move).id;
            });
            for (const auto& frequency : frequencies) {
                if (!frequency.move.value) { continue; }
                std::cerr << "move loadout=" << loadout << " skill=" << level << " id=" << content->GetCombatMove(frequency.move).id.c_str()
                    << " player_committed=" << frequency.playerCommitted << " enemy_committed=" << frequency.enemyCommitted
                    << " player_invalidated=" << frequency.playerInvalidated << " enemy_invalidated=" << frequency.enemyInvalidated << '\n';
            }
        }
    }
    if (csv.is_open()) { csv.flush(); summary.flush(); Require(csv.good() && summary.good(), "write CSV output"); }
    Require(std::cout.good(), "write summary output");
    return 0;
}
