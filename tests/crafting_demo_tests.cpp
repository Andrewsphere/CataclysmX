#include "content/content.h"

#include <catch2/catch_test_macros.hpp>

namespace {
using namespace cataclysmx;

TEST_CASE("Bundled stone marker crafting and recycling definitions load", "[content][crafting-demo]") {
    String path = __FILE__;
    const auto separator = path.find_last_of("/\\");
    REQUIRE(separator != String::npos);
    path.resize(separator);
    path += "/../data/terrain.json5";
    const auto catalog = LoadContentFile(path.c_str());
    REQUIRE(catalog.has_value());
    const auto rock = catalog->FindItem("rock");
    const auto marker = catalog->FindItem("stone_marker");
    const auto recipeId = catalog->FindCraftingRecipe("shape_stone_marker");
    const auto recyclerId = catalog->FindRecyclingRecipe("recycle_stone_marker");
    REQUIRE(rock != ItemTypeId{});
    REQUIRE(marker != ItemTypeId{});
    REQUIRE(recipeId != CraftingRecipeId{});
    REQUIRE(recyclerId != RecyclingRecipeId{});
    const auto& item = catalog->GetItem(marker);
    CHECK(item.massGrams == 500);
    CHECK(item.volumeMl == 250);
    const auto& recipe = catalog->GetCraftingRecipe(recipeId);
    CHECK(recipe.enabled);
    CHECK(recipe.work == 300);
    CHECK(recipe.toolTraits.Empty());
    REQUIRE(recipe.inputs.Size() == 1);
    CHECK(recipe.inputs[0].Item() == rock);
    CHECK(recipe.inputs[0].Count() == 1);
    CHECK_FALSE(recipe.inputs[0].Retained());
    CHECK(recipe.result == ItemOutput{marker, 1});
    CHECK(catalog->FindRecyclingFor(marker) == recyclerId);
    const auto& recycler = catalog->GetRecyclingRecipe(recyclerId);
    CHECK(recycler.work == 100);
    CHECK(recycler.inputs.Empty());
    CHECK(recycler.toolTraits.Empty());
    REQUIRE(recycler.returns.Size() == 1);
    CHECK(recycler.returns[0] == ItemOutput{rock, 1});
}
}
