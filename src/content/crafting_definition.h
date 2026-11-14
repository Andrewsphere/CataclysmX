#pragma once

#include "core/containers.h"
#include "core/static_list.h"
#include "items/item_types.h"

namespace cataclysmx {

inline constexpr std::uint32_t maxRecipeItemId = (1u << 20) - 1;
inline constexpr std::size_t maxRecipeInputs = 8;
inline constexpr std::size_t maxRecipeTraits = 4;

struct CraftingRecipeId {
    std::uint32_t value = 0;
    bool operator==(const CraftingRecipeId&) const = default;
};
struct RecyclingRecipeId {
    std::uint32_t value = 0;
    bool operator==(const RecyclingRecipeId&) const = default;
};
struct ToolTraitId {
    std::uint8_t value = 0;
    bool operator==(const ToolTraitId&) const = default;
};

enum class RecipeInputFlags : std::uint8_t { None = 0, Retained = 1, NoRecovery = 2 };

class RecipeInput {
public:
    constexpr RecipeInput() = default;
    constexpr RecipeInput(ItemTypeId item, std::uint8_t count, RecipeInputFlags flags = RecipeInputFlags::None)
        : packed_(item.value | (static_cast<std::uint32_t>(count) << 20)
            | (static_cast<std::uint32_t>(flags) << 28)) {
        if (item.value == 0 || item.value > maxRecipeItemId || count == 0
            || static_cast<std::uint8_t>(flags) > 3) { std::abort(); }
    }
    constexpr ItemTypeId Item() const { return {packed_ & maxRecipeItemId}; }
    constexpr std::uint8_t Count() const { return static_cast<std::uint8_t>((packed_ >> 20) & 255); }
    constexpr bool Retained() const { return (packed_ & (1u << 28)) != 0; }
    constexpr bool NoRecovery() const { return (packed_ & (2u << 28)) != 0; }
    constexpr std::uint32_t Packed() const { return packed_; }
    bool operator==(const RecipeInput&) const = default;
private:
    std::uint32_t packed_ = 0;
};
static_assert(sizeof(RecipeInput) == 4);

struct TraitRequirement {
    ToolTraitId trait{};
    std::uint8_t tier = 0;
    bool operator==(const TraitRequirement&) const = default;
};
static_assert(sizeof(TraitRequirement) == 2);

struct ItemOutput {
    ItemTypeId item{};
    std::uint32_t count = 0;
    bool operator==(const ItemOutput&) const = default;
};

struct CraftingRecipeDefinition {
    String id;
    String name;
    StaticList<RecipeInput, maxRecipeInputs> inputs;
    StaticList<TraitRequirement, maxRecipeTraits> toolTraits;
    ItemOutput result;
    std::uint32_t work = 0;
    bool enabled = true;
};

struct RecyclingRecipeDefinition {
    String id;
    String name;
    ItemTypeId target{};
    StaticList<RecipeInput, maxRecipeInputs> inputs;
    StaticList<TraitRequirement, maxRecipeTraits> toolTraits;
    StaticList<ItemOutput, maxRecipeInputs> returns;
    std::uint32_t work = 0;
};

struct ToolTraitDefinition {
    String id;
    String name;
};

}
