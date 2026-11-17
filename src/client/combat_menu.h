#pragma once

#include "core/server.h"

namespace cataclysmx {

struct CombatMenuEntry {
    String label;
    String details;
    Command command;
    bool enabled = true;
    AbilityTargetKind targeting = AbilityTargetKind::Self;
};

// Snapshot-only presentation. Reuses output storage; unavailable choices stay visible.
// Target adapters replace command.ability.target, preserving ability and source.
void CollectCombatMenu(const ContentCatalog& content, const WorldView& view,
    Vector<CombatMenuEntry>& rows);
String CombatDetailsText(const ContentCatalog& content, const WorldView& view);
String CombatRefusalText(CombatRefusal reason);
String CombatEventText(const CombatEvent& event, const ContentCatalog& content);

}
