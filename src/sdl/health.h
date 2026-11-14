#pragma once

#include "sdl/text.h"
#include "combat/combat_types.h"
#include "client/localization.h"

namespace cataclysmx::sdl {

RgbColor HealthColor(const CombatPartView& part);
String HealthPartText(BodyPart part, const CombatPartView& state, const Localization* localization = nullptr);
String CombatSummaryText(const CombatView& view);
// SDL HUD only. usedHeight is zero when no body data is present.
bool DrawHealth(SDL_Renderer* renderer, SdlText& text, const CombatView& view,
    SDL_Rect bounds, int& usedHeight, const Localization* localization = nullptr);

}
