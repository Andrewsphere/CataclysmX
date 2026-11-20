#include "core/first_house_scenario.h"
#include "worldgen/worldgen.h"

#include <EASTL/algorithm.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <limits>

namespace {
using namespace cataclysmx;

// Disposable plans and measurement buffers only: no Server, runtime entities or
// simulation advancement. Standard filesystem/string use stays at the IO boundary.
struct File {
    std::FILE* stream = nullptr;
    ~File() { if (stream) { std::fclose(stream); } }
    File() = default;
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    bool Open(const std::filesystem::path& directory, const char* name) {
        stream = std::fopen((directory / name).string().c_str(), "wb");
        return stream != nullptr;
    }
    bool Finish() {
        if (!stream) { return false; }
        const bool good = std::ferror(stream) == 0;
        const bool closed = std::fclose(stream) == 0;
        stream = nullptr;
        return good && closed;
    }
};

struct Sample {
    SettlementDescriptor descriptor;
    bool originPatch = false;
    bool panel = false;
    bool example = false;
    bool starting = false;
};
bool Overlap(TileRect a, TileRect b) {
    return a.min.x < b.max.x && b.min.x < a.max.x
        && a.min.z < b.max.z && b.min.z < a.max.z;
}
void Add(Vector<Sample>& samples, const SettlementDescriptor& descriptor,
    bool origin, bool panel, bool example, bool starting) {
    for (auto& sample : samples) {
        if (sample.descriptor.id != descriptor.id) { continue; }
        sample.originPatch |= origin;
        sample.panel |= panel;
        sample.example |= example;
        sample.starting |= starting;
        return;
    }
    samples.push_back({descriptor, origin, panel, example, starting});
}
const char* Scale(SettlementScale scale) {
    switch (scale) {
    case SettlementScale::Small: return "small";
    case SettlementScale::Medium: return "medium";
    case SettlementScale::Large: return "large";
    }
    std::abort();
}
const char* Era(SettlementEra era) {
    switch (era) {
    case SettlementEra::Older: return "older";
    case SettlementEra::Mixed: return "mixed";
    case SettlementEra::Newer: return "newer";
    }
    std::abort();
}
struct PlanCache {
    const ContentCatalog& content;
    std::uint64_t seed;
    Vector<RegionPlan> regions;
    const RegionPlan& Get(RegionCoord coord) {
        for (const auto& region : regions) {
            if (region.region == coord) { return region; }
        }
        regions.push_back(GenerateRegionPlan(content, seed, coord));
        return regions.back();
    }
};
struct Cell {
    int distance = -1;
    bool walkable = false;
    bool stairs = false;
};
struct Count {
    std::uint32_t total = 0;
    std::uint32_t reachable = 0;
    int steps = -1;
};
struct SiteScratch {
    Vector<Cell> cells;
    Vector<int> queue;
    Vector<GeneratedItem> items;
    Vector<GeneratedFurniture> furniture;
    Vector<Count> itemCounts;
    Vector<Count> objectCounts;
    ChunkPlacements placements;
    TileCoord minimum{};
    int width = 0;
    int depth = 0;
    int floors = 0;

    bool Inside(TileCoord p) const {
        return p.x >= minimum.x && p.x < minimum.x + width
            && p.z >= minimum.z && p.z < minimum.z + depth
            && p.y >= minimum.y && p.y < minimum.y + floors;
    }
    int Index(TileCoord p) const {
        return ((p.y - minimum.y) * depth + p.z - minimum.z) * width + p.x - minimum.x;
    }
    TileCoord Position(int index) const {
        return {minimum.x + index % width, minimum.z + (index / width) % depth,
            minimum.y + index / (width * depth)};
    }
    void Prepare(const ContentCatalog& content, PlanCache& cache, const BuildingPlan& building) {
        const auto& pattern = content.GetBuildingPattern(building.pattern);
        int low = building.basementVariant ? -1 : 0, high = 0;
        for (const auto& layer : pattern.layers) {
            low = eastl::min(low, layer.y);
            high = eastl::max(high, layer.y);
        }
        // An exterior ring admits secondary doors reachable around the building.
        minimum = {building.bounds.min.x - 1, building.bounds.min.z - 1, low};
        width = building.bounds.max.x - building.bounds.min.x + 2;
        depth = building.bounds.max.z - building.bounds.min.z + 2;
        floors = high - low + 1;
        cells.assign(static_cast<std::size_t>(width * depth * floors), {});
        items.clear();
        furniture.clear();
        queue.clear();
        itemCounts.assign(content.ItemCount() + 1, {});
        objectCounts.assign(content.ObjectCount() + 1, {});
        const auto first = ChunkOf(minimum);
        const auto last = ChunkOf({minimum.x + width - 1, minimum.z + depth - 1, high});
        for (int y = low; y <= high; ++y) {
            for (int z = first.z; z <= last.z; ++z) {
                for (int x = first.x; x <= last.x; ++x) {
                    const ChunkCoord chunk{x, z, y};
                    const auto& plan = cache.Get(RegionOf({x * chunk_size, z * chunk_size}));
                    const auto tiles = GenerateChunk(content, cache.seed, plan, chunk);
                    GenerateChunkPlacements(content, cache.seed, plan, chunk, placements);
                    for (int localZ = 0; localZ < chunk_size; ++localZ) {
                        for (int localX = 0; localX < chunk_size; ++localX) {
                            const TileCoord position{x * chunk_size + localX, z * chunk_size + localZ, y};
                            if (!Inside(position)) { continue; }
                            auto& cell = cells[Index(position)];
                            const auto& terrain = content.GetTerrain(tiles[localZ * chunk_size + localX].terrain);
                            const auto& opened = terrain.opensTo.value ? content.GetTerrain(terrain.opensTo) : terrain;
                            cell.walkable = opened.passable && !opened.openSpace;
                            cell.stairs = terrain.stairs;
                        }
                    }
                    for (const auto& item : placements.items) {
                        if (item.key.owner == building.owner) { items.push_back(item); }
                    }
                    for (const auto& object : placements.furniture) {
                        if (object.key.owner == building.owner) { furniture.push_back(object); }
                        if (Inside(object.position) && content.GetObject(object.type).blocksMovement) {
                            cells[Index(object.position)].walkable = false;
                        }
                    }
                }
            }
        }
        if (!Inside(building.entrance) || !cells[Index(building.entrance)].walkable) { return; }
        const int start = Index(building.entrance);
        cells[start].distance = 0;
        queue.push_back(start);
        constexpr TileCoord directions[] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0},
            {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
        for (std::size_t i = 0; i < queue.size(); ++i) {
            const int current = queue[i];
            const auto position = Position(current);
            for (const auto direction : directions) {
                const TileCoord next{position.x + direction.x, position.z + direction.z, position.y + direction.y};
                if (!Inside(next)) { continue; }
                auto& destination = cells[Index(next)];
                if (!destination.walkable || destination.distance >= 0) { continue; }
                if (direction.y && !(cells[current].stairs && destination.stairs)) { continue; }
                destination.distance = cells[current].distance + 1;
                queue.push_back(Index(next));
            }
        }
    }
    int Access(TileCoord position, bool adjacent) const {
        if (!Inside(position)) { return -1; }
        int best = cells[Index(position)].distance;
        if (!adjacent) { return best; }
        constexpr TileCoord directions[] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
        for (const auto d : directions) {
            const TileCoord p{position.x + d.x, position.z + d.z, position.y};
            if (!Inside(p)) { continue; }
            const int distance = cells[Index(p)].distance;
            if (distance >= 0 && (best < 0 || distance < best)) { best = distance; }
        }
        return best;
    }
};
void AddCount(Count& count, int steps) {
    ++count.total;
    if (steps < 0) { return; }
    ++count.reachable;
    if (count.steps < 0 || steps < count.steps) { count.steps = steps; }
}
void Key(std::FILE* file, std::uint64_t seed, FeatureOwner owner) {
    std::fprintf(file, "%llu,%d,%d,%u,%u", static_cast<unsigned long long>(seed),
        owner.region.x, owner.region.z, owner.settlement, owner.parcel);
}
bool Excluded(TileCoord position, TileCoord start) {
    const auto dx = static_cast<std::int64_t>(position.x) - start.x;
    const auto dz = static_cast<std::int64_t>(position.z) - start.z;
    return position.y == start.y && dx * dx + dz * dz <= 16 * 16;
}
void MeasureSite(const ContentCatalog& content, std::uint64_t seed, const Sample& sample,
    const SettlementPlan& settlement, const BuildingPlan& building, TileCoord start,
    PlanCache& cache, SiteScratch& scratch, std::FILE* sites, std::FILE* stock) {
    scratch.Prepare(content, cache, building);
    std::uint64_t inaccessible = 0;
    for (const auto& item : scratch.items) {
        const int distance = scratch.Access(item.position, item.destination == LootDestination::Furniture);
        AddCount(scratch.itemCounts[item.type.value], distance);
        inaccessible += distance < 0;
    }
    for (const auto& object : scratch.furniture) {
        AddCount(scratch.objectCounts[object.type.value], scratch.Access(object.position, true));
    }
    std::uint32_t indoor = 0, outdoorNear = 0;
    for (const auto& monster : settlement.monsters) {
        if (Excluded(monster.position, start)) { continue; }
        if (monster.key.owner == building.owner
            && monster.key.kind == GenerationPlacementKind::IndoorPopulation) { ++indoor; }
        if (monster.key.kind != GenerationPlacementKind::OutdoorPopulation || monster.position.y) { continue; }
        const auto dx = static_cast<std::int64_t>(monster.position.x) - building.entrance.x;
        const auto dz = static_cast<std::int64_t>(monster.position.z) - building.entrance.z;
        if (dx * dx + dz * dz <= 16 * 16) { ++outdoorNear; }
    }
    const auto& pattern = content.GetBuildingPattern(building.pattern);
    const double dx = static_cast<double>(building.entrance.x) - start.x;
    const double dz = static_cast<double>(building.entrance.z) - start.z;
    Key(sites, seed, building.owner);
    std::fprintf(sites, ",%s,%s,%s,%d,%d,%d,%d,%d,%d,%u,%u,%.3f,%u,%u,%zu,%llu\n",
        pattern.id.c_str(), Scale(sample.descriptor.scale), Era(sample.descriptor.era),
        sample.originPatch, sample.panel, sample.example, sample.starting,
        building.entrance.x, building.entrance.z, building.basementVariant, building.quarterTurns,
        std::sqrt(dx * dx + dz * dz), indoor, outdoorNear, scratch.items.size(),
        static_cast<unsigned long long>(inaccessible));
    for (std::uint32_t i = 1; i < scratch.itemCounts.size(); ++i) {
        const auto& count = scratch.itemCounts[i];
        if (!count.total) { continue; }
        Key(stock, seed, building.owner);
        std::fprintf(stock, ",item,%s,%u,%u,%d\n", content.GetItem({i}).id.c_str(),
            count.total, count.reachable, count.steps);
    }
    for (std::uint32_t i = 1; i < scratch.objectCounts.size(); ++i) {
        const auto& count = scratch.objectCounts[i];
        if (!count.total) { continue; }
        Key(stock, seed, building.owner);
        std::fprintf(stock, ",object,%s,%u,%u,%d\n", content.GetObject({i}).id.c_str(),
            count.total, count.reachable, count.steps);
    }
}
template<class Traits>
void Providers(std::FILE* file, const ContentCatalog& content, const char* kind,
    const char* id, const Traits& traits) {
    for (const auto& trait : traits) {
        std::fprintf(file, "%s,%s,%s,%u\n", kind, id,
            content.GetToolTrait(trait.trait).id.c_str(), trait.tier);
    }
}
template<class Recipe>
void Requirements(std::FILE* file, const ContentCatalog& content,
    const char* kind, const Recipe& recipe) {
    for (const auto& input : recipe.inputs) {
        std::fprintf(file, "%s,%s,%s,%s,%u\n", kind, recipe.id.c_str(),
            input.Retained() ? "retained" : "consumed",
            content.GetItem(input.Item()).id.c_str(), input.Count());
    }
    for (const auto& skill : recipe.skills) {
        std::fprintf(file, "%s,%s,skill,%s,%u\n", kind, recipe.id.c_str(),
            content.GetSkill(skill.skill).id.c_str(), skill.level);
    }
}
bool Catalog(const ContentCatalog& content, const std::filesystem::path& directory) {
    File items, objects, providers, recipes, requirements;
    if (!items.Open(directory, "items.csv") || !objects.Open(directory, "objects.csv")
        || !providers.Open(directory, "providers.csv") || !recipes.Open(directory, "recipes.csv")
        || !requirements.Open(directory, "requirements.csv")) { return false; }
    std::fprintf(items.stream, "id,mass_g,volume_ml,placed_object,books,initial_energy,capacity_energy,crafting_energy_per_work\n");
    std::fprintf(objects.stream, "id,packing_item,storage_ml,ignition_heat,fuel_item,heat_per_fuel\n");
    std::fprintf(providers.stream, "kind,id,trait,tier\n");
    std::fprintf(recipes.stream, "kind,id,output,count,work,heat_per_work,energy,known_by_default,setup_work,setup_work_per_unit\n");
    std::fprintf(requirements.stream, "kind,recipe,requirement,id,count\n");
    for (std::uint32_t i = 1; i <= content.ItemCount(); ++i) {
        const auto& item = content.GetItem({i});
        Vector<BookId> books(item.books.begin(), item.books.end());
        for (const auto information : item.suppliedInformation) {
            for (const auto book : content.GetInformation(information).books) {
                if (eastl::find(books.begin(), books.end(), book) == books.end()) { books.push_back(book); }
            }
        }
        String names;
        for (const auto book : books) {
            if (!names.empty()) { names += ';'; }
            names += content.GetBook(book).id;
        }
        std::uint64_t initial = 0, capacity = 0;
        for (const auto& energy : item.energyStores) {
            initial += energy.config.initialEnergy;
            capacity += energy.config.capacityEnergy;
        }
        std::fprintf(items.stream, "%s,%u,%u,%s,%s,%llu,%llu,%u\n", item.id.c_str(), item.massGrams,
            item.volumeMl, item.placedObject.value ? content.GetObject(item.placedObject).id.c_str() : "",
            names.c_str(), static_cast<unsigned long long>(initial), static_cast<unsigned long long>(capacity),
            item.craftingEnergyPerWork);
        Providers(providers.stream, content, "item", item.id.c_str(), item.toolTraits);
    }
    for (std::uint32_t i = 1; i <= content.ObjectCount(); ++i) {
        const auto& object = content.GetObject({i});
        // The current forge has one fuel. Report its actual budget, rather than
        // duplicating heat constants in the downstream arithmetic.
        const bool oneFuel = object.forge.fuels.size() == 1;
        std::fprintf(objects.stream, "%s,%s,%u,%llu,%s,%llu\n", object.id.c_str(),
            object.packingItem.value ? content.GetItem(object.packingItem).id.c_str() : "",
            object.capacityVolumeMl.value_or(0), static_cast<unsigned long long>(object.forge.ignitionCost),
            oneFuel ? content.GetItem(object.forge.fuels[0].item).id.c_str() : "",
            oneFuel ? static_cast<unsigned long long>(object.forge.fuels[0].heat) : 0ULL);
        Providers(providers.stream, content, "object", object.id.c_str(), object.toolTraits);
    }
    for (std::uint32_t i = 1; i <= content.CraftingRecipeCount(); ++i) {
        const auto& recipe = content.GetCraftingRecipe({i});
        std::fprintf(recipes.stream, "craft,%s,%s,%u,%u,%u,0,%d,0,0\n", recipe.id.c_str(),
            content.GetItem(recipe.result.item).id.c_str(), recipe.result.count,
            recipe.work, recipe.heatPerWork, recipe.knownByDefault);
        Requirements(requirements.stream, content, "craft", recipe);
        for (const auto& trait : recipe.toolTraits) {
            std::fprintf(requirements.stream, "craft,%s,trait,%s,%u\n", recipe.id.c_str(),
                content.GetToolTrait(trait.trait).id.c_str(), trait.tier);
        }
        for (const auto& training : recipe.training) {
            std::fprintf(requirements.stream, "craft,%s,training_level,%s,%u\n", recipe.id.c_str(),
                content.GetSkill(training.skill).id.c_str(), training.level);
            std::fprintf(requirements.stream, "craft,%s,training_rate,%s,%u\n", recipe.id.c_str(),
                content.GetSkill(training.skill).id.c_str(), training.xpPerWork);
        }
    }
    for (std::uint32_t i = 1; i <= content.MachineRecipeCount(); ++i) {
        const auto& recipe = content.GetMachineRecipe({i});
        std::fprintf(recipes.stream, "machine,%s,%s,%u,%llu,0,%llu,0,%u,%u\n", recipe.id.c_str(),
            content.GetItem(recipe.result.item).id.c_str(), recipe.result.count,
            static_cast<unsigned long long>(recipe.duration),
            static_cast<unsigned long long>(recipe.duration) * recipe.energyPerTick,
            recipe.setupWork, recipe.setupWorkPerUnit);
        Requirements(requirements.stream, content, "machine", recipe);
        if (recipe.learning.skill.value) {
            std::fprintf(requirements.stream, "machine,%s,training_level,%s,%u\n", recipe.id.c_str(),
                content.GetSkill(recipe.learning.skill).id.c_str(), recipe.learning.level);
            std::fprintf(requirements.stream, "machine,%s,completion_xp,%s,%u\n", recipe.id.c_str(),
                content.GetSkill(recipe.learning.skill).id.c_str(), recipe.learning.xpPerExecution);
        }
    }
    return items.Finish() && objects.Finish() && providers.Finish()
        && recipes.Finish() && requirements.Finish();
}
int Run(const ContentCatalog& content, const std::filesystem::path& directory,
    eastl::optional<std::uint32_t> oneSeed) {
    if (!Catalog(content, directory)) { std::fprintf(stderr, "cannot write catalog\n"); return 1; }
    File sites, stock, starts;
    if (!sites.Open(directory, "sites.csv") || !stock.Open(directory, "stock.csv")
        || !starts.Open(directory, "starts.csv")) { std::fprintf(stderr, "cannot open reports\n"); return 1; }
    std::fprintf(sites.stream, "seed,owner_region_x,owner_region_z,settlement,parcel,pattern,scale,era,origin_patch,panel,example,starting_settlement,entrance_x,entrance_z,basement,rotation,start_distance,indoor,outdoor_near16,item_count,inaccessible_items\n");
    std::fprintf(stock.stream, "seed,owner_region_x,owner_region_z,settlement,parcel,kind,id,count,reachable,min_steps\n");
    std::fprintf(starts.stream, "seed,x,z,y,generation_version,sampled_settlements\n");
    bool examples[3][3]{};
    SiteScratch scratch;
    std::size_t totalSites = 0;
    for (std::uint32_t index = 0; index < (oneSeed ? 1u : 33u); ++index) {
        const std::uint32_t seed = oneSeed.value_or(index == 32 ? 42 : index);
        const auto began = std::chrono::steady_clock::now();
        Diagnostics diagnostics;
        const auto start = FindTownStart(content, seed, &diagnostics);
        if (!start) { std::fprintf(stderr, "start: %s\n", FormatDiagnostics(diagnostics).c_str()); return 1; }
        Vector<Sample> samples;
        const auto origin = GeneratePlanningPatch(seed, {0, 0});
        for (const auto& descriptor : origin.settlements) {
            const auto scale = static_cast<unsigned>(descriptor.scale);
            const auto era = static_cast<unsigned>(descriptor.era);
            const bool example = !examples[scale][era];
            examples[scale][era] = true;
            Add(samples, descriptor, true, false, example, Contains(descriptor.bounds, start->playerPosition));
        }
        const RegionCoord regions[] = {{0, 0}, {-1, 0}, {0, -1}, {1, 1}};
        for (const auto region : regions) {
            const TileRect bounds{{region.x * region_size, region.z * region_size},
                {(region.x + 1) * region_size, (region.z + 1) * region_size}};
            for (int dz = -1; dz <= 1; ++dz) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const auto patch = GeneratePlanningPatch(seed,
                        {region.x + dx * planning_patch_regions, region.z + dz * planning_patch_regions});
                    for (const auto& descriptor : patch.settlements) {
                        if (Overlap(bounds, descriptor.bounds)) {
                            Add(samples, descriptor, false, true, false, Contains(descriptor.bounds, start->playerPosition));
                        }
                    }
                }
            }
        }
        bool foundStart = false;
        const auto startRegion = RegionOf(start->playerPosition);
        for (int dz = -1; dz <= 1; ++dz) {
            for (int dx = -1; dx <= 1; ++dx) {
                const auto patch = GeneratePlanningPatch(seed, {startRegion.x + dx * planning_patch_regions,
                    startRegion.z + dz * planning_patch_regions});
                for (const auto& descriptor : patch.settlements) {
                    if (!Contains(descriptor.bounds, start->playerPosition)) { continue; }
                    Add(samples, descriptor, false, false, false, true);
                    foundStart = true;
                }
            }
        }
        if (!foundStart) { std::fprintf(stderr, "start has no settlement\n"); return 1; }
        std::fprintf(starts.stream, "%u,%d,%d,%d,%u,%zu\n", seed, start->playerPosition.x,
            start->playerPosition.z, start->playerPosition.y, world_generation_version, samples.size());
        PlanCache cache{content, seed, {}};
        for (const auto& sample : samples) {
            const auto plan = GenerateSettlementPlan(content, seed, sample.descriptor);
            for (const auto& building : plan.buildings) {
                MeasureSite(content, seed, sample, plan, building, start->playerPosition,
                    cache, scratch, sites.stream, stock.stream);
                ++totalSites;
            }
        }
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
        std::fprintf(stderr, "seed=%u settlements=%zu cached_regions=%zu elapsed_s=%.3f\n",
            seed, samples.size(), cache.regions.size(), seconds);
    }
    if (!oneSeed) {
        for (const auto& row : examples) {
            for (const bool found : row) {
                if (!found) { std::fprintf(stderr, "missing size/era example\n"); return 1; }
            }
        }
    }
    if (!sites.Finish() || !stock.Finish() || !starts.Finish()) {
        std::fprintf(stderr, "report write failed\n"); return 1;
    }
    std::fprintf(stderr, "sites=%zu generation=%u; pure generation, cardinal structural access, Town safety radius=16\n",
        totalSites, world_generation_version);
    return 0;
}
}

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) {
        std::fprintf(stderr, "usage: cataclysmx_workshop_probe output_directory [seed]\n"); return 2;
    }
    eastl::optional<std::uint32_t> seed;
    if (argc == 3) {
        char* end = nullptr;
        if (!*argv[2] || *argv[2] == '-') { std::fprintf(stderr, "invalid seed\n"); return 2; }
        const auto value = std::strtoull(argv[2], &end, 10);
        if (*end || value > std::numeric_limits<std::uint32_t>::max()) {
            std::fprintf(stderr, "invalid seed\n"); return 2;
        }
        seed = static_cast<std::uint32_t>(value);
    }
    const std::filesystem::path directory(argv[1]);
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) { std::fprintf(stderr, "output directory: %s\n", error.message().c_str()); return 1; }
    Diagnostics diagnostics;
    const auto content = LoadContentPack(CATACLYSMX_CONTENT_PACK, &diagnostics);
    if (!content || !ValidateWorldgenContent(*content, &diagnostics)) {
        std::fprintf(stderr, "content: %s\n", FormatDiagnostics(diagnostics).c_str()); return 1;
    }
    return Run(*content, directory, seed);
}
