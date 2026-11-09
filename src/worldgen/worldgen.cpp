#include "worldgen/worldgen.h"

#include <cstdlib>

namespace cataclysmx {
namespace {

void Require(bool condition) {
    if (!condition) { std::abort(); }
}

int Min(int a, int b) { return a < b ? a : b; }
int Max(int a, int b) { return a > b ? a : b; }
int FloorDiv(int value, int divisor) {
    const int quotient = value / divisor;
    return quotient - (value % divisor < 0 ? 1 : 0);
}
void CheckPosition(TileCoord p) {
    Require(p.x >= -world_coordinate_limit && p.x <= world_coordinate_limit
        && p.z >= -world_coordinate_limit && p.z <= world_coordinate_limit
        && p.y >= world_min_y && p.y <= world_max_y);
}
void CheckRegion(RegionCoord r) {
    constexpr int minimum = -world_coordinate_limit / region_size - 1;
    constexpr int maximum = world_coordinate_limit / region_size;
    Require(r.x >= minimum && r.x <= maximum && r.z >= minimum && r.z <= maximum);
}
bool Inside(TileRect r, TileCoord p) {
    return p.x >= r.min.x && p.x < r.max.x && p.z >= r.min.z && p.z < r.max.z;
}
bool Intersects(TileRect a, TileRect b) {
    return a.min.x < b.max.x && b.min.x < a.max.x
        && a.min.z < b.max.z && b.min.z < a.max.z;
}
bool Demo(TileCoord p) {
    return p.x >= -32 && p.x <= 32 && p.z >= -32 && p.z <= 32;
}
std::uint64_t Mix(std::uint64_t value) {
    value ^= value >> 30;
    value *= 0xbf58476d1ce4e5b9ULL;
    value ^= value >> 27;
    value *= 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}
std::uint64_t Hash(std::uint64_t seed, int x, int z, std::uint64_t domain) {
    return Mix(seed ^ Mix(static_cast<std::uint64_t>(x))
        ^ Mix(static_cast<std::uint64_t>(z) + 0x9e3779b97f4a7c15ULL) ^ domain);
}
int Smooth(int value) { // Fixed point [0,1024], cubic smoothstep.
    return static_cast<int>((static_cast<std::int64_t>(value) * value
        * (3072 - 2 * value)) / (1024 * 1024));
}
int Lerp(int a, int b, int fraction) { return (a * (1024 - fraction) + b * fraction) / 1024; }
int Noise(std::uint64_t seed, TileCoord p, int scale, std::uint64_t domain) {
    const int x = FloorDiv(p.x, scale);
    const int z = FloorDiv(p.z, scale);
    const int fx = Smooth((p.x - x * scale) * 1024 / scale);
    const int fz = Smooth((p.z - z * scale) * 1024 / scale);
    const int a = static_cast<int>(Hash(seed, x, z, domain) % 1001);
    const int b = static_cast<int>(Hash(seed, x + 1, z, domain) % 1001);
    const int c = static_cast<int>(Hash(seed, x, z + 1, domain) % 1001);
    const int d = static_cast<int>(Hash(seed, x + 1, z + 1, domain) % 1001);
    return Lerp(Lerp(a, b, fx), Lerp(c, d, fx), fz);
}

ContentResult<void> ValidatePalette(const ContentCatalog& content, Diagnostics* diagnostics, bool requireAir) {
    for (const char* name : {"floor", "wall", "grass", "road", "rubble", "air", "stairs"}) {
        const auto role = eastl::string_view(name);
        const auto* terrain = content.TryGetTerrain(content.FindTerrain(name));
        const bool required = role == "floor" || role == "wall" || (role == "air" && requireAir);
        const bool missing = terrain == nullptr && required;
        bool invalid = false;
        const char* requirement = "' must be passable floor support without stairs";
        if (role == "air") {
            invalid = terrain && !terrain->openSpace;
            requirement = "' must be open_space";
        } else if (role == "stairs") {
            invalid = terrain && !terrain->stairs;
            requirement = "' must have stairs=true";
        } else if (role != "wall") {
            invalid = terrain && (!terrain->passable || terrain->openSpace || terrain->stairs);
        }
        if (!missing && !invalid) { continue; }
        if (diagnostics) {
            String message = "World generation terrain '";
            message += name;
            message += missing ? "' is required" : requirement;
            diagnostics->Add(eastl::string_view(message.data(), message.size()));
        }
        return eastl::unexpected(ContentError::InvalidData);
    }
    return {};
}

struct Palette {
    TerrainId floor;
    TerrainId wall;
    TerrainId grass;
    TerrainId tree;
    TerrainId road;
    TerrainId rubble;
};
Palette GetPalette(const ContentCatalog& content) {
    Require(ValidatePalette(content, nullptr, false).has_value());
    Palette p{};
    p.floor = content.FindTerrain("floor");
    p.wall = content.FindTerrain("wall");
    Require(p.floor.value != 0 && p.wall.value != 0);
    p.grass = content.FindTerrain("grass");
    p.tree = content.FindTerrain("tree");
    p.road = content.FindTerrain("road");
    p.rubble = content.FindTerrain("rubble");
    if (!p.grass.value) { p.grass = p.floor; }
    if (!p.tree.value) { p.tree = p.wall; }
    if (!p.road.value) { p.road = p.floor; }
    if (!p.rubble.value) { p.rubble = p.floor; }
    return p;
}

BoundaryCrossing Crossing(std::uint64_t seed, BorderAxis axis, int border, int along) {
    const auto random = Hash(seed, border, along, axis == BorderAxis::X ? 11 : 12);
    const int offset = along * region_size + 80 + static_cast<int>(random % 97);
    return {{axis, border, along}, axis == BorderAxis::X
        ? TileCoord{border, offset} : TileCoord{offset, border}, 3, 16};
}
void AddRoad(RegionPlan& plan, TileCoord a, TileCoord b) {
    Require(a.x == b.x || a.z == b.z);
    plan.roads.push_back({a, b, 3});
}
bool OnSegment(const RoadSegment& road, TileCoord p) {
    Require(road.width == 1 || road.width == 3);
    Require(road.first.x == road.last.x || road.first.z == road.last.z);
    const int radius = road.width / 2;
    // Square ends make orthogonal joints seamless. Plan bounds clip outer caps.
    return p.x >= Min(road.first.x, road.last.x) - radius
        && p.x <= Max(road.first.x, road.last.x) + radius
        && p.z >= Min(road.first.z, road.last.z) - radius
        && p.z <= Max(road.first.z, road.last.z) + radius;
}

int EntranceSide(const BuildingPattern& pattern) {
    if (pattern.entrance.z == 0) { return 0; }
    if (pattern.entrance.x == pattern.width - 1) { return 1; }
    if (pattern.entrance.z == pattern.height - 1) { return 2; }
    Require(pattern.entrance.x == 0);
    return 3;
}
PatternPoint Rotate(PatternPoint p, int width, int height, int turns) {
    switch (turns) {
    case 0: return p;
    case 1: return {height - 1 - p.z, p.x};
    case 2: return {width - 1 - p.x, height - 1 - p.z};
    case 3: return {p.z, width - 1 - p.x};
    default: std::abort();
    }
}
int Turns(const BuildingPattern& pattern, Frontage frontage) {
    Require(frontage == Frontage::North || frontage == Frontage::South);
    return ((frontage == Frontage::North ? 0 : 2) - EntranceSide(pattern) + 4) % 4;
}
bool Fits(const BuildingPattern& pattern, const ParcelPlan& parcel) {
    const int turns = Turns(pattern, parcel.frontage);
    const int width = turns % 2 ? pattern.height : pattern.width;
    const int height = turns % 2 ? pattern.width : pattern.height;
    return width <= parcel.bounds.max.x - parcel.bounds.min.x - 4
        && height <= parcel.bounds.max.z - parcel.bounds.min.z - 4;
}
void AddBuilding(const ContentCatalog& content, RegionPlan& plan, const ParcelPlan& parcel) {
    std::uint64_t total = 0;
    for (std::size_t i = 1; i <= content.BuildingPatternCount(); ++i) {
        const auto& pattern = content.GetBuildingPattern({static_cast<std::uint32_t>(i)});
        if (Fits(pattern, parcel)) { total += pattern.weight; }
    }
    if (total == 0) { return; }
    const auto random = Hash(plan.seed, plan.region.x, plan.region.z, 100 + parcel.owner.parcel);
    auto choice = random % total;
    BuildingPatternId selected{};
    for (std::size_t i = 1; i <= content.BuildingPatternCount(); ++i) {
        const auto& pattern = content.GetBuildingPattern({static_cast<std::uint32_t>(i)});
        if (!Fits(pattern, parcel)) { continue; }
        if (choice < pattern.weight) { selected = {static_cast<std::uint32_t>(i)}; break; }
        choice -= pattern.weight;
    }
    Require(selected.value != 0);
    const auto& pattern = content.GetBuildingPattern(selected);
    const int turns = Turns(pattern, parcel.frontage);
    const int width = turns % 2 ? pattern.height : pattern.width;
    const int height = turns % 2 ? pattern.width : pattern.height;
    const int slack = parcel.bounds.max.x - parcel.bounds.min.x - width - 4;
    const int x = parcel.bounds.min.x + 2 + static_cast<int>(Mix(random) % (slack + 1));
    const int z = parcel.frontage == Frontage::North ? parcel.bounds.min.z + 2
        : parcel.bounds.max.z - height - 2;
    const auto entry = Rotate(pattern.entrance, pattern.width, pattern.height, turns);
    const TileCoord entrance{x + entry.x, z + entry.z};
    const auto abandonmentSeed = Mix(random ^ 0xa54ff53a5f1d36f1ULL);
    plan.buildings.push_back({parcel.owner, selected, static_cast<std::uint8_t>(turns),
        {{x, z}, {x + width, z + height}}, entrance,
        {entrance, {entrance.x, parcel.streetZ}, 1}, abandonmentSeed,
        abandonmentSeed % 4 != 0});
}

bool Accessible(const ContentCatalog& content, TerrainId terrain) {
    const auto& definition = content.GetTerrain(terrain);
    if (definition.passable) { return true; }
    const auto* opened = content.TryGetTerrain(definition.opensTo);
    return opened && opened->passable;
}

void DecorateAbandonedBuilding(const ContentCatalog& content, const Palette& palette,
                              const BuildingPlan& building, Vector<Tile>& tiles,
                              int width, int height) {
    if (!building.abandoned) { return; }

    // BFS in fixed N,E,S,W order finds a usable route to the farthest reachable
    // interior tile, treating immediately openable doors as traversable. Preserve
    // that route and every door; decorator only replaces exact floor/wall IDs.
    const int start = (building.entrance.z - building.bounds.min.z) * width
        + building.entrance.x - building.bounds.min.x;
    Require(start >= 0 && start < width * height);
    Vector<int> parent(tiles.size(), -1);
    Vector<int> queue;
    queue.push_back(start);
    parent[static_cast<std::size_t>(start)] = start;
    for (std::size_t head = 0; head < queue.size(); ++head) {
        const int current = queue[head];
        const int x = current % width;
        const int z = current / width;
        const TileCoord neighbors[] = {{x, z - 1}, {x + 1, z}, {x, z + 1}, {x - 1, z}};
        for (const auto p : neighbors) {
            if (p.x < 0 || p.x >= width || p.z < 0 || p.z >= height) { continue; }
            const auto next = static_cast<std::size_t>(p.z * width + p.x);
            if (parent[next] != -1 || !Accessible(content, tiles[next].terrain)) { continue; }
            parent[next] = current;
            queue.push_back(static_cast<int>(next));
        }
    }
    Vector<std::uint8_t> protectedTiles(tiles.size(), 0);
    int current = queue.back();
    for (;;) {
        protectedTiles[static_cast<std::size_t>(current)] = 1;
        if (current == start) { break; }
        current = parent[static_cast<std::size_t>(current)];
    }
    for (std::size_t i = 0; i < tiles.size(); ++i) {
        if (protectedTiles[i]) { continue; }
        auto& tile = tiles[i];
        if (tile.terrain != palette.wall && tile.terrain != palette.floor) { continue; }
        const auto random = Mix(building.abandonmentSeed ^ Mix(i));
        if (random % 100 < (tile.terrain == palette.wall ? 18 : 12)) {
            tile.terrain = palette.rubble;
            tile.damage = static_cast<std::uint8_t>(32 + (random >> 16) % 160);
        }
    }
}

Vector<Tile> MaterializeBuilding(const ContentCatalog& content, const Palette& palette,
                                 const BuildingPlan& building) {
    const auto& pattern = content.GetBuildingPattern(building.pattern);
    const int width = building.bounds.max.x - building.bounds.min.x;
    const int height = building.bounds.max.z - building.bounds.min.z;
    Require(building.quarterTurns < 4 && width > 0 && height > 0 && width <= 64 && height <= 64);
    Vector<Tile> tiles(static_cast<std::size_t>(width * height));
    for (int z = 0; z < pattern.height; ++z) {
        for (int x = 0; x < pattern.width; ++x) {
            const auto p = Rotate({x, z}, pattern.width, pattern.height, building.quarterTurns);
            Require(p.x < width && p.z < height);
            tiles[static_cast<std::size_t>(p.z * width + p.x)] =
                {pattern.tiles[static_cast<std::size_t>(z * pattern.width + x)], 0};
        }
    }
    DecorateAbandonedBuilding(content, palette, building, tiles, width, height);
    return tiles;
}
}

ContentResult<void> ValidateWorldgenContent(const ContentCatalog& content, Diagnostics* diagnostics) {
    return ValidatePalette(content, diagnostics, true);
}

RegionCoord RegionOf(TileCoord position) {
    CheckPosition(position);
    return {FloorDiv(position.x, region_size), FloorDiv(position.z, region_size)};
}

EnvironmentSample SampleEnvironment(std::uint64_t seed, TileCoord position) {
    CheckPosition(position);
    position.y = 0;
    EnvironmentSample sample;
    sample.moisture = (Noise(seed, position, 256, 21) + 2 * Noise(seed, position, 1024, 22)) / 3;
    sample.temperature = Noise(seed, position, 1024, 23);
    sample.woodland = (2 * Noise(seed, position, 128, 24) + sample.moisture) / 3;
    sample.biome = sample.woodland >= 520 ? Biome::Woodland : Biome::Meadow;
    return sample;
}

RegionPlan GenerateRegionPlan(const ContentCatalog& content, std::uint64_t seed, RegionCoord region) {
    CheckRegion(region);
    (void)GetPalette(content);
    RegionPlan plan;
    plan.region = region;
    plan.seed = seed;
    const int x = region.x * region_size;
    const int z = region.z * region_size;
    plan.bounds = {{Max(x, -world_coordinate_limit), Max(z, -world_coordinate_limit)},
        {Min(x + region_size, world_coordinate_limit + 1), Min(z + region_size, world_coordinate_limit + 1)}};
    plan.townBounds = {{x + 72, z + 72}, {x + 184, z + 184}};
    plan.townCenter = {x + 128, z + 128};
    plan.hasTown = Inside(plan.bounds, plan.townBounds.min)
        && Inside(plan.bounds, {plan.townBounds.max.x - 1, plan.townBounds.max.z - 1});
    plan.boundaries = {{Crossing(seed, BorderAxis::Z, z, region.x),
        Crossing(seed, BorderAxis::X, x + 256, region.z),
        Crossing(seed, BorderAxis::Z, z + 256, region.x),
        Crossing(seed, BorderAxis::X, x, region.z)}};

    // Three vertical/horizontal town streets, followed by N,E,S,W connectors.
    for (int offset : {72, 128, 184}) {
        AddRoad(plan, {x + offset, z + 72}, {x + offset, z + 184});
        AddRoad(plan, {x + 72, z + offset}, {x + 184, z + offset});
    }
    auto p = plan.boundaries[0].position;
    AddRoad(plan, p, {p.x, z + 56});
    AddRoad(plan, {p.x, z + 56}, {x + 128, z + 56});
    AddRoad(plan, {x + 128, z + 56}, {x + 128, z + 72});
    p = plan.boundaries[1].position;
    AddRoad(plan, p, {x + 200, p.z});
    AddRoad(plan, {x + 200, p.z}, {x + 200, z + 128});
    AddRoad(plan, {x + 200, z + 128}, {x + 184, z + 128});
    p = plan.boundaries[2].position;
    AddRoad(plan, p, {p.x, z + 200});
    AddRoad(plan, {p.x, z + 200}, {x + 128, z + 200});
    AddRoad(plan, {x + 128, z + 200}, {x + 128, z + 184});
    p = plan.boundaries[3].position;
    AddRoad(plan, p, {x + 56, p.z});
    AddRoad(plan, {x + 56, p.z}, {x + 56, z + 128});
    AddRoad(plan, {x + 56, z + 128}, {x + 72, z + 128});

    if (!plan.hasTown) { return plan; }
    for (int row = 0; row < 2; ++row) {
        for (int column = 0; column < 2; ++column) {
            const int left = x + 74 + column * 56;
            const int top = z + 74 + row * 56;
            for (int half = 0; half < 2; ++half) {
                ParcelPlan parcel{{region, static_cast<std::uint32_t>(plan.parcels.size())},
                    {{left, top + half * 26}, {left + 53, top + (half == 0 ? 26 : 53)}},
                    half == 0 ? Frontage::North : Frontage::South,
                    z + 72 + (row + half) * 56};
                plan.parcels.push_back(parcel);
                AddBuilding(content, plan, parcel);
            }
        }
    }
    return plan;
}

bool Contains(TileRect bounds, TileCoord position) {
    CheckPosition(position);
    // Covers all nominal geometry of valid regions, including max-exclusive.
    constexpr int geometryLimit = 1'000'192;
    Require(bounds.min.x >= -geometryLimit && bounds.min.z >= -geometryLimit
        && bounds.max.x <= geometryLimit && bounds.max.z <= geometryLimit
        && bounds.min.x <= bounds.max.x && bounds.min.z <= bounds.max.z);
    return Inside(bounds, position);
}

bool IsRoad(const RegionPlan& plan, TileCoord position) {
    CheckPosition(position);
    CheckRegion(plan.region);
    if (!Inside(plan.bounds, position) || Demo(position)) { return false; }
    for (const auto& road : plan.roads) {
        if (OnSegment(road, position)) { return true; }
    }
    for (const auto& building : plan.buildings) {
        if (!Inside(building.bounds, position) && OnSegment(building.driveway, position)) { return true; }
    }
    return false;
}
const ParcelPlan* ParcelAt(const RegionPlan& plan, TileCoord position) {
    CheckPosition(position);
    CheckRegion(plan.region);
    if (!Inside(plan.bounds, position)) { return nullptr; }
    for (const auto& parcel : plan.parcels) {
        if (Inside(parcel.bounds, position)) { return &parcel; }
    }
    return nullptr;
}
const BuildingPlan* BuildingAt(const RegionPlan& plan, TileCoord position) {
    CheckPosition(position);
    CheckRegion(plan.region);
    if (!Inside(plan.bounds, position)) { return nullptr; }
    for (const auto& building : plan.buildings) {
        if (Inside(building.bounds, position)) { return &building; }
    }
    return nullptr;
}

eastl::array<Tile, chunk_size * chunk_size> GenerateChunk(
    const ContentCatalog& content, std::uint64_t seed, const RegionPlan& plan, ChunkCoord chunk) {
    constexpr int chunkLimit = world_coordinate_limit / chunk_size;
    Require(chunk.x >= -chunkLimit && chunk.x <= chunkLimit
        && chunk.z >= -chunkLimit && chunk.z <= chunkLimit
        && chunk.y >= world_min_y && chunk.y <= world_max_y);
    CheckRegion(plan.region);
    Require(plan.seed == seed);
    const int x = chunk.x * chunk_size;
    const int z = chunk.z * chunk_size;
    Require((RegionCoord{FloorDiv(x, region_size), FloorDiv(z, region_size)}) == plan.region);
    const auto palette = GetPalette(content);
    const auto air = content.FindTerrain("air");
    if (chunk.y > 0) {
        const auto* definition = content.TryGetTerrain(air);
        Require(definition && definition->openSpace);
    }
    eastl::array<Tile, chunk_size * chunk_size> tiles{};
    for (int dz = 0; dz < chunk_size; ++dz) {
        for (int dx = 0; dx < chunk_size; ++dx) {
            const TileCoord p{x + dx, z + dz, chunk.y};
            auto& tile = tiles[static_cast<std::size_t>(dz * chunk_size + dx)];
            tile = {palette.wall, 0};
            if (!Inside(plan.bounds, p)) { continue; }
            if (chunk.y < 0) { continue; }
            if (chunk.y > 0) {
                tile.terrain = chunk.y == 1 && BuildingAt(plan, p) ? palette.floor : air;
                continue;
            }
            if (Demo(p)) { tile.terrain = palette.floor; continue; }
            const auto environment = SampleEnvironment(seed, p);
            const int treeThreshold = environment.woodland < 400 ? 15 : (environment.woodland - 350) / 2;
            tile.terrain = Hash(seed, p.x, p.z, 25) % 1000 < static_cast<std::uint64_t>(treeThreshold)
                ? palette.tree : palette.grass;
            // Keep parcels usable: no trees in yards, access or building sites.
            if (ParcelAt(plan, p)) { tile.terrain = palette.grass; }
            if (IsRoad(plan, p)) { tile.terrain = palette.road; }
        }
    }
    if (chunk.y != 0) { return tiles; }
    const TileRect chunkBounds{{x, z}, {x + chunk_size, z + chunk_size}};
    for (const auto& building : plan.buildings) {
        if (!Intersects(building.bounds, chunkBounds)) { continue; }
        const auto complete = MaterializeBuilding(content, palette, building);
        const int width = building.bounds.max.x - building.bounds.min.x;
        for (int pz = Max(z, building.bounds.min.z); pz < Min(z + chunk_size, building.bounds.max.z); ++pz) {
            for (int px = Max(x, building.bounds.min.x); px < Min(x + chunk_size, building.bounds.max.x); ++px) {
                const TileCoord p{px, pz, chunk.y};
                if (!Inside(plan.bounds, p) || Demo(p)) { continue; }
                // Driveway geometry includes the entrance; IsRoad excludes it.
                // Street centerlines never enter a building's setback footprint.
                tiles[static_cast<std::size_t>((pz - z) * chunk_size + px - x)] =
                    complete[static_cast<std::size_t>((pz - building.bounds.min.z) * width
                        + px - building.bounds.min.x)];
            }
        }
    }
    return tiles;
}
}
