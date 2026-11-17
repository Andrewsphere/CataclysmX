#pragma once

#include "combat/body.h"
#include <cstdint>

namespace cataclysmx {

enum class ContactRegion : std::uint8_t {
    Head, Eyes, Mouth, Ears, Torso,
    LeftArm, RightArm, LeftHand, RightHand,
    LeftLeg, RightLeg, LeftFoot, RightFoot,
    AttackApparatus, Locomotion, Core, Count
};
inline constexpr std::size_t contactRegionCount = static_cast<std::size_t>(ContactRegion::Count);
using ContactMask = std::uint32_t;
inline constexpr ContactMask detailedContactMask =
    (ContactMask{1} << static_cast<unsigned>(ContactRegion::AttackApparatus)) - 1;
inline constexpr ContactMask simpleContactMask =
    ((ContactMask{1} << static_cast<unsigned>(ContactRegion::Count)) - 1) ^ detailedContactMask;
inline constexpr ContactMask allContactMask = detailedContactMask | simpleContactMask;
static_assert(contactRegionCount < 32);

enum class ReachHeight : std::uint8_t { Any, Lower, Upper };
struct ReachProfile {
    std::uint8_t distance = 1; // V1: 1..2, same Y, supercover obstruction test.
    ReachHeight height = ReachHeight::Any;
    ContactMask regions = allContactMask;
    bool operator==(const ReachProfile&) const = default;
};
struct ContactWeight {
    ContactRegion region = ContactRegion::Torso;
    std::uint32_t weight = 1;
    bool operator==(const ContactWeight&) const = default;
};

ContactMask ContactBit(ContactRegion region);
bool HasContact(BodyKind body, ContactRegion region);
BodyPart InjuryPartFor(ContactRegion region);
bool ReachAllowsContact(const ReachProfile& reach, BodyKind body, ContactRegion region);
bool ValidateReachProfile(const ReachProfile& reach);

}
