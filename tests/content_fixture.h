#pragma once

#include "content/content.h"

#include <catch2/catch_test_macros.hpp>
#include <EASTL/utility.h>

// Immutable test fixture; production loads content explicitly at startup.
inline const cataclysmx::ContentCatalog& TestContent() {
    static const cataclysmx::ContentCatalog content = [] {
        cataclysmx::Diagnostics diagnostics;
        auto result = cataclysmx::LoadContentFile(CATACLYSMX_TERRAIN_FILE, &diagnostics);
        INFO(cataclysmx::FormatDiagnostics(diagnostics).c_str());
        REQUIRE(result.has_value());
        REQUIRE(result->GetTerrain(result->FindTerrain("air")).openSpace);
        REQUIRE(result->GetTerrain(result->FindTerrain("stairs")).stairs);
        return eastl::move(*result);
    }();
    return content;
}

inline cataclysmx::TerrainId TestTerrain(eastl::string_view id) {
    const auto result = TestContent().FindTerrain(id);
    REQUIRE(result.value != 0);
    return result;
}

inline cataclysmx::ItemTypeId TestItem(eastl::string_view id) {
    const auto result = TestContent().FindItem(id);
    REQUIRE(result.value != 0);
    return result;
}
