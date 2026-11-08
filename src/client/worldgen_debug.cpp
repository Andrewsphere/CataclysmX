#include "client/worldgen_debug.h"

#include <cstdio>
#include <cstdlib>

namespace cataclysmx {
namespace {

void Require(bool condition) {
    if (!condition) { std::abort(); }
}

int Min(int a, int b) { return a < b ? a : b; }
int Max(int a, int b) { return a > b ? a : b; }

void CheckPlan(const ContentCatalog& content, const RegionPlan& plan) {
    constexpr int minimum = -world_coordinate_limit / region_size - 1;
    constexpr int maximum = world_coordinate_limit / region_size;
    Require(plan.region.x >= minimum && plan.region.x <= maximum
        && plan.region.z >= minimum && plan.region.z <= maximum);
    const int x = plan.region.x * region_size;
    const int z = plan.region.z * region_size;
    const TileRect expected{{Max(x, -world_coordinate_limit), Max(z, -world_coordinate_limit)},
        {Min(x + region_size, world_coordinate_limit + 1),
         Min(z + region_size, world_coordinate_limit + 1)}};
    Require(plan.bounds == expected);
    Require(ValidateWorldgenContent(content).has_value());
    // As with GenerateChunk, the remaining plan records must be the unmodified
    // generated plan for this immutable catalog; do not regenerate to verify it.
}

char OwnerGlyph(const FeatureOwner& owner) {
    return static_cast<char>('A' + owner.parcel % 26);
}

TerminalAppearance Glyph(char symbol, RgbColor foreground) {
    TerminalAppearance result;
    result.symbol += symbol;
    result.foreground = foreground;
    return result;
}

struct CachedChunk {
    bool generated = false;
    eastl::array<Tile, chunk_size * chunk_size> tiles{};
};

TerminalAppearance FeatureCell(const RegionPlan& plan, WorldgenDebugLayer layer, TileRect cell) {
    bool road = false;
    const ParcelPlan* parcel = nullptr;
    const BuildingPlan* building = nullptr;
    // Scan the entire clipped cell: even a one-tile driveway or width-three
    // crossing must survive reduction. Equal-priority owners use the lowest
    // parcel ordinal, independent of traversal order or glyph wraparound.
    for (int z = cell.min.z; z < cell.max.z; ++z) {
        for (int x = cell.min.x; x < cell.max.x; ++x) {
            const TileCoord p{x, z};
            road |= IsRoad(plan, p);
            if (layer == WorldgenDebugLayer::Roads) { continue; }
            if (const auto* candidate = ParcelAt(plan, p)) {
                if (!parcel || candidate->owner.parcel < parcel->owner.parcel) { parcel = candidate; }
            }
            if (const auto* candidate = BuildingAt(plan, p)) {
                if (!building || candidate->owner.parcel < building->owner.parcel) { building = candidate; }
            }
        }
    }
    // Priority: parcels = building > road > parcel; ownership = building owner
    // > parcel owner > road; roads = road. Features always outrank outlines.
    if (layer == WorldgenDebugLayer::Ownership) {
        if (building) { return Glyph(OwnerGlyph(building->owner), {255, 210, 110}); }
        if (parcel) { return Glyph(OwnerGlyph(parcel->owner), {100, 210, 230}); }
    }
    if (layer == WorldgenDebugLayer::Parcels && building) { return Glyph('#', {255, 210, 110}); }
    if (road) { return Glyph('+', {220, 220, 220}); }
    if (layer == WorldgenDebugLayer::Parcels && parcel) {
        return Glyph(OwnerGlyph(parcel->owner), {100, 210, 230});
    }
    if (cell.min.x == plan.bounds.min.x || cell.max.x == plan.bounds.max.x) {
        return Glyph('|', {100, 110, 130});
    }
    if (cell.min.z == plan.bounds.min.z || cell.max.z == plan.bounds.max.z) {
        return Glyph('-', {100, 110, 130});
    }
    const bool town = plan.hasTown && cell.min.x < plan.townBounds.max.x
        && cell.max.x > plan.townBounds.min.x && cell.min.z < plan.townBounds.max.z
        && cell.max.z > plan.townBounds.min.z;
    return Glyph(town ? '.' : ' ', {80, 120, 80});
}

}

const char* WorldgenDebugLayerName(WorldgenDebugLayer layer) {
    switch (layer) {
    case WorldgenDebugLayer::Roads: return "Roads";
    case WorldgenDebugLayer::Parcels: return "Parcels";
    case WorldgenDebugLayer::Ownership: return "Ownership";
    case WorldgenDebugLayer::Biomes: return "Biomes";
    case WorldgenDebugLayer::Terrain: return "Terrain";
    default: std::abort();
    }
}

const char* WorldgenDebugLegend(WorldgenDebugLayer layer) {
    switch (layer) {
    case WorldgenDebugLayer::Roads:
        return "+ road; . town; | - region boundary";
    case WorldgenDebugLayer::Parcels:
        return "# building > + road > A-Z parcel; | - region boundary";
    case WorldgenDebugLayer::Ownership:
        return "A-Z owner (building > parcel, ordinal modulo 26); + road; | - region boundary";
    case WorldgenDebugLayer::Biomes:
        return "T woodland; . meadow (cell center sample)";
    case WorldgenDebugLayer::Terrain:
        return "Generated baseline terrain: content appearance at cell top-left; excludes player changes";
    default: std::abort();
    }
}

WorldgenDebugView BuildWorldgenDebugView(const ContentCatalog& content,
    const RegionPlan& plan, WorldgenDebugLayer layer, int scale) {
    (void)WorldgenDebugLayerName(layer); // Always-on enum validation, including empty views.
    Require(scale >= 1 && scale <= 16);
    CheckPlan(content, plan);
    WorldgenDebugView view;
    view.bounds = plan.bounds;
    view.scale = scale;
    view.width = (plan.bounds.max.x - plan.bounds.min.x + scale - 1) / scale;
    view.height = (plan.bounds.max.z - plan.bounds.min.z + scale - 1) / scale;
    view.cells.reserve(static_cast<std::size_t>(view.width * view.height));

    Vector<CachedChunk> chunks;
    const ChunkCoord first = ChunkOf(plan.bounds.min);
    const ChunkCoord last = ChunkOf({plan.bounds.max.x - 1, plan.bounds.max.z - 1});
    const int chunkColumns = last.x - first.x + 1;
    const int chunkCount = chunkColumns * (last.z - first.z + 1);
    Require(chunkCount > 0 && chunkCount <= 256);
    if (layer == WorldgenDebugLayer::Terrain) { chunks.resize(static_cast<std::size_t>(chunkCount)); }
    for (int row = 0; row < view.height; ++row) {
        for (int column = 0; column < view.width; ++column) {
            const TileCoord p{plan.bounds.min.x + column * scale, plan.bounds.min.z + row * scale};
            const TileRect cell{p, {Min(p.x + scale, plan.bounds.max.x), Min(p.z + scale, plan.bounds.max.z)}};
            if (layer == WorldgenDebugLayer::Terrain) {
                // Only the top-left sample is required. Direct indexing retains
                // each required chunk exactly once, with at most 16*16 entries.
                // No environment/feature query loop is performed by this view.
                const ChunkCoord position = ChunkOf(p);
                auto& cached = chunks[static_cast<std::size_t>((position.z - first.z) * chunkColumns
                    + position.x - first.x)];
                if (!cached.generated) {
                    cached.tiles = GenerateChunk(content, plan.seed, plan, position);
                    cached.generated = true;
                }
                const auto terrain = cached.tiles[static_cast<std::size_t>(TileIndex(p))].terrain;
                // Preserve Unicode and both colors; display-width fallback belongs
                // to RenderTerrainCell in the frontend, not this baseline preview.
                view.cells.push_back(content.GetTerrain(terrain).appearance);
            } else if (layer == WorldgenDebugLayer::Biomes) {
                // Center sample of the clipped cell (positive-side center for an
                // even extent), rather than a potentially expensive majority vote.
                const TileCoord center{p.x + (cell.max.x - p.x) / 2, p.z + (cell.max.z - p.z) / 2};
                const bool woodland = SampleEnvironment(plan.seed, center).biome == Biome::Woodland;
                view.cells.push_back(Glyph(woodland ? 'T' : '.', woodland
                    ? RgbColor{45, 150, 70} : RgbColor{155, 200, 85}));
            } else {
                view.cells.push_back(FeatureCell(plan, layer, cell));
            }
        }
    }
    return view;
}

Vector<String> WorldgenDebugDetails(const ContentCatalog& content,
    const RegionPlan& plan, TileCoord selection) {
    CheckPlan(content, plan);
    Require(Contains(plan.bounds, selection));
    Vector<String> lines;
    char buffer[512];
    std::snprintf(buffer, sizeof(buffer), "Tile=%d,%d seed=%llu region=%d,%d (generated baseline)",
        selection.x, selection.z, static_cast<unsigned long long>(plan.seed), plan.region.x, plan.region.z);
    lines.push_back(String(buffer));
    const auto environment = SampleEnvironment(plan.seed, selection);
    std::snprintf(buffer, sizeof(buffer), "Environment: moisture=%d temperature=%d woodland=%d biome=%s",
        environment.moisture, environment.temperature, environment.woodland,
        environment.biome == Biome::Woodland ? "Woodland" : "Meadow");
    lines.push_back(String(buffer));
    std::snprintf(buffer, sizeof(buffer), "Town: present=%s selected=%s center=%d,%d bounds=[%d,%d)-[%d,%d)",
        plan.hasTown ? "yes" : "no", plan.hasTown && Contains(plan.townBounds, selection) ? "yes" : "no",
        plan.townCenter.x, plan.townCenter.z, plan.townBounds.min.x, plan.townBounds.min.z,
        plan.townBounds.max.x, plan.townBounds.max.z);
    lines.push_back(String(buffer));
    if (const auto* parcel = ParcelAt(plan, selection)) {
        std::snprintf(buffer, sizeof(buffer), "Parcel: owner=%d,%d:%u letter=%c bounds=[%d,%d)-[%d,%d)",
            parcel->owner.region.x, parcel->owner.region.z, static_cast<unsigned>(parcel->owner.parcel),
            OwnerGlyph(parcel->owner), parcel->bounds.min.x, parcel->bounds.min.z,
            parcel->bounds.max.x, parcel->bounds.max.z);
        lines.push_back(String(buffer));
    } else { lines.push_back(String("Parcel: none")); }
    if (const auto* building = BuildingAt(plan, selection)) {
        std::snprintf(buffer, sizeof(buffer), "Building: owner=%d,%d:%u letter=%c pattern=",
            building->owner.region.x, building->owner.region.z, static_cast<unsigned>(building->owner.parcel),
            OwnerGlyph(building->owner));
        String line(buffer);
        line += content.GetBuildingPattern(building->pattern).id;
        std::snprintf(buffer, sizeof(buffer), " turns=%u footprint=[%d,%d)-[%d,%d) entrance=%d,%d abandoned=%s",
            static_cast<unsigned>(building->quarterTurns), building->bounds.min.x, building->bounds.min.z,
            building->bounds.max.x, building->bounds.max.z, building->entrance.x, building->entrance.z,
            building->abandoned ? "yes" : "no");
        line += buffer;
        lines.push_back(line);
    } else { lines.push_back(String("Building: none")); }
    for (const auto& crossing : plan.boundaries) {
        Require(crossing.key.axis == BorderAxis::X || crossing.key.axis == BorderAxis::Z);
        std::snprintf(buffer, sizeof(buffer), "Crossing: axis=%c key=%d,%d position=%d,%d width=%d approach=%d",
            crossing.key.axis == BorderAxis::X ? 'X' : 'Z', crossing.key.border, crossing.key.alongRegion,
            crossing.position.x, crossing.position.z, crossing.width, crossing.approachLength);
        lines.push_back(String(buffer));
    }
    return lines;
}

}
