#pragma once

#include "content_fixture.h"
#include "content/combat_definition.h"
#include "combat/combat_types.h"

namespace cataclysmx {
// Typed, deterministic repertoire for timing/lifetime regression tests. Configure
// the owned catalog before constructing World; production catalogs stay immutable.
inline void ConfigureCombatRuntimeTestContent(ContentCatalog& content) {
    const auto edit = [&](const char* name) -> CombatMoveDefinition& {
        return const_cast<CombatMoveDefinition&>(content.GetCombatMove(content.FindCombatMove(name)));
    };
    auto& fist = edit("basic_fist");
    fist.cost = {0, 100, 0}; fist.strikes[0].intrinsicMiss = 0; fist.strikes[0].damageRatio = ratioScale;
    auto& natural = edit("basic_natural");
    natural.cost = {0, 100, 0}; natural.strikes[0].intrinsicMiss = 0;
    auto& heavy = edit("basic_heavy");
    heavy.requirements.primary = {AttackSourceKind::EmptyHand, SourceHand::Both};
    heavy.cost = {80, 120, 20}; heavy.cooldown = 400;
    heavy.strikes[0].intrinsicMiss = 0; heavy.strikes[0].damageRatio = 20000;
    auto& parry = edit("basic_parry");
    parry.requirements.primary = {AttackSourceKind::EmptyHand};
    parry.cost = {0, 100, 10}; parry.cooldown = 100; parry.followThrough.counterOpening = 0;
    for (const auto legacy : {CombatStyle::Balanced, CombatStyle::Aggressive, CombatStyle::Defensive}) {
        const char* name = legacy == CombatStyle::Balanced ? "basic_balanced" : legacy == CombatStyle::Aggressive ? "basic_aggressive" : "basic_defensive";
        auto& style = const_cast<CombatStyleDefinition&>(content.GetCombatStyle(content.FindCombatStyle(name)));
        style.moves.clear();
        style.moves.push_back({content.FindCombatMove(legacy == CombatStyle::Aggressive ? "basic_heavy" : "basic_fist"), 1});
        style.moves.push_back({content.FindCombatMove("basic_natural"), 1});
        if (legacy == CombatStyle::Defensive) { style.moves.push_back({content.FindCombatMove("basic_parry"), 1}); }
    }
}
inline ContentCatalog CombatRuntimeTestContent() {
    auto content = TestContent();
    ConfigureCombatRuntimeTestContent(content);
    return content;
}
}
