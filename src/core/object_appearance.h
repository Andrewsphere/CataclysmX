#pragma once

#include <cstdint>

namespace cataclysmx {

struct ObjectTypeId {
    std::uint32_t value = 0;
    bool operator==(const ObjectTypeId&) const = default;
};

// Closed semantic identities, never content ordinals or renderer sprite indices.
enum class ObjectKind : std::uint8_t { None, Generic, Chair, Table, Lamp };
enum class ObjectFacing : std::uint8_t { North, East, South, West };

// Owned observation data, independent of machine/behavior state.
struct ObjectAppearance {
    ObjectKind kind = ObjectKind::None;
    ObjectFacing facing = ObjectFacing::North;
    bool active = false;
    bool broken = false;
    ObjectTypeId type{}; // Observed catalog identity; zero preserves legacy generic artwork.
    bool operator==(const ObjectAppearance&) const = default;
};

constexpr bool ValidObjectAppearance(const ObjectAppearance& appearance) {
    return appearance.kind <= ObjectKind::Lamp && appearance.facing <= ObjectFacing::West
        && (appearance.kind != ObjectKind::None || appearance == ObjectAppearance{});
}

}
