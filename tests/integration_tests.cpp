#include "core/integration.h"

#include <catch2/catch_test_macros.hpp>

#if !defined(__cpp_exceptions) && !defined(_CPPUNWIND)
#error Catch2 harness must compile with exceptions
#endif

TEST_CASE("Containers and stale SlotMap keys work in no-exceptions engine", "[core_smoke]") {
    REQUIRE(cataclysmx::core_smoke());
}

TEST_CASE("yyjson accepts comments and trailing commas and rejects bad input", "[json_smoke]") {
    REQUIRE(cataclysmx::json_smoke("{/* comment */\"seed\":42,}"));
    REQUIRE(cataclysmx::json_smoke("{seed:0x2a, label:'JSON5',}"));
    CHECK_FALSE(cataclysmx::json_smoke("{broken"));
    CHECK_FALSE(cataclysmx::json_smoke("{\"seed\":7}"));
}

TEST_CASE("Fixed seed produces a portable checksum", "[headless_smoke]") {
    CHECK(cataclysmx::headless_checksum(42) == 3365722423U);
    CHECK(cataclysmx::headless_checksum(43) != cataclysmx::headless_checksum(42));
}

TEST_CASE("Compiled spdlog writes to console and rotating file", "[logging_smoke]") {
    REQUIRE(cataclysmx::logging_smoke("."));
}

TEST_CASE("Intentional assertion failure verifies Catch2 exit status", "[.intentional-failure]") {
    REQUIRE(false);
}
