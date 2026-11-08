#pragma once

#include "core/containers.h"
#include "core/diagnostics.h"

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

struct BuildingPattern {
    String id;
    int width = 0;
    int height = 0;
    Vector<TerrainId> tiles; // Row-major: z * width + x; all terrain references are resolved.
    PatternPoint entrance;
    std::uint32_t weight = 1;
};

enum class ContentError { Io, InvalidData, UnsupportedVersion };

template<class T>
using ContentResult = eastl::expected<T, ContentError>;

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

private:
    Vector<TerrainDefinition> definitions_;
    Vector<BuildingPattern> patterns_;
    friend ContentResult<ContentCatalog> LoadContent(eastl::span<const ContentSource>, Diagnostics*);
};

ContentResult<ContentCatalog> LoadContent(eastl::span<const ContentSource> sources,
                                        Diagnostics* diagnostics = nullptr);
ContentResult<ContentCatalog> LoadContentFiles(eastl::span<const eastl::string_view> paths,
                                             Diagnostics* diagnostics = nullptr);
ContentResult<ContentCatalog> LoadContentFile(const char* path, Diagnostics* diagnostics = nullptr);

}
