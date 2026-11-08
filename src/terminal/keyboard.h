#pragma once

#include "client/input.h"

namespace cataclysmx {

// Decodes legacy bytes and the CSI-u subset used for case-sensitive Ctrl keys.
// No reads, clocks, or global keyboard state. Unknown/Alt/release events are ignored.
eastl::optional<KeyChord> DecodeTerminalInput(eastl::string_view input);
String InspectTerminalInput(eastl::string_view input);

class TerminalKeyboard {
public:
    ~TerminalKeyboard();
    // Call after entering the terminal's alternate screen, and Stop before exit.
    void Query();
    bool HandleResponse(eastl::string_view input);
    void Stop();
    bool Enhanced() const;
private:
    bool queried_ = false;
    bool pushed_ = false;
    bool enhanced_ = false;
};

}
