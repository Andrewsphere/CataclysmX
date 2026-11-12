#pragma once

#include "combat/combat_types.h"
#include "core/diagnostics.h"

namespace cataclysmx {

// Empty strings mean the corresponding presentation data is unavailable.
String CombatStatusText(const CombatView& view);
String CombatFeedbackText(const CombatView& view);

}
