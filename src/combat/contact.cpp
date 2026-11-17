#include "combat/contact.h"

#include <cstdlib>

namespace cataclysmx {

ContactMask ContactBit(ContactRegion region) {
    if (region >= ContactRegion::Count) { std::abort(); }
    return ContactMask{1} << static_cast<unsigned>(region);
}

bool HasContact(BodyKind body, ContactRegion region) {
    if (region >= ContactRegion::Count) { return false; }
    switch (body) {
    case BodyKind::Detailed: return (ContactBit(region) & detailedContactMask) != 0;
    case BodyKind::Simple: return (ContactBit(region) & simpleContactMask) != 0;
    }
    return false;
}

BodyPart InjuryPartFor(ContactRegion region) {
    switch (region) {
    case ContactRegion::Head:
    case ContactRegion::Eyes:
    case ContactRegion::Mouth:
    case ContactRegion::Ears: return BodyPart::Head;
    case ContactRegion::Torso: return BodyPart::Torso;
    case ContactRegion::LeftArm:
    case ContactRegion::LeftHand: return BodyPart::LeftHand;
    case ContactRegion::RightArm:
    case ContactRegion::RightHand: return BodyPart::RightHand;
    case ContactRegion::LeftLeg:
    case ContactRegion::LeftFoot: return BodyPart::LeftLeg;
    case ContactRegion::RightLeg:
    case ContactRegion::RightFoot: return BodyPart::RightLeg;
    case ContactRegion::AttackApparatus: return BodyPart::AttackApparatus;
    case ContactRegion::Locomotion: return BodyPart::Locomotion;
    case ContactRegion::Core: return BodyPart::Core;
    case ContactRegion::Count: break;
    }
    std::abort();
}

bool ValidateReachProfile(const ReachProfile& reach) {
    return reach.distance >= 1 && reach.distance <= 2 && reach.height <= ReachHeight::Upper
        && reach.regions != 0 && (reach.regions & ~allContactMask) == 0;
}

bool ReachAllowsContact(const ReachProfile& reach, BodyKind body, ContactRegion region) {
    if (!ValidateReachProfile(reach) || !HasContact(body, region)
        || (reach.regions & ContactBit(region)) == 0) { return false; }
    const bool lower = region == ContactRegion::LeftLeg || region == ContactRegion::RightLeg
        || region == ContactRegion::LeftFoot || region == ContactRegion::RightFoot
        || region == ContactRegion::Locomotion;
    switch (reach.height) {
    case ReachHeight::Any: return true;
    case ReachHeight::Lower: return lower;
    case ReachHeight::Upper: return !lower;
    }
    return false;
}

}
