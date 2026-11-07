#include "core/world.h"

#include <catch2/catch_test_macros.hpp>

#include <climits>
#include <EASTL/utility.h>

namespace {

using namespace cataclysmx;

template<class T>
T take(WorldResult<T> result) {
    REQUIRE(result.has_value());
    return eastl::move(*result);
}

LoadRequest load(World& world, ChunkRegion region = {{0, 0}, 0}, LoadMode mode = LoadMode::active) {
    const LoadRequest request = take(world.request_chunks(region, mode));
    world.reconcile_chunks();
    REQUIRE(world.check_invariants());
    return request;
}

Vector<PersistentId> actor_ids(const World& world) {
    Vector<PersistentId> result;
    for (ActorHandle handle : world.active_actors()) {
        result.push_back(world.actor(handle)->id);
    }
    return result;
}

Vector<PersistentId> object_ids(const World& world) {
    Vector<PersistentId> result;
    for (ObjectHandle handle : world.active_objects()) {
        result.push_back(world.object(handle)->id);
    }
    return result;
}

TEST_CASE("World coordinates use floor division and bounded positions", "[world][coordinates]") {
    CHECK(chunk_of({0, 0}) == ChunkCoord{0, 0});
    CHECK(chunk_of({15, 16}) == ChunkCoord{0, 1});
    CHECK(chunk_of({-1, -16}) == ChunkCoord{-1, -1});
    CHECK(chunk_of({-17, 31}) == ChunkCoord{-2, 1});
    CHECK(tile_index({-1, -1}) == 255);
    CHECK(tile_index({-16, -16}) == 0);
    CHECK(tile_index({-17, 16}) == 15);
    CHECK(tile_index({INT_MIN, INT_MAX}) == 240);
    CHECK(valid_position({-world_coordinate_limit, world_coordinate_limit}));
    CHECK_FALSE(valid_position({world_coordinate_limit + 1, 0}));
    CHECK_FALSE(valid_position({0, -world_coordinate_limit - 1}));
    CHECK_FALSE(valid_position({INT_MIN, INT_MAX}));
    CHECK(passable(Terrain::floor));
    CHECK(passable(Terrain::open_door));
    CHECK_FALSE(passable(Terrain::wall));
    CHECK_FALSE(passable(Terrain::closed_door));

    World world;
    load(world, {{-1, -1}, 0});
    REQUIRE(world.set_tile({-1, -1}, {Terrain::open_door, 7}));
    CHECK(world.chunk({-1, -1})->tiles[255] == Tile{Terrain::open_door, 7});
    CHECK(world.tile({0, 0}) == nullptr);
    CHECK(world.set_tile({0, 0}, {}) == eastl::unexpected(WorldError::chunk_unavailable));
    CHECK(world.create_actor({INT_MAX, 0}) == eastl::unexpected(WorldError::invalid_position));
    CHECK(world.create_object({0, INT_MIN}) == eastl::unexpected(WorldError::invalid_position));
    CHECK(world.create_item(ItemKind::rock, ItemLocation::ground({world_coordinate_limit + 1, 0}))
        == eastl::unexpected(WorldError::invalid_position));

    load(world, {{62'500, -62'500}, 0});
    REQUIRE(world.tile({world_coordinate_limit, -world_coordinate_limit}) != nullptr);
    CHECK(world.tile({world_coordinate_limit + 1, -world_coordinate_limit}) == nullptr);
    REQUIRE(world.set_tile({world_coordinate_limit, -world_coordinate_limit}, {}));
    take(world.create_actor({world_coordinate_limit, -world_coordinate_limit}));
    CHECK(world.check_invariants());
}

TEST_CASE("Chunk requests combine demand and reject invalid updates atomically", "[world][streaming]") {
    World world;
    CHECK(world.request_chunks({{0, 0}, -1}, LoadMode::active)
        == eastl::unexpected(WorldError::invalid_region));
    CHECK(world.request_chunks({{0, 0}, 9}, LoadMode::active)
        == eastl::unexpected(WorldError::invalid_region));
    CHECK(world.request_chunks({{INT_MAX, INT_MIN}, 1}, LoadMode::active)
        == eastl::unexpected(WorldError::invalid_region));
    CHECK(world.request_chunks({{62'500, 0}, 1}, LoadMode::resident)
        == eastl::unexpected(WorldError::invalid_region));
    CHECK(world.request_chunks({{0, 0}, 0}, static_cast<LoadMode>(99))
        == eastl::unexpected(WorldError::invalid_request));
    const LoadRequest resident = take(world.request_chunks({{0, 0}, 1}, LoadMode::resident));
    const LoadRequest active = take(world.request_chunks({{0, 0}, 0}, LoadMode::active));
    const LoadRequest overlap = take(world.request_chunks({{1, 0}, 0}, LoadMode::active));
    CHECK(world.loaded_chunks().empty());
    world.reconcile_chunks();
    REQUIRE(world.loaded_chunks().size() == 9);
    CHECK(world.loaded_chunks().front() == ChunkCoord{-1, -1});
    CHECK(world.loaded_chunks().back() == ChunkCoord{1, 1});
    CHECK(world.active({0, 0}));
    CHECK(world.active({16, 0}));
    CHECK_FALSE(world.active({-1, -1}));
    CHECK(world.update_request(resident, {{0, 0}, 9}, LoadMode::active)
        == eastl::unexpected(WorldError::invalid_region));
    CHECK(world.update_request(active, {{0, 0}, 0}, static_cast<LoadMode>(99))
        == eastl::unexpected(WorldError::invalid_request));
    world.reconcile_chunks();
    CHECK(world.loaded_chunks().size() == 9);
    REQUIRE(world.release_request(active));
    world.reconcile_chunks();
    CHECK_FALSE(world.active({0, 0}));
    CHECK(world.chunk({0, 0}) != nullptr);
    CHECK(world.release_request(active) == eastl::unexpected(WorldError::invalid_request));
    CHECK(world.update_request(active, {{0, 0}, 0}, LoadMode::active)
        == eastl::unexpected(WorldError::invalid_request));
    CHECK(world.release_request({}) == eastl::unexpected(WorldError::invalid_request));
    CHECK(world.update_request({UINT64_MAX}, {{0, 0}, 0}, LoadMode::active)
        == eastl::unexpected(WorldError::invalid_request));
    REQUIRE(world.update_request(overlap, {{-2, 0}, 0}, LoadMode::resident));
    world.reconcile_chunks();
    CHECK(world.loaded_chunks().size() == 10);
    CHECK(world.loaded_chunks().front() == ChunkCoord{-2, 0});
    CHECK_FALSE(world.active({16, 0}));
    REQUIRE(world.release_request(resident));
    world.reconcile_chunks();
    CHECK(world.loaded_chunks() == Vector<ChunkCoord>{{-2, 0}});
    CHECK(world.archived_chunk_count() == 9);
    REQUIRE(world.release_request(overlap));
    world.reconcile_chunks();
    CHECK(world.loaded_chunks().empty());
    CHECK(world.archived_chunk_count() == 10);
    CHECK(world.check_invariants());

    // Exercise SlotMap request recycling, not just never-reused indices.
    for (int index = 0; index < 140; ++index) {
        const auto request = take(world.request_chunks({{0, 0}, 0}, LoadMode::resident));
        CHECK(request != active);
        REQUIRE(world.release_request(request));
    }
    CHECK(world.release_request(active) == eastl::unexpected(WorldError::invalid_request));
}

TEST_CASE("Only active chunks accrue turns and enabled object updates", "[world][activity]") {
    World world;
    const auto request = load(world, {{0, 0}, 0}, LoadMode::resident);
    const auto actor = take(world.create_actor({0, 0}, true));
    const auto object = take(world.create_object({1, 0}, true));
    CHECK(world.active_actors().empty());
    CHECK(world.active_objects().empty());
    CHECK(world.record_actor_turn(actor) == eastl::unexpected(WorldError::chunk_unavailable));
    CHECK(world.record_object_update(object) == eastl::unexpected(WorldError::chunk_unavailable));
    CHECK(world.actor(actor)->turns == 0);
    CHECK(world.object(object)->updates == 0);
    REQUIRE(world.update_request(request, {{0, 0}, 0}, LoadMode::active));
    world.reconcile_chunks();
    CHECK(world.active_actors() == Vector<ActorHandle>{actor});
    CHECK(world.active_objects() == Vector<ObjectHandle>{object});
    REQUIRE(world.record_actor_turn(actor));
    REQUIRE(world.record_object_update(object));
    REQUIRE(world.set_object_enabled(object, false));
    REQUIRE(world.record_object_update(object));
    CHECK(world.actor(actor)->turns == 1);
    CHECK(world.object(object)->updates == 1);
    REQUIRE(world.update_request(request, {{0, 0}, 0}, LoadMode::resident));
    world.reconcile_chunks();
    CHECK(world.record_object_update(object) == eastl::unexpected(WorldError::chunk_unavailable));
    CHECK(world.record_actor_turn({}) == eastl::unexpected(WorldError::invalid_handle));
    CHECK(world.record_object_update({UINT64_MAX}) == eastl::unexpected(WorldError::invalid_handle));
    CHECK(world.check_invariants());
}

TEST_CASE("Actor movement keeps handles and inventory across chunk boundaries", "[world][actors]") {
    World world;
    const auto origin = load(world);
    load(world, {{1, 0}, 0}, LoadMode::resident);
    const auto actor = take(world.create_actor({15, 1}, true));
    const auto backpack = take(world.create_item(ItemKind::backpack, ItemLocation::carried(actor)));
    const auto rock = take(world.create_item(ItemKind::rock, ItemLocation::inside(backpack)));
    const PersistentId id = world.actor(actor)->id;
    REQUIRE(world.move_actor(actor, {16, 1}));
    CHECK(world.actor(actor)->id == id);
    CHECK(world.actor_at({15, 1}) == ActorHandle{});
    CHECK(world.actor_at({16, 1}) == actor);
    CHECK(world.chunk({0, 0})->actors.empty());
    CHECK(world.chunk({1, 0})->actors == Vector<ActorHandle>{actor});
    CHECK(world.item(backpack)->location == ItemLocation::carried(actor));
    CHECK(world.item(rock)->location == ItemLocation::inside(backpack));
    CHECK(world.active_actors().empty());
    REQUIRE(world.move_actor(actor, {16, 1}));
    const auto other = take(world.create_actor({17, 1}));
    CHECK(world.move_actor(actor, {17, 1}) == eastl::unexpected(WorldError::occupied));
    CHECK(world.create_actor({17, 1}) == eastl::unexpected(WorldError::occupied));
    REQUIRE(world.set_tile({18, 1}, {Terrain::closed_door, 0}));
    CHECK(world.move_actor(actor, {18, 1}) == eastl::unexpected(WorldError::blocked));
    CHECK(world.move_actor(actor, {32, 1}) == eastl::unexpected(WorldError::chunk_unavailable));
    CHECK(world.move_actor(actor, {INT_MAX, 1}) == eastl::unexpected(WorldError::invalid_position));
    CHECK(world.move_actor({}, {16, 1}) == eastl::unexpected(WorldError::invalid_handle));
    CHECK(world.actor(actor)->position == TileCoord{16, 1});
    CHECK(world.actor(other)->position == TileCoord{17, 1});
    REQUIRE(world.release_request(origin));
    world.reconcile_chunks();
    REQUIRE(world.actor(actor) != nullptr);
    REQUIRE(world.item(rock) != nullptr);
    CHECK(world.find_actor(id) == actor);
    CHECK(world.check_invariants());
}

TEST_CASE("Item transfers validate destinations and cycles before changing ownership", "[world][items]") {
    World world;
    load(world);
    load(world, {{1, 0}, 0}, LoadMode::resident);
    const auto actor = take(world.create_actor({1, 1}));
    const auto object = take(world.create_object({2, 1}));
    const auto root = take(world.create_item(ItemKind::backpack, ItemLocation::ground({0, 0})));
    const auto nested = take(world.create_item(ItemKind::backpack, ItemLocation::inside(root)));
    const auto rock = take(world.create_item(ItemKind::rock, ItemLocation::inside(nested)));
    CHECK(world.transfer_item(root, ItemLocation::inside(root))
        == eastl::unexpected(WorldError::containment_cycle));
    CHECK(world.transfer_item(root, ItemLocation::inside(nested))
        == eastl::unexpected(WorldError::containment_cycle));
    CHECK(world.transfer_item(nested, ItemLocation::inside(rock))
        == eastl::unexpected(WorldError::not_container));
    CHECK(world.create_item(ItemKind::bandage, ItemLocation::inside(rock))
        == eastl::unexpected(WorldError::not_container));
    CHECK(world.transfer_item(root, ItemLocation::carried({UINT64_MAX}))
        == eastl::unexpected(WorldError::invalid_handle));
    CHECK(world.transfer_item(root, ItemLocation::stored({}))
        == eastl::unexpected(WorldError::invalid_handle));
    CHECK(world.transfer_item(root, ItemLocation::inside({}))
        == eastl::unexpected(WorldError::invalid_handle));
    CHECK(world.transfer_item(root, ItemLocation::ground({32, 0}))
        == eastl::unexpected(WorldError::chunk_unavailable));
    CHECK(world.transfer_item(root, ItemLocation::ground({INT_MIN, 0}))
        == eastl::unexpected(WorldError::invalid_position));
    CHECK(world.transfer_item({}, ItemLocation::carried(actor))
        == eastl::unexpected(WorldError::invalid_handle));
    CHECK(world.items_at({0, 0}) == Vector<ItemHandle>{root});
    CHECK(world.item(root)->contents == Vector<ItemHandle>{nested});
    CHECK(world.item(nested)->contents == Vector<ItemHandle>{rock});
    CHECK(world.item(root)->location == ItemLocation::ground({0, 0}));
    CHECK(world.actor(actor)->inventory.empty());
    CHECK(world.object(object)->contents.empty());
    REQUIRE(world.transfer_item(root, ItemLocation::carried(actor)));
    CHECK(world.items_at({0, 0}).empty());
    CHECK(world.actor(actor)->inventory == Vector<ItemHandle>{root});
    REQUIRE(world.transfer_item(root, ItemLocation::carried(actor)));
    CHECK(world.actor(actor)->inventory.size() == 1);
    REQUIRE(world.transfer_item(root, ItemLocation::stored(object)));
    CHECK(world.actor(actor)->inventory.empty());
    CHECK(world.object(object)->contents == Vector<ItemHandle>{root});
    REQUIRE(world.transfer_item(root, ItemLocation::ground({16, 2})));
    CHECK(world.object(object)->contents.empty());
    CHECK(world.chunk({0, 0})->ground_items.empty());
    CHECK(world.chunk({1, 0})->ground_items == Vector<ItemHandle>{root});
    CHECK(world.item(rock)->location == ItemLocation::inside(nested));
    REQUIRE(world.transfer_item(rock, ItemLocation::carried(actor)));
    CHECK(world.item(nested)->contents.empty());
    CHECK(world.actor(actor)->inventory == Vector<ItemHandle>{rock});
    CHECK(world.check_invariants());
}

TEST_CASE("Chunk archives preserve values and nested ownership with fresh runtime keys", "[world][streaming][items]") {
    World world(123);
    const auto origin = load(world);
    load(world, {{2, 0}, 0}, LoadMode::resident);
    const auto actor = take(world.create_actor({1, 1}, true));
    const auto object = take(world.create_object({2, 2}, true));
    const auto bag = take(world.create_item(ItemKind::backpack, ItemLocation::carried(actor)));
    const auto nested = take(world.create_item(ItemKind::backpack, ItemLocation::inside(bag)));
    const auto rock = take(world.create_item(ItemKind::rock, ItemLocation::inside(nested)));
    const auto bandage = take(world.create_item(ItemKind::bandage, ItemLocation::inside(bag)));
    const auto carried = take(world.create_item(ItemKind::rock, ItemLocation::carried(actor)));
    const auto stored = take(world.create_item(ItemKind::backpack, ItemLocation::stored(object)));
    const auto stored_child = take(world.create_item(ItemKind::bandage, ItemLocation::inside(stored)));
    const auto ground = take(world.create_item(ItemKind::backpack, ItemLocation::ground({0, 4})));
    const auto ground_child = take(world.create_item(ItemKind::rock, ItemLocation::inside(ground)));
    // Reorder siblings so reload cannot accidentally rely on ID or slot order.
    REQUIRE(world.transfer_item(nested, ItemLocation::ground({0, 0})));
    REQUIRE(world.transfer_item(nested, ItemLocation::inside(bag)));
    const PersistentId actor_id = world.actor(actor)->id;
    const PersistentId object_id = world.object(object)->id;
    const Vector<ItemHandle> old_items{bag, nested, rock, bandage, carried, stored,
        stored_child, ground, ground_child};
    Vector<PersistentId> item_ids;
    for (const auto handle : old_items) {
        item_ids.push_back(world.item(handle)->id);
    }
    REQUIRE(world.record_actor_turn(actor));
    REQUIRE(world.record_actor_turn(actor));
    REQUIRE(world.record_object_update(object));
    REQUIRE(world.set_object_enabled(object, false));
    REQUIRE(world.set_tile({3, 3}, {Terrain::closed_door, 19}));
    CHECK(world.check_invariants());
    REQUIRE(world.release_request(origin));
    world.reconcile_chunks();
    CHECK(world.archived_chunk_count() == 1);
    CHECK(world.actor(actor) == nullptr);
    CHECK(world.object(object) == nullptr);
    CHECK(world.find_actor(actor_id) == ActorHandle{});
    CHECK(world.find_object(object_id) == ObjectHandle{});
    for (std::size_t index = 0; index < old_items.size(); ++index) {
        CHECK(world.item(old_items[index]) == nullptr);
        CHECK(world.find_item(item_ids[index]) == ItemHandle{});
    }
    CHECK(world.destroy_actor(actor) == eastl::unexpected(WorldError::invalid_handle));
    CHECK(world.destroy_object(object) == eastl::unexpected(WorldError::invalid_handle));
    CHECK(world.destroy_item(bag) == eastl::unexpected(WorldError::invalid_handle));
    CHECK(world.check_invariants());
    const auto newer = take(world.create_item(ItemKind::rock, ItemLocation::ground({32, 0})));
    CHECK(world.item(newer)->id > item_ids.back());
    CHECK(world.transfer_item(newer, ItemLocation::carried(actor))
        == eastl::unexpected(WorldError::invalid_handle));
    CHECK(world.transfer_item(newer, ItemLocation::stored(object))
        == eastl::unexpected(WorldError::invalid_handle));
    CHECK(world.transfer_item(newer, ItemLocation::inside(bag))
        == eastl::unexpected(WorldError::invalid_handle));
    CHECK(world.transfer_item(newer, ItemLocation::ground({0, 0}))
        == eastl::unexpected(WorldError::chunk_unavailable));
    CHECK(world.item(newer)->location == ItemLocation::ground({32, 0}));
    const auto reload = load(world);
    CHECK(world.archived_chunk_count() == 0);
    const auto fresh_actor = world.find_actor(actor_id);
    const auto fresh_object = world.find_object(object_id);
    REQUIRE(world.actor(fresh_actor) != nullptr);
    REQUIRE(world.object(fresh_object) != nullptr);
    CHECK(fresh_actor != actor);
    CHECK(fresh_object != object);
    CHECK(world.actor(fresh_actor)->player);
    CHECK(world.actor(fresh_actor)->turns == 2);
    CHECK(world.actor(fresh_actor)->position == TileCoord{1, 1});
    CHECK_FALSE(world.object(fresh_object)->enabled);
    CHECK(world.object(fresh_object)->updates == 1);
    CHECK(*world.tile({3, 3}) == Tile{Terrain::closed_door, 19});
    Vector<ItemHandle> fresh;
    for (std::size_t index = 0; index < old_items.size(); ++index) {
        const auto handle = world.find_item(item_ids[index]);
        REQUIRE(world.item(handle) != nullptr);
        CHECK(handle != old_items[index]);
        CHECK(world.item(handle)->id == item_ids[index]);
        CHECK(world.item(old_items[index]) == nullptr);
        fresh.push_back(handle);
    }
    CHECK(world.actor(fresh_actor)->inventory == Vector<ItemHandle>{fresh[0], fresh[4]});
    CHECK(world.item(fresh[0])->location == ItemLocation::carried(fresh_actor));
    CHECK(world.item(fresh[0])->contents == Vector<ItemHandle>{fresh[3], fresh[1]});
    CHECK(world.item(fresh[1])->location == ItemLocation::inside(fresh[0]));
    CHECK(world.item(fresh[1])->contents == Vector<ItemHandle>{fresh[2]});
    CHECK(world.item(fresh[2])->kind == ItemKind::rock);
    CHECK(world.item(fresh[2])->location == ItemLocation::inside(fresh[1]));
    CHECK(world.item(fresh[3])->kind == ItemKind::bandage);
    CHECK(world.object(fresh_object)->contents == Vector<ItemHandle>{fresh[5]});
    CHECK(world.item(fresh[5])->location == ItemLocation::stored(fresh_object));
    CHECK(world.item(fresh[5])->contents == Vector<ItemHandle>{fresh[6]});
    CHECK(world.item(fresh[6])->location == ItemLocation::inside(fresh[5]));
    CHECK(world.items_at({0, 4}) == Vector<ItemHandle>{fresh[7]});
    CHECK(world.item(fresh[7])->contents == Vector<ItemHandle>{fresh[8]});
    CHECK(world.item(fresh[8])->location == ItemLocation::inside(fresh[7]));
    CHECK(world.check_invariants());
    // A second archive replaces the first value snapshot, including mutations.
    REQUIRE(world.record_actor_turn(fresh_actor));
    REQUIRE(world.set_object_enabled(fresh_object, true));
    REQUIRE(world.record_object_update(fresh_object));
    REQUIRE(world.transfer_item(fresh[2], ItemLocation::stored(fresh_object)));
    REQUIRE(world.release_request(reload));
    world.reconcile_chunks();
    load(world);
    CHECK(world.actor(world.find_actor(actor_id))->turns == 3);
    CHECK(world.object(world.find_object(object_id))->updates == 2);
    CHECK(world.object(world.find_object(object_id))->enabled);
    CHECK(world.item(world.find_item(item_ids[2]))->location
        == ItemLocation::stored(world.find_object(object_id)));
    CHECK(world.check_invariants());
}

TEST_CASE("Actor and object destruction drop trees while item destruction removes descendants", "[world][destruction]") {
    World world;
    const auto request = load(world);
    const auto actor = take(world.create_actor({1, 1}));
    const auto object = take(world.create_object({2, 2}));
    const auto bag = take(world.create_item(ItemKind::backpack, ItemLocation::carried(actor)));
    const auto child = take(world.create_item(ItemKind::backpack, ItemLocation::inside(bag)));
    const auto grandchild = take(world.create_item(ItemKind::bandage, ItemLocation::inside(child)));
    const auto loose = take(world.create_item(ItemKind::rock, ItemLocation::carried(actor)));
    const auto stored = take(world.create_item(ItemKind::backpack, ItemLocation::stored(object)));
    const auto stored_child = take(world.create_item(ItemKind::rock, ItemLocation::inside(stored)));
    const auto id = world.item(grandchild)->id;
    REQUIRE(world.destroy_actor(actor));
    CHECK(world.actor_at({1, 1}) == ActorHandle{});
    CHECK(world.items_at({1, 1}) == Vector<ItemHandle>{bag, loose});
    CHECK(world.item(bag)->location == ItemLocation::ground({1, 1}));
    CHECK(world.item(child)->location == ItemLocation::inside(bag));
    CHECK(world.item(grandchild)->location == ItemLocation::inside(child));
    REQUIRE(world.destroy_object(object));
    CHECK(world.object_at({2, 2}) == ObjectHandle{});
    CHECK(world.item(stored)->location == ItemLocation::ground({2, 2}));
    CHECK(world.item(stored_child)->location == ItemLocation::inside(stored));
    REQUIRE(world.destroy_item(child));
    CHECK(world.item(child) == nullptr);
    CHECK(world.item(grandchild) == nullptr);
    CHECK(world.find_item(id) == ItemHandle{});
    CHECK(world.item(bag)->contents.empty());
    REQUIRE(world.destroy_item(stored));
    CHECK(world.item(stored_child) == nullptr);
    CHECK(world.items_at({2, 2}).empty());
    CHECK(world.destroy_item(child) == eastl::unexpected(WorldError::invalid_handle));
    CHECK(world.destroy_actor(actor) == eastl::unexpected(WorldError::invalid_handle));
    CHECK(world.destroy_object(object) == eastl::unexpected(WorldError::invalid_handle));
    CHECK(world.set_object_enabled(object, true) == eastl::unexpected(WorldError::invalid_handle));
    CHECK(world.check_invariants());
    REQUIRE(world.release_request(request));
    world.reconcile_chunks();
    load(world);
    CHECK(world.find_item(id) == ItemHandle{});
    CHECK(world.check_invariants());
}

TEST_CASE("Active lists retain persistent ID order across different chunk reload orders", "[world][determinism]") {
    World world;
    const auto left = load(world);
    const auto right = load(world, {{1, 0}, 0});
    take(world.create_actor({17, 1}));
    take(world.create_object({17, 2}, false));
    take(world.create_actor({1, 1}));
    take(world.create_object({1, 2}, true));
    const auto actors = actor_ids(world);
    const auto objects = object_ids(world);
    REQUIRE(actors.size() == 2);
    REQUIRE(objects.size() == 2);
    CHECK(actors[0] < actors[1]);
    CHECK(objects[0] < objects[1]);
    REQUIRE(world.release_request(left));
    world.reconcile_chunks();
    load(world);
    CHECK(actor_ids(world) == actors);
    CHECK(object_ids(world) == objects);
    REQUIRE(world.release_request(right));
    world.reconcile_chunks();
    load(world, {{1, 0}, 0});
    CHECK(actor_ids(world) == actors);
    CHECK(object_ids(world) == objects);
    CHECK(world.check_invariants());
}

TEST_CASE("Full unload preserves moved item roots and never resets entity slot maps", "[world][streaming]") {
    World world;
    const auto request = load(world, {{0, 0}, 1});
    const auto actor = take(world.create_actor({1, 1}));
    const auto object = take(world.create_object({17, 1}));
    const auto bag = take(world.create_item(ItemKind::backpack, ItemLocation::carried(actor)));
    const auto child = take(world.create_item(ItemKind::rock, ItemLocation::inside(bag)));
    const auto actor_id = world.actor(actor)->id;
    const auto object_id = world.object(object)->id;
    const auto bag_id = world.item(bag)->id;
    const auto child_id = world.item(child)->id;
    REQUIRE(world.transfer_item(bag, ItemLocation::stored(object)));
    REQUIRE(world.release_request(request));
    world.reconcile_chunks();
    CHECK(world.loaded_chunks().empty());
    CHECK(world.actor(actor) == nullptr);
    CHECK(world.object(object) == nullptr);
    CHECK(world.item(bag) == nullptr);
    CHECK(world.item(child) == nullptr);
    CHECK(world.check_invariants());
    load(world); // The origin must not restore a tree transferred elsewhere.
    CHECK(world.find_actor(actor_id) != actor);
    REQUIRE(world.actor(world.find_actor(actor_id)) != nullptr);
    CHECK(world.actor(world.find_actor(actor_id))->inventory.empty());
    CHECK(world.find_item(bag_id) == ItemHandle{});
    CHECK(world.find_item(child_id) == ItemHandle{});
    load(world, {{1, 0}, 0});
    const auto fresh_object = world.find_object(object_id);
    const auto fresh_bag = world.find_item(bag_id);
    const auto fresh_child = world.find_item(child_id);
    REQUIRE(world.object(fresh_object) != nullptr);
    REQUIRE(world.item(fresh_bag) != nullptr);
    REQUIRE(world.item(fresh_child) != nullptr);
    CHECK(fresh_object != object);
    CHECK(fresh_bag != bag);
    CHECK(fresh_child != child);
    CHECK(world.item(fresh_bag)->location == ItemLocation::stored(fresh_object));
    CHECK(world.item(fresh_child)->location == ItemLocation::inside(fresh_bag));
    const auto newer = take(world.create_item(ItemKind::bandage, ItemLocation::ground({0, 0})));
    CHECK(world.item(newer)->id > child_id);
    CHECK(world.check_invariants());
}

TEST_CASE("Terrain generation depends only on seed and position and never issues entity IDs", "[world][determinism]") {
    World first(91);
    World second(91);
    const auto remote = load(first, {{10, -10}, 0});
    const auto terrain = first.chunk({10, -10})->tiles;
    load(first);
    load(second);
    load(second, {{10, -10}, 0});
    CHECK(second.chunk({10, -10})->tiles == terrain);
    CHECK(first.active_actors().empty());
    CHECK(first.active_objects().empty());
    take(first.create_actor({0, 0}));
    take(second.create_actor({0, 0}));
    CHECK(first.actor(first.actor_at({0, 0}))->id == 1);
    CHECK(second.actor(second.actor_at({0, 0}))->id == 1);
    REQUIRE(first.release_request(remote));
    first.reconcile_chunks();
    load(first, {{10, -10}, 0});
    CHECK(first.chunk({10, -10})->tiles == terrain);
    CHECK(first.check_invariants());
    CHECK(second.check_invariants());
}

}
