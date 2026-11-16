#pragma once

#include "core/containers.h"

#include <EASTL/expected.h>
#include <EASTL/span.h>

#include <cstddef>
#include <cstdint>

namespace cataclysmx {

// One-based catalog-local index; zero is invalid. Persist the string ID instead.
struct FlagId {
    std::uint32_t value = 0;
    bool operator==(const FlagId&) const = default;
};

enum class FlagDomain : std::uint8_t { Item, Monster, Terrain, Object };
enum class FlagKind : std::uint8_t { Category, Property };
enum class FlagDomainError : std::uint8_t { InvalidDomain, DuplicateDomain };

// Empty while parsing; a published flag definition must allow at least one domain.
class FlagDomains {
public:
    eastl::expected<void, FlagDomainError> Add(FlagDomain domain);
    bool Contains(FlagDomain domain) const;
    bool Empty() const;
    bool operator==(const FlagDomains&) const = default;

private:
    std::uint8_t bits_ = 0;
};

// Descriptive metadata. A flag has effects only where an explicit consumer uses it.
struct FlagDefinition {
    String id;
    String name;
    String description;
    FlagDomains domains;
    FlagKind kind = FlagKind::Property;
    bool operator==(const FlagDefinition&) const = default;
};

enum class FlagSetError : std::uint8_t { InvalidId, DuplicateId, UnsortedIds };

// Definition-owned sparse set. Catalog existence and domain checks belong to the loader.
class FlagSet {
public:
    FlagSet() = default;
    // Takes ownership on success. Requires strictly increasing, nonzero IDs.
    static eastl::expected<FlagSet, FlagSetError> FromSorted(Vector<FlagId> ids);
    bool Contains(FlagId id) const;
    bool Empty() const;
    std::size_t Size() const;
    eastl::span<const FlagId> Ids() const;
    bool operator==(const FlagSet& other) const;

private:
    Vector<FlagId> ids_;
};

}
