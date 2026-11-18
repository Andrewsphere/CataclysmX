#pragma once

#include "sdl/text.h"
#include "combat/combat_types.h"
#include "client/localization.h"

namespace cataclysmx {
struct EnemyHealthView;
struct ExaminedEnemy;
}

namespace cataclysmx::sdl {

struct ScreenFrame;
RgbColor HealthColor(const CombatPartView& part);
String HealthPartText(BodyPart part, const CombatPartView& state, const Localization* localization = nullptr);
String CombatSummaryText(const CombatView& view);
void AppendEnemyExamine(const ExaminedEnemy& enemy, const Localization& localization, ScreenFrame& frame);
// SDL HUD only. usedHeight is zero when no body data is present.
bool DrawHealth(SDL_Renderer* renderer, SdlText& text, const CombatView& view,
    SDL_Rect bounds, int& usedHeight, const Localization* localization = nullptr);
bool DrawEnemyTileHealth(SDL_Renderer* renderer, SDL_Rect tile, const EnemyHealthView& health);

}
