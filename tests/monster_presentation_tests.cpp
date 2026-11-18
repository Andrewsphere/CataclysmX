#include "client/tile_inspection.h"
#include "client/text.h"
#include "client/combat_text.h"
#include "client/localization.h"
#include "combat/combat_system.h"

#include <catch2/catch_test_macros.hpp>

#if defined(CATACLYSMX_MONSTER_PRESENTATION_TERMINAL)
#include "terminal/gameplay_screen.h"
#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>
#include <string>
#endif

namespace {
using namespace cataclysmx;

ContentCatalog PresentationContent() {
    Diagnostics diagnostics;
    auto loaded = LoadContentPack(CATACLYSMX_CONTENT_PACK, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str());
    REQUIRE(loaded);
    REQUIRE(loaded->FindMonster("cellar_zombie").value != 0);
    return eastl::move(*loaded);
}

ViewCell VisibleMonster(const ContentCatalog& content) {
    ViewCell cell;
    cell.actor = 42;
    cell.monsterType = content.FindMonster("cellar_zombie");
    cell.overlayVisibility = Visibility::Visible;
    return cell;
}

bool Contains(const Vector<String>& lines, const String& text) {
    for (const auto& line : lines) { if (line.find(text) != String::npos) { return true; } }
    return false;
}

TEST_CASE("Visible actor names borrow catalog identity and use safe fallbacks", "[client][monster-presentation]") {
    const auto content = PresentationContent();
    auto cell = VisibleMonster(content);
    const auto& definition = content.GetMonster(cell.monsterType);
    const auto name = VisibleActorName(content, cell);
    CHECK(name == eastl::string_view(definition.name.data(), definition.name.size()));
    CHECK(name.data() == definition.name.data());

    for (const auto visibility : {Visibility::Unknown, Visibility::Remembered}) {
        cell.overlayVisibility = visibility;
        CHECK(VisibleActorName(content, cell).empty());
    }
    cell.overlayVisibility = Visibility::Visible;
    cell.actor = 0;
    CHECK(VisibleActorName(content, cell).empty());
    cell.actor = 42;
    cell.monsterType = {};
    CHECK(VisibleActorName(content, cell) == "Creature");
    cell.monsterType = {0xffffffffU};
    CHECK(VisibleActorName(content, cell) == "Creature");
    cell.player = true;
    CHECK(VisibleActorName(content, cell) == "Player");
    cell.monsterType = {};
    CHECK(VisibleActorName(content, cell) == "Player");

    cell = VisibleMonster(content);
    cell.terrainVisibility = Visibility::Remembered;
    cell.terrainDepth = 2;
    cell.overlayDepth = 1;
    CHECK(VisibleActorName(content, cell) == name);
}

TEST_CASE("Local monster inspection keeps live speed combat and position gates", "[client][monster-presentation]") {
    const auto content = PresentationContent();
    Server server(content);
    auto& world = server.GetWorld();
    const TileCoord position{1, 0};
    const auto created = world.CreateMonster(content.FindMonster("cellar_zombie"), position);
    REQUIRE(created);
    REQUIRE(world.SetActorSpeed(*created, 137));
    REQUIRE(world.Combat().Damage(*created, {}, BodyPart::Core, {DamageType::Blunt, 1000}).outcome == AttackOutcome::Hit);
    const auto view = server.View();
    const auto& cell = view.cells[static_cast<std::size_t>((position.z - view.origin.z) * view.width
        + position.x - view.origin.x)];
    REQUIRE(cell.actor == world.GetActor(*created)->id);
    REQUIRE(cell.overlayVisibility == Visibility::Visible);
    Vector<String> lines;
    DescribeTile(world, view, position, lines);
    String expected = content.GetMonster(cell.monsterType).name;
    expected += " #"; expected += UnsignedText(cell.actor); expected += " | Speed 137";
    CHECK(Contains(lines, expected));
    CHECK(Contains(lines, CombatStatusText(world.Combat().Describe(*created))));

    TileInspection details;
    REQUIRE(CaptureTileInspection(world, view, position, details));
    eastl::optional<ExaminedEnemy> enemy;
    lines.clear();
    DescribeTile(content, view, position, details, lines, &enemy);
    REQUIRE(enemy);
    CHECK(enemy->name == content.GetMonster(cell.monsterType).name);
    CHECK(enemy->combat == details.combat);
    CHECK(enemy->status.find("Speed 137") != String::npos);
    CHECK(enemy->status.find("core") == String::npos);
    CHECK_FALSE(Contains(lines, expected));

    auto hidden = view;
    auto& hiddenCell = hidden.cells[static_cast<std::size_t>((position.z - view.origin.z) * view.width
        + position.x - view.origin.x)];
    for (const auto visibility : {Visibility::Unknown, Visibility::Remembered}) {
        hiddenCell.overlayVisibility = visibility;
        lines.clear();
        DescribeTile(world, hidden, position, lines);
        CHECK_FALSE(Contains(lines, expected));
        DescribeTile(content, hidden, position, details, lines, &enemy);
        CHECK_FALSE(enemy);
    }

    // The old projection must not expose live metadata after the actor moves.
    REQUIRE(world.SetTile({1, 1}, {content.FindTerrain("floor"), 0}));
    REQUIRE(world.MoveActor(*created, {1, 1}));
    lines.clear();
    DescribeTile(world, view, position, lines);
    CHECK_FALSE(Contains(lines, expected));
}

TEST_CASE("Remote text recognizes projected monsters without a world", "[client][monster-presentation]") {
    const auto content = PresentationContent();
    WorldView view;
    view.width = 1; view.height = 1;
    view.cells.push_back(VisibleMonster(content));
    const auto& name = content.GetMonster(view.cells.front().monsterType).name;
    CHECK(render_text(view, content).find(name) != String::npos);
    for (const auto visibility : {Visibility::Unknown, Visibility::Remembered}) {
        view.cells.front().overlayVisibility = visibility;
        CHECK(render_text(view, content).find(name) == String::npos);
    }
}

#if defined(CATACLYSMX_MONSTER_PRESENTATION_TERMINAL)
TEST_CASE("Remote terminal status recognizes projected monster names", "[terminal][monster-presentation]") {
    const auto content = PresentationContent();
    Localization localization;
    REQUIRE(localization.Load(CATACLYSMX_LOC_DIRECTORY, "en"));
    WorldView view;
    view.width = 1; view.height = 1;
    view.cells.push_back(VisibleMonster(content));
    const auto& name = content.GetMonster(view.cells.front().monsterType).name;
    GameplayUi ui;
    GameplayPresentation presentation;
    presentation.session = "Remote";
    const auto keys = DefaultKeymap();
    for (const int width : {80, 120}) {
        for (const int height : {24, 40}) {
            for (const auto visibility : {Visibility::Visible, Visibility::Remembered, Visibility::Unknown}) {
                INFO("width=" << width << ", height=" << height << ", visibility=" << static_cast<int>(visibility));
                view.cells.front().overlayVisibility = visibility;
                auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(width), ftxui::Dimension::Fixed(height));
                ftxui::Render(screen, ui.Render(view, content, localization, keys, presentation));
                std::string text;
                for (int y = 0; y < screen.dimy(); ++y) {
                    for (int x = 0; x < screen.dimx(); ++x) {
                        // Paragraph word gaps are blank cells, not explicit space glyphs.
                        const auto& character = screen.PixelAt(x, y).character;
                        text += character.empty() ? " " : character;
                    }
                    text += '\n';
                }
                INFO(text);
                CHECK((text.find(std::string(name.data(), name.size())) != std::string::npos)
                    == (visibility == Visibility::Visible));
            }
        }
    }
}
#endif
}
