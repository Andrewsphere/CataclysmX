#pragma once

#include "combat/action_types.h"
#include "skills/skill_types.h"
#include <EASTL/unique_ptr.h>

namespace cataclysmx {

struct ArtProgress {
    CombatArtId art{};
    std::uint64_t xp = 0; // 1/ratioScale XP, no separate art level.
    bool operator==(const ArtProgress&) const = default;
};
struct AbilityGrant {
    AbilityId ability{};
    AbilityGrantKey source;
    bool operator==(const AbilityGrant&) const = default;
};
struct AbilityCooldown {
    AbilityId ability{};
    AbilityGrantKey source;
    Tick remaining = 0;
    bool operator==(const AbilityCooldown&) const = default;
};
struct ArtManualRequest {
    BookId book{};
    CombatArtId art{};
    bool operator==(const ArtManualRequest&) const = default;
};
struct ManualAttemptProgress {
    BookId book{};
    CombatArtId art{};
    Tick remaining = 0;
    Tick totalDuration = 0;
    std::uint32_t successChance = 0;
    bool active = false;
    Tick fundedTicks = 0;
    bool pauseRequested = false;
    bool operator==(const ManualAttemptProgress&) const = default;
};
struct CharacterCombatState {
    Vector<ArtProgress> arts;
    Vector<AbilityGrant> abilityGrants;
    Vector<AbilityCooldown> abilityCooldowns;
    // Paused attempts retain progress; at most one active across all study kinds.
    Vector<ManualAttemptProgress> manuals;
    bool operator==(const CharacterCombatState&) const = default;
};

// Explicit deep snapshot operations; never compare unique_ptr addresses as state.
eastl::unique_ptr<CharacterCombatState> CloneCharacterCombat(const CharacterCombatState* state);
bool EqualCharacterCombat(const CharacterCombatState* a, const CharacterCombatState* b);

}
