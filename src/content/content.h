#pragma once

#include "core/containers.h"
#include "core/diagnostics.h"
#include "core/light_types.h"
#include "content/ai_definition.h"
#include "content/item_definition.h"
#include "content/crafting_definition.h"
#include "content/object_definition.h"
#include "content/building_population.h"
#include "content/content_result.h"
#include "content/content_assets.h"

#include <EASTL/expected.h>
#include <EASTL/span.h>
#include <EASTL/string_view.h>

#include <cstddef>
#include <cstdint>

namespace cataclysmx {

struct TerrainId {
    std::uint32_t value = 0;
    bool operator==(const TerrainId&) const = default;
};

struct RgbColor {
    std::uint8_t red = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;
    bool operator==(const RgbColor&) const = default;
};

struct TerminalAppearance {
    String symbol;
    RgbColor foreground;
    RgbColor background;
    bool operator==(const TerminalAppearance&) const = default;
};

struct TerrainDefinition {
    String id;
    String name;
    TerminalAppearance appearance;
    bool passable = false;
    bool blocksSight = false;
    TerrainId opensTo{};
    TerrainId closesTo{};
    TerrainId locksTo{};
    TerrainId unlocksTo{};
    bool openSpace = false; // No floor support; transmits downward. All other terrain supports a floor.
    bool stairs = false; // Matched stairs at adjacent Y connect travel and vertical sight.
    LightEmission light{};
};

struct BuildingPatternId {
    std::uint32_t value = 0;
    bool operator==(const BuildingPatternId&) const = default;
};

struct PatternPoint {
    int x = 0;
    int z = 0;
    bool operator==(const PatternPoint&) const = default;
};

struct PatternLayer {
    int y = 0;
    Vector<TerrainId> tiles; // Row-major; same width/height as the surface.
    bool operator==(const PatternLayer&) const = default;
};

struct BasementVariant {
    String id; // Unique within the building; part of generation seed derivation.
    std::uint32_t weight = 1;
    PatternLayer layer{-1, {}};
    PatternPopulation population;
    bool operator==(const BasementVariant&) const = default;
};

enum class BuildingSite : std::uint8_t { Building, Landmark, Park, Square };
// Soft neighborhood preference for ordinary building sites, never eligibility.
enum class BuildingUse : std::uint8_t { Mixed, Residential, Commercial, Industrial };

struct BuildingPattern {
    String id;
    int width = 0;
    int height = 0;
    Vector<TerrainId> tiles; // Row-major: z * width + x; all terrain references are resolved.
    PatternPoint entrance;
    std::uint32_t weight = 1;
    BuildingSite site = BuildingSite::Building; // Placement class for dense town lots.
    Vector<PatternLayer> layers; // Unique nonzero Y; no -1 if basements supply it.
    PatternPopulation population;
    Vector<BasementVariant> basements;
    // Generation 6 selection metadata; versions 1..5 ignore both fields.
    // Empty family is a singleton keyed by id, with effective family weight=weight.
    String family;
    // Named families share one site and this resolved weight (JSON default 1).
    // Existing weight selects fitting variants within a named family.
    std::uint32_t familyWeight = 1;
    // JSON default Mixed is neutral. Named family members must agree on use.
    // Generation 6 biases family weights only; versions 1..5 ignore this field.
    BuildingUse use = BuildingUse::Mixed;
    bool operator==(const BuildingPattern&) const = default;
};

class ItemBehaviorRegistry;

struct ContentSource {
    eastl::string_view name;
    eastl::string_view text;
};

class ContentCatalog {
public:
    TerrainId FindTerrain(eastl::string_view id) const;
    const TerrainDefinition* TryGetTerrain(TerrainId id) const;
    const TerrainDefinition& GetTerrain(TerrainId id) const;
    std::size_t TerrainCount() const;
    BuildingPatternId FindBuildingPattern(eastl::string_view id) const;
    const BuildingPattern* TryGetBuildingPattern(BuildingPatternId id) const;
    const BuildingPattern& GetBuildingPattern(BuildingPatternId id) const;
    std::size_t BuildingPatternCount() const;
    ItemTypeId FindItem(eastl::string_view id) const;
    const ItemDefinition* TryGetItem(ItemTypeId id) const;
    const ItemDefinition& GetItem(ItemTypeId id) const;
    std::size_t ItemCount() const;
    FlagId FindFlag(eastl::string_view id) const;
    const FlagDefinition* TryGetFlag(FlagId id) const;
    const FlagDefinition& GetFlag(FlagId id) const;
    std::size_t FlagCount() const;
    ObjectTypeId FindObject(eastl::string_view id) const;
    const ObjectDefinition* TryGetObject(ObjectTypeId id) const;
    const ObjectDefinition& GetObject(ObjectTypeId id) const;
    std::size_t ObjectCount() const;
    LootPoolId FindLootPool(eastl::string_view id) const;
    const LootPoolDefinition* TryGetLootPool(LootPoolId id) const;
    const LootPoolDefinition& GetLootPool(LootPoolId id) const;
    std::size_t LootPoolCount() const;
    MonsterTypeId FindMonster(eastl::string_view id) const;
    const MonsterDefinition* TryGetMonster(MonsterTypeId id) const;
    const MonsterDefinition& GetMonster(MonsterTypeId id) const;
    std::size_t MonsterCount() const;
    MonsterGroupId FindMonsterGroup(eastl::string_view id) const;
    const MonsterGroupDefinition* TryGetMonsterGroup(MonsterGroupId id) const;
    const MonsterGroupDefinition& GetMonsterGroup(MonsterGroupId id) const;
    std::size_t MonsterGroupCount() const;
    AiProfileId FindAiProfile(eastl::string_view id) const;
    const AiProfile* TryGetAiProfile(AiProfileId id) const;
    const AiProfile& GetAiProfile(AiProfileId id) const;
    std::size_t AiProfileCount() const;
    CraftingRecipeId FindCraftingRecipe(eastl::string_view id) const;
    const CraftingRecipeDefinition* TryGetCraftingRecipe(CraftingRecipeId id) const;
    const CraftingRecipeDefinition& GetCraftingRecipe(CraftingRecipeId id) const;
    std::size_t CraftingRecipeCount() const;
    RecyclingRecipeId FindRecyclingRecipe(eastl::string_view id) const;
    const RecyclingRecipeDefinition* TryGetRecyclingRecipe(RecyclingRecipeId id) const;
    const RecyclingRecipeDefinition& GetRecyclingRecipe(RecyclingRecipeId id) const;
    std::size_t RecyclingRecipeCount() const;
    RecyclingRecipeId FindRecyclingFor(ItemTypeId item) const;
    ToolTraitId FindToolTrait(eastl::string_view id) const;
    const ToolTraitDefinition* TryGetToolTrait(ToolTraitId id) const;
    const ToolTraitDefinition& GetToolTrait(ToolTraitId id) const;
    std::size_t ToolTraitCount() const;
    ItemTypeId UnfinishedCraftItem() const;

private:
    Vector<TerrainDefinition> definitions_;
    Vector<BuildingPattern> patterns_;
    Vector<ItemDefinition> items_;
    Vector<FlagDefinition> flags_;
    Vector<ObjectDefinition> objects_;
    Vector<LootPoolDefinition> lootPools_;
    Vector<MonsterDefinition> monsters_;
    Vector<MonsterGroupDefinition> monsterGroups_;
    Vector<AiProfile> aiProfiles_;
    Vector<CraftingRecipeDefinition> craftingRecipes_;
    Vector<RecyclingRecipeDefinition> recyclingRecipes_;
    Vector<ToolTraitDefinition> toolTraits_;
    ItemTypeId unfinishedCraftItem_{};
    friend ContentResult<ContentCatalog> LoadContent(eastl::span<const ContentSource>, Diagnostics*);
    friend ContentResult<ContentCatalog> LoadContent(eastl::span<const ContentSource>, const ItemBehaviorRegistry&, Diagnostics*);
    friend ContentResult<ContentCatalog> LoadContent(eastl::span<const ContentSource>, const ItemBehaviorRegistry&,
        const ContentAssetReader&, Diagnostics*);
};

ContentResult<ContentCatalog> LoadContent(eastl::span<const ContentSource> sources,
                                        Diagnostics* diagnostics = nullptr);
ContentResult<ContentCatalog> LoadContentFiles(eastl::span<const eastl::string_view> paths,
                                             Diagnostics* diagnostics = nullptr);
ContentResult<ContentCatalog> LoadContentFile(const char* path, Diagnostics* diagnostics = nullptr);
ContentResult<ContentCatalog> LoadContent(eastl::span<const ContentSource> sources,
    const ItemBehaviorRegistry& registry, Diagnostics* diagnostics = nullptr);
ContentResult<ContentCatalog> LoadContentFiles(eastl::span<const eastl::string_view> paths,
    const ItemBehaviorRegistry& registry, Diagnostics* diagnostics = nullptr);
ContentResult<ContentCatalog> LoadContentFile(const char* path,
    const ItemBehaviorRegistry& registry, Diagnostics* diagnostics = nullptr);

// Memory sources use an explicit synchronous asset reader. Existing overloads
// still support inline art; an external reference without a reader is an error.
ContentResult<ContentCatalog> LoadContent(eastl::span<const ContentSource> sources,
    const ContentAssetReader& assets, Diagnostics* diagnostics = nullptr);
ContentResult<ContentCatalog> LoadContent(eastl::span<const ContentSource> sources,
    const ItemBehaviorRegistry& registry, const ContentAssetReader& assets,
    Diagnostics* diagnostics = nullptr);
// File-backed assets are resolved within dataRoot, not relative to definitions.
ContentResult<ContentCatalog> LoadContentFiles(eastl::span<const eastl::string_view> paths,
    eastl::string_view dataRoot, Diagnostics* diagnostics = nullptr);
ContentResult<ContentCatalog> LoadContentFiles(eastl::span<const eastl::string_view> paths,
    const ItemBehaviorRegistry& registry, eastl::string_view dataRoot,
    Diagnostics* diagnostics = nullptr);

// Explicit manifest loader. Definition-file loaders never discover siblings or
// infer manifests. Paths are relative to the manifest; sources resolve together.
ContentResult<ContentCatalog> LoadContentPack(const char* manifestPath,
    Diagnostics* diagnostics = nullptr);
ContentResult<ContentCatalog> LoadContentPack(const char* manifestPath,
    const ItemBehaviorRegistry& registry, Diagnostics* diagnostics = nullptr);

}
