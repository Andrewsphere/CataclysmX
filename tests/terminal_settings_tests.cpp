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
    int right = 1000000, int top = 0, bool selected = false) {
    for (int y = top; y < screen.dimy(); ++y) {
        if (selected && screen.PixelAt(2, y).character != ">") { continue; }
        for (int x = 0; x < std::min(screen.dimx(), right); ++x) {
            if (selected && !screen.PixelAt(x, y).inverted) { continue; }
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
void NextScope(const ftxui::Component& menu) { Key(menu, 'g'); }
void NextSection(const ftxui::Component& menu) { Event(menu, ftxui::Event::Tab); }
void NextCategory(const ftxui::Component& menu, unsigned count = 1) {
    for (unsigned i = 0; i < count; ++i) { Event(menu, ftxui::Event::ArrowDownCtrl); }
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
    Number(f.menu, "0"); Key(f.menu, 's');
    REQUIRE(f.store.Effective().viewRadius == 0);
    Number(f.menu, "64"); Key(f.menu, 's');
    REQUIRE(f.store.Effective().viewRadius == 64);
    Number(f.menu, "65");
    CHECK(DrawSettings(f.menu).find("Invalid value") != std::string::npos);
    CHECK(f.store.Effective().viewRadius == 64);
    Event(f.menu, ftxui::Event::Escape);
    Event(f.menu, ftxui::Event::Backspace); Key(f.menu, 's');
    CHECK_FALSE(f.store.Overrides(SettingsScope::Global).viewRadius);
    CHECK(f.store.Effective().viewRadius == 64);
}

TEST_CASE("Settings modal edits sparse scopes applies saves resets and never advances simulation", "[terminal][settings]") {
    MenuFixture f;
    CHECK(DrawSettings(f.menu).find("[Global]") != std::string::npos);
    CHECK(DrawSettings(f.menu).find("[Preferences]") != std::string::npos);
    CHECK(DrawSettings(f.menu).find("Interface") != std::string::npos);
    Event(f.menu, ftxui::Event::ArrowDown);
    Number(f.menu, "10");
    CHECK(f.store.Effective().viewRadius == 64);
    CHECK(DrawSettings(f.menu).find("Unapplied") != std::string::npos);
    Key(f.menu, 's');
    CHECK(f.store.Effective().viewRadius == 10);
    CHECK_FALSE(f.store.Dirty(SettingsScope::Global));
    CHECK(f.files.Read("user/config/settings.json5").find("view_radius") != std::string::npos);
    NextScope(f.menu);
    Event(f.menu, ftxui::Event::ArrowDown);
    Number(f.menu, "3"); Key(f.menu, 's');
    CHECK(f.store.Effective().viewRadius == 3);
    CHECK(f.store.Overrides(SettingsScope::World).viewRadius == 3);
    CHECK_FALSE(f.store.Overrides(SettingsScope::World).language);
    Event(f.menu, ftxui::Event::Backspace); Key(f.menu, 's');
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
    Number(f.menu, "37"); Key(f.menu, 's');
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
    Event(f.menu, ftxui::Event::Return); Key(f.menu, 's'); // Circle -> Square.
    CHECK_FALSE(f.server.GetCircleResidency());
    CHECK(f.store.Overrides(SettingsScope::World).residencyMode == ResidencyMode::Square);
    CHECK(f.store.Overrides(SettingsScope::Global).residentRadiusTiles == 0);
    Event(f.menu, ftxui::Event::Backspace); Key(f.menu, 's'); // Inherit global circle again.
    REQUIRE(f.server.GetCircleResidency());
    CHECK(f.server.GetCircleResidency()->radiusTiles == 0);
    Event(f.menu, ftxui::Event::ArrowUp); // Square radius.
    Number(f.menu, "1"); Key(f.menu, 's'); // Editing inactive radius explicitly chooses square.
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
    Key(f.menu, 'J'); Key(f.menu, 's');
    CHECK(ResolveAction(f.store.Keys(), InputContext::Gameplay, {"J", false}) == InputAction::West);
    CHECK_FALSE(ResolveAction(f.store.Keys(), InputContext::Gameplay, {"j", false}));
    Key(f.menu, 'i'); Key(f.menu, 'g'); Key(f.menu, 'S');
    CHECK(DrawSettings(f.menu).find("53 => S") != std::string::npos);
    Event(f.menu, ftxui::Event::Escape);
    CHECK(f.server.GetTick() == 0);
    Event(f.menu, ftxui::Event::Backspace); Key(f.menu, 's');
    CHECK(ResolveAction(f.store.Keys(), InputContext::Gameplay, {"Left", false}) == InputAction::West);
    Key(f.menu, 'u'); Key(f.menu, 's');
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
    const auto failed = FindText(screen, "Applied, unsaved");
    REQUIRE(failed);
    CHECK(screen.PixelAt(failed->x, failed->y).foreground_color == ftxui::Color(ftxui::Color::RedLight));
    Event(f.menu, ftxui::Event::Return); Key(f.menu, '9'); Key(f.menu, '9'); Event(f.menu, ftxui::Event::Return);
    screen = RenderSettings(f.menu);
    CHECK(Plain(screen).find("Invalid value") != std::string::npos);
    CHECK(Plain(screen).find("from 0 to 64") != std::string::npos);
    CHECK(f.store.Effective().viewRadius == 4);
    Event(f.menu, ftxui::Event::Escape); Key(f.menu, 's');
    CHECK(f.store.Effective().viewRadius == 4);
    const auto revision = f.server.GetRevision();
    REQUIRE(std::filesystem::remove(f.files.root / "occupied"));
    Key(f.menu, 's');
    CHECK_FALSE(f.store.Dirty(SettingsScope::Global));
    CHECK(f.files.Read("occupied/config/settings.json5").find("view_radius") != std::string::npos);
    CHECK(f.server.GetRevision() == revision);
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
    Key(f.menu, 's');
    CHECK(f.localization.Text("settings.title") == "Настройки");
    CHECK(DrawSettings(f.menu).find("Настройки") != std::string::npos);
    CHECK(f.store.Effective().language == "xx");
    Key(f.menu, 's');
    CHECK(f.files.Read("world/settings.json5").find("language") != std::string::npos);
    CHECK(f.files.Read("world/settings.json5").find("view_radius") == std::string::npos);
    Event(f.menu, ftxui::Event::Backspace); Key(f.menu, 's');
    CHECK(f.store.Effective().language == "en");
    CHECK(f.localization.Text("settings.title") == "Settings");
    CHECK(f.server.GetTick() == 0);
}

TEST_CASE("Settings scope changes and closing discard only uncommitted edits", "[terminal][settings]") {
    MenuFixture f;
    Event(f.menu, ftxui::Event::ArrowDown);
    Number(f.menu, "11");
    NextSection(f.menu); Event(f.menu, ftxui::Event::TabReverse);
    CHECK(DrawSettings(f.menu).find("Applied value: 64") != std::string::npos);
    NextScope(f.menu); // World opens with a fresh draft.
    Event(f.menu, ftxui::Event::ArrowDown); Number(f.menu, "9"); Key(f.menu, 's');
    REQUIRE(f.store.Overrides(SettingsScope::World).viewRadius == 9);
    CHECK_FALSE(f.store.Overrides(SettingsScope::Global).viewRadius);
    CHECK(f.files.Read("user/config/settings.json5").empty());

    NextScope(f.menu);
    Event(f.menu, ftxui::Event::ArrowDown); Number(f.menu, "7");
    CHECK(DrawSettings(f.menu).find("This world has an override") != std::string::npos);
    Event(f.menu, ftxui::Event::Escape); REQUIRE(f.closed == 1);
    f.Open();
    CHECK(DrawSettings(f.menu).find("Unapplied") == std::string::npos);
    CHECK(f.store.Effective().viewRadius == 9);
    CHECK(f.server.GetTick() == 0);
}

TEST_CASE("Settings edit individual chords and jump between context sections", "[terminal][settings]") {
    MenuFixture f;
    NextSection(f.menu);
    CHECK(DrawSettings(f.menu).find("Shared directions") != std::string::npos);
    Event(f.menu, ftxui::Event::ArrowRight); // Add slot after Left.
    Event(f.menu, ftxui::Event::Return); Key(f.menu, 'J');
    Event(f.menu, ftxui::Event::ArrowLeft); // Select Left again.
    Key(f.menu, 'u'); Key(f.menu, 's');
    for (const auto context : {InputContext::Gameplay, InputContext::Menu, InputContext::Interaction,
            InputContext::Items, InputContext::Debug}) {
        CHECK_FALSE(ResolveAction(f.store.Keys(), context, {"Left"}));
        CHECK(ResolveAction(f.store.Keys(), context, {"J"}) == InputAction::West);
    }
    Event(f.menu, ftxui::Event::ArrowUpCtrl); // Wrap to Items.
    CHECK(DrawSettings(f.menu, 80, 24).find("Item inventory") != std::string::npos);
    Event(f.menu, ftxui::Event::ArrowDownCtrl); // Back to Shared directions.
    CHECK(DrawSettings(f.menu, 80, 24).find("Shared directions") != std::string::npos);
    const auto before = DrawSettings(f.menu, 80, 24);
    NextSection(f.menu); Event(f.menu, ftxui::Event::TabReverse);
    CHECK(DrawSettings(f.menu, 80, 24) == before);
    Event(f.menu, ftxui::Event::Backspace); Key(f.menu, 's');
    CHECK_FALSE(ResolveAction(f.store.Keys(), InputContext::Gameplay, {"J"}));
    CHECK(ResolveAction(f.store.Keys(), InputContext::Gameplay, {"Left"}) == InputAction::West);
}

TEST_CASE("Settings edit one inventory shortcut across gameplay interaction and debug", "[terminal][settings][gameplay]") {
    MenuFixture f;
    NextSection(f.menu);
    NextCategory(f.menu);
    CHECK(DrawSettings(f.menu).find("Shared shortcuts") != std::string::npos);
    for (int i = 0; i < 4; ++i) { Event(f.menu, ftxui::Event::ArrowDown); }
    REQUIRE(FindText(RenderSettings(f.menu), "inventory", 7, 0, true));
    Event(f.menu, ftxui::Event::Return); Key(f.menu, 'V'); Key(f.menu, 's');
    REQUIRE(f.store.KeyOverrides(SettingsScope::Global).size() == 1);
    CHECK(f.store.KeyOverrides(SettingsScope::Global).front().context == InputContext::Shared);
    for (auto context : {InputContext::Gameplay, InputContext::Interaction, InputContext::Debug}) {
        CHECK(ResolveAction(f.store.Keys(), context, {"V", false}) == InputAction::Inventory);
        CHECK_FALSE(ResolveAction(f.store.Keys(), context, {"i", false}));
    }
    for (auto context : {InputContext::Gameplay, InputContext::Interaction}) {
        CHECK(ResolveAction(f.store.Keys(), context, {"P", false}) == InputAction::Players);
        CHECK(ResolveAction(f.store.Keys(), context, {"F1", false}) == InputAction::Help);
        CHECK(ResolveAction(f.store.Keys(), context, {"/", false}) == InputAction::AdvancedInventory);
    }
    CHECK(ResolveAction(f.store.Keys(), InputContext::Items, {"I"}) == InputAction::Inventory);
    CHECK(ResolveAction(f.store.Keys(), InputContext::Menu, {"i"}) == InputAction::Inspect);
    CHECK(f.server.GetTick() == 0);
}

TEST_CASE("Settings booleans toggle numeric drafts replace text and cross-field validation waits for Save", "[terminal][settings]") {
    MenuFixture f;
    NextCategory(f.menu, 2); // Timing.
    Event(f.menu, ftxui::Event::ArrowDown); Event(f.menu, ftxui::Event::Return);
    CHECK(DrawSettings(f.menu).find("On") != std::string::npos);
    CHECK_FALSE(f.store.Effective().pacing.rateLimited);
    Key(f.menu, 's'); CHECK(f.store.Effective().pacing.rateLimited);
    Event(f.menu, ftxui::Event::ArrowDown); Event(f.menu, ftxui::Event::Return); Key(f.menu, 's');
    CHECK_FALSE(f.store.Effective().pacing.pauseWhenIdle);
    NextCategory(f.menu, 2); // Interface.
    Event(f.menu, ftxui::Event::ArrowDown); Number(f.menu, "0"); Key(f.menu, 's');
    CHECK(f.store.Effective().viewRadius == 0);
    Event(f.menu, ftxui::Event::Return); Key(f.menu, '1'); Event(f.menu, ftxui::Event::Escape);
    CHECK(f.store.Effective().viewRadius == 0);
    NextCategory(f.menu); // World.
    // Use a small explicit resident bubble so active radius 3 is invalid.
    Event(f.menu, ftxui::Event::ArrowDown); Event(f.menu, ftxui::Event::ArrowDown);
    Number(f.menu, "2"); Key(f.menu, 's');
    CHECK(f.store.Effective().residentRadius == 2);
    Event(f.menu, ftxui::Event::ArrowUp);
    Number(f.menu, "3");
    CHECK(DrawSettings(f.menu).find("Invalid value") == std::string::npos);
    Key(f.menu, 's');
    CHECK(f.store.Effective().activeRadius == 1);
    CHECK(DrawSettings(f.menu).find("Could not apply") != std::string::npos);
    Event(f.menu, ftxui::Event::ArrowDown); Number(f.menu, "4"); Key(f.menu, 's');
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
    CHECK(screen.PixelAt(cell->x, cell->y).foreground_color == ftxui::Color(ftxui::Color::GreenLight));
    cell = FindText(screen, "Source: Global override"); REQUIRE(cell);
    CHECK(screen.PixelAt(cell->x, cell->y).foreground_color == ftxui::Color(ftxui::Color::CyanLight));
    CHECK(FindText(screen, "Scope & reset"));
    CHECK(FindText(screen, "Effect"));
    NextScope(f.menu); Event(f.menu, ftxui::Event::ArrowDown);
    screen = RenderSettings(f.menu);
    cell = FindText(screen, "Source: Inherited from Global"); REQUIRE(cell);
    CHECK(screen.PixelAt(cell->x, cell->y).foreground_color == ftxui::Color(ftxui::Color::CyanLight));
    CHECK(DrawSettings(f.menu).find("Reset restores: 5") != std::string::npos);
    Number(f.menu, "7"); Key(f.menu, 's');
    screen = RenderSettings(f.menu);
    cell = FindText(screen, "Source: World override"); REQUIRE(cell);
    CHECK(screen.PixelAt(cell->x, cell->y).foreground_color == ftxui::Color(ftxui::Color::MagentaLight));
    Event(f.menu, ftxui::Event::Backspace);
    CHECK(DrawSettings(f.menu).find("Applied value: 7") != std::string::npos);
    NextCategory(f.menu); // World seed first.
    screen = RenderSettings(f.menu);
    cell = FindText(screen, "World seed", screen.dimx(), 0, true); REQUIRE(cell);
    CHECK_FALSE(screen.PixelAt(cell->x, cell->y).dim);
    CHECK(screen.PixelAt(cell->x, cell->y).foreground_color == ftxui::Color(ftxui::Color::Default));
    CHECK(Plain(screen).find("Read-only") != std::string::npos);
    Event(f.menu, ftxui::Event::Return); Key(f.menu, '9'); Event(f.menu, ftxui::Event::Backspace);
    CHECK_FALSE(f.store.Overrides(SettingsScope::World).seed);
    CHECK(f.server.GetWorld().GetSeed() == 197);
}

TEST_CASE("Settings dividers and footer remain visible when the terminal resizes", "[terminal][settings]") {
    MenuFixture f;
    Event(f.menu, ftxui::Event::ArrowDown);
    for (const int width : {80, 99, 100, 120, 99, 120, 80}) {
        const auto screen = RenderSettings(f.menu, width, width >= 100 ? 32 : 24);
        INFO(Plain(screen));
        REQUIRE(FindText(screen, "View radius", screen.dimx(), 0, true));
        for (const auto label : {"[Global]", "[Preferences]", "Interface", "s Save", "Escape Close", "Backspace Reset"}) {
            CHECK(Plain(screen).find(label) != std::string::npos);
        }
        CHECK(Plain(screen).find("a Apply") == std::string::npos);
        CHECK(Plain(screen).find("R Reload") == std::string::npos);
    }
    CHECK(f.server.GetTick() == 0);
}

TEST_CASE("Settings long translated descriptions scroll independently of rows and retain their viewport per tab", "[terminal][settings][localization]") {
    MenuFixture f;
    const auto english = LoadLanguageCatalog(CATACLYSMX_LOC_DIRECTORY "/en.json5"); REQUIRE(english);
    String longDescription;
    for (int i = 0; i < 80; ++i) { longDescription += "Long translated description with Unicode Привет. "; }
    longDescription += "DESCRIPTION_END";
    LanguageCatalog translated{{"settings.ui.description.language", longDescription, {}}};
    REQUIRE(f.localization.SetCatalogs(*english, translated));
    auto screen = RenderSettings(f.menu, 80, 24);
    CHECK(Plain(screen).find("DESCRIPTION_END") == std::string::npos);
    // Render as the viewport moves, stopping when the end of the description is
    // visible rather than assuming it is also the end of the complete details.
    for (int i = 0; i < 500 && Plain(screen).find("DESCRIPTION_END") == std::string::npos; ++i) {
        Event(f.menu, ftxui::Event::PageDown);
        screen = RenderSettings(f.menu, 80, 24);
    }
    screen = RenderSettings(f.menu, 80, 24);
    INFO(Plain(screen));
    CHECK(Plain(screen).find("DESCRIPTION_END") != std::string::npos);
    CHECK(Plain(screen).find("View radius") != std::string::npos);
    CHECK(Plain(screen).find("Close") != std::string::npos);
    NextSection(f.menu); Event(f.menu, ftxui::Event::TabReverse);
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
    for (unsigned i = 0; i <= static_cast<unsigned>(InputAction::NextSettingsSection); ++i) {
        String key = "settings.ui.action_help."; const auto name = ActionName(static_cast<InputAction>(i)); key.append(name.data(), name.size());
        INFO(key.c_str());
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

TEST_CASE("Settings honor rebound actions and expose the scope button when g is claimed", "[terminal][settings]") {
    MenuFixture f;
    const Keymap keys{
        {InputContext::Menu, InputAction::Next, {{"n"}}},
        {InputContext::Menu, InputAction::Accept, {{"e"}}},
        {InputContext::Menu, InputAction::North, {{"g"}}},
        {InputContext::Menu, InputAction::SaveSettings, {{"S"}}}
    };
    REQUIRE(f.store.SetOverrides(SettingsScope::Global, {}, keys)); f.Open();
    auto text = DrawSettings(f.menu, 80, 24);
    CHECK(text.find("e Edit") != std::string::npos);
    CHECK(text.find("S Save") != std::string::npos);
    Key(f.menu, 'n'); CHECK(DrawSettings(f.menu).find("[Keybindings]") != std::string::npos);
    Event(f.menu, ftxui::Event::TabReverse);
    Key(f.menu, 'g'); Key(f.menu, 'e'); // Up to the scope button, then activate it.
    CHECK(DrawSettings(f.menu).find("[World]") != std::string::npos);
    Event(f.menu, ftxui::Event::ArrowDown); Key(f.menu, 'e');
    CHECK(DrawSettings(f.menu, 80, 24).find("Enter Accept") != std::string::npos);
    Key(f.menu, '6'); Event(f.menu, ftxui::Event::Return); Key(f.menu, 'S');
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
        CHECK(Plain(screen).find("Save") != std::string::npos);
        CHECK(Plain(screen).find("Close") != std::string::npos);
        CHECK(screen.PixelAt(width - 1, view->y).character == "│");
        if (width >= 100) {
            const auto divider = 1 + (width - 2) * 2 / 3;
            CHECK(screen.PixelAt(divider, view->y).character == "│");
            CHECK(view->x < divider);
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
        CHECK(Plain(screen).find("6 _") != std::string::npos);
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
    Key(f.menu, 's'); CHECK(f.store.Effective().viewRadius == 6);

    NextSection(f.menu); Event(f.menu, ftxui::Event::Return); // Binding capture.
    for (const int width : widths) {
        const auto screen = RenderSettings(f.menu, width, width < 100 ? 24 : 32);
        CHECK(Plain(screen).find("Press a key to bind") != std::string::npos);
        CHECK(Plain(screen).find("Escape Cancel") != std::string::npos);
        CHECK(Plain(screen).find("[Keybindings]") != std::string::npos);
    }
    Key(f.menu, 'J'); // Accept a draft binding after the resizes.
    CHECK(ResolveAction(f.store.Keys(), InputContext::Gameplay, {"Left", false}) == InputAction::West);
    Key(f.menu, 'i'); Key(f.menu, 'S'); // Inspect without replacing the draft.
    for (const int width : widths) {
        const auto screen = RenderSettings(f.menu, width, width < 100 ? 24 : 32);
        INFO(Plain(screen));
        CHECK(Plain(screen).find("53 => S") != std::string::npos);
        CHECK(Plain(screen).find("Escape Cancel") != std::string::npos);
    }
    Event(f.menu, ftxui::Event::Escape);
    Key(f.menu, 's');
    CHECK(ResolveAction(f.store.Keys(), InputContext::Gameplay, {"J", false}) == InputAction::West);

    NextSection(f.menu); Event(f.menu, ftxui::Event::ArrowUp);
    Event(f.menu, ftxui::Event::Return); // Language picker.
    for (const int width : widths) {
        const auto screen = RenderSettings(f.menu, width, width < 100 ? 24 : 32);
        REQUIRE(FindText(screen, "en", screen.dimx(), 0, true));
        CHECK(Plain(screen).find("Choose a language") != std::string::npos);
        CHECK(Plain(screen).find("Escape Cancel") != std::string::npos);
    }
    Event(f.menu, ftxui::Event::Escape);
    CHECK_FALSE(f.store.Overrides(SettingsScope::Global).language);
    CHECK(f.server.GetTick() == 0);
}

}
