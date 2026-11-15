#include "core/server.h"
#include "core/first_house_scenario.h"
#include "worldgen/worldgen.h"

#include <catch2/catch_test_macros.hpp>
#include <EASTL/array.h>
#include <cstdlib>
#include <initializer_list>

namespace {
using namespace cataclysmx;

template<class T, class E> T Take(eastl::expected<T, E> value) {
    REQUIRE(value);
    return eastl::move(*value);
}

Command Action(InteractionRequest request) {
    Command command;
    command.kind = CommandKind::interact_action;
    command.interaction = request;
    REQUIRE(ValidCommandShape(command));
    return command;
}

InteractionChoice Choice(Server& server, TileCoord position, InteractionKind kind,
                         ObjectAction action = ObjectAction::Start) {
    Vector<InteractionChoice> choices;
    QueryInteractions(server.GetWorld(), server.GetPlayer(), position, choices);
    for (const auto& choice : choices) {
        if (choice.request.kind == kind && choice.request.action == action) { return choice; }
    }
    FAIL("Expected interaction was not discovered");
}

// Read resident geometry to plan a bounded cardinal path, then execute every
// door transition and step through Server. No actor relocation is used.
void WalkTo(Server& server, TileCoord goal, TileRect bounds) {
    auto& world = server.GetWorld();
    struct Node { TileCoord position; std::size_t parent; };
    Vector<Node> nodes;
    nodes.push_back({world.GetActor(server.GetPlayer())->position, 0});
    REQUIRE(nodes.front().position.y == goal.y);
    constexpr TileCoord directions[] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
    std::size_t destination = 0;
    bool found = false;
    for (std::size_t head = 0; head < nodes.size() && !found; ++head) {
        const auto from = nodes[head].position;
        if (from == goal) { destination = head; found = true; break; }
        for (const auto d : directions) {
            const TileCoord next{from.x + d.x, from.z + d.z, from.y};
            if (!Contains(bounds, next) || !world.IsActive(next)) { continue; }
            bool seen = false;
            for (const auto& node : nodes) { if (node.position == next) { seen = true; break; } }
            if (seen || world.ActorAt(next).value) { continue; }
            const auto* tile = world.GetTile(next);
            if (!tile) { continue; }
            const auto& terrain = world.GetContent().GetTerrain(tile->terrain);
            const auto* opened = world.GetContent().TryGetTerrain(terrain.opensTo);
            if (terrain.openSpace || (!terrain.passable && (!opened || !opened->passable))) { continue; }
            const auto* object = world.GetObject(world.ObjectAt(next));
            if (object && object->type.value && world.GetContent().GetObject(object->type).blocksMovement) { continue; }
            nodes.push_back({next, head});
        }
    }
    REQUIRE(found);
    Vector<TileCoord> reverse;
    for (auto index = destination; index != 0; index = nodes[index].parent) {
        reverse.push_back(nodes[index].position);
    }
    for (auto i = reverse.size(); i != 0; --i) {
        const auto next = reverse[i - 1];
        const auto from = world.GetActor(server.GetPlayer())->position;
        if (world.GetContent().GetTerrain(world.GetTile(next)->terrain).opensTo.value) {
            const auto open = Choice(server, next, InteractionKind::Open);
            REQUIRE(open.reason == ObjectUseReason::None);
            REQUIRE(server.Send(Action(open.request)).outcome == Outcome::opened);
        }
        Command move;
        move.kind = CommandKind::move;
        move.dx = next.x - from.x;
        move.dz = next.z - from.z;
        REQUIRE(std::abs(move.dx) + std::abs(move.dz) == 1);
        const auto result = server.Send(move);
        REQUIRE(result.outcome == Outcome::moved);
        REQUIRE(result.advanced);
        REQUIRE(world.GetActor(server.GetPlayer())->position == next);
    }
}

TileCoord Position(const BuildingPattern& pattern, const BuildingPlan& building, TileCoord local) {
    int width = pattern.width;
    int height = pattern.height;
    for (int i = 0; i < building.quarterTurns; ++i) {
        const int oldX = local.x;
        local.x = height - 1 - local.z;
        local.z = oldX;
        eastl::swap(width, height);
    }
    return {building.bounds.min.x + local.x, building.bounds.min.z + local.z, local.y};
}

TEST_CASE("Bundled first-house Server starts at the scenario result and traverses both basements", "[house][server][scenario]") {
    Diagnostics diagnostics;
    auto loaded = LoadContentPack(CATACLYSMX_CONTENT_PACK, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str());
    REQUIRE(loaded);
    const auto& content = *loaded;
    REQUIRE(ValidateServerContent(content));
    const auto& pattern = content.GetBuildingPattern(content.FindBuildingPattern("furnished_house"));
    REQUIRE(pattern.basements.size() == 2);
    eastl::array<bool, 2> visited{};
    // Review seeds: workshop and storage respectively. No combat or particular
    // monster chance roll is needed to exercise the matched stairs.
    for (const std::uint32_t seed : {0u, 13u}) {
        const auto start = Take(FindFirstHouseStart(content, seed));
        const auto plan = GenerateRegionPlan(content, seed, RegionOf(start.entrance));
        BuildingPlan building;
        bool matched = false;
        for (const auto& candidate : plan.buildings) {
            if (candidate.entrance == start.entrance && candidate.pattern == content.FindBuildingPattern("furnished_house")) {
                building = candidate; matched = true; break;
            }
        }
        REQUIRE(matched);
        REQUIRE(building.basementVariant >= 1);
        REQUIRE(building.basementVariant <= 2);
        const auto variantIndex = building.basementVariant - 1;
        if (visited[variantIndex]) { continue; }
        INFO("seed=" << seed << " basement=" << pattern.basements[variantIndex].id.c_str());
        Server server(content, seed, NewGameScenario::FirstHouse);
        auto& world = server.GetWorld();
        CHECK(server.GetTick() == 0);
        CHECK(world.GetActor(server.GetPlayer())->position == start.playerPosition);
        CHECK(server.View(0).player_position == start.playerPosition);
        CHECK(world.IsActive(start.playerPosition));
        REQUIRE(world.GetTile(start.entrance));
        CHECK(world.GetTile(start.entrance)->terrain == content.FindTerrain("closed_door"));
        CHECK_FALSE(world.ObjectAt(start.playerPosition).value);
        REQUIRE(std::abs(start.playerPosition.x - start.entrance.x)
            + std::abs(start.playerPosition.z - start.entrance.z) == 1);

        const TileRect walkingBounds{{building.bounds.min.x - 2, building.bounds.min.z - 2},
            {building.bounds.max.x + 2, building.bounds.max.z + 2}};
        WalkTo(server, start.entrance, walkingBounds);
        CHECK(world.GetTile(start.entrance)->terrain == content.FindTerrain("open_door"));
        const auto stairs = Position(pattern, building, {3, 6, 0});
        WalkTo(server, stairs, walkingBounds);
        REQUIRE(content.GetTerrain(world.GetTile(stairs)->terrain).stairs);
        Command down;
        down.kind = CommandKind::move;
        down.dy = -1;
        REQUIRE(server.Send(down).outcome == Outcome::moved);
        const TileCoord below{stairs.x, stairs.z, -1};
        CHECK(world.GetActor(server.GetPlayer())->position == below);
        REQUIRE(content.GetTerrain(world.GetTile(below)->terrain).stairs);
        const auto storagePosition = Position(pattern, building, {1, 1, -1});
        const auto storage = world.ObjectAt(storagePosition);
        REQUIRE(storage.value != 0);
        const auto& variant = pattern.basements[variantIndex];
        CHECK(world.GetObject(storage)->type == content.FindObject(
            variant.id == "storage_cellar" ? "cellar_chest" : "workshop_bench"));
        REQUIRE_FALSE(world.GetObject(storage)->contents.empty());

        if (variant.id == "storage_cellar") {
            WalkTo(server, Position(pattern, building, {2, 1, -1}), walkingBounds);
            const auto contents = world.GetObject(storage)->contents;
            const auto collect = Choice(server, storagePosition, InteractionKind::Object, ObjectAction::Collect);
            CHECK(collect.target == "storage chest");
            CHECK(collect.label == "Take contents");
            REQUIRE(collect.reason == ObjectUseReason::None);
            const auto taken = server.Send(Action(collect.request));
            CHECK(taken.outcome == Outcome::toggled);
            CHECK(taken.advanced);
            CHECK(world.GetObject(storage)->contents.empty());
            for (const auto item : contents) {
                REQUIRE(world.GetItem(item));
                CHECK(world.GetItem(item)->location == ItemLocation::Carried(server.GetPlayer()));
            }
            WalkTo(server, below, walkingBounds);
        }
        Command up;
        up.kind = CommandKind::move;
        up.dy = 1;
        REQUIRE(server.Send(up).outcome == Outcome::moved);
        CHECK(world.GetActor(server.GetPlayer())->position == stairs);
        REQUIRE(world.CheckInvariants());
        visited[variantIndex] = true;
    }
    CHECK(visited[0]);
    CHECK(visited[1]);
}

ContentCatalog RuntimeContent() {
    // Standalone source includes the complete demo-constructor contract. This
    // avoids copying/merging immutable catalogs or accidentally loading a manifest
    // as a definition document.
    const ContentSource source{"house-server.json5", R"({format_version:1,objects:[
      {id:'cabinet',name:'Test cabinet',behavior:'none',capacity_volume_ml:1000,blocks_movement:true,movable:true}
    ],definitions:[
      {type:'terrain',id:'floor',name:'Floor',movement:{passable:true},blocks_sight:false,appearance:{symbol:'.',foreground:'white'}},
      {type:'terrain',id:'wall',name:'Wall',movement:{passable:false},blocks_sight:true,appearance:{symbol:'#',foreground:'white'}},
      {type:'terrain',id:'air',name:'Air',movement:{passable:true},open_space:true,blocks_sight:false,looks_like:'floor'},
      {type:'terrain',id:'closed_door',name:'Door',movement:{passable:false},blocks_sight:true,opens_to:'open_door',looks_like:'wall'},
      {type:'terrain',id:'open_door',name:'Open door',movement:{passable:true},blocks_sight:false,closes_to:'closed_door',looks_like:'floor'},
      {type:'terrain',id:'stairs',name:'Stairs',movement:{passable:true},stairs:true,blocks_sight:false,looks_like:'floor'},
      {type:'item',id:'rock',name:'Rock',mass_g:10,volume_ml:20},
      {type:'item',id:'bandage',name:'Bandage',mass_g:2,volume_ml:5},
      {type:'item',id:'backpack',name:'Backpack',mass_g:10,volume_ml:10,capabilities:[{type:'container',id:'storage',capacity_volume_ml:100}]},
      {type:'item',id:'material',name:'Material',mass_g:1,volume_ml:40},
      {type:'item',id:'tool',name:'Tool',mass_g:1,volume_ml:10},
      {type:'item',id:'product',name:'Product',mass_g:1,volume_ml:10},
      {type:'recipe',id:'make',inputs:[{item:'material',count:1},{item:'tool',count:1,retained:true}],result:{item:'product',count:1},work:100}
    ]})"};
    Diagnostics diagnostics;
    auto content = LoadContent({&source, 1}, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str());
    REQUIRE(content);
    REQUIRE(ValidateServerContent(*content));
    return eastl::move(*content);
}

struct RuntimeRig {
    ContentCatalog content = RuntimeContent();
    Server server{content};
    RuntimeRig() {
        auto& world = server.GetWorld();
        Vector<ActorHandle> actors;
        world.CollectActiveActors(actors);
        for (const auto actor : actors) {
            if (actor != server.GetPlayer()) { REQUIRE(world.DestroyActor(actor)); }
        }
        // The demo's generic prop occupies the crafting/storage test tile.
        // Clear it explicitly when preparing this controlled runtime arena.
        const auto demoProp = world.ObjectAt({2, 0});
        REQUIRE(demoProp.value != 0);
        REQUIRE(world.DestroyObject(demoProp));
        REQUIRE(server.SetBubbleRadii(1, 2));
        for (int z = -2; z <= 0; ++z) {
            for (int x = 0; x <= 18; ++x) {
                REQUIRE(world.SetTile({x, z}, {content.FindTerrain("floor"), 0}));
            }
        }
    }
    ObjectHandle Cabinet(TileCoord position) {
        return Take(server.GetWorld().CreateObject(content.FindObject("cabinet"), position));
    }
};

CommandResult FreeRefusal(Server& server, Command command) {
    const auto tick = server.GetTick();
    const auto turns = server.GetWorld().GetActor(server.GetPlayer())->turns;
    const auto result = server.Send(command);
    CHECK_FALSE(result.advanced);
    CHECK(result.cost == 0);
    CHECK(server.GetTick() == tick);
    CHECK(server.GetWorld().GetActor(server.GetPlayer())->turns == turns);
    CHECK(server.GetWorld().GetActorRemainingTicks(server.GetPlayer()) == 0);
    return result;
}

TEST_CASE("Server push and pull charge once and reconcile the post-move chunk bubble", "[house][server][movement]") {
    RuntimeRig r;
    auto& world = r.server.GetWorld();
    WalkTo(r.server, {14, 0}, {{0, -2}, {19, 1}});
    REQUIRE(world.SetActorSpeed(r.server.GetPlayer(), 125));
    const auto cabinet = r.Cabinet({15, 0});
    const auto item = Take(world.CreateItem(r.content.FindItem("material"), ItemLocation::Stored(cabinet)));
    const auto itemId = world.GetItem(item)->id;
    const auto objectId = world.GetObject(cabinet)->id;
    const auto stale = Choice(r.server, {15, 0}, InteractionKind::PushObject).request;
    const auto relocate = [&](InteractionKind kind, TileCoord actorPosition, TileCoord objectPosition) {
        const auto choice = Choice(r.server, world.GetObject(cabinet)->position, kind);
        CHECK(choice.target == "Test cabinet");
        CHECK(choice.label == (kind == InteractionKind::PushObject ? "Push" : "Pull"));
        REQUIRE(choice.reason == ObjectUseReason::None);
        CHECK(choice.request.object == objectId);
        CHECK(choice.request.terrain.value == 0);
        CHECK(choice.request.action == ObjectAction::Start);
        const auto tick = r.server.GetTick();
        const auto turns = world.GetActor(r.server.GetPlayer())->turns;
        const Tick cost = ActionTicks(ticks_per_turn, world.GetActor(r.server.GetPlayer())->speed);
        const auto result = r.server.Send(Action(choice.request));
        REQUIRE(result.outcome == Outcome::object_moved);
        CHECK(result.advanced);
        CHECK(result.cost == cost);
        CHECK(r.server.GetTick() == tick + cost);
        CHECK(world.GetActor(r.server.GetPlayer())->turns == turns + 1);
        CHECK(world.GetActorRemainingTicks(r.server.GetPlayer()) == 0);
        CHECK(world.GetActor(r.server.GetPlayer())->position == actorPosition);
        CHECK(world.GetObject(cabinet)->position == objectPosition);
        CHECK(world.ActorAt(actorPosition) == r.server.GetPlayer());
        CHECK(world.ObjectAt(objectPosition) == cabinet);
        CHECK(world.GetItem(item)->id == itemId);
        CHECK(world.GetItem(item)->location == ItemLocation::Stored(cabinet));
        unsigned memberships = 0;
        for (const auto chunk : world.LoadedChunks()) {
            for (const auto handle : world.GetChunk(chunk)->objects) {
                if (handle == cabinet) { CHECK(chunk == ChunkOf(objectPosition)); ++memberships; }
            }
        }
        CHECK(memberships == 1);
        CHECK(world.IsActive({32, 0}) == (actorPosition.x >= chunk_size));
        CHECK(world.IsActive({-16, 0}) == (actorPosition.x < chunk_size));
        CHECK((world.GetChunk({3, 0, 0}) != nullptr) == (actorPosition.x >= chunk_size));
        CHECK((world.GetChunk({-2, 0, 0}) != nullptr) == (actorPosition.x < chunk_size));
        REQUIRE(world.CheckInvariants());
    };
    relocate(InteractionKind::PushObject, {15, 0}, {16, 0});
    // Position changed but persistent ID is still live.
    CHECK(FreeRefusal(r.server, Action(stale)).outcome == Outcome::unavailable);
    relocate(InteractionKind::PushObject, {16, 0}, {17, 0});
    relocate(InteractionKind::PullObject, {15, 0}, {16, 0});
    relocate(InteractionKind::PullObject, {14, 0}, {15, 0});
    REQUIRE(world.DestroyObject(cabinet));
    const auto replacement = r.Cabinet({15, 0});
    CHECK(world.GetObject(replacement)->id != objectId);
    // Same position now belongs to a different object.
    CHECK(FreeRefusal(r.server, Action(stale)).outcome == Outcome::unavailable);
    CHECK(world.GetObject(replacement)->position == TileCoord{15, 0});
    REQUIRE(world.CheckInvariants());
}

TEST_CASE("Server rejects diagonal furniture movement without spending time", "[house][server][movement]") {
    RuntimeRig r;
    auto& world = r.server.GetWorld();
    const auto cabinet = r.Cabinet({1, -1});
    for (const auto kind : {InteractionKind::PushObject, InteractionKind::PullObject}) {
        const InteractionRequest request{{1, -1}, world.GetObject(cabinet)->id, {}, kind};
        FreeRefusal(r.server, Action(request));
        CHECK(world.GetActor(r.server.GetPlayer())->position == TileCoord{0, 0});
        CHECK(world.GetObject(cabinet)->position == TileCoord{1, -1});
    }
    REQUIRE(world.CheckInvariants());
}

TEST_CASE("Furniture in an open doorway refuses Close until the doorway is clear", "[house][server][doors]") {
    RuntimeRig r;
    auto& world = r.server.GetWorld();
    const TileCoord doorway{2, 0};
    REQUIRE(world.SetTile(doorway, {r.content.FindTerrain("open_door"), 0}));
    const auto cabinet = r.Cabinet({1, 0});
    const auto push = Choice(r.server, {1, 0}, InteractionKind::PushObject);
    REQUIRE(r.server.Send(Action(push.request)).outcome == Outcome::object_moved);
    REQUIRE(world.GetObject(cabinet)->position == doorway);
    WalkTo(r.server, {2, -1}, {{0, -2}, {4, 1}});
    CHECK_FALSE(world.ActorAt(doorway).value);

    const auto close = Choice(r.server, doorway, InteractionKind::Close);
    CHECK(close.reason == ObjectUseReason::Occupied);
    CHECK(FreeRefusal(r.server, Action(close.request)).outcome == Outcome::blocked);
    Command directional;
    directional.kind = CommandKind::interact;
    directional.dz = 1;
    CHECK(FreeRefusal(r.server, directional).outcome == Outcome::occupied);
    CHECK(world.GetTile(doorway)->terrain == r.content.FindTerrain("open_door"));
    CHECK(world.GetObject(cabinet)->position == doorway);
    REQUIRE(world.CheckInvariants());

    // Push the cabinet through the door, then leave the doorway with an ordinary
    // move so neither furniture nor the player remains as a closing obstruction.
    WalkTo(r.server, {1, 0}, {{0, -2}, {4, 1}});
    const auto clear = Choice(r.server, doorway, InteractionKind::PushObject);
    REQUIRE(r.server.Send(Action(clear.request)).outcome == Outcome::object_moved);
    REQUIRE(world.GetActor(r.server.GetPlayer())->position == doorway);
    REQUIRE(world.GetObject(cabinet)->position == TileCoord{3, 0});
    WalkTo(r.server, {2, -1}, {{0, -2}, {4, 1}});
    const auto available = Choice(r.server, doorway, InteractionKind::Close);
    REQUIRE(available.reason == ObjectUseReason::None);
    const auto tick = r.server.GetTick();
    const auto turns = world.GetActor(r.server.GetPlayer())->turns;
    CommandResult closed;
    SECTION("Explicit Close revalidates the previously refused request") {
        closed = r.server.Send(Action(close.request));
    }
    SECTION("Directional interaction closes the now unoccupied doorway") {
        closed = r.server.Send(directional);
    }
    CHECK(closed.outcome == Outcome::closed);
    CHECK(closed.advanced);
    CHECK(closed.cost == ActionTicks(ticks_per_turn, world.GetActor(r.server.GetPlayer())->speed));
    CHECK(r.server.GetTick() == tick + closed.cost);
    CHECK(world.GetActor(r.server.GetPlayer())->turns == turns + 1);
    CHECK(world.GetTile(doorway)->terrain == r.content.FindTerrain("closed_door"));
    CHECK(world.GetObject(cabinet)->position == TileCoord{3, 0});
    REQUIRE(world.CheckInvariants());
}

TEST_CASE("Server exposes storage Collect and crafts directly from nearby cabinet contents", "[house][server][storage][crafting]") {
    RuntimeRig r;
    auto& world = r.server.GetWorld();
    SECTION("Collect transfers contents once and an empty retry is free") {
        const auto cabinet = r.Cabinet({1, 0});
        const auto material = Take(world.CreateItem(r.content.FindItem("material"), ItemLocation::Stored(cabinet)));
        const auto collect = Choice(r.server, {1, 0}, InteractionKind::Object, ObjectAction::Collect);
        CHECK(collect.label == "Take contents");
        CHECK(collect.target == "Test cabinet");
        REQUIRE(collect.reason == ObjectUseReason::None);
        const auto tick = r.server.GetTick();
        const auto turns = world.GetActor(r.server.GetPlayer())->turns;
        const auto result = r.server.Send(Action(collect.request));
        CHECK(result.outcome == Outcome::toggled);
        CHECK(result.advanced);
        CHECK(result.cost == ActionTicks(ticks_per_turn, world.GetActor(r.server.GetPlayer())->speed));
        CHECK(r.server.GetTick() == tick + result.cost);
        CHECK(world.GetActor(r.server.GetPlayer())->turns == turns + 1);
        CHECK(world.GetItem(material)->location == ItemLocation::Carried(r.server.GetPlayer()));
        CHECK(world.GetObject(cabinet)->contents.empty());
        const auto empty = Choice(r.server, {1, 0}, InteractionKind::Object, ObjectAction::Collect);
        CHECK(empty.reason == ObjectUseReason::NoContents);
        FreeRefusal(r.server, Action(collect.request));
    }
    SECTION("Crafting reaches beyond immediate Collect range without first taking ingredients") {
        const auto cabinet = r.Cabinet({2, 0});
        const auto material = Take(world.CreateItem(r.content.FindItem("material"), ItemLocation::Stored(cabinet)));
        const auto tool = Take(world.CreateItem(r.content.FindItem("tool"), ItemLocation::Stored(cabinet)));
        REQUIRE(world.SetCraftingSettings({3, 100}));
        CHECK_FALSE(world.CanAccessObjectStorage(r.server.GetPlayer(), cabinet));
        CHECK_FALSE(world.CanAccessItem(r.server.GetPlayer(), material));
        auto pacing = r.server.GetPacing();
        pacing.rateLimited = true;
        pacing.ticksPerSecond = 100;
        REQUIRE(r.server.SetPacing(pacing));
        Command craft;
        craft.kind = CommandKind::craft;
        craft.recipe = r.content.FindCraftingRecipe("make");
        const auto started = r.server.Send(craft);
        REQUIRE(started.outcome == Outcome::craft_started);
        CHECK(started.advanced);
        CHECK(started.cost == 100);
        REQUIRE(r.server.View(0).craftingJobs.size() == 1);
        const auto unfinished = world.FindItem(r.server.View(0).craftingJobs.front().id);
        REQUIRE(unfinished.value != 0);
        CHECK(world.GetItem(material)->location == ItemLocation::Inside(unfinished));
        CHECK(world.GetItem(tool)->location == ItemLocation::Stored(cabinet));
        r.server.Update(1'000'000);
        CHECK(r.server.View(0).craftingJobs.empty());
        CHECK_FALSE(world.GetItem(material));
        CHECK(world.GetItem(tool)->location == ItemLocation::Stored(cabinet));
        unsigned products = 0;
        for (const auto item : world.GetActor(r.server.GetPlayer())->inventory) {
            if (world.GetItem(item)->type == r.content.FindItem("product")) { ++products; }
        }
        CHECK(products == 1);
    }
    REQUIRE(world.CheckInvariants());
}
}
