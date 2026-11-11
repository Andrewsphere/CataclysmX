#include "sdl/animation.h"
#include "sdl/map_packet.h"
#include "content_fixture.h"

#include <catch2/catch_test_macros.hpp>

using namespace cataclysmx;
using namespace cataclysmx::sdl;

namespace {
TilesetDefinition Clips() {
    TilesetDefinition definition;
    definition.animations.push_back({"blink", {{1, 10}, {2, 30}}, 30});
    definition.animations.push_back({"single", {{1, 15}}, 15});
    return definition;
}
TilesetMapping Controlled(SpriteActivation kind, std::uint32_t clip = 0) {
    return {TilesetMappingKind::Sprite, 0, SpriteActivationBinding{clip, kind}};
}
}

TEST_CASE("animation once sampling completes without wrapping", "[sdl][animation]") {
    const auto definition = Clips();
    const auto& clip = definition.animations[0];
    CHECK(SampleAnimation(clip, 0, AnimationPlayback::Once).spriteIndex == 1);
    CHECK(*SampleAnimation(clip, 9, AnimationPlayback::Once).delayMilliseconds == 1);
    CHECK(SampleAnimation(clip, 10, AnimationPlayback::Once).spriteIndex == 2);
    CHECK(*SampleAnimation(clip, 29, AnimationPlayback::Once).delayMilliseconds == 1);
    CHECK_FALSE(SampleAnimation(clip, 29, AnimationPlayback::Once).completed);
    for (auto time : {std::uint64_t{30}, std::uint64_t{31}, UINT64_MAX}) {
        const auto sample = SampleAnimation(clip, time, AnimationPlayback::Once);
        CHECK(sample.completed);
        CHECK(sample.spriteIndex == 2);
        CHECK_FALSE(sample.delayMilliseconds);
    }
    const auto& single = definition.animations[1];
    CHECK_FALSE(SampleAnimation(single, UINT64_MAX).delayMilliseconds);
    CHECK(*SampleAnimation(single, 14, AnimationPlayback::Once).delayMilliseconds == 1);
    CHECK(SampleAnimation(single, 15, AnimationPlayback::Once).completed);
}

TEST_CASE("sprite activation toggles synchronized loops and restarts independent single blinks", "[sdl][animation]") {
    const auto definition = Clips();
    const auto loop = Controlled(SpriteActivation::ToggleLoop);
    const auto once = Controlled(SpriteActivation::PlayOnce);
    SpritePlaybackController playback;
    playback.SetGeneration(1, 2);
    CHECK(playback.Sample(7, loop, definition, 10).mapping.index == 0);
    CHECK_FALSE(playback.Sample(7, loop, definition, 10).delayMilliseconds);
    REQUIRE(playback.Activate(7, loop, definition, 12) == SpriteActivationResult::LoopStarted);
    // Activation does not shift a loop's shared epoch.
    CHECK(playback.Sample(7, loop, definition, 12).mapping.index == 2);
    REQUIRE(playback.Activate(8, loop, definition, 22) == SpriteActivationResult::LoopStarted);
    CHECK(playback.Sample(7, loop, definition, 31).mapping == playback.Sample(8, loop, definition, 31).mapping);
    REQUIRE(playback.Activate(7, loop, definition, 32) == SpriteActivationResult::Stopped);
    CHECK_FALSE(playback.Sample(7, loop, definition, 32).delayMilliseconds);
    CHECK(playback.Sample(8, loop, definition, 32).delayMilliseconds);
    playback.Stop(8);
    CHECK_FALSE(playback.Sample(8, loop, definition, 32).delayMilliseconds);

    REQUIRE(playback.Activate(7, once, definition, 100) == SpriteActivationResult::PlayedOnce);
    REQUIRE(playback.Activate(8, once, definition, 108) == SpriteActivationResult::PlayedOnce);
    CHECK(playback.Sample(7, once, definition, 110).mapping.index == 2);
    CHECK(playback.Sample(8, once, definition, 110).mapping.index == 1);
    REQUIRE(playback.Activate(7, once, definition, 110) == SpriteActivationResult::PlayedOnce);
    CHECK(playback.Sample(7, once, definition, 110).mapping.index == 1);
    CHECK(playback.Sample(7, once, definition, 140).mapping.index == 0);
    CHECK_FALSE(playback.Sample(7, once, definition, 140).delayMilliseconds);
    playback.RetireCompleted(definition, UINT64_MAX);
    CHECK(playback.Sample(8, once, definition, UINT64_MAX).mapping.index == 0);
    CHECK(playback.Activate(0, once, definition, 0) == SpriteActivationResult::Unavailable);
    CHECK(playback.Activate(7, {}, definition, 0) == SpriteActivationResult::Unavailable);
    const TilesetMapping ambientBase{TilesetMappingKind::Animation, 0,
        SpriteActivationBinding{1, SpriteActivation::PlayOnce}};
    REQUIRE(playback.Activate(9, ambientBase, definition, 100) == SpriteActivationResult::PlayedOnce);
    CHECK(playback.Sample(9, ambientBase, definition, 100).mapping.index == 1);
    CHECK(playback.Sample(9, ambientBase, definition, 115).mapping.index == 2);
    CHECK(*playback.Sample(9, ambientBase, definition, 115).delayMilliseconds == 5);
}

TEST_CASE("sprite playback survives absent packets and resets for new generations", "[sdl][animation]") {
    const auto definition = Clips();
    const auto loop = Controlled(SpriteActivation::ToggleLoop);
    const auto once = Controlled(SpriteActivation::PlayOnce, 1);
    SpritePlaybackController playback;
    playback.SetGeneration(1, 2);
    REQUIRE(playback.Activate(7, loop, definition, 0) == SpriteActivationResult::LoopStarted);
    // No sampling while the item is hidden, carried, occluded, or minimized.
    playback.SetGeneration(1, 2);
    CHECK(playback.Sample(7, loop, definition, UINT64_MAX).delayMilliseconds);
    REQUIRE(playback.Activate(8, once, definition, 100) == SpriteActivationResult::PlayedOnce);
    CHECK(*playback.Sample(8, once, definition, 114).delayMilliseconds == 1);
    CHECK(playback.Sample(8, once, definition, 115).mapping.index == 0);
    playback.SetGeneration(1, 3);
    CHECK_FALSE(playback.Sample(7, loop, definition, 0).delayMilliseconds);
    REQUIRE(playback.Activate(7, loop, definition, 0) == SpriteActivationResult::LoopStarted);
    playback.SetGeneration(2, 3);
    CHECK_FALSE(playback.Sample(7, loop, definition, 0).delayMilliseconds);
}

TEST_CASE("SDL item visual capture follows visibility overlay depth and persistent ID order", "[sdl][animation][map]") {
    Server server(TestContent());
    auto& world = server.GetWorld();
    WorldView view;
    view.width = view.height = 1;
    view.origin = {1, 1, 1};
    view.cells.resize(1);
    auto& cell = view.cells[0];
    cell.overlayVisibility = Visibility::Visible;
    cell.ground_items = 1;
    cell.overlayDepth = 1;
    cell.terrainDepth = 2;
    const auto initial = CaptureMapItems(world, view);
    REQUIRE(initial[0].item != 0);
    CHECK(initial[0].type == TestItem("blinking_lightbulb"));
    const auto second = world.CreateItem(TestItem("toggle_lightbulb"), ItemLocation::Ground({1, 1}));
    REQUIRE(second);
    CHECK(CaptureMapItems(world, view) == initial);
    cell.overlayVisibility = Visibility::Unknown;
    CHECK(CaptureMapItems(world, view)[0].item == 0);
    cell.overlayVisibility = Visibility::Remembered;
    CHECK(CaptureMapItems(world, view)[0].item == 0);
    cell.overlayVisibility = Visibility::Visible;
    cell.actor = 123;
    CHECK(CaptureMapItems(world, view)[0].item == 0);
    cell.actor = 0;
    cell.ground_items = 0;
    CHECK(CaptureMapItems(world, view)[0].item == 0);

    const char* names[] = {"blinking_lightbulb", "toggle_lightbulb", "single_blink_lightbulb"};
    const TileCoord positions[] = {{1, 1}, {2, 1}, {1, 2}};
    for (unsigned i = 0; i < 3; ++i) {
        const auto items = world.ItemsAt(positions[i]);
        REQUIRE_FALSE(items.Empty());
        bool found = false;
        for (ItemHandle handle : items) {
            if (world.GetItem(handle)->type == TestItem(names[i])) { found = true; }
        }
        CHECK(found);
    }
}

TEST_CASE("map packets resolve item artwork and retain visual identity without world borrows", "[sdl][animation][map]") {
    Server server(TestContent());
    WorldView view;
    view.width = view.height = 1;
    view.origin = {1, 1};
    view.cells.resize(1);
    view.cells[0].terrainVisibility = view.cells[0].overlayVisibility = Visibility::Visible;
    view.cells[0].tile.terrain = TestTerrain("floor");
    view.cells[0].ground_items = 1;
    view.cells[0].object = 123;
    auto items = CaptureMapItems(server.GetWorld(), view);
    TilesetDefinition tileset = Clips();
    tileset.items.resize(TestContent().ItemCount() + 1);
    const auto mapping = Controlled(SpriteActivation::ToggleLoop);
    tileset.items[items[0].type.value] = mapping;
    tileset.generic[static_cast<unsigned>(GenericSprite::GroundItems)] = {TilesetMappingKind::Sprite, 9};
    const auto packet = PrepareMapPacket(view, TestContent(), tileset, {1, 2, 3, 4}, items);
    CHECK(packet.cells[0].itemVisual == items[0]);
    CHECK(packet.cells[0].overlaySprite == mapping);
    SpritePlaybackController playback;
    playback.SetGeneration(1, 3);
    REQUIRE(playback.Activate(items[0].item, mapping, tileset, 0) == SpriteActivationResult::LoopStarted);
    const auto replacement = PrepareMapPacket(view, TestContent(), tileset, {1, 3, 3, 5}, items);
    CHECK(*NextAnimationDelay(replacement, tileset, 9, &playback) == 1);
    auto handle = server.GetWorld().FindItem(items[0].item);
    REQUIRE(server.GetWorld().TransferItem(handle, ItemLocation::Carried(server.GetPlayer())));
    CHECK(CaptureMapItems(server.GetWorld(), view)[0].item == 0);
    REQUIRE(server.GetWorld().TransferItem(handle, ItemLocation::Ground({1, 1})));
    items = CaptureMapItems(server.GetWorld(), view);
    const auto dropped = PrepareMapPacket(view, TestContent(), tileset, {1, 4, 3, 5}, items);
    CHECK(*NextAnimationDelay(dropped, tileset, 10, &playback) == 20);
    tileset.items[items[0].type.value] = {};
    CHECK(PrepareMapPacket(view, TestContent(), tileset, {}, items).cells[0].overlaySprite.index == 9);
    view.cells[0].actor = 123;
    CHECK(PrepareMapPacket(view, TestContent(), tileset, {}, items).cells[0].itemVisual.item == 0);
    view.cells[0].terrainVisibility = view.cells[0].overlayVisibility = Visibility::Unknown;
    const auto hidden = PrepareMapPacket(view, TestContent(), tileset, {}, items);
    CHECK(hidden.cells[0].itemVisual.item == 0);
    CHECK_FALSE(NextAnimationDelay(hidden, tileset, 0, &playback));
}
