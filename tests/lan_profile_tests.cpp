#include "client/lan_profile.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdio>
#include <string>

namespace {
std::string ProfilePath() {
    return "lan-profile-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".txt";
}
struct TemporaryFile {
    std::string path = ProfilePath();
    ~TemporaryFile() { std::remove(path.c_str()); }
};

TEST_CASE("LAN profile roundtrips independent endpoint identities and replaces only a matching port", "[client][lan]") {
    TemporaryFile file;
    cataclysmx::client::LanProfile profile;
    REQUIRE(profile.Set({"127.0.0.1", 24680, 123, "Alice"}));
    REQUIRE(profile.Set({"127.0.0.1", 24682, 456, "Bob"}));
    REQUIRE(profile.Set({"server.local", 24680, 789, "Zoë"}));
    REQUIRE(profile.Save(file.path.c_str()));
    cataclysmx::client::LanProfile loaded;
    REQUIRE(loaded.Load(file.path.c_str()));
    REQUIRE(loaded.Entries().size() == 3);
    CHECK(loaded.Find("127.0.0.1", 24680)->token == 123);
    CHECK(loaded.Find("127.0.0.1", 24682)->token == 456);
    CHECK(loaded.Find("server.local", 24680)->name == "Zoë");
    REQUIRE(loaded.Set({"127.0.0.1", 24680, 999, "Alice"}));
    CHECK(loaded.Find("127.0.0.1", 24682)->token == 456);
    CHECK_FALSE(loaded.Set({"bad\taddress", 24680, 1, "Name"}));
    CHECK_FALSE(loaded.Set({"server.local", 24680, 0, "Name"}));
    REQUIRE(loaded.Forget("127.0.0.1", 24680));
    CHECK(loaded.Find("127.0.0.1", 24680) == nullptr);
}

TEST_CASE("LAN profile rejects truncation without replacing loaded identities", "[client][lan]") {
    TemporaryFile file;
    cataclysmx::client::LanProfile profile;
    REQUIRE(profile.Set({"localhost", 24680, 72, "Keeper"}));
    std::FILE* output = std::fopen(file.path.c_str(), "wb");
    REQUIRE(output != nullptr);
    REQUIRE(std::fputs("CataclysmX-LAN-1\nlocalhost\t24680\t72\tKeeper\ntruncated", output) >= 0);
    REQUIRE(std::fclose(output) == 0);
    const auto truncated = profile.Load(file.path.c_str());
    REQUIRE_FALSE(truncated);
    CHECK(truncated.error() == cataclysmx::client::ProfileError::Malformed);
    REQUIRE(profile.Find("localhost", 24680));
    CHECK(profile.Find("localhost", 24680)->token == 72);
}

TEST_CASE("LAN profile refuses occupied temporary names and keeps existing file", "[client][lan]") {
    TemporaryFile file;
    cataclysmx::client::LanProfile profile;
    REQUIRE(profile.Set({"localhost", 24680, 72, "Keeper"}));
    REQUIRE(profile.Save(file.path.c_str()));
    std::string collisions[32];
    for (int i = 0; i < 32; ++i) {
        collisions[i] = file.path + ".tmp" + std::to_string(i);
        auto* output = std::fopen(collisions[i].c_str(), "wb");
        REQUIRE(output != nullptr);
        REQUIRE(std::fclose(output) == 0);
    }
    REQUIRE(profile.Set({"localhost", 24680, 99, "Keeper"}));
    const auto blocked = profile.Save(file.path.c_str());
    REQUIRE_FALSE(blocked);
    CHECK(blocked.error() == cataclysmx::client::ProfileError::TemporaryCollision);
    cataclysmx::client::LanProfile loaded;
    REQUIRE(loaded.Load(file.path.c_str()));
    CHECK(loaded.Find("localhost", 24680)->token == 72);
    for (const auto& path : collisions) { std::remove(path.c_str()); }
}

TEST_CASE("LAN profile caps endpoints without overwriting earlier identities", "[client][lan]") {
    cataclysmx::client::LanProfile profile;
    for (int i = 0; i < 64; ++i) {
        REQUIRE(profile.Set({"localhost", static_cast<std::uint16_t>(1000 + i), static_cast<std::uint64_t>(i + 1), "Guest"}));
    }
    const auto overflow = profile.Set({"localhost", 1064, 65, "Extra"});
    REQUIRE_FALSE(overflow);
    CHECK(overflow.error() == cataclysmx::client::ProfileError::Full);
    CHECK(profile.Find("localhost", 1000)->token == 1);
    CHECK(profile.Find("localhost", 1064) == nullptr);
}
}
