#include "client/ai_debug.h"

#include "actors/ai_controller.h"
#include "client/localization.h"
#include "combat/body.h"
#include "core/world.h"
#include <EASTL/algorithm.h>

namespace cataclysmx {
namespace {

String Position(TileCoord p) {
    return NumberText(p.x) + "," + NumberText(p.z) + "," + NumberText(p.y);
}

const char* Behavior(AiBehavior behavior) {
    switch (behavior) {
    case AiBehavior::Idle: return "Idle";
    case AiBehavior::Pursue: return "Pursue";
    case AiBehavior::Search: return "Search";
    case AiBehavior::Attack: return "Attack";
    }
    return "Unknown";
}

const char* Intent(ActorIntentKind intent) {
    switch (intent) {
    case ActorIntentKind::Wait: return "Wait";
    case ActorIntentKind::Move: return "Move";
    case ActorIntentKind::Attack: return "Attack";
    }
    return "Unknown";
}

const char* Reason(AiMoveStatus status) {
    switch (status) {
    case AiMoveStatus::Available: return "eligible";
    case AiMoveStatus::Unavailable: return "unavailable tile";
    case AiMoveStatus::Inactive: return "inactive tile";
    case AiMoveStatus::Impassable: return "impassable";
    case AiMoveStatus::Unsupported: return "unsupported/open space";
    case AiMoveStatus::Occupied: return "occupied";
    case AiMoveStatus::StairsRequired: return "stairs required";
    case AiMoveStatus::Backtracking: return "previous position";
    case AiMoveStatus::OutsideSearch: return "outside search";
    }
    return "Unknown";
}

TileCoord Add(TileCoord p, TileCoord offset) {
    return {p.x + offset.x, p.z + offset.z, p.y + offset.y};
}

void Mark(DebugMapView& image, TileCoord p, RgbColor background) {
    const int x = p.x - image.bounds.min.x;
    const int z = p.z - image.bounds.min.z;
    if (p.y != image.bounds.min.y || x < 0 || z < 0 || x >= image.width || z >= image.height) { return; }
    auto& cell = image.cells[static_cast<std::size_t>(z * image.width + x)];
    cell.background = background;
    cell.foreground = {255, 255, 255};
}

}

void CaptureAiDebugView(const World& world, TileCoord cursor, AiDebugView& output) {
    cursor.x = eastl::clamp(cursor.x, -world_coordinate_limit, world_coordinate_limit);
    cursor.z = eastl::clamp(cursor.z, -world_coordinate_limit, world_coordinate_limit);
    cursor.y = eastl::clamp(cursor.y, world_min_y, world_max_y);
    output.cursor = cursor;
    output.tick = world.GetTick();
    output.actor = 0;
    output.details.clear();
    auto& image = output.image;
    image.bounds = {{cursor.x - 8, cursor.z - 8, cursor.y}, {cursor.x + 9, cursor.z + 9, cursor.y}};
    image.scale = 1;
    image.width = 17;
    image.height = 17;
    image.cells.resize(17 * 17);
    for (int z = 0; z < image.height; ++z) {
        for (int x = 0; x < image.width; ++x) {
            const TileCoord p{image.bounds.min.x + x, image.bounds.min.z + z, cursor.y};
            auto& cell = image.cells[static_cast<std::size_t>(z * image.width + x)];
            const auto* tile = world.GetTile(p);
            cell = tile ? world.GetContent().GetTerrain(tile->terrain).appearance
                : TerminalAppearance{"?", {80, 80, 80}, {0, 0, 0}};
            if (const auto* actor = world.GetActor(world.ActorAt(p))) {
                cell.symbol = actor->player ? "@" : actor->combat.faction == CombatFaction::Undead ? "Z" : "n";
                cell.foreground = actor->player ? RgbColor{255, 255, 255} : RgbColor{240, 160, 90};
            }
        }
    }
    auto& details = output.details;
    details.push_back("Tick: " + UnsignedText(output.tick) + " | Cursor X,Z,Y: " + Position(cursor));
    const auto handle = world.ActorAt(cursor);
    const auto* actor = world.GetActor(handle);
    if (!actor) {
        details.push_back(world.GetTile(cursor) ? "No actor here. Move the cursor onto an actor."
            : "Tile is not resident. Inspection does not load chunks.");
        return;
    }
    output.actor = actor->id;
    details.push_back("Actor: " + UnsignedText(actor->id) + (actor->player ? " (player)" : " (NPC)"));
    const auto controller = world.CaptureActorController(handle);
    const auto kind = controller.GetString("type");
    details.push_back("Controller: " + (kind ? String(kind->data(), kind->size()) : String("none")));
    if (const auto* state = controller.Find("state")) {
        if (const auto profile = state->GetString("profile")) {
            details.push_back("Profile: " + (profile->empty() ? String("intrinsic defaults") : String(profile->data(), profile->size())));
        }
        if (const auto remaining = state->GetUnsigned("search_remaining")) {
            details.push_back("Search remaining: " + UnsignedText(*remaining));
        }
    }
    details.push_back(world.IsActive(cursor) ? "Activity: active" : "Activity: resident-only (frozen)");
    details.push_back("Action remaining: " + UnsignedText(world.GetActorRemainingTicks(handle))
        + " | Speed factor: " + UnsignedText(actor->speed));
    const auto baseCost = LocomotionBaseCost(actor->combat.body);
    details.push_back("Move ticks now: cardinal " + UnsignedText(ActionTicks(baseCost, actor->speed))
        + " / diagonal " + UnsignedText(ActionTicks(ActorStepBaseCost(baseCost, {1, 1}), actor->speed)));
    const auto ai = world.InspectActorAi(handle);
    details.push_back(String("Behavior now: ") + Behavior(ai.behavior));
    details.push_back("Target memory: " + UnsignedText(ai.target) + " | remaining " + UnsignedText(ai.memoryRemaining));
    if (ai.target) {
        details.push_back("Last known: " + Position(ai.lastKnownPosition));
        details.push_back("Observed tick: " + UnsignedText(ai.observedTick) + " | age "
            + UnsignedText(output.tick >= ai.observedTick ? output.tick - ai.observedTick : 0));
        details.push_back(ai.targetVisible ? "Target seen on last decision" : "Target not seen on last decision");
    }
    details.push_back("Scan remaining: " + UnsignedText(ai.scanRemaining));
    const auto& decision = ai.decision;
    if (!decision.valid) {
        details.push_back("No decision recorded (new/restored controller or legacy AI).");
        return;
    }
    details.push_back("Last decision tick: " + UnsignedText(decision.tick) + " | origin " + Position(decision.origin));
    details.push_back(String("Decision: ") + Behavior(decision.behavior) + " -> " + Intent(decision.intent));
    if (decision.hasGoal) { details.push_back("Decision goal: " + Position(decision.goal)); }
    if (decision.hasPrevious) { details.push_back("Avoided previous position: " + Position(decision.previousPosition)); }
    details.push_back("Work last decision: candidates " + UnsignedText(ai.candidates) + " buckets " + UnsignedText(ai.buckets)
        + " rays " + UnsignedText(ai.rays) + " cells " + UnsignedText(ai.cells));
    details.push_back("Historical probes; map shows current resident terrain/actors.");
    details.push_back("Pursuit score = squared distance; lower wins, then stable ID tie order.");
    constexpr const char* names[] = {"E", "S", "W", "N", "SE", "SW", "NW", "NE", "Up", "Down"};
    for (std::size_t i = 0; i < actorStepDirections.size(); ++i) {
        const auto p = Add(decision.origin, actorStepDirections[i]);
        const bool chosen = decision.selectedMove == static_cast<int>(i);
        String line = chosen ? "* " : "  ";
        line += names[i];
        line += ": ";
        line += Reason(decision.moves[i]);
        if (decision.hasGoal) { line += " | distance2=" + UnsignedText(static_cast<std::uint64_t>(AiMovementScore(p, decision.goal))); }
        if (chosen) { line += " | CHOSEN"; }
        details.push_back(eastl::move(line));
        Mark(image, p, decision.moves[i] == AiMoveStatus::Available ? RgbColor{20, 95, 35}
            : decision.moves[i] == AiMoveStatus::Backtracking ? RgbColor{135, 75, 15} : RgbColor{110, 25, 25});
    }
    if (decision.hasGoal) { Mark(image, decision.goal, {105, 35, 130}); }
    Mark(image, decision.origin, {30, 60, 150});
    if (decision.selectedMove >= 0 && decision.selectedMove < static_cast<int>(actorStepDirections.size())) {
        Mark(image, Add(decision.origin, actorStepDirections[decision.selectedMove]), {150, 125, 10});
    }
}

}
