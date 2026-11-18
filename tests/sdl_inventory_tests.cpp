#include "sdl/inventory_screen.h"
#include "sdl/input.h"
#include "sdl/modal.h"
#include "client/item_browser.h"
#include "content_fixture.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdlib>
#include <thread>

using namespace cataclysmx;
using namespace cataclysmx::sdl;

extern const char* const expansionItemIds[80];

namespace {
struct Display {
    SDL_Surface* surface = nullptr;
    SDL_Renderer* renderer = nullptr;
    Display(int width, int height) {
        SDL_Init(0);
        surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32);
        if (surface) { renderer = SDL_CreateSoftwareRenderer(surface); }
    }
    ~Display() { SDL_DestroyRenderer(renderer); SDL_DestroySurface(surface); SDL_Quit(); }
};
std::uint64_t SurfaceHash(SDL_Surface* surface) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (int y = 0; y < surface->h; ++y) {
        for (int x = 0; x < surface->w; ++x) {
            Uint8 r = 0, g = 0, b = 0, a = 0;
            REQUIRE(SDL_ReadSurfacePixel(surface, x, y, &r, &g, &b, &a));
            for (const auto value : {r, g, b, a}) { hash = (hash ^ value) * 1099511628211ULL; }
        }
    }
    return hash;
}
}

TEST_CASE("SDL item icons fit the cropped image rather than map cells", "[sdl][inventory]") {
    const auto tall = FitItemIcon({10, 20, 40, 40}, 32, 64);
    CHECK(tall.x == 20); CHECK(tall.y == 20); CHECK(tall.w == 20); CHECK(tall.h == 40);
    const auto wide = FitItemIcon({10, 20, 40, 40}, 128, 32);
    CHECK(wide.x == 10); CHECK(wide.y == 35); CHECK(wide.w == 40); CHECK(wide.h == 10);
    CHECK(FitItemIcon({}, 0, 0).w == 0);
}

TEST_CASE("SDL item pane scrolling retains selected rows and handles empty lists", "[sdl][inventory]") {
    CHECK(ItemPaneScroll(999, 0, 0, false, 4) == 0);
    CHECK(ItemPaneScroll(0, 30, 29, true, 4) == 26);
    CHECK(ItemPaneScroll(26, 30, 0, true, 4) == 0);
    CHECK(ItemPaneScroll(20, 2, 1, true, 8) == 0);
    CHECK(ItemPaneScroll(-1, 30, 29, true, 0) == 29);
    CHECK(ItemPaneScroll(999, 30, 100, false, 4) == 26);
}

TEST_CASE("SDL inventory text sizes retain selected rows in compact and wide windows", "[sdl][inventory][text-size]") {
    for (const auto size : {SDL_Point{480, 360}, SDL_Point{1120, 780}}) {
        Display display(size.x, size.y); REQUIRE(display.renderer);
        const auto footerHash = [&] {
            std::uint64_t hash = 14695981039346656037ULL;
            for (int y = size.y - 32; y < size.y - 8; ++y) {
                for (int x = 8; x < size.x - 8; ++x) {
                    Uint8 r, g, b, a;
                    REQUIRE(SDL_ReadSurfacePixel(display.surface, x, y, &r, &g, &b, &a));
                    for (const auto value : {r, g, b, a}) { hash = (hash ^ value) * 1099511628211ULL; }
                }
            }
            return hash;
        };
        SdlAssets assets;
        for (int i = 0; i < 1000000 && assets.Poll(display.renderer) == AssetState::Loading; ++i) { std::this_thread::yield(); }
        REQUIRE(assets.State() == AssetState::Ready);
        for (int points : {8, 14, 24, 48}) {
            SdlText text(display.renderer, nullptr, static_cast<float>(points)); REQUIRE(text.Ready());
            ItemUiFrame frame; frame.screen = ItemUiScreen::Inventory;
            frame.title = "Inventory"; frame.footer = "Escape close";
            frame.panes[0].title = "Inventory";
            for (int i = 0; i < 20; ++i) {
                ItemUiRow row; row.type = TestContent().FindItem("rock"); row.name = "Rock";
                row.items = {static_cast<PersistentId>(i + 1)};
                frame.panes[0].rows.push_back(eastl::move(row));
            }
            frame.panes[0].selected = 19; frame.panes[0].selectionValid = true;
            REQUIRE(DrawInventoryScreen(display.renderer, text, assets, TestContent(), frame, size.x, size.y, 1));
            CHECK(frame.panes[0].scroll > 0); CHECK(frame.panes[0].scroll <= 19);
            REQUIRE(SDL_RenderPresent(display.renderer));
            const auto before = footerHash();
            frame.panes[0].rows.back().volumeMl = 999999;
            frame.panes[0].rows.back().massGrams = 333333;
            REQUIRE(DrawInventoryScreen(display.renderer, text, assets, TestContent(), frame, size.x, size.y, 1));
            REQUIRE(SDL_RenderPresent(display.renderer));
            CHECK(footerHash() == before);
            if (const char* path = std::getenv("CATACLYSMX_TEXT_SIZE_PREVIEW")) {
                const auto output = String(path) + ".inventory." + NumberText(size.x) + "." + NumberText(points) + ".png";
                REQUIRE(SDL_SavePNG(display.surface, output.c_str()));
            }
        }
    }
}

TEST_CASE("SDL inventory renders both capacities empty panes and compact selected rows", "[sdl][inventory][render]") {
    for (const auto size : {SDL_Point{480, 360}, SDL_Point{1120, 780}, SDL_Point{1600, 1200}}) {
        Display display(size.x, size.y); REQUIRE(display.renderer);
        SdlText text(display.renderer); REQUIRE(text.Ready());
        SdlAssets assets;
        for (int i = 0; i < 1000000 && assets.Poll(display.renderer) == AssetState::Loading; ++i) { std::this_thread::yield(); }
        REQUIRE(assets.State() == AssetState::Ready);
        ItemUiFrame frame; frame.screen = ItemUiScreen::Advanced;
        frame.title = "Advanced inventory"; frame.footer = "Tab pane | Enter transfer | Escape close";
        frame.panes[0].title = "Inventory"; frame.panes[0].capacity = "1 kg / 100 kg";
        frame.panes[1].title = "Floor"; frame.panes[1].capacity = "500 ml / unlimited";
        for (int i = 0; i < 40; ++i) {
            ItemUiRow row; row.type = TestContent().FindItem("rock"); row.name = "A named rock";
            row.items = {static_cast<PersistentId>(i + 1)}; row.volumeMl = 250; row.massGrams = 500;
            row.depth = i % 4; row.wielded = i == 39;
            frame.panes[0].rows.push_back(eastl::move(row));
        }
        frame.panes[0].selected = 39; frame.panes[0].selectionValid = true;
        REQUIRE(SDL_SetRenderScale(display.renderer, 1.5f, 1.5f));
        const SDL_Rect originalClip{2, 3, 100, 80};
        REQUIRE(SDL_SetRenderClipRect(display.renderer, &originalClip));
        REQUIRE(DrawInventoryScreen(display.renderer, text, assets, TestContent(), frame, size.x, size.y, 2));
        CHECK(frame.panes[0].scroll > 0); CHECK(frame.panes[0].scroll <= 39);
        CHECK(frame.panes[1].scroll == 0);
        CHECK(text.Width(70) > 0); CHECK(text.Width(71) > 0); // Both capacity lines.
        float x = 0, y = 0; REQUIRE(SDL_GetRenderScale(display.renderer, &x, &y));
        CHECK(x == 1.5f); CHECK(y == 1.5f);
        SDL_Rect clip{}; REQUIRE(SDL_GetRenderClipRect(display.renderer, &clip));
        CHECK(clip.x == 2); CHECK(clip.y == 3); CHECK(clip.w == 100); CHECK(clip.h == 80);
        frame.panes[0].selected = 0;
        REQUIRE(DrawInventoryScreen(display.renderer, text, assets, TestContent(), frame, size.x, size.y, 1));
        CHECK(frame.panes[0].scroll == 0);
        frame.screen = ItemUiScreen::Inventory;
        REQUIRE(DrawInventoryScreen(display.renderer, text, assets, TestContent(), frame, size.x, size.y, 1));
        CHECK(text.Width(122) > 0); // Table labels; missing tileset mapping uses a glyph icon.
        CHECK(text.Width(127) > 0);
    }
}

TEST_CASE("SDL item examination preserves colored UTF8 cells clips wide art and scrolls stats", "[sdl][inventory][render]") {
    Display display(480, 360); REQUIRE(display.renderer);
    SdlText text(display.renderer); REQUIRE(text.Ready());
    SdlAssets assets;
    for (int i = 0; i < 1000000 && assets.Poll(display.renderer) == AssetState::Loading; ++i) { std::this_thread::yield(); }
    REQUIRE(assets.State() == AssetState::Ready);
    TextArt art;
    art.lines = {{{"  /", ArtColor::Rgb(37, 219, 143), 3}, {"─\\  ", ArtColor::Rgb(90, 200, 210), 4}}}; art.width = 7;
    for (int i = 0; i < 100; ++i) { art.lines[0].push_back({"─", ArtColor::Rgb(88, 194, 132), 1}); ++art.width; }
    ItemUiFrame frame; frame.screen = ItemUiScreen::Examine; frame.title = "Examine";
    frame.examination.type = TestContent().FindItem("rock"); frame.examination.name = "Rock";
    frame.examination.description = "A description that wraps in a compact window.";
    frame.examination.art = &art;
    for (int i = 0; i < 60; ++i) { frame.examination.stats.push_back("An authored item statistic"); }
    REQUIRE(DrawInventoryScreen(display.renderer, text, assets, TestContent(), frame, 480, 360, 1));
    REQUIRE(SDL_RenderPresent(display.renderer));
    // The run palette reaches actual pixels, not just metadata.
    bool red = false, cyan = false;
    const auto redColor = ItemArtColor(ArtColor::Rgb(37, 219, 143)), cyanColor = ItemArtColor(ArtColor::Rgb(90, 200, 210));
    for (int y = 0; y < 360; ++y) {
        for (int x = 0; x < 480; ++x) {
            Uint8 r = 0, g = 0, b = 0, a = 0;
            REQUIRE(SDL_ReadSurfacePixel(display.surface, x, y, &r, &g, &b, &a));
            red |= r == redColor.r && g == redColor.g && b == redColor.b;
            cyan |= r == cyanColor.r && g == cyanColor.g && b == cyanColor.b;
        }
    }
    CHECK(red); CHECK(cyan);
    frame.examination.scroll = 9999;
    REQUIRE(DrawInventoryScreen(display.renderer, text, assets, TestContent(), frame, 480, 360, 1));
    CHECK(frame.examination.scroll > 0); CHECK(frame.examination.scroll < 9999);
    frame.examination.art = nullptr; frame.examination.stats.clear(); frame.examination.description.clear();
    REQUIRE(DrawInventoryScreen(display.renderer, text, assets, TestContent(), frame, 480, 360, 1));
    CHECK(frame.examination.scroll == 0); CHECK(text.Width(127) > 0); // Icon is independent of art.
}

TEST_CASE("SDL item submodal transitions stop held accept and quantity keys", "[sdl][inventory][input]") {
    Server server(TestContent());
    REQUIRE(server.GetWorld().CreateItem(TestContent().FindItem("rock"), ItemLocation::Carried(server.GetPlayer())));
    auto view = server.View();
    Localization localization; REQUIRE(localization.Load(CATACLYSMX_LOC_DIRECTORY, "en"));
    const auto keys = DefaultKeymap();
    ItemUi ui; ui.Open(); ui.Refresh(view, TestContent(), localization, keys);
    HeldKey held; ModalController modal(held);
    modal.BeginOpen(ModalScreen::ItemInventory); modal.Paused();
    SDL_Event enter{}; enter.type = SDL_EVENT_KEY_DOWN; enter.key.key = SDLK_RETURN; enter.key.type = SDL_EVENT_KEY_DOWN;
    REQUIRE(held.Observe(enter, 0));
    HandleInventoryKey(ui, held, {"Enter"}, keys, view, TestContent(), localization);
    REQUIRE(ui.Frame().screen == ItemUiScreen::Examine);
    CHECK_FALSE(held.Poll(1000, 10));
    CHECK(modal.Active() == ModalScreen::ItemInventory); CHECK(modal.CapturesInput());
    HandleInventoryKey(ui, held, {"Escape"}, keys, view, TestContent(), localization);
    ui.Close(); ui.Open(true); ui.Refresh(view, TestContent(), localization, keys);
    SDL_Event amount = enter; amount.key.key = SDLK_M;
    REQUIRE(held.Observe(amount, 1001));
    HandleInventoryKey(ui, held, {"m"}, keys, view, TestContent(), localization);
    REQUIRE(ui.Frame().screen == ItemUiScreen::Amount);
    CHECK_FALSE(held.Poll(2000, 10));
    HandleInventoryKey(ui, held, {"5"}, keys, view, TestContent(), localization);
    CHECK(ui.Frame().amount == "5");
    CHECK_FALSE(ui.TakeCommand(view));
    CHECK(modal.Active() != ModalScreen::Examine); // World Examine identity is separate.
}

TEST_CASE("SDL item command and cancellation keys cannot repeat across paid recovery", "[sdl][inventory][input]") {
    Server server(TestContent());
    REQUIRE(server.GetWorld().CreateItem(TestContent().FindItem("rock"), ItemLocation::Carried(server.GetPlayer())));
    auto view = server.View();
    Localization localization; REQUIRE(localization.Load(CATACLYSMX_LOC_DIRECTORY, "en"));
    const auto keys = DefaultKeymap();
    ItemUi ui; ui.Open(true); ui.Refresh(view, TestContent(), localization, keys);
    HeldKey held;
    SDL_Event key{}; key.type = SDL_EVENT_KEY_DOWN; key.key.type = SDL_EVENT_KEY_DOWN; key.key.key = SDLK_W;
    REQUIRE(held.Observe(key, 0));
    HandleInventoryKey(ui, held, {"w"}, keys, view, TestContent(), localization);
    REQUIRE(ui.NeedsTime()); CHECK(ui.Frame().screen == ItemUiScreen::Advanced);
    CHECK_FALSE(held.Poll(1000, 10)); // Queueing on the same screen terminates the hold.
    REQUIRE(ui.TakeCommand(view));
    CommandResult result; result.outcome = Outcome::wielded; result.advanced = true; result.cost = 100; result.tick = view.tick;
    ui.CommandFinished(result);
    view.remainingTicks = 100; ++view.revision;
    key.key.key = SDLK_ESCAPE; REQUIRE(held.Observe(key, 1001));
    HandleInventoryKey(ui, held, {"Escape"}, keys, view, TestContent(), localization);
    CHECK(ui.IsOpen()); CHECK(ui.NeedsTime()); CHECK_FALSE(held.Poll(2000, 10));
    view.remainingTicks = 0; view.tick += 100; ++view.revision;
    ui.Refresh(view, TestContent(), localization, keys);
    CHECK_FALSE(ui.NeedsTime()); CHECK(ui.IsOpen()); CHECK_FALSE(held.Poll(3000, 10));
}

TEST_CASE("SDL compact advanced inventory keeps the final supplied controls visible", "[sdl][inventory][render]") {
    Display display(480, 360); REQUIRE(display.renderer);
    SdlText text(display.renderer); REQUIRE(text.Ready());
    SdlAssets assets;
    for (int i = 0; i < 1000000 && assets.Poll(display.renderer) == AssetState::Loading; ++i) { std::this_thread::yield(); }
    REQUIRE(assets.State() == AssetState::Ready);
    Server server(TestContent());
    Localization localization; REQUIRE(localization.Load(CATACLYSMX_LOC_DIRECTORY, "en"));
    ItemUi ui; ui.Open(true); ui.Refresh(server.View(), TestContent(), localization, DefaultKeymap());
    // Equal-width final markers keep wrapping/layout identical; pixel changes
    // therefore demonstrate that the end of the actual long footer is visible.
    auto frame = ui.Frame(); frame.footer += " | ZZZZZZ";
    REQUIRE(DrawInventoryScreen(display.renderer, text, assets, TestContent(), frame, 480, 360, 1));
    REQUIRE(SDL_RenderPresent(display.renderer));
    const auto first = SurfaceHash(display.surface);
    frame.footer.replace(frame.footer.size() - 6, 6, "MMMMMM");
    REQUIRE(DrawInventoryScreen(display.renderer, text, assets, TestContent(), frame, 480, 360, 1));
    REQUIRE(SDL_RenderPresent(display.renderer));
    CHECK(SurfaceHash(display.surface) != first);
}

TEST_CASE("SDL colored art runs retain full monospaced widths and leading spaces", "[sdl][inventory][render]") {
    Display display(480, 360); REQUIRE(display.renderer);
    SdlText text(display.renderer); REQUIRE(text.Ready());
    REQUIRE(text.Draw(125, "M", -10000, -10000, {255, 255, 255, 255}));
    const int cell = text.Width(125);
    REQUIRE(text.Draw(126, "  /─\\", -10000, -10000, ItemArtColor(ArtColor::Rgb(220, 83, 83))));
    const int firstWidth = text.Width(126);
    REQUIRE(text.DrawCells(128, "  /─\\", 5, 20, 20, ItemArtColor(ArtColor::Rgb(220, 83, 83)), cell));
    CHECK(text.Width(128) == firstWidth); CHECK(firstWidth > cell * 2);
    REQUIRE(text.Draw(126, "  []  ", -10000, -10000, ItemArtColor(ArtColor::Rgb(90, 200, 210))));
    const int secondWidth = text.Width(126);
    REQUIRE(text.DrawCells(129, "  []  ", 6, float(20 + cell * 5), 20, ItemArtColor(ArtColor::Rgb(90, 200, 210)), cell));
    CHECK(text.Width(129) == secondWidth); CHECK(secondWidth > cell * 4);
    REQUIRE(text.DrawCells(128, "  /─\\", 5, 20, 20, ItemArtColor(ArtColor::Rgb(220, 83, 83)), cell));
    CHECK(text.Width(128) == firstWidth); CHECK(text.Width(129) == secondWidth);
}

TEST_CASE("SDL real inventory screens render bundled sprites descriptions and art", "[sdl][inventory][render]") {
    const auto& content = TestContent();
    Server server(content);
    auto& world = server.GetWorld();
    for (int index = 0; index < 3; ++index) {
        REQUIRE(world.CreateItem(content.FindItem("rock"), ItemLocation::Carried(server.GetPlayer())));
    }
    REQUIRE(world.CreateItem(content.FindItem("metal_pipe"), ItemLocation::Carried(server.GetPlayer())));
    const auto bag = world.CreateItem(content.FindItem("backpack"), ItemLocation::Carried(server.GetPlayer())); REQUIRE(bag);
    REQUIRE(world.CreateItem(content.FindItem("bandage"), ItemLocation::Inside(*bag)));
    const auto view = server.View(0);
    Localization localization; REQUIRE(localization.Load(CATACLYSMX_LOC_DIRECTORY, "en"));
    const auto keys = DefaultKeymap();
    for (const auto size : {SDL_Point{640, 480}, SDL_Point{1200, 800}}) {
        Display display(size.x, size.y); REQUIRE(display.renderer);
        SdlText text(display.renderer); REQUIRE(text.Ready());
        SdlAssets assets(&content);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (assets.Poll(display.renderer) == AssetState::Loading && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::yield();
        }
        REQUIRE(assets.State() == AssetState::Ready);
        REQUIRE(content.FindItem("backpack").value < assets.Definition().items.size());
        CHECK(assets.Definition().items[content.FindItem("backpack").value].kind != TilesetMappingKind::Missing);
        ItemUi ui;
        const auto draw = [&](const char* screen) {
            ui.Refresh(view, content, localization, keys);
            REQUIRE(DrawInventoryScreen(display.renderer, text, assets, content, ui.Frame(), size.x, size.y, 1));
            REQUIRE(SDL_RenderPresent(display.renderer));
            if (const char* path = std::getenv("CATACLYSMX_SDL_INVENTORY_PREVIEW")) {
                const auto output = String(path) + "." + screen + "." + NumberText(size.x) + ".png";
                REQUIRE(SDL_SavePNG(display.surface, output.c_str()));
            }
        };
        ui.Open(); draw("inventory");
        REQUIRE(ui.Frame().panes[0].rows[0].items.size() == 3);
        ui.Handle({"e"}, keys, view, content, localization); draw("examine");
        REQUIRE(ui.Frame().examination.art); CHECK_FALSE(ui.Frame().examination.description.empty());
        ui.Close(); ui.Open(true); draw("advanced");
        CHECK_FALSE(ui.Frame().panes[0].rows.empty()); CHECK_FALSE(ui.Frame().panes[1].rows.empty());
    }
}

TEST_CASE("SDL art uses context default only when requested", "[sdl][inventory]") {
    const auto normal = ItemArtColor({});
    CHECK(normal.r == 210); CHECK(normal.g == 220); CHECK(normal.b == 218); CHECK(normal.a == 255);
    const auto custom = ItemArtColor(ArtColor::Rgb(13, 79, 241));
    CHECK(custom.r == 13); CHECK(custom.g == 79); CHECK(custom.b == 241); CHECK(custom.a == 255);
    const auto black = ItemArtColor(ArtColor::Rgb(0, 0, 0));
    CHECK(black.r == 0); CHECK(black.g == 0); CHECK(black.b == 0);
    CHECK(ItemArtColor({13, 79, 241, true}).r == normal.r);
}

TEST_CASE("SDL item pilot renders five authored assets at compact and wide sizes", "[item-pilot][sdl][inventory][render]") {
    Diagnostics diagnostics;
    auto loaded = LoadContentPack(CATACLYSMX_CONTENT_PACK, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str()); REQUIRE(loaded);
    const auto& content = *loaded;
    Server server(content);
    for (const char* id : {"claw_hammer", "wood_scrap", "glass_bottle", "work_shirt", "arming_sword"}) {
        REQUIRE(server.GetWorld().CreateItem(content.FindItem(id), ItemLocation::Carried(server.GetPlayer())));
    }
    const auto view = server.View(0);
    Localization localization; REQUIRE(localization.Load(CATACLYSMX_LOC_DIRECTORY, "en"));
    for (const auto size : {SDL_Point{480, 360}, SDL_Point{1120, 900}}) {
        CAPTURE(size.x, size.y);
        Display display(size.x, size.y); REQUIRE(display.renderer);
        SdlText text(display.renderer); REQUIRE(text.Ready());
        SdlAssets assets(&content);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (assets.Poll(display.renderer) == AssetState::Loading && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::yield();
        }
        REQUIRE(assets.State() == AssetState::Ready);
        for (const char* id : {"claw_hammer", "wood_scrap", "glass_bottle", "work_shirt", "arming_sword"}) {
            CAPTURE(id);
            const auto type = content.FindItem(id); REQUIRE(type.value != 0);
            ItemUiFrame frame; frame.screen = ItemUiScreen::Examine;
            frame.title = "Examine"; frame.footer = "Up/Down scroll | Escape close";
            bool found = false;
            for (const auto& entry : view.inventory) {
                if (entry.type != type) { continue; }
                DescribeInventoryItem(entry, content, localization, frame.examination); found = true; break;
            }
            REQUIRE(found); REQUIRE(frame.examination.art == &content.GetItem(type).art);
            REQUIRE_FALSE(frame.examination.description.empty()); REQUIRE_FALSE(frame.examination.stats.empty());
            REQUIRE_FALSE(frame.examination.art->lines.empty());
            const SDL_Rect originalClip{2, 3, 100, 80};
            REQUIRE(SDL_SetRenderScale(display.renderer, 1.5f, 1.25f));
            REQUIRE(SDL_SetRenderClipRect(display.renderer, &originalClip));
            const auto draw = [&] {
                REQUIRE(DrawInventoryScreen(display.renderer, text, assets, content, frame, size.x, size.y, 1));
                REQUIRE(SDL_RenderPresent(display.renderer));
                float x = 0, y = 0; REQUIRE(SDL_GetRenderScale(display.renderer, &x, &y));
                CHECK(x == 1.5f); CHECK(y == 1.25f);
                REQUIRE(SDL_RenderClipEnabled(display.renderer));
                SDL_Rect clip{}; REQUIRE(SDL_GetRenderClipRect(display.renderer, &clip));
                CHECK(clip.x == originalClip.x); CHECK(clip.y == originalClip.y);
                CHECK(clip.w == originalClip.w); CHECK(clip.h == originalClip.h);
            };
            draw();
            const auto top = SurfaceHash(display.surface);
            if (const char* prefix = std::getenv("CATACLYSMX_ITEM_PILOT_PREVIEW")) {
                const auto output = String(prefix) + "_" + id + "_" + NumberText(size.x) + ".png";
                REQUIRE(SDL_SavePNG(display.surface, output.c_str()));
            }
            // Actual catalog art must reach pixels, including in the small pane.
            const auto* art = frame.examination.art;
            frame.examination.art = nullptr; draw();
            CHECK(SurfaceHash(display.surface) != top);
            frame.examination.art = art;
            frame.examination.scroll = 9999; draw();
            const int bottom = frame.examination.scroll;
            CHECK(bottom >= 0); CHECK(bottom < 9999);
            if (size.x == 480) { CHECK(bottom > 0); CHECK(SurfaceHash(display.surface) != top); }
            const auto bottomPixels = SurfaceHash(display.surface);
            frame.examination.scroll = 9999; draw();
            CHECK(frame.examination.scroll == bottom); CHECK(SurfaceHash(display.surface) == bottomPixels);
            frame.examination.scroll = 0; draw();
            CHECK(frame.examination.scroll == 0); CHECK(SurfaceHash(display.surface) == top);
        }
    }
}

TEST_CASE("SDL item slice and expansion render through the item browser", "[item-slice][item-expansion][sdl][inventory][render]") {
    Diagnostics diagnostics;
    auto loaded = LoadContentPack(CATACLYSMX_CONTENT_PACK, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str()); REQUIRE(loaded);
    const auto& content = *loaded;
    const auto obsolete = content.FindFlag("obsolete"); REQUIRE(obsolete.value);
    Localization localization; REQUIRE(localization.Load(CATACLYSMX_LOC_DIRECTORY, "en"));
    const auto keys = DefaultKeymap();
    ItemBrowser browser; browser.Open(content, localization);
    CHECK(browser.Frame().matches.size() == 176); // Authored definitions only; both generated containers are hidden.
    for (const auto type : browser.Frame().matches) {
        CHECK(type != content.CorpseItem());
        CHECK_FALSE(content.GetItem(type).unfinishedCraft);
    }
    for (const auto size : {SDL_Point{480, 360}, SDL_Point{1120, 900}}) {
        CAPTURE(size.x, size.y);
        Display display(size.x, size.y); REQUIRE(display.renderer);
        SdlText text(display.renderer); REQUIRE(text.Ready());
        SdlAssets assets(&content);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (assets.Poll(display.renderer) == AssetState::Loading && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::yield();
        }
        REQUIRE(assets.State() == AssetState::Ready);
        unsigned rendered = 0;
        unsigned learningRendered = 0;
        unsigned combatListed = 0;
        unsigned expansionRendered = 0;
        for (std::size_t index = 0; index < browser.Frame().matches.size(); ++index) {
            const auto type = browser.Frame().matches[index];
            const auto& item = content.GetItem(type);
            if (item.flags.Contains(obsolete)) { continue; }
            bool pilot = false;
            for (const char* id : {"claw_hammer", "wood_scrap", "glass_bottle", "work_shirt", "arming_sword"}) {
                pilot |= item.id == id;
            }
            if (pilot) { continue; }
            bool combat = false;
            for (const char* id : {"brass_knuckles", "wooden_shield", "boxing_manual",
                    "weapon_forms_manual", "cloth_face_wrap", "work_boots"}) {
                combat |= type == content.FindItem(id);
            }
            if (combat) {
                // These authored additions use the missing-art fallback, rather than slice artwork.
                CHECK(item.art.lines.empty());
                ++combatListed;
                continue;
            }
            bool expansion = false;
            for (const char* id : expansionItemIds) { expansion |= item.id == id; }
            CAPTURE(item.id.c_str());
            browser.Frame().selected = index;
            browser.Handle({"e"}, keys, content, localization);
            REQUIRE(browser.Frame().screen == ItemBrowserScreen::Examine);
            auto& frame = browser.Frame().examination;
            REQUIRE(frame.examination.art == &item.art);
            REQUIRE_FALSE(frame.examination.description.empty());
            const auto draw = [&] {
                REQUIRE(DrawInventoryScreen(display.renderer, text, assets, content, frame, size.x, size.y, 1));
                REQUIRE(SDL_RenderPresent(display.renderer));
            };
            draw();
            const auto top = SurfaceHash(display.surface);
            if (const char* prefix = std::getenv(expansion ? "CATACLYSMX_ITEM_EXPANSION_PREVIEW" : "CATACLYSMX_ITEM_SLICE_PREVIEW")) {
                const auto output = String(prefix) + "_" + item.id + "_" + NumberText(size.x) + ".png";
                REQUIRE(SDL_SavePNG(display.surface, output.c_str()));
            }
            frame.examination.art = nullptr; draw();
            CHECK(SurfaceHash(display.surface) != top);
            frame.examination.art = &item.art;
            frame.examination.scroll = 9999; draw();
            CHECK(frame.examination.scroll >= 0); CHECK(frame.examination.scroll < 9999);
            if (size.x == 480 && !expansion) { CHECK(frame.examination.scroll > 0); }
            frame.examination.scroll = 0; draw();
            CHECK(SurfaceHash(display.surface) == top);
            browser.Handle({"Escape"}, keys, content, localization);
            // Render the new learning carriers too, while retaining the original
            // sixty-item slice coverage independently of the catalog additions.
            if (expansion) { ++expansionRendered; }
            else if (item.id == "workshop_primer_print" || item.id == "salvage_notes_print"
                || item.id == "workshop_ebook_reader") { ++learningRendered; }
            else { ++rendered; }
        }
        CHECK(rendered == 60);
        CHECK(expansionRendered == 80);
        CHECK(learningRendered == 3);
        CHECK(combatListed == 6);
    }
}

TEST_CASE("SDL larger colored artwork retains overflow runs and stable slots", "[sdl][inventory][render]") {
    Display display(1120, 1200); REQUIRE(display.renderer);
    SdlText text(display.renderer); REQUIRE(text.Ready());
    SdlAssets assets;
    for (int i = 0; i < 1000000 && assets.Poll(display.renderer) == AssetState::Loading; ++i) { std::this_thread::yield(); }
    REQUIRE(assets.State() == AssetState::Ready);
    TextArt art; art.width = 24;
    for (int y = 0; y < 24; ++y) {
        ArtLine line;
        for (int x = 0; x < 24; ++x) {
            line.push_back({"M", (x + y) % 2 ? ArtColor::Rgb(13, 79, 241) : ArtColor::Rgb(37, 219, 143), 1});
        }
        art.lines.push_back(eastl::move(line));
    }
    // Index 512 was the final overwrite of slot 128 with modulo run indexing.
    art.lines[21][8].text = "MM"; art.lines[21][8].cells = 2; art.width = 25;
    ItemUiFrame frame; frame.screen = ItemUiScreen::Examine; frame.title = "Examine";
    frame.examination.type = TestContent().FindItem("rock"); frame.examination.name = "Rock";
    frame.examination.art = &art;
    REQUIRE(DrawInventoryScreen(display.renderer, text, assets, TestContent(), frame, 1120, 1200, 1));
    REQUIRE(SDL_RenderPresent(display.renderer));
    CHECK(text.Width(128) == text.Width(125)); // First run remains M, rather than overflow MM.
    const auto original = SurfaceHash(display.surface);
    // The final run is far beyond the stable-slot budget and must reach pixels.
    art.lines.back().back().color = ArtColor::Rgb(247, 31, 173);
    REQUIRE(DrawInventoryScreen(display.renderer, text, assets, TestContent(), frame, 1120, 1200, 1));
    REQUIRE(SDL_RenderPresent(display.renderer));
    CHECK(SurfaceHash(display.surface) != original);
    frame.examination.scroll = 8;
    REQUIRE(DrawInventoryScreen(display.renderer, text, assets, TestContent(), frame, 1120, 1200, 1));
    CHECK(frame.examination.scroll == 0); // The complete 24-row artwork fits.
}
