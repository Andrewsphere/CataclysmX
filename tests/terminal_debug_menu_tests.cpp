#include "terminal/debug_menu.h"

#include "client/worldgen_debug.h"
#include "content_fixture.h"
#include "core/server.h"
#include "core/save_database.h"

#include <catch2/catch_test_macros.hpp>
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>

#include <string>
#include <chrono>
#include <filesystem>

namespace {
using namespace cataclysmx;

std::string Draw(const ftxui::Component& menu, int width = 80, int height = 24) {
    auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(width), ftxui::Dimension::Fixed(height));
    ftxui::Render(screen, menu->Render());
    // Read characters without terminal color escape sequences.
    std::string result;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) { result += screen.PixelAt(x, y).character; }
        result += '\n';
    }
    return result;
}

void Has(const ftxui::Component& menu, const std::string& expected) {
    INFO(expected);
    CHECK(Draw(menu).find(expected) != std::string::npos);
}

void Key(const ftxui::Component& menu, char key) {
    CHECK(menu->OnEvent(ftxui::Event::Character(key)));
}

struct DebugSaveDirectory {
    std::filesystem::path directory;
    std::string database;

    DebugSaveDirectory() {
        std::error_code error;
        const auto parent = std::filesystem::temp_directory_path(error);
        REQUIRE_FALSE(error);
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (int attempt = 0; ; ++attempt) {
            directory = parent / ("cataclysmx-debug-menu-" + std::to_string(stamp) + "-" + std::to_string(attempt));
            const bool created = std::filesystem::create_directory(directory, error);
            REQUIRE_FALSE(error);
            if (created) { break; }
        }
        database = (directory / "world.sqlite").string();
    }

    ~DebugSaveDirectory() {
        std::error_code error;
        std::filesystem::remove_all(directory, error);
    }
};

TEST_CASE("Worldgen debug modal renders metadata and navigates cached regional previews", "[terminal][worldgen-debug]") {
    World world(TestContent(), 987654);
    auto menu = CreateWorldgenDebugMenu(world, {250, -1}, [] {});
    Has(menu, "Debug menu | World generation");
    Has(menu, "Seed: 987654 | Generation version: 2");
    Has(menu, "excludes saved edits and entities");
    Has(menu, "Layer: Roads | Scale: 8 | Cursor: 250,-1");
    Has(menu, "Region: 0,-1");
    CHECK(menu->OnEvent(ftxui::Event::ArrowRight));
    Has(menu, "Cursor: 258,-1");
    Has(menu, "Region: 1,-1");
    Key(menu, 'j');
    Has(menu, "Cursor: 258,7");
    Has(menu, "Region: 1,0");
    Key(menu, 'H');
    Key(menu, 'K');
    Has(menu, "Cursor: 2,-249");
    CHECK(menu->OnEvent(ftxui::Event::Home));
    Has(menu, "Cursor: 250,-1");
    Key(menu, 'J');
    Key(menu, 'L');
    Has(menu, "Cursor: 506,255");
    Key(menu, 'c');
    Has(menu, "Cursor: 250,-1");

    const char* names[] = {"Roads", "Parcels", "Ownership", "Biomes", "Terrain"};
    for (int i = 0; i < 5; ++i) {
        Key(menu, static_cast<char>('1' + i));
        Has(menu, std::string("Layer: ") + names[i]);
    }
    CHECK(menu->OnEvent(ftxui::Event::Tab));
    Has(menu, "Layer: Roads");
    CHECK(menu->OnEvent(ftxui::Event::TabReverse));
    Has(menu, "Layer: Terrain");
    Key(menu, '1');
    for (int i = 0; i < 5; ++i) { Key(menu, '+'); }
    Has(menu, "Scale: 1");
    Key(menu, 'l');
    Has(menu, "Cursor: 251,-1");
    // A 256-cell map must still leave the controls visible at 80x24.
    Has(menu, "F11 / Escape / q: close");
    CHECK(Draw(menu, 140, 50).find("Cursor: 251,-1") != std::string::npos);
    for (int i = 0; i < 6; ++i) { Key(menu, '-'); }
    Has(menu, "Scale: 16");
    Key(menu, 'k');
    Has(menu, "Cursor: 251,-17");
    CHECK(world.LoadedChunks().empty());
}

TEST_CASE("Worldgen debug modal clamps each axis before generating edge regions", "[terminal][worldgen-debug]") {
    World world(TestContent());
    for (const int sign : {-1, 1}) {
        const TileCoord edge{sign * world_coordinate_limit, sign * world_coordinate_limit};
        auto menu = CreateWorldgenDebugMenu(world, edge, [] {});
        Key(menu, sign > 0 ? 'L' : 'H');
        Key(menu, sign > 0 ? 'J' : 'K');
        Has(menu, "Cursor: " + std::to_string(edge.x) + "," + std::to_string(edge.z));
        Key(menu, sign > 0 ? 'h' : 'l');
        Key(menu, sign > 0 ? 'k' : 'j');
        Has(menu, "Cursor: " + std::to_string(edge.x - sign * 8) + "," + std::to_string(edge.z - sign * 8));
        CHECK(menu->OnEvent(ftxui::Event::Home));
        Has(menu, "Cursor: " + std::to_string(edge.x) + "," + std::to_string(edge.z));
    }
    CHECK(world.LoadedChunks().empty());
}

TEST_CASE("Worldgen debug modal consumes gameplay events and never mutates the server world", "[terminal][worldgen-debug]") {
    Server server(TestContent(), 173);
    const auto before = server.View();
    const auto loaded = server.GetWorld().LoadedChunks();
    const auto archived = server.GetWorld().ArchivedChunkCount();
    Vector<Tile> tiles;
    for (const auto position : loaded) {
        for (const auto tile : server.GetWorld().GetChunk(position)->tiles) { tiles.push_back(tile); }
    }
    int closed = 0;
    auto menu = CreateWorldgenDebugMenu(server.GetWorld(), before.player_position, [&closed] { ++closed; });
    for (const char key : std::string("god.Si ")) { Key(menu, key); }
    CHECK(menu->OnEvent(ftxui::Event::Return));
    Key(menu, 'L'); // Inspect an unexplored region as well.
    Key(menu, '5');
    Key(menu, '+');
    Draw(menu);
    CHECK(closed == 0);
    CHECK(server.GetTick() == before.tick);
    CHECK(server.GetWorld().LoadedChunks() == loaded);
    CHECK(server.GetWorld().ArchivedChunkCount() == archived);
    std::size_t index = 0;
    for (const auto position : loaded) {
        for (const auto tile : server.GetWorld().GetChunk(position)->tiles) { CHECK(tile == tiles[index++]); }
    }
    const auto after = server.View();
    CHECK(after.player_position == before.player_position);
    CHECK(after.cells == before.cells);
    CHECK(after.inventory == before.inventory);
    CHECK(server.GetWorld().CheckInvariants());
    CHECK(menu->OnEvent(ftxui::Event::F11));
    CHECK(closed == 1);
    CHECK(menu->OnEvent(ftxui::Event::Escape));
    CHECK(closed == 2);
    Key(menu, 'q');
    CHECK(closed == 3);
}

TEST_CASE("Worldgen debug menu uses saved seed and refuses a regional preview for legacy saves",
          "[terminal][worldgen-debug][save]") {
    for (const std::uint32_t version : {1U, world_generation_version}) {
        DebugSaveDirectory file;
        {
            SaveDatabase database(TestContent());
            REQUIRE(database.Create(file.database.c_str()));
            REQUIRE(database.Begin());
            SaveSnapshot snapshot;
            snapshot.seed = 1707;
            snapshot.generationVersion = version;
            REQUIRE(database.WriteSnapshot(snapshot));
            REQUIRE(database.Commit());
        }
        World world(TestContent(), 999);
        REQUIRE(world.LoadSave(file.database.c_str()));
        CHECK(world.GetSeed() == 1707);
        CHECK(world.GetGenerationVersion() == version);
        int closed = 0;
        auto menu = CreateWorldgenDebugMenu(world, {0, 0}, [&] { ++closed; });
        Has(menu, "Seed: 1707 | Generation version: " + std::to_string(version));
        if (version == 1) {
            Has(menu, "Generated preview unavailable");
            CHECK(Draw(menu).find("Layer: Roads") == std::string::npos);
            for (const char key : std::string("12345+jL")) { Key(menu, key); }
            Has(menu, "Generated preview unavailable");
        } else {
            Has(menu, "Layer: Roads");
            Key(menu, '5');
            Has(menu, "Layer: Terrain");
        }
        CHECK(world.LoadedChunks().empty());
        CHECK(world.ArchivedChunkCount() == 0);
        CHECK(closed == 0);
        CHECK(menu->OnEvent(ftxui::Event::Escape));
        CHECK(closed == 1);
    }
}

TEST_CASE("Zoomed debug map keeps the selected tile visible in a small terminal", "[terminal][worldgen-debug]") {
    World world(TestContent(), 42);
    const TileCoord selection{255, 255};
    auto menu = CreateWorldgenDebugMenu(world, selection, [] {});
    for (int i = 0; i < 3; ++i) { Key(menu, '+'); }
    const auto plan = GenerateRegionPlan(TestContent(), world.GetSeed(), RegionOf(selection));
    const auto image = BuildWorldgenDebugView(TestContent(), plan, WorldgenDebugLayer::Roads, 1);
    const auto& appearance = image.cells.back();
    auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80), ftxui::Dimension::Fixed(24));
    ftxui::Render(screen, menu->Render());
    int selected = 0;
    for (int y = 0; y < 24; ++y) {
        for (int x = 0; x < 80; ++x) {
            const auto& pixel = screen.PixelAt(x, y);
            if (pixel.character == std::string(appearance.symbol.data(), appearance.symbol.size())
                && pixel.inverted
                && pixel.background_color == ftxui::Color::RGB(0, 0, 0)
                && pixel.foreground_color == ftxui::Color::RGB(appearance.foreground.red,
                    appearance.foreground.green, appearance.foreground.blue)) {
                ++selected;
            }
        }
    }
    CHECK(selected == 1);
    Has(menu, "F11 / Escape / q: close");
}

TEST_CASE("Worldgen debug menu uses configured bindings and localized presentation", "[terminal][worldgen-debug][input]") {
    World world(TestContent(), 123);
    Localization localization;
    REQUIRE(localization.Load(CATACLYSMX_LOC_DIRECTORY, "en"));
    Keymap overrides{{InputContext::Debug, InputAction::West, {{"W", false}}}};
    const auto keys = ResolveKeymap({}, overrides);
    REQUIRE(ValidateKeymap(keys));
    auto menu = CreateWorldgenDebugMenu(world, {1, 2}, [] {}, &localization, &keys);
    Has(menu, "Debug menu | World generation");
    Has(menu, "Cursor: 1,2");
    Key(menu, 'h');
    Has(menu, "Cursor: 1,2");
    Key(menu, 'W');
    Has(menu, "Cursor: -7,2");
    CHECK(world.LoadedChunks().empty());
}

}
