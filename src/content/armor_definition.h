#pragma once

#include "combat/contact.h"
#include "combat/damage.h"
#include "core/containers.h"
#include "core/ratio.h"

namespace cataclysmx {

enum class ArmorLayer : std::uint8_t { Under, Normal, Outer, Strapped };
struct ArmorRegionDefinition {
    ContactRegion region = ContactRegion::Torso;
    std::uint32_t coverage = ratioScale;
    std::uint16_t encumbrance = 0;
    bool overlapExempt = false;
    bool operator==(const ArmorRegionDefinition&) const = default;
};
struct ArmorDefinition {
    ArmorLayer layer = ArmorLayer::Normal;
    SmallVector<ArmorRegionDefinition, 2> regions;
    ProtectionValues protection{};
    // Same-layer overlapping rigid regions conflict, independently of exemption.
    ContactMask rigidRegions = 0;
    std::uint32_t wearCost = 100;
    std::uint32_t removeCost = 100;
    bool operator==(const ArmorDefinition&) const = default;
};
struct EquipmentModifiers {
    eastl::array<std::uint32_t, contactRegionCount> encumbrance{};
    bool operator==(const EquipmentModifiers&) const = default;
};

}
