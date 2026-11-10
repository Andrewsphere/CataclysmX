#include "core/server.h"

#include <EASTL/sort.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>

namespace {
using namespace cataclysmx;
using Clock = std::chrono::steady_clock;

void Require(bool value) {
    if (!value) { std::fprintf(stderr, "benchmark invariant failed\n"); std::abort(); }
}

struct Samples {
    Vector<double> values;
    void Add(Clock::time_point start) {
        values.push_back(std::chrono::duration<double, std::milli>(Clock::now() - start).count());
    }
    void Print(const char* name, int radius, const World& world, ViewStats views = {},
        const PerceptionStats* phaseStats = nullptr) {
        eastl::sort(values.begin(), values.end());
        const auto p = [&](std::size_t n) { return values[(values.size() * n + 99) / 100 - 1]; };
        const auto s = phaseStats ? *phaseStats : world.GetPerceptionStats();
        std::printf("%s,%d,%zu,%.3f,%.3f,%.3f,%.3f,%zu,%zu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu\n",
            name, radius, values.size(), p(50), p(95), p(99), values.back(),
            world.LoadedChunks().size(), world.ArchivedChunkCount(),
            (unsigned long long)s.demandReconciliations, (unsigned long long)s.lightingRebuilds,
            (unsigned long long)s.observationScans, (unsigned long long)s.geometryRebuilds,
            (unsigned long long)s.opticalRebuilds, (unsigned long long)s.filterTilesSampled,
            (unsigned long long)s.memoryTilesExamined, (unsigned long long)s.memoryTilesChanged,
            (unsigned long long)views.requests, (unsigned long long)views.mapConstructions,
            (unsigned long long)s.columnsLoaded, (unsigned long long)s.columnsUnloaded,
            (unsigned long long)s.lightingOutputChunks, (unsigned long long)s.lightingSkyDirectChunks,
            (unsigned long long)s.lightingSkyFirstChunks, (unsigned long long)s.lightingSourcePatches);
        std::fflush(stdout);
    }
};

void AddStats(PerceptionStats& a, const PerceptionStats& b) {
    a.demandReconciliations += b.demandReconciliations;
    a.lightingRebuilds += b.lightingRebuilds;
    a.observationScans += b.observationScans;
    a.geometryRebuilds += b.geometryRebuilds;
    a.opticalRebuilds += b.opticalRebuilds;
    a.filterTilesSampled += b.filterTilesSampled;
    a.memoryTilesExamined += b.memoryTilesExamined;
    a.memoryTilesChanged += b.memoryTilesChanged;
    a.columnsLoaded += b.columnsLoaded;
    a.columnsUnloaded += b.columnsUnloaded;
    a.lightingOutputChunks += b.lightingOutputChunks;
    a.lightingSkyDirectChunks += b.lightingSkyDirectChunks;
    a.lightingSkyFirstChunks += b.lightingSkyFirstChunks;
    a.lightingSourcePatches += b.lightingSourcePatches;
}

void ClearActors(World& world, ActorHandle player) {
    for (auto actor : world.ActiveActors()) {
        if (actor != player) { Require(world.DestroyActor(actor).has_value()); }
    }
    for (auto object : world.ActiveObjects()) { Require(world.DestroyObject(object).has_value()); }
}

// Position A/B straddle the requested seam. Ground and headroom are edited
// only on the route; surrounding generated terrain remains representative.
void ClearRoute(World& world, const ContentCatalog& content, bool zAxis, int a, int b) {
    for (int n = a; n <= b; ++n) {
        for (int y = 0; y <= 2; ++y) {
            TileCoord p = zAxis ? TileCoord{0, n, y} : TileCoord{n, 0, y};
            Require(world.SetTile(p, {content.FindTerrain(y == 0 ? "floor" : "air"), 0}).has_value());
        }
    }
}

void Split(const ContentCatalog& content, int radius, int count, bool fresh) {
    World world(content, 42);
    auto resident = world.RequestChunks({{0, 0, 0}, radius}, LoadMode::resident);
    auto active = world.RequestChunks({{0, 0, 0}, 1}, LoadMode::active);
    Require(resident.has_value() && active.has_value() && world.ReconcileChunks().has_value());
    ClearActors(world, {});
    ClearRoute(world, content, false, 0, 15);
    auto player = world.CreateActor({15, 0, 0}, true);
    Require(player.has_value());
    world.UpdatePlayerObservations();
    if (!fresh) {
        Require(world.UpdateRequest(*resident, {{1, 0, 0}, radius}, LoadMode::resident).has_value());
        Require(world.UpdateRequest(*active, {{1, 0, 0}, 1}, LoadMode::active).has_value());
        Require(world.ReconcileChunks().has_value());
        ClearRoute(world, content, false, 16, 16);
        Require(world.UpdateRequest(*resident, {{0, 0, 0}, radius}, LoadMode::resident).has_value());
        Require(world.UpdateRequest(*active, {{0, 0, 0}, 1}, LoadMode::active).has_value());
        Require(world.ReconcileChunks().has_value());
    }
    world.ResetPerceptionStats();
    Samples reconcile, observe;
    // Fresh runs advance the request into previously unseen columns; the player
    // remains at the origin, so observation is measured only in the warm case.
    PerceptionStats reconcileStats{}, observeStats{};
    for (int i = 0; i < count; ++i) {
        const int center = fresh ? i + 1 : (i % 2 == 0 ? 1 : 0);
        if (!fresh) { Require(world.MoveActor(*player, {center ? 16 : 15, 0, 0}).has_value()); }
        world.ResetPerceptionStats();
        auto start = Clock::now();
        Require(world.UpdateRequest(*resident, {{center, 0, 0}, radius}, LoadMode::resident).has_value());
        Require(world.UpdateRequest(*active, {{center, 0, 0}, 1}, LoadMode::active).has_value());
        Require(world.ReconcileChunks().has_value());
        reconcile.Add(start);
        AddStats(reconcileStats, world.GetPerceptionStats());
        if (!fresh) {
            world.ResetPerceptionStats();
            start = Clock::now();
            (void)world.GetLightLevel({center ? 16 : 15, 0, 0});
            world.UpdatePlayerObservations();
            observe.Add(start);
            AddStats(observeStats, world.GetPerceptionStats());
        }
    }
    reconcile.Print(fresh ? "fresh_reconcile" : "archive_reconcile", radius, world, {}, &reconcileStats);
    if (!fresh) { observe.Print("archive_light_observe", radius, world, {}, &observeStats); }
}

void ServerCase(const ContentCatalog& content, int radius, int count, bool zAxis, bool negative, int circleTiles = -1) {
    Server server(content, 42);
    if (circleTiles >= 0) { Require(server.SetCircleResidency(1, {circleTiles}).has_value()); }
    else { Require(server.SetBubbleRadii(1, radius).has_value()); }
    World& world = server.GetWorld();
    ClearActors(world, server.GetPlayer());
    const int a = negative ? -1 : 15;
    ClearRoute(world, content, zAxis, negative ? -1 : 0, negative ? 0 : 16);
    Require(world.MoveActor(server.GetPlayer(), zAxis ? TileCoord{0, a, 0} : TileCoord{a, 0, 0}).has_value());
    auto move = [&](int direction) {
        const auto result = server.Send({CommandKind::move, zAxis ? 0 : direction, zAxis ? direction : 0});
        Require(result.outcome == Outcome::moved && result.advanced && !result.storageError);
        auto view = server.View(12);
        Require(view.cells.size() == 25 * 25);
    };
    // Synchronize the server's bubble after the administrative teleport, then
    // warm both sides and their archived outer columns before measuring.
    move(1); move(-1); move(1); move(-1);
    world.ResetPerceptionStats();
    server.ResetViewStats();
    Samples samples;
    for (int i = 0; i < count; ++i) {
        auto start = Clock::now();
        move(i % 2 == 0 ? 1 : -1);
        samples.Add(start);
    }
    const char* name = circleTiles >= 0
        ? (zAxis ? (negative ? "circle_z_negative" : "circle_z_15_16") :
            (negative ? "circle_x_negative" : "circle_x_15_16"))
        : (zAxis ? (negative ? "server_z_negative" : "server_z_15_16") :
            (negative ? "server_x_negative" : "server_x_15_16"));
    samples.Print(name, radius, world, server.GetViewStats());
    Require(world.CheckInvariants());
}

void CircleForward(const ContentCatalog& content, int radius, int circleTiles, int count) {
    Server server(content, 42);
    Require(server.SetCircleResidency(1, {circleTiles}).has_value());
    World& world = server.GetWorld();
    ClearActors(world, server.GetPlayer());
    ClearRoute(world, content, false, 0, 16);
    Require(world.MoveActor(server.GetPlayer(), {0, 0, 0}).has_value());
    auto move = [&](int direction) {
        const auto result = server.Send({CommandKind::move, direction, 0});
        Require(result.outcome == Outcome::moved && result.advanced && !result.storageError);
        const auto view = server.View(12);
        Require(view.cells.size() == 25 * 25);
    };
    // Synchronize the bubble after teleport and warm the complete route.
    for (int i = 0; i < 16; ++i) { move(1); }
    for (int i = 0; i < 16; ++i) { move(-1); }
    Samples samples[16];
    PerceptionStats stats[16]{};
    ViewStats views[16]{};
    for (int run = 0; run < count; ++run) {
        for (int step = 0; step < 16; ++step) {
            world.ResetPerceptionStats();
            server.ResetViewStats();
            const auto start = Clock::now();
            move(1);
            samples[step].Add(start);
            AddStats(stats[step], world.GetPerceptionStats());
            const auto v = server.GetViewStats();
            views[step].requests += v.requests;
            views[step].mapConstructions += v.mapConstructions;
        }
        for (int i = 0; i < 16; ++i) { move(-1); }
    }
    for (int step = 0; step < 16; ++step) {
        char name[40];
        std::snprintf(name, sizeof(name), "circle_forward_x_step_%02d", step + 1);
        samples[step].Print(name, radius, world, views[step], &stats[step]);
    }
    Require(world.CheckInvariants());
}

// A newly created directory gives this invocation exclusive ownership of every
// SQLite sidecar as well as the database. Never remove a pre-existing path.
struct SaveDirectory {
    std::filesystem::path directory;
    std::string database;
    SaveDirectory() {
        std::error_code error;
        const auto parent = std::filesystem::temp_directory_path(error);
        Require(!error);
        const auto stamp = Clock::now().time_since_epoch().count();
        for (int attempt = 0; attempt < 100; ++attempt) {
            auto candidate = parent / ("cataclysmx-chunk-bench-" + std::to_string(stamp) + "-" + std::to_string(attempt));
            if (std::filesystem::create_directory(candidate, error)) { directory = candidate; break; }
            Require(!error);
        }
        Require(!directory.empty());
        database = (directory / "world.sqlite").string();
    }
    ~SaveDirectory() {
        std::error_code error;
        std::filesystem::remove_all(directory, error);
        if (error) { std::fprintf(stderr, "could not remove benchmark directory: %s\n", directory.string().c_str()); }
    }
};

void Disk(const ContentCatalog& content, int radius, int count) {
    SaveDirectory files;
    const TileCoord marker{(radius + 1) * chunk_size, 0, 0};
    const Tile savedTile{content.FindTerrain("floor"), 37};
    SessionState session;
    {
        World world(content, 42);
        auto base = world.RequestChunks({{0, 0, 0}, radius}, LoadMode::resident);
        auto extra = world.RequestChunks({{1, 0, 0}, radius}, LoadMode::resident);
        auto active = world.RequestChunks({{0, 0, 0}, 1}, LoadMode::active);
        Require(base && extra && active && world.ReconcileChunks().has_value());
        ClearActors(world, {});
        ClearRoute(world, content, false, 0, 15);
        auto player = world.CreateActor({15, 0, 0}, true);
        Require(player.has_value());
        session.player = world.GetActor(*player)->id;
        session.playerPosition = {15, 0, 0};
        Require(world.SetTile(marker, savedTile).has_value());
        Require(world.CreateSave(files.database.c_str(), session).has_value());
        // Evict the checkpointed incoming edge before closing this world.
        Require(world.ReleaseRequest(*extra).has_value() && world.ReconcileChunks().has_value());
    }
    Samples cold, coldLight, warm, warmLight;
    PerceptionStats coldStats{}, coldLightStats{}, warmStats{}, warmLightStats{};
    // The reopened world has no process-local warm archive. Its base residency
    // is materialized outside the timer; only the incoming edge is timed.
    for (int i = 0; i < count; ++i) {
        World world(content, 42);
        Require(world.LoadSave(files.database.c_str()).has_value());
        auto base = world.RequestChunks({{0, 0, 0}, radius}, LoadMode::resident);
        auto active = world.RequestChunks({{0, 0, 0}, 1}, LoadMode::active);
        Require(base && active && world.ReconcileChunks().has_value());
        world.ResetPerceptionStats();
        auto start = Clock::now();
        Require(world.UpdateRequest(*base, {{1, 0, 0}, radius}, LoadMode::resident).has_value());
        Require(world.ReconcileChunks().has_value());
        cold.Add(start);
        AddStats(coldStats, world.GetPerceptionStats());
        Require(world.GetTile(marker) && *world.GetTile(marker) == savedTile);
        world.ResetPerceptionStats();
        start = Clock::now();
        (void)world.GetLightLevel({15, 0, 0});
        world.UpdatePlayerObservations();
        coldLight.Add(start);
        AddStats(coldLightStats, world.GetPerceptionStats());
        if (i == count - 1) {
            cold.Print("disk_reopen_reconcile", radius, world, {}, &coldStats);
            coldLight.Print("disk_reopen_light_observe", radius, world, {}, &coldLightStats);
        }
    }
    {
        World world(content, 42);
        Require(world.LoadSave(files.database.c_str()).has_value());
        auto base = world.RequestChunks({{1, 0, 0}, radius}, LoadMode::resident);
        auto active = world.RequestChunks({{0, 0, 0}, 1}, LoadMode::active);
        Require(base && active && world.ReconcileChunks().has_value());
        for (int i = 0; i < count; ++i) {
            Require(world.UpdateRequest(*base, {{0, 0, 0}, radius}, LoadMode::resident).has_value());
            Require(world.ReconcileChunks().has_value());
            Require(!world.GetTile(marker));
            // Checkpoint promotes eligible evicted archives to the bounded warm
            // cache; a repeated reload without this would use dirty archives.
            Require(world.Save(session).has_value());
            world.ResetPerceptionStats();
            auto start = Clock::now();
            Require(world.UpdateRequest(*base, {{1, 0, 0}, radius}, LoadMode::resident).has_value());
            Require(world.ReconcileChunks().has_value());
            warm.Add(start);
            AddStats(warmStats, world.GetPerceptionStats());
            Require(world.GetTile(marker) && *world.GetTile(marker) == savedTile);
            world.ResetPerceptionStats();
            start = Clock::now();
            (void)world.GetLightLevel({15, 0, 0});
            world.UpdatePlayerObservations();
            warmLight.Add(start);
            AddStats(warmLightStats, world.GetPerceptionStats());
        }
        warm.Print("disk_same_world_reconcile", radius, world, {}, &warmStats);
        warmLight.Print("disk_same_world_light_observe", radius, world, {}, &warmLightStats);
    }
}
}

int main(int argc, char** argv) {
    int count = 10, radius = 2, circleTiles = CircleResidency{}.radiusTiles;
    const char* scenario = "all";
    auto parse = [](const char* text, int& value) {
        char* end = nullptr;
        const long number = std::strtol(text, &end, 10);
        if (!*text || *end || number < 0 || number > 10000) { return false; }
        value = static_cast<int>(number);
        return true;
    };
    bool valid = true;
    if (argc > 1) { valid = parse(argv[1], count); }
    if (argc > 2) { valid = parse(argv[2], radius) && valid; }
    if (argc > 3) { scenario = argv[3]; }
    if (argc > 4) { valid = parse(argv[4], circleTiles) && valid; }
    if (!valid || argc > 5 || count < 1 || count > 10000 || (radius != 2 && radius != 5) ||
        circleTiles > maxCircleResidencyRadius ||
        (std::strcmp(scenario, "all") && std::strcmp(scenario, "split") &&
         std::strcmp(scenario, "fresh") && std::strcmp(scenario, "server") &&
         std::strcmp(scenario, "circle") && std::strcmp(scenario, "disk"))) {
        std::fprintf(stderr, "usage: cataclysmx_chunk_benchmark [samples:1..10000] [radius:2|5] [all|split|fresh|server|circle|disk] [circle_radius_tiles:0..128]\n");
        return 2;
    }
    auto content = LoadContentFile(CATACLYSMX_TERRAIN_FILE);
    Require(content.has_value() && ValidateServerContent(*content).has_value());
    std::printf("seed=42 square_radius_chunks=%d circle_radius_tiles=%d samples=%d view_radius=12\n", radius, circleTiles, count);
    if (count < 100) { std::fprintf(stderr, "note: fewer than 100 samples gives coarse p95/p99 estimates\n"); }
    std::printf("case,radius,n,median_ms,p95_ms,p99_ms,max_ms,loaded,archived,demand,lighting,observations,geometry,optics,filter_tiles,memory_examined,memory_changed,view_requests,map_builds,columns_loaded,columns_unloaded,lighting_output_chunks,lighting_sky_direct_chunks,lighting_sky_first_chunks,lighting_source_patches\n");
    if (!std::strcmp(scenario, "all") || !std::strcmp(scenario, "split")) { Split(*content, radius, count, false); }
    if (!std::strcmp(scenario, "all") || !std::strcmp(scenario, "fresh")) { Split(*content, radius, count, true); }
    if (!std::strcmp(scenario, "all") || !std::strcmp(scenario, "server")) {
        for (int axis = 0; axis < 2; ++axis) {
            for (int negative = 0; negative < 2; ++negative) { ServerCase(*content, radius, count, axis != 0, negative != 0); }
        }
    }
    if (!std::strcmp(scenario, "all") || !std::strcmp(scenario, "circle")) {
        for (int axis = 0; axis < 2; ++axis) {
            for (int negative = 0; negative < 2; ++negative) { ServerCase(*content, radius, count, axis != 0, negative != 0, circleTiles); }
        }
        CircleForward(*content, radius, circleTiles, count);
    }
    if (!std::strcmp(scenario, "all") || !std::strcmp(scenario, "disk")) { Disk(*content, radius, count); }
}
