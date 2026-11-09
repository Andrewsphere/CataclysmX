#include "terminal/keyboard.h"

#include <catch2/catch_test_macros.hpp>

namespace {
using namespace cataclysmx;

TEST_CASE("Stair bindings use requested symbols in directional contexts", "[terminal][input]") {
    const auto keys = DefaultKeymap();
    REQUIRE(ValidateKeymap(keys));
    for (auto context : {InputContext::Gameplay, InputContext::Interaction, InputContext::Items}) {
        const auto down = DecodeTerminalInput("<");
        const auto up = DecodeTerminalInput(">");
        REQUIRE(down); REQUIRE(up);
        CHECK(ResolveAction(keys, context, *down) == InputAction::MoveDown);
        CHECK(ResolveAction(keys, context, *up) == InputAction::MoveUp);
    }
}

TEST_CASE("Terminal decoder preserves character case and distinguishes enhanced Ctrl case", "[terminal][input]") {
    CHECK(DecodeTerminalInput("t") == KeyChord{"t", false});
    CHECK(DecodeTerminalInput("T") == KeyChord{"T", false});
    CHECK(DecodeTerminalInput("\x14") == KeyChord{"t", true});
    CHECK(DecodeTerminalInput("\x1b[116;5u") == KeyChord{"t", true});
    CHECK(DecodeTerminalInput("\x1b[116;6u") == KeyChord{"T", true});
    CHECK(DecodeTerminalInput("\x1b[116:84;6u") == KeyChord{"T", true});
    CHECK(DecodeTerminalInput("\x1b[84;5u") == KeyChord{"T", true});
    CHECK(DecodeTerminalInput("\x1b[116;69u") == KeyChord{"T", true}); // Caps Lock + Ctrl.
    CHECK(DecodeTerminalInput("\x1b[116;70u") == KeyChord{"t", true}); // Caps Lock + Shift + Ctrl.
    CHECK(DecodeTerminalInput("\x1b[116;5:2u") == KeyChord{"t", true});
    CHECK_FALSE(DecodeTerminalInput("\x1b[116;5:3u"));
    CHECK(DecodeTerminalInput("界") == KeyChord{"界", false});
    CHECK(DecodeTerminalInput("\x1b[30028u") == KeyChord{"界", false});
    CHECK(InspectTerminalInput("\x14") == "14 => Ctrl+t");
}

TEST_CASE("Terminal decoder rejects Alt malformed sequences and unsupported modifier chords", "[terminal][input]") {
    for (const char* input : {"\x1bt", "\x1b[116;3u", "\x1b[116;7u", "\x1b[116;9u",
        "\x1b[116;0u", "\x1b[116;999999999999u", "\x1b[116;5:4u", "\x1b[116;5:3u",
        "\x1b[116:bad;6u", "\x1b[1114112u", "\x1b[55296u", "\x1b[57344u", "\x1b[?5u", "ab", ""}) {
        INFO(input); CHECK_FALSE(DecodeTerminalInput(input));
    }
    CHECK(DecodeTerminalInput("\t") == KeyChord{"Tab", false});
    CHECK(DecodeTerminalInput("\r") == KeyChord{"Enter", false});
    CHECK(DecodeTerminalInput("\x1b[9;5u") == KeyChord{"Tab", true});
    CHECK(DecodeTerminalInput("\x1b[105;5u") == KeyChord{"i", true});
    CHECK(DecodeTerminalInput("\x1b[1;5A") == KeyChord{"Up", true});
    CHECK(DecodeTerminalInput("\x1b[1;1Q") == KeyChord{"F2", false});
    CHECK(DecodeTerminalInput("\x1b[1;5Q") == KeyChord{"F2", true});
    CHECK(DecodeTerminalInput("\x1b[15;5~") == KeyChord{"F5", true});
    CHECK(DecodeTerminalInput("\x1b[24;5~") == KeyChord{"F12", true});
    CHECK(DecodeTerminalInput("\x1b[9;2u") == KeyChord{"BackTab", false});
    CHECK_FALSE(DecodeTerminalInput("\x1b[1;6A"));
    CHECK(DecodeTerminalInput("\x1b[Z") == KeyChord{"BackTab", false});
}

TEST_CASE("Case sensitive Ctrl bindings resolve to separate actions after terminal decoding", "[terminal][input]") {
    const Keymap map{{InputContext::Gameplay, InputAction::Wait, {{"t", true}}},
        {InputContext::Gameplay, InputAction::PickUp, {{"T", true}}}};
    REQUIRE(ValidateKeymap(map));
    const auto lower = DecodeTerminalInput("\x1b[116;5u");
    const auto upper = DecodeTerminalInput("\x1b[116;6u");
    REQUIRE(lower); REQUIRE(upper);
    CHECK(ResolveAction(map, InputContext::Gameplay, *lower) == InputAction::Wait);
    CHECK(ResolveAction(map, InputContext::Gameplay, *upper) == InputAction::PickUp);
}

}
