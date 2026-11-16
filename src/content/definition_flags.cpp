#include "content/definition_flags.h"

#include <EASTL/utility.h>

namespace cataclysmx {
namespace {

std::uint8_t DomainBit(FlagDomain domain) {
    switch (domain) {
    case FlagDomain::Item: return 1;
    case FlagDomain::Monster: return 2;
    case FlagDomain::Terrain: return 4;
    case FlagDomain::Object: return 8;
    }
    return 0;
}

}

eastl::expected<void, FlagDomainError> FlagDomains::Add(FlagDomain domain) {
    const auto bit = DomainBit(domain);
    if (!bit) { return eastl::unexpected(FlagDomainError::InvalidDomain); }
    if (bits_ & bit) { return eastl::unexpected(FlagDomainError::DuplicateDomain); }
    bits_ = static_cast<std::uint8_t>(bits_ | bit);
    return {};
}

bool FlagDomains::Contains(FlagDomain domain) const { return (bits_ & DomainBit(domain)) != 0; }
bool FlagDomains::Empty() const { return bits_ == 0; }

eastl::expected<FlagSet, FlagSetError> FlagSet::FromSorted(Vector<FlagId> ids) {
    std::uint32_t previous = 0;
    for (const auto id : ids) {
        if (!id.value) { return eastl::unexpected(FlagSetError::InvalidId); }
        if (id.value == previous) { return eastl::unexpected(FlagSetError::DuplicateId); }
        if (id.value < previous) { return eastl::unexpected(FlagSetError::UnsortedIds); }
        previous = id.value;
    }
    FlagSet result;
    result.ids_ = eastl::move(ids);
    return result;
}

bool FlagSet::Contains(FlagId id) const {
    std::size_t first = 0;
    std::size_t last = ids_.size();
    while (first < last) {
        const auto middle = first + (last - first) / 2;
        if (ids_[middle].value < id.value) {
            first = middle + 1;
        } else {
            last = middle;
        }
    }
    return first < ids_.size() && ids_[first] == id;
}

bool FlagSet::Empty() const { return ids_.empty(); }
std::size_t FlagSet::Size() const { return ids_.size(); }
eastl::span<const FlagId> FlagSet::Ids() const { return {ids_.data(), ids_.size()}; }
bool FlagSet::operator==(const FlagSet& other) const { return ids_ == other.ids_; }

}
