#include "content/content.h"
#include <catch2/catch_test_macros.hpp>

namespace {
using namespace cataclysmx;

constexpr eastl::string_view digitalContent = R"({
 format_version:1,
 definitions:[
  {type:'skill',id:'computers',name:'Computers'},
  {type:'book',id:'manual',name:'Manual',study_work:100},
  {type:'item',id:'filament',name:'Filament',mass_g:10,volume_ml:15},
  {type:'item',id:'cup',name:'Cup',mass_g:40,volume_ml:400},
  {type:'item',id:'printer',name:'Printer',mass_g:8000,volume_ml:65000,
   digital_device:'controller',supplied_information:['supplied'],placed_object:'station'},
  {type:'information',id:'supplied',name:'Supplied',books:['manual'],designs:['print_cup'],software:true},
  {type:'machine_recipe',id:'print_cup',name:'Print cup',inputs:[{item:'filament',count:5}],
   result:{item:'cup',count:1},duration:1200,energy_per_tick:1,
   required_software:'supplied',learning:{skill:'computers',level:0,xp_per_execution:10}}
 ],
 objects:[{id:'station',name:'Printer',behavior:'selectable_machine',
  packing_item:'printer',capacity_volume_ml:12000,selectable_recipes:['print_cup']}]
})";

ContentResult<ContentCatalog> Digital(eastl::string_view text = digitalContent) {
    const ContentSource source{"digital-test", text};
    return LoadContent({&source, 1});
}
String Replaced(eastl::string_view from, eastl::string_view to) {
    String text(digitalContent.data(), digitalContent.size());
    const auto at = text.find(from.data(), 0, from.size());
    REQUIRE(at != String::npos);
    text.replace(at, from.size(), to.data(), to.size());
    return text;
}

TEST_CASE("Digital content resolves package recipe and controller identities", "[content][digital]") {
    auto loaded = Digital();
    REQUIRE(loaded);
    REQUIRE(loaded->InformationCount() == 1);
    REQUIRE(loaded->MachineRecipeCount() == 1);
    const auto recipeId = loaded->FindMachineRecipe("print_cup");
    const auto packageId = loaded->FindInformation("supplied");
    const auto& recipe = loaded->GetMachineRecipe(recipeId);
    CHECK(recipe.inputs.Size() == 1);
    CHECK(recipe.inputs[0].Item() == loaded->FindItem("filament"));
    CHECK(recipe.inputs[0].Count() == 5);
    CHECK(recipe.result.item == loaded->FindItem("cup"));
    CHECK(recipe.duration == 1200);
    CHECK(recipe.setupWork == 200);
    CHECK(recipe.setupWorkPerUnit == 20);
    CHECK(recipe.requiredSoftware == packageId);
    CHECK(recipe.learning.skill == loaded->FindSkill("computers"));
    CHECK(recipe.learning.level == 0);
    CHECK(recipe.learning.xpPerExecution == 10);
    const auto& package = loaded->GetInformation(packageId);
    CHECK(package.software);
    CHECK(package.books[0] == loaded->FindBook("manual"));
    CHECK(package.designs[0] == recipeId);
    const auto& item = loaded->GetItem(loaded->FindItem("printer"));
    CHECK(item.digitalDevice == DigitalDeviceKind::Controller);
    CHECK(item.suppliedInformation[0] == packageId);
    const auto& object = loaded->GetObject(item.placedObject);
    CHECK(object.behavior == ObjectBehaviorKind::SelectableMachine);
    CHECK(object.packingItem == loaded->FindItem("printer"));
    CHECK(object.selectableRecipes[0] == recipeId);
    const auto exported = WriteCombatContent(*loaded);
    REQUIRE(exported.Find("information"));
    CHECK(exported.Find("information")->Size() == 1);
    REQUIRE(exported.Find("machine_recipes"));
    const auto& exportedRecipe = exported.Find("machine_recipes")->At(0);
    CHECK(*exportedRecipe.GetUnsigned("setup_work") == 200);
    CHECK(*exportedRecipe.GetString("required_software") == "supplied");
}

TEST_CASE("Digital schemas reject broken references bounds and device contracts", "[content][digital]") {
    struct Change { eastl::string_view from, to; };
    const Change changes[] = {
        {"books:['manual']", "books:['missing']"},
        {"designs:['print_cup']", "designs:['missing']"},
        {"software:true", "software:false"},
        {"supplied_information:['supplied']", "supplied_information:['missing']"},
        {"supplied_information:['supplied']", "supplied_information:['supplied','supplied']"},
        {"digital_device:'controller'", "digital_device:'storage'"},
        {"digital_device:'controller'", "digital_device:'reader'"},
        {"digital_device:'controller',", ""},
        {"selectable_recipes:['print_cup']", "selectable_recipes:['missing']"},
        {"selectable_recipes:['print_cup']", "selectable_recipes:[]"},
        {"capacity_volume_ml:12000", "capacity_volume_ml:399"},
        {"packing_item:'printer',", ""},
        {"count:5", "count:256"},
        {"count:5", "count:0"},
        {"count:5", "count:5,retained:true"},
        {"duration:1200", "duration:0"},
        {"duration:1200", "duration:18446744073709551615"},
        {"energy_per_tick:1", "energy_per_tick:0"},
        {"energy_per_tick:1", "energy_per_tick:1,setup_work_per_unit:4294967295"},
        {"xp_per_execution:10", "xp_per_execution:4294967295"},
        {"level:0,xp_per_execution:10", "level:11,xp_per_execution:10"},
        {"skill:'computers',level:0", "skill:'missing',level:0"},
        {"type:'information',id:'supplied'", "type:'information',id:'cup'"},
        {"type:'machine_recipe',id:'print_cup'", "type:'machine_recipe',id:'filament'"},
        {"name:'Supplied',books", "name:'Supplied',unexpected:1,books"}
    };
    for (const auto& change : changes) {
        INFO(change.to.data());
        const auto text = Replaced(change.from, change.to);
        CHECK_FALSE(Digital(text));
    }
}

TEST_CASE("Digital readers require an actual local power store", "[content][digital]") {
    const ContentSource source{"reader", R"({format_version:1,definitions:[
      {type:'item',id:'usb',name:'USB',mass_g:1,volume_ml:1,digital_device:'storage'},
      {type:'item',id:'phone',name:'Phone',mass_g:1,volume_ml:1,digital_device:'reader',
       reading_power:'power',capabilities:[
        {id:'power',type:'energy_store',capacity_energy:100,initial_energy:0,rechargeable:true}]}
    ]})"};
    const auto loaded = LoadContent({&source, 1});
    REQUIRE(loaded);
    const auto& reader = loaded->GetItem(loaded->FindItem("phone"));
    CHECK(reader.digitalDevice == DigitalDeviceKind::Reader);
    CHECK(ItemEnergyStore(reader, reader.readingPower) != nullptr);
}
}
