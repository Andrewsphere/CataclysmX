#include "worldgen/worldgen.h"
#include "core/ratio.h"
#include <EASTL/algorithm.h>
#include <EASTL/sort.h>

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
    TerrainId pavement;
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
    p.pavement = content.FindTerrain("pavement");
    if (!p.grass.value) { p.grass = p.floor; }
    if (!p.tree.value) { p.tree = p.wall; }
    if (!p.road.value) { p.road = p.floor; }
    if (!p.rubble.value) { p.rubble = p.floor; }
    if (!p.pavement.value) { p.pavement = p.floor; }
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
    Require(plan.roads.PushBack({a, b, 3}));
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
    int side = 0;
    switch (frontage) {
    case Frontage::North: side = 0; break;
    case Frontage::East: side = 1; break;
    case Frontage::South: side = 2; break;
    case Frontage::West: side = 3; break;
    }
    return (side - EntranceSide(pattern) + 4) % 4;
}
bool Fits(const BuildingPattern& pattern, const ParcelPlan& parcel, int setback) {
    const int turns = Turns(pattern, parcel.frontage);
    const int width = turns % 2 ? pattern.height : pattern.width;
    const int height = turns % 2 ? pattern.width : pattern.height;
    return width <= parcel.bounds.max.x - parcel.bounds.min.x - 2 * setback
        && height <= parcel.bounds.max.z - parcel.bounds.min.z - 2 * setback;
}
bool Extended(const BuildingPattern& pattern) {
    return !pattern.layers.empty() || !pattern.basements.empty()
        || !pattern.population.furniture.empty() || !pattern.population.loot.empty()
        || !pattern.population.monsters.empty();
}

// SplitMix64 finalizer; sequential fields are mixed separately. Strings include
// their byte length, then unsigned UTF-8 bytes (never catalog numeric IDs).
void SeedField(std::uint64_t& state, std::uint64_t value) {
    state = Mix(state ^ Mix(value + 0x9e3779b97f4a7c15ULL));
}
void SeedString(std::uint64_t& state, const String& value) {
    SeedField(state, value.size());
    for (unsigned char byte : value) { SeedField(state, byte); }
}
std::uint64_t FeatureSeed(std::uint64_t seed, const FeatureOwner& owner,
                          const BuildingPattern& pattern, std::uint32_t version,
                          std::uint64_t domain) {
    auto state = Mix(seed ^ domain);
    SeedField(state, static_cast<std::uint64_t>(owner.region.x));
    SeedField(state, static_cast<std::uint64_t>(owner.region.z));
    SeedField(state, owner.parcel);
    SeedString(state, pattern.id);
    // V7 adds electrical descriptors without changing v6 population choices.
    SeedField(state, version == 7 ? 6 : version);
    return state;
}
struct GenerationRandom {
    std::uint64_t state;
    std::uint64_t Next() {
        state += 0x9e3779b97f4a7c15ULL;
        return Mix(state);
    }
    std::uint32_t Bounded(std::uint32_t bound) {
        Require(bound != 0);
        const auto wide = static_cast<std::uint64_t>(bound);
        const auto threshold = (std::uint64_t{0} - wide) % wide;
        for (;;) {
            const auto value = Next();
            if (value >= threshold) { return static_cast<std::uint32_t>(value % wide); }
        }
    }
};
template<class Entries>
std::size_t WeightedIndex(const Entries& entries, GenerationRandom& random) {
    std::uint64_t total = 0;
    for (const auto& entry : entries) {
        Require(entry.weight > 0);
        total += entry.weight;
        Require(total <= UINT32_MAX);
    }
    Require(total > 0);
    auto choice = random.Bounded(static_cast<std::uint32_t>(total));
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (choice < entries[i].weight) { return i; }
        choice -= entries[i].weight;
    }
    std::abort();
}
void AddBuilding(const ContentCatalog& content, RegionPlan& plan, const ParcelPlan& parcel) {
    const int setback = plan.generationVersion >= 5 ? 1 : 2;
    const auto eligible = [&](const BuildingPattern& pattern) {
        return Fits(pattern, parcel, setback)
            && (plan.generationVersion >= 4 || !Extended(pattern))
            && (plan.generationVersion < 5 || pattern.site == parcel.site);
    };
    std::uint64_t total = 0;
    for (std::size_t i = 1; i <= content.BuildingPatternCount(); ++i) {
        const auto& pattern = content.GetBuildingPattern({static_cast<std::uint32_t>(i)});
        if (eligible(pattern)) {
            total += pattern.weight;
        }
    }
    if (total == 0) { return; }
    const auto random = Hash(plan.seed, plan.region.x, plan.region.z, 100 + parcel.owner.parcel);
    auto choice = random % total;
    BuildingPatternId selected{};
    for (std::size_t i = 1; i <= content.BuildingPatternCount(); ++i) {
        const auto& pattern = content.GetBuildingPattern({static_cast<std::uint32_t>(i)});
        if (!eligible(pattern)) { continue; }
        if (choice < pattern.weight) { selected = {static_cast<std::uint32_t>(i)}; break; }
        choice -= pattern.weight;
    }
    Require(selected.value != 0);
    const auto& pattern = content.GetBuildingPattern(selected);
    const int turns = Turns(pattern, parcel.frontage);
    const int width = turns % 2 ? pattern.height : pattern.width;
    const int height = turns % 2 ? pattern.width : pattern.height;
    const int slack = parcel.bounds.max.x - parcel.bounds.min.x - width - 2 * setback;
    const int x = parcel.bounds.min.x + setback + static_cast<int>(Mix(random) % (slack + 1));
    const int z = parcel.frontage == Frontage::North ? parcel.bounds.min.z + setback
        : parcel.bounds.max.z - height - setback;
    const auto entry = Rotate(pattern.entrance, pattern.width, pattern.height, turns);
    const TileCoord entrance{x + entry.x, z + entry.z};
    const auto abandonmentSeed = Mix(random ^ 0xa54ff53a5f1d36f1ULL);
    std::uint32_t basementVariant = 0;
    if (plan.generationVersion >= 4 && !pattern.basements.empty()) {
        GenerationRandom basementRandom{FeatureSeed(plan.seed, parcel.owner, pattern,
            plan.generationVersion, 0x626173656d656e74ULL)}; // "basement"
        basementVariant = static_cast<std::uint32_t>(WeightedIndex(pattern.basements, basementRandom) + 1);
    }
    plan.buildings.push_back({parcel.owner, selected, static_cast<std::uint8_t>(turns),
        {{x, z}, {x + width, z + height}}, entrance,
        {entrance, {entrance.x, parcel.streetZ}, 1}, abandonmentSeed,
        abandonmentSeed % 4 != 0 && !(plan.generationVersion >= 4 && Extended(pattern)),
        basementVariant});
}

bool HasSite(const ContentCatalog& content, BuildingSite site) {
    for (std::size_t i = 1; i <= content.BuildingPatternCount(); ++i) {
        if (content.GetBuildingPattern({static_cast<std::uint32_t>(i)}).site == site) { return true; }
    }
    return false;
}

void AddDenseParcels(const ContentCatalog& content, RegionPlan& plan, int x, int z) {
    const int civicStrip = static_cast<int>(Hash(plan.seed, plan.region.x, plan.region.z, 0x6369766963ULL) % 8);
    const bool landmarks = HasSite(content, BuildingSite::Landmark);
    const bool parks = HasSite(content, BuildingSite::Park);
    const bool squares = HasSite(content, BuildingSite::Square);
    for (int row = 0; row < 2; ++row) {
        for (int column = 0; column < 2; ++column) {
            const int left = x + 74 + column * 56;
            const int top = z + 74 + row * 56;
            for (int half = 0; half < 2; ++half) {
                const int strip = (row * 2 + column) * 2 + half;
                const bool landmark = landmarks && strip == civicStrip;
                // Alternate wide and narrow shop/house frontages; a three-tile
                // shared rear alley separates the two 25-tile-deep strips.
                const int count = landmark ? 1 : 2 + (strip + civicStrip) % 2;
                for (int lot = 0; lot < count; ++lot) {
                    BuildingSite site = landmark ? BuildingSite::Landmark : BuildingSite::Building;
                    if (lot == 0 && parks && strip == (civicStrip + 2) % 8) { site = BuildingSite::Park; }
                    if (lot == 0 && squares && strip == (civicStrip + 5) % 8) { site = BuildingSite::Square; }
                    ParcelPlan parcel{{plan.region, static_cast<std::uint32_t>(plan.parcels.size())},
                        {{left + 53 * lot / count, top + half * 28},
                         {left + 53 * (lot + 1) / count, top + half * 28 + 25}},
                        half == 0 ? Frontage::North : Frontage::South,
                        z + 72 + (row + half) * 56, site};
                    plan.parcels.push_back(parcel);
                    AddBuilding(content, plan, parcel);
                }
            }
        }
    }
}

// V6 streams and scratch are local to a plan. Numeric catalog IDs are only
// borrowed lookup indices; ordering and all seed fields use stable content keys.
std::uint64_t TownSeed(const RegionPlan& plan, std::uint64_t domain,
                       std::uint64_t street = 0, std::uint64_t side = 0,
                       std::uint64_t interval = 0, std::uint64_t slot = 0) {
    auto state = Mix(plan.seed ^ domain);
    SeedField(state, static_cast<std::uint64_t>(plan.region.x));
    SeedField(state, static_cast<std::uint64_t>(plan.region.z));
    SeedField(state, 6);
    SeedField(state, street);
    SeedField(state, side);
    SeedField(state, interval);
    SeedField(state, slot);
    return state;
}
std::uint64_t Bounded64(GenerationRandom& random, std::uint64_t bound) {
    Require(bound != 0);
    const auto threshold = (std::uint64_t{0} - bound) % bound;
    for (;;) {
        const auto value = random.Next();
        if (value >= threshold) { return value % bound; }
    }
}
void AddWeight(std::uint64_t& total, std::uint64_t weight) {
    Require(weight > 0 && total <= UINT64_MAX - weight);
    total += weight;
}
TileRect RoadBounds(const RoadSegment& road) {
    const int radius = road.width / 2;
    return {{Min(road.first.x, road.last.x) - radius, Min(road.first.z, road.last.z) - radius},
        {Max(road.first.x, road.last.x) + radius + 1, Max(road.first.z, road.last.z) + radius + 1}};
}
bool Encloses(TileRect outer, TileRect inner) {
    return Inside(outer, inner.min) && Inside(outer, {inner.max.x - 1, inner.max.z - 1});
}
struct FrontageInterval {
    TileRect bounds;
    bool horizontal;
    Frontage frontage;
    int axis;
    std::uint32_t street;
    std::uint32_t side;
    std::uint32_t interval;
    int cursor;
    int end;
};
int FrontageWidth(const FrontageInterval& strip) {
    return strip.horizontal ? strip.bounds.max.x - strip.bounds.min.x
        : strip.bounds.max.z - strip.bounds.min.z;
}
int StripDepth(const FrontageInterval& strip) {
    return strip.horizontal ? strip.bounds.max.z - strip.bounds.min.z
        : strip.bounds.max.x - strip.bounds.min.x;
}
int PedestrianGap(const FrontageInterval& strip) {
    return strip.street <= 1 && FrontageWidth(strip) >= 12 ? 3 : 0;
}
void Footprint(const BuildingPattern& pattern, const FrontageInterval& strip,
               int& frontage, int& depth) {
    const int turns = Turns(pattern, strip.frontage);
    const int width = turns % 2 ? pattern.height : pattern.width;
    const int height = turns % 2 ? pattern.width : pattern.height;
    frontage = strip.horizontal ? width : height;
    depth = strip.horizontal ? height : width;
}
struct FrontageCandidates {
    Vector<BuildingPatternId> ordered;
    Vector<BuildingPatternId> fitting;
    Vector<std::size_t> families;
};
bool SameFamily(const BuildingPattern& a, const BuildingPattern& b) {
    if (a.family.empty() != b.family.empty()) { return false; }
    if (a.family.empty()) { return a.id == b.id; }
    return a.family == b.family;
}
void CollectCandidates(const ContentCatalog& content, const FrontageInterval& strip,
                       BuildingSite site, int remaining, FrontageCandidates& scratch) {
    scratch.fitting.clear();
    scratch.families.clear();
    for (const auto id : scratch.ordered) {
        const auto& pattern = content.GetBuildingPattern(id);
        int width = 0;
        int depth = 0;
        Footprint(pattern, strip, width, depth);
        if (pattern.site != site || width + 2 > remaining || depth + 2 > StripDepth(strip)) { continue; }
        if (scratch.fitting.empty() || !SameFamily(pattern,
            content.GetBuildingPattern(scratch.fitting.back()))) {
            scratch.families.push_back(scratch.fitting.size());
        }
        scratch.fitting.push_back(id);
    }
}
struct SlotPreference {
    bool sideStreet = false;
    bool centralMain = false;
    bool outerEnd = false;
};
SlotPreference ClassifyFrontageSlot(const RegionPlan& plan, const FrontageInterval& strip) {
    const auto& street = plan.roads[strip.street];
    const int q = strip.cursor + 1;
    const int center = strip.horizontal ? plan.townCenter.x : plan.townCenter.z;
    const int low = strip.horizontal ? Min(street.first.x, street.last.x) : Min(street.first.z, street.last.z);
    const int high = strip.horizontal ? Max(street.first.x, street.last.x) : Max(street.first.z, street.last.z);
    const int distance = q < center ? center - q : q - center;
    const int reach = q < center ? center - low : high - center;
    Require(reach > 0 && distance <= reach);
    return {strip.street != 0, strip.street == 0 && distance <= 24, 4 * distance >= 3 * reach};
}
std::uint64_t EffectiveFamilyWeight(const BuildingPattern& pattern, SlotPreference preference) {
    const auto base = pattern.family.empty() ? pattern.weight : pattern.familyWeight;
    Require(base > 0 && base <= 1'000'000);
    std::uint64_t multiplier = ratioScale;
    switch (pattern.use) {
    case BuildingUse::Mixed: break;
    case BuildingUse::Residential:
        if (preference.sideStreet) { multiplier = 3ULL * ratioScale; }
        break;
    case BuildingUse::Commercial:
        if (preference.centralMain) { multiplier = 4ULL * ratioScale; }
        break;
    case BuildingUse::Industrial:
        if (preference.outerEnd) { multiplier = 4ULL * ratioScale; }
        break;
    default: std::abort();
    }
    Require(multiplier > 0 && multiplier <= 4ULL * ratioScale
        && static_cast<std::uint64_t>(base) <= UINT64_MAX / multiplier);
    const auto effective = static_cast<std::uint64_t>(base) * multiplier / ratioScale;
    Require(effective > 0 && effective <= 4'000'000);
    return effective;
}
BuildingPatternId SelectFrontage(const ContentCatalog& content, const RegionPlan& plan,
                                const FrontageInterval& strip, BuildingSite site,
                                std::uint32_t slot, FrontageCandidates& scratch) {
    CollectCandidates(content, strip, site, strip.end - strip.cursor, scratch);
    if (scratch.fitting.empty()) { return {}; }
    // Classification precedes selection, independent of candidate footprint or
    // entrance. Civic classes retain neutral weights, including fallback parks.
    const auto preference = plan.generationVersion >= 6 && site == BuildingSite::Building
        ? ClassifyFrontageSlot(plan, strip) : SlotPreference{};
    std::uint64_t total = 0;
    for (const auto first : scratch.families) {
        const auto& pattern = content.GetBuildingPattern(scratch.fitting[first]);
        AddWeight(total, EffectiveFamilyWeight(pattern, preference));
    }
    GenerationRandom familyRandom{TownSeed(plan, 0x763666616d696c79ULL,
        strip.street, strip.side, strip.interval, slot)};
    SeedField(familyRandom.state, static_cast<std::uint32_t>(site));
    auto choice = Bounded64(familyRandom, total);
    std::size_t family = 0;
    for (; family < scratch.families.size(); ++family) {
        const auto& pattern = content.GetBuildingPattern(scratch.fitting[scratch.families[family]]);
        const auto weight = EffectiveFamilyWeight(pattern, preference);
        if (choice < weight) { break; }
        choice -= weight;
    }
    Require(family < scratch.families.size());
    const auto first = scratch.families[family];
    const auto last = family + 1 < scratch.families.size() ? scratch.families[family + 1] : scratch.fitting.size();
    const auto& representative = content.GetBuildingPattern(scratch.fitting[first]);
    GenerationRandom variantRandom{TownSeed(plan, 0x763676617269616eULL,
        strip.street, strip.side, strip.interval, slot)};
    SeedField(variantRandom.state, static_cast<std::uint32_t>(site));
    SeedField(variantRandom.state, representative.family.empty() ? 0 : 1);
    SeedString(variantRandom.state, representative.family.empty() ? representative.id : representative.family);
    total = 0;
    for (auto i = first; i < last; ++i) { AddWeight(total, content.GetBuildingPattern(scratch.fitting[i]).weight); }
    choice = Bounded64(variantRandom, total);
    for (auto i = first; i < last; ++i) {
        const auto id = scratch.fitting[i];
        const auto weight = content.GetBuildingPattern(id).weight;
        if (choice < weight) { return id; }
        choice -= weight;
    }
    std::abort();
}

struct LongitudinalInterval { int first; int last; };
void SubtractInterval(Vector<LongitudinalInterval>& intervals, int first, int last) {
    const auto count = intervals.size();
    for (std::size_t i = 0; i < count; ++i) {
        const auto original = intervals[i];
        if (first >= original.last || last <= original.first) { continue; }
        intervals[i].last = Max(original.first, first);
        if (last < original.last) { intervals.push_back({Max(last, original.first), original.last}); }
    }
}
void AddFrontageStrips(RegionPlan& plan, std::size_t streetCount,
                      Vector<FrontageInterval>& strips) {
    Vector<LongitudinalInterval> intervals;
    for (std::size_t street = 0; street < streetCount; ++street) {
        if (street == 1 && plan.townProfile == TownProfile::MainStreet) { continue; }
        const auto& road = plan.roads[street];
        const bool horizontal = road.first.z == road.last.z;
        const int axis = horizontal ? road.first.z : road.first.x;
        for (std::uint32_t side = 0; side < 2; ++side) {
            GenerationRandom random{TownSeed(plan, 0x7636737472697073ULL, street, side)};
            // Deep reserved strips consume the perpendicular street's usable
            // frontage even when the selected buildings are shallow.
            const int depth = 28 + static_cast<int>(random.Bounded(
                plan.townProfile == TownProfile::MainStreet ? 13 : 5));
            const int first = (horizontal ? Min(road.first.x, road.last.x) : Min(road.first.z, road.last.z))
                + static_cast<int>(random.Bounded(7));
            const int last = (horizontal ? Max(road.first.x, road.last.x) : Max(road.first.z, road.last.z))
                + 1 - static_cast<int>(random.Bounded(7));
            const int near = side == 0 ? axis - 1 : axis + 2;
            const int far = side == 0 ? near - depth : near + depth;
            TileRect bounds = horizontal ? TileRect{{first, Min(near, far)}, {last, Max(near, far)}}
                : TileRect{{Min(near, far), first}, {Max(near, far), last}};
            bounds.min.x = Max(bounds.min.x, plan.townBounds.min.x);
            bounds.min.z = Max(bounds.min.z, plan.townBounds.min.z);
            bounds.max.x = Min(bounds.max.x, plan.townBounds.max.x);
            bounds.max.z = Min(bounds.max.z, plan.townBounds.max.z);
            intervals.clear();
            intervals.push_back({horizontal ? bounds.min.x : bounds.min.z,
                horizontal ? bounds.max.x : bounds.max.z});
            const auto subtract = [&](TileRect blocker) {
                if (Intersects(bounds, blocker)) {
                    SubtractInterval(intervals, horizontal ? blocker.min.x : blocker.min.z,
                        horizontal ? blocker.max.x : blocker.max.z);
                }
            };
            for (const auto& other : plan.roads) { subtract(RoadBounds(other)); }
            for (const auto& earlier : strips) { subtract(earlier.bounds); }
            eastl::sort(intervals.begin(), intervals.end(), [](auto a, auto b) { return a.first < b.first; });
            std::uint32_t ordinal = 0;
            for (const auto interval : intervals) {
                if (interval.last - interval.first < 3) { continue; }
                auto part = bounds;
                if (horizontal) { part.min.x = interval.first; part.max.x = interval.last; }
                else { part.min.z = interval.first; part.max.z = interval.last; }
                FrontageInterval strip{part, horizontal,
                    horizontal ? (side == 0 ? Frontage::South : Frontage::North)
                        : (side == 0 ? Frontage::East : Frontage::West),
                    axis, static_cast<std::uint32_t>(street), side, ordinal++, interval.first, interval.last};
                if (StripDepth(strip) < 3) { continue; }
                // Reserve pedestrian access before committing lots. A short
                // interval stays entirely available to tiny authored sites.
                strip.cursor += PedestrianGap(strip);
                strips.push_back(strip);
            }
        }
    }
}

void CommitFrontage(const ContentCatalog& content, RegionPlan& plan, FrontageInterval& strip,
                    BuildingPatternId selected, std::uint32_t slot) {
    const auto& pattern = content.GetBuildingPattern(selected);
    int frontage = 0;
    int depth = 0;
    Footprint(pattern, strip, frontage, depth);
    GenerationRandom yardRandom{TownSeed(plan, 0x7636796172647321ULL,
        strip.street, strip.side, strip.interval, slot)};
    SeedString(yardRandom.state, pattern.id);
    const int rear = Min(3 + static_cast<int>(yardRandom.Bounded(6)), StripDepth(strip) - depth - 1);
    Require(rear >= 1);
    const int lotDepth = depth + 1 + rear;
    const int cursor = strip.cursor;
    TileRect lot;
    switch (strip.frontage) {
    case Frontage::North: lot = {{cursor, strip.axis + 2}, {cursor + frontage + 2, strip.axis + 2 + lotDepth}}; break;
    case Frontage::South: lot = {{cursor, strip.axis - 1 - lotDepth}, {cursor + frontage + 2, strip.axis - 1}}; break;
    case Frontage::East: lot = {{strip.axis - 1 - lotDepth, cursor}, {strip.axis - 1, cursor + frontage + 2}}; break;
    case Frontage::West: lot = {{strip.axis + 2, cursor}, {strip.axis + 2 + lotDepth, cursor + frontage + 2}}; break;
    }
    const bool forward = strip.frontage == Frontage::North || strip.frontage == Frontage::West;
    const int width = strip.horizontal ? frontage : depth;
    const int height = strip.horizontal ? depth : frontage;
    const int x = strip.horizontal ? cursor + 1 : forward ? lot.min.x + 1 : lot.max.x - width - 1;
    const int z = !strip.horizontal ? cursor + 1 : forward ? lot.min.z + 1 : lot.max.z - height - 1;
    const TileRect buildingBounds{{x, z}, {x + width, z + height}};
    Require(Encloses(plan.bounds, lot) && Encloses(strip.bounds, lot) && Encloses(lot, buildingBounds));
    const FeatureOwner owner{plan.region, static_cast<std::uint32_t>(plan.parcels.size())};
    const int turns = Turns(pattern, strip.frontage);
    const auto localEntry = Rotate(pattern.entrance, pattern.width, pattern.height, turns);
    const TileCoord entrance{x + localEntry.x, z + localEntry.z};
    const TileCoord terminal = strip.horizontal ? TileCoord{entrance.x, strip.axis} : TileCoord{strip.axis, entrance.z};
    const auto abandonment = FeatureSeed(plan.seed, owner, pattern, 6, 0x76366162616e646fULL);
    std::uint32_t basement = 0;
    if (!pattern.basements.empty()) {
        GenerationRandom random{FeatureSeed(plan.seed, owner, pattern, 6, 0x626173656d656e74ULL)};
        basement = static_cast<std::uint32_t>(WeightedIndex(pattern.basements, random) + 1);
    }
    plan.parcels.push_back({owner, lot, strip.frontage, strip.horizontal ? strip.axis : 0,
        pattern.site, strip.horizontal ? 0 : strip.axis});
    plan.buildings.push_back({owner, selected, static_cast<std::uint8_t>(turns), buildingBounds,
        entrance, {entrance, terminal, 1}, abandonment, abandonment % 4 != 0 && !Extended(pattern), basement});
    strip.cursor += frontage + 2;
}

struct AlleyOpportunity {
    TileRect lane;
    TileRect connection;
    std::size_t firstParcel;
    std::size_t lastParcel;
    Frontage frontage;
};
void PlanServiceAlley(const RegionPlan& plan, const FrontageInterval& strip,
                     std::size_t first, Vector<AlleyOpportunity>& opportunities) {
    if (strip.street > 1 || plan.parcels.size() - first < 2 || FrontageWidth(strip) < 12) { return; }
    const int start = strip.horizontal ? strip.bounds.min.x : strip.bounds.min.z;
    int far = strip.axis;
    const bool positive = strip.frontage == Frontage::North || strip.frontage == Frontage::West;
    for (auto i = first; i < plan.parcels.size(); ++i) {
        const auto b = plan.parcels[i].bounds;
        const int edge = strip.horizontal ? (positive ? b.max.z : b.min.z) : (positive ? b.max.x : b.min.x);
        far = positive ? Max(far, edge) : Min(far, edge);
    }
    const int low = positive ? far : far - 3;
    const int high = positive ? far + 3 : far;
    const int roadEdge = positive ? strip.axis - 1 : strip.axis + 2;
    const auto lane = strip.horizontal ? TileRect{{start, low}, {strip.cursor, high}}
        : TileRect{{low, start}, {high, strip.cursor}};
    const auto connection = strip.horizontal
        ? TileRect{{start, Min(low, roadEdge)}, {start + 3, Max(high, roadEdge)}}
        : TileRect{{Min(low, roadEdge), start}, {Max(high, roadEdge), start + 3}};
    if (Encloses(plan.townBounds, lane) && Encloses(plan.townBounds, connection)) {
        opportunities.push_back({lane, connection, first, plan.parcels.size(), strip.frontage});
    }
}

bool ClearServiceSpace(const RegionPlan& plan, TileRect bounds, bool allowRoads = false) {
    if (!Encloses(plan.bounds, bounds) || !Encloses(plan.townBounds, bounds)) { return false; }
    for (const auto& parcel : plan.parcels) {
        if (Intersects(bounds, parcel.bounds)) { return false; }
    }
    if (!allowRoads) {
        for (const auto& road : plan.roads) {
            if (Intersects(bounds, RoadBounds(road))) { return false; }
        }
    }
    for (const auto alley : plan.serviceAlleys) {
        if (Intersects(bounds, alley)) { return false; }
    }
    return true;
}

TileRect RearAccessSpur(const ParcelPlan& parcel, TileRect lane) {
    const auto b = parcel.bounds;
    const int x = b.min.x + (b.max.x - b.min.x) / 2;
    const int z = b.min.z + (b.max.z - b.min.z) / 2;
    // Half-open endpoints touch both the rear garden and lane without entering
    // either parcel interiors or the lane. Deepest lots need no extra rectangle.
    switch (parcel.frontage) {
    case Frontage::North: return {{x, b.max.z}, {x + 1, lane.min.z}};
    case Frontage::South: return {{x, lane.max.z}, {x + 1, b.min.z}};
    case Frontage::West: return {{b.max.x, z}, {lane.min.x, z + 1}};
    case Frontage::East: return {{lane.max.x, z}, {b.min.x, z + 1}};
    }
    std::abort();
}

void GenerateTownV6(const ContentCatalog& content, RegionPlan& plan) {
    const int x = plan.region.x * region_size;
    const int z = plan.region.z * region_size;
    plan.townBounds = {{x + 40, z + 40}, {x + 216, z + 216}};
    plan.hasTown = true;
    GenerationRandom geometry{TownSeed(plan, 0x763670726f66696cULL)};
    plan.townProfile = static_cast<TownProfile>(1 + geometry.Bounded(3));
    plan.townCenter = {x + 124 + static_cast<int>(geometry.Bounded(9)),
        z + 124 + static_cast<int>(geometry.Bounded(9))};
    const auto center = plan.townCenter;
    const bool shortFeeder = plan.townProfile == TownProfile::MainStreet;
    // Longer perpendicular arms must extend past the reserved main-street
    // strips. Center 124..132 plus reach <=80 keeps radius-one caps in [40,216).
    const int west = center.x - (shortFeeder ? 56 : 64) - static_cast<int>(geometry.Bounded(17));
    const int east = center.x + (shortFeeder ? 56 : 64) + static_cast<int>(geometry.Bounded(17));
    const int north = center.z - (shortFeeder ? 12 : 72) - static_cast<int>(geometry.Bounded(9));
    const int south = center.z + (shortFeeder ? 12 : 72) + static_cast<int>(geometry.Bounded(9));
    AddRoad(plan, {west, center.z}, {east, center.z});
    AddRoad(plan, {center.x, north}, {center.x, south});
    if (plan.townProfile == TownProfile::OffsetNeighborhood) {
        // Two full cross streets provide neighborhood-scale frontage. Independent
        // ends stay uneven; the 72..80 spine always reaches both 60..64 branches.
        for (std::uint32_t side = 0; side < 2; ++side) {
            const int branchZ = center.z + (side == 0 ? -1 : 1) * (60 + static_cast<int>(geometry.Bounded(5)));
            const int left = center.x - 60 - static_cast<int>(geometry.Bounded(17));
            const int right = center.x + 60 + static_cast<int>(geometry.Bounded(17));
            AddRoad(plan, {left, branchZ}, {right, branchZ});
        }
    }
    const auto streetCount = plan.roads.Size();
    auto p = plan.boundaries[0].position;
    AddRoad(plan, p, {p.x, z + 24});
    AddRoad(plan, {p.x, z + 24}, {center.x, z + 24});
    AddRoad(plan, {center.x, z + 24}, {center.x, north});
    p = plan.boundaries[1].position;
    AddRoad(plan, p, {x + 232, p.z});
    AddRoad(plan, {x + 232, p.z}, {x + 232, center.z});
    AddRoad(plan, {x + 232, center.z}, {east, center.z});
    p = plan.boundaries[2].position;
    AddRoad(plan, p, {p.x, z + 232});
    AddRoad(plan, {p.x, z + 232}, {center.x, z + 232});
    AddRoad(plan, {center.x, z + 232}, {center.x, south});
    p = plan.boundaries[3].position;
    AddRoad(plan, p, {x + 24, p.z});
    AddRoad(plan, {x + 24, p.z}, {x + 24, center.z});
    AddRoad(plan, {x + 24, center.z}, {west, center.z});

    Vector<FrontageInterval> strips;
    AddFrontageStrips(plan, streetCount, strips);
    FrontageCandidates scratch;
    scratch.ordered.reserve(content.BuildingPatternCount());
    scratch.fitting.reserve(content.BuildingPatternCount());
    scratch.families.reserve(content.BuildingPatternCount());
    for (std::size_t i = 1; i <= content.BuildingPatternCount(); ++i) {
        scratch.ordered.push_back({static_cast<std::uint32_t>(i)});
    }
    eastl::sort(scratch.ordered.begin(), scratch.ordered.end(), [&](auto a, auto b) {
        const auto& left = content.GetBuildingPattern(a);
        const auto& right = content.GetBuildingPattern(b);
        if (left.family.empty() != right.family.empty()) { return left.family.empty(); }
        const auto& leftKey = left.family.empty() ? left.id : left.family;
        const auto& rightKey = right.family.empty() ? right.id : right.family;
        if (leftKey != rightKey) { return leftKey < rightKey; }
        return left.id < right.id;
    });
    struct CivicReservation { std::size_t strip; BuildingPatternId pattern; };
    Vector<CivicReservation> civic;
    Vector<std::size_t> available;
    // Temporary cursors reserve actual widths; emission later restores them and
    // assigns owner ordinals only in canonical frontage order.
    for (const auto site : {BuildingSite::Landmark, BuildingSite::Park, BuildingSite::Square}) {
        available.clear();
        for (std::size_t i = 0; i < strips.size(); ++i) {
            const auto& strip = strips[i];
            if (strip.street != 0) { continue; }
            if (site == BuildingSite::Landmark && (strip.end - strip.cursor < 42 || StripDepth(strip) < 27)) { continue; }
            CollectCandidates(content, strip, site, strip.end - strip.cursor, scratch);
            if (!scratch.fitting.empty()) { available.push_back(i); }
        }
        if (available.empty()) { continue; }
        GenerationRandom random{TownSeed(plan, 0x7636636976696373ULL)};
        SeedField(random.state, static_cast<std::uint32_t>(site));
        const auto i = available[static_cast<std::size_t>(Bounded64(random, available.size()))];
        auto& strip = strips[i];
        const auto selected = SelectFrontage(content, plan, strip, site, static_cast<std::uint32_t>(civic.size()), scratch);
        Require(selected.value != 0);
        civic.push_back({i, selected});
        int width = 0;
        int depth = 0;
        Footprint(content.GetBuildingPattern(selected), strip, width, depth);
        strip.cursor += width + 2;
    }
    // Grow output only as actual fitting sites are committed. Empty/no-fit
    // catalogs must not allocate buffers sized for hypothetical 1x1 buildings.
    Vector<AlleyOpportunity> alleys;
    for (std::size_t i = 0; i < strips.size(); ++i) {
        auto& strip = strips[i];
        strip.cursor = (strip.horizontal ? strip.bounds.min.x : strip.bounds.min.z) + PedestrianGap(strip);
        const auto first = plan.parcels.size();
        std::uint32_t slot = 0;
        for (const auto reservation : civic) {
            if (reservation.strip == i) { CommitFrontage(content, plan, strip, reservation.pattern, slot++); }
        }
        while (strip.end - strip.cursor >= 3) {
            auto selected = SelectFrontage(content, plan, strip, BuildingSite::Building, slot, scratch);
            if (!selected.value) { selected = SelectFrontage(content, plan, strip, BuildingSite::Park, slot, scratch); }
            if (!selected.value) { break; }
            CommitFrontage(content, plan, strip, selected, slot++);
        }
        PlanServiceAlley(plan, strip, first, alleys);
    }
    Vector<TileRect> accessSpurs;
    for (const auto opportunity : alleys) {
        if (!ClearServiceSpace(plan, opportunity.lane)
            || !ClearServiceSpace(plan, opportunity.connection, true)) { continue; }
        accessSpurs.clear();
        bool clear = true;
        for (auto i = opportunity.firstParcel; i < opportunity.lastParcel; ++i) {
            const auto& parcel = plan.parcels[i];
            Require(parcel.frontage == opportunity.frontage);
            const auto spur = RearAccessSpur(parcel, opportunity.lane);
            if (spur.min.x == spur.max.x || spur.min.z == spur.max.z) { continue; }
            if (!ClearServiceSpace(plan, spur)) { clear = false; break; }
            accessSpurs.push_back(spur);
        }
        // Commit atomically: an emitted lane serves every associated rear yard,
        // rather than leaving a shallower lot separated by uncleared woodland.
        if (!clear) { continue; }
        plan.serviceAlleys.push_back(opportunity.lane);
        plan.serviceAlleys.push_back(opportunity.connection);
        for (const auto spur : accessSpurs) { plan.serviceAlleys.push_back(spur); }
    }
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
                                 const BuildingPlan& building, const Vector<TerrainId>& source,
                                 bool decorate) {
    const auto& pattern = content.GetBuildingPattern(building.pattern);
    const int width = building.bounds.max.x - building.bounds.min.x;
    const int height = building.bounds.max.z - building.bounds.min.z;
    Require(building.quarterTurns < 4 && width > 0 && height > 0 && width <= 64 && height <= 64);
    Require(source.size() == static_cast<std::size_t>(pattern.width * pattern.height));
    Vector<Tile> tiles(static_cast<std::size_t>(width * height));
    for (int z = 0; z < pattern.height; ++z) {
        for (int x = 0; x < pattern.width; ++x) {
            const auto p = Rotate({x, z}, pattern.width, pattern.height, building.quarterTurns);
            Require(p.x < width && p.z < height);
            tiles[static_cast<std::size_t>(p.z * width + p.x)] =
                {source[static_cast<std::size_t>(z * pattern.width + x)], 0};
        }
    }
    if (decorate) { DecorateAbandonedBuilding(content, palette, building, tiles, width, height); }
    return tiles;
}

TileCoord PopulationPosition(const BuildingPattern& pattern, const BuildingPlan& building,
                             TileCoord local) {
    const auto rotated = Rotate({local.x, local.z}, pattern.width, pattern.height, building.quarterTurns);
    return {building.bounds.min.x + rotated.x, building.bounds.min.z + rotated.z, local.y};
}

GenerationRandom MarkerRandom(const RegionPlan& plan, const BuildingPlan& building,
                              const BuildingPattern& pattern, const String& variant,
                              GenerationPlacementKind kind, std::uint32_t placement) {
    auto state = FeatureSeed(plan.seed, building.owner, pattern, plan.generationVersion,
        0x706f70756c617465ULL); // "populate", distinct from basement selection.
    SeedString(state, variant); // Empty string denotes common population.
    SeedField(state, static_cast<std::uint32_t>(kind));
    SeedField(state, placement);
    return {state};
}

bool Chance(std::uint32_t chance, GenerationRandom& random) {
    Require(chance <= ratioScale);
    return random.Bounded(ratioScale) < chance;
}

// Bounded recursive DFS: the validated catalog guarantees these limits across
// all rolls. Runtime checks fail on broken catalog invariants; never truncate.
void ExpandLoot(const ContentCatalog& content, LootPoolId pool, GenerationRandom& random,
                GenerationKey& key, TileCoord position, LootDestination destination,
                std::uint32_t depth, std::uint32_t& work, Vector<GeneratedItem>& output) {
    Require(depth <= maxLootDepth && ++work <= maxLootExpansionWork);
    const auto& entries = content.GetLootPool(pool).entries;
    const auto& entry = entries[WeightedIndex(entries, random)];
    Require(entry.count.min >= 1 && entry.count.min <= entry.count.max
        && entry.count.max <= maxSpawnCount);
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
              const BuildingPattern& pattern, const PatternPopulation& population,
              std::uint32_t variantIndex, const String& variant, ChunkCoord chunk,
              ChunkPlacements& output) {
    for (std::size_t i = 0; i < population.furniture.size(); ++i) {
        const auto& marker = population.furniture[i];
        const auto position = PopulationPosition(pattern, building, marker.position);
        if (ChunkOf(position) != chunk) { continue; }
        Require(marker.facing <= ObjectFacing::West);
        const auto facing = static_cast<ObjectFacing>((static_cast<unsigned>(marker.facing)
            + building.quarterTurns) % 4);
        output.furniture.push_back({{building.owner, variantIndex, GenerationPlacementKind::Furniture,
            static_cast<std::uint32_t>(i), 0}, position, marker.type, facing});
    }
    for (std::size_t i = 0; i < population.loot.size(); ++i) {
        const auto& marker = population.loot[i];
        const auto position = PopulationPosition(pattern, building, marker.position);
        if (ChunkOf(position) != chunk) { continue; }
        auto random = MarkerRandom(plan, building, pattern, variant, GenerationPlacementKind::Loot,
            static_cast<std::uint32_t>(i));
        Require(marker.rolls <= maxSpawnRolls);
        if (!Chance(marker.chance, random)) { continue; }
        GenerationKey key{building.owner, variantIndex, GenerationPlacementKind::Loot,
            static_cast<std::uint32_t>(i), 0};
        std::uint32_t work = 0;
        for (std::uint32_t roll = 0; roll < marker.rolls; ++roll) {
            ExpandLoot(content, marker.pool, random, key, position, marker.destination, 1, work, output.items);
        }
    }
    for (std::size_t i = 0; i < population.monsters.size(); ++i) {
        const auto& marker = population.monsters[i];
        const auto position = PopulationPosition(pattern, building, marker.position);
        if (ChunkOf(position) != chunk) { continue; }
        auto random = MarkerRandom(plan, building, pattern, variant, GenerationPlacementKind::Monster,
            static_cast<std::uint32_t>(i));
        if (!Chance(marker.chance, random)) { continue; }
        const auto& entries = content.GetMonsterGroup(marker.group).entries;
        const auto type = entries[WeightedIndex(entries, random)].monster;
        output.monsters.push_back({{building.owner, variantIndex, GenerationPlacementKind::Monster,
            static_cast<std::uint32_t>(i), 0}, position, type});
    }
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
    return GenerateRegionPlan(content, seed, region, world_generation_version);
}

RegionPlan GenerateRegionPlan(const ContentCatalog& content, std::uint64_t seed, RegionCoord region,
                              std::uint32_t generationVersion) {
    Require(generationVersion >= 2 && generationVersion <= world_generation_version);
    CheckRegion(region);
    (void)GetPalette(content);
    RegionPlan plan;
    plan.region = region;
    plan.seed = seed;
    plan.generationVersion = generationVersion;
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

    if (generationVersion >= 6 && plan.bounds.min.x == x && plan.bounds.min.z == z
        && plan.bounds.max.x == x + region_size && plan.bounds.max.z == z + region_size) {
        GenerateTownV6(content, plan);
        return plan;
    }
    // V6 partial regions deliberately use nominal v5 roads, without a town.
    if (generationVersion >= 6) { plan.hasTown = false; }

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
    if (generationVersion >= 5) {
        AddDenseParcels(content, plan, x, z);
        return plan;
    }
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
    Require(plan.generationVersion >= 2 && plan.generationVersion <= world_generation_version);
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
                tile.terrain = plan.generationVersion <= 4 && chunk.y == 1 && BuildingAt(plan, p) ? palette.floor : air;
                continue;
            }
            if (Demo(p)) { tile.terrain = palette.floor; continue; }
            const auto environment = SampleEnvironment(seed, p);
            const int treeThreshold = environment.woodland < 400 ? 15 : (environment.woodland - 350) / 2;
            tile.terrain = Hash(seed, p.x, p.z, 25) % 1000 < static_cast<std::uint64_t>(treeThreshold)
                ? palette.tree : palette.grass;
            // Keep parcels usable: no trees in yards, access or building sites.
            if (ParcelAt(plan, p)) { tile.terrain = palette.grass; }
            if (plan.generationVersion == 5 && plan.hasTown && Inside(plan.townBounds, p)) {
                tile.terrain = palette.pavement;
            }
            if (plan.generationVersion >= 6) {
                if (const auto* parcel = ParcelAt(plan, p)) {
                    const auto b = parcel->bounds;
                    bool front = false;
                    switch (parcel->frontage) {
                    case Frontage::North: front = p.z == b.min.z; break;
                    case Frontage::South: front = p.z == b.max.z - 1; break;
                    case Frontage::East: front = p.x == b.max.x - 1; break;
                    case Frontage::West: front = p.x == b.min.x; break;
                    }
                    if (front) { tile.terrain = palette.pavement; }
                }
                for (const auto alley : plan.serviceAlleys) {
                    if (Inside(alley, p)) { tile.terrain = palette.pavement; break; }
                }
            }
            if (IsRoad(plan, p)) { tile.terrain = palette.road; }
        }
    }
    if (chunk.y != 0 && plan.generationVersion <= 3) { return tiles; }
    const TileRect chunkBounds{{x, z}, {x + chunk_size, z + chunk_size}};
    for (const auto& building : plan.buildings) {
        if (!Intersects(building.bounds, chunkBounds)) { continue; }
        const auto& pattern = content.GetBuildingPattern(building.pattern);
        const Vector<TerrainId>* source = chunk.y == 0 ? &pattern.tiles : nullptr;
        if (plan.generationVersion >= 4) {
            for (const auto& layer : pattern.layers) {
                if (layer.y == chunk.y) { source = &layer.tiles; }
            }
            Require(building.basementVariant <= pattern.basements.size());
            if (building.basementVariant != 0) {
                const auto& basement = pattern.basements[building.basementVariant - 1];
                if (basement.layer.y == chunk.y) { source = &basement.layer.tiles; }
            }
        }
        // Unauthored single-storey roofs follow indoor surface tiles, leaving
        // gardens and civic outdoor sites open to the sky. Taller buildings
        // explicitly author their upper floors and roof (including setbacks).
        const bool roof = !source && chunk.y == 1 && plan.generationVersion >= 5
            && pattern.site != BuildingSite::Park && pattern.site != BuildingSite::Square;
        if (roof) { source = &pattern.tiles; }
        if (!source) { continue; }
        const auto complete = MaterializeBuilding(content, palette, building, *source,
            chunk.y == 0 && (plan.generationVersion <= 3 || !Extended(pattern)));
        const int width = building.bounds.max.x - building.bounds.min.x;
        for (int pz = Max(z, building.bounds.min.z); pz < Min(z + chunk_size, building.bounds.max.z); ++pz) {
            for (int px = Max(x, building.bounds.min.x); px < Min(x + chunk_size, building.bounds.max.x); ++px) {
                const TileCoord p{px, pz, chunk.y};
                if (!Inside(plan.bounds, p) || (chunk.y == 0 && Demo(p))) { continue; }
                // Driveway geometry includes the entrance; IsRoad excludes it.
                // Street centerlines never enter a building's setback footprint.
                auto tile = complete[static_cast<std::size_t>((pz - building.bounds.min.z) * width
                    + px - building.bounds.min.x)];
                if (roof) {
                    const bool outdoors = tile.terrain == content.FindTerrain("grass")
                        || tile.terrain == content.FindTerrain("tree") || tile.terrain == content.FindTerrain("road")
                        || tile.terrain == content.FindTerrain("pavement") || content.GetTerrain(tile.terrain).openSpace;
                    tile = {outdoors ? air : palette.floor, 0};
                }
                tiles[static_cast<std::size_t>((pz - z) * chunk_size + px - x)] = tile;
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
    int minY = 0;
    int maxY = 0;
    for (const auto& layer : pattern.layers) {
        minY = Min(minY, layer.y);
        maxY = Max(maxY, layer.y);
    }
    if (basementVariant != 0) {
        const auto y = pattern.basements[basementVariant - 1].layer.y;
        minY = Min(minY, y);
        maxY = Max(maxY, y);
    }
    GeneratedBuildingPower result;
    result.origin = {footprint.min.x, footprint.min.z, 0};
    result.bounds = {{footprint.min.x, footprint.min.z, minY},
        {footprint.max.x - 1, footprint.max.z - 1, maxY}};
    Require(ValidPowerBounds(result.bounds));
    return result;
}

void GenerateBuildingPower(const ContentCatalog& content, const RegionPlan& plan,
    ChunkCoord chunk, Vector<GeneratedBuildingPower>& output) {
    output.clear();
    constexpr int chunkLimit = world_coordinate_limit / chunk_size;
    Require(chunk.x >= -chunkLimit && chunk.x <= chunkLimit
        && chunk.z >= -chunkLimit && chunk.z <= chunkLimit
        && chunk.y >= world_min_y && chunk.y <= world_max_y);
    CheckRegion(plan.region);
    Require((RegionCoord{FloorDiv(chunk.x * chunk_size, region_size),
        FloorDiv(chunk.z * chunk_size, region_size)}) == plan.region);
    // Registration policy/version gating belongs to World; no terrain/version change.
    const TileRect chunkBounds{{chunk.x * chunk_size, chunk.z * chunk_size},
        {(chunk.x + 1) * chunk_size, (chunk.z + 1) * chunk_size}};
    for (const auto& building : plan.buildings) {
        if (!Intersects(building.bounds, chunkBounds)) { continue; }
        const auto descriptor = DescribeBuildingPower(content.GetBuildingPattern(building.pattern),
            building.bounds, building.basementVariant);
        if (descriptor && chunk.y >= descriptor->bounds.min.y && chunk.y <= descriptor->bounds.max.y) {
            output.push_back(*descriptor);
        }
    }
}

void GenerateChunkPlacements(const ContentCatalog& content, std::uint64_t seed,
                             const RegionPlan& plan, ChunkCoord chunk, ChunkPlacements& output) {
    output.furniture.clear();
    output.items.clear();
    output.monsters.clear();
    constexpr int chunkLimit = world_coordinate_limit / chunk_size;
    Require(chunk.x >= -chunkLimit && chunk.x <= chunkLimit
        && chunk.z >= -chunkLimit && chunk.z <= chunkLimit
        && chunk.y >= world_min_y && chunk.y <= world_max_y);
    CheckRegion(plan.region);
    Require(plan.seed == seed && plan.generationVersion >= 1
        && plan.generationVersion <= world_generation_version);
    Require((RegionCoord{FloorDiv(chunk.x * chunk_size, region_size),
        FloorDiv(chunk.z * chunk_size, region_size)}) == plan.region);
    if (plan.generationVersion <= 3) { return; }
    const TileRect chunkBounds{{chunk.x * chunk_size, chunk.z * chunk_size},
        {(chunk.x + 1) * chunk_size, (chunk.z + 1) * chunk_size}};
    const String commonVariant;
    for (const auto& building : plan.buildings) {
        if (!Intersects(building.bounds, chunkBounds)) { continue; }
        const auto& pattern = content.GetBuildingPattern(building.pattern);
        Populate(content, plan, building, pattern, pattern.population, 0, commonVariant, chunk, output);
        Require(building.basementVariant <= pattern.basements.size());
        if (building.basementVariant != 0) {
            const auto& basement = pattern.basements[building.basementVariant - 1];
            Populate(content, plan, building, pattern, basement.population,
                building.basementVariant, basement.id, chunk, output);
        }
    }
}
}
