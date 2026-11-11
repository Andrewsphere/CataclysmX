#include "terminal/gameplay_screen.h"
#include "terminal/keyboard.h"
#include "terminal/render.h"
#include "client/text.h"
#include "network/network.h"
#include "content_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>

#include <chrono>
#include <string>
#include <thread>

namespace {
using namespace cataclysmx;

struct Position { int x; int y; };
eastl::optional<Position> Find(const ftxui::Screen& screen, const std::string& text, int top = 0) {
    for (int y = top; y < screen.dimy(); ++y) {
        for (int x = 0; x < screen.dimx(); ++x) {
            std::string candidate;
            for (int end = x; end < screen.dimx() && candidate.size() < text.size(); ++end) {
                const auto& character = screen.PixelAt(end, y).character;
                candidate += character.empty() ? " " : character;
            }
            if (candidate == text) { return Position{x, y}; }
        }
    }
    return {};
}
std::string Plain(const ftxui::Screen& screen) {
    std::string result;
    for (int y = 0; y < screen.dimy(); ++y) {
        for (int x = 0; x < screen.dimx(); ++x) {
            const auto& character = screen.PixelAt(x, y).character;
            result += character.empty() ? " " : character;
        }
        result += '\n';
    }
    return result;
}
std::string Row(const ftxui::Screen& screen, int y) {
    std::string result;
    for (int x = 0; x < screen.dimx(); ++x) {
        const auto& character = screen.PixelAt(x, y).character;
        result += character.empty() ? " " : character;
    }
    return result;
}
struct Fixture {
    Localization localization;
    Keymap keys = DefaultKeymap();
    GameplayUi ui;
    Server server{TestContent()};
    WorldView view = server.View();
    GameplayPresentation presentation;
    int submissions = 0;
    Command submitted;
    bool accept = true;
    int activations = 0;
    InputAction activated = InputAction::Cancel;
    Fixture() {
        REQUIRE(localization.Load(CATACLYSMX_LOC_DIRECTORY, "en"));
        presentation.session = "Local";
        presentation.message = "Latest outcome.";
        Items(0);
    }
    ftxui::Screen Draw(int width = 80, int height = 24) {
        auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(width), ftxui::Dimension::Fixed(height));
        ftxui::Render(screen, ui.Render(view, TestContent(), localization, keys, presentation));
        return screen;
    }
    void Event(const ftxui::Event& event) {
        REQUIRE(ui.HandleEvent(event, view, localization, keys, [&](Command command) {
            ++submissions; submitted = command; return accept;
        }, [&](InputAction action) { ++activations; activated = action; }));
    }
    void Items(int count) {
        view.inventory.clear();
        // Synthetic inventories replace the entire accessible-item snapshot,
        // including demo ground descendants, so IDs and empty states are real.
        view.groundItems.clear(); view.carriedMassGrams = 0;
        const auto type = TestItem("rock");
        const auto& definition = TestContent().GetItem(type);
        for (int i = 1; i <= count; ++i) {
            InventoryEntry item; item.id = static_cast<PersistentId>(i); item.type = type;
            item.massGrams = definition.massGrams; item.volumeMl = definition.volumeMl;
            view.inventory.push_back(item); view.carriedMassGrams += item.massGrams;
        }
    }
};

TEST_CASE("Terminal timer coalesces outstanding wakeups and rearms on consumption", "[terminal][gameplay][input]") {
    TerminalTimerWakeup wakeup;
    CHECK(wakeup.TryPost());
    for (int i = 0; i < 100; ++i) { CHECK_FALSE(wakeup.TryPost()); }
    wakeup.Consumed();
    CHECK(wakeup.TryPost());
    CHECK_FALSE(wakeup.TryPost());
    wakeup.Consumed();
    CHECK(wakeup.TryPost());
}

TEST_CASE("Gameplay localizes player light in wide and compact status", "[terminal][gameplay][perception]") {
    Fixture f;
    const LanguageCatalog labels{{"game.ui.light", "Illumination:", {}},
        {"game.light.dark", "Night", {}}, {"game.light.dim", "Faint", {}},
        {"game.light.ordinary", "Normal", {}}, {"game.light.bright", "Radiant", {}}};
    auto english = LoadLanguageCatalog(JoinConfigurationPath(CATACLYSMX_LOC_DIRECTORY, "en.json5").c_str());
    REQUIRE(english);
    REQUIRE(f.localization.SetCatalogs(*english, labels));
    for (const int width : {80, 120}) {
        for (const auto band : {LightBand::Dark, LightBand::Dim, LightBand::Ordinary, LightBand::Bright}) {
            f.view.playerLight = band;
            const auto screen = f.Draw(width, 40);
            CHECK(Find(screen, "Illumination:"));
            const char* expected = band == LightBand::Dark ? "Night" : band == LightBand::Dim ? "Faint"
                : band == LightBand::Ordinary ? "Normal" : "Radiant";
            CHECK(Find(screen, expected));
        }
    }
}

TEST_CASE("Gameplay layout keeps map status and message visible without inventory players or footer", "[terminal][gameplay][layout]") {
    Fixture f;
    f.Items(1);
    f.view.players.push_back({17, "Hidden player", PlayerConnectionState::Active});
    for (const int width : {80, 99, 100, 120, 99, 80}) {
        const int height = width < 100 ? 24 : width == 100 ? 32 : 40;
        const auto screen = f.Draw(width, height);
        INFO(Plain(screen));
        const auto world = Find(screen, "World"); REQUIRE(world);
        const auto position = Find(screen, "X 0  Z 0"); REQUIRE(position);
        const auto message = Find(screen, "Latest outcome."); REQUIRE(message);
        CHECK(message->y == height - 2);
        CHECK(Find(screen, "@"));
        CHECK_FALSE(Find(screen, "Inventory"));
        CHECK_FALSE(Find(screen, "Hidden player"));
        CHECK_FALSE(Find(screen, "F1"));
        if (width >= 100) {
            const auto status = Find(screen, "Status"); REQUIRE(status);
            CHECK(status->x == width - 32);
            CHECK(status->y == world->y);
        } else { CHECK(position->y < world->y); }
        const auto title = Find(screen, "CataclysmX"); REQUIRE(title);
        CHECK(screen.PixelAt(title->x, title->y).bold);
        CHECK(screen.PixelAt(title->x, title->y).foreground_color == ftxui::Color::Default);
        CHECK(screen.PixelAt(title->x, title->y).background_color == ftxui::Color::Default);
    }
}

TEST_CASE("Gameplay map crops around player preserves glyph colors and handles radius zero", "[terminal][gameplay][render]") {
    Fixture f;
    for (const int radius : {0, 8, 12}) {
        f.view = f.server.View(radius);
        for (const int height : {24, 40}) {
            const auto screen = f.Draw(120, height);
            INFO(Plain(screen));
            const auto player = Find(screen, "@"); REQUIRE(player);
            const auto& pixel = screen.PixelAt(player->x, player->y);
            CHECK(pixel.foreground_color == ftxui::Color::RGB(255, 255, 255));
            CHECK_FALSE(pixel.inverted);
            CHECK(player->x > 3);
            CHECK(player->x < 90);
            CHECK(Find(screen, "Map cropped").has_value() == (radius >= 8 && height == 24));
        }
    }
}

TEST_CASE("Gameplay reuses prepared map across status redraws and refreshes changed cells", "[terminal][gameplay][render]") {
    Fixture f;
    auto first = f.Draw(120, 40);
    REQUIRE(Find(first, "@"));
    REQUIRE(f.ui.MapPreparations() == 1);
    f.view.tick += 1; f.view.remainingTicks = 4; f.presentation.message = "Updated status";
    auto status = f.Draw(80, 24);
    CHECK(Find(status, "Updated status"));
    CHECK(Find(status, "@"));
    CHECK(f.ui.MapPreparations() == 1);
    // The crop anchor is recomputed independently of cached visible appearances.
    f.view.player_position.x += 1;
    f.Draw(120, 24);
    CHECK(f.ui.MapPreparations() == 1);
    auto& cell = f.view.cells[static_cast<std::size_t>((f.view.height / 2) * f.view.width + f.view.width / 2)];
    cell.player = false; cell.actor = 7; cell.overlayVisibility = Visibility::Visible;
    auto changed = f.Draw(120, 40);
    CHECK(Find(changed, "z"));
    CHECK(f.ui.MapPreparations() == 2);
    cell.overlayLight = LightBand::Dim;
    auto relit = f.Draw(120, 40);
    GameplayUi fresh;
    auto reference = ftxui::Screen::Create(ftxui::Dimension::Fixed(120), ftxui::Dimension::Fixed(40));
    ftxui::Render(reference, fresh.Render(f.view, TestContent(), f.localization, f.keys, f.presentation));
    CHECK(relit.ToString() == reference.ToString());
    CHECK(f.ui.MapPreparations() == 3);
    f.view = f.server.View(0);
    f.Draw(120, 40);
    CHECK(f.ui.MapPreparations() == 4);
}

TEST_CASE("Large map rasterizes only visible panel and preserves appearance across resize and preferences", "[terminal][gameplay][render]") {
    Fixture f;
    const auto center = f.server.View(0).cells[0];
    f.view.width = 129; f.view.height = 129;
    f.view.origin = {-64, -64, f.view.player_position.y};
    ViewCell floor;
    floor.terrainVisibility = Visibility::Visible;
    floor.tile.terrain = TestTerrain("floor");
    floor.terrainLight = LightBand::Ordinary;
    f.view.cells.assign(129 * 129, floor);
    const std::size_t centerIndex = 64 * 129 + 64;
    f.view.cells[centerIndex] = center;
    auto& neighbor = f.view.cells[centerIndex + 1];
    neighbor.actor = 77; neighbor.overlayVisibility = Visibility::Visible;
    neighbor.overlayLight = LightBand::Dim;
    auto expected = ftxui::Screen::Create(ftxui::Dimension::Fixed(1), ftxui::Dimension::Fixed(1));
    ftxui::Render(expected, RenderTerrainCell(CellAppearance(neighbor, TestContent())));
    for (const int width : {80, 120, 40, 120, 80}) {
        const int height = width == 120 ? 40 : 24;
        const auto screen = f.Draw(width, height);
        const auto player = Find(screen, "@"); REQUIRE(player);
        REQUIRE(player->x + 1 < screen.dimx());
        const auto& actual = screen.PixelAt(player->x + 1, player->y);
        CHECK(actual.character == expected.PixelAt(0, 0).character);
        CHECK(actual.foreground_color == expected.PixelAt(0, 0).foreground_color);
        CHECK(actual.background_color == expected.PixelAt(0, 0).background_color);
        CHECK(screen.PixelAt(player->x, player->y).foreground_color == ftxui::Color::RGB(255, 255, 255));
        CHECK(Find(screen, "Map cropped"));
        CHECK(f.ui.MapRasterizedCells() > 1);
        CHECK(f.ui.MapRasterizedCells() < static_cast<std::size_t>(width * height));
        CHECK(f.ui.MapPreparations() == 1);
        CHECK(Find(screen, "Latest outcome."));
    }
    const auto large = f.ui.MapRasterizedCells();
    f.Draw(40, 16);
    CHECK(f.ui.MapRasterizedCells() < large);
    f.presentation.viewRadius = 0;
    auto screen = f.Draw(80, 24);
    CHECK(Find(screen, "@")); CHECK_FALSE(Find(screen, "z"));
    CHECK_FALSE(Find(screen, "Map cropped"));
    CHECK(f.ui.MapRasterizedCells() == 1);
    CHECK(f.ui.MapPreparations() == 1);
    f.presentation.viewRadius = 1;
    screen = f.Draw(80, 24);
    CHECK(Find(screen, "z"));
    CHECK(f.ui.MapRasterizedCells() == 9);
    CHECK(f.view.width == 129); CHECK(f.view.cells.size() == 129 * 129);

    // A parent clip must bound direct rasterization as well as DOM children.
    f.presentation.viewRadius = 64;
    auto root = f.ui.Render(f.view, TestContent(), f.localization, f.keys, f.presentation);
    auto full = ftxui::Screen::Create(ftxui::Dimension::Fixed(120), ftxui::Dimension::Fixed(40));
    ftxui::Render(full, root);
    const auto player = Find(full, "@"); REQUIRE(player);
    auto clipped = ftxui::Screen::Create(ftxui::Dimension::Fixed(120), ftxui::Dimension::Fixed(40));
    for (int y = 0; y < clipped.dimy(); ++y) {
        for (int x = 0; x < clipped.dimx(); ++x) { clipped.PixelAt(x, y).character = "!"; }
    }
    const ftxui::Box clip{player->x - 2, player->x + 2, player->y - 2, player->y + 2};
    clipped.stencil = clip;
    root->Render(clipped);
    CHECK(f.ui.MapRasterizedCells() == 25);
    CHECK(f.ui.MapResolvedCells() <= 25);
    CHECK(clipped.stencil == clip);
    // PixelAt outside the current stencil returns FTXUI's shared discard pixel,
    // not screen storage. Restore full access before inspecting painted pixels.
    clipped.stencil = {0, clipped.dimx() - 1, 0, clipped.dimy() - 1};
    // Check the clipped map and a one-cell map-only guard ring, without making
    // assumptions about unrelated border/text nodes elsewhere in the UI.
    for (int y = player->y - 3; y <= player->y + 3; ++y) {
        for (int x = player->x - 3; x <= player->x + 3; ++x) {
            if (clip.Contain(x, y)) {
                CHECK(clipped.PixelAt(x, y).character == full.PixelAt(x, y).character);
                CHECK(clipped.PixelAt(x, y).foreground_color == full.PixelAt(x, y).foreground_color);
                CHECK(clipped.PixelAt(x, y).background_color == full.PixelAt(x, y).background_color);
            } else { CHECK(clipped.PixelAt(x, y).character == "!"); }
        }
    }
}

TEST_CASE("Changed large snapshots resolve only clipped cells and unchanged redraws reuse them", "[terminal][gameplay][render]") {
    Fixture f;
    ViewCell floor;
    floor.tile.terrain = TestTerrain("floor");
    floor.terrainVisibility = Visibility::Visible;
    floor.terrainLight = LightBand::Ordinary;
    f.view.width = 129; f.view.height = 129; f.view.origin = {-64, -64};
    f.view.cells.assign(129 * 129, floor);
    f.Draw(80, 24);
    const auto visible = f.ui.MapRasterizedCells();
    REQUIRE(visible > 0); REQUIRE(visible < 80 * 24);
    CHECK(f.ui.MapResolvedCells() == visible);
    CHECK(f.ui.MapPreparations() == 1);
    // All 16,641 inputs change, but neither comparison nor shading scans the map.
    for (auto& cell : f.view.cells) { cell.terrainLight = LightBand::Dim; }
    f.Draw(80, 24);
    CHECK(f.ui.MapResolvedCells() == visible);
    CHECK(f.ui.MapPreparations() == 2);
    f.view.tick += 1; f.presentation.message = "Only status changed";
    f.Draw(80, 24);
    CHECK(f.ui.MapResolvedCells() == 0);
    CHECK(f.ui.MapPreparations() == 2);
    // An offscreen change is not prepared until a layout actually exposes it.
    f.view.cells[0].terrainLight = LightBand::Bright;
    f.Draw(80, 24);
    CHECK(f.ui.MapResolvedCells() == 0);
    CHECK(f.ui.MapPreparations() == 2);
    f.Draw(120, 40);
    CHECK(f.ui.MapResolvedCells() > 0);
    CHECK(f.ui.MapResolvedCells() <= f.ui.MapRasterizedCells());
    f.view.player_position = f.view.origin;
    const auto corner = f.Draw(80, 24);
    CHECK(f.ui.MapResolvedCells() > 0);
    CHECK(f.ui.MapResolvedCells() <= f.ui.MapRasterizedCells());
    const auto dot = Find(corner, "."); REQUIRE(dot);
    const auto expected = CellAppearance(f.view.cells[0], TestContent());
    CHECK(corner.PixelAt(dot->x, dot->y).foreground_color == ftxui::Color::RGB(
        expected.foreground.red, expected.foreground.green, expected.foreground.blue));
}

TEST_CASE("Map nodes own temporary snapshot cells until deferred rasterization", "[terminal][gameplay][render]") {
    Fixture f;
    auto root = f.ui.Render(f.server.View(0), TestContent(), f.localization, f.keys, f.presentation);
    // Allocate/overwrite other snapshots before drawing the returned tree.
    f.view.cells.assign(129 * 129, ViewCell{});
    auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80), ftxui::Dimension::Fixed(24));
    ftxui::Render(screen, root);
    REQUIRE(Find(screen, "@"));
    CHECK(f.ui.MapRasterizedCells() == 1);
    CHECK(f.ui.MapResolvedCells() == 1);
}

TEST_CASE("Clipped appearance resolution preserves Unicode single-column fallback and colors", "[terminal][gameplay][render]") {
    struct Glyph { const char* symbol; const char* expected; };
    const Glyph glyphs[] = {{"#", "#"}, {"\xe2\x94\x82", "\xe2\x94\x82"},
        {"\xe7\x95\x8c", "?"}, {"\xcc\x81", "?"}, {"\xf0\x9f\x98\x80", "?"}};
    for (const auto& glyph : glyphs) {
        Fixture f;
        String text = "{format_version:1,definitions:[{type:'terrain',id:'glyph',name:'Glyph',movement:{passable:true},blocks_sight:false,appearance:{symbol:'";
        text += glyph.symbol; text += "',foreground:'#123456',background:'#789abc'}}]}";
        const ContentSource source{"glyph.json5", {text.data(), text.size()}};
        auto content = LoadContent({&source, 1}); REQUIRE(content);
        f.view = f.server.View(0);
        ViewCell cell; cell.terrainVisibility = Visibility::Visible;
        cell.terrainLight = LightBand::Ordinary; cell.tile.terrain = content->FindTerrain("glyph");
        f.view.cells = {cell};
        auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80), ftxui::Dimension::Fixed(24));
        ftxui::Render(screen, f.ui.Render(std::move(f.view), *content, f.localization, f.keys, f.presentation));
        CHECK(f.ui.MapResolvedCells() == 1);
        // The foreground color identifies the map cell even for a border glyph.
        std::size_t matches = 0;
        for (int y = 0; y < screen.dimy(); ++y) {
            for (int x = 0; x < screen.dimx(); ++x) {
                const auto& pixel = screen.PixelAt(x, y);
                if (pixel.foreground_color != ftxui::Color::RGB(18, 52, 86)) { continue; }
                ++matches;
                CHECK(pixel.character == glyph.expected);
                CHECK(pixel.background_color == ftxui::Color::RGB(120, 154, 188));
            }
        }
        CHECK(matches == 1);
    }
}

TEST_CASE("Status groups readiness location and pacing with aligned values and complete large numbers", "[terminal][gameplay][layout]") {
    Fixture f;
    f.view.tick = UINT64_MAX;
    f.view.player_position = {INT32_MIN, INT32_MAX};
    f.view.speed = 250; f.view.remainingTicks = 120; f.view.commandQueued = true;
    f.view.pacing.ticksPerSecond = 1000000;
    for (const int height : {24, 32, 40}) {
        const auto screen = f.Draw(120, height);
        INFO(Plain(screen));
        CHECK(Find(screen, "Location"));
        CHECK(Find(screen, "Pacing"));
        CHECK(Find(screen, "X -2147483648  Z 2147483647"));
        CHECK(Find(screen, "18446744073709551615"));
        const auto rate = Find(screen, "1000000 /s"); REQUIRE(rate);
        const auto speed = Find(screen, "250"); REQUIRE(speed);
        const auto cooldown = Find(screen, "120 ticks"); REQUIRE(cooldown);
        CHECK(rate->x + 10 == speed->x + 3);
        CHECK(cooldown->x + 9 == speed->x + 3);
        const auto queued = Find(screen, "Queued"); REQUIRE(queued);
        CHECK(screen.PixelAt(queued->x, queued->y).bold);
        CHECK(screen.PixelAt(queued->x, queued->y).foreground_color == ftxui::Color::Yellow);
        CHECK(Find(screen, "Latest outcome."));
    }
    f.view.commandQueued = false;
    CHECK(Find(f.Draw(100, 24), "Busy"));
    f.view.remainingTicks = 0;
    CHECK(Find(f.Draw(100, 24), "Ready"));
    f.presentation.pending = "move east #14";
    auto screen = f.Draw(100, 24);
    CHECK(Find(screen, "Pending"));
    CHECK(Find(screen, "Pending: move east #14"));
    CHECK(Find(screen, "Latest outcome."));
}

TEST_CASE("Small gameplay modals fit content and long lists expose overflow markers", "[terminal][gameplay][layout]") {
    Fixture f;
    for (const auto action : {InputAction::Inventory, InputAction::Players}) {
        f.ui.Open(action);
        for (const int width : {80, 120}) {
            const auto screen = f.Draw(width, width == 80 ? 24 : 40);
            const auto title = Find(screen, action == InputAction::Inventory ? "Inventory" : "Players"); REQUIRE(title);
            CHECK(title->y > 3); // Short content gets a smaller centered window.
            const auto close = Find(screen, "Escape"); REQUIRE(close);
            CHECK(close->y > title->y);
            CHECK(close->y < screen.dimy() - 4);
            CHECK(Find(screen, action == InputAction::Inventory ? "0 / 0" : "1 player"));
            CHECK_FALSE(Find(screen, "↓"));
        }
    }
    f.Items(40); f.ui.Open(InputAction::Inventory);
    auto screen = f.Draw();
    CHECK(Find(screen, "↓")); CHECK_FALSE(Find(screen, "↑"));
    f.Event(ftxui::Event::ArrowUp);
    screen = f.Draw();
    CHECK(Find(screen, "↑")); CHECK_FALSE(Find(screen, "↓"));
    CHECK(Find(screen, "40 / 40"));
}

TEST_CASE("Quick actions wrap navigate preserve selection on resize and activate only on accept", "[terminal][gameplay][input]") {
    Fixture f;
    const auto tick = f.server.GetTick();
    REQUIRE(f.ui.PausesLocal(InputAction::QuickActions));
    REQUIRE(f.ui.Open(InputAction::QuickActions));
    CHECK(Find(f.Draw(), "Quick actions"));
    f.Event(ftxui::Event::ArrowUp);
    for (int width : {80, 99, 100, 120, 80}) {
        const auto screen = f.Draw(width, width < 100 ? 24 : 32);
        INFO(Plain(screen));
        const auto row = Find(screen, "> debug"); REQUIRE(row);
        CHECK(screen.PixelAt(row->x, row->y).inverted);
        CHECK(Find(screen, "14 / 14"));
        CHECK(Find(screen, "Escape"));
    }
    f.Event(ftxui::Event::Character("g"));
    f.Event(ftxui::Event::Custom);
    CHECK(f.activations == 0); CHECK(f.submissions == 0);
    CHECK(f.server.GetTick() == tick);
    f.Event(ftxui::Event::Return);
    CHECK_FALSE(f.ui.IsOpen());
    CHECK(f.activations == 1); CHECK(f.activated == InputAction::Debug);
    f.ui.Open(InputAction::QuickActions);
    f.Event(ftxui::Event::ArrowDown); // Wrap back to Inventory.
    f.Event(ftxui::Event::Return);
    CHECK(f.ui.IsOpen()); CHECK(Find(f.Draw(), "You are carrying no items."));
    CHECK(f.activations == 1); CHECK(f.submissions == 0);
    f.Event(ftxui::Event::Escape);
    CHECK_FALSE(f.ui.IsOpen()); CHECK(f.server.GetTick() == tick);
}

TEST_CASE("Quick actions use rebound menu controls and dispatch a gameplay command once", "[terminal][gameplay][input]") {
    Fixture f;
    f.keys = ResolveKeymap({{InputContext::Gameplay, InputAction::QuickActions, {{"A", false}}},
        {InputContext::Menu, InputAction::South, {{"x", false}}},
        {InputContext::Menu, InputAction::Accept, {{"v", false}}}}, {});
    REQUIRE(ValidateKeymap(f.keys));
    CHECK(GameplayWelcome(f.localization, f.keys).find("A: Quick actions") != String::npos);
    REQUIRE(f.ui.Open(InputAction::QuickActions)); f.Draw();
    // Inventory, Use, Pick up, Interact, Wait.
    for (int i = 0; i < 4; ++i) { f.Event(ftxui::Event::Character("x")); }
    CHECK(Find(f.Draw(), "> wait"));
    f.Event(ftxui::Event::Return); // Physical Enter is not the rebound Menu accept.
    CHECK(f.ui.IsOpen()); CHECK(f.activations == 0);
    const auto before = f.server.GetTick();
    int dispatched = 0;
    REQUIRE(f.ui.HandleEvent(ftxui::Event::Character("v"), f.view, f.localization, f.keys,
        [&](Command) { FAIL("Quick actions must dispatch an action rather than a direct inventory drop"); return false; },
        [&](InputAction action) {
            ++dispatched; REQUIRE(action == InputAction::Wait);
            CHECK(f.server.Send({CommandKind::wait}).advanced);
        }));
    CHECK(dispatched == 1); CHECK_FALSE(f.ui.IsOpen());
    CHECK(f.server.GetTick() == before + ticks_per_turn);
    f.ui.Open(InputAction::QuickActions); f.Event(ftxui::Event::Escape);
    CHECK(f.server.GetTick() == before + ticks_per_turn);
}

TEST_CASE("Gameplay modals fit terminal margins and preserve inventory selection across resize", "[terminal][gameplay][layout]") {
    Fixture f; f.Items(40);
    REQUIRE(f.ui.Open(InputAction::Inventory));
    f.Draw();
    // Wrapping selects the final entry, then resizing must keep it visible.
    f.Event(ftxui::Event::ArrowUp);
    for (const int width : {80, 99, 100, 120, 80}) {
        const int height = width < 100 ? 24 : width == 100 ? 32 : 40;
        const auto screen = f.Draw(width, height);
        INFO(Plain(screen));
        const auto title = Find(screen, "Inventory"); REQUIRE(title);
        CHECK(title->x >= 4);
        CHECK(title->y == 3);
        const auto item = Find(screen, "> 40:rock"); REQUIRE(item);
        CHECK(screen.PixelAt(item->x, item->y).inverted);
        CHECK(screen.PixelAt(item->x, item->y).bold);
        const auto close = Find(screen, "Escape"); REQUIRE(close);
        CHECK(close->y == height - 4);
        CHECK(screen.PixelAt(close->x, close->y).bold);
        // Find the effective binding in the fixed footer, not the explanatory
        // sentence above the list, which also mentions Enter.
        const auto drop = Find(screen, "Enter", close->y - 1); REQUIRE(drop);
        CHECK(drop->y == close->y - 1);
        CHECK(screen.PixelAt(drop->x, drop->y).bold);
        CHECK(Find(screen, "drop  |  u use item  |  a manage", close->y - 1));
        CHECK_FALSE(screen.PixelAt(close->x, close->y).inverted);
    }
    f.Event(ftxui::Event::Return);
    REQUIRE(f.submissions == 1);
    CHECK(f.submitted.kind == CommandKind::drop);
    CHECK(f.submitted.item == 40);
    CHECK_FALSE(f.ui.IsOpen());
}

TEST_CASE("Inventory rejects stale selections and failed submissions without dropping a neighbor", "[terminal][gameplay][inventory]") {
    Fixture f; f.Items(3);
    for (auto& item : f.view.inventory) { item.id *= 10; }
    REQUIRE(f.ui.Open(InputAction::Inventory)); f.Draw();
    f.Event(ftxui::Event::ArrowDown); f.Draw();
    f.view.inventory.erase(f.view.inventory.begin() + 1); // Snapshot arrives after the last frame.
    f.Event(ftxui::Event::Return);
    CHECK(f.submissions == 0);
    CHECK(f.ui.IsOpen());
    CHECK(Find(f.Draw(), "That item is no longer accessible."));
    // Neither rendering nor a second activation may substitute ID 30 for 20.
    f.Event(ftxui::Event::Return);
    CHECK(f.submissions == 0);
    f.Event(ftxui::Event::ArrowDown); f.Draw(); // Explicitly select ID 10.
    f.Event(ftxui::Event::ArrowDown); f.Draw(); // Explicitly select ID 30.
    f.accept = false;
    f.Event(ftxui::Event::Return);
    CHECK(f.submissions == 1);
    CHECK(f.submitted.item == 30);
    CHECK(f.ui.IsOpen());
    CHECK(Find(f.Draw(), "The command was refused."));
    // A new lower-ID item does not move the selection to a different identity.
    f.view.inventory.insert(f.view.inventory.begin(), {1, TestItem("backpack"), 1});
    f.Draw(); f.accept = true; f.Event(ftxui::Event::Return);
    CHECK(f.submitted.item == 30);
    CHECK_FALSE(f.ui.IsOpen());
}

TEST_CASE("Gameplay modal empty states cancellation and rebound menu controls isolate gameplay", "[terminal][gameplay][input]") {
    Fixture f;
    f.keys = ResolveKeymap({{InputContext::Menu, InputAction::Accept, {{"v", false}}},
        {InputContext::Menu, InputAction::South, {{"x", false}}},
        {InputContext::Menu, InputAction::Cancel, {}}}, {});
    REQUIRE(ValidateKeymap(f.keys));
    const auto tick = f.server.GetTick();
    for (auto action : {InputAction::Inventory, InputAction::Players, InputAction::Help}) {
        REQUIRE(f.ui.PausesLocal(action));
        REQUIRE(f.ui.Open(action));
        CHECK(f.ui.PausesLocal({}));
        const auto screen = f.Draw();
        if (action == InputAction::Inventory) { CHECK(Find(screen, "You are carrying no items.")); }
        f.Event(ftxui::Event::Character("g"));
        f.Event(ftxui::Event::Character("v"));
        f.Event(ftxui::Event::ArrowRight);
        f.Event(ftxui::Event::Custom);
        CHECK(f.submissions == 0);
        f.Event(ftxui::Event::Escape);
        CHECK_FALSE(f.ui.IsOpen());
        CHECK_FALSE(f.ui.PausesLocal({}));
        CHECK(f.server.GetTick() == tick);
    }
    f.Items(2); f.ui.Open(InputAction::Inventory); f.Draw();
    f.Event(ftxui::Event::Character("x"));
    CHECK(Find(f.Draw(), "v drop"));
    f.Event(ftxui::Event::Character("v"));
    CHECK(f.submitted.item == 2);
    CHECK(f.submissions == 1);
    f.presentation.interacting = true;
    f.ui.Open(InputAction::Players); f.Event(ftxui::Event::Escape);
    CHECK(Find(f.Draw(), "Interact in which direction?"));
}

TEST_CASE("Players and Controls scroll independently and expose effective bindings and fixed controls", "[terminal][gameplay][input]") {
    Fixture f;
    f.keys = ResolveKeymap({{InputContext::Gameplay, InputAction::Inventory, {{"I", false}}},
        {InputContext::Gameplay, InputAction::Help, {{"?", false}}}}, {});
    const auto welcome = GameplayWelcome(f.localization, f.keys);
    CHECK(welcome.find("I: Inventory") != String::npos);
    CHECK(welcome.find("?: Controls") != String::npos);
    f.ui.Open(InputAction::Players);
    f.view.players.clear();
    CHECK(Find(f.Draw(), "No players to display."));
    for (int i = 0; i < 40; ++i) {
        f.view.players.push_back({static_cast<PersistentId>(i + 1), String("Player") + UnsignedText(i),
            PlayerConnectionState::Active, true, true, 1, 1234567});
    }
    auto screen = f.Draw();
    CHECK(Find(screen, "Player0"));
    CHECK(Find(screen, "active | ready yes | queued yes grace 1.2s"));
    for (int i = 0; i < 160; ++i) { f.Event(ftxui::Event::ArrowDown); f.Draw(); }
    CHECK(Find(f.Draw(), "Player39"));
    f.ui.Close(); f.ui.Open(InputAction::Help);
    screen = f.Draw();
    const auto inventory = Find(screen, "inventory"); REQUIRE(inventory);
    const auto controls = Find(screen, "controls"); REQUIRE(controls);
    CHECK(Row(screen, inventory->y).find("I") != std::string::npos);
    CHECK(Row(screen, controls->y).find("?") != std::string::npos);
    for (int i = 0; i < 100; ++i) { f.Event(ftxui::Event::ArrowDown); f.Draw(); }
    screen = f.Draw();
    CHECK(Find(screen, "Fixed LAN keys:"));
    CHECK(Find(screen, "Ctrl+c"));
    f.ui.Close(); f.ui.Open(InputAction::Players);
    CHECK(Find(f.Draw(), "Player39"));
}

TEST_CASE("Gameplay modal labels wrap and remain contained with long translated text", "[terminal][gameplay][localization]") {
    Fixture f;
    auto english = LoadLanguageCatalog(JoinConfigurationPath(CATACLYSMX_LOC_DIRECTORY, "en.json5").c_str());
    REQUIRE(english);
    LanguageCatalog translated{{"game.ui.inventory_empty", "A long translated explanation of the empty inventory that must wrap inside the modal while its fixed navigation controls stay visible.", {}},
        {"game.ui.controls", "Translated controls reference", {}},
        {"action.inventory", "A long translated description of opening the directly carried item selection window", {}}};
    REQUIRE(f.localization.SetCatalogs(*english, translated));
    for (auto action : {InputAction::Inventory, InputAction::Help, InputAction::Players, InputAction::QuickActions}) {
        f.ui.Open(action);
        for (int width : {80, 99, 100, 120, 80}) {
            const auto screen = f.Draw(width, width < 100 ? 24 : 32);
            CHECK(Find(screen, "Escape"));
            CHECK(screen.PixelAt(0, 0).foreground_color == ftxui::Color::Default);
            CHECK(Find(screen, "Latest outcome."));
        }
    }
}

TEST_CASE("Controls label unbound actions and show keyboard limitations only when needed", "[terminal][gameplay][input]") {
    Fixture f;
    f.keys = ResolveKeymap({{InputContext::Gameplay, InputAction::Inventory, {}},
        {InputContext::Gameplay, InputAction::Wait, {{"T", true}}}}, {});
    REQUIRE(ValidateKeymap(f.keys));
    CHECK(GameplayWelcome(f.localization, f.keys).find("unbound: Inventory") != String::npos);
    f.ui.Open(InputAction::Help);
    const auto screen = f.Draw();
    const auto inventory = Find(screen, "inventory"); REQUIRE(inventory);
    CHECK(Row(screen, inventory->y).find("unbound") != std::string::npos);
    for (int i = 0; i < 100; ++i) { f.Event(ftxui::Event::ArrowDown); f.Draw(); }
    CHECK(Find(f.Draw(), "Basic keyboard:"));
    f.presentation.enhancedKeyboard = true;
    CHECK_FALSE(Find(f.Draw(), "Basic keyboard:"));
}

TEST_CASE("Inventory modal drops selected local items preserves contents and accepts queued drops", "[terminal][gameplay][inventory]") {
    Fixture f;
    auto& world = f.server.GetWorld();
    REQUIRE_FALSE(world.ItemsAt({0, 0}).Empty());
    const auto bag = *world.ItemsAt({0, 0}).begin();
    const auto bandage = world.GetItem(bag)->contents[0];
    const auto bagId = world.GetItem(bag)->id;
    REQUIRE(world.TransferItem(bag, ItemLocation::Carried(f.server.GetPlayer())));
    f.view = f.server.View(); f.ui.Open(InputAction::Inventory);
    CHECK(Find(f.Draw(), "1 inside"));
    bool committed = false;
    REQUIRE(f.ui.HandleEvent(ftxui::Event::Return, f.view, f.localization, f.keys, [&](Command command) {
        CHECK(command.item == bagId);
        const auto result = f.server.Send(command);
        committed = result.advanced;
        return result.advanced || result.outcome == Outcome::queued;
    }));
    CHECK(committed); CHECK_FALSE(f.ui.IsOpen());
    CHECK(world.GetItem(bag)->location == ItemLocation::Ground({0, 0}));
    CHECK(world.GetItem(bandage)->location == ItemLocation::Inside(bag));
    REQUIRE(world.TransferItem(bag, ItemLocation::Carried(f.server.GetPlayer())));
    auto pacing = f.server.GetPacing(); pacing.rateLimited = true;
    REQUIRE(f.server.SetPacing(pacing));
    f.server.Send({CommandKind::wait});
    REQUIRE(f.server.View().remainingTicks != 0);
    f.view = f.server.View(); f.ui.Open(InputAction::Inventory); f.Draw();
    Outcome outcome = Outcome::invalid_command;
    REQUIRE(f.ui.HandleEvent(ftxui::Event::Return, f.view, f.localization, f.keys, [&](Command command) {
        const auto result = f.server.Send(command); outcome = result.outcome;
        return result.advanced || result.outcome == Outcome::queued;
    }));
    CHECK(outcome == Outcome::queued); CHECK_FALSE(f.ui.IsOpen());
}

TEST_CASE("Inventory modal submits selected host and guest drops over real loopback", "[terminal][gameplay][network]") {
    using namespace cataclysmx::network;
    Fixture f;
    auto runtime = NetworkRuntime::Create(); REQUIRE(runtime);
    LanHost host(**runtime, f.server, TestContent()); REQUIRE(host.Open({0, 0, "Inventory test"}));
    LanClient guest(**runtime, TestContent()); REQUIRE(guest.Connect({"127.0.0.1", host.GetPort(), "Guest", 0}));
    const auto poll = [&](const std::function<bool()>& done) {
        for (int i = 0; i < 500; ++i) {
            host.Pump(10000); guest.Pump(10000);
            if (done()) { return true; }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return false;
    };
    REQUIRE(poll([&] { return guest.GetState() == ConnectionState::Active && guest.HasView(); }));
    const auto guestActor = f.server.GetWorld().FindActor(guest.View().playerId);
    auto& world = f.server.GetWorld();
    const auto hostItem = world.CreateItem(TestItem("rock"), ItemLocation::Carried(f.server.GetPlayer())); REQUIRE(hostItem);
    const auto guestItem = world.CreateItem(TestItem("backpack"), ItemLocation::Carried(guestActor)); REQUIRE(guestItem);
    const auto guestContents = world.CreateItem(TestItem("bandage"), ItemLocation::Inside(*guestItem)); REQUIRE(guestContents);
    const auto hostId = world.GetItem(*hostItem)->id;
    const auto guestId = world.GetItem(*guestItem)->id;
    // Publish the administrative fixture changes through a normal round.
    REQUIRE(host.QueueLocal({CommandKind::wait}) != 0); REQUIRE(guest.Submit({CommandKind::wait}));
    REQUIRE(poll([&] { return guest.View().inventory.size() == 2; }));
    REQUIRE(guest.View().inventory[0].id == guestId);
    CHECK(guest.View().inventory[1].parent == guestId);
    f.view = f.server.View(); f.ui.Open(InputAction::Inventory); f.Draw();
    CHECK(Find(f.Draw(), "> " + std::to_string(hostId) + ":rock"));
    REQUIRE(f.ui.HandleEvent(ftxui::Event::Return, f.view, f.localization, f.keys, [&](Command command) {
        CHECK(command.item == hostId); return host.QueueLocal(command) != 0;
    }));
    CHECK_FALSE(f.ui.IsOpen());
    f.view = guest.View(); f.ui.Open(InputAction::Inventory); f.Draw();
    CHECK(Find(f.Draw(), "> " + std::to_string(guestId) + ":backpack"));
    CHECK(Find(f.Draw(), "1 inside"));
    CHECK(Find(f.Draw(), "[in #" + std::to_string(guestId)));
    REQUIRE(f.ui.HandleEvent(ftxui::Event::Return, f.view, f.localization, f.keys, [&](Command command) {
        CHECK(command.item == guestId); return guest.Submit(command).has_value();
    }));
    CHECK_FALSE(f.ui.IsOpen());
    REQUIRE(poll([&] { return f.server.View().inventory.empty() && guest.View().inventory.empty(); }));
    CHECK(world.GetItem(*hostItem)->location.kind == ItemLocation::Ground({0, 0}).kind);
    CHECK(world.GetItem(*guestItem)->location == ItemLocation::Ground(guest.View().player_position));
    CHECK(world.GetItem(*guestContents)->location == ItemLocation::Inside(*guestItem));
    host.Close();
}

}
