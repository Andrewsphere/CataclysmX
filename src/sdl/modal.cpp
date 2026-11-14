#include "sdl/modal.h"

namespace cataclysmx::sdl {

void ModalController::BeginOpen(ModalScreen screen) {
    requested_ = screen;
    pending_ = true;
    actionsCursor_.reset();
    selectedAction_ = 0;
    targetScroll_ = 0;
    actionScroll_ = 0;
    heldKey_.Reset();
}

void ModalController::Paused() {
    active_ = requested_;
    pending_ = false;
    heldKey_.Reset();
}

void ModalController::BeginClose() {
    pending_ = true;
    heldKey_.Reset();
}

void ModalController::Closed() {
    active_ = ModalScreen::None;
    requested_ = ModalScreen::None;
    pending_ = false;
    heldKey_.Reset();
}

void ModalController::EnterActions(int& scroll) {
    targetCursor_ = cursor_;
    targetScroll_ = scroll;
    if (!actionsCursor_ || *actionsCursor_ != cursor_) {
        selectedAction_ = 0;
        actionScroll_ = 0;
    }
    actionsCursor_ = cursor_;
    scroll = actionScroll_;
    active_ = ModalScreen::Actions;
    heldKey_.Reset();
}

void ModalController::BackToTargets(int& scroll) {
    actionScroll_ = scroll;
    scroll = targetScroll_;
    cursor_ = targetCursor_;
    active_ = ModalScreen::Targets;
    heldKey_.Reset();
}

}
