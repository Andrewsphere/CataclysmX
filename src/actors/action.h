#pragma once

#include "core/timing.h"
#include "core/world_types.h"
#include "combat/action_types.h"
#include <EASTL/optional.h>

namespace cataclysmx {

enum class ActorActionKind { None, Recovery, Aiming, MeleeWindup, DisruptionRecovery, AbilityWindup };

// The actor owns the deadline separately. Pending actions use persistent targets,
// so this state never retains a runtime entity pointer or handle.
struct ActorActionState {
    ActorActionKind kind = ActorActionKind::None;
    PersistentId target = 0;
    // Stable combat move identifier; combat validates its meaning.
    std::uint32_t move = 0;
    // Windup captures positive post-strike recovery at the original speed.
    Tick recoveryTicks = 0;
    std::uint8_t accuracyPenalty = 0;
    PersistentId weapon = 0;
    // Expansion authority; old fields above remain aggregate-compatible adapters.
    CombatMoveId moveId{};
    CombatStyleId committedStyle{};
    CombatArtId trainingArt{};
    SourceCommitment sources;
    AbilityId ability{};
    AbilityGrantKey abilitySource;
    AbilityTarget abilityTarget;
    std::int32_t ratingPenalty100 = 0;
    std::uint64_t trainingWork = 0;
    bool counter = false;
    bool definitionDriven = false;
    bool operator==(const ActorActionState&) const = default;
};

enum class DisruptionCause { DamagingHit, ForcedMovement, RequiredCapabilityLost, Stagger };

struct Disruption {
    DisruptionCause cause;
    PersistentId source = 0;
    std::uint8_t accuracyPenalty = 15;
    Tick recoveryTicks = 20;
};

enum class DisruptionResponse { Ignored, AccuracyReduced, Canceled };

struct DisruptionResult {
    DisruptionResponse response;
    // Present only on cancellation: replace the actor's deadline with this
    // positive relative delay. Otherwise preserve the existing deadline.
    eastl::optional<Tick> nextDelay;
};

// Checks structural invariants, not combat move existence or target liveness.
bool ValidateAction(const ActorActionState& state);

// Relative timing only; does not read time or mutate a deadline. Ordinary hits
// and movement only raise windup penalty (maximum 30). All causes cancel aiming;
// capability loss and stagger cancel windup. Cancellation clears pending data.
// Conservative stagger policy: None and Recovery ignore it, avoiding shortened
// committed recovery. DisruptionRecovery ignores every further disruption,
// bounding cancellation recovery instead of resetting its timer.
// Invalid state/cause is fatal; zero recovery is fatal only when canceling.
DisruptionResult ApplyDisruption(ActorActionState& state, const Disruption& disruption);

}
