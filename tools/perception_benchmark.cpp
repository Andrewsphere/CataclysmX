#include "core/server.h"
#include "core/perception.h"
#include "core/lighting.h"
#include "terminal/gameplay_screen.h"

#include <EASTL/sort.h>
#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
using namespace cataclysmx;
using Clock = std::chrono::steady_clock;

void Require(bool condition) {
    if (!condition) { std::fprintf(stderr, "benchmark invariant failed\n"); std::abort(); }
}

struct Samples {
    const char* name;
    Vector<double> milliseconds;
    void Add(Clock::time_point start) {
        milliseconds.push_back(std::chrono::duration<double, std::milli>(Clock::now() - start).count());
    }
};

void PrintCounters(const PerceptionStats& p, const ViewStats& v, std::uint64_t maps) {
    std::printf(",%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu\n",
        static_cast<unsigned long long>(p.geometryRebuilds),
        static_cast<unsigned long long>(p.pointLosCalls),
        static_cast<unsigned long long>(p.traversedVoxels),
        static_cast<unsigned long long>(p.memoryTilesChanged),
        static_cast<unsigned long long>(p.observationScans),
        static_cast<unsigned long long>(p.lightingRebuilds),
        static_cast<unsigned long long>(p.demandReconciliations),
        static_cast<unsigned long long>(v.requests),
        static_cast<unsigned long long>(v.constructions),
        static_cast<unsigned long long>(v.mapConstructions),
        static_cast<unsigned long long>(maps),
        static_cast<unsigned long long>(p.opticalRebuilds),
        static_cast<unsigned long long>(p.volumeRebuilds),
        static_cast<unsigned long long>(p.filterRebuilds),
        static_cast<unsigned long long>(p.filterTilesSampled),
        static_cast<unsigned long long>(p.memoryTilesExamined));
    std::fflush(stdout);
}

template<class F>
void MeasureScenario(const char* name, int count, Server& server, GameplayUi& ui, F operation,
    PerceptionStats* isolatedStats = nullptr) {
    Samples samples{name};
    samples.milliseconds.reserve(static_cast<std::size_t>(count));
    server.GetWorld().ResetPerceptionStats();
    server.ResetViewStats();
    const auto mapStart = ui.MapPreparations();
    if (isolatedStats) { *isolatedStats = {}; }
    for (int i = 0; i < count; ++i) {
        const auto start = Clock::now();
        operation(i);
        samples.Add(start);
    }
    eastl::sort(samples.milliseconds.begin(), samples.milliseconds.end());
    const auto percentile = [&](std::size_t percent) {
        return samples.milliseconds[(samples.milliseconds.size() * percent + 99) / 100 - 1];
    };
    std::printf("%s,%zu,%.3f,%.3f,%.3f", name, samples.milliseconds.size(),
        percentile(50), percentile(95), percentile(99));
    auto stats = server.GetWorld().GetPerceptionStats();
    if (isolatedStats) {
        stats.geometryRebuilds += isolatedStats->geometryRebuilds;
        stats.pointLosCalls += isolatedStats->pointLosCalls;
        stats.traversedVoxels += isolatedStats->traversedVoxels;
        stats.opticalRebuilds += isolatedStats->opticalRebuilds;
        stats.volumeRebuilds += isolatedStats->volumeRebuilds;
    }
    PrintCounters(stats, server.GetViewStats(), ui.MapPreparations() - mapStart);
}

struct Frontend {
    Localization localization;
    Keymap keys = DefaultKeymap();
    GameplayUi ui;
    GameplayPresentation presentation;
    std::uint64_t sink = 0;
    Frontend() {
        Require(localization.Load(CATACLYSMX_LOC_DIRECTORY, "en").has_value());
        presentation.session = "Local";
        presentation.message = "Perception benchmark";
    }
    void Draw(const WorldView& view, const ContentCatalog& content) {
        auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(120), ftxui::Dimension::Fixed(40));
        ftxui::Render(screen, ui.Render(view, content, localization, keys, presentation));
        sink += screen.ToString().size();
    }
    void Frame(Server& server) {
        const auto view = server.View(12);
        Require(view.cells.size() == 625);
        sink += view.tick + view.cells.size();
        Draw(view, server.GetWorld().GetContent());
    }
};

void Move(Server& server, int dx) {
    const auto result = server.Send({CommandKind::move, dx, 0});
    Require(result.outcome == Outcome::moved && result.advanced && !result.storageError);
}

// Deterministic flat patch, with generated terrain retained outside it. Setup is untimed.
void Prepare(Server& server, const ContentCatalog& content, int resident, bool flat) {
    World& world = server.GetWorld();
    // Pre-materialize the east crossing's incoming columns so flat mode can
    // edit them before timing; releasing the request archives the outer edge.
    eastl::optional<LoadRequest> extra;
    if (flat) {
        auto request = world.RequestChunks({{1, 0, 0}, resident}, LoadMode::resident);
        Require(request.has_value());
        extra = *request;
        Require(world.ReconcileChunks().has_value());
    }
    for (const auto handle : world.ActiveActors()) {
        if (handle != server.GetPlayer()) { Require(world.DestroyActor(handle).has_value()); }
    }
    for (const auto handle : world.ActiveObjects()) { Require(world.DestroyObject(handle).has_value()); }
    const int minCoord = flat ? -resident * chunk_size : -12;
    const int maxZ = flat ? (resident + 1) * chunk_size - 1 : 12;
    const int maxX = flat ? (resident + 2) * chunk_size - 1 : 28;
    for (int z = minCoord; z <= maxZ; ++z) {
        for (int x = minCoord; x <= maxX; ++x) {
            for (int y = flat ? world_min_y : 0; y <= (flat ? world_max_y : 2); ++y) {
                Require(world.SetTile({x, z, y}, {content.FindTerrain(y == 0 ? "floor" : "air"), 0}).has_value());
            }
        }
    }
    if (extra) {
        Require(world.ReleaseRequest(*extra).has_value());
        Require(world.ReconcileChunks().has_value());
    }
    world.UpdatePlayerObservations();
}
}

int main(int argc, char** argv) {
    int count = 100, sight = 12, resident = 2;
    bool flat = true;
    char* end = nullptr;
    if (argc > 1) {
        const long parsed = std::strtol(argv[1], &end, 10);
        if (*argv[1] == '\0' || *end != '\0' || parsed < 2 || parsed > 10000) { end = nullptr; }
        else { count = static_cast<int>(parsed); }
    }
    if ((argc > 1 && !end) || argc > 5
        || (argc > 2 && std::strcmp(argv[2], "12") != 0 && std::strcmp(argv[2], "64") != 0)
        || (argc > 3 && std::strcmp(argv[3], "2") != 0 && std::strcmp(argv[3], "5") != 0)
        || (argc > 4 && std::strcmp(argv[4], "flat") != 0 && std::strcmp(argv[4], "mixed") != 0)) {
        std::fprintf(stderr, "usage: cataclysmx_perception_benchmark [samples:2..10000] [sight:12|64] [resident:2|5] [flat|mixed]\n"); return 2;
    }
    if (argc > 2) { sight = std::atoi(argv[2]); }
    if (argc > 3) { resident = std::atoi(argv[3]); }
    if (argc > 4) { flat = std::strcmp(argv[4], "flat") == 0; }
    auto content = cataclysmx::LoadContentFile(CATACLYSMX_TERRAIN_FILE);
    Require(content.has_value());
    Frontend frontend;
    std::printf("sight=%d resident=%d scene=%s viewport=25x25 screen=120x40 seed=42 samples=%d warmup=8 burst=8\n", sight, resident, flat ? "flat" : "mixed", count);
    std::printf("scenario,n,median_ms,p95_ms,p99_ms,geometry,los,voxels,memoryTilesChanged,observations,lighting,demand,viewRequests,fullViews,mapBuilds,mapPreparations,optics,volumes,filters,filterTiles,memoryTilesExamined\n");
    // Constructor uses production defaults; requested policy is applied afterward.
    Samples initialSamples{"first_server_load_default"};
    PerceptionStats initialStats{};
    ViewStats initialViews{};
    for (int i = 0; i < 8; ++i) {
        const auto start = Clock::now();
        Server initial(*content, 42);
        initialSamples.Add(start);
        const auto p = initial.GetWorld().GetPerceptionStats();
        initialStats.geometryRebuilds += p.geometryRebuilds; initialStats.pointLosCalls += p.pointLosCalls;
        initialStats.traversedVoxels += p.traversedVoxels; initialStats.memoryTilesChanged += p.memoryTilesChanged;
        initialStats.observationScans += p.observationScans; initialStats.lightingRebuilds += p.lightingRebuilds;
        initialStats.demandReconciliations += p.demandReconciliations;
        initialStats.opticalRebuilds += p.opticalRebuilds; initialStats.volumeRebuilds += p.volumeRebuilds;
        initialStats.filterRebuilds += p.filterRebuilds; initialStats.filterTilesSampled += p.filterTilesSampled;
        initialStats.memoryTilesExamined += p.memoryTilesExamined;
        const auto v = initial.GetViewStats();
        initialViews.requests += v.requests; initialViews.constructions += v.constructions; initialViews.mapConstructions += v.mapConstructions;
        frontend.sink += initial.GetTick();
    }
    eastl::sort(initialSamples.milliseconds.begin(), initialSamples.milliseconds.end());
    std::printf("first_server_load_default,8,%.3f,%.3f,%.3f", initialSamples.milliseconds[3], initialSamples.milliseconds[7], initialSamples.milliseconds[7]);
    PrintCounters(initialStats, initialViews, 0);
    // Separate genuinely ungenerated streaming from flat edited/archive reloads.
    {
        Server crossing(*content, 42);
        auto profile = crossing.GetWorld().GetVisionProfile();
        profile.range = sight;
        Require(crossing.GetWorld().SetVisionProfile(profile).has_value());
        Prepare(crossing, *content, 5, false);
        while (crossing.GetWorld().GetActor(crossing.GetPlayer())->position.x < 15) { Move(crossing, 1); }
        MeasureScenario("crossing_ungenerated_default_residency", 1, crossing, frontend.ui,
            [&](int) { Move(crossing, 1); frontend.Frame(crossing); });
    }
    Server server(*content, 42);
    Require(server.SetBubbleRadii(1, resident).has_value());
    auto vision = server.GetWorld().GetVisionProfile();
    vision.range = sight;
    Require(server.GetWorld().SetVisionProfile(vision).has_value());
    Prepare(server, *content, resident, flat);
    World& world = server.GetWorld();
    const auto Measure = [&](const char* name, int n, auto operation) {
        MeasureScenario(name, n, server, frontend.ui, operation);
    };
    for (int i = 0; i < 8; ++i) { Move(server, i % 2 == 0 ? 1 : -1); frontend.Frame(server); }
    std::printf("resident_chunks=%zu active_actors=%zu active_objects=%zu\n",
        world.LoadedChunks().size(), world.ActiveActors().size(), world.ActiveObjects().size());

    Vector<TileCoord> targets;
    for (int y = -10; y <= 10; ++y) {
        for (int z = -sight; z <= sight; ++z) {
            for (int x = -sight; x <= sight; ++x) {
                if (x*x + y*y + z*z <= sight*sight) { targets.push_back({x, z, y}); }
            }
        }
    }
    std::printf("point_los_calls_per_sweep=%zu\n", targets.size());
    VisibilityMask mask;
    PerceptionStats geometryStats{};
    MeasureScenario("geometry_bulk_visibility_mask", count, server, frontend.ui, [&](int) {
        BuildVisibilityMask(world, {0, 0, 0}, sight, mask, geometryStats);
        frontend.sink += mask.cells.size();
    }, &geometryStats);
    Measure("geometry_point_los_sphere", count, [&](int) {
        for (const auto target : targets) { frontend.sink += LineVisible(world, {0, 0, 0}, target); }
    });
    LightingField lighting;
    lighting.Rebuild(world);
    Measure("lighting_unchanged_refresh", count, [&](int) { lighting.Rebuild(world); frontend.sink += lighting.Get({0, 0}); });
    Measure("lighting_fresh_rebuild", count, [&](int) {
        LightingField fresh;
        fresh.Rebuild(world);
        frontend.sink += fresh.Get({0, 0});
    });
    Measure("memory_observation_warm", count, [&](int) { world.UpdatePlayerObservations(); });
    Measure("view_idle", count, [&](int) { auto view = server.View(12); frontend.sink += view.cells.size(); });
    const auto snapshot = server.View(12);
    Measure("terminal_existing_snapshot", count, [&](int) { frontend.Draw(snapshot, *content); });
    Measure("idle_view_and_terminal", count, [&](int) { frontend.Frame(server); });
    Measure("idle_update_view_terminal", count, [&](int) { server.Update(20000); frontend.Frame(server); });
    Measure("warm_move_server", count, [&](int i) { Move(server, i % 2 == 0 ? 1 : -1); });
    if (count % 2 != 0) { Move(server, -1); }
    Measure("warm_move_and_view", count, [&](int i) {
        Move(server, i % 2 == 0 ? 1 : -1);
        const auto view = server.View(12);
        frontend.sink += view.cells.size();
    });
    if (count % 2 != 0) { Move(server, -1); }
    Measure("warm_move_view_terminal", count, [&](int i) { Move(server, i % 2 == 0 ? 1 : -1); frontend.Frame(server); });
    if (count % 2 != 0) { Move(server, -1); }
    Measure("burst8_move_view_terminal", count, [&](int) {
        for (int k = 0; k < 8; ++k) { Move(server, k % 2 == 0 ? 1 : -1); frontend.Frame(server); }
    });

    // Start at x=15; each measured command changes chunk center. Alternation
    // exercises archive reload, rather than labeling it a no-stream warm move.
    while (world.GetActor(server.GetPlayer())->position.x < 15) { Move(server, 1); }
    Measure("crossing_first_east", 1, [&](int) { Move(server, 1); frontend.Frame(server); });
    Move(server, -1); Move(server, 1);
    Measure("crossing_cached_alternating", count, [&](int i) { Move(server, i % 2 == 0 ? -1 : 1); frontend.Frame(server); });
    while (world.GetActor(server.GetPlayer())->position.x > 0) { Move(server, -1); }

    // Opaque facade adjacent to the player, with one repeatedly toggled door.
    for (int z = -8; z <= 8; ++z) {
        Require(world.SetTile({1, z}, {content->FindTerrain("wall"), 0}).has_value());
    }
    Require(world.SetTile({1, 0}, {content->FindTerrain("closed_door"), 0}).has_value());
    world.UpdatePlayerObservations(); frontend.Frame(server);
    Measure("facade_walk_view_terminal", count, [&](int i) {
        const auto result = server.Send({CommandKind::move, 0, i % 2 == 0 ? 1 : -1});
        Require(result.outcome == Outcome::moved && result.advanced && !result.storageError);
        frontend.Frame(server);
    });
    if (count % 2 != 0) {
        Require(server.Send({CommandKind::move, 0, -1}).outcome == Outcome::moved);
    }
    Measure("door_toggle_view_terminal", count, [&](int) {
        const auto result = server.Send({CommandKind::interact, 1, 0});
        Require(result.outcome == Outcome::opened || result.outcome == Outcome::closed);
        frontend.Frame(server);
    });
    Require(world.SetTile({1, 0}, {content->FindTerrain("floor"), 0}).has_value());
    Require(world.SetActorLight(server.GetPlayer(), {240, 12}).has_value());
    world.UpdatePlayerObservations();
    Measure("moving_source_crossing_view_terminal", count, [&](int i) {
        Move(server, i % 2 == 0 ? -1 : 1); frontend.Frame(server);
    });
    if (count % 2 != 0) { Move(server, 1); }
    // A distinct warmed source path stays inside one chunk, avoiding the x=0 seam.
    Require(world.MoveActor(server.GetPlayer(), {8, 0, 0}).has_value());
    Move(server, -1); Move(server, 1); frontend.Frame(server);
    Measure("moving_source_loaded_view_terminal", count, [&](int i) {
        Move(server, i % 2 == 0 ? -1 : 1); frontend.Frame(server);
    });
    Require(world.SetActorLight(server.GetPlayer(), {}).has_value());

    // Raised platform beside an open shaft: real layered projection and stairs.
    for (int z = -3; z <= 3; ++z) {
        for (int x = -3; x <= 3; ++x) {
            Require(world.SetTile({x, z, 1}, {content->FindTerrain(x <= 0 ? "floor" : "air"), 0}).has_value());
        }
    }
    for (int y = 0; y <= 1; ++y) {
        Require(world.SetTile({-2, 0, y}, {content->FindTerrain("stairs"), 0}).has_value());
    }
    Require(world.MoveActor(server.GetPlayer(), {0, 0, 1}).has_value());
    world.UpdatePlayerObservations(); frontend.Frame(server);
    Measure("multilayer_move_view_terminal", count, [&](int i) {
        Move(server, i % 2 == 0 ? -1 : 1); frontend.Frame(server);
    });
    Require(world.CheckInvariants());
    std::printf("invariants=ok sink=%llu final_tick=%llu\n",
        static_cast<unsigned long long>(frontend.sink), static_cast<unsigned long long>(server.GetTick()));
    return 0;
}
