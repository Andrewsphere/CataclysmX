#include "core/first_house_scenario.h"
#include "core/server.h"
#include "power/power_system.h"
#include "worldgen/worldgen.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <limits>

namespace {
using namespace cataclysmx;
using Clock = std::chrono::steady_clock;

double Milliseconds(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

struct Entry {
    std::uint64_t seed;
    SettlementDescriptor descriptor;
    bool panel = false;
    bool example = false;
};

bool Overlap(TileRect a, TileRect b) {
    return a.min.x < b.max.x && b.min.x < a.max.x
        && a.min.z < b.max.z && b.min.z < a.max.z;
}

void Add(Vector<Entry>& entries, std::uint64_t seed, const SettlementDescriptor& descriptor,
    bool panel, bool example) {
    for (auto& entry : entries) {
        if (entry.seed == seed && entry.descriptor.id == descriptor.id) {
            entry.panel |= panel;
            entry.example |= example;
            return;
        }
    }
    entries.push_back({seed, descriptor, panel, example});
}

const char* Scale(SettlementScale scale) {
    switch (scale) {
    case SettlementScale::Small: return "small";
    case SettlementScale::Medium: return "medium";
    case SettlementScale::Large: return "large";
    }
    return "unknown";
}

const char* Era(SettlementEra era) {
    switch (era) {
    case SettlementEra::Older: return "older";
    case SettlementEra::Mixed: return "mixed";
    case SettlementEra::Newer: return "newer";
    }
    return "unknown";
}

int ScaleIndex(SettlementScale scale) {
    switch (scale) {
    case SettlementScale::Small: return 0;
    case SettlementScale::Medium: return 1;
    case SettlementScale::Large: return 2;
    }
    return -1;
}

int EraIndex(SettlementEra era) {
    switch (era) {
    case SettlementEra::Older: return 0;
    case SettlementEra::Mixed: return 1;
    case SettlementEra::Newer: return 2;
    }
    return -1;
}

int Panel(const ContentCatalog& content) {
    Vector<Entry> entries;
    bool examples[3][3]{};
    std::uint32_t descriptorCounts[3][3]{};
    Vector<std::uint64_t> patternCounts(content.BuildingPatternCount() + 1, 0);
    const RegionCoord regions[] = {{0, 0}, {-1, 0}, {0, -1}, {1, 1}};
    for (std::uint64_t index = 0; index < 33; ++index) {
        const std::uint64_t seed = index == 32 ? 42 : index;
        // Descriptors belong to their patch; neighboring patches are checked
        // because a developed envelope may cross a patch boundary.
        for (const auto region : regions) {
            const TileRect bounds{{region.x * region_size, region.z * region_size},
                {(region.x + 1) * region_size, (region.z + 1) * region_size}};
            for (int dz = -1; dz <= 1; ++dz) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const auto patch = GeneratePlanningPatch(seed,
                        {region.x + dx * planning_patch_regions, region.z + dz * planning_patch_regions});
                    for (const auto& descriptor : patch.settlements) {
                        if (Overlap(bounds, descriptor.bounds)) {
                            Add(entries, seed, descriptor, true, false);
                        }
                    }
                }
            }
        }
        const auto origin = GeneratePlanningPatch(seed, {0, 0});
        for (const auto& descriptor : origin.settlements) {
            const int scale = ScaleIndex(descriptor.scale);
            const int era = EraIndex(descriptor.era);
            if (scale < 0 || era < 0) {
                std::fprintf(stderr, "unknown settlement scale or era\n");
                return 1;
            }
            ++descriptorCounts[scale][era];
            if (!examples[scale][era]) {
                examples[scale][era] = true;
                Add(entries, seed, descriptor, false, true);
            }
        }
    }
    for (int scale = 0; scale < 3; ++scale) {
        for (int era = 0; era < 3; ++era) {
            if (!examples[scale][era]) {
                std::fprintf(stderr, "missing origin-patch example: scale=%d era=%d\n", scale, era);
                return 1;
            }
        }
    }
    std::printf("seed,owner_region_x,owner_region_z,ordinal,scale,era,min_x,min_z,max_x,max_z,sites,occupied_floors,newer_sites,older_sites,indoor_expected_ratio_scale,actual_indoor,actual_outdoor,eligible_outdoor_tiles,planning_ms,panel,example,residential_sites,residential_floors,nearest_envelope_gap\n");
    std::uint64_t sites = 0, indoor = 0, outdoor = 0;
    for (const auto& entry : entries) {
        const auto start = Clock::now();
        const auto plan = GenerateSettlementPlan(content, entry.seed, entry.descriptor);
        const double elapsed = Milliseconds(start);
        std::uint64_t floors = 0, newer = 0, older = 0, actualIndoor = 0, actualOutdoor = 0;
        std::uint64_t residentialSites = 0, residentialFloors = 0;
        for (const auto& building : plan.buildings) {
            const auto& pattern = content.GetBuildingPattern(building.pattern);
            ++patternCounts[building.pattern.value];
            if (pattern.site == BuildingSite::Building) { floors += pattern.occupiedFloors; }
            if (pattern.use == BuildingUse::Residential) {
                ++residentialSites;
                residentialFloors += pattern.occupiedFloors;
            }
            if (pattern.era == BuildingEra::Newer) { ++newer; } else { ++older; }
        }
        for (const auto& monster : plan.monsters) {
            if (monster.key.kind == GenerationPlacementKind::IndoorPopulation) { ++actualIndoor; }
            if (monster.key.kind == GenerationPlacementKind::OutdoorPopulation) { ++actualOutdoor; }
        }
        sites += plan.buildings.size();
        indoor += actualIndoor;
        outdoor += actualOutdoor;
        const auto& d = entry.descriptor;
        double nearest = 1e20;
        for (int dz = -1; dz <= 1; ++dz) {
            for (int dx = -1; dx <= 1; ++dx) {
                const auto neighbors = GeneratePlanningPatch(entry.seed,
                    {d.id.region.x + dx * planning_patch_regions, d.id.region.z + dz * planning_patch_regions});
                for (const auto& neighbor : neighbors.settlements) {
                    if (neighbor.id == d.id) { continue; }
                    const int gapX = d.bounds.min.x > neighbor.bounds.max.x ? d.bounds.min.x - neighbor.bounds.max.x
                        : neighbor.bounds.min.x > d.bounds.max.x ? neighbor.bounds.min.x - d.bounds.max.x : 0;
                    const int gapZ = d.bounds.min.z > neighbor.bounds.max.z ? d.bounds.min.z - neighbor.bounds.max.z
                        : neighbor.bounds.min.z > d.bounds.max.z ? neighbor.bounds.min.z - d.bounds.max.z : 0;
                    const double gap = std::sqrt(static_cast<double>(gapX) * gapX
                        + static_cast<double>(gapZ) * gapZ);
                    if (gap < nearest) { nearest = gap; }
                }
            }
        }
        std::printf("%llu,%d,%d,%u,%s,%s,%d,%d,%d,%d,%zu,%llu,%llu,%llu,%llu,%llu,%llu,%u,%.3f,%d,%d,%llu,%llu,%.3f\n",
            static_cast<unsigned long long>(entry.seed), d.id.region.x, d.id.region.z, d.id.ordinal,
            Scale(d.scale), Era(d.era), d.bounds.min.x, d.bounds.min.z, d.bounds.max.x, d.bounds.max.z,
            plan.buildings.size(), static_cast<unsigned long long>(floors),
            static_cast<unsigned long long>(newer), static_cast<unsigned long long>(older),
            static_cast<unsigned long long>(plan.indoorExpected), static_cast<unsigned long long>(actualIndoor),
            static_cast<unsigned long long>(actualOutdoor), plan.eligibleOutdoorTiles, elapsed,
            entry.panel ? 1 : 0, entry.example ? 1 : 0,
            static_cast<unsigned long long>(residentialSites),
            static_cast<unsigned long long>(residentialFloors), nearest);
    }
    std::fprintf(stderr, "settlements=%zu sites=%llu indoor=%llu outdoor=%llu (panel and examples combined)\n",
        entries.size(), static_cast<unsigned long long>(sites), static_cast<unsigned long long>(indoor),
        static_cast<unsigned long long>(outdoor));
    for (int scale = 0; scale < 3; ++scale) {
        std::fprintf(stderr, "origin-patch descriptors scale=%d older=%u mixed=%u newer=%u\n",
            scale, descriptorCounts[scale][0], descriptorCounts[scale][1], descriptorCounts[scale][2]);
    }
    for (std::size_t value = 1; value < patternCounts.size(); ++value) {
        std::fprintf(stderr, "sites %s=%llu\n",
            content.GetBuildingPattern({static_cast<std::uint32_t>(value)}).id.c_str(),
            static_cast<unsigned long long>(patternCounts[value]));
    }
    return 0;
}

int Runtime(const ContentCatalog& content, std::uint32_t seed) {
    Diagnostics diagnostics;
    if (!FindTownStart(content, seed, &diagnostics)) {
        std::fprintf(stderr, "Town start: %s\n", FormatDiagnostics(diagnostics).c_str());
        return 1;
    }
    const auto patch = GeneratePlanningPatch(seed, {0, 0});
    const SettlementDescriptor* city = nullptr;
    for (const auto& descriptor : patch.settlements) {
        if (descriptor.scale == SettlementScale::Large) { city = &descriptor; break; }
    }
    if (!city) { std::fprintf(stderr, "origin patch has no large city\n"); return 1; }
    const auto setup = Clock::now();
    Server server(content, seed, NewGameScenario::Town);
    if (!server.SetBubbleRadii(1, 5)) {
        std::fprintf(stderr, "cannot set bubble radii\n"); return 1;
    }
    World& world = server.GetWorld();
    // Explicit workload fixture in the always-open demo clearing. Use real
    // catalog inputs, building mains and the ordinary machine action/payment.
    const auto workshopRequest = world.RequestChunks({{0, 0}, 0}, LoadMode::active);
    if (!workshopRequest || !world.ReconcileChunks()
        || !world.MoveActor(server.GetPlayer(), {1, 0})
        || !world.Power().RegisterBuilding({0, 0}, PowerBounds{{0, 0}, {3, 3}})) {
        std::fprintf(stderr, "cannot prepare workshop fixture\n"); return 1;
    }
    const auto press = world.CreateObject(content.FindObject("grid_scrap_press"), {0, 0});
    if (!press) { std::fprintf(stderr, "cannot create workshop press\n"); return 1; }
    const auto pressId = world.GetObject(*press)->id;
    for (int i = 0; i < 20; ++i) {
        if (!world.CreateItem(content.FindItem("steel_scrap"), ItemLocation::Carried(server.GetPlayer()))) {
            std::fprintf(stderr, "cannot supply workshop inputs\n"); return 1;
        }
    }
    const auto started = world.UseObject(server.GetPlayer(), *press, ObjectAction::Start);
    if (started.outcome != ObjectUseOutcome::Succeeded) {
        std::fprintf(stderr, "cannot start workshop: %s\n", ObjectReasonText(started.reason)); return 1;
    }
    const auto initialWork = world.DescribeObjectState(*press)->remainingWork;
    const auto center = ChunkOf(city->center);
    auto resident = world.RequestChunks({center, 5}, LoadMode::resident);
    auto active = world.RequestChunks({center, 1}, LoadMode::active);
    if (!resident || !active || !world.ReconcileChunks()) {
        std::fprintf(stderr, "cannot load city chunks\n"); return 1;
    }
    bool placed = false;
    TileCoord position{};
    for (int radius = 0; radius <= 24 && !placed; ++radius) {
        for (int z = -radius; z <= radius && !placed; ++z) {
            for (int x = -radius; x <= radius && !placed; ++x) {
                position = {city->center.x + x, city->center.z + z, 0};
                placed = world.MoveActor(server.GetPlayer(), position).has_value();
            }
        }
    }
    if (!placed) { std::fprintf(stderr, "cannot position player in city\n"); return 1; }
    // Administrative moves bypass the server's movement/bubble transaction.
    // Recenter its permanent requests before releasing the temporary demand;
    // a wait alone does not update those requests.
    if (!server.SetBubbleRadii(1, 5)) {
        std::fprintf(stderr, "cannot recenter city bubble\n"); return 1;
    }
    const auto warm = server.Send({CommandKind::wait});
    if (warm.storageError || !warm.advanced) {
        std::fprintf(stderr, "cannot synchronize city bubble\n"); return 1;
    }
    if (!world.ReleaseRequest(*resident) || !world.ReleaseRequest(*active)
        || !world.ReleaseRequest(*workshopRequest) || !world.ReconcileChunks()) {
        std::fprintf(stderr, "cannot release setup requests\n"); return 1;
    }
    const auto joined = server.JoinPlayer("Probe", 0, 0xC17A);
    if (!joined || !server.ActivatePlayer(joined->player) || !server.SetPacing({100, false, true})) {
        std::fprintf(stderr, "cannot activate second player or set pacing\n"); return 1;
    }
    const PersistentId players[] = {server.GetHostPlayerId(), joined->player};
    Vector<ChunkResidencyDebug> residency;
    ChunkResidencyCounters residencyCounters;
    world.CollectChunkResidencyDebug(residency, residencyCounters);
    const auto machineOnlyColumns = residencyCounters.machineOnlyColumns;
    if (machineOnlyColumns == 0) {
        std::fprintf(stderr, "offscreen press did not retain its workshop column\n"); return 1;
    }
    const double setupMs = Milliseconds(setup);
    std::size_t residentActors = 0, activeActors = 0;
    for (const auto coord : world.LoadedChunks()) {
        const auto* chunk = world.GetChunk(coord);
        if (!chunk) { continue; }
        residentActors += chunk->actors.size();
        if (chunk->active) { activeActors += chunk->actors.size(); }
    }
    const auto initialTick = server.GetTick();
    int completed = 0;
    bool dead[2]{};
    double waitMs = 0, viewMs = 0;
    std::size_t cells = 0;
    for (int i = 0; i < 20; ++i) {
        auto start = Clock::now();
        for (const auto player : players) {
            const auto input = server.GetInputContext(player);
            const auto result = server.Submit(player, {input.lastInputSequence + 1, input.opportunity,
                {CommandKind::wait}});
            if (result.outcome != Outcome::queued || result.storageError) {
                std::fprintf(stderr, "player wait submission failed: %s\n", OutcomeMessage(result.outcome));
                return 1;
            }
        }
        const auto update = server.Update(0);
        waitMs += Milliseconds(start);
        if (update.storageError) { std::fprintf(stderr, "round storage failure\n"); return 1; }
        for (const auto& status : server.GetPlayers()) {
            for (int p = 0; p < 2; ++p) {
                if (status.id == players[p] && status.life == ActorLifeState::Dead) { dead[p] = true; }
            }
        }
        if (dead[0] || dead[1]) { break; }
        if (update.commandResults.size() != 2) {
            std::fprintf(stderr, "round did not resolve both waits\n"); return 1;
        }
        for (const auto& result : update.commandResults) {
            if (result.result.outcome != Outcome::waited || !result.result.advanced) {
                std::fprintf(stderr, "round did not advance a wait: %s\n", OutcomeMessage(result.result.outcome));
                return 1;
            }
        }
        ++completed;
        start = Clock::now();
        for (const auto player : players) {
            const auto view = server.ViewForPlayer(player, 12);
            cells += view.cells.size();
        }
        viewMs += Milliseconds(start);
    }
    // Reload only for the final observation, outside the measured rounds.
    const auto inspectRequest = world.RequestChunks({{0, 0}, 0}, LoadMode::resident);
    if (!inspectRequest || !world.ReconcileChunks()) {
        std::fprintf(stderr, "cannot inspect completed workshop\n"); return 1;
    }
    const auto finalPress = world.FindObject(pressId);
    const auto finalState = world.DescribeObjectState(finalPress);
    const auto* pressObject = world.GetObject(finalPress);
    if (!finalState || !pressObject) {
        std::fprintf(stderr, "workshop press disappeared\n"); return 1;
    }
    const auto outputCount = pressObject->contents.size();
    std::printf("seed,owner_region_x,owner_region_z,ordinal,player_x,player_z,setup_ms,active_actors,resident_actors,rounds,ticks_advanced,round_ms,two_views_ms,view_cells,host_dead,guest_dead,machine_only_columns,initial_work,remaining_work,press_outputs\n");
    std::printf("%u,%d,%d,%u,%d,%d,%.3f,%zu,%zu,%d,%llu,%.3f,%.3f,%zu,%d,%d,%u,%llu,%llu,%zu\n",
        seed, city->id.region.x, city->id.region.z, city->id.ordinal, position.x, position.z,
        setupMs, activeActors, residentActors, completed,
        static_cast<unsigned long long>(server.GetTick() - initialTick), waitMs, viewMs, cells,
        dead[0] ? 1 : 0, dead[1] ? 1 : 0, machineOnlyColumns,
        static_cast<unsigned long long>(initialWork),
        static_cast<unsigned long long>(finalState->remainingWork), outputCount);
    std::fprintf(stderr, "runtime: two players; counts include players; setup includes warm wait; offscreen press uses a clearing fixture and 20 consumed steel scrap\n");
    return 0;
}
}

int main(int argc, char** argv) {
    const char* mode = argc > 1 ? argv[1] : "panel";
    if (argc > 3 || (std::strcmp(mode, "panel") && std::strcmp(mode, "runtime"))
        || (argc > 2 && std::strcmp(mode, "runtime"))) {
        std::fprintf(stderr, "usage: cataclysmx_city_probe [panel|runtime [seed]]\n"); return 2;
    }
    std::uint32_t seed = 42;
    if (argc > 2) {
        char* end = nullptr;
        if (!*argv[2] || *argv[2] == '-') { std::fprintf(stderr, "invalid seed\n"); return 2; }
        const auto value = std::strtoull(argv[2], &end, 10);
        if (*end || value > std::numeric_limits<std::uint32_t>::max()) {
            std::fprintf(stderr, "invalid seed\n"); return 2;
        }
        seed = static_cast<std::uint32_t>(value);
    }
    Diagnostics diagnostics;
    auto content = LoadContentPack(CATACLYSMX_CONTENT_PACK, &diagnostics);
    if (!content || !ValidateWorldgenContent(*content, &diagnostics)
        || !ValidateServerContent(*content, &diagnostics)) {
        std::fprintf(stderr, "content: %s\n", FormatDiagnostics(diagnostics).c_str()); return 1;
    }
    return std::strcmp(mode, "panel") == 0 ? Panel(*content) : Runtime(*content, seed);
}
