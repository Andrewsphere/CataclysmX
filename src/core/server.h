#pragma once

#include "core/world.h"

namespace cataclysmx {

enum class CommandKind { wait, move, pick_up, drop, interact };
struct Command {
    CommandKind kind = CommandKind::wait;
    int dx = 0;
    int dz = 0;
    // Zero picks the first ground/inventory item in persistent-ID order.
    PersistentId item = 0;
};
enum class Outcome {
    waited, moved, picked_up, dropped, opened, closed, toggled,
    blocked, occupied, nothing_here, invalid_command, not_carried, unavailable
};
const char* outcome_message(Outcome outcome);
struct CommandResult {
    Outcome outcome = Outcome::invalid_command;
    bool advanced = false;
    std::uint64_t tick = 0;
};

struct ViewCell {
    bool visible = false;
    Tile tile{};
    PersistentId actor = 0;
    bool player = false;
    unsigned int ground_items = 0;
    PersistentId object = 0;
    bool object_enabled = false;
    bool operator==(const ViewCell&) const = default;
};
struct InventoryEntry {
    PersistentId id = 0;
    ItemKind kind = ItemKind::rock;
    unsigned int contained_items = 0;
    bool operator==(const InventoryEntry&) const = default;
};
struct WorldView {
    TileCoord origin{};
    int width = 0;
    int height = 0; // Display rows along world Z, not world elevation.
    TileCoord player_position{};
    std::uint64_t tick = 0;
    Vector<ViewCell> cells; // Row major. Hidden cells contain no world information.
    Vector<InventoryEntry> inventory;
};

// Single-player authoritative local endpoint. No transport or wall-clock logic.
class Server {
public:
    explicit Server(std::uint32_t seed = 42);
    CommandResult send(Command command);
    WorldView view(int radius = 8) const;
    WorldResult<void> set_bubble_radii(int active_radius, int resident_radius);

    // Administrative/headless inspection; ordinary clients consume view().
    World& world();
    const World& world() const;
    ActorHandle player() const;
    std::uint64_t tick() const;

private:
    World world_;
    ActorHandle player_{};
    LoadRequest active_request_{};
    LoadRequest resident_request_{};
    int active_radius_ = 1;
    int resident_radius_ = 2;
    std::uint64_t tick_ = 0;
    std::uint32_t rng_state_ = 0;

    void update_bubble();
};

}
