#include "actors/action.h"

#include <cstdlib>

namespace cataclysmx {
namespace {

void Require(bool condition) {
    if (!condition) { std::abort(); }
}

}

bool ValidateAction(const ActorActionState& state) {
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
            return {DisruptionResponse::AccuracyReduced, eastl::nullopt};
        }
        break;
    case ActorActionKind::Aiming:
        break;
    }

    Require(disruption.recoveryTicks != 0);
    state = ActorActionState{ActorActionKind::DisruptionRecovery};
    return {DisruptionResponse::Canceled, disruption.recoveryTicks};
}

}
