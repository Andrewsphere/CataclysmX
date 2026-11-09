#include "core/server.h"
#include "core/simulation.h"
#include "content_fixture.h"

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
    const LoadRequest request = take(world.RequestChunks(region, mode));
    world.ReconcileChunks();
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
    const std::uint64_t before = server.GetTick();
    const CommandResult result = server.Send(command);
    CHECK(result.outcome == outcome);
    CHECK(result.advanced == advances);
    CHECK(result.tick == before + (advances ? 100 : 0));
    CHECK(server.GetTick() == result.tick);
    CHECK(server.GetWorld().GetActor(server.GetPlayer())->turns == result.tick / 100);
    CHECK(server.GetWorld().CheckInvariants());
}

struct ActorState {
    PersistentId id;
    TileCoord position;
    std::uint64_t turns;
    bool operator==(const ActorState&) const = default;
};

Vector<ActorState> actors(const World& world) {
    Vector<ActorState> result;
    for (ChunkCoord position : world.LoadedChunks()) {
        for (ActorHandle handle : world.GetChunk(position)->actors) {
            const Actor* actor = world.GetActor(handle);
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
    Server server(TestContent(), 73);
    World& world = server.GetWorld();
    REQUIRE(world.GetActor(server.GetPlayer()) != nullptr);
    CHECK(world.GetActor(server.GetPlayer())->position == TileCoord{0, 0});
    CHECK(world.GetActor(server.GetPlayer())->player);
    CHECK(server.GetTick() == 0);
    CHECK(world.LoadedChunks().size() == 25 * world_layer_count);
    CHECK(world.IsActive({-16, -16}));
    CHECK_FALSE(world.IsActive({-32, -32}));
    const auto backpack = world.ItemsAt({0, 0});
    REQUIRE(backpack.size() == 1);
    CHECK(world.GetItem(backpack[0])->type == TestItem("backpack"));
    REQUIRE(world.GetItem(backpack[0])->contents.size() == 1);
    CHECK(world.GetItem(world.GetItem(backpack[0])->contents[0])->type == TestItem("bandage"));
    REQUIRE(world.ItemsAt({1, 0}).size() == 1);
    CHECK(world.GetItem(world.ItemsAt({1, 0})[0])->type == TestItem("rock"));
    const ObjectHandle object = world.ObjectAt({2, 0});
    REQUIRE(world.GetObject(object) != nullptr);
    CHECK_FALSE(world.GetObject(object)->enabled);
    CHECK(world.GetTile({3, 0})->terrain == TestTerrain("closed_door"));
    CHECK(world.GetTile({3, 1})->terrain == TestTerrain("wall"));
    const ActorHandle npc = world.ActorAt({5, 5});
    REQUIRE(world.GetActor(npc) != nullptr);
    CHECK_FALSE(world.GetActor(npc)->player);
    // Keep command outcome checks independent of the NPC's chosen route.
    for (TileCoord position : {TileCoord{4, 5}, TileCoord{6, 5}, TileCoord{5, 4}, TileCoord{5, 6}}) {
        REQUIRE(world.SetTile(position, {TestTerrain("wall"), 0}));
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
    CHECK(world.GetActor(npc)->turns == 0);

    // Fields irrelevant to the selected command are deliberately ignored.
    expect(server, {CommandKind::wait, INT_MIN, INT_MAX, UINT64_MAX}, Outcome::waited, true);
    expect(server, {CommandKind::pick_up, INT_MAX, INT_MIN}, Outcome::picked_up, true);
    expect(server, {CommandKind::drop, INT_MIN, INT_MAX}, Outcome::dropped, true);
    expect(server, {CommandKind::move, 1, 0, UINT64_MAX}, Outcome::moved, true);
    expect(server, {CommandKind::interact, 1, 0, UINT64_MAX}, Outcome::toggled, true);
    CHECK(world.GetObject(object)->enabled);
    CHECK(world.GetObject(object)->updates == 1);
    expect(server, {CommandKind::interact, 1, 0}, Outcome::toggled, true);
    CHECK_FALSE(world.GetObject(object)->enabled);
    CHECK(world.GetObject(object)->updates == 1);
    expect(server, {CommandKind::move, 1, 0}, Outcome::moved, true);
    expect(server, {CommandKind::move, 1, 0}, Outcome::blocked, false);
    expect(server, {CommandKind::interact, 1, 0}, Outcome::opened, true);
    expect(server, {CommandKind::interact, 1, 0}, Outcome::closed, true);
    expect(server, {CommandKind::interact, 1, 0}, Outcome::opened, true);
    expect(server, {CommandKind::move, 1, 0}, Outcome::moved, true);
    expect(server, {CommandKind::move, 0, 1}, Outcome::blocked, false);
    expect(server, {CommandKind::interact, 0, 1}, Outcome::nothing_here, false);
    CHECK(world.GetActor(npc)->turns == server.GetTick() / 100);
}

TEST_CASE("Failures do not consume RNG and occupied open doors cannot close", "[server][commands][determinism]") {
    Server subject(TestContent(), 918);
    Server reference(TestContent(), 918);
    const auto blocker = take(subject.GetWorld().CreateActor({-1, 0}));
    const auto other = take(reference.GetWorld().CreateActor({-1, 0}));
    expect(subject, {CommandKind::move, -1, 0}, Outcome::occupied, false);
    REQUIRE(subject.GetWorld().DestroyActor(blocker));
    REQUIRE(reference.GetWorld().DestroyActor(other));
    REQUIRE(subject.GetWorld().SetTile({1, 0}, {TestTerrain("closed_door"), 9}));
    REQUIRE(reference.GetWorld().SetTile({1, 0}, {TestTerrain("closed_door"), 9}));
    expect(subject, {CommandKind::move, 1, 0}, Outcome::blocked, false);
    expect(subject, {CommandKind::drop, 0, 0, UINT64_MAX}, Outcome::not_carried, false);
    expect(subject, {static_cast<CommandKind>(-1)}, Outcome::invalid_command, false);
    CHECK(actors(subject.GetWorld()) == actors(reference.GetWorld()));
    same_view(subject.View(), reference.View());
    for (int turn = 0; turn < 24; ++turn) {
        expect(subject, {}, Outcome::waited, true);
        expect(reference, {}, Outcome::waited, true);
        CHECK(actors(subject.GetWorld()) == actors(reference.GetWorld()));
    }

    Server door(TestContent());
    REQUIRE(door.GetWorld().SetTile({1, 0}, {TestTerrain("open_door"), 17}));
    take(door.GetWorld().CreateActor({1, 0}));
    expect(door, {CommandKind::interact, 1, 0}, Outcome::occupied, false);
    CHECK(*door.GetWorld().GetTile({1, 0}) == Tile{TestTerrain("open_door"), 17});
}

TEST_CASE("Pickup and drop use direct ownership and persistent IDs while preserving nested items", "[server][inventory]") {
    Server server(TestContent());
    World& world = server.GetWorld();
    const ItemHandle bag = world.ItemsAt({0, 0})[0];
    const ItemHandle bandage = world.GetItem(bag)->contents[0];
    const PersistentId bag_id = world.GetItem(bag)->id;
    const PersistentId bandage_id = world.GetItem(bandage)->id;
    const ItemHandle rock = world.ItemsAt({1, 0})[0];
    const PersistentId rock_id = world.GetItem(rock)->id;
    const ItemHandle loose = take(world.CreateItem(TestItem("rock"), ItemLocation::Ground({0, 0})));
    const PersistentId loose_id = world.GetItem(loose)->id;
    // Collection order differs from persistent-ID order.
    REQUIRE(world.TransferItem(bag, ItemLocation::Ground({0, 1})));
    REQUIRE(world.TransferItem(bag, ItemLocation::Ground({0, 0})));

    expect(server, {CommandKind::pick_up, 0, 0, rock_id}, Outcome::nothing_here, false);
    expect(server, {CommandKind::pick_up, 0, 0, bandage_id}, Outcome::nothing_here, false);
    expect(server, {CommandKind::pick_up}, Outcome::picked_up, true);
    CHECK(world.GetItem(bag)->location == ItemLocation::Carried(server.GetPlayer()));
    CHECK(world.GetItem(bandage)->location == ItemLocation::Inside(bag));
    expect(server, {CommandKind::drop, 0, 0, bandage_id}, Outcome::not_carried, false);
    expect(server, {CommandKind::pick_up, 0, 0, loose_id}, Outcome::picked_up, true);
    REQUIRE(world.TransferItem(bag, ItemLocation::Ground({0, 0})));
    REQUIRE(world.TransferItem(bag, ItemLocation::Carried(server.GetPlayer())));
    const WorldView inventory = server.View();
    REQUIRE(inventory.inventory.size() == 3);
    CHECK(inventory.inventory[0].id == bag_id);
    CHECK(inventory.inventory[0].type == TestItem("backpack"));
    CHECK(inventory.inventory[0].contained_items == 1);
    CHECK(inventory.inventory[0].parent == 0);
    CHECK(inventory.inventory[1].id == bandage_id);
    CHECK(inventory.inventory[1].type == TestItem("bandage"));
    CHECK(inventory.inventory[1].contained_items == 0);
    CHECK(inventory.inventory[1].parent == bag_id);
    CHECK(inventory.inventory[2].id == loose_id);
    CHECK(inventory.inventory[2].type == TestItem("rock"));
    CHECK(inventory.inventory[2].contained_items == 0);
    CHECK(inventory.inventory[2].parent == 0);
    expect(server, {CommandKind::drop}, Outcome::dropped, true);
    CHECK(world.GetItem(bag)->location == ItemLocation::Ground({0, 0}));
    CHECK(world.GetItem(bandage)->id == bandage_id);
    CHECK(world.GetItem(bandage)->location == ItemLocation::Inside(bag));
    expect(server, {CommandKind::drop, 0, 0, bag_id}, Outcome::not_carried, false);
    expect(server, {CommandKind::drop, 0, 0, loose_id}, Outcome::dropped, true);
    expect(server, {CommandKind::pick_up, 0, 0, bag_id}, Outcome::picked_up, true);
    CHECK(world.FindItem(bag_id) == bag);
    CHECK(world.GetItem(bag)->contents == Vector<ItemHandle>{bandage});
    CHECK(world.CheckInvariants());
}

TEST_CASE("Views are isolated value snapshots with bounded circular LOS and empty hidden cells", "[server][view]") {
    Server server(TestContent());
    World& world = server.GetWorld();
    REQUIRE(world.SetTile({1, 0}, {TestTerrain("wall"), 23}));
    REQUIRE(world.SetTile({2, 0}, {TestTerrain("floor"), 71}));
    const auto hidden_actor = take(world.CreateActor({2, 0}));
    take(world.CreateItem(TestItem("rock"), ItemLocation::Ground({2, 0})));
    const auto object = world.ObjectAt({2, 0});
    REQUIRE(world.SetObjectEnabled(object, true));
    REQUIRE(world.SetTile({0, 1}, {TestTerrain("closed_door"), 8}));
    REQUIRE(world.SetTile({0, 2}, {TestTerrain("open_door"), 93}));
    const auto before_observation = actors(world);
    const auto chunks_before_observation = world.LoadedChunks();
    const auto tick_before_observation = server.GetTick();
    const WorldView old = server.View(4);
    server.View(12);
    CHECK(actors(world) == before_observation);
    CHECK(world.LoadedChunks() == chunks_before_observation);
    CHECK(server.GetTick() == tick_before_observation);
    CHECK(old.origin == TileCoord{-4, -4});
    CHECK(old.width == 9);
    CHECK(old.height == 9);
    CHECK(old.cells.size() == 81);
    CHECK(cell_at(old, {0, 0}).player);
    CHECK(cell_at(old, {0, 0}).actor == world.GetActor(server.GetPlayer())->id);
    CHECK(cell_at(old, {1, 0}).visible);
    CHECK(cell_at(old, {1, 0}).tile == Tile{TestTerrain("wall"), 23});
    CHECK(cell_at(old, {2, 0}) == ViewCell{});
    CHECK(cell_at(old, {0, 1}).visible);
    CHECK(cell_at(old, {0, 1}).tile.terrain == TestTerrain("closed_door"));
    CHECK(cell_at(old, {0, 2}) == ViewCell{});
    CHECK(cell_at(old, {-4, -4}) == ViewCell{});

    REQUIRE(world.SetTile({1, 0}, {TestTerrain("open_door"), 12}));
    const WorldView revealed = server.View(4);
    CHECK(cell_at(revealed, {2, 0}).visible);
    CHECK(cell_at(revealed, {2, 0}).actor == world.GetActor(hidden_actor)->id);
    CHECK(cell_at(revealed, {2, 0}).ground_items == 1);
    CHECK(cell_at(revealed, {2, 0}).object == world.GetObject(object)->id);
    CHECK(cell_at(revealed, {2, 0}).object_enabled);
    CHECK(cell_at(revealed, {2, 0}).tile == Tile{TestTerrain("floor"), 71});
    CHECK(cell_at(old, {2, 0}) == ViewCell{});
    CHECK(cell_at(old, {1, 0}).tile == Tile{TestTerrain("wall"), 23});

    expect(server, {CommandKind::pick_up}, Outcome::picked_up, true);
    const WorldView carrying = server.View(0);
    REQUIRE(carrying.inventory.size() == 2);
    expect(server, {CommandKind::drop}, Outcome::dropped, true);
    CHECK(carrying.inventory.size() == 2);
    CHECK(carrying.tick + 100 == server.GetTick());
    CHECK(old.inventory.empty());
    CHECK(server.View(INT_MIN).width == 1);
    CHECK(server.View(INT_MAX).width == 25);
    CHECK(server.View(INT_MAX).cells.size() == 625);

    REQUIRE(server.SetBubbleRadii(0, 0));
    const WorldView unavailable = server.View(4);
    CHECK(cell_at(unavailable, {-1, 0}) == ViewCell{});
    CHECK(cell_at(unavailable, {0, -1}) == ViewCell{});
    CHECK(world.GetChunk({-1, 0}) == nullptr);
    CHECK(world.CheckInvariants());
}

TEST_CASE("Bubble radii validate atomically and resident force loads never simulate", "[server][streaming]") {
    Server server(TestContent());
    World& world = server.GetWorld();
    const ActorHandle player = server.GetPlayer();
    const auto before = world.LoadedChunks();
    CHECK(server.SetBubbleRadii(-1, 2) == eastl::unexpected(WorldError::invalid_region));
    CHECK(server.SetBubbleRadii(2, 1) == eastl::unexpected(WorldError::invalid_region));
    CHECK(server.SetBubbleRadii(0, 9) == eastl::unexpected(WorldError::invalid_region));
    CHECK(server.SetBubbleRadii(INT_MAX, INT_MIN) == eastl::unexpected(WorldError::invalid_region));
    CHECK(world.LoadedChunks() == before);
    CHECK(world.IsActive({16, 0}));
    CHECK_FALSE(world.IsActive({32, 0}));
    CHECK(server.GetTick() == 0);
    REQUIRE(server.SetBubbleRadii(0, 1));
    CHECK(server.GetPlayer() == player);
    CHECK(world.GetActor(player) != nullptr);
    REQUIRE(world.SetTile({17, 1}, {TestTerrain("floor"), 0}));
    const ActorHandle npc = take(world.CreateActor({17, 1}));
    const ObjectHandle object = take(world.CreateObject({18, 1}, true));
    expect(server, {}, Outcome::waited, true);
    CHECK(world.GetActor(npc)->turns == 0);
    CHECK(world.GetObject(object)->updates == 0);
    const auto resident = load(world, {{1, 0}, 0}, LoadMode::resident);
    expect(server, {}, Outcome::waited, true);
    CHECK(world.GetActor(npc)->turns == 0);
    CHECK(world.GetObject(object)->updates == 0);
    const auto active = load(world, {{1, 0}, 0});
    expect(server, {}, Outcome::waited, true);
    CHECK(world.GetActor(npc)->turns == 1);
    CHECK(world.GetObject(object)->updates == 1);
    REQUIRE(world.ReleaseRequest(active));
    world.ReconcileChunks();
    expect(server, {}, Outcome::waited, true);
    CHECK(world.GetActor(npc)->turns == 1);
    CHECK(world.GetObject(object)->updates == 1);
    REQUIRE(world.ReleaseRequest(resident));
    REQUIRE(server.SetBubbleRadii(0, 0));
    CHECK(server.GetPlayer() == player);
    CHECK(world.GetActor(player) != nullptr);
    CHECK(server.GetTick() == 400);
}

TEST_CASE("Zero-radius movement loads before crossing and restores frozen archived NPCs", "[server][streaming]") {
    Server server(TestContent());
    World& world = server.GetWorld();
    const ActorHandle player = server.GetPlayer();
    const ActorHandle old_npc = world.ActorAt({5, 5});
    const PersistentId npc_id = world.GetActor(old_npc)->id;
    // Box the NPC so its chunk membership remains fixed for the freeze test.
    for (TileCoord position : {TileCoord{4, 5}, TileCoord{6, 5}, TileCoord{5, 4}, TileCoord{5, 6}}) {
        REQUIRE(world.SetTile(position, {TestTerrain("wall"), 0}));
    }
    const ObjectHandle old_object = world.ObjectAt({2, 0});
    const PersistentId object_id = world.GetObject(old_object)->id;
    REQUIRE(world.SetObjectEnabled(old_object, true));
    expect(server, {CommandKind::pick_up}, Outcome::picked_up, true);
    const ItemHandle bag = world.GetActor(player)->inventory[0];
    const ItemHandle bandage = world.GetItem(bag)->contents[0];
    const PersistentId bag_id = world.GetItem(bag)->id;
    REQUIRE(server.SetBubbleRadii(0, 0));
    // Relocate administratively to exercise the exact boundary in one command.
    REQUIRE(world.MoveActor(player, {15, 0}));
    CHECK(world.GetTile({16, 0}) == nullptr);
    expect(server, {CommandKind::interact, 1, 0}, Outcome::unavailable, false);
    CHECK(world.GetTile({16, 0}) == nullptr);
    expect(server, {CommandKind::move, 1, 0}, Outcome::moved, true);
    CHECK(server.GetPlayer() == player);
    REQUIRE(world.GetActor(player) != nullptr);
    CHECK(world.GetActor(player)->position == TileCoord{16, 0});
    CHECK(world.GetActor(player)->turns == 2);
    CHECK(world.LoadedChunks().size() == world_layer_count);
    for (ChunkCoord chunk : world.LoadedChunks()) {
        CHECK(chunk.x == 1);
        CHECK(chunk.z == 0);
    }
    CHECK(world.GetActor(old_npc) == nullptr);
    CHECK(world.GetObject(old_object) == nullptr);
    CHECK(world.FindItem(bag_id) == bag);
    CHECK(world.GetItem(bandage)->location == ItemLocation::Inside(bag));
    for (int turn = 0; turn < 5; ++turn) {
        expect(server, {}, Outcome::waited, true);
    }
    // Load origin as resident only: it must still contain its pre-crossing
    // counters, with no catch-up for the six turns it was inactive.
    const auto inspect = load(world, {{0, 0}, 0}, LoadMode::resident);
    const ActorHandle fresh_npc = world.FindActor(npc_id);
    const ObjectHandle fresh_object = world.FindObject(object_id);
    REQUIRE(world.GetActor(fresh_npc) != nullptr);
    REQUIRE(world.GetObject(fresh_object) != nullptr);
    CHECK(fresh_npc != old_npc);
    CHECK(world.GetActor(fresh_npc)->turns == 1);
    CHECK(world.GetObject(fresh_object)->updates == 1);
    expect(server, {CommandKind::move, -1, 0}, Outcome::moved, true);
    CHECK(world.GetActor(fresh_npc)->turns == 2);
    CHECK(world.GetObject(fresh_object)->updates == 2);
    CHECK(server.GetPlayer() == player);
    CHECK(world.GetActor(player)->inventory == Vector<ItemHandle>{bag});
    REQUIRE(world.ReleaseRequest(inspect));
    world.ReconcileChunks();
    CHECK(world.LoadedChunks().size() == world_layer_count);
    for (ChunkCoord chunk : world.LoadedChunks()) {
        CHECK(chunk.x == 0);
        CHECK(chunk.z == 0);
    }
    CHECK(world.CheckInvariants());
}

TEST_CASE("Failed temporary loads preserve the original bubble and the player key", "[server][streaming]") {
    Server server(TestContent());
    World& world = server.GetWorld();
    const ActorHandle player = server.GetPlayer();
    REQUIRE(world.SetTile({16, 0}, {TestTerrain("wall"), 64}));
    REQUIRE(world.MoveActor(player, {15, 0}));
    REQUIRE(server.SetBubbleRadii(0, 0));
    const auto before = actors(world);
    const auto chunks = world.LoadedChunks();
    expect(server, {CommandKind::move, 1, 0}, Outcome::blocked, false);
    CHECK(world.LoadedChunks() == chunks);
    CHECK(actors(world) == before);
    CHECK(world.GetTile({16, 0}) == nullptr);
    CHECK(world.IsActive({15, 0}));
    CHECK_FALSE(world.IsActive({16, 0}));
    CHECK(server.GetPlayer() == player);
    // The failed destination retains its archived values, and resident demand
    // cannot accidentally inherit the temporary request after failure.
    const auto inspect = load(world, {{1, 0}, 0}, LoadMode::resident);
    CHECK(*world.GetTile({16, 0}) == Tile{TestTerrain("wall"), 64});
    CHECK_FALSE(world.IsActive({16, 0}));
    REQUIRE(world.SetTile({16, 0}, {TestTerrain("floor"), 0}));
    take(world.CreateActor({16, 0}));
    REQUIRE(world.ReleaseRequest(inspect));
    world.ReconcileChunks();
    expect(server, {CommandKind::move, 1, 0}, Outcome::occupied, false);
    CHECK(world.LoadedChunks() == chunks);
    CHECK(world.GetActor(player)->position == TileCoord{15, 0});
    CHECK(world.CheckInvariants());
}

TEST_CASE("Boundary coordinates are rejected before loading or changing radius requests", "[server][streaming]") {
    Server server(TestContent());
    World& world = server.GetWorld();
    REQUIRE(server.SetBubbleRadii(0, 0));
    load(world, {{world_coordinate_limit / chunk_size, 0}, 0}, LoadMode::resident);
    REQUIRE(world.SetTile({world_coordinate_limit, 0}, {TestTerrain("floor"), 0}));
    REQUIRE(world.MoveActor(server.GetPlayer(), {world_coordinate_limit, 0}));
    REQUIRE(server.SetBubbleRadii(0, 0));
    const auto before = world.LoadedChunks();
    expect(server, {CommandKind::move, 1, 0}, Outcome::unavailable, false);
    expect(server, {CommandKind::interact, 1, 0}, Outcome::unavailable, false);
    CHECK(world.LoadedChunks() == before);
    CHECK(server.SetBubbleRadii(0, 1) == eastl::unexpected(WorldError::invalid_region));
    CHECK(world.LoadedChunks() == before);
    CHECK(world.IsActive({world_coordinate_limit, 0}));
    CHECK(world.GetActor(server.GetPlayer())->position == TileCoord{world_coordinate_limit, 0});
}

TEST_CASE("Simulation snapshots actors once and uses explicit RNG in persistent ID order", "[simulation][determinism]") {
    World world(TestContent());
    load(world, {{0, 0}, 1});
    // The oldest actor is in the later chunk; the second crosses into it.
    const auto first = take(world.CreateActor({17, 5}));
    const auto second = take(world.CreateActor({15, 5}));
    const auto player = take(world.CreateActor({0, 0}, true));
    const auto enabled = take(world.CreateObject({2, 2}, true));
    const auto disabled = take(world.CreateObject({3, 2}, false));
    std::uint32_t rng = 0;
    cataclysmx::Advance(world, player, rng);
    CHECK(rng == 1'196'435'762U);
    CHECK(world.GetActor(first)->position == TileCoord{18, 5});
    CHECK(world.GetActor(second)->position == TileCoord{15, 6});
    CHECK(world.GetActor(first)->turns == 1);
    CHECK(world.GetActor(second)->turns == 1);
    CHECK(world.GetActor(player)->position == TileCoord{0, 0});
    CHECK(world.GetActor(player)->turns == 1);
    CHECK(world.GetObject(enabled)->updates == 1);
    CHECK(world.GetObject(disabled)->updates == 0);
    // A fresh actor is eligible on the next snapshot, never retroactively.
    const auto newcomer = take(world.CreateActor({10, 10}));
    CHECK(world.GetActor(newcomer)->turns == 0);
    rng = 0;
    // Remove the older NPC so the boundary-crossing NPC gets the east draw.
    REQUIRE(world.DestroyActor(first));
    REQUIRE(world.MoveActor(second, {15, 5}));
    cataclysmx::Advance(world, player, rng);
    CHECK(world.GetActor(second)->position == TileCoord{16, 5});
    CHECK(world.GetActor(second)->turns == 2);
    CHECK(world.GetActor(newcomer)->turns == 1);
    CHECK(world.GetActor(player)->turns == 2);
    CHECK(world.CheckInvariants());
}

TEST_CASE("Simulation never moves NPCs into resident-only or unavailable tiles", "[simulation][activity]") {
    World world(TestContent());
    load(world, {{0, 0}, 0});
    const auto resident = load(world, {{1, 0}, 0}, LoadMode::resident);
    const auto npc = take(world.CreateActor({15, 2}));
    const auto player = take(world.CreateActor({0, 0}, true));
    const auto sleeping = take(world.CreateActor({17, 2}));
    const auto object = take(world.CreateObject({18, 2}, true));
    std::uint32_t rng = 0;
    cataclysmx::Advance(world, player, rng);
    CHECK(rng == 1'013'904'223U);
    CHECK(world.GetActor(npc)->position == TileCoord{15, 2});
    CHECK(world.GetActor(npc)->turns == 1);
    CHECK(world.GetActor(sleeping)->turns == 0);
    CHECK(world.GetObject(object)->updates == 0);
    REQUIRE(world.ReleaseRequest(resident));
    world.ReconcileChunks();
    rng = 0;
    const auto chunks = world.LoadedChunks();
    cataclysmx::Advance(world, player, rng);
    CHECK(world.LoadedChunks() == chunks);
    CHECK(world.GetTile({16, 2}) == nullptr);
    CHECK(world.GetActor(npc)->position == TileCoord{15, 2});
    CHECK(world.GetActor(npc)->turns == 2);
    CHECK(world.GetActor(sleeping) == nullptr);
    // A wall and then an occupied active target also consume only the NPC's
    // draw, without changing positions or loading chunks.
    REQUIRE(world.MoveActor(npc, {14, 2}));
    REQUIRE(world.SetTile({15, 2}, {TestTerrain("wall"), 0}));
    rng = 0;
    cataclysmx::Advance(world, player, rng);
    CHECK(world.GetActor(npc)->position == TileCoord{14, 2});
    REQUIRE(world.SetTile({15, 2}, {TestTerrain("floor"), 0}));
    REQUIRE(world.MoveActor(player, {15, 2}));
    rng = 0;
    cataclysmx::Advance(world, player, rng);
    CHECK(world.GetActor(npc)->position == TileCoord{14, 2});
    CHECK(world.GetActor(player)->position == TileCoord{15, 2});
    CHECK(world.CheckInvariants());
}

TEST_CASE("Simulation replay is independent of arbitrary chunk reload order", "[simulation][streaming][determinism]") {
    World first(TestContent(), 128);
    World second(TestContent(), 128);
    const auto left_first = load(first, {{0, 0}, 0});
    const auto right_first = load(first, {{1, 0}, 0});
    const auto left_second = load(second, {{0, 0}, 0});
    const auto right_second = load(second, {{1, 0}, 0});
    for (World* world : {&first, &second}) {
        take(world->CreateActor({17, 5}));
        take(world->CreateActor({14, 5}));
        take(world->CreateActor({1, 1}, true));
        take(world->CreateObject({18, 3}, true));
        take(world->CreateObject({3, 3}, false));
    }
    const PersistentId player_id = first.GetActor(first.ActorAt({1, 1}))->id;
    // Recreate runtime slots in opposite orders, before running any turns.
    REQUIRE(first.ReleaseRequest(left_first));
    REQUIRE(first.ReleaseRequest(right_first));
    first.ReconcileChunks();
    REQUIRE(second.ReleaseRequest(left_second));
    REQUIRE(second.ReleaseRequest(right_second));
    second.ReconcileChunks();
    load(first, {{1, 0}, 0});
    load(first, {{0, 0}, 0});
    load(second, {{0, 0}, 0});
    load(second, {{1, 0}, 0});
    std::uint32_t first_rng = 0x12345678U;
    std::uint32_t second_rng = first_rng;
    for (int turn = 0; turn < 80; ++turn) {
        cataclysmx::Advance(first, first.FindActor(player_id), first_rng);
        cataclysmx::Advance(second, second.FindActor(player_id), second_rng);
        CHECK(first_rng == second_rng);
        CHECK(actors(first) == actors(second));
        const auto first_objects = first.ActiveObjects();
        const auto second_objects = second.ActiveObjects();
        REQUIRE(first_objects.size() == second_objects.size());
        for (std::size_t index = 0; index < first_objects.size(); ++index) {
            const auto* a = first.GetObject(first_objects[index]);
            const auto* b = second.GetObject(second_objects[index]);
            CHECK(a->id == b->id);
            CHECK(a->enabled == b->enabled);
            CHECK(a->updates == b->updates);
        }
    }
    CHECK(first.CheckInvariants());
    CHECK(second.CheckInvariants());
}

TEST_CASE("Six axial commands require exact paired stairs and respect layer bounds", "[server][3d][commands]") {
    Server server(TestContent());
    World& world = server.GetWorld();
    Command up{CommandKind::move};
    up.dy = 1;
    Command down = up;
    down.dy = -1;
    CHECK(ValidCommandShape(up));
    CHECK(ValidCommandShape(down));
    Command diagonal = up;
    diagonal.dx = 1;
    CHECK_FALSE(ValidCommandShape(diagonal));
    diagonal.dx = 0;
    diagonal.dy = INT_MIN;
    CHECK_FALSE(ValidCommandShape(diagonal));
    Command directedUse{CommandKind::use_item};
    directedUse.item = 1;
    directedUse.capability = {1};
    directedUse.itemAction = "arm";
    directedUse.dy = -1;
    CHECK(ValidCommandShape(directedUse));
    directedUse.dx = 1;
    CHECK_FALSE(ValidCommandShape(directedUse));
    directedUse.dx = 0;
    directedUse.otherItem = 2;
    CHECK_FALSE(ValidCommandShape(directedUse));
    REQUIRE(world.SetTile({0, 0, 1}, {TestTerrain("stairs"), 0}));
    expect(server, up, Outcome::blocked, false); // Source is ordinary floor.
    REQUIRE(world.SetTile({0, 0, 0}, {TestTerrain("stairs"), 0}));
    REQUIRE(world.SetTile({0, 0, 1}, {TestTerrain("floor"), 0}));
    expect(server, up, Outcome::blocked, false); // Target must be stairs too.
    REQUIRE(world.SetTile({0, 0, 1}, {TestTerrain("stairs"), 0}));
    const auto occupied = take(world.CreateActor({0, 0, 1}));
    expect(server, up, Outcome::occupied, false);
    REQUIRE(world.DestroyActor(occupied));
    const auto chunks = world.LoadedChunks();
    expect(server, up, Outcome::moved, true);
    CHECK(world.GetActor(server.GetPlayer())->position == TileCoord{0, 0, 1});
    CHECK(world.LoadedChunks() == chunks);
    CHECK(server.View().origin.y == 1);
    REQUIRE(world.SetTile({1, 0, 1}, {TestTerrain("floor"), 0}));
    expect(server, {CommandKind::move, 1, 0}, Outcome::moved, true);
    CHECK(server.View().player_position == TileCoord{1, 0, 1});
    expect(server, {CommandKind::move, -1, 0}, Outcome::moved, true);
    expect(server, down, Outcome::moved, true);
    for (int y : {world_min_y, world_max_y}) {
        REQUIRE(world.SetTile({0, 0, y}, {TestTerrain("stairs"), 0}));
        REQUIRE(world.MoveActor(server.GetPlayer(), {0, 0, y}));
        expect(server, y == world_min_y ? down : up, Outcome::unavailable, false);
        CHECK(server.View().player_position.y == y);
    }
}

TEST_CASE("Vertical interaction checks the shared face before exposing an object", "[server][3d][commands]") {
    Server server(TestContent());
    World& world = server.GetWorld();
    REQUIRE(world.SetTile({0, 0, 1}, {TestTerrain("stairs"), 0}));
    const auto object = take(world.CreateObject({0, 0, 1}));
    Command interact{CommandKind::interact};
    interact.dy = 1;
    expect(server, interact, Outcome::blocked, false);
    CHECK_FALSE(world.GetObject(object)->enabled);
    REQUIRE(world.SetTile({0, 0}, {TestTerrain("stairs"), 0}));
    expect(server, interact, Outcome::toggled, true);
    CHECK(world.GetObject(object)->enabled);
    REQUIRE(world.SetTile({0, 0, 1}, {TestTerrain("air"), 0}));
    expect(server, interact, Outcome::toggled, true); // Upper air opens its bottom.
    CHECK_FALSE(world.GetObject(object)->enabled);
}

TEST_CASE("Downward projection uses straight spherical LOS and isolates lower items", "[server][3d][view]") {
    Server server(TestContent());
    World& world = server.GetWorld();
    REQUIRE(world.DestroyObject(world.ObjectAt({2, 0})));
    for (int x = 1; x <= 2; ++x) {
        REQUIRE(world.SetTile({x, 0, -1}, {TestTerrain("floor"), 41}));
        REQUIRE(world.SetTile({x, 0, 0}, {TestTerrain("air"), 0}));
    }
    const auto rock = take(world.CreateItem(TestItem("rock"), ItemLocation::Ground({2, 0, -1})));
    const auto id = world.GetItem(rock)->id;
    const auto old = server.View(4);
    const auto& below = cell_at(old, {2, 0});
    CHECK(below.visible);
    CHECK(below.tile == Tile{TestTerrain("floor"), 41});
    CHECK(below.terrainDepth == 1);
    CHECK(below.overlayDepth == 1);
    CHECK(below.ground_items == 1); // Original rock fell into the other hole, at x=1.
    CHECK(world.ItemsAt({2, 0, -1}).size() == below.ground_items);
    expect(server, {CommandKind::pick_up, 0, 0, id}, Outcome::nothing_here, false);
    CHECK_FALSE(world.CanAccessItem(server.GetPlayer(), rock));
    CHECK(server.View().groundItems[0].id != id);
    CHECK(cell_at(server.View(2), {2, 0}).terrainDepth == 0); // sqrt(5) exceeds radius 2.
    REQUIRE(world.SetTile({1, 0, 0}, {TestTerrain("floor"), 0}));
    const auto closed = server.View(4);
    CHECK(cell_at(closed, {2, 0}).tile.terrain == TestTerrain("air"));
    CHECK(cell_at(closed, {2, 0}).ground_items == 0);
    CHECK(cell_at(closed, {2, 0}).terrainDepth == 0);
    CHECK(cell_at(old, {2, 0}).terrainDepth == 1); // Value snapshot remains isolated.
    REQUIRE(world.SetTile({1, 0, 0}, {TestTerrain("wall"), 0}));
    CHECK(cell_at(server.View(4), {2, 0}) == ViewCell{});
    CHECK(world.CheckInvariants());
}

TEST_CASE("Air retains a foreground actor over lower terrain and stops at the first floor", "[server][3d][view]") {
    Server server(TestContent());
    World& world = server.GetWorld();
    REQUIRE(world.DestroyObject(world.ObjectAt({2, 0})));
    for (int x = 1; x <= 2; ++x) {
        REQUIRE(world.SetTile({x, 0, -1}, {TestTerrain("floor"), 19}));
        REQUIRE(world.SetTile({x, 0, 0}, {TestTerrain("air"), 0}));
    }
    const auto support = take(world.CreateActor({2, 0, -1}));
    const auto foreground = take(world.CreateActor({2, 0, 0}));
    REQUIRE(world.SetTile({2, 0, -2}, {TestTerrain("floor"), 0}));
    take(world.CreateItem(TestItem("rock"), ItemLocation::Ground({2, 0, -2})));
    const auto foregroundView = server.View(4);
    const auto& cell = cell_at(foregroundView, {2, 0});
    CHECK(cell.tile == Tile{TestTerrain("floor"), 19});
    CHECK(cell.terrainDepth == 1);
    CHECK(cell.actor == world.GetActor(foreground)->id);
    CHECK(cell.overlayDepth == 0);
    CHECK(cell.ground_items == 0);
    REQUIRE(world.DestroyActor(foreground));
    const auto view = server.View(4);
    CHECK(cell_at(view, {2, 0}).actor == world.GetActor(support)->id);
    CHECK(cell_at(view, {2, 0}).overlayDepth == 1);
    CHECK(cell_at(view, {2, 0}).ground_items == 0); // Opaque-bottom floor stops deeper content.
}

TEST_CASE("Voxel corner ties conservatively block walls and closed floor edges", "[server][3d][view]") {
    Server server(TestContent());
    World& world = server.GetWorld();
    REQUIRE(world.SetTile({1, 1}, {TestTerrain("floor"), 0}));
    REQUIRE(world.SetTile({0, 1}, {TestTerrain("floor"), 0}));
    REQUIRE(world.SetTile({1, 0}, {TestTerrain("wall"), 0}));
    CHECK(cell_at(server.View(4), {1, 1}) == ViewCell{});
    REQUIRE(world.SetTile({1, 0, -1}, {TestTerrain("floor"), 11}));
    REQUIRE(world.SetTile({1, 0}, {TestTerrain("air"), 0}));
    // The exact diagonal to (1,0,-1) touches the origin's closed bottom edge.
    const auto view = server.View(4);
    CHECK(cell_at(view, {1, 0}).terrainDepth == 0);
    CHECK(cell_at(view, {1, 0}).ground_items == 0);
}

TEST_CASE("Local server command replay returns identical outcomes and snapshots", "[server][determinism]") {
    Server first(TestContent(), 4321);
    Server second(TestContent(), 4321);
    const Command commands[] = {
        {CommandKind::pick_up}, {CommandKind::wait}, {CommandKind::move, 1, 0},
        {CommandKind::pick_up}, {CommandKind::interact, 1, 0}, {CommandKind::move, 1, 0},
        {CommandKind::move, 1, 0}, {CommandKind::interact, 1, 0}, {CommandKind::move, 1, 0},
        {CommandKind::move, 0, 1}, {CommandKind::drop}, {CommandKind::wait},
        {CommandKind::move, 1, 1}, {CommandKind::pick_up}, {CommandKind::wait}
    };
    for (Command command : commands) {
        const auto a = first.Send(command);
        const auto b = second.Send(command);
        CHECK(a.outcome == b.outcome);
        CHECK(a.advanced == b.advanced);
        CHECK(a.tick == b.tick);
        same_view(first.View(), second.View());
        CHECK(actors(first.GetWorld()) == actors(second.GetWorld()));
    }
    CHECK(first.GetWorld().CheckInvariants());
    CHECK(second.GetWorld().CheckInvariants());
}

}
