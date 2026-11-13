#pragma once

#include "client/debug_map.h"
#include "core/timing.h"

namespace cataclysmx {

class World;

struct AiDebugView {
    TileCoord cursor{};
    Tick tick = 0;
    PersistentId actor = 0;
    DebugMapView image;
    Vector<String> details;
};

// Explicit local debug inspection of resident world state, not player knowledge.
// Captures a 17x17 current-plane map and the actor under the cursor. Never loads
// chunks, advances time, updates memory, or invokes an AI decision. Output owns
// all values; retained capacity is reused. Decision overlays describe the last
// actual decision and may precede the actor's displayed current position.
void CaptureAiDebugView(const World& world, TileCoord cursor, AiDebugView& output);

}
