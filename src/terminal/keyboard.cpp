#include "terminal/keyboard.h"

#include <charconv>
#include <cstdio>

namespace cataclysmx {
namespace {

bool Number(eastl::string_view text, unsigned& value) {
    if (text.empty()) { return false; }
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}
String Scalar(unsigned scalar) {
    String text;
    if (scalar < 0x80) { text += static_cast<char>(scalar); }
    else if (scalar < 0x800) {
        text += static_cast<char>(0xc0 | (scalar >> 6)); text += static_cast<char>(0x80 | (scalar & 63));
    } else if (scalar < 0x10000) {
        text += static_cast<char>(0xe0 | (scalar >> 12)); text += static_cast<char>(0x80 | ((scalar >> 6) & 63));
        text += static_cast<char>(0x80 | (scalar & 63));
    } else if (scalar <= 0x10ffff) {
        text += static_cast<char>(0xf0 | (scalar >> 18)); text += static_cast<char>(0x80 | ((scalar >> 12) & 63));
        text += static_cast<char>(0x80 | ((scalar >> 6) & 63)); text += static_cast<char>(0x80 | (scalar & 63));
    }
    return text;
}
eastl::optional<KeyChord> CsiKey(eastl::string_view input) {
    // CSI key[:shifted[:base]] ; modifiers[:event] u. We request flags 1|4:
    // disambiguation plus alternate keys, leaving ordinary text entry unchanged.
    auto body = input.substr(2, input.size() - 3);
    const auto separator = body.find(';');
    auto keyPart = body.substr(0, separator);
    auto modifierPart = separator == eastl::string_view::npos ? eastl::string_view("1") : body.substr(separator + 1);
    unsigned event = 1;
    const auto eventSeparator = modifierPart.find(':');
    if (eventSeparator != eastl::string_view::npos) {
        if (!Number(modifierPart.substr(eventSeparator + 1), event) || event == 0 || event > 3) { return {}; }
        modifierPart = modifierPart.substr(0, eventSeparator);
    }
    if (event == 3) { return {}; }
    unsigned encodedModifiers = 0;
    if (!Number(modifierPart, encodedModifiers) || encodedModifiers == 0) { return {}; }
    const unsigned modifiers = encodedModifiers - 1;
    // Shift is used only to determine character case; it is not a bindable flag.
    if (modifiers & ~(1U | 2U | 4U | 64U | 128U)) { return {}; }
    unsigned key = 0;
    unsigned shifted = 0;
    const auto alternate = keyPart.find(':');
    if (!Number(keyPart.substr(0, alternate), key)) { return {}; }
    if (alternate != eastl::string_view::npos) {
        auto value = keyPart.substr(alternate + 1);
        const auto base = value.find(':');
        if (!value.substr(0, base).empty() && !Number(value.substr(0, base), shifted)) { return {}; }
        if (base != eastl::string_view::npos) {
            unsigned ignored = 0;
            if (!Number(value.substr(base + 1), ignored)) { return {}; }
        }
    }
    const bool ctrl = (modifiers & 4) != 0;
    if (key == 9 && (modifiers & 1)) { return KeyChord{"BackTab", ctrl}; }
    if ((modifiers & 1) && (key == 13 || key == 27 || key == 127)) { return {}; }
    if (key == 27) { return KeyChord{"Escape", ctrl}; }
    if (key == 13) { return KeyChord{"Enter", ctrl}; }
    if (key == 9) { return KeyChord{"Tab", ctrl}; }
    if (key == 127) { return KeyChord{"Backspace", ctrl}; }
    // Protocol functional/modifier keys occupy the private-use range. Named
    // navigation/function keys retain their standard CSI forms, decoded below.
    if (key >= 57344 && key <= 63743) { return {}; }
    if (key >= 'a' && key <= 'z') {
        if (((modifiers & 1) != 0) != ((modifiers & 64) != 0)) { key -= 'a' - 'A'; }
    } else if ((modifiers & 1) && shifted) { key = shifted; }
    KeyChord chord{Scalar(key), ctrl, (modifiers & 2) != 0};
    return ValidKey(chord) ? eastl::optional<KeyChord>(chord) : eastl::nullopt;
}
eastl::optional<KeyChord> CsiFunctional(eastl::string_view input) {
    auto body = input.substr(2, input.size() - 3);
    const auto separator = body.find(';');
    unsigned number = 1;
    const auto numberPart = body.substr(0, separator);
    if (!numberPart.empty() && !Number(numberPart, number)) { return {}; }
    unsigned encodedModifiers = 1;
    if (separator != eastl::string_view::npos) {
        auto modifierPart = body.substr(separator + 1);
        const auto eventSeparator = modifierPart.find(':');
        if (eventSeparator != eastl::string_view::npos) {
            unsigned event = 0;
            if (!Number(modifierPart.substr(eventSeparator + 1), event) || event < 1 || event > 2) { return {}; }
            modifierPart = modifierPart.substr(0, eventSeparator);
        }
        if (!Number(modifierPart, encodedModifiers) || encodedModifiers == 0) { return {}; }
    }
    const auto modifiers = encodedModifiers - 1;
    if (modifiers & ~(1U | 2U | 4U | 64U | 128U)) { return {}; }
    const char* name = nullptr;
    if (input.back() == '~') {
        switch (number) {
        case 1: case 7: name = "Home"; break;
        case 4: case 8: name = "End"; break;
        case 2: name = "Insert"; break; case 3: name = "Delete"; break;
        case 5: name = "PageUp"; break; case 6: name = "PageDown"; break;
        case 11: name = "F1"; break; case 12: name = "F2"; break;
        case 13: name = "F3"; break; case 14: name = "F4"; break;
        case 15: name = "F5"; break; case 17: name = "F6"; break;
        case 18: name = "F7"; break; case 19: name = "F8"; break;
        case 20: name = "F9"; break; case 21: name = "F10"; break;
        case 23: name = "F11"; break; case 24: name = "F12"; break;
        default: return {};
        }
    } else if (number == 1) {
        switch (input.back()) {
        case 'A': name = "Up"; break; case 'B': name = "Down"; break;
        case 'C': name = "Right"; break; case 'D': name = "Left"; break;
        case 'H': name = "Home"; break; case 'F': name = "End"; break;
        case 'P': name = "F1"; break; case 'Q': name = "F2"; break;
        case 'R': name = "F3"; break; case 'S': name = "F4"; break;
        default: return {};
        }
    }
    return name ? eastl::optional<KeyChord>(KeyChord{String(name), (modifiers & 4) != 0, (modifiers & 2) != 0, (modifiers & 1) != 0}) : eastl::nullopt;
}
struct NamedSequence { eastl::string_view input; eastl::string_view key; bool ctrl = false; };
constexpr NamedSequence sequences[] = {
    {"\x1b", "Escape"}, {"\n", "Enter"}, {"\r", "Enter"}, {"\t", "Tab"}, {"\x1b[Z", "BackTab"},
    {"\x7f", "Backspace"}, {"\x08", "Backspace"},
    {"\x1b[A", "Up"}, {"\x1b[B", "Down"}, {"\x1b[C", "Right"}, {"\x1b[D", "Left"},
    {"\x1b[H", "Home"}, {"\x1b[F", "End"}, {"\x1b[2~", "Insert"}, {"\x1b[3~", "Delete"},
    {"\x1b[5~", "PageUp"}, {"\x1b[6~", "PageDown"},
    {"\x1bOP", "F1"}, {"\x1bOQ", "F2"}, {"\x1bOR", "F3"}, {"\x1bOS", "F4"},
    {"\x1b[15~", "F5"}, {"\x1b[17~", "F6"}, {"\x1b[18~", "F7"}, {"\x1b[19~", "F8"},
    {"\x1b[20~", "F9"}, {"\x1b[21~", "F10"}, {"\x1b[23~", "F11"}, {"\x1b[24~", "F12"},
    {"\x1b[1;5A", "Up", true}, {"\x1b[1;5B", "Down", true},
    {"\x1b[1;5C", "Right", true}, {"\x1b[1;5D", "Left", true},
    {"\x1b[1;5H", "Home", true}, {"\x1b[1;5F", "End", true},
    {"\x1b[2;5~", "Insert", true}, {"\x1b[3;5~", "Delete", true},
    {"\x1b[5;5~", "PageUp", true}, {"\x1b[6;5~", "PageDown", true}
};

}

eastl::optional<KeyChord> DecodeTerminalInput(eastl::string_view input) {
    if (input.size() > 128 || input.empty()) { return {}; }
    if (input.size() >= 4 && input.substr(0, 2) == "\x1b[" && input.back() == 'u') { return CsiKey(input); }
    for (const auto& sequence : sequences) {
        if (input == sequence.input) { return KeyChord{String(sequence.key.data(), sequence.key.size()), sequence.ctrl}; }
    }
    if (input.size() >= 3 && input.substr(0, 2) == "\x1b[") { return CsiFunctional(input); }
    if (input.size() == 1 && input[0] >= 1 && input[0] <= 26) {
        String letter; letter += static_cast<char>('a' + input[0] - 1); return KeyChord{letter, true};
    }
    KeyChord chord{String(input.data(), input.size()), false};
    return ValidKey(chord) ? eastl::optional<KeyChord>(chord) : eastl::nullopt;
}
String InspectTerminalInput(eastl::string_view input) {
    String result;
    constexpr char digits[] = "0123456789abcdef";
    for (const unsigned char byte : input) {
        if (!result.empty()) { result += ' '; }
        result += digits[byte >> 4]; result += digits[byte & 15];
    }
    if (const auto chord = DecodeTerminalInput(input)) { result += " => "; result += ChordName(*chord); }
    return result;
}
TerminalKeyboard::~TerminalKeyboard() { Stop(); }
void TerminalKeyboard::Query() {
    if (queried_) { return; }
    queried_ = true;
    std::fputs("\x1b[?u", stdout); std::fflush(stdout);
}
bool TerminalKeyboard::HandleResponse(eastl::string_view input) {
    if (input.size() < 5 || input.substr(0, 3) != "\x1b[?" || input.back() != 'u') { return false; }
    unsigned flags = 0;
    if (!Number(input.substr(3, input.size() - 4), flags)) { return false; }
    if (!queried_) { return true; }
    if (!pushed_) {
        std::fputs("\x1b[>5u\x1b[?u", stdout); std::fflush(stdout); pushed_ = true;
    } else { enhanced_ = (flags & 5U) == 5U; }
    return true;
}
void TerminalKeyboard::Stop() {
    if (pushed_) { std::fputs("\x1b[<u", stdout); std::fflush(stdout); }
    pushed_ = false; enhanced_ = false;
}
bool TerminalKeyboard::Enhanced() const { return enhanced_; }

}
