#pragma once

#include "core/server.h"
#include <cstdint>
#include <string>
#include <vector>

namespace cataclysmx::headless {
struct LanOptions {
    bool host = false;
    bool discover = false;
    bool newCharacter = false;
    std::string join;
    std::string name = "Guest";
    std::string profile = "cataclysmx-lan-profile.txt";
    std::string discoveryAddress;
    std::uint16_t port = 24680;
    std::uint16_t discoveryPort = 24681;
    std::uint64_t runForMs = 1000;
    std::vector<std::string> commands;
};
int RunLan(Server& server, const ContentCatalog& content, const LanOptions& options);
}
