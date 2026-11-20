#include "client/crafting_menu.h"
#include "client/digital_commands.h"
#include <catch2/catch_test_macros.hpp>

namespace {
using namespace cataclysmx;

TEST_CASE("Crafting choices retain machine quantity action and transfer endpoint identity", "[digital][ui]") {
    Command left; left.kind = CommandKind::begin_machine; left.target = 1; left.machineRecipe = {2}; left.quantity = 3;
    Command right = left; CHECK(SameCraftingChoice(left, right));
    ++right.quantity; CHECK_FALSE(SameCraftingChoice(left, right));
    right = left; ++right.target; CHECK_FALSE(SameCraftingChoice(left, right));
    left = {}; left.kind = CommandKind::copy_information;
    left.informationSource.item = 1; left.informationDestination.item = 2; left.information = {3};
    right = left; right.informationHost = 4; CHECK_FALSE(SameCraftingChoice(left, right));
    right = left; right.informationDestination = {0, 2}; CHECK_FALSE(SameCraftingChoice(left, right));
}

TEST_CASE("Shared crafting controls use owned machine snapshots", "[digital][ui]") {
    const ContentSource source{"digital-ui", R"({format_version:1,definitions:[
        {type:'item',id:'scrap',name:'Scrap',mass_g:0,volume_ml:1}
    ]})"};
    auto content = LoadContent({&source, 1}); REQUIRE(content);
    WorldView view; MachineView machine; machine.object = 17; machine.reservedVolumeMl = 4000;
    view.machines.push_back(machine);
    Vector<CraftingMenuRow> rows; CollectCraftingMenu(view, *content, Localization{}, rows);
    REQUIRE(rows.size() == 4);
    for (const auto& row : rows) {
        CHECK(row.command.kind == CommandKind::machine_action);
        CHECK(row.command.target == 17); CHECK(ValidCommandShape(row.command));
        CHECK(row.label.find("reserved 4000 ml") != String::npos);
    }
    CHECK(rows[0].command.machineAction == MachineAction::Stop);
    CHECK(rows[3].command.machineAction == MachineAction::Collect);
}

TEST_CASE("Local and LAN digital script grammar captures exact endpoints and quantity", "[digital][ui]") {
    eastl::string_view begin[] = {"begin-machine", "17", "2", "10"};
    Command command; REQUIRE(ParseDigitalCommand(begin, command)); CHECK(command.quantity == 10);
    eastl::string_view copy[] = {"import-information", "item:2", "object:17", "3", "8"};
    REQUIRE(ParseDigitalCommand(copy, command)); CHECK(command.informationHost == 8);
    CHECK(command.informationDestination.object == 17);
    eastl::string_view invalid[] = {"begin-machine", "17", "2", "65536"};
    CHECK_FALSE(ParseDigitalCommand(invalid, command));
    CHECK(command.kind == CommandKind::copy_information); // Failure preserves captured intent.
}
}
