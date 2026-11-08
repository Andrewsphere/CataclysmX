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
    const LoadRequest request = take(world.RequestChunks(region, mode));
    world.ReconcileChunks();
    REQUIRE(world.CheckInvariants());
    return request;
}

Vector<PersistentId> actor_ids(const World& world) {
    Vector<PersistentId> result;
    for (ActorHandle handle : world.ActiveActors()) {
        result.push_back(world.GetActor(handle)->id);
    }
    return result;
}

Vector<PersistentId> object_ids(const World& world) {
    Vector<PersistentId> result;
    for (ObjectHandle handle : world.ActiveObjects()) {
        result.push_back(world.GetObject(handle)->id);
    }
    return result;
}

TEST_CASE("World coordinates use floor division and bounded positions", "[world][coordinates]") {
    CHECK(ChunkOf({0, 0}) == ChunkCoord{0, 0});
    CHECK(ChunkOf({15, 16}) == ChunkCoord{0, 1});
    CHECK(ChunkOf({-1, -16}) == ChunkCoord{-1, -1});
    CHECK(ChunkOf({-17, 31}) == ChunkCoord{-2, 1});
    CHECK(TileIndex({-1, -1}) == 255);
    CHECK(TileIndex({-16, -16}) == 0);
    CHECK(TileIndex({-17, 16}) == 15);
    CHECK(TileIndex({INT_MIN, INT_MAX}) == 240);
    CHECK(ValidPosition({-world_coordinate_limit, world_coordinate_limit}));
    CHECK_FALSE(ValidPosition({world_coordinate_limit + 1, 0}));
    CHECK_FALSE(ValidPosition({0, -world_coordinate_limit - 1}));
    CHECK_FALSE(ValidPosition({INT_MIN, INT_MAX}));
    CHECK(Passable(Terrain::floor));
    CHECK(Passable(Terrain::open_door));
    CHECK_FALSE(Passable(Terrain::wall));
    CHECK_FALSE(Passable(Terrain::closed_door));

    World world;
    load(world, {{-1, -1}, 0});
    REQUIRE(world.SetTile({-1, -1}, {Terrain::open_door, 7}));
    CHECK(world.GetChunk({-1, -1})->tiles[255] == Tile{Terrain::open_door, 7});
    CHECK(world.GetTile({0, 0}) == nullptr);
    CHECK(world.SetTile({0, 0}, {}) == eastl::unexpected(WorldError::chunk_unavailable));
    CHECK(world.CreateActor({INT_MAX, 0}) == eastl::unexpected(WorldError::invalid_position));
    CHECK(world.CreateObject({0, INT_MIN}) == eastl::unexpected(WorldError::invalid_position));
    CHECK(world.CreateItem(ItemKind::rock, ItemLocation::Ground({world_coordinate_limit + 1, 0}))
        == eastl::unexpected(WorldError::invalid_position));

    load(world, {{62'500, -62'500}, 0});
    REQUIRE(world.GetTile({world_coordinate_limit, -world_coordinate_limit}) != nullptr);
    CHECK(world.GetTile({world_coordinate_limit + 1, -world_coordinate_limit}) == nullptr);
    REQUIRE(world.SetTile({world_coordinate_limit, -world_coordinate_limit}, {}));
    take(world.CreateActor({world_coordinate_limit, -world_coordinate_limit}));
    CHECK(world.CheckInvariants());
}

TEST_CASE("Chunk requests combine demand and reject invalid updates atomically", "[world][streaming]") {
    World world;
    CHECK(world.RequestChunks({{0, 0}, -1}, LoadMode::active)
        == eastl::unexpected(WorldError::invalid_region));
    CHECK(world.RequestChunks({{0, 0}, 9}, LoadMode::active)
        == eastl::unexpected(WorldError::invalid_region));
    CHECK(world.RequestChunks({{INT_MAX, INT_MIN}, 1}, LoadMode::active)
        == eastl::unexpected(WorldError::invalid_region));
    CHECK(world.RequestChunks({{62'500, 0}, 1}, LoadMode::resident)
        == eastl::unexpected(WorldError::invalid_region));
    CHECK(world.RequestChunks({{0, 0}, 0}, static_cast<LoadMode>(99))
        == eastl::unexpected(WorldError::invalid_request));
    const LoadRequest resident = take(world.RequestChunks({{0, 0}, 1}, LoadMode::resident));
    const LoadRequest active = take(world.RequestChunks({{0, 0}, 0}, LoadMode::active));
    const LoadRequest overlap = take(world.RequestChunks({{1, 0}, 0}, LoadMode::active));
    CHECK(world.LoadedChunks().empty());
    world.ReconcileChunks();
    REQUIRE(world.LoadedChunks().size() == 9);
    CHECK(world.LoadedChunks().front() == ChunkCoord{-1, -1});
    CHECK(world.LoadedChunks().back() == ChunkCoord{1, 1});
    CHECK(world.IsActive({0, 0}));
    CHECK(world.IsActive({16, 0}));
    CHECK_FALSE(world.IsActive({-1, -1}));
    CHECK(world.UpdateRequest(resident, {{0, 0}, 9}, LoadMode::active)
        == eastl::unexpected(WorldError::invalid_region));
    CHECK(world.UpdateRequest(active, {{0, 0}, 0}, static_cast<LoadMode>(99))
        == eastl::unexpected(WorldError::invalid_request));
    world.ReconcileChunks();
    CHECK(world.LoadedChunks().size() == 9);
    REQUIRE(world.ReleaseRequest(active));
    world.ReconcileChunks();
    CHECK_FALSE(world.IsActive({0, 0}));
    CHECK(world.GetChunk({0, 0}) != nullptr);
    CHECK(world.ReleaseRequest(active) == eastl::unexpected(WorldError::invalid_request));
    CHECK(world.UpdateRequest(active, {{0, 0}, 0}, LoadMode::active)
        == eastl::unexpected(WorldError::invalid_request));
    CHECK(world.ReleaseRequest({}) == eastl::unexpected(WorldError::invalid_request));
    CHECK(world.UpdateRequest({UINT64_MAX}, {{0, 0}, 0}, LoadMode::active)
        == eastl::unexpected(WorldError::invalid_request));
    REQUIRE(world.UpdateRequest(overlap, {{-2, 0}, 0}, LoadMode::resident));
    world.ReconcileChunks();
    CHECK(world.LoadedChunks().size() == 10);
    CHECK(world.LoadedChunks().front() == ChunkCoord{-2, 0});
    CHECK_FALSE(world.IsActive({16, 0}));
    REQUIRE(world.ReleaseRequest(resident));
    world.ReconcileChunks();
    CHECK(world.LoadedChunks() == Vector<ChunkCoord>{{-2, 0}});
    CHECK(world.ArchivedChunkCount() == 9);
    REQUIRE(world.ReleaseRequest(overlap));
    world.ReconcileChunks();
    CHECK(world.LoadedChunks().empty());
    CHECK(world.ArchivedChunkCount() == 10);
    CHECK(world.CheckInvariants());

    // Exercise SlotMap request recycling, not just never-reused indices.
    for (int index = 0; index < 140; ++index) {
        const auto request = take(world.RequestChunks({{0, 0}, 0}, LoadMode::resident));
        CHECK(request != active);
        REQUIRE(world.ReleaseRequest(request));
    }
    CHECK(world.ReleaseRequest(active) == eastl::unexpected(WorldError::invalid_request));
}

TEST_CASE("Only active chunks accrue turns and enabled object updates", "[world][activity]") {
    World world;
    const auto request = load(world, {{0, 0}, 0}, LoadMode::resident);
    const auto actor = take(world.CreateActor({0, 0}, true));
    const auto object = take(world.CreateObject({1, 0}, true));
    CHECK(world.ActiveActors().empty());
    CHECK(world.ActiveObjects().empty());
    CHECK(world.RecordActorTurn(actor) == eastl::unexpected(WorldError::chunk_unavailable));
    CHECK(world.RecordObjectUpdate(object) == eastl::unexpected(WorldError::chunk_unavailable));
    CHECK(world.GetActor(actor)->turns == 0);
    CHECK(world.GetObject(object)->updates == 0);
    REQUIRE(world.UpdateRequest(request, {{0, 0}, 0}, LoadMode::active));
    world.ReconcileChunks();
    CHECK(world.ActiveActors() == Vector<ActorHandle>{actor});
    CHECK(world.ActiveObjects() == Vector<ObjectHandle>{object});
    REQUIRE(world.RecordActorTurn(actor));
    REQUIRE(world.RecordObjectUpdate(object));
    REQUIRE(world.SetObjectEnabled(object, false));
    REQUIRE(world.RecordObjectUpdate(object));
    CHECK(world.GetActor(actor)->turns == 1);
    CHECK(world.GetObject(object)->updates == 1);
    REQUIRE(world.UpdateRequest(request, {{0, 0}, 0}, LoadMode::resident));
    world.ReconcileChunks();
    CHECK(world.RecordObjectUpdate(object) == eastl::unexpected(WorldError::chunk_unavailable));
    CHECK(world.RecordActorTurn({}) == eastl::unexpected(WorldError::invalid_handle));
    CHECK(world.RecordObjectUpdate({UINT64_MAX}) == eastl::unexpected(WorldError::invalid_handle));
    CHECK(world.CheckInvariants());
}

TEST_CASE("Actor movement keeps handles and inventory across chunk boundaries", "[world][actors]") {
    World world;
    const auto origin = load(world);
    load(world, {{1, 0}, 0}, LoadMode::resident);
    const auto actor = take(world.CreateActor({15, 1}, true));
    const auto backpack = take(world.CreateItem(ItemKind::backpack, ItemLocation::Carried(actor)));
    const auto rock = take(world.CreateItem(ItemKind::rock, ItemLocation::Inside(backpack)));
    const PersistentId id = world.GetActor(actor)->id;
    REQUIRE(world.MoveActor(actor, {16, 1}));
    CHECK(world.GetActor(actor)->id == id);
    CHECK(world.ActorAt({15, 1}) == ActorHandle{});
    CHECK(world.ActorAt({16, 1}) == actor);
    CHECK(world.GetChunk({0, 0})->actors.empty());
    CHECK(world.GetChunk({1, 0})->actors == Vector<ActorHandle>{actor});
    CHECK(world.GetItem(backpack)->location == ItemLocation::Carried(actor));
    CHECK(world.GetItem(rock)->location == ItemLocation::Inside(backpack));
    CHECK(world.ActiveActors().empty());
    REQUIRE(world.MoveActor(actor, {16, 1}));
    const auto other = take(world.CreateActor({17, 1}));
    CHECK(world.MoveActor(actor, {17, 1}) == eastl::unexpected(WorldError::occupied));
    CHECK(world.CreateActor({17, 1}) == eastl::unexpected(WorldError::occupied));
    REQUIRE(world.SetTile({18, 1}, {Terrain::closed_door, 0}));
    CHECK(world.MoveActor(actor, {18, 1}) == eastl::unexpected(WorldError::blocked));
    CHECK(world.MoveActor(actor, {32, 1}) == eastl::unexpected(WorldError::chunk_unavailable));
    CHECK(world.MoveActor(actor, {INT_MAX, 1}) == eastl::unexpected(WorldError::invalid_position));
    CHECK(world.MoveActor({}, {16, 1}) == eastl::unexpected(WorldError::invalid_handle));
    CHECK(world.GetActor(actor)->position == TileCoord{16, 1});
    CHECK(world.GetActor(other)->position == TileCoord{17, 1});
    REQUIRE(world.ReleaseRequest(origin));
    world.ReconcileChunks();
    REQUIRE(world.GetActor(actor) != nullptr);
    REQUIRE(world.GetItem(rock) != nullptr);
    CHECK(world.FindActor(id) == actor);
    CHECK(world.CheckInvariants());
}

TEST_CASE("Item transfers validate destinations and cycles before changing ownership", "[world][items]") {
    World world;
    load(world);
    load(world, {{1, 0}, 0}, LoadMode::resident);
    const auto actor = take(world.CreateActor({1, 1}));
    const auto object = take(world.CreateObject({2, 1}));
    const auto root = take(world.CreateItem(ItemKind::backpack, ItemLocation::Ground({0, 0})));
    const auto nested = take(world.CreateItem(ItemKind::backpack, ItemLocation::Inside(root)));
    const auto rock = take(world.CreateItem(ItemKind::rock, ItemLocation::Inside(nested)));
    CHECK(world.TransferItem(root, ItemLocation::Inside(root))
        == eastl::unexpected(WorldError::containment_cycle));
    CHECK(world.TransferItem(root, ItemLocation::Inside(nested))
        == eastl::unexpected(WorldError::containment_cycle));
    CHECK(world.TransferItem(nested, ItemLocation::Inside(rock))
        == eastl::unexpected(WorldError::not_container));
    CHECK(world.CreateItem(ItemKind::bandage, ItemLocation::Inside(rock))
        == eastl::unexpected(WorldError::not_container));
    CHECK(world.TransferItem(root, ItemLocation::Carried({UINT64_MAX}))
        == eastl::unexpected(WorldError::invalid_handle));
    CHECK(world.TransferItem(root, ItemLocation::Stored({}))
        == eastl::unexpected(WorldError::invalid_handle));
    CHECK(world.TransferItem(root, ItemLocation::Inside({}))
        == eastl::unexpected(WorldError::invalid_handle));
    CHECK(world.TransferItem(root, ItemLocation::Ground({32, 0}))
        == eastl::unexpected(WorldError::chunk_unavailable));
    CHECK(world.TransferItem(root, ItemLocation::Ground({INT_MIN, 0}))
        == eastl::unexpected(WorldError::invalid_position));
    CHECK(world.TransferItem({}, ItemLocation::Carried(actor))
        == eastl::unexpected(WorldError::invalid_handle));
    CHECK(world.ItemsAt({0, 0}) == Vector<ItemHandle>{root});
    CHECK(world.GetItem(root)->contents == Vector<ItemHandle>{nested});
    CHECK(world.GetItem(nested)->contents == Vector<ItemHandle>{rock});
    CHECK(world.GetItem(root)->location == ItemLocation::Ground({0, 0}));
    CHECK(world.GetActor(actor)->inventory.empty());
    CHECK(world.GetObject(object)->contents.empty());
    REQUIRE(world.TransferItem(root, ItemLocation::Carried(actor)));
    CHECK(world.ItemsAt({0, 0}).empty());
    CHECK(world.GetActor(actor)->inventory == Vector<ItemHandle>{root});
    REQUIRE(world.TransferItem(root, ItemLocation::Carried(actor)));
    CHECK(world.GetActor(actor)->inventory.size() == 1);
    REQUIRE(world.TransferItem(root, ItemLocation::Stored(object)));
    CHECK(world.GetActor(actor)->inventory.empty());
    CHECK(world.GetObject(object)->contents == Vector<ItemHandle>{root});
    REQUIRE(world.TransferItem(root, ItemLocation::Ground({16, 2})));
    CHECK(world.GetObject(object)->contents.empty());
    CHECK(world.GetChunk({0, 0})->ground_items.empty());
    CHECK(world.GetChunk({1, 0})->ground_items == Vector<ItemHandle>{root});
    CHECK(world.GetItem(rock)->location == ItemLocation::Inside(nested));
    REQUIRE(world.TransferItem(rock, ItemLocation::Carried(actor)));
    CHECK(world.GetItem(nested)->contents.empty());
    CHECK(world.GetActor(actor)->inventory == Vector<ItemHandle>{rock});
    CHECK(world.CheckInvariants());
}

TEST_CASE("Chunk archives preserve values and nested ownership with fresh runtime keys", "[world][streaming][items]") {
    World world(123);
    const auto origin = load(world);
    load(world, {{2, 0}, 0}, LoadMode::resident);
    const auto actor = take(world.CreateActor({1, 1}, true));
    const auto object = take(world.CreateObject({2, 2}, true));
    const auto bag = take(world.CreateItem(ItemKind::backpack, ItemLocation::Carried(actor)));
    const auto nested = take(world.CreateItem(ItemKind::backpack, ItemLocation::Inside(bag)));
    const auto rock = take(world.CreateItem(ItemKind::rock, ItemLocation::Inside(nested)));
    const auto bandage = take(world.CreateItem(ItemKind::bandage, ItemLocation::Inside(bag)));
    const auto carried = take(world.CreateItem(ItemKind::rock, ItemLocation::Carried(actor)));
    const auto stored = take(world.CreateItem(ItemKind::backpack, ItemLocation::Stored(object)));
    const auto stored_child = take(world.CreateItem(ItemKind::bandage, ItemLocation::Inside(stored)));
    const auto ground = take(world.CreateItem(ItemKind::backpack, ItemLocation::Ground({0, 4})));
    const auto ground_child = take(world.CreateItem(ItemKind::rock, ItemLocation::Inside(ground)));
    // Reorder siblings so reload cannot accidentally rely on ID or slot order.
    REQUIRE(world.TransferItem(nested, ItemLocation::Ground({0, 0})));
    REQUIRE(world.TransferItem(nested, ItemLocation::Inside(bag)));
    const PersistentId actor_id = world.GetActor(actor)->id;
    const PersistentId object_id = world.GetObject(object)->id;
    const Vector<ItemHandle> old_items{bag, nested, rock, bandage, carried, stored,
        stored_child, ground, ground_child};
    Vector<PersistentId> item_ids;
    for (const auto handle : old_items) {
        item_ids.push_back(world.GetItem(handle)->id);
    }
    REQUIRE(world.RecordActorTurn(actor));
    REQUIRE(world.RecordActorTurn(actor));
    REQUIRE(world.RecordObjectUpdate(object));
    REQUIRE(world.SetObjectEnabled(object, false));
    REQUIRE(world.SetTile({3, 3}, {Terrain::closed_door, 19}));
    CHECK(world.CheckInvariants());
    REQUIRE(world.ReleaseRequest(origin));
    world.ReconcileChunks();
    CHECK(world.ArchivedChunkCount() == 1);
    CHECK(world.GetActor(actor) == nullptr);
    CHECK(world.GetObject(object) == nullptr);
    CHECK(world.FindActor(actor_id) == ActorHandle{});
    CHECK(world.FindObject(object_id) == ObjectHandle{});
    for (std::size_t index = 0; index < old_items.size(); ++index) {
        CHECK(world.GetItem(old_items[index]) == nullptr);
        CHECK(world.FindItem(item_ids[index]) == ItemHandle{});
    }
    CHECK(world.DestroyActor(actor) == eastl::unexpected(WorldError::invalid_handle));
    CHECK(world.DestroyObject(object) == eastl::unexpected(WorldError::invalid_handle));
    CHECK(world.DestroyItem(bag) == eastl::unexpected(WorldError::invalid_handle));
    CHECK(world.CheckInvariants());
    const auto newer = take(world.CreateItem(ItemKind::rock, ItemLocation::Ground({32, 0})));
    CHECK(world.GetItem(newer)->id > item_ids.back());
    CHECK(world.TransferItem(newer, ItemLocation::Carried(actor))
        == eastl::unexpected(WorldError::invalid_handle));
    CHECK(world.TransferItem(newer, ItemLocation::Stored(object))
        == eastl::unexpected(WorldError::invalid_handle));
    CHECK(world.TransferItem(newer, ItemLocation::Inside(bag))
        == eastl::unexpected(WorldError::invalid_handle));
    CHECK(world.TransferItem(newer, ItemLocation::Ground({0, 0}))
        == eastl::unexpected(WorldError::chunk_unavailable));
    CHECK(world.GetItem(newer)->location == ItemLocation::Ground({32, 0}));
    const auto reload = load(world);
    CHECK(world.ArchivedChunkCount() == 0);
    const auto fresh_actor = world.FindActor(actor_id);
    const auto fresh_object = world.FindObject(object_id);
    REQUIRE(world.GetActor(fresh_actor) != nullptr);
    REQUIRE(world.GetObject(fresh_object) != nullptr);
    CHECK(fresh_actor != actor);
    CHECK(fresh_object != object);
    CHECK(world.GetActor(fresh_actor)->player);
    CHECK(world.GetActor(fresh_actor)->turns == 2);
    CHECK(world.GetActor(fresh_actor)->position == TileCoord{1, 1});
    CHECK_FALSE(world.GetObject(fresh_object)->enabled);
    CHECK(world.GetObject(fresh_object)->updates == 1);
    CHECK(*world.GetTile({3, 3}) == Tile{Terrain::closed_door, 19});
    Vector<ItemHandle> fresh;
    for (std::size_t index = 0; index < old_items.size(); ++index) {
        const auto handle = world.FindItem(item_ids[index]);
        REQUIRE(world.GetItem(handle) != nullptr);
        CHECK(handle != old_items[index]);
        CHECK(world.GetItem(handle)->id == item_ids[index]);
        CHECK(world.GetItem(old_items[index]) == nullptr);
        fresh.push_back(handle);
    }
    CHECK(world.GetActor(fresh_actor)->inventory == Vector<ItemHandle>{fresh[0], fresh[4]});
    CHECK(world.GetItem(fresh[0])->location == ItemLocation::Carried(fresh_actor));
    CHECK(world.GetItem(fresh[0])->contents == Vector<ItemHandle>{fresh[3], fresh[1]});
    CHECK(world.GetItem(fresh[1])->location == ItemLocation::Inside(fresh[0]));
    CHECK(world.GetItem(fresh[1])->contents == Vector<ItemHandle>{fresh[2]});
    CHECK(world.GetItem(fresh[2])->kind == ItemKind::rock);
    CHECK(world.GetItem(fresh[2])->location == ItemLocation::Inside(fresh[1]));
    CHECK(world.GetItem(fresh[3])->kind == ItemKind::bandage);
    CHECK(world.GetObject(fresh_object)->contents == Vector<ItemHandle>{fresh[5]});
    CHECK(world.GetItem(fresh[5])->location == ItemLocation::Stored(fresh_object));
    CHECK(world.GetItem(fresh[5])->contents == Vector<ItemHandle>{fresh[6]});
    CHECK(world.GetItem(fresh[6])->location == ItemLocation::Inside(fresh[5]));
    CHECK(world.ItemsAt({0, 4}) == Vector<ItemHandle>{fresh[7]});
    CHECK(world.GetItem(fresh[7])->contents == Vector<ItemHandle>{fresh[8]});
    CHECK(world.GetItem(fresh[8])->location == ItemLocation::Inside(fresh[7]));
    CHECK(world.CheckInvariants());
    // A second archive replaces the first value snapshot, including mutations.
    REQUIRE(world.RecordActorTurn(fresh_actor));
    REQUIRE(world.SetObjectEnabled(fresh_object, true));
    REQUIRE(world.RecordObjectUpdate(fresh_object));
    REQUIRE(world.TransferItem(fresh[2], ItemLocation::Stored(fresh_object)));
    REQUIRE(world.ReleaseRequest(reload));
    world.ReconcileChunks();
    load(world);
    CHECK(world.GetActor(world.FindActor(actor_id))->turns == 3);
    CHECK(world.GetObject(world.FindObject(object_id))->updates == 2);
    CHECK(world.GetObject(world.FindObject(object_id))->enabled);
    CHECK(world.GetItem(world.FindItem(item_ids[2]))->location
        == ItemLocation::Stored(world.FindObject(object_id)));
    CHECK(world.CheckInvariants());
}

TEST_CASE("Actor and object destruction drop trees while item destruction removes descendants", "[world][destruction]") {
    World world;
    const auto request = load(world);
    const auto actor = take(world.CreateActor({1, 1}));
    const auto object = take(world.CreateObject({2, 2}));
    const auto bag = take(world.CreateItem(ItemKind::backpack, ItemLocation::Carried(actor)));
    const auto child = take(world.CreateItem(ItemKind::backpack, ItemLocation::Inside(bag)));
    const auto grandchild = take(world.CreateItem(ItemKind::bandage, ItemLocation::Inside(child)));
    const auto loose = take(world.CreateItem(ItemKind::rock, ItemLocation::Carried(actor)));
    const auto stored = take(world.CreateItem(ItemKind::backpack, ItemLocation::Stored(object)));
    const auto stored_child = take(world.CreateItem(ItemKind::rock, ItemLocation::Inside(stored)));
    const auto id = world.GetItem(grandchild)->id;
    REQUIRE(world.DestroyActor(actor));
    CHECK(world.ActorAt({1, 1}) == ActorHandle{});
    CHECK(world.ItemsAt({1, 1}) == Vector<ItemHandle>{bag, loose});
    CHECK(world.GetItem(bag)->location == ItemLocation::Ground({1, 1}));
    CHECK(world.GetItem(child)->location == ItemLocation::Inside(bag));
    CHECK(world.GetItem(grandchild)->location == ItemLocation::Inside(child));
    REQUIRE(world.DestroyObject(object));
    CHECK(world.ObjectAt({2, 2}) == ObjectHandle{});
    CHECK(world.GetItem(stored)->location == ItemLocation::Ground({2, 2}));
    CHECK(world.GetItem(stored_child)->location == ItemLocation::Inside(stored));
    REQUIRE(world.DestroyItem(child));
    CHECK(world.GetItem(child) == nullptr);
    CHECK(world.GetItem(grandchild) == nullptr);
    CHECK(world.FindItem(id) == ItemHandle{});
    CHECK(world.GetItem(bag)->contents.empty());
    REQUIRE(world.DestroyItem(stored));
    CHECK(world.GetItem(stored_child) == nullptr);
    CHECK(world.ItemsAt({2, 2}).empty());
    CHECK(world.DestroyItem(child) == eastl::unexpected(WorldError::invalid_handle));
    CHECK(world.DestroyActor(actor) == eastl::unexpected(WorldError::invalid_handle));
    CHECK(world.DestroyObject(object) == eastl::unexpected(WorldError::invalid_handle));
    CHECK(world.SetObjectEnabled(object, true) == eastl::unexpected(WorldError::invalid_handle));
    CHECK(world.CheckInvariants());
    REQUIRE(world.ReleaseRequest(request));
    world.ReconcileChunks();
    load(world);
    CHECK(world.FindItem(id) == ItemHandle{});
    CHECK(world.CheckInvariants());
}

TEST_CASE("Active lists retain persistent ID order across different chunk reload orders", "[world][determinism]") {
    World world;
    const auto left = load(world);
    const auto right = load(world, {{1, 0}, 0});
    take(world.CreateActor({17, 1}));
    take(world.CreateObject({17, 2}, false));
    take(world.CreateActor({1, 1}));
    take(world.CreateObject({1, 2}, true));
    const auto actors = actor_ids(world);
    const auto objects = object_ids(world);
    REQUIRE(actors.size() == 2);
    REQUIRE(objects.size() == 2);
    CHECK(actors[0] < actors[1]);
    CHECK(objects[0] < objects[1]);
    REQUIRE(world.ReleaseRequest(left));
    world.ReconcileChunks();
    load(world);
    CHECK(actor_ids(world) == actors);
    CHECK(object_ids(world) == objects);
    REQUIRE(world.ReleaseRequest(right));
    world.ReconcileChunks();
    load(world, {{1, 0}, 0});
    CHECK(actor_ids(world) == actors);
    CHECK(object_ids(world) == objects);
    CHECK(world.CheckInvariants());
}

TEST_CASE("Full unload preserves moved item roots and never resets entity slot maps", "[world][streaming]") {
    World world;
    const auto request = load(world, {{0, 0}, 1});
    const auto actor = take(world.CreateActor({1, 1}));
    const auto object = take(world.CreateObject({17, 1}));
    const auto bag = take(world.CreateItem(ItemKind::backpack, ItemLocation::Carried(actor)));
    const auto child = take(world.CreateItem(ItemKind::rock, ItemLocation::Inside(bag)));
    const auto actor_id = world.GetActor(actor)->id;
    const auto object_id = world.GetObject(object)->id;
    const auto bag_id = world.GetItem(bag)->id;
    const auto child_id = world.GetItem(child)->id;
    REQUIRE(world.TransferItem(bag, ItemLocation::Stored(object)));
    REQUIRE(world.ReleaseRequest(request));
    world.ReconcileChunks();
    CHECK(world.LoadedChunks().empty());
    CHECK(world.GetActor(actor) == nullptr);
    CHECK(world.GetObject(object) == nullptr);
    CHECK(world.GetItem(bag) == nullptr);
    CHECK(world.GetItem(child) == nullptr);
    CHECK(world.CheckInvariants());
    load(world); // The origin must not restore a tree transferred elsewhere.
    CHECK(world.FindActor(actor_id) != actor);
    REQUIRE(world.GetActor(world.FindActor(actor_id)) != nullptr);
    CHECK(world.GetActor(world.FindActor(actor_id))->inventory.empty());
    CHECK(world.FindItem(bag_id) == ItemHandle{});
    CHECK(world.FindItem(child_id) == ItemHandle{});
    load(world, {{1, 0}, 0});
    const auto fresh_object = world.FindObject(object_id);
    const auto fresh_bag = world.FindItem(bag_id);
    const auto fresh_child = world.FindItem(child_id);
    REQUIRE(world.GetObject(fresh_object) != nullptr);
    REQUIRE(world.GetItem(fresh_bag) != nullptr);
    REQUIRE(world.GetItem(fresh_child) != nullptr);
    CHECK(fresh_object != object);
    CHECK(fresh_bag != bag);
    CHECK(fresh_child != child);
    CHECK(world.GetItem(fresh_bag)->location == ItemLocation::Stored(fresh_object));
    CHECK(world.GetItem(fresh_child)->location == ItemLocation::Inside(fresh_bag));
    const auto newer = take(world.CreateItem(ItemKind::bandage, ItemLocation::Ground({0, 0})));
    CHECK(world.GetItem(newer)->id > child_id);
    CHECK(world.CheckInvariants());
}

TEST_CASE("Terrain generation depends only on seed and position and never issues entity IDs", "[world][determinism]") {
    World first(91);
    World second(91);
    const auto remote = load(first, {{10, -10}, 0});
    const auto terrain = first.GetChunk({10, -10})->tiles;
    load(first);
    load(second);
    load(second, {{10, -10}, 0});
    CHECK(second.GetChunk({10, -10})->tiles == terrain);
    CHECK(first.ActiveActors().empty());
    CHECK(first.ActiveObjects().empty());
    take(first.CreateActor({0, 0}));
    take(second.CreateActor({0, 0}));
    CHECK(first.GetActor(first.ActorAt({0, 0}))->id == 1);
    CHECK(second.GetActor(second.ActorAt({0, 0}))->id == 1);
    REQUIRE(first.ReleaseRequest(remote));
    first.ReconcileChunks();
    load(first, {{10, -10}, 0});
    CHECK(first.GetChunk({10, -10})->tiles == terrain);
    CHECK(first.CheckInvariants());
    CHECK(second.CheckInvariants());
}

}
