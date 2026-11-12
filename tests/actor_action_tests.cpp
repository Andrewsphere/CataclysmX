#include "actors/action.h"

#include <catch2/catch_test_macros.hpp>
#include <initializer_list>

namespace {
using namespace cataclysmx;

TEST_CASE("Actor action defaults are canonical", "[actors][action]") {
    const ActorActionState state;
    CHECK(state == ActorActionState{ActorActionKind::None, 0, 0, 0, 0});
    CHECK(ValidateAction(state));
    const Disruption hit{DisruptionCause::DamagingHit};
    CHECK(hit.source == 0);
    CHECK(hit.accuracyPenalty == 15);
    CHECK(hit.recoveryTicks == 20);
}

TEST_CASE("Windup disruption keeps the strongest bounded penalty without rescheduling", "[actors][action]") {
    for (const auto cause : {DisruptionCause::DamagingHit, DisruptionCause::ForcedMovement}) {
        ActorActionState state{ActorActionKind::MeleeWindup, 42, 7, 83, 0};
        auto expected = state;
        // Zero cancellation recovery is irrelevant for a noncanceling hit.
        Disruption disruption{cause, 99, 15, 0};
        const auto first = ApplyDisruption(state, disruption);
        CHECK(first.response == DisruptionResponse::AccuracyReduced);
        CHECK_FALSE(first.nextDelay);
        expected.accuracyPenalty = 15;
        CHECK(state == expected);
        for (const auto penalty : {15, 4, 0, 29, 255, 30, 255}) {
            disruption.accuracyPenalty = static_cast<std::uint8_t>(penalty);
            const auto oldPenalty = state.accuracyPenalty;
            const auto result = ApplyDisruption(state, disruption);
            if (penalty > oldPenalty && oldPenalty < 30) {
                CHECK(result.response == DisruptionResponse::AccuracyReduced);
                expected.accuracyPenalty = static_cast<std::uint8_t>(penalty > 30 ? 30 : penalty);
            } else {
                CHECK(result.response == DisruptionResponse::Ignored);
            }
            CHECK_FALSE(result.nextDelay);
            CHECK(state == expected);
            CHECK(ValidateAction(state));
        }
        CHECK(state.accuracyPenalty == 30);
    }
}

TEST_CASE("All aiming disruptions clear pending data and request a positive relative delay", "[actors][action]") {
    for (const auto cause : {DisruptionCause::DamagingHit, DisruptionCause::ForcedMovement,
        DisruptionCause::RequiredCapabilityLost, DisruptionCause::Stagger}) {
        ActorActionState state{ActorActionKind::Aiming, 42};
        const auto result = ApplyDisruption(state, Disruption{cause, 99, 255, 7});
        CHECK(result.response == DisruptionResponse::Canceled);
        REQUIRE(result.nextDelay);
        CHECK(*result.nextDelay == 7);
        CHECK(state == ActorActionState{ActorActionKind::DisruptionRecovery});
        CHECK(ValidateAction(state));
    }
}

TEST_CASE("Capability loss and explicit stagger cancel committed melee windup", "[actors][action]") {
    for (const auto cause : {DisruptionCause::RequiredCapabilityLost, DisruptionCause::Stagger}) {
        ActorActionState state{ActorActionKind::MeleeWindup, 42, 7, 83, 30};
        const auto result = ApplyDisruption(state, Disruption{cause});
        CHECK(result.response == DisruptionResponse::Canceled);
        REQUIRE(result.nextDelay);
        CHECK(*result.nextDelay == 20);
        CHECK(state == ActorActionState{ActorActionKind::DisruptionRecovery});
        CHECK(ValidateAction(state));
    }
}

TEST_CASE("Disruption recovery never requests a timer reset", "[actors][action]") {
    ActorActionState state{ActorActionKind::Aiming, 42};
    REQUIRE(ApplyDisruption(state, Disruption{DisruptionCause::DamagingHit}).nextDelay);
    const auto recovering = state;
    for (const auto cause : {DisruptionCause::DamagingHit, DisruptionCause::ForcedMovement,
        DisruptionCause::RequiredCapabilityLost, DisruptionCause::Stagger}) {
        for (const Tick delay : {Tick{0}, Tick{1}, Tick{999}}) {
            const auto result = ApplyDisruption(state, Disruption{cause, 99, 255, delay});
            CHECK(result.response == DisruptionResponse::Ignored);
            CHECK_FALSE(result.nextDelay);
            CHECK(state == recovering);
        }
    }
}

TEST_CASE("Idle and noninterruptible recovery ignore disruption including stagger", "[actors][action]") {
    for (const auto kind : {ActorActionKind::None, ActorActionKind::Recovery}) {
        ActorActionState state{kind};
        const auto original = state;
        for (const auto cause : {DisruptionCause::DamagingHit, DisruptionCause::ForcedMovement,
            DisruptionCause::RequiredCapabilityLost, DisruptionCause::Stagger}) {
            const auto result = ApplyDisruption(state, Disruption{cause, 99, 255, 0});
            CHECK(result.response == DisruptionResponse::Ignored);
            CHECK_FALSE(result.nextDelay);
            CHECK(state == original);
        }
    }
}

TEST_CASE("Action validation rejects unknown kinds and noncanonical unused fields", "[actors][action]") {
    CHECK_FALSE(ValidateAction(ActorActionState{static_cast<ActorActionKind>(255)}));
    for (const auto kind : {ActorActionKind::None, ActorActionKind::Recovery,
        ActorActionKind::DisruptionRecovery, ActorActionKind::Aiming}) {
        const ActorActionState valid{kind, kind == ActorActionKind::Aiming ? PersistentId{42} : 0};
        CHECK(ValidateAction(valid));
        auto invalid = valid;
        if (kind == ActorActionKind::Aiming) { invalid.target = 0; }
        else { invalid.target = 42; }
        CHECK_FALSE(ValidateAction(invalid));
        invalid = valid;
        invalid.move = 1;
        CHECK_FALSE(ValidateAction(invalid));
        invalid = valid;
        invalid.recoveryTicks = 1;
        CHECK_FALSE(ValidateAction(invalid));
        invalid = valid;
        invalid.accuracyPenalty = 1;
        CHECK_FALSE(ValidateAction(invalid));
    }
    const ActorActionState valid{ActorActionKind::MeleeWindup, 42, 7, 83, 30};
    CHECK(ValidateAction(valid));
    auto invalid = valid;
    invalid.target = 0;
    CHECK_FALSE(ValidateAction(invalid));
    invalid = valid;
    invalid.move = 0;
    CHECK_FALSE(ValidateAction(invalid));
    invalid = valid;
    invalid.recoveryTicks = 0;
    CHECK_FALSE(ValidateAction(invalid));
    invalid = valid;
    invalid.accuracyPenalty = 31;
    CHECK_FALSE(ValidateAction(invalid));
    invalid.accuracyPenalty = 255;
    CHECK_FALSE(ValidateAction(invalid));
}

}
