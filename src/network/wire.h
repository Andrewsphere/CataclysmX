#pragma once

#include "network/network.h"

#include <EASTL/span.h>

namespace cataclysmx::network::wire {

inline constexpr std::uint32_t protocol = 0x43584e03;
inline constexpr std::size_t maxPacket = 32768;
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
    bool View(const WorldView& v);
    void CommandValue(const Command& v);
    void Result(const PlayerCommandResult& v);
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
