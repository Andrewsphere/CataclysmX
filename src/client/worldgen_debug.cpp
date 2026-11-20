#include "client/worldgen_debug.h"
#include "client/localization.h"

#include <algorithm>
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
    Require(plan.generationVersion == world_generation_version);
    // As with GenerateChunk, the remaining plan records must be the unmodified
    // generated plan for this immutable catalog; do not regenerate to verify it.
}

char OwnerGlyph(const FeatureOwner& owner) {
    return static_cast<char>('A' + owner.parcel % 26);
}

bool OwnerBefore(const FeatureOwner& a, const FeatureOwner& b) {
    if (a.region.x != b.region.x) { return a.region.x < b.region.x; }
    if (a.region.z != b.region.z) { return a.region.z < b.region.z; }
    if (a.settlement != b.settlement) { return a.settlement < b.settlement; }
    return a.parcel < b.parcel;
}

const char* ScaleName(SettlementScale scale) {
    switch (scale) {
    case SettlementScale::Small: return "small";
    case SettlementScale::Medium: return "medium";
    case SettlementScale::Large: return "large";
    }
    std::abort();
}

const char* EraName(SettlementEra era) {
    switch (era) {
    case SettlementEra::Older: return "older";
    case SettlementEra::Mixed: return "mixed";
    case SettlementEra::Newer: return "newer";
    }
    std::abort();
}

const char* DistrictName(BuildingUse use) {
    switch (use) {
    case BuildingUse::Mixed: return "mixed";
    case BuildingUse::Residential: return "residential";
    case BuildingUse::Commercial: return "commercial";
    case BuildingUse::Industrial: return "industrial/service";
    case BuildingUse::Civic: return "civic/technical";
    }
    std::abort();
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
                if (!parcel || OwnerBefore(candidate->owner, parcel->owner)) { parcel = candidate; }
            }
            if (const auto* candidate = BuildingAt(plan, p)) {
                if (!building || OwnerBefore(candidate->owner, building->owner)) { building = candidate; }
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
    bool town = false;
    for (const auto& settlement : plan.settlements) {
        town |= cell.min.x < settlement.bounds.max.x && cell.max.x > settlement.bounds.min.x
            && cell.min.z < settlement.bounds.max.z && cell.max.z > settlement.bounds.min.z;
    }
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
    const RegionPlan& plan, TileCoord selection, const Localization* localization) {
    CheckPlan(content, plan);
    Require(Contains(plan.bounds, selection));
    Vector<String> lines;
    char buffer[512];
    std::snprintf(buffer, sizeof(buffer), "Tile=%d,%d seed=%llu region=%d,%d (generated baseline)",
        selection.x, selection.z, static_cast<unsigned long long>(plan.seed), plan.region.x, plan.region.z);
    lines.push_back(localization ? localization->Format("debug.details.tile", {
        {"x", NumberText(selection.x)}, {"z", NumberText(selection.z)}, {"seed", UnsignedText(plan.seed)},
        {"rx", NumberText(plan.region.x)}, {"rz", NumberText(plan.region.z)}}) : String(buffer));
    const auto environment = SampleEnvironment(plan.seed, selection);
    std::snprintf(buffer, sizeof(buffer), "Environment: moisture=%d temperature=%d woodland=%d biome=%s",
        environment.moisture, environment.temperature, environment.woodland,
        environment.biome == Biome::Woodland ? "Woodland" : "Meadow");
    lines.push_back(localization ? localization->Format("debug.details.environment", {
        {"moisture", NumberText(environment.moisture)}, {"temperature", NumberText(environment.temperature)},
        {"woodland", NumberText(environment.woodland)}, {"biome", localization->Text(environment.biome == Biome::Woodland
            ? "debug.woodland" : "debug.meadow")}}) : String(buffer));
    std::snprintf(buffer, sizeof(buffer), "Town: present=%s selected=%s center=%d,%d bounds=[%d,%d)-[%d,%d)",
        plan.hasTown ? "yes" : "no", plan.hasTown && Contains(plan.townBounds, selection) ? "yes" : "no",
        plan.townCenter.x, plan.townCenter.z, plan.townBounds.min.x, plan.townBounds.min.z,
        plan.townBounds.max.x, plan.townBounds.max.z);
    lines.push_back(localization ? localization->Format("debug.details.town", {
        {"present", localization->Text(plan.hasTown ? "game.yes" : "game.no")},
        {"selected", localization->Text(plan.hasTown && Contains(plan.townBounds, selection) ? "game.yes" : "game.no")},
        {"x", NumberText(plan.townCenter.x)}, {"z", NumberText(plan.townCenter.z)},
        {"min_x", NumberText(plan.townBounds.min.x)}, {"min_z", NumberText(plan.townBounds.min.z)},
        {"max_x", NumberText(plan.townBounds.max.x)}, {"max_z", NumberText(plan.townBounds.max.z)}}) : String(buffer));
    if (const auto* parcel = ParcelAt(plan, selection)) {
        std::snprintf(buffer, sizeof(buffer), "Parcel: owner=%d,%d:%u letter=%c bounds=[%d,%d)-[%d,%d)",
            parcel->owner.region.x, parcel->owner.region.z, static_cast<unsigned>(parcel->owner.parcel),
            OwnerGlyph(parcel->owner), parcel->bounds.min.x, parcel->bounds.min.z,
            parcel->bounds.max.x, parcel->bounds.max.z);
        lines.push_back(localization ? localization->Format("debug.details.parcel", {
            {"x", NumberText(parcel->owner.region.x)}, {"z", NumberText(parcel->owner.region.z)},
            {"id", UnsignedText(parcel->owner.parcel)}, {"letter", String(1, OwnerGlyph(parcel->owner))},
            {"min_x", NumberText(parcel->bounds.min.x)}, {"min_z", NumberText(parcel->bounds.min.z)},
            {"max_x", NumberText(parcel->bounds.max.x)}, {"max_z", NumberText(parcel->bounds.max.z)}}) : String(buffer));
    } else { lines.push_back(localization ? localization->Text("debug.details.no_parcel") : String("Parcel: none")); }
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
        lines.push_back(localization ? localization->Format("debug.details.building", {
            {"x", NumberText(building->owner.region.x)}, {"z", NumberText(building->owner.region.z)},
            {"id", UnsignedText(building->owner.parcel)}, {"letter", String(1, OwnerGlyph(building->owner))},
            {"pattern", content.GetBuildingPattern(building->pattern).id}, {"turns", UnsignedText(building->quarterTurns)},
            {"min_x", NumberText(building->bounds.min.x)}, {"min_z", NumberText(building->bounds.min.z)},
            {"max_x", NumberText(building->bounds.max.x)}, {"max_z", NumberText(building->bounds.max.z)},
            {"entrance_x", NumberText(building->entrance.x)}, {"entrance_z", NumberText(building->entrance.z)},
            {"abandoned", localization->Text(building->abandoned ? "game.yes" : "game.no")}}) : line);
        if (building->basementVariant != 0) {
            const auto& pattern = content.GetBuildingPattern(building->pattern);
            Require(building->basementVariant <= pattern.basements.size());
            String basement = "Basement: ";
            basement += pattern.basements[building->basementVariant - 1].id;
            lines.push_back(eastl::move(basement));
        }
    } else { lines.push_back(localization ? localization->Text("debug.details.no_building") : String("Building: none")); }
    std::snprintf(buffer, sizeof(buffer), "Generation version=%u", plan.generationVersion);
    lines.push_back(String(buffer));
    for (const auto& settlement : plan.settlements) {
        std::size_t sites = 0;
        for (const auto& building : plan.buildings) {
            sites += building.owner.region == settlement.id.region
                && building.owner.settlement == settlement.id.ordinal;
        }
        std::snprintf(buffer, sizeof(buffer),
            "Settlement %d,%d:%u: %s / %s | selected=%s | envelope=%dx%d | intersecting sites=%zu",
            settlement.id.region.x, settlement.id.region.z, settlement.id.ordinal,
            ScaleName(settlement.scale), EraName(settlement.era),
            Contains(settlement.bounds, selection) ? "yes" : "no",
            settlement.bounds.max.x - settlement.bounds.min.x,
            settlement.bounds.max.z - settlement.bounds.min.z, sites);
        lines.push_back(String(buffer));
    }
    if (const auto* parcel = ParcelAt(plan, selection)) {
        std::snprintf(buffer, sizeof(buffer), "District: %s | construction: %s | settlement ordinal=%u",
            DistrictName(parcel->district), parcel->era == BuildingEra::Newer ? "newer" : "older",
            parcel->owner.settlement);
        lines.push_back(String(buffer));
    }
    std::snprintf(buffer, sizeof(buffer),
        "Region slice: %zu settlements | %zu sites | %zu access segments | %zu initial hostiles before scenario exclusion",
        plan.settlements.size(), plan.buildings.size(), plan.serviceAlleys.size(), plan.urbanMonsters.size());
    lines.push_back(String(buffer));
    return lines;
}

void WorldgenDebugPreview::Reset() {
    *this = WorldgenDebugPreview{};
}

const RegionPlan& WorldgenDebugPreview::Plan(RegionCoord region) {
    for (const auto& plan : plans_) {
        if (plan.region == region) { return plan; }
    }
    plans_.push_back(GenerateRegionPlan(*content_, seed_, region));
    ++work_.regions;
    return plans_.back();
}

void WorldgenDebugPreview::Select(const ContentCatalog& content, std::uint64_t seed,
    TileCoord selection, WorldgenDebugLayer layer, int scale, const Localization* localization) {
    Require(scale >= 1 && scale <= 16 && (scale & (scale - 1)) == 0);
    (void)WorldgenDebugLayerName(layer);
    Require(selection.x >= -world_coordinate_limit && selection.x <= world_coordinate_limit
        && selection.z >= -world_coordinate_limit && selection.z <= world_coordinate_limit);
    selection.y = 0;
    if (content_ != &content || seed_ != seed) {
        Reset();
        content_ = &content;
        seed_ = seed;
    }
    layer_ = layer;
    scale_ = scale;
    if (selection_ != selection || localization_ != localization) {
        details_ = WorldgenDebugDetails(content, Plan(RegionOf(selection)), selection, localization);
        ++work_.details;
        selection_ = selection;
        localization_ = localization;
    }
}

TerminalAppearance WorldgenDebugPreview::Sample(TileRect cell) {
    ++work_.cells;
    if (layer_ == WorldgenDebugLayer::Biomes) {
        const TileCoord center{cell.min.x + (cell.max.x - cell.min.x) / 2,
            cell.min.z + (cell.max.z - cell.min.z) / 2};
        const bool woodland = SampleEnvironment(seed_, center).biome == Biome::Woodland;
        return Glyph(woodland ? 'T' : '.', woodland
            ? RgbColor{45, 150, 70} : RgbColor{155, 200, 85});
    }
    if (layer_ == WorldgenDebugLayer::Terrain) {
        const auto position = ChunkOf(cell.min);
        for (const auto& chunk : chunks_) {
            if (chunk.position == position) {
                return content_->GetTerrain(chunk.tiles[static_cast<std::size_t>(TileIndex(cell.min))].terrain).appearance;
            }
        }
        const auto& plan = Plan(RegionOf(cell.min));
        PreviewChunk chunk;
        chunk.position = position;
        chunk.tiles = GenerateChunk(*content_, seed_, plan, position);
        chunks_.push_back(eastl::move(chunk));
        ++work_.chunks;
        return content_->GetTerrain(chunks_.back().tiles[static_cast<std::size_t>(TileIndex(cell.min))].terrain).appearance;
    }
    return FeatureCell(Plan(RegionOf(cell.min)), layer_, cell);
}

const WorldgenDebugView& WorldgenDebugPreview::Prepare(int columns, int rows) {
    Require(content_ && selection_ && columns > 0 && rows > 0);
    const auto floorCell = [&](int coordinate) {
        const int quotient = coordinate / scale_;
        return quotient - (coordinate % scale_ < 0 ? 1 : 0);
    };
    const auto bound = [](std::int64_t coordinate) {
        return static_cast<int>(std::clamp(coordinate, std::int64_t{-world_coordinate_limit},
            std::int64_t{world_coordinate_limit + 1}));
    };
    const std::int64_t left = (std::int64_t{floorCell(selection_->x)} - columns / 2) * scale_;
    const std::int64_t top = (std::int64_t{floorCell(selection_->z)} - rows / 2) * scale_;
    const TileRect bounds{{bound(left), bound(top)},
        {bound(left + std::int64_t{columns} * scale_), bound(top + std::int64_t{rows} * scale_)}};
    const bool reuse = image_.scale == scale_ && imageLayer_ == layer_;

    // Keep the visible working set plus a one-region/chunk margin for reversing
    // direction. Evict before generating; never retain references across erasure.
    const auto firstRegion = RegionOf(bounds.min);
    const auto lastRegion = RegionOf({bounds.max.x - 1, bounds.max.z - 1});
    for (std::size_t i = 0; i < plans_.size();) {
        const auto region = plans_[i].region;
        if (region.x < firstRegion.x - 1 || region.x > lastRegion.x + 1
            || region.z < firstRegion.z - 1 || region.z > lastRegion.z + 1) {
            if (i + 1 != plans_.size()) { plans_[i] = eastl::move(plans_.back()); }
            plans_.pop_back();
        } else { ++i; }
    }
    const auto firstChunk = ChunkOf(bounds.min);
    const auto lastChunk = ChunkOf({bounds.max.x - 1, bounds.max.z - 1});
    for (std::size_t i = 0; i < chunks_.size();) {
        const auto position = chunks_[i].position;
        if (position.x < firstChunk.x - 1 || position.x > lastChunk.x + 1
            || position.z < firstChunk.z - 1 || position.z > lastChunk.z + 1) {
            if (i + 1 != chunks_.size()) { chunks_[i] = eastl::move(chunks_.back()); }
            chunks_.pop_back();
        } else { ++i; }
    }

    if (reuse && image_.bounds == bounds) { return image_; }
    scratch_.bounds = bounds;
    scratch_.scale = scale_;
    scratch_.width = (bounds.max.x - bounds.min.x + scale_ - 1) / scale_;
    scratch_.height = (bounds.max.z - bounds.min.z + scale_ - 1) / scale_;
    scratch_.cells.resize(static_cast<std::size_t>(scratch_.width) * scratch_.height);
    for (int row = 0; row < scratch_.height; ++row) {
        for (int column = 0; column < scratch_.width; ++column) {
            const TileCoord p{bounds.min.x + column * scale_, bounds.min.z + row * scale_};
            auto& output = scratch_.cells[static_cast<std::size_t>(row) * scratch_.width + column];
            if (reuse && Contains(image_.bounds, p)) {
                const int oldColumn = (p.x - image_.bounds.min.x) / scale_;
                const int oldRow = (p.z - image_.bounds.min.z) / scale_;
                output = image_.cells[static_cast<std::size_t>(oldRow) * image_.width + oldColumn];
            } else {
                output = Sample({p, {Min(p.x + scale_, world_coordinate_limit + 1),
                    Min(p.z + scale_, world_coordinate_limit + 1)}});
            }
        }
    }
    eastl::swap(image_, scratch_);
    imageLayer_ = layer_;
    return image_;
}

}
