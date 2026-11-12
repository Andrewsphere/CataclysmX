#include "content/content.h"

#include <catch2/catch_test_macros.hpp>

namespace {
using namespace cataclysmx;

ContentResult<ContentCatalog> Load(eastl::string_view profiles, Diagnostics* diagnostics = nullptr) {
    String text = "{format_version:1,ai_profiles:[";
    text.append(profiles.data(), profiles.size());
    text += "]}";
    const ContentSource source{"ai.json5", {text.data(), text.size()}};
    return LoadContent({&source, 1}, diagnostics);
}

TEST_CASE("AI profiles have sorted typed IDs and defaults", "[content][ai-content]") {
    const ContentSource sources[] = {
        {"z", "{format_version:1,ai_profiles:[{id:'z'}]}"},
        {"a", "{format_version:1,ai_profiles:[{id:'a',sight_range:32,dim_range:0,dark_range:0,"
              "scan_interval_ticks:60000,memory_ticks:60000,search_ticks:60000,"
              "candidate_budget:32,bucket_budget:64,switch_advantage:0}]}"}
    };
    const ContentSource reversed[] = {sources[1], sources[0]};
    const auto first = LoadContent({sources, 2});
    const auto second = LoadContent({reversed, 2});
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    CHECK(first->AiProfileCount() == 2);
    CHECK(first->FindAiProfile("a") == AiProfileId{1});
    CHECK(first->FindAiProfile("z") == second->FindAiProfile("z"));
    CHECK(first->FindAiProfile("missing") == AiProfileId{});
    CHECK(first->TryGetAiProfile({}) == nullptr);
    CHECK(first->TryGetAiProfile({3}) == nullptr);
    AiProfile defaults;
    defaults.id = "z";
    CHECK(first->GetAiProfile({2}) == defaults);
    CHECK(ValidAiProfile(first->GetAiProfile({1})));
    const ContentSource legacy{"legacy", "{format_version:1,definitions:[]}"};
    const auto old = LoadContent({&legacy, 1});
    REQUIRE(old.has_value());
    CHECK(old->AiProfileCount() == 0);
}

TEST_CASE("AI profile schema rejects invalid fields with source paths", "[content][ai-content]") {
    struct InvalidCase { const char* profile; const char* path; };
    const InvalidCase cases[] = {
        {"null", "ai_profiles[0]"}, {"{}", "ai_profiles[0].id"},
        {"{id:'Bad'}", "ai_profiles[0].id"}, {"{id:'a',extra:1}", "ai_profiles[0].extra"},
        {"{id:'a',id:'a'}", "ai_profiles[0].id"},
        {"{id:'a',sight_range:0}", "ai_profiles[0].sight_range"},
        {"{id:'a',sight_range:33}", "ai_profiles[0].sight_range"},
        {"{id:'a',dim_range:33}", "ai_profiles[0].dim_range"},
        {"{id:'a',dark_range:-1}", "ai_profiles[0].dark_range"},
        {"{id:'a',dim_range:13}", "ai_profiles[0].dim_range"},
        {"{id:'a',dark_range:7}", "ai_profiles[0].dark_range"},
        {"{id:'a',scan_interval_ticks:0}", "ai_profiles[0].scan_interval_ticks"},
        {"{id:'a',scan_interval_ticks:60001}", "ai_profiles[0].scan_interval_ticks"},
        {"{id:'a',memory_ticks:0}", "ai_profiles[0].memory_ticks"},
        {"{id:'a',memory_ticks:60001}", "ai_profiles[0].memory_ticks"},
        {"{id:'a',search_ticks:0}", "ai_profiles[0].search_ticks"},
        {"{id:'a',search_ticks:60001}", "ai_profiles[0].search_ticks"},
        {"{id:'a',search_ticks:1201}", "ai_profiles[0].search_ticks"},
        {"{id:'a',candidate_budget:0}", "ai_profiles[0].candidate_budget"},
        {"{id:'a',candidate_budget:33}", "ai_profiles[0].candidate_budget"},
        {"{id:'a',bucket_budget:0}", "ai_profiles[0].bucket_budget"},
        {"{id:'a',bucket_budget:65}", "ai_profiles[0].bucket_budget"},
        {"{id:'a',switch_advantage:33}", "ai_profiles[0].switch_advantage"},
        {"{id:'a',switch_advantage:true}", "ai_profiles[0].switch_advantage"},
        {"{id:'a',switch_advantage:1.0}", "ai_profiles[0].switch_advantage"},
    };
    for (const auto& invalid : cases) {
        CAPTURE(invalid.profile);
        Diagnostics diagnostics;
        const auto result = Load(invalid.profile, &diagnostics);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error() == ContentError::InvalidData);
        REQUIRE_FALSE(diagnostics.Empty());
        REQUIRE(diagnostics.Frames().front().location.has_value());
        CHECK(diagnostics.Frames().front().location->file == "ai.json5");
        CHECK(diagnostics.Frames().front().location->path == invalid.path);
        CHECK_FALSE(Load(invalid.profile).has_value());
    }
}

TEST_CASE("AI profile duplicate IDs report both sources", "[content][ai-content]") {
    const ContentSource sources[] = {
        {"first", "{format_version:1,ai_profiles:[{id:'same'}]}"},
        {"second", "{format_version:1,ai_profiles:[{id:'same'}]}"},
    };
    Diagnostics diagnostics;
    const auto result = LoadContent({sources, 2}, &diagnostics);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error() == ContentError::InvalidData);
    REQUIRE(diagnostics.Frames().size() >= 2);
    REQUIRE(diagnostics.Frames()[0].location.has_value());
    REQUIRE(diagnostics.Frames()[1].location.has_value());
    CHECK(diagnostics.Frames()[0].location->file == "second");
    CHECK(diagnostics.Frames()[1].location->file == "first");
    CHECK(diagnostics.Frames()[0].location->path == "ai_profiles[0].id");
    const ContentSource badArray{"bad", "{format_version:1,ai_profiles:{}}"};
    CHECK_FALSE(LoadContent({&badArray, 1}).has_value());
    const ContentSource collision{"collision", "{format_version:1,ai_profiles:[{id:'x'}],definitions:[{type:'item',id:'x',name:'X',mass_g:0,volume_ml:1}]}"};
    CHECK_FALSE(LoadContent({&collision, 1}).has_value());
}

}
