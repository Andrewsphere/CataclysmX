#pragma once

#include "sdl/input.h"
#include "core/world_types.h"

namespace cataclysmx::sdl {

enum class ModalScreen { None, Help, Examine, Targets, Actions, Options, Debug };

// Main-thread UI state only. The caller submits the authoritative pause/resume
// operation first, then enters its pending transition here.
class ModalController {
public:
    explicit ModalController(HeldKey& heldKey) : heldKey_(heldKey) {}
    ModalScreen Active() const { return active_; }
    bool Pending() const { return pending_; }
    bool CapturesInput() const { return pending_ || active_ != ModalScreen::None; }
    void BeginOpen(ModalScreen screen);
    void Paused();
    void BeginClose();
    void Closed();
    void EnterActions(int& scroll);
    void BackToTargets(int& scroll);
    TileCoord& Cursor() { return cursor_; }
    std::size_t& ActionSelection() { return selectedAction_; }
private:
    HeldKey& heldKey_;
    ModalScreen active_ = ModalScreen::None;
    ModalScreen requested_ = ModalScreen::None;
    bool pending_ = false;
    TileCoord cursor_{};
    eastl::optional<TileCoord> actionsCursor_;
    TileCoord targetCursor_{};
    std::size_t selectedAction_ = 0;
    int targetScroll_ = 0;
    int actionScroll_ = 0;
};

}
