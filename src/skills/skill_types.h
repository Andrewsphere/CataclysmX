#pragma once

#include <cstdint>

namespace cataclysmx {
struct SkillId {
    std::uint32_t value = 0;
    bool operator==(const SkillId&) const = default;
};
struct BookId {
    std::uint32_t value = 0;
    bool operator==(const BookId&) const = default;
};
enum class SkillCategory : std::uint8_t { Noncombat, Combat };
enum class SkillActivity : std::uint8_t { Craft, Disassembly, Combat };
inline constexpr std::uint32_t maxSkillId = (1u << 12) - 1;
inline constexpr std::uint8_t maximumSkillLevel = 10;
}
