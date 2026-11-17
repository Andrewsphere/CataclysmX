#pragma once

#include "actors/action.h"
#include "combat/body.h"
#include "combat/damage.h"
#include "combat/contact.h"
#include "combat/character_combat.h"
#include "core/data_object.h"
#include "core/state_result.h"

namespace cataclysmx {

class World;
class ContentCatalog;

enum class CombatStyle : std::uint8_t { Balanced, Aggressive, Defensive };
enum class CombatFaction : std::uint8_t { Neutral, Survivors, Undead };
enum class CombatMove : std::uint32_t { None, PlainAttack, HeavyAttack, Bite, Parry };

enum class ActorLifeState : std::uint8_t { Alive, Dead };
enum class ExertionKind : std::uint8_t { Rest, Moving, Working, Combat };
struct MoveCooldown {
    CombatMoveId move{};
    Tick remaining = 0;
    bool operator==(const MoveCooldown&) const = default;
};
struct CounterOpening {
    PersistentId opponent = 0;
    Tick remaining = 0;
    bool operator==(const CounterOpening&) const = default;
};

struct CombatState {
    BodyState body = MakeBody(BodyKind::Detailed);
    InjuryProfile injury{};
    ActorActionState action{};
    CombatStyle style = CombatStyle::Balanced;
    CombatFaction faction = CombatFaction::Neutral;
    eastl::array<eastl::array<std::uint32_t, damage_type_count>, body_part_count> protection{};
    std::uint32_t attackDamage = 12; // Whole HP before protection.
    std::uint8_t accuracy = 90; // Percent; explicit RNG resolves contact, not selection.
    std::uint32_t stamina = 100;
    Tick staminaElapsed = 0;
    Tick reactionCooldown = 0; // Remaining active simulation time.
    Tick heavyCooldown = 0;
    CombatStyleId styleId{}; // Nonzero is authoritative after catalog binding.
    ActorLifeState life = ActorLifeState::Alive;
    bool inexhaustible = false;
    bool authoredTrainingRanks = false; // False derives ranks from learned skills.
    std::uint8_t offensiveTrainingRank = 0;
    std::uint8_t defensiveTrainingRank = 0;
    std::int32_t attackRating100 = 0;
    std::int32_t defenseRating100 = 0;
    ReachProfile reach;
    Tick staggerRemaining = 0;
    Tick focusRemaining = 0;
    std::int32_t focusRatingBonus100 = 0;
    Tick combatLockRemaining = 0;
    ExertionKind exertion = ExertionKind::Rest;
    std::uint32_t staminaRegenerationRemainder = 0;
    Vector<MoveCooldown> moveCooldowns;
    Vector<CounterOpening> openings;
    // Migration-only intrinsic accuracy policy; new content uses strike defaults.
    bool legacyAccuracyOverride = false;
    std::uint32_t intrinsicMissOverride = 500;
    PersistentId corpse = 0; // Nonzero after the single terminal death finalization.
    bool operator==(const CombatState&) const = default;
};

enum class AttackOutcome : std::uint8_t {
    Unavailable, NotReady, NoTarget, OutOfReach, NoMove, Windup, Miss, Parried, Blocked, Hit, Defeated
};
struct AttackResult {
    AttackOutcome outcome = AttackOutcome::Unavailable;
    CombatMove move = CombatMove::None;
    PersistentId target = 0;
    BodyPart part = BodyPart::Count;
    std::uint32_t injury = 0;
    Tick cost = 0;
    bool committed = false;
    bool contact = false;
    bool lostCapability = false;
    DisruptionResponse disruption = DisruptionResponse::Ignored;
    CombatMoveId moveId{};
    CombatRefusal reason = CombatRefusal::None;
    std::uint32_t strikeCount = 0;
    std::uint32_t resolvedStrikes = 0;
    std::uint64_t totalHpLoss = 0; // Hundredths; excludes overkill/max-HP loss.
    bool operator==(const AttackResult&) const = default;
};

// Presentation-only last interaction, intentionally not part of replay/save state.
struct CombatFeedback {
    AttackResult result{};
    bool incoming = false;
    Tick tick = 0;
    bool operator==(const CombatFeedback&) const = default;
};
struct CombatPartView {
    std::uint32_t hp = 0; // Hundredths, as in BodyState.
    std::uint32_t maximum = 0;
    bool broken = false;
    bool operator==(const CombatPartView&) const = default;
};
struct CombatView {
    BodyKind bodyKind = BodyKind::Detailed;
    CombatStyle style = CombatStyle::Balanced;
    ActorActionKind action = ActorActionKind::None;
    std::uint32_t stamina = 100;
    std::uint8_t accuracyPenalty = 0;
    eastl::array<CombatPartView, body_part_count> parts{};
    CombatFeedback feedback{};
    CombatStyleId styleId{};
    ActorLifeState life = ActorLifeState::Alive;
    bool inexhaustible = false;
    Tick staggerRemaining = 0;
    Tick focusRemaining = 0;
    bool operator==(const CombatView&) const = default;
};

CombatState MakeCombatState(bool player);
bool Hostile(CombatFaction a, CombatFaction b);
bool ValidateCombatState(const CombatState& state);
DataObject CaptureCombatState(const CombatState& state);
StateResult<CombatState> ReadCombatState(const DataObject& data, Diagnostics* diagnostics = nullptr);
const char* CombatStyleName(CombatStyle style);

// Catalog-aware expansion persistence. Legacy overloads above remain legacy-only.
bool ValidateCombatState(const CombatState& state, const ContentCatalog& content);
DataObject CaptureCombatState(const CombatState& state, const ContentCatalog& content);
StateResult<CombatState> ReadCombatState(const DataObject& data, const ContentCatalog& content,
    Diagnostics* diagnostics = nullptr);

enum class StrikeOutcome : std::uint8_t { Invalidated, Miss, Dodged, Parried, Contact };
struct StrikeResult {
    PersistentId attacker = 0;
    PersistentId target = 0;
    CombatMoveId move{};
    StrikeOutcome outcome = StrikeOutcome::Invalidated;
    CombatRefusal reason = CombatRefusal::None;
    DefenseKind response = DefenseKind::None;
    CombatMoveId responseMove{};
    bool defenseSucceeded = false;
    bool armorStopped = false;
    bool killed = false;
    bool counter = false;
    ContactRegion region = ContactRegion::Count;
    BodyPart injuryPart = BodyPart::Count;
    std::uint64_t blockAbsorbed = 0;
    std::uint64_t armorAbsorbed = 0;
    std::uint64_t incomingDamage = 0;
    std::uint64_t postProtectionDamage = 0; // Hundredths before injury rounding.
    std::uint32_t hpLoss = 0; // Actual hundredths removed, not raw damage/overkill.
    std::uint32_t maxHpLoss = 0;
    DisruptionResponse disruption = DisruptionResponse::Ignored;
    bool operator==(const StrikeResult&) const = default;
};
enum class CombatEventKind : std::uint8_t {
    Strike, ActionCommitted, ActionInvalidated, TrainingSuppressed, ArtStudyCompleted, ActorDied
};
struct CombatEvent {
    Tick tick = 0;
    StrikeResult strike;
    CombatEventKind kind = CombatEventKind::Strike;
    PersistentId actor = 0;
    PersistentId opponent = 0;
    CombatMoveId move{};
    AbilityId ability{};
    CombatArtId art{};
    CombatRefusal reason = CombatRefusal::None;
    Tick cost = 0;
    std::uint32_t staminaSpent = 0;
    bool learnedArt = false;
    std::uint64_t serial = 0; // Session-local presentation order, assigned after draining.
    bool operator==(const CombatEvent&) const = default;
};
struct EquipmentQuery {
    CombatRefusal reason = CombatRefusal::None;
    std::uint32_t baseCost = 0;
    bool operator==(const EquipmentQuery&) const = default;
};
struct HeldItemView {
    PersistentId item = 0;
    GripKind grip = GripKind::OneHanded;
    bool usable = false;
    bool operator==(const HeldItemView&) const = default;
};
struct EquipmentView {
    eastl::array<HeldItemView, 2> hands{};
    Vector<PersistentId> worn;
    eastl::array<std::uint32_t, contactRegionCount> encumbrance{};
    bool operator==(const EquipmentView&) const = default;
};
struct AbilityView {
    AbilityId ability{};
    AbilityGrantKey source;
    CombatAvailability availability;
    Tick cooldownRemaining = 0;
    AbilityTargetKind targeting = AbilityTargetKind::Self;
    bool operator==(const AbilityView&) const = default;
};
struct ArtManualView {
    BookId book{};
    CombatArtId art{};
    CombatRefusal reason = CombatRefusal::None;
    std::uint32_t successChance = 0;
    Tick duration = 0;
    Tick remaining = 0;
    bool active = false;
    bool operator==(const ArtManualView&) const = default;
};
struct CombatMoveView {
    CombatMoveId move{};
    CombatAvailability availability;
    Tick cooldownRemaining = 0;
    bool unlocked = false;
    bool operator==(const CombatMoveView&) const = default;
};
struct CombatInfoView {
    CombatView combat;
    EquipmentView equipment;
    Vector<CombatMoveView> moves;
    Vector<AbilityView> abilities;
    Vector<ArtProgress> arts;
    Vector<ArtManualView> manuals;
    bool operator==(const CombatInfoView&) const = default;
};

}
