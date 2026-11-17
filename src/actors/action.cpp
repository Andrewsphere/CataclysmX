#include "actors/action.h"
#include "actors/movement.h"

#include <cstdlib>

namespace cataclysmx {
namespace {

void Require(bool condition) {
    if (!condition) { std::abort(); }
}

}

std::uint32_t ActorStepBaseCost(std::uint32_t baseCost, TileCoord direction) {
    bool valid = false;
    for (const auto step : actorStepDirections) { valid = valid || direction == step; }
    Require(baseCost > 0 && valid);
    const std::uint64_t cost = direction.x != 0 && direction.z != 0
        ? (std::uint64_t{baseCost} * 141 + 99) / 100 : baseCost;
    Require(cost <= std::numeric_limits<std::uint32_t>::max());
    return static_cast<std::uint32_t>(cost);
}

bool ValidateAction(const ActorActionState& state) {
    if (state.definitionDriven) {
        if (state.ratingPenalty100 < 0 || state.accuracyPenalty > 30 || state.recoveryTicks == 0) { return false; }
        if (state.sources.primaryHand > Hand::Right || state.sources.primaryKind > AttackSourceKind::Natural ||
            state.sources.secondaryKind > AttackSourceKind::Natural ||
            static_cast<unsigned>(state.sources.functionalHands) > 3 || static_cast<unsigned>(state.sources.functionalLegs) > 3) { return false; }
        for (const auto& hand : state.sources.hands) {
            if (hand.grip > GripKind::TwoHanded || (!hand.required && hand.item != 0)) { return false; }
        }
        if (state.kind == ActorActionKind::AbilityWindup) { return state.ability.value != 0; }
        if (state.kind == ActorActionKind::MeleeWindup) { return state.moveId.value != 0 && state.target != 0 && state.ability.value == 0; }
        if (state.kind == ActorActionKind::Recovery) {
            return state.moveId.value != 0 || state.ability.value != 0;
        }
        return false;
    }
    if (state.kind != ActorActionKind::MeleeWindup && state.weapon != 0) { return false; }
    if (state.accuracyPenalty > 30) { return false; }
    switch (state.kind) {
    case ActorActionKind::None:
    case ActorActionKind::Recovery:
    case ActorActionKind::DisruptionRecovery:
        return state.target == 0 && state.move == 0 && state.recoveryTicks == 0
            && state.accuracyPenalty == 0;
    case ActorActionKind::Aiming:
        return state.target != 0 && state.move == 0 && state.recoveryTicks == 0
            && state.accuracyPenalty == 0;
    case ActorActionKind::MeleeWindup:
        return state.target != 0 && state.move != 0 && state.recoveryTicks != 0;
    case ActorActionKind::AbilityWindup:
        return false;
    }
    return false;
}

DisruptionResult ApplyDisruption(ActorActionState& state, const Disruption& disruption) {
    Require(ValidateAction(state));
    switch (disruption.cause) {
    case DisruptionCause::DamagingHit:
    case DisruptionCause::ForcedMovement:
    case DisruptionCause::RequiredCapabilityLost:
    case DisruptionCause::Stagger:
        break;
    default:
        std::abort();
    }

    switch (state.kind) {
    case ActorActionKind::None:
    case ActorActionKind::Recovery:
    case ActorActionKind::DisruptionRecovery:
        return {DisruptionResponse::Ignored, eastl::nullopt};
    case ActorActionKind::MeleeWindup:
        if (disruption.cause == DisruptionCause::DamagingHit
            || disruption.cause == DisruptionCause::ForcedMovement) {
            const std::uint8_t penalty = disruption.accuracyPenalty > 30 ? 30 : disruption.accuracyPenalty;
            if (penalty <= state.accuracyPenalty) {
                return {DisruptionResponse::Ignored, eastl::nullopt};
            }
            state.accuracyPenalty = penalty;
            if (state.definitionDriven) {
                state.ratingPenalty100 = static_cast<std::int32_t>(penalty) * 10;
            }
            return {DisruptionResponse::AccuracyReduced, eastl::nullopt};
        }
        break;
    case ActorActionKind::Aiming:
    case ActorActionKind::AbilityWindup:
        break;
    }

    Require(disruption.recoveryTicks != 0);
    state = ActorActionState{ActorActionKind::DisruptionRecovery};
    return {DisruptionResponse::Canceled, disruption.recoveryTicks};
}

}
