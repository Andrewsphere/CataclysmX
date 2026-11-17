#pragma once

#include "combat/combat_types.h"
#include "core/diagnostics.h"

namespace cataclysmx {

struct WorldView;
const char* CombatRegionName(ContactRegion region);
String CombatEventText(const CombatEvent& event, const ContentCatalog& content);
// Empty strings mean the corresponding presentation data is unavailable.
String CombatStatusText(const CombatView& view);
String CombatStatusText(const WorldView& view, const ContentCatalog& content);
String CombatFeedbackText(const CombatView& view);

}
