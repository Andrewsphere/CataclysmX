#pragma once

#include "client/localization.h"

namespace cataclysmx {

struct ControlHelpRow {
    String label;
    String keys;
    bool heading = false;
};

Vector<ControlHelpRow> CollectControlsHelp(const Keymap& keys, const Localization& loc, bool sdl);

}
