#include "content/content.h"
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <cstdio>
#include <vector>

namespace {
using namespace cataclysmx;
namespace fs = std::filesystem;

struct PackFiles {
    fs::path directory;
    PackFiles() {
        directory = fs::temp_directory_path() / "cataclysmx_content_pack_tests";
        fs::create_directories(directory);
    }
    ~PackFiles() { fs::remove_all(directory); }
    void Write(const char* name, const char* text) {
        const auto path = (directory / name).string();
        std::FILE* file = nullptr;
#ifdef _MSC_VER
        fopen_s(&file, path.c_str(), "wb");
#else
        file = std::fopen(path.c_str(), "wb");
#endif
        REQUIRE(file != nullptr);
        const auto length = std::char_traits<char>::length(text);
        const auto written = std::fwrite(text, 1, length, file);
        const auto closed = std::fclose(file);
        REQUIRE(written == length);
        REQUIRE(closed == 0);
    }
    std::string Path(const char* name) const { return (directory / name).string(); }
};

TEST_CASE("Content packs resolve forward references relative to the manifest", "[content][pack]") {
    PackFiles files;
    files.Write("one.json5", "{format_version:1,definitions:[{type:'terrain',id:'child',name:'child',movement:{passable:true},blocks_sight:false,looks_like:'parent'}]}");
    files.Write("two.json5", "{format_version:1,definitions:[{type:'terrain',id:'parent',name:'parent',movement:{passable:true},blocks_sight:false,appearance:{symbol:'.',foreground:'white'}}]}");
    files.Write("pack.json5", "{format_version:1,files:['./one.json5','two.json5']}");
    auto pack = LoadContentPack(files.Path("pack.json5").c_str());
    REQUIRE(pack.has_value());
    CHECK(pack->GetTerrain(pack->FindTerrain("child")).appearance.symbol == ".");
    CHECK_FALSE(LoadContentFile(files.Path("pack.json5").c_str()).has_value());
    CHECK_FALSE(LoadContentFile(files.Path("one.json5").c_str()).has_value());
}

TEST_CASE("Content packs reject invalid manifests with field context", "[content][pack]") {
    PackFiles files;
    struct Example { const char* text; ContentError error; };
    const Example examples[] = {
        {"{format_version:1,files:[]}",ContentError::InvalidData},
        {"{format_version:2,files:['a']}",ContentError::UnsupportedVersion},
        {"{format_version:'1',files:['a']}",ContentError::InvalidData},
        {"{format_version:1,files:['a'],files:['b']}",ContentError::InvalidData},
        {"{format_version:1,files:['a'],extra:0}",ContentError::InvalidData},
        {"{format_version:1,files:['a','./a']}",ContentError::InvalidData},
        {"{format_version:1,files:['a/../b']}",ContentError::InvalidData},
        {"{format_version:1,files:['/absolute']}",ContentError::InvalidData},
        {"{format_version:1,files:['C:/absolute']}",ContentError::InvalidData},
        {"{format_version:1,files:['a\\u0000b']}",ContentError::InvalidData},
        {"{format_version:1,files:[42]}",ContentError::InvalidData},
    };
    for (const auto& example : examples) {
        INFO(example.text);
        files.Write("pack.json5", example.text);
        Diagnostics diagnostics;
        const auto result = LoadContentPack(files.Path("pack.json5").c_str(), &diagnostics);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error() == example.error);
        REQUIRE_FALSE(diagnostics.Empty());
        REQUIRE(diagnostics.Frames().front().location.has_value());
        CHECK_FALSE(diagnostics.Frames().front().location->path.empty());
    }
}

TEST_CASE("Bundled house has matched cellar stairs and accessible placements", "[content][pack][house]") {
    auto result = LoadContentPack(CATACLYSMX_CONTENT_PACK);
    REQUIRE(result.has_value());
    const auto& catalog = *result;
    const auto id = catalog.FindBuildingPattern("furnished_house");
    REQUIRE(id.value != 0);
    const auto& house = catalog.GetBuildingPattern(id);
    REQUIRE(house.basements.size() == 2);
    CHECK(house.basements[0].id == "storage_cellar");
    CHECK(house.basements[1].id == "workshop_cellar");
    const auto stairs = catalog.FindTerrain("stairs");
    std::size_t stairsCount = 0;
    for (std::size_t i = 0; i < house.tiles.size(); ++i) {
        if (house.tiles[i] != stairs) { continue; }
        ++stairsCount;
        for (const auto& cellar : house.basements) {
            REQUIRE(cellar.layer.tiles.size() == house.tiles.size());
            CHECK(cellar.layer.tiles[i] == stairs);
        }
    }
    CHECK(stairsCount == 1);
    CHECK(catalog.FindCraftingRecipe("cut_house_bandage").value != 0);
    CHECK(catalog.FindMonster("cellar_zombie").value != 0);
    CHECK(house.population.furniture.size() >= 7);
    CHECK(house.population.loot.size() >= 4);
    const auto lamp = catalog.FindObject("cellar_lamp");
    REQUIRE(lamp.value != 0);
    CHECK(catalog.GetObject(lamp).kind == ObjectKind::Lamp);
    CHECK(catalog.GetObject(lamp).movable);
    CHECK_FALSE(catalog.GetObject(lamp).blocksMovement);
    for (const auto& cellar : house.basements) {
        bool found = false;
        for (const auto& furniture : cellar.population.furniture) {
            if (furniture.type == lamp && furniture.position.x == 4 &&
                furniture.position.z == 5 && furniture.position.y == -1) { found = true; }
        }
        CHECK(found);
    }
    const auto reachable = [&](const auto& tiles, const auto& population, int startX, int startZ) {
        const int width = house.width, height = house.height;
        std::vector<bool> visited(static_cast<std::size_t>(width * height), false);
        std::vector<int> queue;
        const int start = startZ * width + startX;
        visited[start] = true;
        queue.push_back(start);
        for (std::size_t cursor = 0; cursor < queue.size(); ++cursor) {
            const int x = queue[cursor] % width, z = queue[cursor] / width;
            constexpr int dx[] = {1, -1, 0, 0}, dz[] = {0, 0, 1, -1};
            for (int direction = 0; direction < 4; ++direction) {
                const int nx = x + dx[direction], nz = z + dz[direction];
                if (nx < 0 || nz < 0 || nx >= width || nz >= height) { continue; }
                const int index = nz * width + nx;
                const auto& terrain = catalog.GetTerrain(tiles[index]);
                if (visited[index] || (!terrain.passable && terrain.opensTo.value == 0)) { continue; }
                bool blocked = false;
                for (const auto& furniture : population.furniture) {
                    if (furniture.position.x == nx && furniture.position.z == nz &&
                        catalog.GetObject(furniture.type).blocksMovement) { blocked = true; }
                }
                if (!blocked) { visited[index] = true; queue.push_back(index); }
            }
        }
        for (int z = 0; z < height; ++z) {
            for (int x = 0; x < width; ++x) {
                const int index = z * width + x;
                const auto& terrain = catalog.GetTerrain(tiles[index]);
                if (!terrain.passable) { continue; }
                bool blocked = false;
                for (const auto& furniture : population.furniture) {
                    if (furniture.position.x == x && furniture.position.z == z &&
                        catalog.GetObject(furniture.type).blocksMovement) { blocked = true; }
                }
                if (!blocked) { CHECK(visited[index]); }
            }
        }
    };
    reachable(house.tiles, house.population, house.entrance.x, house.entrance.z);
    for (const auto& cellar : house.basements) {
        reachable(cellar.layer.tiles, cellar.population, 3, 6);
    }
}
}
