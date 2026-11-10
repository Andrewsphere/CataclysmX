#include "content/tileset_source.h"
#include "content_fixture.h"
#include <catch2/catch_test_macros.hpp>

using namespace cataclysmx;
namespace {
Vector<SpriteSourceFile> Sources() {
    return {{"tile/a.png", {}, 32, 32}, {"tile/b.png", {}, 32, 32},
        {"tile/idle.meta", "{kind:'animation',frames:[{sprite:'tile.a',duration_ms:120},{sprite:'tile.b',duration_ms:180}]}"}};
}
ContentResult<TilesetDefinition> Parse(const char* mappings, Diagnostics* diagnostics = nullptr) {
    String manifest = "{format_version:2,id:'demo',name:'Demo',cell_width:32,cell_height:32,";
    manifest += mappings; manifest += '}';
    const auto files = Sources();
    return ParseSourceTileset(manifest, {files.data(), files.size()}, TestContent(), diagnostics);
}
}

TEST_CASE("tileset resolves mappings and samples exact animation boundaries", "[sdl][tileset]") {
    auto parsed = Parse("terrain:{floor:{visual:'tile.a'}},generic:{player:{visual:'tile.idle'}}"); REQUIRE(parsed);
    CHECK(parsed->terrain[TestTerrain("floor").value].kind == TilesetMappingKind::Sprite);
    CHECK(parsed->generic[0].kind == TilesetMappingKind::Animation);
    const auto& clip = parsed->animations[0];
    CHECK(SampleAnimation(clip, 0).spriteIndex == 0);
    CHECK(*SampleAnimation(clip, 119).delayMilliseconds == 1);
    CHECK(SampleAnimation(clip, 120).spriteIndex == 1);
    CHECK(SampleAnimation(clip, 300).spriteIndex == 0);
    CHECK(SampleAnimation(clip, UINT64_MAX).spriteIndex == SampleAnimation(clip, UINT64_MAX % 300).spriteIndex);
    AnimationClip single; single.frames.push_back({7, 1}); single.totalMilliseconds = 1;
    CHECK_FALSE(SampleAnimation(single, 123).delayMilliseconds);
}

TEST_CASE("tileset rejects prebuilt atlas fields and invalid references", "[sdl][tileset]") {
    CHECK(Parse(""));
    for (const char* fields : {
        "atlases:{}", "sprites:{}", "animations:{}",
        "terrain:{unknown:{visual:'tile.a'}}", "generic:{player:{visual:'missing'}}",
        "terrain:{floor:{sprite:'tile.a'}}", "generic:{player:{animation:'tile.idle'}}"}) {
        CAPTURE(fields);
        Diagnostics diagnostics;
        CHECK_FALSE(Parse(fields, &diagnostics)); CHECK_FALSE(diagnostics.Empty());
    }
    for (const char* frames : {"[]", "[{sprite:'tile.a',duration_ms:0}]",
        "[{sprite:'tile.a',duration_ms:18446744073709551615}]",
        "[{sprite:'tile.a',duration_ms:18446744073709},{sprite:'tile.b',duration_ms:1}]"}) {
        auto files = Sources();
        files.back().text = "{kind:'animation',frames:"; files.back().text += frames; files.back().text += '}';
        CHECK_FALSE(ParseSourceTileset("{format_version:2,id:'demo',name:'Demo',cell_width:32,cell_height:32}",
            {files.data(), files.size()}, TestContent()));
    }
}

TEST_CASE("tileset validates optional sprite activation bindings", "[sdl][tileset][animation]") {
    const auto parse = [](const char* mapping, Diagnostics* diagnostics = nullptr) {
        String text = "generic:{player:"; text += mapping; text += '}';
        return Parse(text.c_str(), diagnostics);
    };
    const auto toggle = parse("{visual:'tile.a',activation:{animation:'tile.idle',mode:'toggle_loop'}}");
    REQUIRE(toggle); REQUIRE(toggle->generic[0].activation);
    CHECK(toggle->generic[0].activation->kind == SpriteActivation::ToggleLoop);
    CHECK(toggle->generic[0].activation->animationIndex == 0);
    const auto once = parse("{visual:'tile.idle',activation:{animation:'tile.idle',mode:'once'}}");
    REQUIRE(once); CHECK(once->generic[0].activation->kind == SpriteActivation::PlayOnce);
    for (const char* mapping : {
        "{visual:'tile.a',animation:'tile.idle'}", "{activation:{animation:'tile.idle',mode:'once'}}",
        "{visual:'tile.a',activation:{animation:'missing',mode:'once'}}",
        "{visual:'tile.a',activation:{animation:'tile.idle',mode:'invalid'}}",
        "{visual:'tile.a',activation:{animation:'tile.idle'}}",
        "{visual:'tile.a',activation:{animation:'tile.idle',mode:'once',extra:true}}",
        "{visual:'tile.a',activation:null}"}) {
        Diagnostics diagnostics;
        CHECK_FALSE(parse(mapping, &diagnostics)); CHECK_FALSE(diagnostics.Empty());
    }
}
