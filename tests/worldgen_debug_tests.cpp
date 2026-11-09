#include "client/worldgen_debug.h"
#include "content_fixture.h"

#include <catch2/catch_test_macros.hpp>
#include <EASTL/utility.h>

#include <cstdio>
#include <initializer_list>

namespace {
using namespace cataclysmx;

constexpr WorldgenDebugLayer layers[] = {WorldgenDebugLayer::Roads, WorldgenDebugLayer::Parcels,
    WorldgenDebugLayer::Ownership, WorldgenDebugLayer::Biomes, WorldgenDebugLayer::Terrain};

const TerminalAppearance& At(const WorldgenDebugView& view, TileCoord position) {
    REQUIRE(Contains(view.bounds, position));
    return view.cells[static_cast<std::size_t>((position.z - view.bounds.min.z) / view.scale * view.width
        + (position.x - view.bounds.min.x) / view.scale)];
}

bool Has(const Vector<String>& lines, const char* text) {
    for (const auto& line : lines) {
        if (line.find(text) != String::npos) { return true; }
    }
    return false;
}

TEST_CASE("Worldgen debug layers own bounded repeatable whole-region images", "[client][worldgen-debug]") {
    const RegionCoord regions[] = {{-2, -3}, {-3907, -3907}, {-3907, 3906},
        {3906, -3907}, {3906, 3906}};
    for (const auto region : regions) {
        const auto plan = GenerateRegionPlan(TestContent(), 42, region);
        const auto original = plan;
        for (const auto layer : layers) {
            CHECK(WorldgenDebugLayerName(layer)[0] != '\0');
            CHECK(WorldgenDebugLegend(layer)[0] != '\0');
            for (int scale : {1, 7, 16}) {
                CAPTURE(region.x, region.z, scale, WorldgenDebugLayerName(layer));
                const auto view = BuildWorldgenDebugView(TestContent(), plan, layer, scale);
                CHECK(view.bounds == plan.bounds);
                CHECK(view.scale == scale);
                CHECK(view.width == (plan.bounds.max.x - plan.bounds.min.x + scale - 1) / scale);
                CHECK(view.height == (plan.bounds.max.z - plan.bounds.min.z + scale - 1) / scale);
                CHECK(view.width > 0);
                CHECK(view.height > 0);
                CHECK(view.width <= region_size);
                CHECK(view.height <= region_size);
                REQUIRE(view.cells.size() == static_cast<std::size_t>(view.width * view.height));
                for (const auto& cell : view.cells) { REQUIRE_FALSE(cell.symbol.empty()); }
                const auto again = BuildWorldgenDebugView(TestContent(), plan, layer, scale);
                CHECK(view.cells == again.cells);
                CHECK(view.bounds == again.bounds);
            }
        }
        CHECK(plan == original);
    }
}

TEST_CASE("Coarse debug feature cells preserve exact crossings and feature priorities", "[client][worldgen-debug]") {
    const auto plan = GenerateRegionPlan(TestContent(), 1234, {-2, -3});
    for (int scale : {8, 16}) {
        const auto roads = BuildWorldgenDebugView(TestContent(), plan, WorldgenDebugLayer::Roads, scale);
        const auto parcels = BuildWorldgenDebugView(TestContent(), plan, WorldgenDebugLayer::Parcels, scale);
        const auto owners = BuildWorldgenDebugView(TestContent(), plan, WorldgenDebugLayer::Ownership, scale);
        for (const auto& crossing : plan.boundaries) {
            auto position = crossing.position;
            if (position.x == plan.bounds.max.x) { --position.x; }
            if (position.z == plan.bounds.max.z) { --position.z; }
            REQUIRE(IsRoad(plan, position));
            CHECK(At(roads, position).symbol == "+");
            CHECK(At(parcels, position).symbol == "+");
            CHECK(At(owners, position).symbol == "+");
        }
        // Independent per-cell oracle for feature presence and lowest ordinal:
        // a center/top-left-only implementation misses narrow features here.
        for (int row = 0; row < roads.height; ++row) {
            for (int column = 0; column < roads.width; ++column) {
                const TileCoord start{plan.bounds.min.x + column * scale, plan.bounds.min.z + row * scale};
                bool road = false;
                int parcel = 1000;
                int building = 1000;
                for (int z = start.z; z < start.z + scale && z < plan.bounds.max.z; ++z) {
                    for (int x = start.x; x < start.x + scale && x < plan.bounds.max.x; ++x) {
                        road |= IsRoad(plan, {x, z});
                        if (const auto* p = ParcelAt(plan, {x, z})) {
                            if (static_cast<int>(p->owner.parcel) < parcel) { parcel = static_cast<int>(p->owner.parcel); }
                        }
                        if (const auto* b = BuildingAt(plan, {x, z})) {
                            if (static_cast<int>(b->owner.parcel) < building) { building = static_cast<int>(b->owner.parcel); }
                        }
                    }
                }
                CHECK((At(roads, start).symbol == "+") == road);
                if (building != 1000) { CHECK(At(parcels, start).symbol == "#"); }
                else if (road) { CHECK(At(parcels, start).symbol == "+"); }
                else if (parcel != 1000) {
                    CHECK(At(parcels, start).symbol == String(1, static_cast<char>('A' + parcel % 26)));
                }
                const int owner = building != 1000 ? building : parcel;
                if (owner != 1000) {
                    CHECK(At(owners, start).symbol == String(1, static_cast<char>('A' + owner % 26)));
                } else if (road) { CHECK(At(owners, start).symbol == "+"); }
            }
        }
        // The first corner has no crossing, so an outline remains visible.
        CHECK(roads.cells.front().symbol == "|");
        CHECK(parcels.cells.front().symbol == "|");
        CHECK(owners.cells.front().symbol == "|");
    }
}

TEST_CASE("Selected debug details identify stable owners buildings environment and contracts", "[client][worldgen-debug]") {
    const auto plan = GenerateRegionPlan(TestContent(), 73, {-2, 1});
    REQUIRE_FALSE(plan.buildings.empty());
    const auto owners = BuildWorldgenDebugView(TestContent(), plan, WorldgenDebugLayer::Ownership, 1);
    for (const auto& building : plan.buildings) {
        const auto selection = building.entrance;
        CHECK(At(owners, selection).symbol == String(1, static_cast<char>('A' + building.owner.parcel % 26)));
        const auto details = WorldgenDebugDetails(TestContent(), plan, selection);
        CHECK(details == WorldgenDebugDetails(TestContent(), plan, selection));
        char expected[256];
        std::snprintf(expected, sizeof(expected), "Tile=%d,%d", selection.x, selection.z);
        CHECK(Has(details, expected));
        const auto environment = SampleEnvironment(plan.seed, selection);
        std::snprintf(expected, sizeof(expected), "moisture=%d temperature=%d woodland=%d biome=%s",
            environment.moisture, environment.temperature, environment.woodland,
            environment.biome == Biome::Woodland ? "Woodland" : "Meadow");
        CHECK(Has(details, expected));
        std::snprintf(expected, sizeof(expected), "owner=%d,%d:%u letter=%c", building.owner.region.x,
            building.owner.region.z, static_cast<unsigned>(building.owner.parcel),
            static_cast<char>('A' + building.owner.parcel % 26));
        REQUIRE(details.size() >= 5);
        CHECK(details[3].find(expected) != String::npos);
        CHECK(details[4].find(expected) != String::npos);
        String pattern = "pattern=";
        pattern += TestContent().GetBuildingPattern(building.pattern).id;
        CHECK(Has(details, pattern.c_str()));
        std::snprintf(expected, sizeof(expected), "turns=%u footprint=[%d,%d)-[%d,%d) entrance=%d,%d abandoned=%s",
            static_cast<unsigned>(building.quarterTurns), building.bounds.min.x, building.bounds.min.z,
            building.bounds.max.x, building.bounds.max.z, building.entrance.x, building.entrance.z,
            building.abandoned ? "yes" : "no");
        CHECK(Has(details, expected));
        CHECK(Has(details, "Town: present=yes selected=yes"));
        for (const auto& crossing : plan.boundaries) {
            std::snprintf(expected, sizeof(expected), "axis=%c key=%d,%d position=%d,%d width=%d approach=%d",
                crossing.key.axis == BorderAxis::X ? 'X' : 'Z', crossing.key.border, crossing.key.alongRegion,
                crossing.position.x, crossing.position.z, crossing.width, crossing.approachLength);
            CHECK(Has(details, expected));
        }
    }
    const auto outside = WorldgenDebugDetails(TestContent(), plan, plan.bounds.min);
    CHECK(Has(outside, "Parcel: none"));
    CHECK(Has(outside, "Building: none"));
}

TEST_CASE("Biome debug cells classify their clipped centers", "[client][worldgen-debug]") {
    for (const RegionCoord region : {RegionCoord{-2, -3}, {3906, -3907}}) {
        const auto plan = GenerateRegionPlan(TestContent(), 42, region);
        const auto view = BuildWorldgenDebugView(TestContent(), plan, WorldgenDebugLayer::Biomes, 7);
        for (int row = 0; row < view.height; ++row) {
            for (int column = 0; column < view.width; ++column) {
                const TileCoord start{view.bounds.min.x + column * view.scale,
                    view.bounds.min.z + row * view.scale};
                const int width = start.x + view.scale > view.bounds.max.x
                    ? view.bounds.max.x - start.x : view.scale;
                const int height = start.z + view.scale > view.bounds.max.z
                    ? view.bounds.max.z - start.z : view.scale;
                const bool woodland = SampleEnvironment(plan.seed,
                    {start.x + width / 2, start.z + height / 2}).biome == Biome::Woodland;
                const auto& appearance = At(view, start);
                CHECK(appearance.symbol == (woodland ? "T" : "."));
                CHECK(appearance.foreground.green > appearance.foreground.red);
                CHECK(appearance.foreground.green > appearance.foreground.blue);
            }
        }
    }
}

TEST_CASE("Terrain debug samples match generated chunks including full Unicode appearance", "[client][worldgen-debug]") {
    // A wide Unicode symbol must reach the renderer intact, with both colors.
    const ContentSource source{"debug-unicode.json5", R"({format_version:1,definitions:[
        {type:'terrain',id:'air',name:'Air',movement:{passable:true},blocks_sight:false,open_space:true,
         appearance:{symbol:' ',foreground:'white'}},
        {type:'terrain',id:'floor',name:'Floor',movement:{passable:true},blocks_sight:false,
         appearance:{symbol:'\u754c',foreground:'#123456',background:'#654321'}},
        {type:'terrain',id:'wall',name:'Wall',movement:{passable:false},blocks_sight:true,
         appearance:{symbol:'#',foreground:'white'}}]})"};
    auto loaded = LoadContent({&source, 1});
    REQUIRE(loaded.has_value());
    const auto content = eastl::move(*loaded);
    for (const RegionCoord region : {RegionCoord{0, 0}, {-2, -3}, {3906, -3907}}) {
        const auto plan = GenerateRegionPlan(content, 42, region);
        const auto view = BuildWorldgenDebugView(content, plan, WorldgenDebugLayer::Terrain, 7);
        const auto first = ChunkOf(plan.bounds.min);
        const auto last = ChunkOf({plan.bounds.max.x - 1, plan.bounds.max.z - 1});
        for (int z = first.z; z <= last.z; ++z) {
            for (int x = first.x; x <= last.x; ++x) {
                const ChunkCoord chunk{x, z};
                const auto tiles = GenerateChunk(content, plan.seed, plan, chunk);
                for (int row = 0; row < view.height; ++row) {
                    for (int column = 0; column < view.width; ++column) {
                        const TileCoord p{view.bounds.min.x + column * view.scale,
                            view.bounds.min.z + row * view.scale};
                        if (ChunkOf(p) != chunk) { continue; }
                        CHECK(At(view, p) == content.GetTerrain(tiles[static_cast<std::size_t>(TileIndex(p))].terrain).appearance);
                    }
                }
            }
        }
        if (region == RegionCoord{0, 0}) {
            const auto& appearance = At(view, {0, 0});
            CHECK(appearance.symbol == "\xe7\x95\x8c");
            CHECK(appearance.foreground == RgbColor{0x12, 0x34, 0x56});
            CHECK(appearance.background == RgbColor{0x65, 0x43, 0x21});
        }
    }
}

}
