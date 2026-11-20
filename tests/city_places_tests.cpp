#include "content/content.h"
#include "worldgen/worldgen.h"

#include <catch2/catch_test_macros.hpp>
#include <EASTL/algorithm.h>
#include <EASTL/utility.h>

namespace {
using namespace cataclysmx;

const ContentCatalog& CityContent() {
    static const ContentCatalog content = [] {
        Diagnostics diagnostics;
        auto loaded = LoadContentPack(CATACLYSMX_CONTENT_PACK, &diagnostics);
        INFO(FormatDiagnostics(diagnostics).c_str());
        REQUIRE(loaded);
        REQUIRE(ValidateWorldgenContent(*loaded, &diagnostics));
        return eastl::move(*loaded);
    }();
    return content;
}

bool Walkable(const ContentCatalog& content, TerrainId id) {
    const auto& terrain = content.GetTerrain(id);
    if (terrain.passable && !terrain.openSpace) { return true; }
    const auto* opened = content.TryGetTerrain(terrain.opensTo);
    return opened && opened->passable && !opened->openSpace;
}

const Vector<TerrainId>& Floor(const BuildingPattern& pattern, int y) {
    if (!y) { return pattern.tiles; }
    for (const auto& layer : pattern.layers) {
        if (layer.y == y) { return layer.tiles; }
    }
    FAIL("Occupied floor lacks an authored layer");
    return pattern.tiles;
}

void CheckRoutes(const ContentCatalog& content, const BuildingPattern& pattern) {
    const int area = pattern.width * pattern.height;
    const int floors = pattern.occupiedFloors;
    Vector<std::uint8_t> walkable(static_cast<std::size_t>(area * floors), 0);
    Vector<std::uint8_t> stairs(walkable.size(), 0);
    Vector<std::uint8_t> reached(walkable.size(), 0);
    for (int y = 0; y < floors; ++y) {
        const auto& tiles = Floor(pattern, y);
        for (int cell = 0; cell < area; ++cell) {
            const auto index = static_cast<std::size_t>(y * area + cell);
            walkable[index] = Walkable(content, tiles[static_cast<std::size_t>(cell)]);
            stairs[index] = content.GetTerrain(tiles[static_cast<std::size_t>(cell)]).stairs;
        }
    }
    for (const auto& object : pattern.population.furniture) {
        if (object.position.y < 0 || object.position.y >= floors) { continue; }
        if (content.GetObject(object.type).blocksMovement) {
            walkable[static_cast<std::size_t>(object.position.y * area
                + object.position.z * pattern.width + object.position.x)] = 0;
        }
    }
    const int start = pattern.entrance.z * pattern.width + pattern.entrance.x;
    REQUIRE(walkable[static_cast<std::size_t>(start)]);
    Vector<int> queue{start};
    reached[static_cast<std::size_t>(start)] = 1;
    for (std::size_t head = 0; head < queue.size(); ++head) {
        const int index = queue[head];
        const int y = index / area;
        const int z = index % area / pattern.width;
        const int x = index % pattern.width;
        const auto visit = [&](int nx, int nz, int ny, bool vertical) {
            if (nx < 0 || nz < 0 || ny < 0 || nx >= pattern.width
                || nz >= pattern.height || ny >= floors) { return; }
            const auto next = static_cast<std::size_t>(ny * area + nz * pattern.width + nx);
            if (!walkable[next] || reached[next]) { return; }
            if (vertical && (!stairs[static_cast<std::size_t>(index)] || !stairs[next])) { return; }
            reached[next] = 1;
            queue.push_back(static_cast<int>(next));
        };
        visit(x - 1, z, y, false);
        visit(x + 1, z, y, false);
        visit(x, z - 1, y, false);
        visit(x, z + 1, y, false);
        visit(x, z, y - 1, true);
        visit(x, z, y + 1, true);
    }
    for (std::size_t index = 0; index < walkable.size(); ++index) {
        if (!walkable[index]) { continue; }
        CAPTURE(pattern.id.c_str(), index % pattern.width,
            index % area / pattern.width, index / area);
        CHECK(reached[index] != 0);
    }
    for (const auto& object : pattern.population.furniture) {
        const auto p = object.position;
        if (p.y < 0 || p.y >= floors) { continue; }
        bool accessible = false;
        for (const PatternPoint delta : {PatternPoint{-1, 0}, {1, 0}, {0, -1}, {0, 1}}) {
            const int x = p.x + delta.x;
            const int z = p.z + delta.z;
            if (x >= 0 && z >= 0 && x < pattern.width && z < pattern.height) {
                accessible |= reached[static_cast<std::size_t>(p.y * area + z * pattern.width + x)] != 0;
            }
        }
        CAPTURE(pattern.id.c_str(), content.GetObject(object.type).id.c_str(), p.x, p.z, p.y);
        CHECK(accessible);
    }
}

TEST_CASE("City catalog preserves the named pre-D4 indoor reference budgets", "[city][content][population]") {
    const auto& content = CityContent();
    struct Baseline { const char* id; std::uint64_t expected; };
    for (const auto baseline : {Baseline{"bungalow", 2500}, {"tall_row_house", 8500},
            {"workshop", 17000}, {"hospital", 52500}}) {
        const auto& pattern = content.GetBuildingPattern(content.FindBuildingPattern(baseline.id));
        std::uint64_t total = 0;
        for (const auto& marker : pattern.population.monsters) { total += marker.chance; }
        REQUIRE(pattern.basements.size() <= 1);
        for (const auto& basement : pattern.basements) {
            for (const auto& marker : basement.population.monsters) { total += marker.chance; }
        }
        CAPTURE(baseline.id);
        CHECK(total == baseline.expected);
    }
}

TEST_CASE("City occupied floors and equipment have traversable authored approaches", "[city][content][routes]") {
    const auto& content = CityContent();
    std::size_t newerPatterns = 0;
    for (std::uint32_t value = 1; value <= content.BuildingPatternCount(); ++value) {
        const auto& pattern = content.GetBuildingPattern({value});
        // Existing old patterns have their established route suite. This covers
        // every new-era variant, including each height and dystopian variant.
        if (pattern.era != BuildingEra::Newer) { continue; }
        ++newerPatterns;
        CheckRoutes(content, pattern);
    }
    CHECK(newerPatterns >= 12);
    for (const char* id : {"retrofitted_foundry", "covered_market_arcade", "neighborhood_compute_house"}) {
        const auto pattern = content.FindBuildingPattern(id);
        REQUIRE(pattern.value != 0);
        CheckRoutes(content, content.GetBuildingPattern(pattern));
    }
}

TEST_CASE("City terrain roster supplies forty usable identities and correct door states", "[city][content][terrain]") {
    const auto& content = CityContent();
    constexpr const char* ids[] = {
        "wall_weathered_red_brick", "wall_soot_brick", "wall_repaired_brick", "wall_fresh_red_brick",
        "wall_pale_brick", "wall_gray_block", "wall_aged_plaster", "wall_modular_partition",
        "wall_concrete_off_white", "wall_concrete_sand", "wall_concrete_rust_red", "wall_concrete_blue",
        "wall_concrete_green", "wall_concrete_charcoal", "wall_riveted_steel", "wall_ribbed_steel",
        "wall_insulated_panel", "wall_white_composite", "wall_graphite_composite", "wall_glazed_curtain",
        "floor_weathered_timber", "floor_dark_tile", "floor_white_tile", "floor_terrazzo",
        "floor_sealed_concrete", "floor_gray_epoxy", "floor_green_epoxy", "floor_clean_room",
        "floor_steel_plate", "floor_rubber_service", "road_worn_asphalt", "road_patched_asphalt",
        "pavement_pale_pavers", "pavement_dark_pavers", "pavement_brick", "pavement_service_concrete",
        "industrial_door_closed", "industrial_door_open", "glazed_door_closed", "glazed_door_open"
    };
    static_assert(sizeof(ids) / sizeof(ids[0]) == 40);
    for (const char* id : ids) {
        CAPTURE(id);
        const auto type = content.FindTerrain(id);
        REQUIRE(type.value != 0);
        CHECK_FALSE(content.GetTerrain(type).appearance.symbol.empty());
        bool used = false;
        const auto uses = [&](const Vector<TerrainId>& tiles) {
            for (const auto tile : tiles) {
                if (tile == type || content.GetTerrain(tile).opensTo == type) { return true; }
            }
            return false;
        };
        for (std::uint32_t value = 1; value <= content.BuildingPatternCount() && !used; ++value) {
            const auto& pattern = content.GetBuildingPattern({value});
            used = uses(pattern.tiles);
            for (const auto& layer : pattern.layers) { used |= uses(layer.tiles); }
            for (const auto& basement : pattern.basements) { used |= uses(basement.layer.tiles); }
        }
        CHECK(used);
    }
    for (const char* id : {"industrial_door_closed", "glazed_door_closed"}) {
        const auto& closed = content.GetTerrain(content.FindTerrain(id));
        REQUIRE(closed.opensTo.value != 0);
        const auto& open = content.GetTerrain(closed.opensTo);
        CHECK_FALSE(closed.passable);
        CHECK(open.passable);
        CHECK_FALSE(open.blocksSight);
        CHECK(open.closesTo == content.FindTerrain(id));
    }
    CHECK_FALSE(content.GetTerrain(content.FindTerrain("glazed_door_closed")).blocksSight);
    CHECK(content.GetTerrain(content.FindTerrain("industrial_door_closed")).blocksSight);
    const auto& glass = content.GetTerrain(content.FindTerrain("wall_glazed_curtain"));
    CHECK_FALSE(glass.passable);
    CHECK_FALSE(glass.blocksSight);
}

TEST_CASE("Teaching and electronics sites retain the supplied printer acquisition budgets", "[city][content][printing]") {
    const auto& content = CityContent();
    const auto printer = content.FindObject("home_polymer_printer_station");
    const auto filament = content.FindLootPool("loot_printer_filament_large");
    REQUIRE(printer.value != 0);
    REQUIRE(filament.value != 0);
    for (const char* id : {"polytechnic_school", "electronics_service_center", "device_reclamation_depot"}) {
        const auto patternId = content.FindBuildingPattern(id);
        REQUIRE(patternId.value != 0);
        const auto& pattern = content.GetBuildingPattern(patternId);
        std::size_t printers = 0;
        std::uint32_t draws = 0;
        for (const auto& marker : pattern.population.furniture) { printers += marker.type == printer; }
        for (const auto& marker : pattern.population.loot) {
            if (marker.pool != filament) { continue; }
            CHECK(marker.chance == ratioScale);
            draws += marker.rolls;
        }
        CAPTURE(id);
        CHECK(printers == 1);
        CHECK(draws == (pattern.id == "polytechnic_school" ? 1 : 2));
    }
}
}
