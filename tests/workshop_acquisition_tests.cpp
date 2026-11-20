#include "content/content.h"
#include "core/simulation.h"
#include "worldgen/worldgen.h"

#include <catch2/catch_test_macros.hpp>
#include <EASTL/utility.h>

namespace {
using namespace cataclysmx;

bool PoolContains(const ContentCatalog& content, LootPoolId pool, ItemTypeId item, unsigned depth = 0) {
    REQUIRE(depth < maxLootDepth);
    for (const auto& entry : content.GetLootPool(pool).entries) {
        if (entry.kind == LootEntryKind::Item && entry.item == item) { return true; }
        if (entry.kind == LootEntryKind::Pool && PoolContains(content, entry.pool, item, depth + 1)) { return true; }
    }
    return false;
}

std::uint32_t PoolMaximum(const ContentCatalog& content, LootPoolId pool, unsigned depth = 0) {
    REQUIRE(depth < maxLootDepth);
    std::uint32_t maximum = 0;
    for (const auto& entry : content.GetLootPool(pool).entries) {
        const auto leaves = entry.kind == LootEntryKind::Item
            ? entry.count.max : entry.count.max * PoolMaximum(content, entry.pool, depth + 1);
        if (leaves > maximum) { maximum = leaves; }
    }
    return maximum;
}

bool PopulationContains(const ContentCatalog& content, const PatternPopulation& population, ItemTypeId item) {
    for (const auto& marker : population.loot) {
        if (marker.chance && marker.rolls && marker.destination == LootDestination::Ground
            && PoolContains(content, marker.pool, item)) { return true; }
    }
    return false;
}

bool SiteContains(const ContentCatalog& content, const char* site, const char* name) {
    const auto id = content.FindBuildingPattern(site);
    const auto item = content.FindItem(name);
    REQUIRE(id.value);
    REQUIRE(item.value);
    const auto& pattern = content.GetBuildingPattern(id);
    if (PopulationContains(content, pattern.population, item)) { return true; }
    for (const auto& basement : pattern.basements) {
        if (PopulationContains(content, basement.population, item)) { return true; }
    }
    return false;
}

TileCoord Rotate(TileCoord p, int width, int height, unsigned turns) {
    for (unsigned i = 0; i < turns; ++i) {
        const int x = p.x;
        p.x = height - 1 - p.z;
        p.z = x;
        eastl::swap(width, height);
    }
    return p;
}

TEST_CASE("Workshop acquisition sources are placed and ground bounded", "[content][workshop][w3][acquisition]") {
    Diagnostics diagnostics;
    const auto loaded = LoadContentPack(CATACLYSMX_CONTENT_PACK, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str());
    REQUIRE(loaded);
    const auto& content = *loaded;
    for (const char* name : {"loot_forging_hardware_ground","loot_forging_workshop_ground",
             "loot_forging_auto_ground","loot_forging_camping_ground","loot_forging_storage_ground",
             "loot_welding_hardware_ground","loot_welding_workshop_ground",
             "loot_workshop_electrical_ground","loot_sunglasses_ground"}) {
        INFO("pool=" << name);
        const auto pool = content.FindLootPool(name);
        REQUIRE(pool.value);
        CHECK(PoolMaximum(content, pool) <= 4);
    }
    struct Route { const char* site; const char* item; };
    constexpr Route routes[] = {
        {"hardware_store","solid_steel_block"},{"hardware_store","refractory_brick"},
        {"hardware_store","charcoal_charge"},{"sporting_goods_store","charcoal_charge"},
        {"workshop","solid_steel_block"},{"workshop","refractory_brick"},{"workshop","charcoal_charge"},
        {"workshop","crude_anvil_kit"},{"workshop","fabricated_anvil_kit"},
        {"workshop","forging_tongs"},{"workshop","smithing_hammer"},
        {"workshop","spring_steel_bar"},{"auto_repair","spring_steel_bar"},
        {"workshop","tool_steel_blank"},{"warehouse","wooden_workshop_crate"},
        {"hardware_store","welding_filter"},{"hardware_store","welding_electrode_packet"},
        {"workshop","welding_filter"},{"workshop","welding_mask"},{"workshop","welding_electrode_packet"},
        {"auto_repair","welding_filter"},{"auto_repair","welding_mask"},
        {"auto_repair","welding_electrode_packet"},{"auto_repair","salvaged_welder_power_module"},
        {"workshop","salvaged_welder_power_module"},{"workshop","arc_welder"},{"auto_repair","arc_welder"},
        {"electronics_store","electrical_lead_set"},{"electronics_store","aa_rechargeable_battery"},
        {"electronics_store","workshop_battery_pack"},{"workshop","workshop_battery_pack"},
        {"clothing_store","sunglasses"},{"sporting_goods_store","sunglasses"},
    };
    for (const auto& route : routes) {
        INFO("site=" << route.site << " item=" << route.item);
        CHECK(SiteContains(content, route.site, route.item));
    }
    const auto house = LoadContentPack(CATACLYSMX_FIRST_HOUSE_PACK, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str());
    REQUIRE(house);
    for (const char* item : {"charcoal_charge", "solid_steel_block", "arc_welder"}) {
        INFO("FirstHouse item=" << item);
        CHECK(SiteContains(*house, "furnished_house", item));
    }
    // New child pools never enter a furniture destination, including through shared pools.
    for (const char* site : {"furnished_house","hardware_store","workshop","auto_repair",
             "electronics_store","clothing_store","sporting_goods_store","warehouse"}) {
        const auto& pattern = content.GetBuildingPattern(content.FindBuildingPattern(site));
        const auto check = [&](const PatternPopulation& population) {
            for (const auto& marker : population.loot) {
                if (marker.destination != LootDestination::Furniture) { continue; }
                for (const char* item : {"solid_steel_block","charcoal_charge","arc_welder",
                         "welding_filter","electrical_lead_set","sunglasses"}) {
                    INFO("site=" << site << " item=" << item);
                    CHECK_FALSE(PoolContains(content, marker.pool, content.FindItem(item)));
                }
            }
        };
        check(pattern.population);
        for (const auto& basement : pattern.basements) { check(basement.population); }
    }
}

TEST_CASE("Workshop stations have a common reachable approach through every rotation",
    "[content][workshop][w3][acquisition][layout]") {
    const auto loaded = LoadContentPack(CATACLYSMX_CONTENT_PACK);
    REQUIRE(loaded);
    const auto& content = *loaded;
    const auto& pattern = content.GetBuildingPattern(content.FindBuildingPattern("workshop"));
    const auto anvil = content.FindObject("workshop_anvil");
    const auto forge = content.FindObject("charcoal_forge");
    bool hasAnvil = false, hasForge = false;
    for (const auto& marker : pattern.population.furniture) {
        hasAnvil |= marker.type == anvil && marker.position == TileCoord{3,7,0};
        hasForge |= marker.type == forge && marker.position == TileCoord{5,7,0};
    }
    REQUIRE(hasAnvil);
    REQUIRE(hasForge);
    for (unsigned turn = 0; turn < 4; ++turn) {
        INFO("rotation=" << turn);
        const int width = turn % 2 ? pattern.height : pattern.width;
        const int height = turn % 2 ? pattern.width : pattern.height;
        Vector<std::uint8_t> walk(static_cast<std::size_t>(width * height), 0);
        for (int z = 0; z < pattern.height; ++z) {
            for (int x = 0; x < pattern.width; ++x) {
                const auto& terrain = content.GetTerrain(pattern.tiles[static_cast<std::size_t>(z * pattern.width + x)]);
                bool passable = terrain.passable && !terrain.openSpace;
                if (!passable && terrain.opensTo.value) {
                    const auto& opened = content.GetTerrain(terrain.opensTo);
                    passable = opened.passable && !opened.openSpace;
                }
                const auto p = Rotate({x,z,0}, pattern.width, pattern.height, turn);
                walk[static_cast<std::size_t>(p.z * width + p.x)] = passable;
            }
        }
        for (const auto& marker : pattern.population.furniture) {
            if (marker.position.y != 0 || !content.GetObject(marker.type).blocksMovement) { continue; }
            const auto p = Rotate(marker.position, pattern.width, pattern.height, turn);
            walk[static_cast<std::size_t>(p.z * width + p.x)] = 0;
        }
        const auto start = Rotate({pattern.entrance.x,pattern.entrance.z,0}, pattern.width, pattern.height, turn);
        const auto approach = Rotate({4,7,0}, pattern.width, pattern.height, turn);
        Vector<TileCoord> queue;
        const auto index = [width](TileCoord p) { return static_cast<std::size_t>(p.z * width + p.x); };
        REQUIRE(walk[index(start)]);
        queue.push_back(start);
        walk[index(start)] = 0;
        bool reached = false;
        for (std::size_t i = 0; i < queue.size(); ++i) {
            const auto p = queue[i];
            reached |= p == approach;
            for (const TileCoord d : {TileCoord{1,0,0}, TileCoord{-1,0,0}, TileCoord{0,1,0}, TileCoord{0,-1,0}}) {
                const TileCoord next{p.x+d.x,p.z+d.z,0};
                if (next.x < 0 || next.z < 0 || next.x >= width || next.z >= height || !walk[index(next)]) { continue; }
                walk[index(next)] = 0;
                queue.push_back(next);
            }
        }
        CHECK(reached);
    }
}

TEST_CASE("Generated town workshop stock and cold forge have traceable sites",
    "[content][workshop][w3][acquisition][worldgen]") {
    const auto loaded = LoadContentPack(CATACLYSMX_CONTENT_PACK);
    REQUIRE(loaded);
    const auto& content = *loaded;
    struct Evidence { const char* item; unsigned count; std::uint32_t seed; TileCoord position; String site; };
    Evidence evidence[] = {
        {"solid_steel_block",0,0,{}},{"refractory_brick",0,0,{}},{"charcoal_charge",0,0,{}},
        {"spring_steel_bar",0,0,{}},{"forging_tongs",0,0,{}},{"smithing_hammer",0,0,{}},
        {"welding_filter",0,0,{}},{"welding_electrode_packet",0,0,{}},{"welding_mask",0,0,{}},
        {"salvaged_welder_power_module",0,0,{}},{"electrical_lead_set",0,0,{}},
        {"arc_welder",0,0,{}},{"sunglasses",0,0,{}},{"workshop_battery_pack",0,0,{}},
    };
    bool checkedForge = false;
    ChunkPlacements placements;
    for (std::uint32_t seed = 0; seed < 32; ++seed) {
        const auto plan = GenerateRegionPlan(content, seed, {0,0});
        Vector<RegionPlan> neighboringPlans;
        for (const auto& building : plan.buildings) {
            const auto& pattern = content.GetBuildingPattern(building.pattern);
            const auto first = ChunkOf(building.bounds.min);
            const auto last = ChunkOf({building.bounds.max.x-1,building.bounds.max.z-1});
            for (int y = -1; y <= 0; ++y) {
                for (int z = first.z; z <= last.z; ++z) {
                    for (int x = first.x; x <= last.x; ++x) {
                        const auto region = RegionOf({x * chunk_size, z * chunk_size});
                        const RegionPlan* chunkPlan = &plan;
                        if (region != plan.region) {
                            chunkPlan = nullptr;
                            for (const auto& neighbor : neighboringPlans) {
                                if (neighbor.region == region) { chunkPlan = &neighbor; break; }
                            }
                            if (!chunkPlan) {
                                neighboringPlans.push_back(GenerateRegionPlan(content, seed, region));
                                chunkPlan = &neighboringPlans.back();
                            }
                        }
                        GenerateChunkPlacements(content, seed, *chunkPlan, {x,z,y}, placements);
                        for (const auto& item : placements.items) {
                            if (!(item.key.owner == building.owner)) { continue; }
                            for (auto& e : evidence) {
                                if (item.type != content.FindItem(e.item)) { continue; }
                                if (!e.count) { e.seed = seed; e.position = item.position; e.site = pattern.id; }
                                ++e.count;
                            }
                        }
                        if (checkedForge || pattern.id != "workshop") { continue; }
                        for (const auto& object : placements.furniture) {
                            if (!(object.key.owner == building.owner) || object.type != content.FindObject("charcoal_forge")) { continue; }
                            INFO("seed=" << seed << " site=workshop position=(" << object.position.x << ','
                                << object.position.z << ',' << object.position.y << ')');
                            World world(content, seed);
                            REQUIRE(world.RequestChunks({ChunkOf(object.position), 1}, LoadMode::active));
                            REQUIRE(world.ReconcileChunks());
                            const auto handle = world.ObjectAt(object.position);
                            REQUIRE(handle.value);
                            const auto state = world.DescribeObjectState(handle);
                            REQUIRE(state);
                            CHECK(state->behavior == ObjectBehaviorKind::Forge);
                            CHECK(state->forge.fuel == 0);
                            CHECK_FALSE(state->forge.ready);
                            checkedForge = true;
                        }
                    }
                }
            }
        }
    }
    CHECK(checkedForge);
    for (const auto& e : evidence) {
        INFO("item=" << e.item << " count=" << e.count << " first seed=" << e.seed << " site=" << e.site.c_str()
            << " position=(" << e.position.x << ',' << e.position.z << ',' << e.position.y << ')');
        CHECK(e.count > 0);
    }
}
}
