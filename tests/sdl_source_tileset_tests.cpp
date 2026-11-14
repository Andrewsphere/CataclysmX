#include "content/tileset_source.h"
#include "sdl/atlas_builder.h"
#include "sdl/renderer.h"
#include "content_fixture.h"

#include <catch2/catch_test_macros.hpp>
#include <SDL3_image/SDL_image.h>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>

namespace {
using namespace cataclysmx;
using namespace cataclysmx::sdl;
namespace fs = std::filesystem;

String Manifest(const char* mappings = "") {
    String result = "{format_version:2,id:'test',name:'Test',cell_width:2,cell_height:2,";
    result += mappings; result += '}'; return result;
}
ContentResult<TilesetDefinition> Parse(const Vector<SpriteSourceFile>& files, const char* mappings = "", Diagnostics* report = nullptr) {
    const auto manifest = Manifest(mappings);
    return ParseSourceTileset(manifest, {files.data(), files.size()}, TestContent(), report);
}
Vector<SpriteSourceFile> ConnectedSources() {
    Vector<SpriteSourceFile> files;
    const char* names[] = {"isolated", "end", "straight", "corner", "tee", "cross"};
    String group = "{kind:'connections',connects_to:['road'],shapes:{";
    for (auto* name : names) {
        String path = "tile/road/"; path += name; path += ".png";
        files.push_back({path, "{anchor:[1,1],cell_anchor:[1,1]}", 2, 2});
        group += name; group += ":{visual:'tile.road."; group += name; group += "'},";
    }
    group += "}}";
    files.push_back({"tile/road/connected.meta", group});
    return files;
}

struct Fixture {
    fs::path root;
    SDL_Surface* surface = nullptr;
    SDL_Renderer* renderer = nullptr;
    Fixture() {
        root = fs::temp_directory_path() / ("catax-source-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        REQUIRE(fs::create_directories(root / "tile")); REQUIRE(fs::create_directories(root / "sprite"));
        REQUIRE(SDL_Init(0));
        surface = SDL_CreateSurface(800, 600, SDL_PIXELFORMAT_RGBA32); REQUIRE(surface);
        renderer = SDL_CreateSoftwareRenderer(surface); REQUIRE(renderer);
    }
    ~Fixture() {
        SDL_DestroyRenderer(renderer); SDL_DestroySurface(surface); SDL_Quit();
        std::error_code error; fs::remove_all(root, error);
    }
    String Directory() const { const auto p = root.string(); return String(p.data(), p.size()); }
    void Write(const char* path, const String& text) {
        std::ofstream file(root / path, std::ios::binary); REQUIRE(file.good()); file.write(text.data(), text.size()); REQUIRE(file.good());
    }
    void Image(const char* path, int width = 2, int height = 2, Uint8 alpha = 255) {
        auto* image = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32); REQUIRE(image);
        REQUIRE(SDL_FillSurfaceRect(image, nullptr, SDL_MapSurfaceRGBA(image, 255, 0, 0, alpha)));
        const SDL_Rect topLeft{0, 0, 1, 1};
        REQUIRE(SDL_FillSurfaceRect(image, &topLeft, SDL_MapSurfaceRGBA(image, 0, 255, 0, alpha)));
        REQUIRE(SDL_SavePNG(image, (root / path).string().c_str())); SDL_DestroySurface(image);
    }
};
struct Wake {
    std::mutex mutex; std::condition_variable condition; bool ready = false;
    static void Signal(void* data) {
        auto& self = *static_cast<Wake*>(data);
        { std::lock_guard lock(self.mutex); self.ready = true; } self.condition.notify_one();
    }
    AssetState Finish(SdlAssets& assets, SDL_Renderer* renderer) {
        std::unique_lock lock(mutex);
        REQUIRE(condition.wait_for(lock, std::chrono::seconds(10), [&] { return ready; })); lock.unlock();
        for (unsigned i = 0; i < 66 && assets.State() == AssetState::Loading; ++i) { assets.Poll(renderer); }
        return assets.State();
    }
};
void Pixel(SDL_Surface* surface, int x, int y, Uint8 expectedRed, Uint8 expectedGreen, Uint8 expectedBlue = 0) {
    Uint8 r, g, b, a; REQUIRE(SDL_ReadSurfacePixel(surface, x, y, &r, &g, &b, &a));
    CHECK(r == expectedRed); CHECK(g == expectedGreen); CHECK(b == expectedBlue);
}
}

TEST_CASE("source packs sort package IDs and deterministically pack frames with borders", "[sdl][tileset][source]") {
    Vector<SpriteSourceFile> files{{"sprite/zombie/default.png", "", 3, 7}, {"tile/grass/meadow.png", "", 2, 2}};
    auto first = Parse(files); REQUIRE(first);
    std::reverse(files.begin(), files.end()); auto second = Parse(files); REQUIRE(second);
    REQUIRE(first->sprites.size() == 2); REQUIRE(first->atlases.size() == 1);
    for (unsigned i = 0; i < 2; ++i) {
        const auto& a = first->sprites[i]; const auto& b = second->sprites[i];
        CHECK(a.id == b.id); CHECK(a.x == b.x); CHECK(a.y == b.y); CHECK(a.atlasIndex == b.atlasIndex);
        CHECK(a.x >= 1); CHECK(a.y >= 1);
        CHECK(a.x + a.width < first->atlases[a.atlasIndex].width);
        CHECK(a.y + a.height < first->atlases[a.atlasIndex].height);
    }
    CHECK(first->sprites[0].id == "sprite.zombie.default"); CHECK(first->sprites[0].anchorX == 1.5f);
    CHECK(first->sprites[0].anchorY == 7); CHECK(first->sprites[0].layer == SpriteLayer::Upright);
    CHECK(first->sprites[1].layer == SpriteLayer::Ground);
    auto prefixes = Parse({{"tile/a/b.png", {}, 2, 2}, {"tile/a-b.png", {}, 2, 2}, {"tile/a.png", {}, 2, 2}}); REQUIRE(prefixes);
    CHECK(prefixes->sprites[0].id == "tile.a"); CHECK(prefixes->sprites[1].id == "tile.a-b"); CHECK(prefixes->sprites[2].id == "tile.a.b");
    Vector<SpriteSourceFile> pages;
    for (unsigned i = 0; i < 18; ++i) { char path[64]; std::snprintf(path, sizeof(path), "tile/image_%02u.png", i); pages.push_back({path, {}, 512, 512}); }
    auto multiple = Parse(pages); REQUIRE(multiple); CHECK(multiple->atlases.size() == 2);
    auto large = Parse({{"tile/large.png", {}, 2047, 2}}); REQUIRE(large);
    CHECK(large->atlases[0].width == 2049); CHECK(large->atlases[0].height == 4);
    Vector<SpriteSourceFile> excessive;
    for (unsigned i = 0; i < 5; ++i) { char path[64]; std::snprintf(path, sizeof(path), "tile/large_%u.png", i); excessive.push_back({path, {}, 2046, 2046}); }
    CHECK_FALSE(Parse(excessive)); // Padded CPU atlas pages exceed 64 MiB.
}

TEST_CASE("source clips mix separate PNGs and uniform sheet frames", "[sdl][tileset][source]") {
    Vector<SpriteSourceFile> files{
        {"sprite/off.png", {}, 2, 2}, {"sprite/sheet.png", "{frame_width:2,frame_height:2}", 4, 2},
        {"sprite/blink.meta", "{kind:'animation',frames:[{sprite:'sprite.sheet',frame:1,duration_ms:10},{sprite:'sprite.off',duration_ms:20}]}"}
    };
    auto d = Parse(files, "items:{toggle_lightbulb:{visual:'sprite.off',activation:{animation:'sprite.blink',mode:'toggle_loop'}}}"); REQUIRE(d);
    REQUIRE(d->animations.size() == 1); REQUIRE(d->sprites.size() == 3);
    const auto& clip = d->animations[0]; CHECK(d->sprites[SampleAnimation(clip, 0).spriteIndex].id == "sprite.sheet@1");
    CHECK(d->sprites[SampleAnimation(clip, 10).spriteIndex].id == "sprite.off"); CHECK(SampleAnimation(clip, 30).spriteIndex == clip.frames[0].spriteIndex);
    const auto& mapping = d->items[TestItem("toggle_lightbulb").value]; REQUIRE(mapping.activation); CHECK(mapping.activation->animationIndex == 0);
    auto rotated = mapping; rotated.transform = {1, true, false};
    SpritePlaybackController playback; REQUIRE(playback.Activate(7, rotated, *d, 0) == SpriteActivationResult::LoopStarted);
    CHECK(playback.Sample(7, rotated, *d, 0).mapping.transform == rotated.transform);
    CHECK(playback.Sample(7, rotated, *d, 30).mapping.transform == rotated.transform);
}

TEST_CASE("source animation frames and variants can exceed inline capacity", "[sdl][tileset][source]") {
    auto d = Parse({{"tile/a.png", {}, 2, 2}, {"tile/b.png", {}, 2, 2},
        {"tile/clip.meta", "{kind:'animation',frames:[{sprite:'tile.a',duration_ms:10},{sprite:'tile.a',duration_ms:10},{sprite:'tile.a',duration_ms:10},{sprite:'tile.a',duration_ms:10},{sprite:'tile.b',duration_ms:10}]}"},
        {"tile/varied.meta", "{kind:'variants',alternatives:[{visual:'tile.a'},{visual:'tile.a'},{visual:'tile.b',weight:7}]}"}},
        "terrain:{grass:{visual:'tile.varied'}}");
    REQUIRE(d);
    REQUIRE(d->animations[0].frames.size() == 5);
    CHECK(d->sprites[SampleAnimation(d->animations[0], 40).spriteIndex].id == "tile.b");
    const auto mapping = d->terrain[TestTerrain("grass").value];
    const auto& group = d->visuals[mapping.index];
    REQUIRE(group.alternatives.size() == 3);
    CHECK(group.totalWeight == 9);
    CHECK(d->sprites[group.alternatives[2].mapping.index].id == "tile.b");

    String variants = "{kind:'variants',alternatives:[";
    for (std::size_t i = 0; i < MaxTilesetEntries; ++i) {
        if (i) { variants += ','; }
        variants += "{visual:'tile.a'}";
    }
    variants += "]}";
    auto maximum = Parse({{"tile/a.png", {}, 2, 2}, {"tile/varied.meta", variants}},
        "terrain:{grass:{visual:'tile.varied'}}");
    REQUIRE(maximum);
    const auto maximumMapping = maximum->terrain[TestTerrain("grass").value];
    CHECK(maximum->visuals[maximumMapping.index].alternatives.size() == MaxTilesetEntries);
    CHECK(maximum->visuals[maximumMapping.index].totalWeight == MaxTilesetEntries);
}

TEST_CASE("source packs reject invalid metadata with local diagnostics", "[sdl][tileset][source]") {
    const Vector<SpriteSourceFile> badFiles[] = {
        {{"tile/Upper.png", {}, 2, 2}}, {{"tile/a.b.png", {}, 2, 2}}, {{"tile/../a.png", {}, 2, 2}},
        {{"tile/a.png", {}, 2, 2}, {"tile/a.meta", "{kind:'variants',alternatives:[]}"}},
        {{"tile/a.png", "{unknown:1}", 2, 2}}, {{"tile/a.png", "{layer:'top'}", 2, 2}},
        {{"tile/a.png", "{anchor:[1.5,2]}", 2, 2}}, {{"tile/a.png", "{cell_anchor:[3,2]}", 2, 2}},
        {{"tile/a.png", "{frame_width:2}", 2, 2}}, {{"tile/a.png", "{frame_width:3,frame_height:2}", 4, 2}},
        {{"tile/a.png", {}, 0, 2}}, {{"tile/a.png", {}, 8192, 2}},
        {{"tile/a.png", "{layer:'ground',layer:'upright'}", 2, 2}},
        {{"tile/a.meta", "{kind:'animation',frames:[{sprite:'missing',duration_ms:10}]}"}},
        {{"tile/a.png", {}, 2, 2}, {"tile/clip.meta", "{kind:'animation',frames:[{sprite:'tile.a',frame:1,duration_ms:10}]}"}},
        {{"tile/a.png", {}, 2, 2}, {"tile/clip.meta", "{kind:'animation',frames:[{sprite:'tile.a',duration_ms:0}]}"}},
        {{"tile/a.png", {}, 2, 2}, {"tile/v.meta", "{kind:'variants',alternatives:[{visual:'tile.a',weight:0}]}"}},
        {{"tile/a.png", {}, 2, 2}, {"tile/v.meta", "{kind:'variants',alternatives:[{visual:'tile.a',rotation:45}]}"}},
        {{"tile/a.meta", "{kind:'connections',connects_to:['road'],shapes:{}}"}},
    };
    for (const auto& files : badFiles) {
        Diagnostics report; CAPTURE(files.front().path.c_str()); CHECK_FALSE(Parse(files, "", &report));
        REQUIRE_FALSE(report.Empty()); REQUIRE(report.Frames().back().location); CHECK_FALSE(report.Frames().back().location->file.empty());
    }
    CHECK_FALSE(Parse({{"tile/a.png", {}, 2, 2}}, "terrain:{grass:{visual:'missing'}}"));
    CHECK_FALSE(Parse({{"tile/a.png", {}, 2, 2}}, "terrain:{grass:{visual:'tile.a',extra:1}}"));
    CHECK_FALSE(Parse({{"tile/a.png", "{frame_width:1,frame_height:1}", 2, 2}}, "terrain:{grass:{visual:'tile.a@1'}}"));
    auto text = Manifest("source_directories:['tile','tile/grass'],");
    CHECK_FALSE(ParseSourceTileset(text, {}, TestContent()));
}

TEST_CASE("weighted visuals are stable for coordinates and moving entity identity", "[sdl][tileset][source]") {
    auto d = Parse({{"tile/grass.png", "{anchor:[1,1],cell_anchor:[1,1]}", 2, 2},
        {"tile/varied.meta", "{kind:'variants',alternatives:[{visual:'tile.grass',weight:3},{visual:'tile.grass',rotation:90,flip_x:true}]}"}},
        "terrain:{grass:{visual:'tile.varied'}}"); REQUIRE(d);
    const auto mapping = d->terrain[TestTerrain("grass").value];
    CHECK(ResolveVisual(*d, mapping, 0, 0, 0).transform == SpriteTransform{});
    CHECK(ResolveVisual(*d, mapping, -1, -2, -3).transform == SpriteTransform{1, true, false});
    const auto entity = ResolveVisual(*d, mapping, 0, 0, 0, 77);
    CHECK(entity == ResolveVisual(*d, mapping, -100, 45, -4, 77));
    unsigned rotated = 0, base = 0;
    for (int x = -128; x < 128; ++x) {
        const auto visual = ResolveVisual(*d, mapping, x, -2, -3);
        CHECK(visual == ResolveVisual(*d, mapping, x, -2, -3));
        if (visual.transform.quarterTurns) { ++rotated; } else { ++base; }
    }
    CHECK(rotated > 0); CHECK(base > rotated);
    // Exhaust the 16 transform encodings, checking composition against applying
    // both transformations to an asymmetric vector.
    auto point = [](SpriteTransform t, int x, int y) {
        if (t.flipX) { x = -x; } if (t.flipY) { y = -y; }
        for (unsigned i = 0; i < t.quarterTurns; ++i) { const int old = x; x = -y; y = old; }
        return std::pair{x, y};
    };
    for (unsigned a = 0; a < 16; ++a) { for (unsigned b = 0; b < 16; ++b) {
        const SpriteTransform inner{std::uint8_t(a & 3), bool(a & 4), bool(a & 8)}, outer{std::uint8_t(b & 3), bool(b & 4), bool(b & 8)};
        const auto first = point(inner, 2, 3);
        CHECK(point(ComposeSpriteTransforms(inner, outer), 2, 3) == point(outer, first.first, first.second));
    } }
}

TEST_CASE("roads resolve every cardinal mask without consulting hidden or other-elevation terrain", "[sdl][tileset][source]") {
    auto d = Parse(ConnectedSources(), "terrain:{road:{visual:'tile.road.connected'}}"); REQUIRE(d);
    const auto mapping = d->terrain[TestTerrain("road").value];
    const auto& group = d->visuals[mapping.index]; REQUIRE(group.alternatives.size() == 16);
    const char* names[] = {"isolated", "end", "straight", "corner", "tee", "cross"};
    const unsigned canonical[] = {0, 1, 5, 3, 11, 15};
    for (unsigned mask = 0; mask < 16; ++mask) {
        const auto resolved = ResolveVisual(*d, mapping, 0, 0, 0, 0, std::uint8_t(mask));
        REQUIRE(resolved.kind == TilesetMappingKind::Sprite);
        unsigned shape = 0;
        for (; shape < 6; ++shape) { String id = "tile.road."; id += names[shape]; if (d->sprites[resolved.index].id == id) { break; } }
        REQUIRE(shape < 6); unsigned rotated = canonical[shape];
        for (unsigned turn = 0; turn < resolved.transform.quarterTurns; ++turn) { rotated = ((rotated << 1) | (rotated >> 3)) & 15; }
        CHECK(rotated == mask);
        WorldView view; view.width = view.height = 3; view.cells.resize(9); view.origin = {-3, 8, 2};
        view.cells[4].terrainVisibility = Visibility::Visible; view.cells[4].tile.terrain = TestTerrain("road");
        const unsigned indices[] = {1, 5, 7, 3};
        for (unsigned direction = 0; direction < 4; ++direction) {
            if (mask & (1U << direction)) { view.cells[indices[direction]].terrainVisibility = Visibility::Visible; view.cells[indices[direction]].tile.terrain = TestTerrain("road"); }
        }
        const auto packet = PrepareMapPacket(view, TestContent(), *d, {}); CHECK(packet.cells[4].terrainSprite == resolved);
        for (auto index : indices) { view.cells[index].terrainVisibility = Visibility::Unknown; view.cells[index].tile.terrain = TerrainId{UINT32_MAX}; }
        CHECK(PrepareMapPacket(view, TestContent(), *d, {}).cells[4].terrainSprite == ResolveVisual(*d, mapping, 0, 0, 0, 0, 0));
        for (auto index : indices) { view.cells[index].terrainVisibility = Visibility::Visible; view.cells[index].tile.terrain = TestTerrain("road"); view.cells[index].terrainDepth = 1; }
        CHECK(PrepareMapPacket(view, TestContent(), *d, {}).cells[4].terrainSprite == ResolveVisual(*d, mapping, 0, 0, 0, 0, 0));
    }
}

TEST_CASE("source atlas assembly preserves pixels and extrudes borders; cancellation releases caller-owned pages", "[sdl][assets][source]") {
    Fixture f; f.Image("tile/a.png");
    auto d = Parse({{"tile/a.png", {}, 2, 2}}); REQUIRE(d);
    Vector<SDL_Surface*> pages;
    auto built = BuildSourceAtlases(f.Directory(), *d, pages, {}); REQUIRE(built); REQUIRE(pages.size() == 1);
    Pixel(pages[0], 0, 0, 0, 255); Pixel(pages[0], 1, 1, 0, 255); Pixel(pages[0], 2, 2, 255, 0); Pixel(pages[0], 3, 3, 255, 0);
    for (auto* page : pages) { SDL_DestroySurface(page); } pages.clear();
    std::stop_source stop; stop.request_stop();
    const auto cancelled = BuildSourceAtlases(f.Directory(), *d, pages, stop.get_token());
    REQUIRE_FALSE(cancelled); CHECK(cancelled.error() == AssetError::Cancelled); CHECK(pages.empty());
    f.Write("tile/a.png", "corrupt");
    const auto corrupt = BuildSourceAtlases(f.Directory(), *d, pages, {});
    REQUIRE_FALSE(corrupt); CHECK(corrupt.error() == AssetError::Bounds);
    for (auto* page : pages) { SDL_DestroySurface(page); }
}

TEST_CASE("source sheet frames are independently copied into the assembled atlas", "[sdl][assets][source]") {
    Fixture f; f.Image("sprite/sheet.png", 4, 2);
    auto d = Parse({{"sprite/sheet.png", "{frame_width:2,frame_height:2}", 4, 2}}); REQUIRE(d);
    Vector<SDL_Surface*> pages;
    REQUIRE(BuildSourceAtlases(f.Directory(), *d, pages, {}));
    const auto& a = d->sprites[0]; const auto& b = d->sprites[1];
    Pixel(pages[a.atlasIndex], a.x, a.y, 0, 255);
    Pixel(pages[b.atlasIndex], b.x, b.y, 255, 0);
    Pixel(pages[b.atlasIndex], b.x - 1, b.y - 1, 255, 0);
    for (auto* page : pages) { SDL_DestroySurface(page); }
}

TEST_CASE("source loader contains paths and reports corrupt or orphaned assets", "[sdl][assets][source]") {
    Fixture f; f.Image("tile/a.png"); f.Write("tileset.json5", Manifest("terrain:{grass:{visual:'tile.a'}}"));
    auto loaded = LoadTilesetFile((f.root / "tileset.json5").string().c_str(), TestContent()); REQUIRE(loaded); CHECK(loaded->sprites[0].id == "tile.a");
    const auto invalid = [&] {
        Diagnostics report; CHECK_FALSE(LoadTilesetFile((f.root / "tileset.json5").string().c_str(), TestContent(), &report)); CHECK_FALSE(report.Empty());
    };
    SECTION("orphan sidecar") { f.Write("tile/missing.png.meta", "{}"); invalid(); }
    SECTION("corrupt PNG") { f.Write("tile/a.png", "not PNG"); invalid(); }
    SECTION("bad sidecar") { f.Write("tile/a.png.meta", "{layer:'invalid'}"); invalid(); }
}

TEST_CASE("tileset versions are rejected before source discovery", "[sdl][tileset][source]") {
    Fixture f;
    const struct VersionCase { const char* manifest; ContentError error; } cases[] = {
        {"{format_version:1,atlases:{main:'missing.png'},sprites:{},source_directories:['../outside']}", ContentError::UnsupportedVersion},
        {"{format_version:3,source_directories:['../outside']}", ContentError::UnsupportedVersion},
        {"{format_version:0}", ContentError::UnsupportedVersion},
        {"{format_version:-1}", ContentError::UnsupportedVersion},
        {"{format_version:18446744073709551615}", ContentError::UnsupportedVersion},
        {"{}", ContentError::InvalidData}, {"{format_version:'2'}", ContentError::InvalidData},
        {"{format_version:2.5}", ContentError::InvalidData}, {"{format_version:null}", ContentError::InvalidData},
        {"{format_version:true}", ContentError::InvalidData}, {"[]", ContentError::InvalidData},
        {"{format_version:2,format_version:2}", ContentError::InvalidData},
    };
    for (const auto& test : cases) {
        CAPTURE(test.manifest);
        Diagnostics parsedReport;
        auto parsed = ParseSourceTileset(test.manifest, {}, TestContent(), &parsedReport);
        REQUIRE_FALSE(parsed); CHECK(parsed.error() == test.error); CHECK_FALSE(parsedReport.Empty());
        f.Write("tileset.json5", String(test.manifest));
        Diagnostics loadedReport;
        auto loaded = LoadTilesetFile((f.root / "tileset.json5").string().c_str(), TestContent(), &loadedReport);
        REQUIRE_FALSE(loaded); CHECK(loaded.error() == test.error); CHECK_FALSE(loadedReport.Empty());
    }
}

TEST_CASE("tileset loader reports missing metadata as IO", "[sdl][tileset][source]") {
    Fixture f;
    Diagnostics report;
    auto loaded = LoadTilesetFile((f.root / "tileset.json5").string().c_str(), TestContent(), &report);
    REQUIRE_FALSE(loaded); CHECK(loaded.error() == ContentError::Io); CHECK_FALSE(report.Empty());
}

TEST_CASE("source loader rejects PNG symlinks outside the pack", "[sdl][assets][source]") {
    Fixture f; f.Write("tileset.json5", Manifest());
    {
        std::error_code error;
        struct ExternalFile {
            fs::path path;
            ~ExternalFile() { std::error_code ignored; fs::remove(path, ignored); }
        } outside{f.root.string() + ".png"};
        REQUIRE(SDL_SavePNG(f.surface, outside.path.string().c_str()));
        fs::create_symlink(outside.path, f.root / "tile/a.png", error);
        if (error) { SKIP("Symlinks unavailable without Windows developer privileges"); }
        Diagnostics report; CHECK_FALSE(LoadTilesetFile((f.root / "tileset.json5").string().c_str(), TestContent(), &report)); CHECK_FALSE(report.Empty());
    }
}

TEST_CASE("oversized animation deadlines follow visible overhang rather than source cell clipping", "[sdl][source][animation]") {
    TilesetDefinition d; d.cellWidth = d.cellHeight = 32;
    d.sprites = {{"a", 0, 0, 0, 32, 320, 16, 320, SpriteLayer::Upright},
        {"b", 0, 32, 0, 32, 320, 16, 320, SpriteLayer::Upright}};
    d.animations.push_back({"tree", {{0, 10}, {1, 30}}, 30});
    MapPacket packet; packet.width = packet.height = 17; packet.cells.resize(17 * 17);
    auto& source = packet.cells[16 * 17 + 8]; source.visible = true; source.terrainSprite = {TilesetMappingKind::Animation, 0};
    packet.cells[8 * 17 + 8].visible = true;
    const auto delay = VisibleAnimationDelay(packet, d, 9, 480, 360, 16);
    REQUIRE(delay); CHECK(*delay == 1);
    packet.cells[8 * 17 + 8].visible = false;
    // Upright overhang still draws over fog; ground remains masked there.
    REQUIRE(VisibleAnimationDelay(packet, d, 9, 480, 360, 16));
    for (auto& sprite : d.sprites) { sprite.layer = SpriteLayer::Ground; }
    CHECK_FALSE(VisibleAnimationDelay(packet, d, 9, 480, 360, 16));
    for (auto& sprite : d.sprites) { sprite.layer = SpriteLayer::Upright; }
    source.visible = false;
    CHECK_FALSE(VisibleAnimationDelay(packet, d, 9, 480, 360, 16));
}

TEST_CASE("source upload failure after a successful page retires the complete partial pack", "[sdl][source][assets]") {
    Fixture f; f.Image("tile/a.png", 1024, 1024); f.Image("tile/b.png", 1024, 1024);
    f.Write("tileset.json5", Manifest());
    Wake wake; SdlAssets assets(&TestContent(), f.Directory(), Wake::Signal, &wake);
    {
        std::unique_lock lock(wake.mutex);
        REQUIRE(wake.condition.wait_for(lock, std::chrono::seconds(10), [&] { return wake.ready; }));
    }
    REQUIRE(assets.Definition().atlases.size() == 2);
    REQUIRE(assets.Poll(f.renderer) == AssetState::Loading); // Glyph upload.
    REQUIRE(assets.Poll(f.renderer) == AssetState::Loading); // First source page.
    REQUIRE(SDL_SetNumberProperty(SDL_GetRendererProperties(f.renderer), SDL_PROP_RENDERER_MAX_TEXTURE_SIZE_NUMBER, 1));
    CHECK(assets.Poll(f.renderer) == AssetState::Failed);
    CHECK(assets.Error() == AssetError::Bounds);
    CHECK_FALSE(assets.Report().Empty());
    CHECK(assets.Poll(f.renderer) == AssetState::Failed);
}

TEST_CASE("source sprites render anchored transforms and visible upright overhang across fog", "[sdl][assets][source]") {
    Fixture f; f.Image("tile/ground.png"); f.Image("sprite/tree.png", 2, 4);
    f.Write("tileset.json5", Manifest("terrain:{grass:{visual:'tile.ground'}},generic:{npc:{visual:'sprite.tree'}}"));
    Wake wake; SdlAssets assets(&TestContent(), f.Directory(), Wake::Signal, &wake); REQUIRE(wake.Finish(assets, f.renderer) == AssetState::Ready);
    SdlText text(f.renderer); REQUIRE(text.Ready());
    WorldView view; view.width = 1; view.height = 2; view.cells.resize(2);
    for (auto& cell : view.cells) { cell.terrainVisibility = cell.overlayVisibility = Visibility::Visible; cell.terrainLight = cell.overlayLight = LightBand::Ordinary; cell.tile.terrain = TestTerrain("grass"); }
    view.cells[1].actor = 99;
    auto packet = PrepareMapPacket(view, TestContent(), assets.Definition(), {});
    const auto layout = MakeMapLayout(800, 600, 1, 2, 2, 2);
    REQUIRE(DrawFrame(f.renderer, text, assets, &view, TestContent(), "", "", 0, &packet));
    Pixel(f.surface, layout.x + layout.cellSize * 3 / 4, layout.y + layout.cellHeight / 2, 255, 0);
    packet.cells[0].visible = false;
    REQUIRE(DrawFrame(f.renderer, text, assets, &view, TestContent(), "", "", 0, &packet));
    Pixel(f.surface, layout.x + layout.cellSize * 3 / 4, layout.y + layout.cellHeight / 2, 255, 0);
    // Center-pivot rotation + flip: the green top-left source pixel lands bottom-right.
    packet.cells[0].visible = true; packet.cells[1].hasOverlay = false;
    auto& mapping = packet.cells[0].terrainSprite;
    mapping.transform = {1, true, false};
    // Ground defaults still attach at bottom-center; replace this fixture's pivot via metadata below.
    f.Write("tile/ground.png.meta", "{anchor:[1,1],cell_anchor:[1,1]}");
    Wake rotatedWake; SdlAssets rotated(&TestContent(), f.Directory(), Wake::Signal, &rotatedWake); REQUIRE(rotatedWake.Finish(rotated, f.renderer) == AssetState::Ready);
    REQUIRE(DrawFrame(f.renderer, text, rotated, &view, TestContent(), "", "", 0, &packet));
    Pixel(f.surface, layout.x + layout.cellSize * 3 / 4, layout.y + layout.cellHeight * 3 / 4, 0, 255);
}

TEST_CASE("upright terrain retains an independent ground PNG beneath its transparency", "[sdl][assets][source]") {
    Fixture f; f.Image("tile/grass.png");
    auto* tree = SDL_CreateSurface(2, 4, SDL_PIXELFORMAT_RGBA32); REQUIRE(tree);
    REQUIRE(SDL_ClearSurface(tree, 0, 0, 0, 0));
    const SDL_Rect leaves{0, 0, 2, 1}; REQUIRE(SDL_FillSurfaceRect(tree, &leaves, SDL_MapSurfaceRGBA(tree, 0, 0, 255, 255)));
    REQUIRE(SDL_SavePNG(tree, (f.root / "sprite/tree.png").string().c_str())); SDL_DestroySurface(tree);
    f.Write("tileset.json5", Manifest("terrain:{grass:{visual:'tile.grass'},tree:{visual:'sprite.tree',ground:'tile.grass'}}"));
    Wake wake; SdlAssets assets(&TestContent(), f.Directory(), Wake::Signal, &wake); REQUIRE(wake.Finish(assets, f.renderer) == AssetState::Ready);
    SdlText text(f.renderer); REQUIRE(text.Ready());
    WorldView view; view.width = 1; view.height = 2; view.cells.resize(2);
    for (auto& cell : view.cells) { cell.terrainVisibility = Visibility::Visible; cell.terrainLight = LightBand::Ordinary; cell.tile.terrain = TestTerrain("grass"); }
    view.cells[1].tile.terrain = TestTerrain("tree");
    auto packet = PrepareMapPacket(view, TestContent(), assets.Definition(), {});
    REQUIRE(packet.cells[1].groundSprite.kind == TilesetMappingKind::Sprite);
    const auto layout = MakeMapLayout(800, 600, 1, 2, 2, 2);
    REQUIRE(DrawFrame(f.renderer, text, assets, &view, TestContent(), "", "", 0, &packet));
    Pixel(f.surface, layout.x + layout.cellSize * 3 / 4, layout.y + layout.cellHeight / 4, 0, 0, 255);
    Pixel(f.surface, layout.x + layout.cellSize * 3 / 4, layout.y + layout.cellHeight * 7 / 4, 255, 0);
    view.cells[0].terrainVisibility = Visibility::Unknown;
    view.cells[0].tile.terrain = TestTerrain("tree");
    packet = PrepareMapPacket(view, TestContent(), assets.Definition(), {});
    REQUIRE(DrawFrame(f.renderer, text, assets, &view, TestContent(), "", "", 0, &packet));
    // The visible tree canopy survives over fog. Its transparent pixels expose
    // only fog underneath.
    Pixel(f.surface, layout.x + layout.cellSize * 3 / 4, layout.y + layout.cellHeight / 4, 0, 0, 255);
    Pixel(f.surface, layout.x + layout.cellSize * 3 / 4, layout.y + layout.cellHeight * 3 / 4, 12, 16, 20);
    Pixel(f.surface, layout.x + layout.cellSize * 3 / 4, layout.y + layout.cellHeight * 7 / 4, 255, 0);
    view.cells[0].terrainVisibility = Visibility::Visible;
    view.cells[0].tile.terrain = TestTerrain("grass");
    view.cells[1].terrainVisibility = Visibility::Unknown;
    packet = PrepareMapPacket(view, TestContent(), assets.Definition(), {});
    REQUIRE(DrawFrame(f.renderer, text, assets, &view, TestContent(), "", "", 0, &packet));
    // Hidden source trees contribute no canopy over visible ground.
    Pixel(f.surface, layout.x + layout.cellSize * 3 / 4, layout.y + layout.cellHeight / 4, 255, 0);
    CHECK_FALSE(Parse({{"sprite/tree.png", {}, 2, 4}}, "terrain:{tree:{visual:'sprite.tree',ground:'sprite.tree'}}"));
}
