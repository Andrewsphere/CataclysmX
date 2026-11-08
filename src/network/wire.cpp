#include "network/wire.h"

#include <EASTL/utility.h>

namespace cataclysmx::network::wire {

void Writer::U8(std::uint8_t v) { bytes.push_back(v); }
void Writer::U16(std::uint16_t v) { U8(static_cast<std::uint8_t>(v >> 8)); U8(static_cast<std::uint8_t>(v)); }
void Writer::U32(std::uint32_t v) { U16(static_cast<std::uint16_t>(v >> 16)); U16(static_cast<std::uint16_t>(v)); }
void Writer::U64(std::uint64_t v) { U32(static_cast<std::uint32_t>(v >> 32)); U32(static_cast<std::uint32_t>(v)); }
void Writer::I32(std::int32_t v) { U32(static_cast<std::uint32_t>(v)); }
bool Writer::Text(eastl::string_view v, std::size_t limit) {
    if (v.size() > limit || v.size() > 255) return false;
    for (unsigned char c : v) if (c < 32 || c == 127) return false;
    U8(static_cast<std::uint8_t>(v.size()));
    for (char c : v) U8(static_cast<std::uint8_t>(c));
    return true;
}
void Writer::CommandValue(const Command& v) {
    U8(static_cast<std::uint8_t>(v.kind)); I32(v.dx); I32(v.dz); U64(v.item); U32(v.waitCost);
}
void Writer::Result(const PlayerCommandResult& v) {
    U64(v.player); U64(v.sequence); U64(v.opportunity);
    U8(static_cast<std::uint8_t>(v.result.outcome)); U8(v.result.advanced ? 1 : 0);
    U64(v.result.tick); U64(v.result.cost);
    U8(v.result.storageError ? 1 : 0);
    if (v.result.storageError) U8(static_cast<std::uint8_t>(*v.result.storageError));
}
bool Writer::View(const WorldView& v) {
    if (v.width < 1 || v.width > 25 || v.height != v.width || (v.width & 1) == 0 ||
        v.cells.size() != static_cast<std::size_t>(v.width * v.height) ||
        v.inventory.size() > 2048 || v.players.size() > 32 ||
        v.playerId == 0 || v.opportunity == 0 || v.speed == 0 ||
        static_cast<std::int64_t>(v.origin.x) + v.width / 2 != v.player_position.x ||
        static_cast<std::int64_t>(v.origin.z) + v.height / 2 != v.player_position.z) return false;
    I32(v.origin.x); I32(v.origin.z); U8(static_cast<std::uint8_t>(v.width)); U8(static_cast<std::uint8_t>(v.height));
    I32(v.player_position.x); I32(v.player_position.z); U64(v.tick);
    U64(v.playerId); U64(v.revision); U64(v.opportunity); U64(v.lastInputSequence);
    U64(v.remainingTicks); U32(v.speed);
    U32(v.pacing.ticksPerSecond); U8(v.pacing.rateLimited ? 1 : 0);
    U8(v.pacing.pauseWhenIdle ? 1 : 0); U8(v.commandQueued ? 1 : 0);
    U16(static_cast<std::uint16_t>(v.cells.size()));
    for (const ViewCell& c : v.cells) {
        U8(c.visible ? 1 : 0);
        if (c.visible) {
            U32(c.tile.terrain.value); U8(c.tile.damage); U64(c.actor); U8(c.player ? 1 : 0);
            U32(c.ground_items); U64(c.object); U8(c.object_enabled ? 1 : 0);
        }
    }
    U16(static_cast<std::uint16_t>(v.inventory.size()));
    for (const InventoryEntry& i : v.inventory) {
        U64(i.id); U8(static_cast<std::uint8_t>(i.kind)); U32(i.contained_items);
    }
    U8(static_cast<std::uint8_t>(v.players.size()));
    for (const PlayerStatus& p : v.players) {
        U64(p.id); if (!Text({p.name.data(), p.name.size()}, 32)) return false;
        U8(static_cast<std::uint8_t>(p.state)); U8(p.ready ? 1 : 0);
        U8(p.commandQueued ? 1 : 0); U64(p.opportunity); U64(p.graceMicroseconds);
    }
    return bytes.size() <= maxPacket;
}
bool Reader::U8(std::uint8_t& v) { if (offset >= bytes.size()) return false; v = bytes[offset++]; return true; }
bool Reader::U16(std::uint16_t& v) { std::uint8_t a, b; if (!U8(a) || !U8(b)) return false; v = (std::uint16_t(a) << 8) | b; return true; }
bool Reader::U32(std::uint32_t& v) { std::uint16_t a, b; if (!U16(a) || !U16(b)) return false; v = (std::uint32_t(a) << 16) | b; return true; }
bool Reader::U64(std::uint64_t& v) { std::uint32_t a, b; if (!U32(a) || !U32(b)) return false; v = (std::uint64_t(a) << 32) | b; return true; }
bool Reader::I32(std::int32_t& v) { std::uint32_t raw; if (!U32(raw)) return false; v = static_cast<std::int32_t>(raw); return true; }
bool Reader::Text(String& v, std::size_t limit) {
    std::uint8_t n; if (!U8(n) || n > limit || n > bytes.size() - offset) return false;
    for (std::size_t i = 0; i < n; ++i)
        if (bytes[offset + i] < 32 || bytes[offset + i] == 127) return false;
    v.clear(); for (std::uint8_t i = 0; i < n; ++i) v.push_back(static_cast<char>(bytes[offset++]));
    return true;
}
bool Reader::CommandValue(Command& v) {
    std::uint8_t kind;
    if (!U8(kind) || kind > static_cast<std::uint8_t>(CommandKind::interact) ||
        !I32(v.dx) || !I32(v.dz) || !U64(v.item) || !U32(v.waitCost)) return false;
    v.kind = static_cast<CommandKind>(kind); return true;
}
bool Reader::Result(PlayerCommandResult& v) {
    std::uint8_t outcome, advanced;
    if (!U64(v.player) || !v.player || !U64(v.sequence) || !v.sequence ||
        !U64(v.opportunity) || !v.opportunity || !U8(outcome) ||
        outcome > static_cast<std::uint8_t>(Outcome::duplicate_input) || !U8(advanced) || advanced > 1 ||
        !U64(v.result.tick) || !U64(v.result.cost)) return false;
    std::uint8_t hasError;
    if (!U8(hasError) || hasError > 1) return false;
    v.result.storageError.reset();
    if (hasError) {
        std::uint8_t error;
        if (!U8(error) || error > static_cast<std::uint8_t>(SaveError::NotOpen)) return false;
        v.result.storageError = static_cast<SaveError>(error);
    }
    v.result.outcome = static_cast<Outcome>(outcome); v.result.advanced = advanced != 0; return true;
}
bool Reader::View(WorldView& v) {
    std::uint8_t w, h, rate, pause, queued; std::uint16_t count;
    if (!I32(v.origin.x) || !I32(v.origin.z) || !U8(w) || !U8(h) || w == 0 ||
        w > 25 || h != w || (w & 1) == 0 ||
        !I32(v.player_position.x) || !I32(v.player_position.z) || !U64(v.tick) ||
        !U64(v.playerId) || !U64(v.revision) || !U64(v.opportunity) || !U64(v.lastInputSequence) ||
        !U64(v.remainingTicks) || !U32(v.speed) || !U32(v.pacing.ticksPerSecond) ||
        !v.playerId || !v.opportunity || !v.speed || !v.pacing.ticksPerSecond ||
        static_cast<std::int64_t>(v.origin.x) + w / 2 != v.player_position.x ||
        static_cast<std::int64_t>(v.origin.z) + h / 2 != v.player_position.z ||
        !ValidPosition(v.player_position) || !U8(rate) || rate > 1 || !U8(pause) || pause > 1 ||
        !U8(queued) || queued > 1 || !U16(count) || count != w * h) return false;
    v.width = w; v.height = h; v.pacing.rateLimited = rate; v.pacing.pauseWhenIdle = pause;
    v.commandQueued = queued;
    v.cells.clear();
    for (unsigned i = 0; i < count; ++i) {
        ViewCell c; std::uint8_t visible;
        if (!U8(visible) || visible > 1) return false;
        c.visible = visible;
        if (c.visible) {
            std::uint8_t player, enabled;
            if (!U32(c.tile.terrain.value) || !U8(c.tile.damage) || !U64(c.actor) ||
                !U8(player) || player > 1 || !U32(c.ground_items) || !U64(c.object) ||
                !U8(enabled) || enabled > 1) return false;
            c.player = player; c.object_enabled = enabled;
        }
        v.cells.push_back(c);
    }
    std::uint16_t inventoryCount; if (!U16(inventoryCount) || inventoryCount > 2048) return false;
    v.inventory.clear();
    PersistentId lastItem = 0;
    for (unsigned i = 0; i < inventoryCount; ++i) {
        InventoryEntry e; std::uint8_t kind;
        if (!U64(e.id) || e.id <= lastItem || !U8(kind) || kind > static_cast<std::uint8_t>(ItemKind::bandage) ||
            !U32(e.contained_items)) return false;
        lastItem = e.id;
        e.kind = static_cast<ItemKind>(kind); v.inventory.push_back(e);
    }
    std::uint8_t n; if (!U8(n) || n > 32) return false;
    v.players.clear();
    PersistentId lastPlayer = 0;
    for (unsigned i = 0; i < n; ++i) {
        PlayerStatus p; std::uint8_t state, ready, pending;
        if (!U64(p.id) || p.id <= lastPlayer || !Text(p.name, 32) || p.name.empty() || !U8(state) ||
            state > static_cast<std::uint8_t>(PlayerConnectionState::Active) ||
            !U8(ready) || ready > 1 || !U8(pending) || pending > 1 ||
            !U64(p.opportunity) || !U64(p.graceMicroseconds)) return false;
        p.state = static_cast<PlayerConnectionState>(state); p.ready = ready; p.commandQueued = pending;
        lastPlayer = p.id;
        v.players.push_back(eastl::move(p));
    }
    return true;
}

}
