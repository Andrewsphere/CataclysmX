#include "content/definition_flags.h"

#include <catch2/catch_test_macros.hpp>
#include <EASTL/utility.h>

#include <limits>

namespace {
using namespace cataclysmx;

TEST_CASE("Flag domains reject invalid and duplicate membership without mutation", "[content][flags]") {
    FlagDomains domains;
    CHECK(domains.Empty());
    CHECK_FALSE(domains.Contains(FlagDomain::Item));
    REQUIRE(domains.Add(FlagDomain::Item));
    REQUIRE(domains.Add(FlagDomain::Terrain));
    const auto before = domains;
    const auto duplicate = domains.Add(FlagDomain::Item);
    REQUIRE_FALSE(duplicate);
    CHECK(duplicate.error() == FlagDomainError::DuplicateDomain);
    const auto invalid = static_cast<FlagDomain>(255);
    const auto rejected = domains.Add(invalid);
    REQUIRE_FALSE(rejected);
    CHECK(rejected.error() == FlagDomainError::InvalidDomain);
    CHECK(domains == before);
    CHECK_FALSE(domains.Contains(invalid));
    CHECK(domains.Contains(FlagDomain::Item));
    CHECK(domains.Contains(FlagDomain::Terrain));
    CHECK_FALSE(domains.Contains(FlagDomain::Monster));
    CHECK_FALSE(domains.Contains(FlagDomain::Object));
    REQUIRE(domains.Add(FlagDomain::Monster));
    REQUIRE(domains.Add(FlagDomain::Object));
    CHECK(domains.Contains(FlagDomain::Monster));
    CHECK(domains.Contains(FlagDomain::Object));
    CHECK_FALSE(domains.Empty());
}

TEST_CASE("Empty flag sets and sparse membership have value semantics", "[content][flags]") {
    FlagSet empty;
    CHECK(empty.Empty());
    CHECK(empty.Size() == 0);
    CHECK(empty.Ids().empty());
    CHECK_FALSE(empty.Contains({}));
    CHECK_FALSE(empty.Contains({1}));
    const auto builtEmpty = FlagSet::FromSorted({});
    REQUIRE(builtEmpty);
    CHECK(*builtEmpty == empty);

    const auto maximum = std::numeric_limits<std::uint32_t>::max();
    auto set = FlagSet::FromSorted({{2}, {65}, {maximum}});
    REQUIRE(set);
    CHECK(set->Size() == 3);
    CHECK(set->Contains({2}));
    CHECK(set->Contains({65}));
    CHECK(set->Contains({maximum}));
    CHECK_FALSE(set->Contains({}));
    CHECK_FALSE(set->Contains({1}));
    CHECK_FALSE(set->Contains({3}));
    CHECK_FALSE(set->Contains({maximum - 1}));
    CHECK(set->Ids()[1] == FlagId{65});
    auto copy = *set;
    CHECK(copy == *set);
    auto moved = eastl::move(copy);
    CHECK(moved == *set);
    CHECK_FALSE(moved == empty);
}

TEST_CASE("Flag set construction rejects malformed sorted input", "[content][flags]") {
    const auto zero = FlagSet::FromSorted({{0}});
    REQUIRE_FALSE(zero);
    CHECK(zero.error() == FlagSetError::InvalidId);
    const auto duplicate = FlagSet::FromSorted({{1}, {2}, {2}});
    REQUIRE_FALSE(duplicate);
    CHECK(duplicate.error() == FlagSetError::DuplicateId);
    const auto unsorted = FlagSet::FromSorted({{2}, {1}});
    REQUIRE_FALSE(unsorted);
    CHECK(unsorted.error() == FlagSetError::UnsortedIds);
}

TEST_CASE("Definition flag sets support more than 64 members", "[content][flags]") {
    Vector<FlagId> ids;
    for (std::uint32_t value = 1; value <= 130; ++value) { ids.push_back({value}); }
    const auto set = FlagSet::FromSorted(eastl::move(ids));
    REQUIRE(set);
    CHECK(set->Size() == 130);
    for (std::uint32_t value = 1; value <= 130; ++value) { CHECK(set->Contains({value})); }
    CHECK_FALSE(set->Contains({131}));
}

}
