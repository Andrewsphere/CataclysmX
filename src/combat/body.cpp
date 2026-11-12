#include "combat/body.h"

#include <cstdlib>

namespace cataclysmx {
namespace {

std::size_t Index(BodyPart part) { return static_cast<std::size_t>(part); }

bool Extremity(BodyPart part) {
    return part == BodyPart::LeftHand || part == BodyPart::RightHand ||
        part == BodyPart::LeftLeg || part == BodyPart::RightLeg;
}

void Require(bool condition) {
    if (!condition) { std::abort(); }
}

// Avoid adding elapsed to a persisted counter: the sum could overflow even
// though both inputs are valid. Each counter is strictly below its interval.
bool FinishInterval(Tick& counter, Tick interval, Tick& elapsed) {
    const Tick remaining = interval - counter;
    if (elapsed < remaining) {
        counter += elapsed;
        elapsed = 0;
        return false;
    }
    elapsed -= remaining;
    counter = 0;
    return true;
}

void RecoverPart(BodyState& body, BodyPart part, const InjuryProfile& profile, Tick elapsed) {
    auto& state = body.parts[Index(part)];
    if (state.broken) {
        if (!FinishInterval(state.mendingElapsed, profile.mendingDuration, elapsed)) { return; }
        state.broken = false;
        const auto recovery = profile.mendedHp * body_hp_scale;
        const auto maximum = EffectiveMax(body, part);
        state.hp = recovery < maximum ? recovery : maximum;
    }
    // Detailed vital-zero policy belongs to the caller; recovery cannot revive
    // it implicitly. Simple disabled attached zones may regenerate if enabled.
    if (body.kind == BodyKind::Detailed && !Extremity(part) && state.hp == 0) { return; }

    // Each successful iteration restores at least one hundredth, bounded by
    // the fixed anatomical maxima (100 HP), never by the elapsed tick count.
    while (elapsed != 0) {
        const auto maximum = EffectiveMax(body, part);
        if (state.hp < maximum) {
            if (!FinishInterval(state.healingElapsed, profile.healingInterval, elapsed)) { return; }
            const auto missing = maximum - state.hp;
            state.hp += missing < body_hp_scale ? missing : body_hp_scale;
        } else if (state.maxLoss != 0) {
            if (!FinishInterval(state.maxHealingElapsed, profile.maxHealingInterval, elapsed)) { return; }
            const auto restoration = state.maxLoss < body_hp_scale ? state.maxLoss : body_hp_scale;
            state.maxLoss -= restoration;
            // hp intentionally stays unchanged: ordinary healing resumes next.
        } else {
            return; // Healthy bodies cannot bank recovery time.
        }
    }
}

}

bool HasPart(BodyKind kind, BodyPart part) {
    if (kind == BodyKind::Detailed) { return part >= BodyPart::Head && part <= BodyPart::RightLeg; }
    if (kind == BodyKind::Simple) { return part >= BodyPart::AttackApparatus && part <= BodyPart::Core; }
    return false;
}

std::uint32_t UnreducedMax(BodyPart part) {
    Require(part < BodyPart::Count);
    return (part == BodyPart::AttackApparatus || part == BodyPart::Locomotion ? 60U : 100U) * body_hp_scale;
}

BodyState MakeBody(BodyKind kind) {
    Require(kind == BodyKind::Detailed || kind == BodyKind::Simple);
    BodyState body;
    body.kind = kind;
    for (std::size_t i = 0; i < body_part_count; ++i) {
        const auto part = static_cast<BodyPart>(i);
        if (HasPart(kind, part)) { body.parts[i].hp = UnreducedMax(part); }
    }
    return body;
}

std::uint32_t EffectiveMax(const BodyState& body, BodyPart part) {
    Require(HasPart(body.kind, part));
    Require(body.parts[Index(part)].maxLoss <= UnreducedMax(part));
    return UnreducedMax(part) - body.parts[Index(part)].maxLoss;
}

bool Functional(const BodyState& body, BodyPart part) {
    if (!HasPart(body.kind, part)) { return false; }
    const auto& state = body.parts[Index(part)];
    return state.hp != 0 && !state.broken;
}

bool ValidateProfile(const InjuryProfile& profile) {
    return profile.mendingDuration != 0 && profile.healingInterval != 0 &&
        profile.maxHealingInterval != 0 && profile.mendedHp != 0 &&
        profile.mendedHp <= 100 && profile.maxLossConversionPercent <= 100 &&
        profile.maxLossCapPercent <= 50;
}

std::uint32_t LocomotionBaseCost(const BodyState& body) {
    if (body.kind == BodyKind::Simple) { return Functional(body, BodyPart::Locomotion) ? 100 : 200; }
    const unsigned disabled = unsigned(!Functional(body, BodyPart::LeftLeg))
        + unsigned(!Functional(body, BodyPart::RightLeg));
    return 100 + disabled * 50;
}

bool ValidateBody(const BodyState& body, const InjuryProfile& profile) {
    if (!ValidateProfile(profile) ||
        (body.kind != BodyKind::Detailed && body.kind != BodyKind::Simple)) { return false; }
    for (std::size_t i = 0; i < body_part_count; ++i) {
        const auto part = static_cast<BodyPart>(i);
        const auto& state = body.parts[i];
        if (!HasPart(body.kind, part)) {
            if (!(state == BodyPartState{})) { return false; }
            continue;
        }
        const bool canBreak = body.kind == BodyKind::Detailed && Extremity(part);
        if (state.maxLoss > UnreducedMax(part) * profile.maxLossCapPercent / 100 ||
            state.hp > UnreducedMax(part) - state.maxLoss ||
            state.healingElapsed >= profile.healingInterval ||
            state.maxHealingElapsed >= profile.maxHealingInterval) { return false; }
        if (!canBreak && (state.broken || state.maxLoss != 0)) { return false; }
        if (state.broken) {
            if (state.hp != 0 || state.mendingElapsed >= profile.mendingDuration ||
                state.healingElapsed != 0 || state.maxHealingElapsed != 0) { return false; }
        } else if (state.mendingElapsed != 0 || (canBreak && state.hp == 0)) { return false; }
        const auto maximum = UnreducedMax(part) - state.maxLoss;
        if ((state.hp == maximum && state.healingElapsed != 0) ||
            ((state.hp != maximum || state.maxLoss == 0) && state.maxHealingElapsed != 0)) { return false; }
        if (body.kind == BodyKind::Detailed && !canBreak && state.hp == 0 &&
            state.healingElapsed != 0) { return false; }
    }
    return true;
}

InjurySummary ApplyInjury(BodyState& body, BodyPart part,
    std::uint32_t integerInjury, const InjuryProfile& profile) {
    Require(ValidateBody(body, profile) && HasPart(body.kind, part));
    InjurySummary result;
    result.affectedPart = part;
    result.defeated = body.kind == BodyKind::Simple && !Functional(body, BodyPart::Core);
    if (integerInjury == 0) { return result; }
    if (body.kind == BodyKind::Simple && part != BodyPart::Core && !Functional(body, part)) {
        part = BodyPart::Core;
        result.affectedPart = part;
        result.transferredToCore = true;
    }
    auto& state = body.parts[Index(part)];
    if (state.broken) {
        const std::uint64_t loss = std::uint64_t{integerInjury} * profile.maxLossConversionPercent;
        const auto cap = UnreducedMax(part) * profile.maxLossCapPercent / 100;
        const auto room = cap - state.maxLoss;
        state.maxLoss += loss < room ? static_cast<std::uint32_t>(loss) : room;
        return result; // Mending progress is preserved, including at the cap.
    }
    const bool wasFunctional = Functional(body, part);
    const std::uint64_t damage = std::uint64_t{integerInjury} * body_hp_scale;
    state.hp = damage < state.hp ? state.hp - static_cast<std::uint32_t>(damage) : 0;
    // Preserve ordinary healing progress across nonfatal injury. Maximum
    // restoration progress is discarded when no longer at full regular HP.
    state.maxHealingElapsed = 0;
    if (state.hp == 0) {
        state.healingElapsed = 0;
        if (body.kind == BodyKind::Detailed && Extremity(part)) {
            state.broken = true;
            result.broken = true;
        }
    }
    result.lostCapability = wasFunctional && !Functional(body, part);
    result.defeated = body.kind == BodyKind::Simple && !Functional(body, BodyPart::Core);
    return result;
}

void ElapseBody(BodyState& body, const InjuryProfile& profile, Tick elapsed) {
    Require(ValidateBody(body, profile));
    if (!profile.regeneration || elapsed == 0 ||
        (body.kind == BodyKind::Simple && !Functional(body, BodyPart::Core))) { return; }
    for (std::size_t i = 0; i < body_part_count; ++i) {
        const auto part = static_cast<BodyPart>(i);
        if (HasPart(body.kind, part)) { RecoverPart(body, part, profile, elapsed); }
    }
}

}
