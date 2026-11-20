#include "worldgen/worldgen.h"
#include "core/ratio.h"
#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <EASTL/utility.h>
#include <cstdlib>

namespace cataclysmx {
namespace {

void Require(bool condition) { if (!condition) { std::abort(); } }
int Min(int a, int b) { return a < b ? a : b; }
int Max(int a, int b) { return a > b ? a : b; }
int FloorDiv(int value, int divisor) {
    return value / divisor - (value % divisor < 0 ? 1 : 0);
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
bool Encloses(TileRect a, TileRect b) {
    return Inside(a, b.min) && Inside(a, {b.max.x - 1, b.max.z - 1});
}
bool Demo(TileCoord p) {
    return p.x >= -32 && p.x <= 32 && p.z >= -32 && p.z <= 32;
}
TileRect RoadBounds(const RoadSegment& road) {
    const int radius = road.width / 2;
    return {{Min(road.first.x, road.last.x) - radius, Min(road.first.z, road.last.z) - radius},
        {Max(road.first.x, road.last.x) + radius + 1, Max(road.first.z, road.last.z) + radius + 1}};
}
bool OnSegment(const RoadSegment& road, TileCoord p) {
    Require((road.width == 1 || road.width == 3)
        && (road.first.x == road.last.x || road.first.z == road.last.z));
    return Inside(RoadBounds(road), p);
}
void AddRoad(Vector<RoadSegment>& roads, TileCoord a, TileCoord b, int width = 3) {
    Require(a.x == b.x || a.z == b.z);
    roads.push_back({a, b, width});
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
void SeedField(std::uint64_t& state, std::uint64_t value) {
    state = Mix(state ^ Mix(value + 0x9e3779b97f4a7c15ULL));
}
void SeedString(std::uint64_t& state, const String& value) {
    SeedField(state, value.size());
    for (unsigned char byte : value) { SeedField(state, byte); }
}
std::uint64_t SettlementSeed(std::uint64_t seed, SettlementId id, std::uint64_t domain) {
    auto state = Hash(seed, id.region.x, id.region.z, domain);
    SeedField(state, world_generation_version);
    SeedField(state, id.ordinal);
    return state;
}
std::uint64_t FeatureSeed(std::uint64_t seed, const FeatureOwner& owner,
    const BuildingPattern& pattern, std::uint64_t domain) {
    auto state = SettlementSeed(seed, {owner.region, owner.settlement}, domain);
    SeedField(state, owner.parcel);
    SeedString(state, pattern.id);
    return state;
}
struct GenerationRandom {
    std::uint64_t state;
    std::uint64_t Next() { state += 0x9e3779b97f4a7c15ULL; return Mix(state); }
    std::uint64_t Bounded(std::uint64_t bound) {
        Require(bound != 0);
        const auto threshold = (std::uint64_t{0} - bound) % bound;
        for (;;) {
            const auto value = Next();
            if (value >= threshold) { return value % bound; }
        }
    }
};
template<class Entries>
std::size_t WeightedIndex(const Entries& entries, GenerationRandom& random) {
    std::uint64_t total = 0;
    for (const auto& entry : entries) {
        Require(entry.weight > 0 && total <= UINT64_MAX - entry.weight);
        total += entry.weight;
    }
    auto choice = random.Bounded(total);
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (choice < entries[i].weight) { return i; }
        choice -= entries[i].weight;
    }
    std::abort();
}
std::uint32_t RoundExpected(std::uint64_t expected, GenerationRandom& random) {
    Require(expected / ratioScale < UINT32_MAX);
    return static_cast<std::uint32_t>(expected / ratioScale
        + (random.Bounded(ratioScale) < expected % ratioScale ? 1 : 0));
}
int Smooth(int value) {
    return static_cast<int>((static_cast<std::int64_t>(value) * value
        * (3072 - 2 * value)) / (1024 * 1024));
}
int Lerp(int a, int b, int fraction) { return (a * (1024 - fraction) + b * fraction) / 1024; }
int Noise(std::uint64_t seed, TileCoord p, int scale, std::uint64_t domain) {
    const int x = FloorDiv(p.x, scale), z = FloorDiv(p.z, scale);
    const int fx = Smooth((p.x - x * scale) * 1024 / scale);
    const int fz = Smooth((p.z - z * scale) * 1024 / scale);
    const int a = static_cast<int>(Hash(seed, x, z, domain) % 1001);
    const int b = static_cast<int>(Hash(seed, x + 1, z, domain) % 1001);
    const int c = static_cast<int>(Hash(seed, x, z + 1, domain) % 1001);
    const int d = static_cast<int>(Hash(seed, x + 1, z + 1, domain) % 1001);
    return Lerp(Lerp(a, b, fx), Lerp(c, d, fx), fz);
}
ContentResult<void> ValidatePalette(const ContentCatalog& content, Diagnostics* diagnostics, bool requireAir) {
    for (const char* name : {"floor", "wall", "grass", "road", "rubble", "pavement", "air", "stairs"}) {
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
            diagnostics->Add({message.data(), message.size()});
        }
        return eastl::unexpected(ContentError::InvalidData);
    }
    return {};
}
struct Palette {
    TerrainId floor, wall, grass, tree, road, pavement;
    TerrainId wornRoad, patchedRoad, brickPavers, palePavers, darkPavers, servicePavers;
};
TerrainId TerrainOr(const ContentCatalog& content, const char* id, TerrainId fallback) {
    const auto found = content.FindTerrain(id);
    return found.value ? found : fallback;
}
Palette GetPalette(const ContentCatalog& content) {
    Require(ValidatePalette(content, nullptr, false).has_value());
    Palette p{};
    p.floor = content.FindTerrain("floor");
    p.wall = content.FindTerrain("wall");
    p.grass = content.FindTerrain("grass");
    p.tree = content.FindTerrain("tree");
    p.road = content.FindTerrain("road");
    p.pavement = content.FindTerrain("pavement");
    if (!p.grass.value) { p.grass = p.floor; }
    if (!p.tree.value) { p.tree = p.wall; }
    if (!p.road.value) { p.road = p.floor; }
    if (!p.pavement.value) { p.pavement = p.floor; }
    p.wornRoad = TerrainOr(content, "road_worn_asphalt", p.road);
    p.patchedRoad = TerrainOr(content, "road_patched_asphalt", p.road);
    p.brickPavers = TerrainOr(content, "pavement_brick", p.pavement);
    p.palePavers = TerrainOr(content, "pavement_pale_pavers", p.pavement);
    p.darkPavers = TerrainOr(content, "pavement_dark_pavers", p.pavement);
    p.servicePavers = TerrainOr(content, "pavement_service_concrete", p.pavement);
    return p;
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
int EntranceSide(const BuildingPattern& pattern) {
    if (pattern.entrance.z == 0) { return 0; }
    if (pattern.entrance.x == pattern.width - 1) { return 1; }
    if (pattern.entrance.z == pattern.height - 1) { return 2; }
    Require(pattern.entrance.x == 0);
    return 3;
}
int Turns(const BuildingPattern& pattern, Frontage frontage) {
    int side = 0;
    switch (frontage) {
    case Frontage::North: side = 0; break;
    case Frontage::East: side = 1; break;
    case Frontage::South: side = 2; break;
    case Frontage::West: side = 3; break;
    }
    return (side - EntranceSide(pattern) + 4) % 4;
}
TileCoord PopulationPosition(const BuildingPattern& pattern, const BuildingPlan& building, TileCoord local) {
    const auto p = Rotate({local.x, local.z}, pattern.width, pattern.height, building.quarterTurns);
    return {building.bounds.min.x + p.x, building.bounds.min.z + p.z, local.y};
}
PatternPoint LocalPosition(const BuildingPlan& building, TileCoord p) {
    const int width = building.bounds.max.x - building.bounds.min.x;
    const int height = building.bounds.max.z - building.bounds.min.z;
    return Rotate({p.x - building.bounds.min.x, p.z - building.bounds.min.z},
        width, height, (4 - building.quarterTurns) % 4);
}
struct LayerView {
    const Vector<TerrainId>* tiles = nullptr;
    const Vector<PopulationSpace>* spaces = nullptr;
};
LayerView LayerAt(const BuildingPattern& pattern, const BuildingPlan& building, int y) {
    if (y == 0) { return {&pattern.tiles, &pattern.spaces}; }
    for (const auto& layer : pattern.layers) {
        if (layer.y == y) { return {&layer.tiles, &layer.spaces}; }
    }
    Require(building.basementVariant <= pattern.basements.size());
    if (building.basementVariant) {
        const auto& layer = pattern.basements[building.basementVariant - 1].layer;
        if (layer.y == y) { return {&layer.tiles, &layer.spaces}; }
    }
    return {};
}
PopulationSpace SpaceAt(LayerView layer, std::size_t index) {
    Require(layer.tiles && index < layer.tiles->size());
    // Empty masks are useful for value-built catalogs and mean the documented default.
    return !layer.spaces || layer.spaces->empty() ? PopulationSpace::Interior : (*layer.spaces)[index];
}
bool LegalTerrain(const ContentCatalog& content, TerrainId id) {
    const auto& terrain = content.GetTerrain(id);
    return terrain.passable && !terrain.openSpace && !terrain.stairs;
}

// Geometric transforms use edge coordinates, unlike the cell-centered pattern rotation.
TileCoord PatchPoint(TileCoord p, int turns, bool reflect) {
    if (reflect) { p.x = planning_patch_size - p.x; }
    for (int i = 0; i < turns; ++i) { p = {planning_patch_size - p.z, p.x}; }
    return p;
}
TileRect PatchRect(TileRect r, int turns, bool reflect) {
    const auto a = PatchPoint(r.min, turns, reflect);
    const auto b = PatchPoint(r.max, turns, reflect);
    return {{Min(a.x, b.x), Min(a.z, b.z)}, {Max(a.x, b.x), Max(a.z, b.z)}};
}
void Offset(TileCoord& p, TileCoord origin) { p.x += origin.x; p.z += origin.z; }

struct FrontageStrip {
    TileRect bounds;
    Frontage frontage;
    int axis = 0;
    int cursor = 0;
    int end = 0;
    BuildingUse district = BuildingUse::Residential;
    BuildingEra era = BuildingEra::Older;
    bool center = false;
    std::uint32_t block = 0;
};
bool Horizontal(const FrontageStrip& strip) {
    return strip.frontage == Frontage::North || strip.frontage == Frontage::South;
}
void Footprint(const BuildingPattern& pattern, const FrontageStrip& strip, int& width, int& depth) {
    const int turns = Turns(pattern, strip.frontage);
    const int w = turns % 2 ? pattern.height : pattern.width;
    const int h = turns % 2 ? pattern.width : pattern.height;
    width = Horizontal(strip) ? w : h;
    depth = Horizontal(strip) ? h : w;
}
bool SameFamily(const BuildingPattern& a, const BuildingPattern& b) {
    return a.family.empty() ? b.family.empty() && a.id == b.id : a.family == b.family;
}
struct Candidates {
    Vector<BuildingPatternId> ordered;
    Vector<BuildingPatternId> fitting;
    Vector<std::size_t> families;
};
int MinimumEraFrontage(const ContentCatalog& content, const Candidates& scratch,
    SettlementScale scale, const FrontageStrip& strip) {
    const int maxDepth = Horizontal(strip) ? strip.bounds.max.z - strip.bounds.min.z
        : strip.bounds.max.x - strip.bounds.min.x;
    int minimum = 0;
    for (const auto id : scratch.ordered) {
        const auto& pattern = content.GetBuildingPattern(id);
        if (pattern.site != BuildingSite::Building || pattern.era != strip.era
            || pattern.minSettlementSize > scale) { continue; }
        int width = 0, depth = 0;
        Footprint(pattern, strip, width, depth);
        if (depth + 3 > maxDepth) { continue; }
        if (minimum == 0 || width + 2 < minimum) { minimum = width + 2; }
    }
    return minimum;
}
Candidates OrderedCandidates(const ContentCatalog& content) {
    Candidates result;
    for (std::size_t i = 1; i <= content.BuildingPatternCount(); ++i) {
        result.ordered.push_back({static_cast<std::uint32_t>(i)});
    }
    eastl::sort(result.ordered.begin(), result.ordered.end(), [&](auto a, auto b) {
        const auto& left = content.GetBuildingPattern(a);
        const auto& right = content.GetBuildingPattern(b);
        if (left.family.empty() != right.family.empty()) { return left.family.empty(); }
        const auto& lk = left.family.empty() ? left.id : left.family;
        const auto& rk = right.family.empty() ? right.id : right.family;
        return lk != rk ? lk < rk : left.id < right.id;
    });
    return result;
}
std::uint64_t HeightPreference(const BuildingPattern& pattern, const FrontageStrip& strip,
    SettlementScale scale) {
    if (pattern.use != BuildingUse::Residential) { return 1; }
    const auto floors = static_cast<std::uint64_t>(pattern.occupiedFloors);
    if (scale == SettlementScale::Small) {
        // Three-storey housing remains possible, not the normal small-town home.
        return floors == 1 ? 6 : floors == 2 ? 4 : 1;
    }
    if (scale == SettlementScale::Medium) { return floors == 1 ? 2 : floors <= 3 ? 4 : 2; }
    // Most large-city housing should exceed small-town height, not only the
    // representative variant whose content ID happens to sort first.
    const auto preference = floors == 1 ? 1 : 2 * floors * floors;
    return strip.center && floors >= 3 ? 2 * preference : preference;
}
std::uint64_t VariantWeight(const BuildingPattern& pattern, const FrontageStrip& strip,
    SettlementScale scale) {
    return static_cast<std::uint64_t>(pattern.weight) * HeightPreference(pattern, strip, scale);
}
std::uint64_t FamilyWeight(const ContentCatalog& content, const Candidates& scratch,
    std::size_t family, const FrontageStrip& strip, SettlementScale scale) {
    const auto first = scratch.families[family];
    const auto last = family + 1 < scratch.families.size() ? scratch.families[family + 1] : scratch.fitting.size();
    const auto& representative = content.GetBuildingPattern(scratch.fitting[first]);
    std::uint64_t authoredTotal = 0;
    std::uint64_t preferredTotal = 0;
    for (auto i = first; i < last; ++i) {
        const auto& pattern = content.GetBuildingPattern(scratch.fitting[i]);
        authoredTotal += pattern.weight;
        preferredTotal += VariantWeight(pattern, strip, scale);
    }
    std::uint64_t weight = representative.family.empty() ? representative.weight : representative.familyWeight;
    if (representative.use == strip.district) { weight *= 6; }
    // A weighted mean keeps family-first selection independent of variant count,
    // while allowing the heights of every fitting member to influence its role.
    Require(authoredTotal > 0 && preferredTotal <= UINT64_MAX / weight);
    return weight * preferredTotal / authoredTotal;
}
BuildingPatternId SelectPattern(const ContentCatalog& content, SettlementScale scale,
    const FrontageStrip& strip, BuildingSite site, int room, Candidates& scratch,
    GenerationRandom& random, bool allowOtherEra = false) {
    scratch.fitting.clear();
    scratch.families.clear();
    const int maxDepth = Horizontal(strip) ? strip.bounds.max.z - strip.bounds.min.z
        : strip.bounds.max.x - strip.bounds.min.x;
    // The caller exhausts desired-era geometry before permitting this fallback.
    // Catalogs lacking an era still remain usable, without changing its bounds.
    for (int pass = 0; pass < (allowOtherEra ? 2 : 1) && scratch.fitting.empty(); ++pass) {
        for (const auto id : scratch.ordered) {
            const auto& pattern = content.GetBuildingPattern(id);
            int width = 0, depth = 0;
            Footprint(pattern, strip, width, depth);
            if (pattern.site != site || pattern.minSettlementSize > scale
                || width + 2 > room || depth + 3 > maxDepth
                || (pass == 0 && pattern.era != strip.era)) { continue; }
            if (scratch.fitting.empty()
                || !SameFamily(pattern, content.GetBuildingPattern(scratch.fitting.back()))) {
                scratch.families.push_back(scratch.fitting.size());
            }
            scratch.fitting.push_back(id);
        }
    }
    if (scratch.fitting.empty()) { return {}; }
    std::uint64_t total = 0;
    for (std::size_t family = 0; family < scratch.families.size(); ++family) {
        total += FamilyWeight(content, scratch, family, strip, scale);
    }
    auto choice = random.Bounded(total);
    std::size_t family = 0;
    for (; family < scratch.families.size(); ++family) {
        const auto weight = FamilyWeight(content, scratch, family, strip, scale);
        if (choice < weight) { break; }
        choice -= weight;
    }
    Require(family < scratch.families.size());
    const auto first = scratch.families[family];
    const auto last = family + 1 < scratch.families.size() ? scratch.families[family + 1] : scratch.fitting.size();
    total = 0;
    for (auto i = first; i < last; ++i) {
        total += VariantWeight(content.GetBuildingPattern(scratch.fitting[i]), strip, scale);
    }
    choice = random.Bounded(total);
    for (auto i = first; i < last; ++i) {
        const auto id = scratch.fitting[i];
        const auto weight = VariantWeight(content.GetBuildingPattern(id), strip, scale);
        if (choice < weight) { return id; }
        choice -= weight;
    }
    std::abort();
}
void CommitBuilding(const ContentCatalog& content, std::uint64_t seed, SettlementPlan& plan,
    FrontageStrip& strip, BuildingPatternId selected, int gap) {
    const auto& pattern = content.GetBuildingPattern(selected);
    int frontage = 0, depth = 0;
    Footprint(pattern, strip, frontage, depth);
    const int maxDepth = Horizontal(strip) ? strip.bounds.max.z - strip.bounds.min.z
        : strip.bounds.max.x - strip.bounds.min.x;
    // Carry the rear garden to the reserved shared lane. Otherwise shallow
    // footprints would leave a tree-filled, undeveloped slit behind their yards.
    const int yard = maxDepth - depth - 1;
    const int lotDepth = depth + 1 + yard;
    const int cursor = strip.cursor;
    TileRect lot;
    switch (strip.frontage) {
    case Frontage::North: lot = {{cursor, strip.axis + 2}, {cursor + frontage + 2, strip.axis + 2 + lotDepth}}; break;
    case Frontage::South: lot = {{cursor, strip.axis - 1 - lotDepth}, {cursor + frontage + 2, strip.axis - 1}}; break;
    case Frontage::West: lot = {{strip.axis + 2, cursor}, {strip.axis + 2 + lotDepth, cursor + frontage + 2}}; break;
    case Frontage::East: lot = {{strip.axis - 1 - lotDepth, cursor}, {strip.axis - 1, cursor + frontage + 2}}; break;
    }
    const bool forward = strip.frontage == Frontage::North || strip.frontage == Frontage::West;
    const int width = Horizontal(strip) ? frontage : depth;
    const int height = Horizontal(strip) ? depth : frontage;
    const int x = Horizontal(strip) ? cursor + 1 : forward ? lot.min.x + 1 : lot.max.x - width - 1;
    const int z = !Horizontal(strip) ? cursor + 1 : forward ? lot.min.z + 1 : lot.max.z - height - 1;
    const TileRect bounds{{x, z}, {x + width, z + height}};
    Require(Encloses(plan.settlement.bounds, lot) && Encloses(strip.bounds, lot) && Encloses(lot, bounds));
    const FeatureOwner owner{plan.settlement.id.region,
        static_cast<std::uint32_t>(plan.parcels.size()), plan.settlement.id.ordinal};
    const int turns = Turns(pattern, strip.frontage);
    const auto entry = Rotate(pattern.entrance, pattern.width, pattern.height, turns);
    const TileCoord entrance{x + entry.x, z + entry.z};
    const auto terminal = Horizontal(strip) ? TileCoord{entrance.x, strip.axis} : TileCoord{strip.axis, entrance.z};
    std::uint32_t basement = 0;
    if (!pattern.basements.empty()) {
        GenerationRandom basementRandom{FeatureSeed(seed, owner, pattern, 0x626173656d656e74ULL)};
        basement = static_cast<std::uint32_t>(WeightedIndex(pattern.basements, basementRandom) + 1);
    }
    plan.parcels.push_back({owner, lot, strip.frontage, Horizontal(strip) ? strip.axis : 0,
        pattern.site, Horizontal(strip) ? 0 : strip.axis, strip.district, pattern.era});
    plan.buildings.push_back({owner, selected, static_cast<std::uint8_t>(turns), bounds, entrance,
        {entrance, terminal, 1}, FeatureSeed(seed, owner, pattern, 0x6162616e646f6eULL), false, basement});
    strip.cursor += frontage + 2 + gap;
}

struct PopulationAnchor {
    TileCoord position;
    MonsterGroupId group;
    std::uint32_t chance = 0;
};
struct PopulationTile {
    TileCoord position;
    std::uint32_t weight = 1;
};
void BlockFurniture(const ContentCatalog& content, const PatternPopulation& population,
    int y, int width, Vector<std::uint8_t>& blocked) {
    for (const auto& marker : population.furniture) {
        if (marker.position.y == y && content.GetObject(marker.type).blocksMovement) {
            blocked[static_cast<std::size_t>(marker.position.z * width + marker.position.x)] = 1;
        }
    }
}
void CollectAnchors(const BuildingPattern& pattern, const BuildingPlan& building,
    const PatternPopulation& population, Vector<PopulationAnchor>& indoor,
    Vector<PopulationAnchor>& outdoor) {
    for (const auto& marker : population.monsters) {
        const int y = marker.position.y;
        if (y >= Max(1, static_cast<int>(pattern.occupiedFloors))) { continue; }
        const auto layer = LayerAt(pattern, building, y);
        if (!layer.tiles) { continue; }
        const auto index = static_cast<std::size_t>(marker.position.z * pattern.width + marker.position.x);
        const auto space = SpaceAt(layer, index);
        const PopulationAnchor anchor{PopulationPosition(pattern, building, marker.position), marker.group, marker.chance};
        if (space == PopulationSpace::Interior) { indoor.push_back(anchor); }
        else if (space == PopulationSpace::Exterior && y == 0) { outdoor.push_back(anchor); }
    }
}
MonsterTypeId GroupMonster(const ContentCatalog& content, MonsterGroupId group, GenerationRandom& random) {
    const auto& entries = content.GetMonsterGroup(group).entries;
    return entries[WeightedIndex(entries, random)].monster;
}
MonsterGroupId CivilianGroup(const ContentCatalog& content, GenerationRandom& random) {
    MonsterGroupId groups[4]{};
    std::size_t count = 0;
    for (const char* id : {"monsters_residential", "monsters_shoppers", "monsters_office", "monsters_school"}) {
        const auto group = content.FindMonsterGroup(id);
        if (group.value) { groups[count++] = group; }
    }
    if (count) { return groups[random.Bounded(count)]; }
    return content.FindMonsterGroup("monsters_outdoors");
}
std::size_t NearestTile(const Vector<PopulationTile>& tiles, TileCoord anchor, GenerationRandom& random) {
    Require(!tiles.empty());
    std::size_t selected = 0;
    std::int64_t best = INT64_MAX;
    // Random start resolves equal distances without biasing the row-major edge.
    const auto start = static_cast<std::size_t>(random.Bounded(tiles.size()));
    for (std::size_t offset = 0; offset < tiles.size(); ++offset) {
        const auto i = (start + offset) % tiles.size();
        const auto p = tiles[i].position;
        const std::int64_t dx = p.x - anchor.x, dz = p.z - anchor.z;
        const std::int64_t distance = dx * dx + dz * dz
            + static_cast<std::int64_t>(p.y - anchor.y) * (p.y - anchor.y) * 1'000'000;
        if (distance < best) { selected = i; best = distance; }
    }
    return selected;
}
std::size_t NearbyOutdoorTile(const Vector<int>& indices, TileRect bounds,
    TileCoord center, GenerationRandom& random) {
    const int width = bounds.max.x - bounds.min.x;
    const int height = bounds.max.z - bounds.min.z;
    const auto find = [&](int x, int z) {
        if (x < bounds.min.x || x >= bounds.max.x || z < bounds.min.z || z >= bounds.max.z) { return -1; }
        return indices[static_cast<std::size_t>((z - bounds.min.z) * width + x - bounds.min.x)];
    };
    const int direct = find(center.x, center.z);
    if (direct >= 0) { return static_cast<std::size_t>(direct); }
    const int turns = static_cast<int>(random.Bounded(4));
    // Search a compact neighborhood, not every eligible tile in a large city
    // for each member of a street group. The raster is updated on swap removal.
    for (int radius = 1; radius <= Max(width, height); ++radius) {
        for (int offset = -radius; offset < radius; ++offset) {
            const PatternPoint ring[4]{{offset, -radius}, {radius, offset},
                {-offset, radius}, {-radius, -offset}};
            for (const auto point : ring) {
                const auto rotated = Rotate({point.x + radius, point.z + radius},
                    2 * radius + 1, 2 * radius + 1, turns);
                const int found = find(center.x + rotated.x - radius, center.z + rotated.z - radius);
                if (found >= 0) { return static_cast<std::size_t>(found); }
            }
        }
    }
    std::abort();
}
void EmitPopulation(const ContentCatalog& content, SettlementPlan& plan, Vector<PopulationTile>& tiles,
    std::uint32_t count, const Vector<PopulationAnchor>& anchors, FeatureOwner owner,
    GenerationPlacementKind kind, GenerationRandom& random, int minGroup, int maxGroup) {
    Require(count <= tiles.size());
    const auto bounds = plan.settlement.bounds;
    const int width = bounds.max.x - bounds.min.x;
    const bool outdoor = kind == GenerationPlacementKind::OutdoorPopulation;
    Vector<int> indices;
    const auto rasterIndex = [&](TileCoord p) {
        return static_cast<std::size_t>((p.z - bounds.min.z) * width + p.x - bounds.min.x);
    };
    if (outdoor && count) {
        indices.assign(static_cast<std::size_t>(width * (bounds.max.z - bounds.min.z)), -1);
        for (std::size_t i = 0; i < tiles.size(); ++i) {
            indices[rasterIndex(tiles[i].position)] = static_cast<int>(i);
        }
    }
    std::uint32_t emitted = 0;
    while (emitted < count) {
        MonsterGroupId group{};
        TileCoord center{};
        if (!anchors.empty() && (kind == GenerationPlacementKind::IndoorPopulation || random.Bounded(4) != 0)) {
            std::uint64_t total = 0;
            for (const auto& anchor : anchors) { total += anchor.chance; }
            if (total) {
                auto choice = random.Bounded(total);
                for (const auto& anchor : anchors) {
                    if (choice < anchor.chance) { group = anchor.group; center = anchor.position; break; }
                    choice -= anchor.chance;
                }
            }
        }
        if (!group.value) {
            group = CivilianGroup(content, random);
            std::uint64_t total = 0;
            for (const auto& tile : tiles) { total += tile.weight; }
            auto choice = random.Bounded(total);
            for (const auto& tile : tiles) {
                if (choice < tile.weight) { center = tile.position; break; }
                choice -= tile.weight;
            }
        }
        // Empty monster catalogs intentionally generate no actors.
        if (!group.value) { return; }
        const auto groupCount = Min(static_cast<int>(count - emitted),
            minGroup + static_cast<int>(random.Bounded(static_cast<std::uint64_t>(maxGroup - minGroup + 1))));
        for (int member = 0; member < groupCount; ++member) {
            const auto index = outdoor ? NearbyOutdoorTile(indices, bounds, center, random)
                : NearestTile(tiles, center, random);
            const auto position = tiles[index].position;
            plan.monsters.push_back({{owner, 0, kind, emitted, 0},
                position, GroupMonster(content, group, random)});
            tiles[index] = tiles.back();
            if (outdoor) {
                indices[rasterIndex(tiles[index].position)] = static_cast<int>(index);
                indices[rasterIndex(position)] = -1;
            }
            tiles.pop_back();
            ++emitted;
        }
    }
}
void PreparePopulation(const ContentCatalog& content, std::uint64_t seed, SettlementPlan& plan) {
    const auto bounds = plan.settlement.bounds;
    const int width = bounds.max.x - bounds.min.x;
    const int height = bounds.max.z - bounds.min.z;
    // One reusable exterior raster: zero undeveloped/blocked, positive density bias.
    Vector<std::uint8_t> exterior(static_cast<std::size_t>(width * height), 0);
    const auto index = [&](TileCoord p) {
        return static_cast<std::size_t>((p.z - bounds.min.z) * width + p.x - bounds.min.x);
    };
    const auto paint = [&](TileRect rectangle, std::uint8_t weight) {
        for (int z = Max(bounds.min.z, rectangle.min.z); z < Min(bounds.max.z, rectangle.max.z); ++z) {
            for (int x = Max(bounds.min.x, rectangle.min.x); x < Min(bounds.max.x, rectangle.max.x); ++x) {
                auto& cell = exterior[index({x, z})];
                cell = eastl::max(cell, weight);
            }
        }
    };
    for (const auto& road : plan.roads) { paint(RoadBounds(road), 2); }
    for (const auto lane : plan.serviceAlleys) { paint(lane, 1); }
    for (const auto& parcel : plan.parcels) {
        paint(parcel.bounds, parcel.district == BuildingUse::Residential ? 1 : 5);
    }
    Vector<PopulationTile> indoorTiles;
    Vector<PopulationAnchor> indoorAnchors, outdoorAnchors;
    Vector<std::uint8_t> blocked;
    const int multiplier = 5 + static_cast<int>(plan.settlement.scale);
    for (const auto& building : plan.buildings) {
        const auto& pattern = content.GetBuildingPattern(building.pattern);
        indoorTiles.clear();
        indoorAnchors.clear();
        CollectAnchors(pattern, building, pattern.population, indoorAnchors, outdoorAnchors);
        if (building.basementVariant) {
            CollectAnchors(pattern, building, pattern.basements[building.basementVariant - 1].population,
                indoorAnchors, outdoorAnchors);
        }
        // Surface classification replaces the yard raster inside every footprint.
        for (int z = building.bounds.min.z; z < building.bounds.max.z; ++z) {
            for (int x = building.bounds.min.x; x < building.bounds.max.x; ++x) {
                exterior[index({x, z})] = 0;
            }
        }
        for (int y = world_min_y; y < Max(1, static_cast<int>(pattern.occupiedFloors)); ++y) {
            const auto layer = LayerAt(pattern, building, y);
            if (!layer.tiles) { continue; }
            blocked.assign(layer.tiles->size(), 0);
            BlockFurniture(content, pattern.population, y, pattern.width, blocked);
            if (building.basementVariant) {
                BlockFurniture(content, pattern.basements[building.basementVariant - 1].population,
                    y, pattern.width, blocked);
            }
            for (int z = 0; z < pattern.height; ++z) {
                for (int x = 0; x < pattern.width; ++x) {
                    const auto i = static_cast<std::size_t>(z * pattern.width + x);
                    if (blocked[i] || !LegalTerrain(content, (*layer.tiles)[i])) { continue; }
                    const auto position = PopulationPosition(pattern, building, {x, z, y});
                    switch (SpaceAt(layer, i)) {
                    case PopulationSpace::Interior: indoorTiles.push_back({position, 1}); break;
                    case PopulationSpace::Exterior:
                        if (y == 0) { exterior[index(position)] = pattern.use == BuildingUse::Residential ? 1 : 6; }
                        break;
                    case PopulationSpace::None: break;
                    }
                }
            }
        }
        std::uint64_t expected = 0;
        for (const auto& anchor : indoorAnchors) { expected += static_cast<std::uint64_t>(anchor.chance) * multiplier; }
        plan.indoorExpected += expected;
        GenerationRandom random{FeatureSeed(seed, building.owner, pattern, 0x696e646f6f727338ULL)};
        const auto count = RoundExpected(expected, random);
        // A catalog with an impossible population budget is an invariant error,
        // not permission to silently drop already budgeted actors.
        Require(count <= indoorTiles.size());
        EmitPopulation(content, plan, indoorTiles, count, indoorAnchors, building.owner,
            GenerationPlacementKind::IndoorPopulation, random, 1, 3);
    }
    Vector<PopulationTile> outdoorTiles;
    for (int z = bounds.min.z; z < bounds.max.z; ++z) {
        for (int x = bounds.min.x; x < bounds.max.x; ++x) {
            const auto weight = exterior[index({x, z})];
            if (weight && !Demo({x, z})) { outdoorTiles.push_back({{x, z}, weight}); }
        }
    }
    plan.eligibleOutdoorTiles = static_cast<std::uint32_t>(outdoorTiles.size());
    const std::uint64_t density = plan.settlement.scale == SettlementScale::Small ? 50
        : plan.settlement.scale == SettlementScale::Medium ? 150 : 300;
    GenerationRandom random{SettlementSeed(seed, plan.settlement.id, 0x6f7574646f6f7238ULL)};
    const auto count = RoundExpected(static_cast<std::uint64_t>(outdoorTiles.size()) * density, random);
    const int minGroup = plan.settlement.scale == SettlementScale::Small ? 1
        : plan.settlement.scale == SettlementScale::Medium ? 2 : 4;
    const int maxGroup = plan.settlement.scale == SettlementScale::Small ? 3
        : plan.settlement.scale == SettlementScale::Medium ? 5 : 10;
    EmitPopulation(content, plan, outdoorTiles, count, outdoorAnchors,
        {plan.settlement.id.region, UINT32_MAX, plan.settlement.id.ordinal},
        GenerationPlacementKind::OutdoorPopulation, random, minGroup, maxGroup);
}

GenerationRandom MarkerRandom(const RegionPlan& plan, const BuildingPlan& building,
    const BuildingPattern& pattern, const String& variant, GenerationPlacementKind kind, std::uint32_t placement) {
    auto state = FeatureSeed(plan.seed, building.owner, pattern, 0x706f70756c617465ULL);
    SeedString(state, variant);
    SeedField(state, static_cast<std::uint32_t>(kind));
    SeedField(state, placement);
    return {state};
}
bool Chance(std::uint32_t chance, GenerationRandom& random) {
    Require(chance <= ratioScale);
    return random.Bounded(ratioScale) < chance;
}
void ExpandLoot(const ContentCatalog& content, LootPoolId pool, GenerationRandom& random,
    GenerationKey& key, TileCoord position, LootDestination destination,
    std::uint32_t depth, std::uint32_t& work, Vector<GeneratedItem>& output) {
    Require(depth <= maxLootDepth && ++work <= maxLootExpansionWork);
    const auto& entries = content.GetLootPool(pool).entries;
    const auto& entry = entries[WeightedIndex(entries, random)];
    Require(entry.count.min >= 1 && entry.count.min <= entry.count.max && entry.count.max <= maxSpawnCount);
    const auto count = entry.count.min + random.Bounded(entry.count.max - entry.count.min + 1);
    for (std::uint32_t i = 0; i < count; ++i) {
        switch (entry.kind) {
        case LootEntryKind::Item:
            Require(key.ordinal < maxLootItems);
            output.push_back({key, position, entry.item, destination});
            ++key.ordinal;
            break;
        case LootEntryKind::Pool:
            ExpandLoot(content, entry.pool, random, key, position, destination, depth + 1, work, output);
            break;
        default: std::abort();
        }
    }
}
void Populate(const ContentCatalog& content, const RegionPlan& plan, const BuildingPlan& building,
    const BuildingPattern& pattern, const PatternPopulation& population, std::uint32_t variantIndex,
    const String& variant, ChunkCoord chunk, ChunkPlacements& output, PopulationMode mode) {
    for (std::size_t i = 0; i < population.furniture.size(); ++i) {
        const auto& marker = population.furniture[i];
        const auto position = PopulationPosition(pattern, building, marker.position);
        if (ChunkOf(position) != chunk) { continue; }
        Require(marker.facing <= ObjectFacing::West);
        const auto facing = static_cast<ObjectFacing>((static_cast<unsigned>(marker.facing) + building.quarterTurns) % 4);
        output.furniture.push_back({{building.owner, variantIndex, GenerationPlacementKind::Furniture,
            static_cast<std::uint32_t>(i), 0}, position, marker.type, facing});
    }
    for (std::size_t i = 0; i < population.loot.size(); ++i) {
        const auto& marker = population.loot[i];
        const auto position = PopulationPosition(pattern, building, marker.position);
        if (ChunkOf(position) != chunk) { continue; }
        auto random = MarkerRandom(plan, building, pattern, variant, GenerationPlacementKind::Loot, static_cast<std::uint32_t>(i));
        Require(marker.rolls <= maxSpawnRolls);
        if (!Chance(marker.chance, random)) { continue; }
        GenerationKey key{building.owner, variantIndex, GenerationPlacementKind::Loot, static_cast<std::uint32_t>(i), 0};
        std::uint32_t work = 0;
        for (std::uint32_t roll = 0; roll < marker.rolls; ++roll) {
            ExpandLoot(content, marker.pool, random, key, position, marker.destination, 1, work, output.items);
        }
    }
    if (mode != PopulationMode::Authored) { return; }
    for (std::size_t i = 0; i < population.monsters.size(); ++i) {
        const auto& marker = population.monsters[i];
        const auto position = PopulationPosition(pattern, building, marker.position);
        if (ChunkOf(position) != chunk) { continue; }
        auto random = MarkerRandom(plan, building, pattern, variant, GenerationPlacementKind::Monster, static_cast<std::uint32_t>(i));
        if (!Chance(marker.chance, random)) { continue; }
        output.monsters.push_back({{building.owner, variantIndex, GenerationPlacementKind::Monster,
            static_cast<std::uint32_t>(i), 0}, position, GroupMonster(content, marker.group, random)});
    }
}
void CheckChunk(const RegionPlan& plan, ChunkCoord chunk) {
    constexpr int limit = world_coordinate_limit / chunk_size;
    Require(chunk.x >= -limit && chunk.x <= limit && chunk.z >= -limit && chunk.z <= limit
        && chunk.y >= world_min_y && chunk.y <= world_max_y);
    CheckRegion(plan.region);
    Require((RegionCoord{FloorDiv(chunk.x * chunk_size, region_size), FloorDiv(chunk.z * chunk_size, region_size)}) == plan.region);
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

PlanningPatch GeneratePlanningPatch(std::uint64_t seed, RegionCoord region) {
    CheckRegion(region);
    PlanningPatch patch;
    patch.origin = {FloorDiv(region.x, planning_patch_regions) * planning_patch_regions,
        FloorDiv(region.z, planning_patch_regions) * planning_patch_regions};
    GenerationRandom random{Hash(seed, patch.origin.x, patch.origin.z, 0x7061746368303038ULL)};
    const int turns = static_cast<int>(random.Bounded(4));
    const bool reflect = random.Bounded(2) != 0;
    struct Row { int kind; int height; };
    Row rows[3]{{0, 0}, {1, 0}, {2, 0}};
    for (int i = 2; i > 0; --i) { eastl::swap(rows[i], rows[random.Bounded(i + 1)]); }
    // IDs are assigned by canonical scale/slot, before row and patch transforms.
    SettlementDescriptor descriptors[10]{};
    for (std::uint32_t i = 0; i < 10; ++i) {
        auto& descriptor = descriptors[i];
        descriptor.id = {patch.origin, i};
        descriptor.scale = i == 0 ? SettlementScale::Large : i < 4 ? SettlementScale::Medium : SettlementScale::Small;
        const int low = i == 0 ? 384 : i < 4 ? 192 : 96;
        const int range = i == 0 ? 129 : i < 4 ? 65 : 49;
        const int width = low + static_cast<int>(random.Bounded(range));
        const int height = low + static_cast<int>(random.Bounded(range));
        descriptor.bounds = {{0, 0}, {width, height}};
        const auto era = random.Bounded(100);
        descriptor.era = era < 25 ? SettlementEra::Older : era < 70 ? SettlementEra::Mixed : SettlementEra::Newer;
    }
    for (auto& row : rows) {
        const int first = row.kind == 0 ? 0 : row.kind == 1 ? 1 : 7;
        const int count = row.kind == 0 ? 4 : 3;
        for (int i = 0; i < count; ++i) {
            const int id = row.kind == 0 ? (i == 0 ? 0 : i + 3) : first + i;
            row.height = Max(row.height, descriptors[id].bounds.max.z);
        }
    }
    const int totalHeight = rows[0].height + rows[1].height + rows[2].height;
    const int rowGap = Min(256, (planning_patch_size - 128 - totalHeight) / 2);
    Require(rowGap >= 128);
    int top = (planning_patch_size - totalHeight - 2 * rowGap) / 2;
    // A transform-invariant perimeter backbone and four canonical mid-edge portals.
    constexpr int low = 32, high = planning_patch_size - 32, middle = planning_patch_size / 2;
    AddRoad(patch.roads, {low, low}, {high, low});
    AddRoad(patch.roads, {high, low}, {high, high});
    AddRoad(patch.roads, {high, high}, {low, high});
    AddRoad(patch.roads, {low, high}, {low, low});
    AddRoad(patch.roads, {0, middle}, {low, middle});
    AddRoad(patch.roads, {high, middle}, {planning_patch_size, middle});
    AddRoad(patch.roads, {middle, 0}, {middle, low});
    AddRoad(patch.roads, {middle, high}, {middle, planning_patch_size});
    for (const auto row : rows) {
        int ids[4]{};
        const int count = row.kind == 0 ? 4 : 3;
        for (int i = 0; i < count; ++i) {
            ids[i] = row.kind == 0 ? (i == 0 ? 0 : i + 3) : (row.kind == 1 ? 1 : 7) + i;
        }
        for (int i = count - 1; i > 0; --i) { eastl::swap(ids[i], ids[random.Bounded(i + 1)]); }
        int used = 0;
        for (int i = 0; i < count; ++i) { used += descriptors[ids[i]].bounds.max.x; }
        const int gap = Min(320, (planning_patch_size - 128 - used) / (count - 1));
        Require(gap >= 128);
        int left = (planning_patch_size - used - (count - 1) * gap) / 2;
        const int north = top - 24, south = top + row.height + 24;
        AddRoad(patch.roads, {low, north}, {high, north});
        AddRoad(patch.roads, {low, south}, {high, south});
        for (int i = 0; i < count; ++i) {
            auto& descriptor = descriptors[ids[i]];
            const auto size = descriptor.bounds.max;
            const int z = top + static_cast<int>(random.Bounded(row.height - size.z + 1));
            descriptor.bounds = {{left, z}, {left + size.x, z + size.z}};
            descriptor.center = {left + size.x / 2, z + size.z / 2};
            // These end at the settlement boundary; the detail plan continues
            // the same north/south through-street all the way across the city.
            AddRoad(patch.roads, {descriptor.center.x, north}, {descriptor.center.x, descriptor.bounds.min.z});
            AddRoad(patch.roads, {descriptor.center.x, descriptor.bounds.max.z}, {descriptor.center.x, south});
            left += size.x + gap;
        }
        top += row.height + rowGap;
    }
    const TileCoord origin{patch.origin.x * region_size, patch.origin.z * region_size};
    const TileRect world{{-world_coordinate_limit, -world_coordinate_limit},
        {world_coordinate_limit + 1, world_coordinate_limit + 1}};
    for (auto descriptor : descriptors) {
        descriptor.bounds = PatchRect(descriptor.bounds, turns, reflect);
        Offset(descriptor.bounds.min, origin);
        Offset(descriptor.bounds.max, origin);
        // Transform the center with the approach endpoints, retaining their
        // exact shared axis even when an odd-sized rectangle is reflected.
        descriptor.center = PatchPoint(descriptor.center, turns, reflect);
        Offset(descriptor.center, origin);
        if (!Encloses(world, descriptor.bounds) || Intersects(descriptor.bounds, {{-32, -32}, {33, 33}})) { continue; }
        patch.settlements.push_back(descriptor);
    }
    for (auto& road : patch.roads) {
        road.first = PatchPoint(road.first, turns, reflect);
        road.last = PatchPoint(road.last, turns, reflect);
        Offset(road.first, origin);
        Offset(road.last, origin);
    }
    return patch;
}

SettlementPlan GenerateSettlementPlan(const ContentCatalog& content, std::uint64_t seed,
    const SettlementDescriptor& settlement) {
    SettlementPlan plan;
    plan.settlement = settlement;
    const auto bounds = settlement.bounds;
    Require(bounds.min.x >= -world_coordinate_limit && bounds.min.z >= -world_coordinate_limit
        && bounds.max.x <= world_coordinate_limit + 1 && bounds.max.z <= world_coordinate_limit + 1
        && bounds.max.x > bounds.min.x && bounds.max.z > bounds.min.z
        && bounds.max.x - bounds.min.x <= 512 && bounds.max.z - bounds.min.z <= 512
        && Inside(bounds, settlement.center)
        && settlement.id.ordinal < 10
        && settlement.id.region.x % planning_patch_regions == 0
        && settlement.id.region.z % planning_patch_regions == 0
        && settlement.scale <= SettlementScale::Large);
    GenerationRandom random{SettlementSeed(seed, settlement.id, 0x626c6f636b733038ULL)};
    const int nx = settlement.scale == SettlementScale::Small ? 2 : settlement.scale == SettlementScale::Medium ? 4 : 6;
    const int nz = settlement.scale == SettlementScale::Large ? 4 : 2;
    const int width = bounds.max.x - bounds.min.x, height = bounds.max.z - bounds.min.z;
    // North/south and east/west through streets are explicit even when the
    // shuffled patch attaches from the other orientation.
    AddRoad(plan.roads, {settlement.center.x, bounds.min.z + 1}, {settlement.center.x, bounds.max.z - 2});
    AddRoad(plan.roads, {bounds.min.x + 1, settlement.center.z}, {bounds.max.x - 2, settlement.center.z});
    Vector<int> xs, zs;
    for (int i = 0; i <= nx; ++i) { xs.push_back(bounds.min.x + 2 + (width - 5) * i / nx); }
    for (int i = 0; i <= nz; ++i) { zs.push_back(bounds.min.z + 2 + (height - 5) * i / nz); }
    // Even block counts put both through routes on reserved block boundaries.
    // Keeping the descriptor center verbatim also preserves reflected approaches.
    xs[static_cast<std::size_t>(nx / 2)] = settlement.center.x;
    zs[static_cast<std::size_t>(nz / 2)] = settlement.center.z;
    bool cutCorners[4]{};
    if (settlement.scale == SettlementScale::Large
        || (settlement.scale == SettlementScale::Medium && random.Bounded(3) == 0)) {
        cutCorners[random.Bounded(4)] = true;
    }
    const auto developed = [&](int bx, int bz) {
        if (bx < 0 || bx >= nx || bz < 0 || bz >= nz) { return false; }
        if (bx == 0 && bz == 0 && cutCorners[0]) { return false; }
        if (bx == nx - 1 && bz == 0 && cutCorners[1]) { return false; }
        if (bx == nx - 1 && bz == nz - 1 && cutCorners[2]) { return false; }
        if (bx == 0 && bz == nz - 1 && cutCorners[3]) { return false; }
        return true;
    };
    // Clip selected corner blocks out of the developed edge. Draw only block
    // boundary segments actually needed by a retained neighbor; do not leave a
    // rectangular ring road enclosing the undeveloped corner.
    for (int bx = 0; bx <= nx; ++bx) {
        for (int bz = 0; bz < nz; ++bz) {
            if (developed(bx - 1, bz) || developed(bx, bz)) {
                AddRoad(plan.roads, {xs[bx], zs[bz]}, {xs[bx], zs[bz + 1]});
            }
        }
    }
    for (int bz = 0; bz <= nz; ++bz) {
        for (int bx = 0; bx < nx; ++bx) {
            if (developed(bx, bz - 1) || developed(bx, bz)) {
                AddRoad(plan.roads, {xs[bx], zs[bz]}, {xs[bx + 1], zs[bz]});
            }
        }
    }
    auto scratch = OrderedCandidates(content);
    // Leave count headroom for real wide civic/industrial footprints, rather
    // than making every nominal slot a compulsory small house.
    const int base = settlement.scale == SettlementScale::Small ? 14 : settlement.scale == SettlementScale::Medium ? 48 : 160;
    const int spread = settlement.scale == SettlementScale::Small ? 5 : settlement.scale == SettlementScale::Medium ? 17 : 41;
    const int target = base + static_cast<int>(random.Bounded(spread));
    int blockCount = 0;
    for (int bz = 0; bz < nz; ++bz) {
        for (int bx = 0; bx < nx; ++bx) { blockCount += developed(bx, bz) ? 1 : 0; }
    }
    int stripOrdinal = 0;
    for (std::size_t bz = 0; bz + 1 < zs.size(); ++bz) {
        for (std::size_t bx = 0; bx + 1 < xs.size(); ++bx) {
            if (!developed(static_cast<int>(bx), static_cast<int>(bz))) { continue; }
            const int block = static_cast<int>(bz * (xs.size() - 1) + bx);
            GenerationRandom blockRandom{SettlementSeed(seed, settlement.id, 0x6469737472696374ULL)};
            SeedField(blockRandom.state, static_cast<std::uint64_t>(block));
            const int newer = settlement.era == SettlementEra::Older ? 20 : settlement.era == SettlementEra::Mixed ? 55 : 85;
            const auto era = blockRandom.Bounded(100) < static_cast<unsigned>(newer) ? BuildingEra::Newer : BuildingEra::Older;
            const bool center = bx > 0 && bx + 2 < xs.size()
                && (nz == 2 || (bz > 0 && bz + 2 < zs.size()));
            const auto district = bx + 2 == xs.size() ? BuildingUse::Industrial
                : center ? (bz == zs.size() / 2 ? BuildingUse::Civic : BuildingUse::Commercial)
                : BuildingUse::Residential;
            const bool horizontal = blockRandom.Bounded(2) == 0;
            const int left = xs[bx] + 2, right = xs[bx + 1] - 1;
            const int top = zs[bz] + 2, bottom = zs[bz + 1] - 1;
            if (right - left < 8 || bottom - top < 8) { stripOrdinal += 2; continue; }
            const int middle = horizontal ? (top + bottom) / 2 : (left + right) / 2;
            // One-tile rear lane plus its street-connected end, outside every lot.
            const TileRect lane = horizontal ? TileRect{{left, middle}, {right, middle + 1}}
                : TileRect{{middle, top}, {middle + 1, bottom}};
            plan.serviceAlleys.push_back(lane);
            plan.serviceAlleys.push_back(horizontal ? TileRect{{left - 2, middle}, {left, middle + 1}}
                : TileRect{{middle, top - 2}, {middle + 1, top}});
            for (int side = 0; side < 2; ++side, ++stripOrdinal) {
                FrontageStrip strip;
                strip.frontage = horizontal ? (side == 0 ? Frontage::North : Frontage::South)
                    : (side == 0 ? Frontage::West : Frontage::East);
                strip.axis = horizontal ? (side == 0 ? zs[bz] : zs[bz + 1])
                    : (side == 0 ? xs[bx] : xs[bx + 1]);
                strip.bounds = horizontal
                    ? TileRect{{left, side == 0 ? top : middle + 1}, {right, side == 0 ? middle : bottom}}
                    : TileRect{{side == 0 ? left : middle + 1, top}, {side == 0 ? middle : right, bottom}};
                strip.cursor = horizontal ? left : top;
                strip.end = horizontal ? right : bottom;
                strip.district = district;
                strip.era = era;
                strip.center = center;
                strip.block = static_cast<std::uint32_t>(block);
                const int strips = blockCount * 2;
                const int quota = target / strips + (stripOrdinal < target % strips ? 1 : 0);
                const int minimumEraFrontage = MinimumEraFrontage(content, scratch, settlement.scale, strip);
                for (int slot = 0; slot < quota && strip.cursor < strip.end; ++slot) {
                    const int remaining = strip.end - strip.cursor;
                    const int perSite = remaining / (quota - slot);
                    const int reserved = minimumEraFrontage * (quota - slot - 1);
                    // Borrow unused frontage from smaller later sites before
                    // declaring a wider desired-era family ineligible.
                    const int desiredRoom = minimumEraFrontage
                        ? Max(perSite, remaining - reserved) : perSite;
                    BuildingSite site = BuildingSite::Building;
                    if ((center || settlement.scale == SettlementScale::Small)
                        && slot == 0 && blockRandom.Bounded(100) < 28) {
                        site = static_cast<BuildingSite>(1 + blockRandom.Bounded(3));
                    }
                    const bool broad = site != BuildingSite::Building
                        || ((center || district == BuildingUse::Industrial) && blockRandom.Bounded(4) == 0);
                    auto selected = SelectPattern(content, settlement.scale, strip, site,
                        broad ? remaining : desiredRoom, scratch, blockRandom);
                    if (!selected.value && !broad) {
                        selected = SelectPattern(content, settlement.scale, strip, site, remaining, scratch, blockRandom);
                    }
                    if (!selected.value && site != BuildingSite::Building) {
                        selected = SelectPattern(content, settlement.scale, strip, BuildingSite::Building,
                            desiredRoom, scratch, blockRandom);
                    }
                    if (!selected.value) {
                        selected = SelectPattern(content, settlement.scale, strip, BuildingSite::Building, remaining, scratch, blockRandom);
                    }
                    // Only now may an otherwise unfillable interval retrofit a
                    // different era. Never choose an old compact site merely
                    // because the artificial per-slot division excluded newer
                    // construction that fits the actual remaining frontage.
                    if (!selected.value && site != BuildingSite::Building) {
                        selected = SelectPattern(content, settlement.scale, strip, site,
                            broad ? remaining : perSite, scratch, blockRandom, true);
                    }
                    if (!selected.value) {
                        selected = SelectPattern(content, settlement.scale, strip, BuildingSite::Building,
                            perSite, scratch, blockRandom, true);
                    }
                    if (!selected.value) {
                        selected = SelectPattern(content, settlement.scale, strip, BuildingSite::Building,
                            remaining, scratch, blockRandom, true);
                    }
                    if (!selected.value) { break; }
                    int frontage = 0, depth = 0;
                    Footprint(content.GetBuildingPattern(selected), strip, frontage, depth);
                    const int gap = Min(Max(0, perSite - frontage - 2),
                        Max(0, remaining - frontage - 2 - reserved));
                    CommitBuilding(content, seed, plan, strip, selected, gap);
                }
            }
        }
    }
    PreparePopulation(content, seed, plan);
    return plan;
}

RegionPlan GenerateRegionPlan(const ContentCatalog& content, std::uint64_t seed, RegionCoord region) {
    return GenerateRegionPlan(content, seed, region, world_generation_version);
}
RegionPlan GenerateRegionPlan(const ContentCatalog& content, std::uint64_t seed, RegionCoord region,
    std::uint32_t generationVersion) {
    Require(generationVersion == world_generation_version);
    CheckRegion(region);
    (void)GetPalette(content);
    RegionPlan plan;
    plan.region = region;
    plan.seed = seed;
    plan.generationVersion = generationVersion;
    const int x = region.x * region_size, z = region.z * region_size;
    plan.bounds = {{Max(x, -world_coordinate_limit), Max(z, -world_coordinate_limit)},
        {Min(x + region_size, world_coordinate_limit + 1), Min(z + region_size, world_coordinate_limit + 1)}};
    const auto patch = GeneratePlanningPatch(seed, region);
    for (const auto& road : patch.roads) {
        if (Intersects(plan.bounds, RoadBounds(road))) { plan.roads.push_back(road); }
    }
    for (const auto& descriptor : patch.settlements) {
        if (!Intersects(plan.bounds, descriptor.bounds)) { continue; }
        plan.settlements.push_back(descriptor);
        const auto settlement = GenerateSettlementPlan(content, seed, descriptor);
        if (!plan.hasTown) {
            plan.hasTown = true;
            plan.townBounds = descriptor.bounds;
            plan.townCenter = descriptor.center;
            plan.townProfile = TownProfile::OffsetNeighborhood;
        }
        for (const auto& road : settlement.roads) {
            if (Intersects(plan.bounds, RoadBounds(road))) { plan.roads.push_back(road); }
        }
        for (const auto& parcel : settlement.parcels) {
            if (Intersects(plan.bounds, parcel.bounds)) { plan.parcels.push_back(parcel); }
        }
        for (const auto& building : settlement.buildings) {
            // Retain full footprints: power, rotations and population ownership
            // must be identical on both sides of a region boundary.
            if (Intersects(plan.bounds, building.bounds) || Intersects(plan.bounds, RoadBounds(building.driveway))) {
                plan.buildings.push_back(building);
            }
        }
        for (const auto lane : settlement.serviceAlleys) {
            if (Intersects(plan.bounds, lane)) { plan.serviceAlleys.push_back(lane); }
        }
        for (const auto& monster : settlement.monsters) {
            if (Inside(plan.bounds, monster.position)) { plan.urbanMonsters.push_back(monster); }
        }
    }
    // Boundary diagnostics are derived from actual crossings, never independent
    // road generation. Empty borders retain a zero approachLength sentinel.
    plan.boundaries = {{{{BorderAxis::Z, z, region.x}, {x, z}, 3, 0},
        {{BorderAxis::X, x + region_size, region.z}, {x + region_size, z}, 3, 0},
        {{BorderAxis::Z, z + region_size, region.x}, {x, z + region_size}, 3, 0},
        {{BorderAxis::X, x, region.z}, {x, z}, 3, 0}}};
    for (auto& boundary : plan.boundaries) {
        for (const auto& road : plan.roads) {
            const bool horizontal = road.first.z == road.last.z;
            if (horizontal != (boundary.key.axis == BorderAxis::X)) { continue; }
            const int first = horizontal ? Min(road.first.x, road.last.x) : Min(road.first.z, road.last.z);
            const int last = horizontal ? Max(road.first.x, road.last.x) : Max(road.first.z, road.last.z);
            const int along = horizontal ? road.first.z : road.first.x;
            const int alongMin = horizontal ? z : x;
            if (first <= boundary.key.border && last >= boundary.key.border
                && along >= alongMin && along < alongMin + region_size) {
                boundary.position = horizontal ? TileCoord{boundary.key.border, along} : TileCoord{along, boundary.key.border};
                boundary.approachLength = 1;
                break;
            }
        }
    }
    return plan;
}

bool Contains(TileRect bounds, TileCoord position) {
    CheckPosition(position);
    constexpr int geometryLimit = world_coordinate_limit + planning_patch_size;
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
    CheckChunk(plan, chunk);
    Require(plan.seed == seed && plan.generationVersion == world_generation_version);
    const int x = chunk.x * chunk_size, z = chunk.z * chunk_size;
    const auto palette = GetPalette(content);
    const auto air = content.FindTerrain("air");
    if (chunk.y > 0) { Require(content.TryGetTerrain(air) && content.GetTerrain(air).openSpace); }
    eastl::array<Tile, chunk_size * chunk_size> tiles{};
    for (int dz = 0; dz < chunk_size; ++dz) {
        for (int dx = 0; dx < chunk_size; ++dx) {
            const TileCoord p{x + dx, z + dz, chunk.y};
            auto& tile = tiles[static_cast<std::size_t>(dz * chunk_size + dx)];
            tile = {palette.wall, 0};
            if (!Inside(plan.bounds, p)) { continue; }
            if (chunk.y < 0) { continue; }
            if (chunk.y > 0) { tile.terrain = air; continue; }
            if (Demo(p)) { tile.terrain = palette.floor; continue; }
            const auto environment = SampleEnvironment(seed, p);
            const int threshold = environment.woodland < 400 ? 15 : (environment.woodland - 350) / 2;
            tile.terrain = Hash(seed, p.x, p.z, 25) % 1000 < static_cast<std::uint64_t>(threshold) ? palette.tree : palette.grass;
            const SettlementDescriptor* settlement = nullptr;
            for (const auto& descriptor : plan.settlements) {
                if (Inside(descriptor.bounds, p)) { settlement = &descriptor; break; }
            }
            TerrainId street = palette.road;
            TerrainId paving = palette.pavement;
            if (settlement) {
                street = settlement->era == SettlementEra::Older ? palette.wornRoad : palette.patchedRoad;
                paving = settlement->era == SettlementEra::Older ? palette.brickPavers
                    : settlement->era == SettlementEra::Newer ? palette.palePavers : palette.darkPavers;
            }
            if (const auto* parcel = ParcelAt(plan, p)) {
                tile.terrain = palette.grass;
                const auto b = parcel->bounds;
                bool front = false;
                switch (parcel->frontage) {
                case Frontage::North: front = p.z == b.min.z; break;
                case Frontage::South: front = p.z == b.max.z - 1; break;
                case Frontage::East: front = p.x == b.max.x - 1; break;
                case Frontage::West: front = p.x == b.min.x; break;
                }
                if (front) {
                    tile.terrain = parcel->district == BuildingUse::Industrial ? palette.servicePavers
                        : parcel->era == BuildingEra::Older ? palette.brickPavers : palette.palePavers;
                }
            }
            for (const auto lane : plan.serviceAlleys) {
                if (Inside(lane, p)) { tile.terrain = paving; break; }
            }
            if (IsRoad(plan, p)) { tile.terrain = street; }
        }
    }
    const TileRect chunkBounds{{x, z}, {x + chunk_size, z + chunk_size}};
    for (const auto& building : plan.buildings) {
        if (!Intersects(building.bounds, chunkBounds)) { continue; }
        const auto& pattern = content.GetBuildingPattern(building.pattern);
        auto layer = LayerAt(pattern, building, chunk.y);
        const bool roof = !layer.tiles && chunk.y == pattern.occupiedFloors
            && pattern.layers.empty() && pattern.site != BuildingSite::Park && pattern.site != BuildingSite::Square;
        if (roof) { layer = {&pattern.tiles, &pattern.spaces}; }
        if (!layer.tiles) { continue; }
        for (int pz = Max(z, building.bounds.min.z); pz < Min(z + chunk_size, building.bounds.max.z); ++pz) {
            for (int px = Max(x, building.bounds.min.x); px < Min(x + chunk_size, building.bounds.max.x); ++px) {
                const TileCoord p{px, pz, chunk.y};
                if (!Inside(plan.bounds, p) || (chunk.y == 0 && Demo(p))) { continue; }
                const auto local = LocalPosition(building, p);
                const auto i = static_cast<std::size_t>(local.z * pattern.width + local.x);
                auto terrain = (*layer.tiles)[i];
                if (roof) {
                    terrain = SpaceAt(layer, i) == PopulationSpace::Interior && !content.GetTerrain(terrain).openSpace
                        ? palette.floor : air;
                }
                tiles[static_cast<std::size_t>((pz - z) * chunk_size + px - x)] = {terrain, 0};
            }
        }
    }
    return tiles;
}
eastl::optional<GeneratedBuildingPower> DescribeBuildingPower(const BuildingPattern& pattern,
    TileRect footprint, std::uint32_t basementVariant) {
    Require(footprint.min.x < footprint.max.x && footprint.min.z < footprint.max.z);
    Require(basementVariant <= pattern.basements.size());
    if (pattern.site == BuildingSite::Park || pattern.site == BuildingSite::Square) { return eastl::nullopt; }
    int minY = 0, maxY = 0;
    for (const auto& layer : pattern.layers) { minY = Min(minY, layer.y); maxY = Max(maxY, layer.y); }
    if (basementVariant) {
        const int y = pattern.basements[basementVariant - 1].layer.y;
        minY = Min(minY, y);
        maxY = Max(maxY, y);
    }
    GeneratedBuildingPower result;
    result.origin = {footprint.min.x, footprint.min.z, 0};
    result.bounds = {{footprint.min.x, footprint.min.z, minY}, {footprint.max.x - 1, footprint.max.z - 1, maxY}};
    Require(ValidPowerBounds(result.bounds));
    return result;
}
void GenerateBuildingPower(const ContentCatalog& content, const RegionPlan& plan,
    ChunkCoord chunk, Vector<GeneratedBuildingPower>& output) {
    output.clear();
    CheckChunk(plan, chunk);
    const TileRect chunkBounds{{chunk.x * chunk_size, chunk.z * chunk_size},
        {(chunk.x + 1) * chunk_size, (chunk.z + 1) * chunk_size}};
    for (const auto& building : plan.buildings) {
        if (!Intersects(building.bounds, chunkBounds)) { continue; }
        const auto descriptor = DescribeBuildingPower(content.GetBuildingPattern(building.pattern), building.bounds, building.basementVariant);
        if (descriptor && chunk.y >= descriptor->bounds.min.y && chunk.y <= descriptor->bounds.max.y) { output.push_back(*descriptor); }
    }
}
void GenerateChunkPlacements(const ContentCatalog& content, std::uint64_t seed,
    const RegionPlan& plan, ChunkCoord chunk, ChunkPlacements& output, PopulationMode population) {
    output.furniture.clear();
    output.items.clear();
    output.monsters.clear();
    CheckChunk(plan, chunk);
    Require(plan.seed == seed && plan.generationVersion == world_generation_version);
    const TileRect chunkBounds{{chunk.x * chunk_size, chunk.z * chunk_size},
        {(chunk.x + 1) * chunk_size, (chunk.z + 1) * chunk_size}};
    const String commonVariant;
    for (const auto& building : plan.buildings) {
        if (!Intersects(building.bounds, chunkBounds)) { continue; }
        const auto& pattern = content.GetBuildingPattern(building.pattern);
        Populate(content, plan, building, pattern, pattern.population, 0, commonVariant, chunk, output, population);
        Require(building.basementVariant <= pattern.basements.size());
        if (building.basementVariant) {
            const auto& basement = pattern.basements[building.basementVariant - 1];
            Populate(content, plan, building, pattern, basement.population,
                building.basementVariant, basement.id, chunk, output, population);
        }
    }
    if (population == PopulationMode::Urban) {
        for (const auto& monster : plan.urbanMonsters) {
            if (ChunkOf(monster.position) == chunk) { output.monsters.push_back(monster); }
        }
    }
}
}
