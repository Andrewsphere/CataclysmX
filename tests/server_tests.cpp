#include "core/server.h"
#include "core/simulation.h"

#include <catch2/catch_test_macros.hpp>

#include <EASTL/sort.h>
#include <EASTL/utility.h>

#include <climits>

namespace {

using namespace cataclysmx;

template<class T>
T take(WorldResult<T> result) {
    REQUIRE(result.has_value());
    return eastl::move(*result);
}

LoadRequest load(World& world, ChunkRegion region, LoadMode mode = LoadMode::active) {
    const LoadRequest request = take(world.request_chunks(region, mode));
    world.reconcile_chunks();
    return request;
}

const ViewCell& cell_at(const WorldView& view, TileCoord position) {
    const int x = position.x - view.origin.x;
    const int z = position.z - view.origin.z;
    REQUIRE(x >= 0);
    REQUIRE(x < view.width);
    REQUIRE(z >= 0);
    REQUIRE(z < view.height);
    return view.cells[static_cast<std::size_t>(z * view.width + x)];
}

void expect(Server& server, Command command, Outcome outcome, bool advances) {
    const std::uint64_t before = server.tick();
    const CommandResult result = server.send(command);
    CHECK(result.outcome == outcome);
    CHECK(result.advanced == advances);
    CHECK(result.tick == before + (advances ? 1 : 0));
    CHECK(server.tick() == result.tick);
    CHECK(server.world().actor(server.player())->turns == result.tick);
    CHECK(server.world().check_invariants());
}

struct ActorState {
    PersistentId id;
    TileCoord position;
    std::uint64_t turns;
    bool operator==(const ActorState&) const = default;
};

Vector<ActorState> actors(const World& world) {
    Vector<ActorState> result;
    for (ChunkCoord position : world.loaded_chunks()) {
        for (ActorHandle handle : world.chunk(position)->actors) {
            const Actor* actor = world.actor(handle);
            result.push_back({actor->id, actor->position, actor->turns});
        }
    }
    eastl::sort(result.begin(), result.end(), [](const ActorState& a, const ActorState& b) {
        return a.id < b.id;
    });
    return result;
}

void same_view(const WorldView& a, const WorldView& b) {
    CHECK(a.origin == b.origin);
    CHECK(a.width == b.width);
    CHECK(a.height == b.height);
    CHECK(a.player_position == b.player_position);
    CHECK(a.tick == b.tick);
    CHECK(a.cells == b.cells);
    CHECK(a.inventory == b.inventory);
}

TEST_CASE("Server creates the deterministic demo and advances only successful commands", "[server][commands]") {
    Server server(73);
    World& world = server.world();
    REQUIRE(world.actor(server.player()) != nullptr);
    CHECK(world.actor(server.player())->position == TileCoord{0, 0});
    CHECK(world.actor(server.player())->player);
    CHECK(server.tick() == 0);
    CHECK(world.loaded_chunks().size() == 25);
    CHECK(world.active({-16, -16}));
    CHECK_FALSE(world.active({-32, -32}));
    const auto backpack = world.items_at({0, 0});
    REQUIRE(backpack.size() == 1);
    CHECK(world.item(backpack[0])->kind == ItemKind::backpack);
    REQUIRE(world.item(backpack[0])->contents.size() == 1);
    CHECK(world.item(world.item(backpack[0])->contents[0])->kind == ItemKind::bandage);
    REQUIRE(world.items_at({1, 0}).size() == 1);
    CHECK(world.item(world.items_at({1, 0})[0])->kind == ItemKind::rock);
    const ObjectHandle object = world.object_at({2, 0});
    REQUIRE(world.object(object) != nullptr);
    CHECK_FALSE(world.object(object)->enabled);
    CHECK(world.tile({3, 0})->terrain == Terrain::closed_door);
    CHECK(world.tile({3, 1})->terrain == Terrain::wall);
    const ActorHandle npc = world.actor_at({5, 5});
    REQUIRE(world.actor(npc) != nullptr);
    CHECK_FALSE(world.actor(npc)->player);
    // Keep command outcome checks independent of the NPC's chosen route.
    for (TileCoord position : {TileCoord{4, 5}, TileCoord{6, 5}, TileCoord{5, 4}, TileCoord{5, 6}}) {
        REQUIRE(world.set_tile(position, {Terrain::wall, 0}));
    }

    expect(server, {CommandKind::move, 0, 0}, Outcome::invalid_command, false);
    expect(server, {CommandKind::move, 1, 1}, Outcome::invalid_command, false);
    expect(server, {CommandKind::move, INT_MIN, INT_MAX}, Outcome::invalid_command, false);
    expect(server, {CommandKind::interact, 0, 0}, Outcome::invalid_command, false);
    expect(server, {CommandKind::interact, INT_MAX, INT_MIN}, Outcome::invalid_command, false);
    expect(server, {static_cast<CommandKind>(999)}, Outcome::invalid_command, false);
    expect(server, {CommandKind::drop}, Outcome::not_carried, false);
    expect(server, {CommandKind::pick_up, 0, 0, UINT64_MAX}, Outcome::nothing_here, false);
    expect(server, {CommandKind::interact, -1, 0}, Outcome::nothing_here, false);
    CHECK(world.actor(npc)->turns == 0);

    // Fields irrelevant to the selected command are deliberately ignored.
    expect(server, {CommandKind::wait, INT_MIN, INT_MAX, UINT64_MAX}, Outcome::waited, true);
    expect(server, {CommandKind::pick_up, INT_MAX, INT_MIN}, Outcome::picked_up, true);
    expect(server, {CommandKind::drop, INT_MIN, INT_MAX}, Outcome::dropped, true);
    expect(server, {CommandKind::move, 1, 0, UINT64_MAX}, Outcome::moved, true);
    expect(server, {CommandKind::interact, 1, 0, UINT64_MAX}, Outcome::toggled, true);
    CHECK(world.object(object)->enabled);
    CHECK(world.object(object)->updates == 1);
    expect(server, {CommandKind::interact, 1, 0}, Outcome::toggled, true);
    CHECK_FALSE(world.object(object)->enabled);
    CHECK(world.object(object)->updates == 1);
    expect(server, {CommandKind::move, 1, 0}, Outcome::moved, true);
    expect(server, {CommandKind::move, 1, 0}, Outcome::blocked, false);
    expect(server, {CommandKind::interact, 1, 0}, Outcome::opened, true);
    expect(server, {CommandKind::interact, 1, 0}, Outcome::closed, true);
    expect(server, {CommandKind::interact, 1, 0}, Outcome::opened, true);
    expect(server, {CommandKind::move, 1, 0}, Outcome::moved, true);
    expect(server, {CommandKind::move, 0, 1}, Outcome::blocked, false);
    expect(server, {CommandKind::interact, 0, 1}, Outcome::nothing_here, false);
    CHECK(world.actor(npc)->turns == server.tick());
}

TEST_CASE("Failures do not consume RNG and occupied open doors cannot close", "[server][commands][determinism]") {
    Server subject(918);
    Server reference(918);
    const auto blocker = take(subject.world().create_actor({-1, 0}));
    const auto other = take(reference.world().create_actor({-1, 0}));
    expect(subject, {CommandKind::move, -1, 0}, Outcome::occupied, false);
    REQUIRE(subject.world().destroy_actor(blocker));
    REQUIRE(reference.world().destroy_actor(other));
    REQUIRE(subject.world().set_tile({1, 0}, {Terrain::closed_door, 9}));
    REQUIRE(reference.world().set_tile({1, 0}, {Terrain::closed_door, 9}));
    expect(subject, {CommandKind::move, 1, 0}, Outcome::blocked, false);
    expect(subject, {CommandKind::drop, 0, 0, UINT64_MAX}, Outcome::not_carried, false);
    expect(subject, {static_cast<CommandKind>(-1)}, Outcome::invalid_command, false);
    CHECK(actors(subject.world()) == actors(reference.world()));
    same_view(subject.view(), reference.view());
    for (int turn = 0; turn < 24; ++turn) {
        expect(subject, {}, Outcome::waited, true);
        expect(reference, {}, Outcome::waited, true);
        CHECK(actors(subject.world()) == actors(reference.world()));
    }

    Server door;
    REQUIRE(door.world().set_tile({1, 0}, {Terrain::open_door, 17}));
    take(door.world().create_actor({1, 0}));
    expect(door, {CommandKind::interact, 1, 0}, Outcome::occupied, false);
    CHECK(*door.world().tile({1, 0}) == Tile{Terrain::open_door, 17});
}

TEST_CASE("Pickup and drop use direct ownership and persistent IDs while preserving nested items", "[server][inventory]") {
    Server server;
    World& world = server.world();
    const ItemHandle bag = world.items_at({0, 0})[0];
    const ItemHandle bandage = world.item(bag)->contents[0];
    const PersistentId bag_id = world.item(bag)->id;
    const PersistentId bandage_id = world.item(bandage)->id;
    const ItemHandle rock = world.items_at({1, 0})[0];
    const PersistentId rock_id = world.item(rock)->id;
    const ItemHandle loose = take(world.create_item(ItemKind::rock, ItemLocation::ground({0, 0})));
    const PersistentId loose_id = world.item(loose)->id;
    // Collection order differs from persistent-ID order.
    REQUIRE(world.transfer_item(bag, ItemLocation::ground({0, 1})));
    REQUIRE(world.transfer_item(bag, ItemLocation::ground({0, 0})));

    expect(server, {CommandKind::pick_up, 0, 0, rock_id}, Outcome::nothing_here, false);
    expect(server, {CommandKind::pick_up, 0, 0, bandage_id}, Outcome::nothing_here, false);
    expect(server, {CommandKind::pick_up}, Outcome::picked_up, true);
    CHECK(world.item(bag)->location == ItemLocation::carried(server.player()));
    CHECK(world.item(bandage)->location == ItemLocation::inside(bag));
    expect(server, {CommandKind::drop, 0, 0, bandage_id}, Outcome::not_carried, false);
    expect(server, {CommandKind::pick_up, 0, 0, loose_id}, Outcome::picked_up, true);
    REQUIRE(world.transfer_item(bag, ItemLocation::ground({0, 0})));
    REQUIRE(world.transfer_item(bag, ItemLocation::carried(server.player())));
    const WorldView inventory = server.view();
    REQUIRE(inventory.inventory.size() == 2);
    CHECK(inventory.inventory[0] == InventoryEntry{bag_id, ItemKind::backpack, 1});
    CHECK(inventory.inventory[1] == InventoryEntry{loose_id, ItemKind::rock, 0});
    expect(server, {CommandKind::drop}, Outcome::dropped, true);
    CHECK(world.item(bag)->location == ItemLocation::ground({0, 0}));
    CHECK(world.item(bandage)->id == bandage_id);
    CHECK(world.item(bandage)->location == ItemLocation::inside(bag));
    expect(server, {CommandKind::drop, 0, 0, bag_id}, Outcome::not_carried, false);
    expect(server, {CommandKind::drop, 0, 0, loose_id}, Outcome::dropped, true);
    expect(server, {CommandKind::pick_up, 0, 0, bag_id}, Outcome::picked_up, true);
    CHECK(world.find_item(bag_id) == bag);
    CHECK(world.item(bag)->contents == Vector<ItemHandle>{bandage});
    CHECK(world.check_invariants());
}

TEST_CASE("Views are isolated value snapshots with bounded circular LOS and empty hidden cells", "[server][view]") {
    Server server;
    World& world = server.world();
    REQUIRE(world.set_tile({1, 0}, {Terrain::wall, 23}));
    REQUIRE(world.set_tile({2, 0}, {Terrain::floor, 71}));
    const auto hidden_actor = take(world.create_actor({2, 0}));
    take(world.create_item(ItemKind::rock, ItemLocation::ground({2, 0})));
    const auto object = world.object_at({2, 0});
    REQUIRE(world.set_object_enabled(object, true));
    REQUIRE(world.set_tile({0, 1}, {Terrain::closed_door, 8}));
    REQUIRE(world.set_tile({0, 2}, {Terrain::open_door, 93}));
    const auto before_observation = actors(world);
    const auto chunks_before_observation = world.loaded_chunks();
    const auto tick_before_observation = server.tick();
    const WorldView old = server.view(4);
    server.view(12);
    CHECK(actors(world) == before_observation);
    CHECK(world.loaded_chunks() == chunks_before_observation);
    CHECK(server.tick() == tick_before_observation);
    CHECK(old.origin == TileCoord{-4, -4});
    CHECK(old.width == 9);
    CHECK(old.height == 9);
    CHECK(old.cells.size() == 81);
    CHECK(cell_at(old, {0, 0}).player);
    CHECK(cell_at(old, {0, 0}).actor == world.actor(server.player())->id);
    CHECK(cell_at(old, {1, 0}).visible);
    CHECK(cell_at(old, {1, 0}).tile == Tile{Terrain::wall, 23});
    CHECK(cell_at(old, {2, 0}) == ViewCell{});
    CHECK(cell_at(old, {0, 1}).visible);
    CHECK(cell_at(old, {0, 1}).tile.terrain == Terrain::closed_door);
    CHECK(cell_at(old, {0, 2}) == ViewCell{});
    CHECK(cell_at(old, {-4, -4}) == ViewCell{});

    REQUIRE(world.set_tile({1, 0}, {Terrain::open_door, 12}));
    const WorldView revealed = server.view(4);
    CHECK(cell_at(revealed, {2, 0}).visible);
    CHECK(cell_at(revealed, {2, 0}).actor == world.actor(hidden_actor)->id);
    CHECK(cell_at(revealed, {2, 0}).ground_items == 1);
    CHECK(cell_at(revealed, {2, 0}).object == world.object(object)->id);
    CHECK(cell_at(revealed, {2, 0}).object_enabled);
    CHECK(cell_at(revealed, {2, 0}).tile == Tile{Terrain::floor, 71});
    CHECK(cell_at(old, {2, 0}) == ViewCell{});
    CHECK(cell_at(old, {1, 0}).tile == Tile{Terrain::wall, 23});

    expect(server, {CommandKind::pick_up}, Outcome::picked_up, true);
    const WorldView carrying = server.view(0);
    REQUIRE(carrying.inventory.size() == 1);
    expect(server, {CommandKind::drop}, Outcome::dropped, true);
    CHECK(carrying.inventory.size() == 1);
    CHECK(carrying.tick + 1 == server.tick());
    CHECK(old.inventory.empty());
    CHECK(server.view(INT_MIN).width == 1);
    CHECK(server.view(INT_MAX).width == 25);
    CHECK(server.view(INT_MAX).cells.size() == 625);

    REQUIRE(server.set_bubble_radii(0, 0));
    const WorldView unavailable = server.view(4);
    CHECK(cell_at(unavailable, {-1, 0}) == ViewCell{});
    CHECK(cell_at(unavailable, {0, -1}) == ViewCell{});
    CHECK(world.chunk({-1, 0}) == nullptr);
    CHECK(world.check_invariants());
}

TEST_CASE("Bubble radii validate atomically and resident force loads never simulate", "[server][streaming]") {
    Server server;
    World& world = server.world();
    const ActorHandle player = server.player();
    const auto before = world.loaded_chunks();
    CHECK(server.set_bubble_radii(-1, 2) == eastl::unexpected(WorldError::invalid_region));
    CHECK(server.set_bubble_radii(2, 1) == eastl::unexpected(WorldError::invalid_region));
    CHECK(server.set_bubble_radii(0, 9) == eastl::unexpected(WorldError::invalid_region));
    CHECK(server.set_bubble_radii(INT_MAX, INT_MIN) == eastl::unexpected(WorldError::invalid_region));
    CHECK(world.loaded_chunks() == before);
    CHECK(world.active({16, 0}));
    CHECK_FALSE(world.active({32, 0}));
    CHECK(server.tick() == 0);
    REQUIRE(server.set_bubble_radii(0, 1));
    CHECK(server.player() == player);
    CHECK(world.actor(player) != nullptr);
    REQUIRE(world.set_tile({17, 1}, {}));
    const ActorHandle npc = take(world.create_actor({17, 1}));
    const ObjectHandle object = take(world.create_object({18, 1}, true));
    expect(server, {}, Outcome::waited, true);
    CHECK(world.actor(npc)->turns == 0);
    CHECK(world.object(object)->updates == 0);
    const auto resident = load(world, {{1, 0}, 0}, LoadMode::resident);
    expect(server, {}, Outcome::waited, true);
    CHECK(world.actor(npc)->turns == 0);
    CHECK(world.object(object)->updates == 0);
    const auto active = load(world, {{1, 0}, 0});
    expect(server, {}, Outcome::waited, true);
    CHECK(world.actor(npc)->turns == 1);
    CHECK(world.object(object)->updates == 1);
    REQUIRE(world.release_request(active));
    world.reconcile_chunks();
    expect(server, {}, Outcome::waited, true);
    CHECK(world.actor(npc)->turns == 1);
    CHECK(world.object(object)->updates == 1);
    REQUIRE(world.release_request(resident));
    REQUIRE(server.set_bubble_radii(0, 0));
    CHECK(server.player() == player);
    CHECK(world.actor(player) != nullptr);
    CHECK(server.tick() == 4);
}

TEST_CASE("Zero-radius movement loads before crossing and restores frozen archived NPCs", "[server][streaming]") {
    Server server;
    World& world = server.world();
    const ActorHandle player = server.player();
    const ActorHandle old_npc = world.actor_at({5, 5});
    const PersistentId npc_id = world.actor(old_npc)->id;
    // Box the NPC so its chunk membership remains fixed for the freeze test.
    for (TileCoord position : {TileCoord{4, 5}, TileCoord{6, 5}, TileCoord{5, 4}, TileCoord{5, 6}}) {
        REQUIRE(world.set_tile(position, {Terrain::wall, 0}));
    }
    const ObjectHandle old_object = world.object_at({2, 0});
    const PersistentId object_id = world.object(old_object)->id;
    REQUIRE(world.set_object_enabled(old_object, true));
    expect(server, {CommandKind::pick_up}, Outcome::picked_up, true);
    const ItemHandle bag = world.actor(player)->inventory[0];
    const ItemHandle bandage = world.item(bag)->contents[0];
    const PersistentId bag_id = world.item(bag)->id;
    REQUIRE(server.set_bubble_radii(0, 0));
    // Relocate administratively to exercise the exact boundary in one command.
    REQUIRE(world.move_actor(player, {15, 0}));
    CHECK(world.tile({16, 0}) == nullptr);
    expect(server, {CommandKind::interact, 1, 0}, Outcome::unavailable, false);
    CHECK(world.tile({16, 0}) == nullptr);
    expect(server, {CommandKind::move, 1, 0}, Outcome::moved, true);
    CHECK(server.player() == player);
    REQUIRE(world.actor(player) != nullptr);
    CHECK(world.actor(player)->position == TileCoord{16, 0});
    CHECK(world.actor(player)->turns == 2);
    CHECK(world.loaded_chunks() == Vector<ChunkCoord>{{1, 0}});
    CHECK(world.actor(old_npc) == nullptr);
    CHECK(world.object(old_object) == nullptr);
    CHECK(world.find_item(bag_id) == bag);
    CHECK(world.item(bandage)->location == ItemLocation::inside(bag));
    for (int turn = 0; turn < 5; ++turn) {
        expect(server, {}, Outcome::waited, true);
    }
    // Load origin as resident only: it must still contain its pre-crossing
    // counters, with no catch-up for the six turns it was inactive.
    const auto inspect = load(world, {{0, 0}, 0}, LoadMode::resident);
    const ActorHandle fresh_npc = world.find_actor(npc_id);
    const ObjectHandle fresh_object = world.find_object(object_id);
    REQUIRE(world.actor(fresh_npc) != nullptr);
    REQUIRE(world.object(fresh_object) != nullptr);
    CHECK(fresh_npc != old_npc);
    CHECK(world.actor(fresh_npc)->turns == 1);
    CHECK(world.object(fresh_object)->updates == 1);
    expect(server, {CommandKind::move, -1, 0}, Outcome::moved, true);
    CHECK(world.actor(fresh_npc)->turns == 2);
    CHECK(world.object(fresh_object)->updates == 2);
    CHECK(server.player() == player);
    CHECK(world.actor(player)->inventory == Vector<ItemHandle>{bag});
    REQUIRE(world.release_request(inspect));
    world.reconcile_chunks();
    CHECK(world.loaded_chunks() == Vector<ChunkCoord>{{0, 0}});
    CHECK(world.check_invariants());
}

TEST_CASE("Failed temporary loads preserve the original bubble and the player key", "[server][streaming]") {
    Server server;
    World& world = server.world();
    const ActorHandle player = server.player();
    REQUIRE(world.set_tile({16, 0}, {Terrain::wall, 64}));
    REQUIRE(world.move_actor(player, {15, 0}));
    REQUIRE(server.set_bubble_radii(0, 0));
    const auto before = actors(world);
    const auto chunks = world.loaded_chunks();
    expect(server, {CommandKind::move, 1, 0}, Outcome::blocked, false);
    CHECK(world.loaded_chunks() == chunks);
    CHECK(actors(world) == before);
    CHECK(world.tile({16, 0}) == nullptr);
    CHECK(world.active({15, 0}));
    CHECK_FALSE(world.active({16, 0}));
    CHECK(server.player() == player);
    // The failed destination retains its archived values, and resident demand
    // cannot accidentally inherit the temporary request after failure.
    const auto inspect = load(world, {{1, 0}, 0}, LoadMode::resident);
    CHECK(*world.tile({16, 0}) == Tile{Terrain::wall, 64});
    CHECK_FALSE(world.active({16, 0}));
    REQUIRE(world.set_tile({16, 0}, {}));
    take(world.create_actor({16, 0}));
    REQUIRE(world.release_request(inspect));
    world.reconcile_chunks();
    expect(server, {CommandKind::move, 1, 0}, Outcome::occupied, false);
    CHECK(world.loaded_chunks() == chunks);
    CHECK(world.actor(player)->position == TileCoord{15, 0});
    CHECK(world.check_invariants());
}

TEST_CASE("Boundary coordinates are rejected before loading or changing radius requests", "[server][streaming]") {
    Server server;
    World& world = server.world();
    REQUIRE(server.set_bubble_radii(0, 0));
    load(world, {{world_coordinate_limit / chunk_size, 0}, 0}, LoadMode::resident);
    REQUIRE(world.set_tile({world_coordinate_limit, 0}, {}));
    REQUIRE(world.move_actor(server.player(), {world_coordinate_limit, 0}));
    REQUIRE(server.set_bubble_radii(0, 0));
    const auto before = world.loaded_chunks();
    expect(server, {CommandKind::move, 1, 0}, Outcome::unavailable, false);
    expect(server, {CommandKind::interact, 1, 0}, Outcome::unavailable, false);
    CHECK(world.loaded_chunks() == before);
    CHECK(server.set_bubble_radii(0, 1) == eastl::unexpected(WorldError::invalid_region));
    CHECK(world.loaded_chunks() == before);
    CHECK(world.active({world_coordinate_limit, 0}));
    CHECK(world.actor(server.player())->position == TileCoord{world_coordinate_limit, 0});
}

TEST_CASE("Simulation snapshots actors once and uses explicit RNG in persistent ID order", "[simulation][determinism]") {
    World world;
    load(world, {{0, 0}, 1});
    // The oldest actor is in the later chunk; the second crosses into it.
    const auto first = take(world.create_actor({17, 5}));
    const auto second = take(world.create_actor({15, 5}));
    const auto player = take(world.create_actor({0, 0}, true));
    const auto enabled = take(world.create_object({2, 2}, true));
    const auto disabled = take(world.create_object({3, 2}, false));
    std::uint32_t rng = 0;
    cataclysmx::advance(world, player, rng);
    CHECK(rng == 1'196'435'762U);
    CHECK(world.actor(first)->position == TileCoord{18, 5});
    CHECK(world.actor(second)->position == TileCoord{15, 6});
    CHECK(world.actor(first)->turns == 1);
    CHECK(world.actor(second)->turns == 1);
    CHECK(world.actor(player)->position == TileCoord{0, 0});
    CHECK(world.actor(player)->turns == 1);
    CHECK(world.object(enabled)->updates == 1);
    CHECK(world.object(disabled)->updates == 0);
    // A fresh actor is eligible on the next snapshot, never retroactively.
    const auto newcomer = take(world.create_actor({10, 10}));
    CHECK(world.actor(newcomer)->turns == 0);
    rng = 0;
    // Remove the older NPC so the boundary-crossing NPC gets the east draw.
    REQUIRE(world.destroy_actor(first));
    REQUIRE(world.move_actor(second, {15, 5}));
    cataclysmx::advance(world, player, rng);
    CHECK(world.actor(second)->position == TileCoord{16, 5});
    CHECK(world.actor(second)->turns == 2);
    CHECK(world.actor(newcomer)->turns == 1);
    CHECK(world.actor(player)->turns == 2);
    CHECK(world.check_invariants());
}

TEST_CASE("Simulation never moves NPCs into resident-only or unavailable tiles", "[simulation][activity]") {
    World world;
    load(world, {{0, 0}, 0});
    const auto resident = load(world, {{1, 0}, 0}, LoadMode::resident);
    const auto npc = take(world.create_actor({15, 2}));
    const auto player = take(world.create_actor({0, 0}, true));
    const auto sleeping = take(world.create_actor({17, 2}));
    const auto object = take(world.create_object({18, 2}, true));
    std::uint32_t rng = 0;
    cataclysmx::advance(world, player, rng);
    CHECK(rng == 1'013'904'223U);
    CHECK(world.actor(npc)->position == TileCoord{15, 2});
    CHECK(world.actor(npc)->turns == 1);
    CHECK(world.actor(sleeping)->turns == 0);
    CHECK(world.object(object)->updates == 0);
    REQUIRE(world.release_request(resident));
    world.reconcile_chunks();
    rng = 0;
    const auto chunks = world.loaded_chunks();
    cataclysmx::advance(world, player, rng);
    CHECK(world.loaded_chunks() == chunks);
    CHECK(world.tile({16, 2}) == nullptr);
    CHECK(world.actor(npc)->position == TileCoord{15, 2});
    CHECK(world.actor(npc)->turns == 2);
    CHECK(world.actor(sleeping) == nullptr);
    // A wall and then an occupied active target also consume only the NPC's
    // draw, without changing positions or loading chunks.
    REQUIRE(world.move_actor(npc, {14, 2}));
    REQUIRE(world.set_tile({15, 2}, {Terrain::wall, 0}));
    rng = 0;
    cataclysmx::advance(world, player, rng);
    CHECK(world.actor(npc)->position == TileCoord{14, 2});
    REQUIRE(world.set_tile({15, 2}, {}));
    REQUIRE(world.move_actor(player, {15, 2}));
    rng = 0;
    cataclysmx::advance(world, player, rng);
    CHECK(world.actor(npc)->position == TileCoord{14, 2});
    CHECK(world.actor(player)->position == TileCoord{15, 2});
    CHECK(world.check_invariants());
}

TEST_CASE("Simulation replay is independent of arbitrary chunk reload order", "[simulation][streaming][determinism]") {
    World first(128);
    World second(128);
    const auto left_first = load(first, {{0, 0}, 0});
    const auto right_first = load(first, {{1, 0}, 0});
    const auto left_second = load(second, {{0, 0}, 0});
    const auto right_second = load(second, {{1, 0}, 0});
    for (World* world : {&first, &second}) {
        take(world->create_actor({17, 5}));
        take(world->create_actor({14, 5}));
        take(world->create_actor({1, 1}, true));
        take(world->create_object({18, 3}, true));
        take(world->create_object({3, 3}, false));
    }
    const PersistentId player_id = first.actor(first.actor_at({1, 1}))->id;
    // Recreate runtime slots in opposite orders, before running any turns.
    REQUIRE(first.release_request(left_first));
    REQUIRE(first.release_request(right_first));
    first.reconcile_chunks();
    REQUIRE(second.release_request(left_second));
    REQUIRE(second.release_request(right_second));
    second.reconcile_chunks();
    load(first, {{1, 0}, 0});
    load(first, {{0, 0}, 0});
    load(second, {{0, 0}, 0});
    load(second, {{1, 0}, 0});
    std::uint32_t first_rng = 0x12345678U;
    std::uint32_t second_rng = first_rng;
    for (int turn = 0; turn < 80; ++turn) {
        cataclysmx::advance(first, first.find_actor(player_id), first_rng);
        cataclysmx::advance(second, second.find_actor(player_id), second_rng);
        CHECK(first_rng == second_rng);
        CHECK(actors(first) == actors(second));
        const auto first_objects = first.active_objects();
        const auto second_objects = second.active_objects();
        REQUIRE(first_objects.size() == second_objects.size());
        for (std::size_t index = 0; index < first_objects.size(); ++index) {
            const auto* a = first.object(first_objects[index]);
            const auto* b = second.object(second_objects[index]);
            CHECK(a->id == b->id);
            CHECK(a->enabled == b->enabled);
            CHECK(a->updates == b->updates);
        }
    }
    CHECK(first.check_invariants());
    CHECK(second.check_invariants());
}

TEST_CASE("Local server command replay returns identical outcomes and snapshots", "[server][determinism]") {
    Server first(4321);
    Server second(4321);
    const Command commands[] = {
        {CommandKind::pick_up}, {CommandKind::wait}, {CommandKind::move, 1, 0},
        {CommandKind::pick_up}, {CommandKind::interact, 1, 0}, {CommandKind::move, 1, 0},
        {CommandKind::move, 1, 0}, {CommandKind::interact, 1, 0}, {CommandKind::move, 1, 0},
        {CommandKind::move, 0, 1}, {CommandKind::drop}, {CommandKind::wait},
        {CommandKind::move, 1, 1}, {CommandKind::pick_up}, {CommandKind::wait}
    };
    for (Command command : commands) {
        const auto a = first.send(command);
        const auto b = second.send(command);
        CHECK(a.outcome == b.outcome);
        CHECK(a.advanced == b.advanced);
        CHECK(a.tick == b.tick);
        same_view(first.view(), second.view());
        CHECK(actors(first.world()) == actors(second.world()));
    }
    CHECK(first.world().check_invariants());
    CHECK(second.world().check_invariants());
}

}
