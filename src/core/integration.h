#pragma once

#include <cstdint>

namespace cataclysmx {

// Build-integration probes; these are not the game's Simulation API.
bool core_smoke();
bool json_smoke(const char* json);
bool logging_smoke(const char* directory);
[[noreturn]] void logging_failure_probe();
std::uint32_t headless_checksum(std::uint32_t seed);

}
