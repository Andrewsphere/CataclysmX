#pragma once

#include "network/network.h"

#include <EASTL/span.h>

namespace cataclysmx::network::wire {

// Version 10 adds combat target and style to command payloads.
inline constexpr std::uint32_t protocol = 0x43584e0a;
inline constexpr int maxViewRadius = 64;
inline constexpr int maxViewDimension = 2 * maxViewRadius + 1;
static_assert(maxViewDimension <= UINT8_MAX && maxViewDimension * maxViewDimension <= UINT16_MAX);
// Worst-case 129 squared cells use 632358 bytes (one 38-byte run per cell),
// before inventory and session metadata. Repeated cells use one run instead.
inline constexpr std::size_t maxPacket = 1024 * 1024;
inline constexpr std::size_t maxItemEntries = 2048;
inline constexpr std::size_t maxItemActions = maxItemActionsPerItem;
// Validate definition-dependent metadata after structural decoding, before publication.
bool ValidItemDefinitions(const WorldView& view, const ContentCatalog& content);
bool ValidTerrainDefinitions(const WorldView& view, const ContentCatalog& content);
enum class Type : std::uint8_t {
    Hello = 1, Accepted, Ready, Snapshot, Input, Receipt, Rejection,
    Closing, Query, Advertisement, SnapshotAck
};

struct Writer {
    Vector<std::uint8_t> bytes;
    void U8(std::uint8_t v);
    void U16(std::uint16_t v);
    void U32(std::uint32_t v);
    void U64(std::uint64_t v);
    void I32(std::int32_t v);
    bool Text(eastl::string_view v, std::size_t limit);
    // Expanded cell count, then (u16 run length, 36-byte cell payload) records.
    bool View(const WorldView& v);
    bool CommandValue(const Command& v);
    bool Result(const PlayerCommandResult& v);
};
struct Reader {
    eastl::span<const std::uint8_t> bytes;
    std::size_t offset = 0;
    bool U8(std::uint8_t& v);
    bool U16(std::uint16_t& v);
    bool U32(std::uint32_t& v);
    bool U64(std::uint64_t& v);
    bool I32(std::int32_t& v);
    bool Text(String& v, std::size_t limit);
    bool View(WorldView& v);
    bool CommandValue(Command& v);
    bool Result(PlayerCommandResult& v);
    bool Done() const { return offset == bytes.size(); }
};

}
