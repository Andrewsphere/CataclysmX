#include "terminal/settings_menu.h"
#include "content_fixture.h"
#include "settings_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <ftxui/component/event.hpp>
#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>

#include <algorithm>

namespace {
using namespace cataclysmx;

ftxui::Screen RenderSettings(const ftxui::Component& menu, int width = 120, int height = 32) {
    auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(width), ftxui::Dimension::Fixed(height));
    ftxui::Render(screen, menu->Render()); return screen;
}
std::string Plain(const ftxui::Screen& screen) {
    std::string result;
    for (int row = 0; row < screen.dimy(); ++row) {
        for (int col = 0; col < screen.dimx(); ++col) {
            const auto& character = screen.PixelAt(col, row).character;
            result += character.empty() ? " " : character;
        }
        result += '\n';
    }
    return result;
}
std::string DrawSettings(const ftxui::Component& menu, int width = 120, int height = 32) {
    return Plain(RenderSettings(menu, width, height));
}
struct CellPosition { int x; int y; };
eastl::optional<CellPosition> FindText(const ftxui::Screen& screen, const std::string& text,
    int right = 1000000, int top = 0) {
    for (int y = top; y < screen.dimy(); ++y) {
        for (int x = 0; x < std::min(screen.dimx(), right); ++x) {
            std::string candidate;
            for (int end = x; end < screen.dimx() && candidate.size() < text.size(); ++end) {
                const auto& character = screen.PixelAt(end, y).character;
                candidate += character.empty() ? " " : character;
            }
            if (candidate == text) { return CellPosition{x, y}; }
        }
    }
    return {};
}
void Key(const ftxui::Component& menu, char key) { REQUIRE(menu->OnEvent(ftxui::Event::Character(key))); }
void Event(const ftxui::Component& menu, const ftxui::Event& event) { REQUIRE(menu->OnEvent(event)); }
// These helpers start and finish with the list focused; they use the same public
// key events as the player rather than inspecting private component state.
void NextScope(const ftxui::Component& menu) {
    Event(menu, ftxui::Event::Tab); Event(menu, ftxui::Event::Tab);
    Event(menu, ftxui::Event::ArrowRight); Event(menu, ftxui::Event::Return);
}
void NextSection(const ftxui::Component& menu) {
    Event(menu, ftxui::Event::TabReverse); Event(menu, ftxui::Event::TabReverse);
    Event(menu, ftxui::Event::ArrowRight); Event(menu, ftxui::Event::Return);
}
void NextCategory(const ftxui::Component& menu, unsigned count = 1) {
    Event(menu, ftxui::Event::TabReverse);
    for (unsigned i = 0; i < count; ++i) { Event(menu, ftxui::Event::ArrowRight); }
    Event(menu, ftxui::Event::Return);
}
void Number(const ftxui::Component& menu, const char* number) {
    Event(menu, ftxui::Event::Return);
    for (const char* p = number; *p; ++p) { Key(menu, *p); }
    Event(menu, ftxui::Event::Return);
}
struct MenuFixture {
    SettingsDirectory files;
    SettingsStore store{String(files.File("user").c_str()), String(files.File("world").c_str())};
    Localization localization;
    Server server{TestContent(), 197};
    TerminalKeyboard keyboard;
    int closed = 0;
    ftxui::Component menu;
    MenuFixture() {
        REQUIRE(localization.Load(CATACLYSMX_LOC_DIRECTORY, "en"));
        Open();
    }
    void Open() {
        menu = CreateSettingsMenu(store, localization, server, keyboard, CATACLYSMX_LOC_DIRECTORY, [&] { ++closed; });
    }
};

TEST_CASE("Settings editor accepts radius 64 rejects 65 and restores inherited default", "[terminal][settings]") {
    MenuFixture f;
    CHECK(f.store.Effective().viewRadius == 64);
    Event(f.menu, ftxui::Event::ArrowDown);
    Number(f.menu, "0"); Key(f.menu, 'a');
    REQUIRE(f.store.Effective().viewRadius == 0);
    Number(f.menu, "64"); Key(f.menu, 'a');
    REQUIRE(f.store.Effective().viewRadius == 64);
    Number(f.menu, "65");
    CHECK(DrawSettings(f.menu).find("Invalid value") != std::string::npos);
    CHECK(f.store.Effective().viewRadius == 64);
    Event(f.menu, ftxui::Event::Escape);
    Event(f.menu, ftxui::Event::Backspace); Key(f.menu, 'a');
    CHECK_FALSE(f.store.Overrides(SettingsScope::Global).viewRadius);
    CHECK(f.store.Effective().viewRadius == 64);
}

TEST_CASE("Settings modal edits sparse scopes applies saves resets and never advances simulation", "[terminal][settings]") {
    MenuFixture f;
    CHECK(DrawSettings(f.menu).find("[Global]") != std::string::npos);
    CHECK(DrawSettings(f.menu).find("[Preferences]") != std::string::npos);
    CHECK(DrawSettings(f.menu).find("[Interface]") != std::string::npos);
    Event(f.menu, ftxui::Event::ArrowDown);
    Number(f.menu, "10");
    CHECK(f.store.Effective().viewRadius == 64);
    CHECK(DrawSettings(f.menu).find("Unapplied") != std::string::npos);
    Key(f.menu, 'a');
    CHECK(f.store.Effective().viewRadius == 10);
    CHECK(f.store.Dirty(SettingsScope::Global));
    CHECK(DrawSettings(f.menu).find("Applied, unsaved") != std::string::npos);
    Key(f.menu, 's');
    CHECK_FALSE(f.store.Dirty(SettingsScope::Global));
    CHECK(f.files.Read("user/config/settings.json5").find("view_radius") != std::string::npos);
    NextScope(f.menu);
    Event(f.menu, ftxui::Event::ArrowDown);
    Number(f.menu, "3"); Key(f.menu, 's');
    CHECK(f.store.Effective().viewRadius == 3);
    CHECK(f.store.Overrides(SettingsScope::World).viewRadius == 3);
    CHECK_FALSE(f.store.Overrides(SettingsScope::World).language);
    Event(f.menu, ftxui::Event::Backspace); Key(f.menu, 'a');
    CHECK(f.store.Effective().viewRadius == 10);
    CHECK_FALSE(f.store.Overrides(SettingsScope::World).viewRadius);
    CHECK(f.server.GetTick() == 0);
    CHECK(f.server.GetWorld().GetSeed() == 197);
    Event(f.menu, ftxui::Event::Escape); CHECK(f.closed == 1);
}

TEST_CASE("Settings show current residency and unrelated edits retain it", "[terminal][settings][circle]") {
    MenuFixture f;
    REQUIRE(f.server.SetCircleResidency(0, {13}));
    f.Open();
    NextCategory(f.menu); // World: seed, active, square radius, mode, circle radius.
    auto text = DrawSettings(f.menu);
    CHECK(text.find("Circle") != std::string::npos);
    CHECK(text.find("Inactive") != std::string::npos);
    CHECK(text.find("13") != std::string::npos);
    Event(f.menu, ftxui::Event::ArrowDown); // Active.
    CHECK(DrawSettings(f.menu).find("Current server policy") != std::string::npos);
    NextCategory(f.menu); // Timing.
    Number(f.menu, "37"); Key(f.menu, 'a');
    REQUIRE(f.server.GetCircleResidency());
    CHECK(f.server.GetCircleResidency()->radiusTiles == 13);
    CHECK(f.server.GetActiveRadius() == 0);
    CHECK(f.server.GetPacing().ticksPerSecond == 37);
    CHECK_FALSE(HasResidencyOverride(f.store.Overrides(SettingsScope::Global)));
    CHECK(f.server.GetTick() == 0);
}

TEST_CASE("Settings radius editors select units and world mode masks the global circle", "[terminal][settings][circle]") {
    MenuFixture f;
    NextCategory(f.menu); // World.
    for (int i = 0; i < 4; ++i) { Event(f.menu, ftxui::Event::ArrowDown); }
    Number(f.menu, "0"); Key(f.menu, 's'); // Circle radius 0, active radius remains 1.
    REQUIRE(f.server.GetCircleResidency());
    CHECK(f.server.GetCircleResidency()->radiusTiles == 0);
    CHECK(f.server.GetActiveRadius() == 1);
    CHECK(f.store.Overrides(SettingsScope::Global).residentRadiusTiles == 0);
    CHECK_FALSE(f.store.Overrides(SettingsScope::Global).residentRadius);
    Number(f.menu, "129");
    CHECK(DrawSettings(f.menu).find("Invalid value") != std::string::npos);
    Event(f.menu, ftxui::Event::Escape);
    NextScope(f.menu); NextCategory(f.menu); // World scope, World category.
    for (int i = 0; i < 3; ++i) { Event(f.menu, ftxui::Event::ArrowDown); }
    Event(f.menu, ftxui::Event::Return); Key(f.menu, 'a'); // Circle -> Square.
    CHECK_FALSE(f.server.GetCircleResidency());
    CHECK(f.store.Overrides(SettingsScope::World).residencyMode == ResidencyMode::Square);
    CHECK(f.store.Overrides(SettingsScope::Global).residentRadiusTiles == 0);
    Event(f.menu, ftxui::Event::Backspace); Key(f.menu, 'a'); // Inherit global circle again.
    REQUIRE(f.server.GetCircleResidency());
    CHECK(f.server.GetCircleResidency()->radiusTiles == 0);
    Event(f.menu, ftxui::Event::ArrowUp); // Square radius.
    Number(f.menu, "1"); Key(f.menu, 'a'); // Editing inactive radius explicitly chooses square.
    CHECK_FALSE(f.server.GetCircleResidency());
    CHECK(f.server.GetResidentRadius() == 1);
    CHECK_FALSE(f.store.Overrides(SettingsScope::World).residentRadiusTiles);
    CHECK(f.store.Source(SettingsField::ResidencyMode) == SettingsSource::World);
    CHECK(f.server.GetTick() == 0);
}

TEST_CASE("Settings capture rejects conflicts preserves case and key inspection consumes gameplay keys", "[terminal][settings]") {
    MenuFixture f;
    NextSection(f.menu);
    Event(f.menu, ftxui::Event::Return); Key(f.menu, 'g');
    INFO(DrawSettings(f.menu));
    CHECK(DrawSettings(f.menu).find("already bound") != std::string::npos);
    Key(f.menu, 'T'); Key(f.menu, 'a');
    CHECK(ResolveAction(f.store.Keys(), InputContext::Gameplay, {"T", false}) == InputAction::West);
    CHECK_FALSE(ResolveAction(f.store.Keys(), InputContext::Gameplay, {"t", false}));
    Key(f.menu, 'i'); Key(f.menu, 'g'); Key(f.menu, 'S');
    CHECK(DrawSettings(f.menu).find("53 => S") != std::string::npos);
    Event(f.menu, ftxui::Event::Escape);
    CHECK(f.server.GetTick() == 0);
    Event(f.menu, ftxui::Event::Backspace); Key(f.menu, 'a');
    CHECK(ResolveAction(f.store.Keys(), InputContext::Gameplay, {"h", false}) == InputAction::West);
    Key(f.menu, 'u'); Key(f.menu, 'a');
    CHECK_FALSE(ResolveAction(f.store.Keys(), InputContext::Gameplay, {"h", false}));
    CHECK_FALSE(ResolveAction(f.store.Keys(), InputContext::Gameplay, {"Left", false}));
}

TEST_CASE("Settings save failure and invalid numeric input remain readable without mutating accepted settings", "[terminal][settings]") {
    MenuFixture f;
    f.files.Write("occupied", "not a directory");
    f.store = SettingsStore(String(f.files.File("occupied").c_str()));
    f.Open();
    Event(f.menu, ftxui::Event::ArrowDown); Number(f.menu, "4"); Key(f.menu, 's');
    CHECK(f.store.Dirty(SettingsScope::Global));
    auto screen = RenderSettings(f.menu);
    INFO(Plain(screen));
    const auto failed = FindText(screen, "Could not save");
    REQUIRE(failed);
    CHECK(screen.PixelAt(failed->x, failed->y).foreground_color == ftxui::Color(ftxui::Color::Red));
    Event(f.menu, ftxui::Event::Return); Key(f.menu, '9'); Key(f.menu, '9'); Event(f.menu, ftxui::Event::Return);
    screen = RenderSettings(f.menu);
    CHECK(Plain(screen).find("Invalid value") != std::string::npos);
    CHECK(Plain(screen).find("from 0 to 64") != std::string::npos);
    CHECK(f.store.Effective().viewRadius == 4);
    Event(f.menu, ftxui::Event::Escape); Key(f.menu, 'a');
    CHECK(f.store.Effective().viewRadius == 4);
    CHECK(f.server.GetTick() == 0);
}

TEST_CASE("Settings language picker applies catalogs live and persists only the language override", "[terminal][settings][localization]") {
    MenuFixture f;
    std::error_code error;
    std::filesystem::copy_file(std::filesystem::path(CATACLYSMX_LOC_DIRECTORY) / "en.json5", f.files.root / "en.json5", error);
    REQUIRE_FALSE(error);
    f.files.Write("xx.json5", "{format_version:1,messages:{'settings.title':'Настройки'}}");
    f.menu = CreateSettingsMenu(f.store, f.localization, f.server, f.keyboard, f.files.root.string().c_str(), [] {});
    NextScope(f.menu);
    Event(f.menu, ftxui::Event::Return);
    Event(f.menu, ftxui::Event::ArrowDown); Event(f.menu, ftxui::Event::Return);
    CHECK(f.localization.Text("settings.title") == "Settings");
    Key(f.menu, 'a');
    CHECK(f.localization.Text("settings.title") == "Настройки");
    CHECK(DrawSettings(f.menu).find("Настройки") != std::string::npos);
    CHECK(f.store.Effective().language == "xx");
    Key(f.menu, 's');
    CHECK(f.files.Read("world/settings.json5").find("language") != std::string::npos);
    CHECK(f.files.Read("world/settings.json5").find("view_radius") == std::string::npos);
    Event(f.menu, ftxui::Event::Backspace); Key(f.menu, 'a');
    CHECK(f.store.Effective().language == "en");
    CHECK(f.localization.Text("settings.title") == "Settings");
    CHECK(f.server.GetTick() == 0);
}

TEST_CASE("Settings cascading tabs filter categories remember selections and preserve drafts per scope", "[terminal][settings]") {
    MenuFixture f;
    CHECK(DrawSettings(f.menu).find("Active radius") == std::string::npos);
    Event(f.menu, ftxui::Event::ArrowDown); Number(f.menu, "11");
    NextCategory(f.menu); // World.
    Event(f.menu, ftxui::Event::ArrowDown); // Active radius.
    NextCategory(f.menu); // Timing.
    CHECK(DrawSettings(f.menu).find("Tick rate") != std::string::npos);
    CHECK(DrawSettings(f.menu).find("Active radius") == std::string::npos);
    Event(f.menu, ftxui::Event::ArrowDown); Event(f.menu, ftxui::Event::Return); // Rate limit -> On.
    NextCategory(f.menu, 3); // Back to World.
    auto screen = RenderSettings(f.menu);
    const auto active = FindText(screen, "Active radius", 40); REQUIRE(active);
    CHECK(screen.PixelAt(active->x, active->y).inverted);
    NextScope(f.menu); // World scope opens its own Interface page.
    Event(f.menu, ftxui::Event::ArrowDown); Number(f.menu, "9");
    NextScope(f.menu); // Global restores World category and Active selection.
    CHECK(DrawSettings(f.menu).find("[World]") != std::string::npos);
    Key(f.menu, 'a');
    CHECK(f.store.Effective().viewRadius == 11);
    CHECK(f.store.Effective().pacing.rateLimited);
    CHECK_FALSE(f.store.Overrides(SettingsScope::World).viewRadius);
    NextScope(f.menu); Key(f.menu, 's');
    CHECK(f.store.Effective().viewRadius == 9);
    CHECK(f.store.Effective().pacing.rateLimited);
    CHECK(f.files.Read("world/settings.json5").find("rate_limited") == std::string::npos);
    CHECK(f.server.GetTick() == 0);
}

TEST_CASE("Settings keybinding tabs show only their context and retain selection and scroll", "[terminal][settings]") {
    MenuFixture f;
    NextSection(f.menu);
    CHECK(DrawSettings(f.menu).find("[Gameplay]") != std::string::npos);
    NextCategory(f.menu, 2); // Menu context.
    CHECK(DrawSettings(f.menu).find("[Menu]") != std::string::npos);
    for (int i = 0; i < 14; ++i) { Event(f.menu, ftxui::Event::ArrowDown); }
    const auto before = DrawSettings(f.menu, 80, 24);
    NextCategory(f.menu); // Debug.
    NextCategory(f.menu); // Items: the fifth context must have its own page.
    const auto items = DrawSettings(f.menu, 80, 24);
    CHECK(items.find("[Item inventory]") != std::string::npos);
    CHECK(items.find("use item") != std::string::npos);
    NextCategory(f.menu, 3); // Items -> Gameplay -> Interaction -> Menu.
    CHECK(DrawSettings(f.menu, 80, 24) == before);
    NextSection(f.menu); NextSection(f.menu);
    CHECK(DrawSettings(f.menu, 80, 24) == before);
    // The action list is filtered: unbind the remembered Menu action only.
    Key(f.menu, 'u'); Key(f.menu, 'a');
    CHECK(f.store.KeyOverrides(SettingsScope::Global).size() == 1);
    CHECK(f.store.KeyOverrides(SettingsScope::Global)[0].context == InputContext::Menu);
    CHECK(f.server.GetTick() == 0);
}

TEST_CASE("Settings expose gameplay modal bindings in both gameplay and interaction contexts", "[terminal][settings][gameplay]") {
    MenuFixture f;
    NextSection(f.menu);
    for (auto context : {InputContext::Gameplay, InputContext::Interaction}) {
        bool found = false;
        for (const auto& binding : DefaultKeymap()) {
            if (binding.context != context) { continue; }
            if (DrawSettings(f.menu).find("Open Inventory.") != std::string::npos) { found = true; break; }
            Event(f.menu, ftxui::Event::ArrowDown);
        }
        REQUIRE(found);
        Event(f.menu, ftxui::Event::Return); Key(f.menu, 'I'); Key(f.menu, 'a');
        CHECK(ResolveAction(f.store.Keys(), context, {"I", false}) == InputAction::Inventory);
        CHECK_FALSE(ResolveAction(f.store.Keys(), context, {"i", false}));
        CHECK(ResolveAction(f.store.Keys(), context, {"P", false}) == InputAction::Players);
        CHECK(ResolveAction(f.store.Keys(), context, {"F1", false}) == InputAction::Help);
        if (context == InputContext::Gameplay) { NextCategory(f.menu); }
    }
    CHECK(f.server.GetTick() == 0);
}

TEST_CASE("Settings booleans toggle numeric drafts replace text and cross-field validation waits for Apply", "[terminal][settings]") {
    MenuFixture f;
    NextCategory(f.menu, 2); // Timing.
    Event(f.menu, ftxui::Event::ArrowDown); Event(f.menu, ftxui::Event::Return);
    CHECK(DrawSettings(f.menu).find("On") != std::string::npos);
    CHECK_FALSE(f.store.Effective().pacing.rateLimited);
    Key(f.menu, 'a'); CHECK(f.store.Effective().pacing.rateLimited);
    Event(f.menu, ftxui::Event::ArrowDown); Event(f.menu, ftxui::Event::Return); Key(f.menu, 'a');
    CHECK_FALSE(f.store.Effective().pacing.pauseWhenIdle);
    NextCategory(f.menu, 2); // Interface.
    Event(f.menu, ftxui::Event::ArrowDown); Number(f.menu, "0"); Key(f.menu, 'a');
    CHECK(f.store.Effective().viewRadius == 0);
    Event(f.menu, ftxui::Event::Return); Key(f.menu, '1'); Event(f.menu, ftxui::Event::Escape);
    CHECK(f.store.Effective().viewRadius == 0);
    NextCategory(f.menu); // World.
    // Use a small explicit resident bubble so active radius 3 is invalid.
    Event(f.menu, ftxui::Event::ArrowDown); Event(f.menu, ftxui::Event::ArrowDown);
    Number(f.menu, "2"); Key(f.menu, 'a');
    CHECK(f.store.Effective().residentRadius == 2);
    Event(f.menu, ftxui::Event::ArrowUp);
    Number(f.menu, "3");
    CHECK(DrawSettings(f.menu).find("Invalid value") == std::string::npos);
    Key(f.menu, 'a');
    CHECK(f.store.Effective().activeRadius == 1);
    CHECK(DrawSettings(f.menu).find("Could not apply") != std::string::npos);
    Event(f.menu, ftxui::Event::ArrowDown); Number(f.menu, "4"); Key(f.menu, 'a');
    CHECK(f.store.Effective().activeRadius == 3);
    CHECK(f.store.Effective().residentRadius == 4);
    CHECK(f.server.GetTick() == 0);
}

TEST_CASE("Settings indicate draft override inheritance success and fixed world seed without color alone", "[terminal][settings]") {
    MenuFixture f;
    Event(f.menu, ftxui::Event::ArrowDown); Number(f.menu, "5");
    auto screen = RenderSettings(f.menu);
    auto cell = FindText(screen, "Unapplied"); REQUIRE(cell);
    CHECK(screen.PixelAt(cell->x, cell->y).foreground_color == ftxui::Color(ftxui::Color::Yellow));
    CHECK(Plain(screen).find("Applied value: 64") != std::string::npos);
    Key(f.menu, 's'); screen = RenderSettings(f.menu);
    cell = FindText(screen, "Saved"); REQUIRE(cell);
    CHECK(screen.PixelAt(cell->x, cell->y).foreground_color == ftxui::Color(ftxui::Color::Green));
    cell = FindText(screen, "Source: Global override"); REQUIRE(cell);
    CHECK(screen.PixelAt(cell->x, cell->y).foreground_color == ftxui::Color(ftxui::Color::Default));
    NextScope(f.menu); Event(f.menu, ftxui::Event::ArrowDown);
    CHECK(DrawSettings(f.menu).find("Source: Inherited from Global") != std::string::npos);
    CHECK(DrawSettings(f.menu).find("Reset restores: 5") != std::string::npos);
    Number(f.menu, "7"); Key(f.menu, 's');
    CHECK(DrawSettings(f.menu).find("Source: World override") != std::string::npos);
    Event(f.menu, ftxui::Event::Backspace);
    CHECK(DrawSettings(f.menu).find("Applied value: 7") != std::string::npos);
    NextCategory(f.menu); // World seed first.
    screen = RenderSettings(f.menu);
    cell = FindText(screen, "World seed"); REQUIRE(cell);
    CHECK_FALSE(screen.PixelAt(cell->x, cell->y).dim);
    CHECK(screen.PixelAt(cell->x, cell->y).foreground_color == ftxui::Color(ftxui::Color::Default));
    CHECK(Plain(screen).find("Read-only") != std::string::npos);
    Event(f.menu, ftxui::Event::Return); Key(f.menu, '9'); Event(f.menu, ftxui::Event::Backspace);
    CHECK_FALSE(f.store.Overrides(SettingsScope::World).seed);
    CHECK(f.server.GetWorld().GetSeed() == 197);
}

TEST_CASE("Settings layout adapts on its first frame and resize while retaining footer controls", "[terminal][settings]") {
    MenuFixture f;
    for (const int width : {80, 99, 100, 120, 99, 120, 80}) {
        const int height = width >= 100 ? 32 : 24;
        const auto screen = RenderSettings(f.menu, width, height);
        INFO(Plain(screen));
        const auto description = FindText(screen, "Choose the language"); REQUIRE(description);
        const auto language = FindText(screen, "Language"); REQUIRE(language);
        const auto view = FindText(screen, "View radius"); REQUIRE(view);
        const auto category = FindText(screen, "[Interface]"); REQUIRE(category);
        const auto timing = FindText(screen, "Timing"); REQUIRE(timing);
        const auto options = FindText(screen, "Options"); REQUIRE(options);
        if (width >= 100) {
            CHECK(description->x > view->x + 20);
            CHECK(timing->y > category->y);
            CHECK(options->x - category->x == 19);
        } else {
            CHECK(description->y > view->y);
            CHECK(timing->y == category->y);
            CHECK(category->y < options->y);
        }
        CHECK(screen.PixelAt(language->x, language->y).inverted);
        for (const auto text : {"[Global]", "[Preferences]", "[Interface]", "Apply", "Save", "Reset", "Close"}) {
            CHECK(Plain(screen).find(text) != std::string::npos);
        }
        CHECK(Plain(screen).find("Basic keyboard") == std::string::npos);
        CHECK(Plain(screen).find("Generation version") == std::string::npos);
    }
    // A separately opened modal must also choose its wide layout on frame one.
    f.Open();
    const auto firstWide = RenderSettings(f.menu, 100, 32);
    const auto categories = FindText(firstWide, "Categories"); REQUIRE(categories);
    const auto options = FindText(firstWide, "Options"); REQUIRE(options);
    CHECK(categories->y == options->y);
    CHECK(options->x > categories->x);
    CHECK(f.server.GetTick() == 0);
}

TEST_CASE("Settings long translated descriptions scroll independently and retain their viewport per category", "[terminal][settings][localization]") {
    MenuFixture f;
    const auto english = LoadLanguageCatalog(CATACLYSMX_LOC_DIRECTORY "/en.json5"); REQUIRE(english);
    String longDescription;
    for (int i = 0; i < 80; ++i) { longDescription += "Long translated description with Unicode Привет. "; }
    longDescription += "DESCRIPTION_END";
    LanguageCatalog translated{{"settings.ui.description.language", longDescription, {}}};
    REQUIRE(f.localization.SetCatalogs(*english, translated));
    auto screen = RenderSettings(f.menu, 80, 24);
    CHECK(Plain(screen).find("DESCRIPTION_END") == std::string::npos);
    Event(f.menu, ftxui::Event::Tab); // Details.
    // Render as the viewport moves, stopping when the end of the description is
    // visible rather than assuming it is also the end of the complete details.
    for (int i = 0; i < 500 && Plain(screen).find("DESCRIPTION_END") == std::string::npos; ++i) {
        Event(f.menu, ftxui::Event::ArrowDown);
        screen = RenderSettings(f.menu, 80, 24);
    }
    screen = RenderSettings(f.menu, 80, 24);
    INFO(Plain(screen));
    CHECK(Plain(screen).find("DESCRIPTION_END") != std::string::npos);
    CHECK(Plain(screen).find("View radius") != std::string::npos);
    CHECK(Plain(screen).find("Close") != std::string::npos);
    Event(f.menu, ftxui::Event::Return); // List.
    NextCategory(f.menu); NextCategory(f.menu, 3);
    CHECK(DrawSettings(f.menu, 80, 24).find("DESCRIPTION_END") != std::string::npos);
    CHECK(f.store.Effective().language == "en");
    CHECK(f.server.GetTick() == 0);
}

TEST_CASE("Settings descriptions cover every field and action and keyboard limitations appear only when relevant", "[terminal][settings][localization]") {
    MenuFixture f;
    for (unsigned i = 0; i < static_cast<unsigned>(SettingsField::Count); ++i) {
        const auto name = SettingName(static_cast<SettingsField>(i));
        for (const auto prefix : {"settings.ui.description.", "settings.ui.constraint.", "settings.ui.effect.", "settings.ui.label."}) {
            String key(prefix); key.append(name.data(), name.size());
            CHECK(f.localization.Text({key.data(), key.size()}) != String("[") + key + "]");
        }
    }
    for (const auto& binding : DefaultKeymap()) {
        String key = "settings.ui.action_help."; const auto name = ActionName(binding.action); key.append(name.data(), name.size());
        CHECK(f.localization.Text({key.data(), key.size()}) != String("[") + key + "]");
    }
    const Keymap keys{{InputContext::Gameplay, InputAction::West, {{"T", true}}}};
    REQUIRE(f.store.SetOverrides(SettingsScope::Global, {}, keys)); f.Open();
    CHECK(DrawSettings(f.menu).find("Basic keyboard") == std::string::npos);
    NextSection(f.menu);
    CHECK(DrawSettings(f.menu).find("Basic keyboard") != std::string::npos);
    Event(f.menu, ftxui::Event::ArrowDown);
    CHECK(DrawSettings(f.menu).find("Basic keyboard") == std::string::npos);
    CHECK(f.server.GetTick() == 0);
}

TEST_CASE("Settings navigation and footer use menu bindings while numeric entry retains explicit text controls", "[terminal][settings]") {
    MenuFixture f;
    const Keymap keys{
        {InputContext::Menu, InputAction::Next, {{"n", false}}},
        {InputContext::Menu, InputAction::Accept, {{"e", false}}},
        {InputContext::Menu, InputAction::Apply, {{"t", false}}},
        {InputContext::Menu, InputAction::SaveSettings, {{"S", false}}}
    };
    REQUIRE(f.store.SetOverrides(SettingsScope::Global, {}, keys)); f.Open();
    auto text = DrawSettings(f.menu, 80, 24);
    CHECK(text.find("e Edit") != std::string::npos);
    CHECK(text.find("t Apply") != std::string::npos);
    CHECK(text.find("S Save") != std::string::npos);
    Key(f.menu, 'n'); Key(f.menu, 'n'); // Scope tabs.
    Event(f.menu, ftxui::Event::ArrowRight); Key(f.menu, 'e');
    Event(f.menu, ftxui::Event::ArrowDown); Key(f.menu, 'e');
    CHECK(DrawSettings(f.menu, 80, 24).find("Enter Accept") != std::string::npos);
    Key(f.menu, '6'); Event(f.menu, ftxui::Event::Return); Key(f.menu, 't');
    CHECK(f.store.Overrides(SettingsScope::World).viewRadius == 6);
    CHECK(f.store.Effective().viewRadius == 6);
    CHECK(f.server.GetTick() == 0);
}

TEST_CASE("Settings language picker cancellation leaves inheritance unchanged", "[terminal][settings]") {
    MenuFixture f;
    Event(f.menu, ftxui::Event::Return);
    CHECK(DrawSettings(f.menu).find("Choose a language") != std::string::npos);
    Event(f.menu, ftxui::Event::Escape);
    CHECK_FALSE(f.store.Overrides(SettingsScope::Global).language);
    CHECK(DrawSettings(f.menu).find("Unapplied") == std::string::npos);
    CHECK(f.server.GetTick() == 0);
}

TEST_CASE("Settings use terminal colors and distinct selection and focus styling", "[terminal][settings]") {
    MenuFixture f;
    for (const int width : {80, 120}) {
        auto screen = RenderSettings(f.menu, width, width == 80 ? 24 : 32);
        const auto global = FindText(screen, "[Global]"); REQUIRE(global);
        const auto world = FindText(screen, "World"); REQUIRE(world);
        const auto language = FindText(screen, "Language"); REQUIRE(language);
        const auto view = FindText(screen, "View radius"); REQUIRE(view);
        for (const auto cell : {*global, *world, *language, *view}) {
            CHECK_FALSE(screen.PixelAt(cell.x, cell.y).dim);
            CHECK(screen.PixelAt(cell.x, cell.y).foreground_color == ftxui::Color(ftxui::Color::Default));
            CHECK(screen.PixelAt(cell.x, cell.y).background_color == ftxui::Color(ftxui::Color::Default));
        }
        CHECK(screen.PixelAt(global->x, global->y).bold);
        CHECK_FALSE(screen.PixelAt(global->x, global->y).inverted);
        CHECK(screen.PixelAt(language->x, language->y).inverted);
        CHECK(screen.PixelAt(language->x - 4, language->y).character == ">");
        const auto languageValue = FindText(screen, "en", 1000000, language->y); REQUIRE(languageValue);
        CHECK(screen.PixelAt(languageValue->x, languageValue->y).inverted);
        // Numeric values are right-aligned with the two-character language code.
        CHECK(screen.PixelAt(languageValue->x, view->y).character == "6");
        CHECK(screen.PixelAt(languageValue->x + 1, view->y).character == "4");
        Event(f.menu, ftxui::Event::ArrowDown);
        screen = RenderSettings(f.menu, width, width == 80 ? 24 : 32);
        CHECK(screen.PixelAt(view->x, view->y).inverted);
        CHECK_FALSE(screen.PixelAt(language->x, language->y).inverted);
        CHECK(screen.PixelAt(view->x - 4, view->y).character == ">");
        Event(f.menu, ftxui::Event::Tab); // Details keep a visible focus header when scrolled.
        screen = RenderSettings(f.menu, width, width == 80 ? 24 : 32);
        const auto details = FindText(screen, "Details"); REQUIRE(details);
        CHECK(screen.PixelAt(details->x, details->y).inverted);
        CHECK_FALSE(screen.PixelAt(view->x, view->y).inverted);
        CHECK(screen.PixelAt(view->x, view->y).bold);
        Event(f.menu, ftxui::Event::Return);
        Event(f.menu, ftxui::Event::ArrowUp);
    }
    CHECK(f.server.GetTick() == 0);
}

TEST_CASE("Settings tabs footer and title feedback stay grouped instead of stretching across the screen", "[terminal][settings]") {
    MenuFixture f;
    const auto narrow = RenderSettings(f.menu, 80, 24);
    const auto wide = RenderSettings(f.menu, 120, 32);
    for (const auto label : {"World", "Keybindings", "a Apply", "s Save", "Backspace Reset", "Escape Close"}) {
        const auto first = FindText(narrow, label); REQUIRE(first);
        const auto second = FindText(wide, label); REQUIRE(second);
        CHECK(first->x == second->x);
        CHECK(second->x < 55);
    }
    CHECK(Plain(wide).find("Enter Edit  |  a Apply  |  s Save  |  Backspace Reset") != std::string::npos);
    Event(f.menu, ftxui::Event::ArrowDown); Number(f.menu, "6");
    for (const auto width : {80, 120}) {
        const auto screen = RenderSettings(f.menu, width, width == 80 ? 24 : 32);
        const auto title = FindText(screen, "Settings"); REQUIRE(title);
        const auto status = FindText(screen, "Unapplied"); REQUIRE(status);
        CHECK(status->y == title->y);
        CHECK(status->x - title->x < 15);
    }
    CHECK(f.server.GetTick() == 0);
}

TEST_CASE("Settings category sidebar uses resolved directions and changes orientation on resize", "[terminal][settings]") {
    MenuFixture f;
    const Keymap keys{
        {InputContext::Menu, InputAction::North, {{"N", false}}},
        {InputContext::Menu, InputAction::South, {{"J", false}}}
    };
    REQUIRE(f.store.SetOverrides(SettingsScope::Global, {}, keys)); f.Open();
    auto screen = RenderSettings(f.menu, 120, 32);
    Event(f.menu, ftxui::Event::TabReverse); // Categories.
    Key(f.menu, 'N'); // Wrap Interface -> Saving.
    CHECK(DrawSettings(f.menu).find("[Saving]") != std::string::npos);
    Key(f.menu, 'J'); // Wrap back to Interface.
    CHECK(DrawSettings(f.menu).find("[Interface]") != std::string::npos);
    Event(f.menu, ftxui::Event::ArrowDown); // The old physical binding no longer selects.
    CHECK(DrawSettings(f.menu).find("[Interface]") != std::string::npos);
    Key(f.menu, 'J'); // World.
    CHECK(DrawSettings(f.menu).find("[World]") != std::string::npos);
    Event(f.menu, ftxui::Event::ArrowRight); // Left/right remain aliases.
    CHECK(DrawSettings(f.menu).find("[Timing]") != std::string::npos);
    Event(f.menu, ftxui::Event::ArrowLeft);
    screen = RenderSettings(f.menu, 99, 24);
    Key(f.menu, 'J'); // Up/down have no category meaning in the horizontal strip.
    CHECK(DrawSettings(f.menu, 99, 24).find("[World]") != std::string::npos);
    Event(f.menu, ftxui::Event::ArrowRight);
    screen = RenderSettings(f.menu, 100, 32);
    const auto timing = FindText(screen, "[Timing]"); REQUIRE(timing);
    CHECK(screen.PixelAt(timing->x, timing->y).inverted);
    Key(f.menu, 'J');
    CHECK(DrawSettings(f.menu, 100, 32).find("[Saving]") != std::string::npos);
    CHECK(f.server.GetTick() == 0);
}

TEST_CASE("Settings focus traverses the visible structure in both directions at either width", "[terminal][settings]") {
    for (const int width : {80, 120}) {
        MenuFixture f;
        const char* focusLabels[] = {"Language", "Details", "[Global]", "[Preferences]", "[Interface]"};
        for (const bool reverse : {false, true}) {
            for (int step = 0; step < 5; ++step) {
                const int selected = reverse ? (5 - step) % 5 : step;
                const auto screen = RenderSettings(f.menu, width, width == 80 ? 24 : 32);
                INFO(Plain(screen));
                for (int area = 0; area < 5; ++area) {
                    const auto label = FindText(screen, focusLabels[area]); REQUIRE(label);
                    CHECK(screen.PixelAt(label->x, label->y).inverted == (area == selected));
                }
                Event(f.menu, reverse ? ftxui::Event::TabReverse : ftxui::Event::Tab);
            }
        }
        CHECK(f.server.GetTick() == 0);
    }
}

TEST_CASE("Settings long translated labels and binding previews stay within their panes", "[terminal][settings][localization]") {
    MenuFixture f;
    const Keymap keys{{InputContext::Gameplay, InputAction::West,
        {{"Left", true}, {"Right", true}, {"Home", true}, {"End", true}}}};
    REQUIRE(f.store.SetOverrides(SettingsScope::Global, {}, keys)); f.Open();
    const auto english = LoadLanguageCatalog(CATACLYSMX_LOC_DIRECTORY "/en.json5"); REQUIRE(english);
    const LanguageCatalog translated{
        {"settings.ui.label.language", "Очень длинное название языка интерфейса", {}},
        {"settings.ui.category.interface", "Очень длинная категория интерфейса", {}},
        {"settings.preferences", "Очень длинное название раздела настроек интерфейса", {}},
        {"action.west", "Очень длинное название действия движения на запад", {}}
    };
    REQUIRE(f.localization.SetCatalogs(*english, translated));
    for (const int width : {80, 100, 120}) {
        const int height = width == 80 ? 24 : 32;
        auto screen = RenderSettings(f.menu, width, height);
        INFO(Plain(screen));
        const auto view = FindText(screen, "View radius"); REQUIRE(view);
        CHECK(Plain(screen).find("Apply") != std::string::npos);
        CHECK(Plain(screen).find("Close") != std::string::npos);
        CHECK(screen.PixelAt(width - 1, view->y).character == "│");
        if (width >= 100) {
            CHECK(screen.PixelAt(19, view->y).character == "│");
            const auto divider = 20 + (width - 22) * 45 / 100;
            CHECK(screen.PixelAt(divider, view->y).character == "│");
        }
        NextSection(f.menu);
        screen = RenderSettings(f.menu, width, height);
        INFO(Plain(screen));
        // Full bindings remain available in details even when the list preview clips.
        CHECK(Plain(screen).find("Ctrl+Home") != std::string::npos);
        CHECK(Plain(screen).find("Ctrl+End") != std::string::npos);
        CHECK(Plain(screen).find("Close") != std::string::npos);
        NextSection(f.menu);
    }
    CHECK(f.server.GetTick() == 0);
}

TEST_CASE("Settings editors retain modes drafts and focus across layout changes", "[terminal][settings]") {
    MenuFixture f;
    const int widths[] = {80, 100, 120, 99, 80};
    Event(f.menu, ftxui::Event::ArrowDown);
    Event(f.menu, ftxui::Event::Return); Key(f.menu, '6');
    for (const int width : widths) {
        const auto screen = RenderSettings(f.menu, width, width < 100 ? 24 : 32);
        INFO(Plain(screen));
        CHECK(Plain(screen).find("6_") != std::string::npos);
        CHECK(Plain(screen).find("Enter Accept") != std::string::npos);
        CHECK(Plain(screen).find("Escape Cancel") != std::string::npos);
        const auto view = FindText(screen, "View radius", width >= 100 ? width / 2 : width); REQUIRE(view);
        CHECK(screen.PixelAt(view->x, view->y).inverted);
    }
    Event(f.menu, ftxui::Event::Return);
    for (const int width : widths) {
        const auto screen = RenderSettings(f.menu, width, width < 100 ? 24 : 32);
        const auto view = FindText(screen, "View radius", width >= 100 ? width / 2 : width); REQUIRE(view);
        CHECK(screen.PixelAt(view->x - 4, view->y).character == ">");
        CHECK(screen.PixelAt(view->x - 2, view->y).character == "*");
        CHECK(Plain(screen).find("Unapplied") != std::string::npos);
    }
    CHECK(f.store.Effective().viewRadius == 64);
    Key(f.menu, 'a'); CHECK(f.store.Effective().viewRadius == 6);

    NextSection(f.menu); Event(f.menu, ftxui::Event::Return); // Binding capture.
    for (const int width : widths) {
        const auto screen = RenderSettings(f.menu, width, width < 100 ? 24 : 32);
        CHECK(Plain(screen).find("Press a key to bind") != std::string::npos);
        CHECK(Plain(screen).find("Escape Cancel") != std::string::npos);
        CHECK(Plain(screen).find("Actions") != std::string::npos);
    }
    Key(f.menu, 'T'); // Accept a draft binding after the resizes.
    CHECK(ResolveAction(f.store.Keys(), InputContext::Gameplay, {"h", false}) == InputAction::West);
    Key(f.menu, 'i'); Key(f.menu, 'S'); // Inspect without replacing the draft.
    for (const int width : widths) {
        const auto screen = RenderSettings(f.menu, width, width < 100 ? 24 : 32);
        INFO(Plain(screen));
        const auto details = FindText(screen, "Details"); REQUIRE(details);
        CHECK(screen.PixelAt(details->x, details->y).inverted);
        CHECK(Plain(screen).find("53 => S") != std::string::npos);
        CHECK(Plain(screen).find("Escape Cancel") != std::string::npos);
    }
    Event(f.menu, ftxui::Event::Escape);
    Key(f.menu, 'a');
    CHECK(ResolveAction(f.store.Keys(), InputContext::Gameplay, {"T", false}) == InputAction::West);

    NextSection(f.menu); Event(f.menu, ftxui::Event::ArrowUp);
    Event(f.menu, ftxui::Event::Return); // Language picker.
    for (const int width : widths) {
        const auto screen = RenderSettings(f.menu, width, width < 100 ? 24 : 32);
        CHECK(Plain(screen).find("Languages") != std::string::npos);
        const auto language = FindText(screen, "> en"); REQUIRE(language);
        CHECK(screen.PixelAt(language->x, language->y).inverted);
        CHECK(Plain(screen).find("Choose a language") != std::string::npos);
        CHECK(Plain(screen).find("Escape Cancel") != std::string::npos);
    }
    Event(f.menu, ftxui::Event::Escape);
    CHECK_FALSE(f.store.Overrides(SettingsScope::Global).language);
    CHECK(f.server.GetTick() == 0);
}

}
