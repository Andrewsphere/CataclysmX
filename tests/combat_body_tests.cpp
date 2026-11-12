#include "combat/body.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>

namespace {
using namespace cataclysmx;

BodyPartState& Part(BodyState& body, BodyPart part) {
    return body.parts[static_cast<std::size_t>(part)];
}

std::uint32_t WholeHp(BodyPart part) { return UnreducedMax(part) / body_hp_scale; }

InjuryProfile FastRecovery() {
    InjuryProfile profile;
    profile.mendingDuration = 100;
    profile.healingInterval = 3;
    profile.maxHealingInterval = 7;
    return profile;
}

TEST_CASE("Combat anatomy is explicit and independent of control", "[combat][body]") {
    const auto detailed = MakeBody(BodyKind::Detailed);
    const auto simple = MakeBody(BodyKind::Simple);
    const InjuryProfile profile;
    CHECK(ValidateBody(detailed, profile));
    CHECK(ValidateBody(simple, profile));
    for (const auto part : {BodyPart::Head, BodyPart::Torso, BodyPart::LeftHand,
        BodyPart::RightHand, BodyPart::LeftLeg, BodyPart::RightLeg}) {
        CHECK(Functional(detailed, part));
        CHECK(EffectiveMax(detailed, part) == UnreducedMax(part));
        CHECK(UnreducedMax(part) == 100 * body_hp_scale);
        CHECK_FALSE(HasPart(simple.kind, part));
    }
    for (const auto part : {BodyPart::AttackApparatus, BodyPart::Locomotion, BodyPart::Core}) {
        CHECK(Functional(simple, part));
        CHECK(EffectiveMax(simple, part) == UnreducedMax(part));
        CHECK_FALSE(Functional(detailed, part));
    }
    CHECK(profile.mendingDuration >= 7 * Tick{ticks_per_turn} * 86400);
    CHECK(profile.healingInterval == Tick{ticks_per_turn} * 3600);
    CHECK(profile.maxHealingInterval == Tick{ticks_per_turn} * 86400);
    CHECK(profile.mendedHp == 40);
    CHECK(profile.maxLossConversionPercent == 10);
    CHECK(profile.maxLossCapPercent == 50);
}

TEST_CASE("Only whole post-protection injury enters the body model", "[combat][body]") {
    auto body = MakeBody(BodyKind::Detailed);
    const auto pristine = body;
    const InjuryProfile profile;
    // Protection/rounding belongs upstream: a result below one HP arrives as 0.
    const auto ignored = ApplyInjury(body, BodyPart::LeftHand, 0, profile);
    CHECK(body == pristine);
    CHECK_FALSE(ignored.broken);
    CHECK_FALSE(ignored.lostCapability);
    ApplyInjury(body, BodyPart::LeftHand, 1, profile);
    CHECK(Part(body, BodyPart::LeftHand).hp == UnreducedMax(BodyPart::LeftHand) - body_hp_scale);
    CHECK(Functional(body, BodyPart::LeftHand));
    CHECK(EffectiveMax(body, BodyPart::LeftHand) == UnreducedMax(BodyPart::LeftHand));
}

TEST_CASE("Detailed extremities break only on crossing zero without overkill loss", "[combat][body]") {
    const InjuryProfile profile;
    for (const auto part : {BodyPart::LeftHand, BodyPart::RightHand,
        BodyPart::LeftLeg, BodyPart::RightLeg}) {
        auto body = MakeBody(BodyKind::Detailed);
        ApplyInjury(body, part, WholeHp(part) - 1, profile);
        CHECK(Part(body, part).hp == body_hp_scale);
        CHECK(Functional(body, part));
        const auto result = ApplyInjury(body, part, std::numeric_limits<std::uint32_t>::max(), profile);
        CHECK(result.affectedPart == part);
        CHECK(result.broken);
        CHECK(result.lostCapability);
        CHECK_FALSE(result.defeated);
        CHECK(Part(body, part).hp == 0);
        CHECK(Part(body, part).maxLoss == 0);
        CHECK_FALSE(Functional(body, part));
        CHECK(ValidateBody(body, profile));
    }
}

TEST_CASE("Already broken injury accumulates fractional maximum loss up to its cap", "[combat][body]") {
    auto body = MakeBody(BodyKind::Detailed);
    const InjuryProfile profile;
    ApplyInjury(body, BodyPart::LeftLeg, WholeHp(BodyPart::LeftLeg), profile);
    for (int hit = 0; hit < 9; ++hit) { ApplyInjury(body, BodyPart::LeftLeg, 1, profile); }
    CHECK(Part(body, BodyPart::LeftLeg).maxLoss == 90);
    CHECK(EffectiveMax(body, BodyPart::LeftLeg) == UnreducedMax(BodyPart::LeftLeg) - 90);
    const auto ignoredState = body;
    ApplyInjury(body, BodyPart::LeftLeg, 0, profile);
    CHECK(body == ignoredState);
    ApplyInjury(body, BodyPart::LeftLeg, 1, profile);
    CHECK(Part(body, BodyPart::LeftLeg).maxLoss == 100);
    const auto further = ApplyInjury(body, BodyPart::LeftLeg,
        std::numeric_limits<std::uint32_t>::max(), profile);
    CHECK_FALSE(further.broken);
    CHECK_FALSE(further.lostCapability);
    CHECK(Part(body, BodyPart::LeftLeg).maxLoss == UnreducedMax(BodyPart::LeftLeg) / 2);
    CHECK(EffectiveMax(body, BodyPart::LeftLeg) == UnreducedMax(BodyPart::LeftLeg) / 2);
    CHECK(Part(body, BodyPart::LeftLeg).hp == 0);
    CHECK(ValidateBody(body, profile));
}

TEST_CASE("Mending preserves progress under injury and clamps configured recovery", "[combat][body]") {
    auto profile = FastRecovery();
    profile.mendedHp = 60;
    auto body = MakeBody(BodyKind::Detailed);
    ApplyInjury(body, BodyPart::RightHand, WholeHp(BodyPart::RightHand), profile);
    ElapseBody(body, profile, 37);
    ApplyInjury(body, BodyPart::RightHand, 1000, profile);
    CHECK(Part(body, BodyPart::RightHand).mendingElapsed == 37);
    ElapseBody(body, profile, 62);
    CHECK_FALSE(Functional(body, BodyPart::RightHand));
    CHECK(Part(body, BodyPart::RightHand).hp == 0);
    ElapseBody(body, profile, 1);
    CHECK(Functional(body, BodyPart::RightHand));
    CHECK_FALSE(Part(body, BodyPart::RightHand).broken);
    CHECK(Part(body, BodyPart::RightHand).mendingElapsed == 0);
    CHECK(Part(body, BodyPart::RightHand).hp == UnreducedMax(BodyPart::RightHand) / 2);
    CHECK(Part(body, BodyPart::RightHand).hp == 50 * body_hp_scale); // 60 clamped to 50.
    CHECK(ValidateBody(body, profile));

    body = MakeBody(BodyKind::Detailed);
    profile.mendedHp = 40;
    ApplyInjury(body, BodyPart::LeftHand, WholeHp(BodyPart::LeftHand), profile);
    ElapseBody(body, profile, profile.mendingDuration);
    CHECK(Part(body, BodyPart::LeftHand).hp == 4000);
    profile.mendedHp = 60;
    body = MakeBody(BodyKind::Detailed);
    ApplyInjury(body, BodyPart::LeftHand, WholeHp(BodyPart::LeftHand), profile);
    ElapseBody(body, profile, profile.mendingDuration);
    CHECK(Part(body, BodyPart::LeftHand).hp == 6000);
}

TEST_CASE("Conversion and accumulated loss cap are independent configurable percentages", "[combat][body]") {
    auto profile = FastRecovery();
    profile.maxLossConversionPercent = 25;
    profile.maxLossCapPercent = 20;
    auto body = MakeBody(BodyKind::Detailed);
    ApplyInjury(body, BodyPart::LeftHand, WholeHp(BodyPart::LeftHand), profile);
    ElapseBody(body, profile, 17);
    ApplyInjury(body, BodyPart::LeftHand, 3, profile);
    CHECK(Part(body, BodyPart::LeftHand).maxLoss == 75);
    CHECK(EffectiveMax(body, BodyPart::LeftHand) == UnreducedMax(BodyPart::LeftHand) - 75);
    ApplyInjury(body, BodyPart::LeftHand, std::numeric_limits<std::uint32_t>::max(), profile);
    CHECK(Part(body, BodyPart::LeftHand).maxLoss == UnreducedMax(BodyPart::LeftHand) / 5);
    CHECK(Part(body, BodyPart::LeftHand).mendingElapsed == 17);
    CHECK(ValidateBody(body, profile));

    for (const std::uint32_t conversion : {0U, 100U}) {
        profile.maxLossConversionPercent = conversion;
        CHECK(ValidateProfile(profile));
        body = MakeBody(BodyKind::Detailed);
        ApplyInjury(body, BodyPart::LeftHand, WholeHp(BodyPart::LeftHand), profile);
        ApplyInjury(body, BodyPart::LeftHand, 1, profile);
        CHECK(Part(body, BodyPart::LeftHand).maxLoss == conversion);
        CHECK(ValidateBody(body, profile));
    }
    profile.maxLossCapPercent = 0;
    CHECK(ValidateProfile(profile));
    body = MakeBody(BodyKind::Detailed);
    ApplyInjury(body, BodyPart::LeftHand, WholeHp(BodyPart::LeftHand), profile);
    ApplyInjury(body, BodyPart::LeftHand, std::numeric_limits<std::uint32_t>::max(), profile);
    CHECK(Part(body, BodyPart::LeftHand).maxLoss == 0);
    CHECK(ValidateBody(body, profile));
}

TEST_CASE("Maximum restoration waits for full HP then ordinary healing resumes", "[combat][body]") {
    auto profile = FastRecovery();
    profile.mendedHp = WholeHp(BodyPart::LeftLeg);
    auto body = MakeBody(BodyKind::Detailed);
    const auto maximum = UnreducedMax(BodyPart::LeftLeg);
    ApplyInjury(body, BodyPart::LeftLeg, WholeHp(BodyPart::LeftLeg), profile);
    ApplyInjury(body, BodyPart::LeftLeg, 11, profile); // Lose 1.10 maximum HP.
    ElapseBody(body, profile, 100);
    CHECK(Part(body, BodyPart::LeftLeg).hp == maximum - 110);
    ElapseBody(body, profile, 6);
    CHECK(EffectiveMax(body, BodyPart::LeftLeg) == maximum - 110);
    ElapseBody(body, profile, 1);
    CHECK(EffectiveMax(body, BodyPart::LeftLeg) == maximum - 10);
    CHECK(Part(body, BodyPart::LeftLeg).hp == maximum - 110);
    ElapseBody(body, profile, 2);
    CHECK(Part(body, BodyPart::LeftLeg).maxHealingElapsed == 0);
    CHECK(Part(body, BodyPart::LeftLeg).hp == maximum - 110);
    ElapseBody(body, profile, 1);
    CHECK(Part(body, BodyPart::LeftLeg).hp == maximum - 10);
    ElapseBody(body, profile, 7);
    CHECK(EffectiveMax(body, BodyPart::LeftLeg) == maximum);
    CHECK(Part(body, BodyPart::LeftLeg).hp == maximum - 10);
    ElapseBody(body, profile, 3);
    CHECK(Part(body, BodyPart::LeftLeg).hp == maximum);
    CHECK(ValidateBody(body, profile));

    ApplyInjury(body, BodyPart::LeftLeg, 2, profile);
    ElapseBody(body, profile, 2);
    CHECK(Part(body, BodyPart::LeftLeg).healingElapsed == 2);
    ApplyInjury(body, BodyPart::LeftLeg, 1, profile);
    ElapseBody(body, profile, 1); // Nonfatal injury preserves the regular clock.
    CHECK(Part(body, BodyPart::LeftLeg).hp == maximum - 2 * body_hp_scale);
}

TEST_CASE("Recovery subdivision and enormous elapsed durations preserve exact state", "[combat][body]") {
    const auto profile = FastRecovery();
    auto initial = MakeBody(BodyKind::Detailed);
    ApplyInjury(initial, BodyPart::LeftHand, WholeHp(BodyPart::LeftHand), profile);
    ApplyInjury(initial, BodyPart::LeftHand, 19, profile);
    ApplyInjury(initial, BodyPart::RightLeg, WholeHp(BodyPart::RightLeg), profile);
    ApplyInjury(initial, BodyPart::RightLeg, 1000, profile);
    ApplyInjury(initial, BodyPart::Head, 7, profile);
    for (const Tick duration : {Tick{99}, Tick{100}, Tick{101}, Tick{159}, Tick{167}, Tick{300}}) {
        auto whole = initial;
        auto split = initial;
        ElapseBody(whole, profile, duration);
        for (Tick tick = 0; tick < duration; ++tick) { ElapseBody(split, profile, 1); }
        CHECK(split == whole);
        CHECK(ValidateBody(whole, profile));
    }
    auto whole = initial;
    auto split = initial;
    constexpr Tick forever = std::numeric_limits<Tick>::max();
    ElapseBody(whole, profile, forever);
    ElapseBody(split, profile, forever - 17);
    ElapseBody(split, profile, 17);
    CHECK(whole == split);
    CHECK(whole == MakeBody(BodyKind::Detailed));

    // Also exercise huge intervals with persisted progress near Tick's limit.
    auto slow = profile;
    slow.mendingDuration = forever;
    slow.healingInterval = forever;
    slow.maxHealingInterval = forever;
    auto body = initial;
    ElapseBody(body, slow, forever - 1);
    CHECK(Part(body, BodyPart::LeftHand).mendingElapsed == forever - 1);
    CHECK(Part(body, BodyPart::Head).healingElapsed == forever - 1);
    ElapseBody(body, slow, 2);
    CHECK(Part(body, BodyPart::LeftHand).hp == 4000);
    CHECK(Part(body, BodyPart::LeftHand).healingElapsed == 1);
    CHECK(Part(body, BodyPart::Head).hp == UnreducedMax(BodyPart::Head) - 6 * body_hp_scale);
    CHECK(Part(body, BodyPart::Head).healingElapsed == 1);
    CHECK(ValidateBody(body, slow));
}

TEST_CASE("Disabled simple attached zones remain targetable and redirect later hits", "[combat][body]") {
    auto profile = FastRecovery();
    profile.regeneration = false;
    for (const auto zone : {BodyPart::AttackApparatus, BodyPart::Locomotion}) {
        auto body = MakeBody(BodyKind::Simple);
        auto result = ApplyInjury(body, zone, 1000, profile);
        CHECK(result.lostCapability);
        CHECK_FALSE(result.broken);
        CHECK_FALSE(result.defeated);
        CHECK_FALSE(result.transferredToCore);
        CHECK(HasPart(body.kind, zone));
        CHECK_FALSE(Functional(body, zone));
        CHECK(Part(body, BodyPart::Core).hp == UnreducedMax(BodyPart::Core));
        const auto disabled = body;
        ElapseBody(body, profile, std::numeric_limits<Tick>::max());
        CHECK(body == disabled);
        result = ApplyInjury(body, zone, 1, profile);
        CHECK(result.transferredToCore);
        CHECK(result.affectedPart == BodyPart::Core);
        CHECK(Part(body, BodyPart::Core).hp == UnreducedMax(BodyPart::Core) - body_hp_scale);
        CHECK_FALSE(result.defeated);
        result = ApplyInjury(body, zone, WholeHp(BodyPart::Core) - 1, profile);
        CHECK(result.defeated);
        CHECK(result.lostCapability);
        CHECK(ValidateBody(body, profile));
        profile.regeneration = true;
        const auto defeated = body;
        ElapseBody(body, profile, 1000);
        CHECK(body == defeated);
        profile.regeneration = false;
    }
    auto detailed = MakeBody(BodyKind::Detailed);
    CHECK_FALSE(ApplyInjury(detailed, BodyPart::Head, WholeHp(BodyPart::Head), profile).defeated);
    CHECK_FALSE(ApplyInjury(detailed, BodyPart::Torso, WholeHp(BodyPart::Torso), profile).defeated);
}

TEST_CASE("Disabling regeneration freezes mending and all recovery counters", "[combat][body]") {
    auto profile = FastRecovery();
    auto body = MakeBody(BodyKind::Detailed);
    ApplyInjury(body, BodyPart::RightLeg, WholeHp(BodyPart::RightLeg), profile);
    ApplyInjury(body, BodyPart::Head, 5, profile);
    ElapseBody(body, profile, 2);
    const auto paused = body;
    profile.regeneration = false;
    ElapseBody(body, profile, std::numeric_limits<Tick>::max());
    CHECK(body == paused);
    profile.regeneration = true;
    ElapseBody(body, profile, 98);
    CHECK(Part(body, BodyPart::RightLeg).hp == 4000);
    CHECK(ValidateBody(body, profile));
}

TEST_CASE("Recovery cannot bank healthy time or retain ineligible maximum progress", "[combat][body]") {
    auto profile = FastRecovery();
    profile.mendedHp = WholeHp(BodyPart::LeftHand);
    auto body = MakeBody(BodyKind::Detailed);
    ElapseBody(body, profile, std::numeric_limits<Tick>::max());
    ApplyInjury(body, BodyPart::LeftHand, 1, profile);
    ElapseBody(body, profile, 2);
    CHECK(Part(body, BodyPart::LeftHand).hp == UnreducedMax(BodyPart::LeftHand) - body_hp_scale);
    ElapseBody(body, profile, 1);
    CHECK(Part(body, BodyPart::LeftHand).hp == UnreducedMax(BodyPart::LeftHand));

    ApplyInjury(body, BodyPart::LeftHand, WholeHp(BodyPart::LeftHand), profile);
    ApplyInjury(body, BodyPart::LeftHand, 10, profile);
    ElapseBody(body, profile, 106); // Mended full, six of seven maximum ticks.
    CHECK(Part(body, BodyPart::LeftHand).maxHealingElapsed == 6);
    ApplyInjury(body, BodyPart::LeftHand, 1, profile);
    CHECK(Part(body, BodyPart::LeftHand).maxHealingElapsed == 0);
    ElapseBody(body, profile, 3); // Fill regular HP again.
    CHECK(Part(body, BodyPart::LeftHand).hp == UnreducedMax(BodyPart::LeftHand) - body_hp_scale);
    ElapseBody(body, profile, 1);
    CHECK(EffectiveMax(body, BodyPart::LeftHand) == UnreducedMax(BodyPart::LeftHand) - body_hp_scale);
    CHECK(Part(body, BodyPart::LeftHand).maxHealingElapsed == 1);
    CHECK(ValidateBody(body, profile));
}

TEST_CASE("Simple attached zones can recover when regeneration is enabled", "[combat][body]") {
    const auto profile = FastRecovery();
    auto body = MakeBody(BodyKind::Simple);
    ApplyInjury(body, BodyPart::Locomotion, WholeHp(BodyPart::Locomotion), profile);
    ElapseBody(body, profile, 2);
    CHECK_FALSE(Functional(body, BodyPart::Locomotion));
    const auto partial = body;
    ApplyInjury(body, BodyPart::Locomotion, 0, profile);
    CHECK(body == partial);
    ElapseBody(body, profile, 1);
    CHECK(Functional(body, BodyPart::Locomotion));
    CHECK(Part(body, BodyPart::Locomotion).hp == 100);
    ApplyInjury(body, BodyPart::Locomotion, 1, profile);
    CHECK(Part(body, BodyPart::Core).hp == UnreducedMax(BodyPart::Core));
    CHECK(ValidateBody(body, profile));
}

TEST_CASE("Persistence validation rejects invalid anatomy and recovery counters", "[combat][body]") {
    auto profile = FastRecovery();
    SECTION("zero mending duration") { profile.mendingDuration = 0; }
    SECTION("zero regular interval") { profile.healingInterval = 0; }
    SECTION("zero maximum interval") { profile.maxHealingInterval = 0; }
    SECTION("zero recovery") { profile.mendedHp = 0; }
    SECTION("unbounded recovery") { profile.mendedHp = std::numeric_limits<std::uint32_t>::max(); }
    SECTION("excessive conversion") { profile.maxLossConversionPercent = 101; }
    SECTION("excessive cap") { profile.maxLossCapPercent = 51; }
    CHECK_FALSE(ValidateProfile(profile));
    CHECK_FALSE(ValidateBody(MakeBody(BodyKind::Detailed), profile));
}

TEST_CASE("Persistence validation checks phase eligibility and absent zones", "[combat][body]") {
    const auto profile = FastRecovery();
    auto body = MakeBody(BodyKind::Detailed);
    auto& hand = Part(body, BodyPart::LeftHand);
    SECTION("invalid kind") { body.kind = static_cast<BodyKind>(255); }
    SECTION("absent zone") { Part(body, BodyPart::Core).hp = 1; }
    SECTION("above maximum") { hand.hp = UnreducedMax(BodyPart::LeftHand) + 1; }
    SECTION("excessive loss") {
        hand.maxLoss = UnreducedMax(BodyPart::LeftHand) * profile.maxLossCapPercent / 100 + 1;
        hand.hp = 0; hand.broken = true;
    }
    SECTION("unbroken zero extremity") { hand.hp = 0; }
    SECTION("broken positive HP") { hand.broken = true; }
    SECTION("unbroken mending progress") { hand.mendingElapsed = 1; }
    SECTION("completed persisted mending") {
        hand.hp = 0; hand.broken = true; hand.mendingElapsed = profile.mendingDuration;
    }
    SECTION("broken healing progress") { hand.hp = 0; hand.broken = true; hand.healingElapsed = 1; }
    SECTION("full HP healing progress") { hand.healingElapsed = 1; }
    SECTION("completed regular interval") {
        hand.hp = UnreducedMax(BodyPart::LeftHand) - body_hp_scale;
        hand.healingElapsed = profile.healingInterval;
    }
    SECTION("maximum progress before regular full") {
        hand.hp = UnreducedMax(BodyPart::LeftHand) - 2 * body_hp_scale;
        hand.maxLoss = body_hp_scale; hand.maxHealingElapsed = 1;
    }
    SECTION("maximum progress with no loss") { hand.maxHealingElapsed = 1; }
    SECTION("completed maximum interval") {
        hand.hp = UnreducedMax(BodyPart::LeftHand) - body_hp_scale;
        hand.maxLoss = body_hp_scale; hand.maxHealingElapsed = profile.maxHealingInterval;
    }
    SECTION("vital cannot break") { Part(body, BodyPart::Head).hp = 0; Part(body, BodyPart::Head).broken = true; }
    SECTION("vital maximum cannot be lost") {
        Part(body, BodyPart::Head).hp = UnreducedMax(BodyPart::Head) - body_hp_scale;
        Part(body, BodyPart::Head).maxLoss = body_hp_scale;
    }
    CHECK_FALSE(ValidateBody(body, profile));
}

}
