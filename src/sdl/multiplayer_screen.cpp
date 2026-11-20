#include "sdl/multiplayer_screen.h"

#include <charconv>

namespace cataclysmx::sdl {
namespace {

String Number(std::uint64_t value) {
    char buffer[32];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    return String(buffer, result.ptr);
}

bool ParseNumber(eastl::string_view text, std::uint32_t maximum, std::uint32_t& value) {
    if (text.empty()) { return false; }
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size()
        && value >= 1 && value <= maximum;
}

// Reject malformed encodings, overlong scalars, surrogates and Unicode control characters.
bool ValidName(eastl::string_view text, std::size_t maximum) {
    if (text.empty() || text.size() > maximum) { return false; }
    for (std::size_t i = 0; i < text.size();) {
        const auto first = static_cast<unsigned char>(text[i++]);
        std::uint32_t scalar = first;
        unsigned remaining = 0;
        std::uint32_t minimum = 0;
        if (first >= 0xc2 && first <= 0xdf) { scalar = first & 0x1f; remaining = 1; minimum = 0x80; }
        else if (first >= 0xe0 && first <= 0xef) { scalar = first & 0x0f; remaining = 2; minimum = 0x800; }
        else if (first >= 0xf0 && first <= 0xf4) { scalar = first & 7; remaining = 3; minimum = 0x10000; }
        else if (first >= 0x80) { return false; }
        if (remaining > text.size() - i) { return false; }
        for (unsigned j = 0; j < remaining; ++j) {
            const auto byte = static_cast<unsigned char>(text[i++]);
            if ((byte & 0xc0) != 0x80) { return false; }
            scalar = (scalar << 6) | (byte & 0x3f);
        }
        if (scalar < minimum || scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff)
            || scalar < 0x20 || (scalar >= 0x7f && scalar <= 0x9f)) { return false; }
    }
    return true;
}

bool ValidAddress(eastl::string_view text, bool allowEmpty) {
    if (text.empty()) { return allowEmpty; }
    if (text.size() > 255) { return false; }
    for (const unsigned char byte : text) {
        if (byte <= 0x20 || byte >= 0x7f || byte == ':') { return false; }
    }
    return true;
}

bool Joining(const SessionSnapshot& snapshot) {
    return snapshot.connectionState == network::ConnectionState::Connecting
        || snapshot.connectionState == network::ConnectionState::Joining;
}

PacingSettings RuntimePacing(const SessionSnapshot& snapshot) {
    return snapshot.mode == SessionMode::Guest && snapshot.view.playerId != 0
        ? snapshot.view.pacing : snapshot.runtimeSettings.pacing;
}

String PacingText(PacingSettings pacing) {
    return Number(pacing.ticksPerSecond) + " ticks/s | limit " + (pacing.rateLimited ? "on" : "off")
        + " | idle pause " + (pacing.pauseWhenIdle ? "on" : "off");
}

const char* StateText(const SessionSnapshot& snapshot) {
    if (snapshot.mode == SessionMode::Host) { return "Host"; }
    if (snapshot.mode == SessionMode::Local) { return "Local (hosting closed)"; }
    switch (snapshot.connectionState) {
    case network::ConnectionState::Disconnected: return "Guest: disconnected";
    case network::ConnectionState::Connecting: return "Guest: connecting";
    case network::ConnectionState::Joining: return "Guest: joining";
    case network::ConnectionState::Active: return "Guest: active";
    case network::ConnectionState::Rejected: return "Guest: rejected";
    }
    return "Guest";
}

std::size_t Selection(std::size_t selected, std::uint64_t instance,
    const SessionSnapshot& snapshot, std::size_t fixedCount) {
    if (instance != 0) {
        for (std::size_t i = 0; i < snapshot.sessions.size(); ++i) {
            if (snapshot.sessions[i].instanceId == instance) { return fixedCount + i; }
        }
        return fixedCount - 1; // Refresh, never a different server at the old index.
    }
    return selected < fixedCount ? selected : fixedCount - 1;
}

}

MultiplayerUi::MultiplayerUi(network::HostSettings host, network::ConnectionSettings connection,
    String discoveryAddress)
    : host_(eastl::move(host)), connection_(eastl::move(connection)), address_(connection_.address),
      discoveryAddress_(eastl::move(discoveryAddress)) {
    if (connection_.port != 24680) { address_ += ":" + Number(connection_.port); }
}

void MultiplayerUi::Open(const SessionSnapshot& snapshot) {
    pacing_ = RuntimePacing(snapshot);
    baseline_ = pacing_;
    selected_ = 0;
    selectedInstance_ = 0;
    editing_.reset();
    edit_.clear();
    replace_ = false;
    feedback_.clear();
}

void MultiplayerUi::Refresh(const SessionSnapshot& snapshot) {
    if (pacing_ == baseline_ || snapshot.mode == SessionMode::Guest) {
        pacing_ = RuntimePacing(snapshot);
        baseline_ = pacing_;
    }
    const auto count = static_cast<std::size_t>(Row::Count);
    selected_ = Selection(selected_, selectedInstance_, snapshot, count);
    if (selected_ < count) { selectedInstance_ = 0; }
    if (editing_ && !Enabled(*editing_, snapshot, false)) {
        editing_.reset();
        edit_.clear();
    }
}

bool MultiplayerUi::Enabled(Row row, const SessionSnapshot& snapshot, bool busy) const {
    if (busy) { return false; }
    switch (row) {
    case Row::SessionName: case Row::Port: case Row::DiscoveryPort: case Row::DiscoveryAddress:
    case Row::OpenHost: return snapshot.mode == SessionMode::Local;
    case Row::CloseHost: return snapshot.mode == SessionMode::Host;
    case Row::Join: return snapshot.mode != SessionMode::Host && !Joining(snapshot);
    case Row::Leave: return snapshot.mode == SessionMode::Guest;
    case Row::Speed: case Row::RateLimit: case Row::IdlePause: case Row::ApplyPacing:
        return snapshot.mode != SessionMode::Guest;
    case Row::Count: return false;
    default: return true;
    }
}

void MultiplayerUi::BeginEdit(Row row) {
    switch (row) {
    case Row::Name: edit_ = connection_.playerName; break;
    case Row::Address: edit_ = address_; break;
    case Row::SessionName: edit_ = host_.sessionName; break;
    case Row::Port: edit_ = Number(host_.port); break;
    case Row::DiscoveryPort: edit_ = Number(host_.discoveryPort); break;
    case Row::DiscoveryAddress: edit_ = discoveryAddress_; break;
    case Row::Speed: edit_ = Number(pacing_.ticksPerSecond); break;
    default: return;
    }
    editing_ = row;
    replace_ = true;
    feedback_.clear();
}

bool MultiplayerUi::CommitEdit() {
    if (!editing_) { return false; }
    const auto row = *editing_;
    std::uint32_t number = 0;
    switch (row) {
    case Row::Name: case Row::SessionName:
        if (!ValidName(edit_, row == Row::Name ? 32 : 64)) {
            feedback_ = row == Row::Name ? "Player name must be 1..32 bytes of non-control UTF-8."
                : "Session name must be 1..64 bytes of non-control UTF-8.";
            return false;
        }
        if (row == Row::Name) { connection_.playerName = edit_; }
        else { host_.sessionName = edit_; }
        break;
    case Row::Address: {
        const auto colon = edit_.find(':');
        const eastl::string_view text(edit_.data(), edit_.size());
        const auto address = text.substr(0, colon);
        number = connection_.port;
        if (!ValidAddress(address, false) || (colon != String::npos
            && !ParseNumber(text.substr(colon + 1), 65535, number))) {
            feedback_ = "Use hostname or IP[:port]: address 1..255 bytes, no spaces/colon; port 1..65535.";
            return false;
        }
        connection_.address.assign(address.data(), address.size());
        connection_.port = static_cast<std::uint16_t>(number);
        address_ = edit_;
        selected_ = static_cast<std::size_t>(Row::Join);
        selectedInstance_ = 0;
        break;
    }
    case Row::DiscoveryAddress:
        if (!ValidAddress(edit_, true)) {
            feedback_ = "Discovery address must be at most 255 bytes, without spaces/colon; empty broadcasts.";
            return false;
        }
        discoveryAddress_ = edit_;
        break;
    case Row::Port: case Row::DiscoveryPort: case Row::Speed:
        if (!ParseNumber(edit_, row == Row::Speed ? 1000000 : 65535, number)) {
            feedback_ = row == Row::Speed ? "Game speed must be an integer in 1..1000000."
                : "Port must be an integer in 1..65535.";
            return false;
        }
        if (row == Row::Speed) { pacing_.ticksPerSecond = number; }
        else if (row == Row::Port) { host_.port = static_cast<std::uint16_t>(number); }
        else { host_.discoveryPort = static_cast<std::uint16_t>(number); }
        break;
    default: return false;
    }
    editing_.reset();
    edit_.clear();
    replace_ = false;
    feedback_ = "Draft updated. Activate the corresponding action to apply.";
    return true;
}

void MultiplayerUi::Text(eastl::string_view text) {
    if (!editing_) { return; }
    const auto size = replace_ ? 0 : edit_.size();
    // Allows invalid field lengths to be diagnosed on commit, with a bounded editor buffer.
    if (text.size() > 261 - size) { feedback_ = "Text is too long (maximum editor size: 261 bytes)."; return; }
    if (replace_) { edit_.clear(); replace_ = false; }
    edit_.append(text.data(), text.size());
}

MultiplayerEvent MultiplayerUi::Activate(const SessionSnapshot& snapshot, bool busy) {
    const auto count = static_cast<std::size_t>(Row::Count);
    if (selected_ >= count) {
        if (!Enabled(Row::Join, snapshot, busy)) {
            feedback_ = busy ? "A request is pending." : "Close hosting or finish connecting before joining.";
            return MultiplayerEvent::None;
        }
        const auto index = selected_ - count;
        if (index >= snapshot.sessions.size() || snapshot.sessions[index].instanceId != selectedInstance_) {
            feedback_ = "Session is no longer available. Refresh discovery.";
            return MultiplayerEvent::None;
        }
        const auto& session = snapshot.sessions[index];
        if (!session.compatible) { feedback_ = "Session has incompatible content or protocol."; return MultiplayerEvent::None; }
        connection_.address = session.address;
        connection_.port = session.gamePort;
        address_ = session.address + ":" + Number(session.gamePort);
        return MultiplayerEvent::Join;
    }
    const auto row = static_cast<Row>(selected_);
    if (!Enabled(row, snapshot, busy)) {
        if (busy) { feedback_ = "A request is pending."; }
        else if (row == Row::Speed || row == Row::RateLimit || row == Row::IdlePause || row == Row::ApplyPacing) {
            feedback_ = "Controlled by host. Guests cannot change pacing.";
        } else if (row == Row::Leave) { feedback_ = "Leave is available only to guests."; }
        else if (row == Row::CloseHost) { feedback_ = "Hosting is already closed."; }
        else if (row == Row::Join) { feedback_ = "Close hosting or finish connecting before joining."; }
        else { feedback_ = "Hosting settings are editable only in Local mode with hosting closed."; }
        return MultiplayerEvent::None;
    }
    switch (row) {
    case Row::NewCharacter: newCharacter_ = !newCharacter_; break;
    case Row::Join: return MultiplayerEvent::Join;
    case Row::Leave: return MultiplayerEvent::Leave;
    case Row::OpenHost: return MultiplayerEvent::OpenHost;
    case Row::CloseHost: return MultiplayerEvent::CloseHost;
    case Row::RateLimit: pacing_.rateLimited = !pacing_.rateLimited; break;
    case Row::IdlePause: pacing_.pauseWhenIdle = !pacing_.pauseWhenIdle; break;
    case Row::ApplyPacing: return MultiplayerEvent::ApplyPacing;
    case Row::Refresh: return MultiplayerEvent::Refresh;
    default: BeginEdit(row); break;
    }
    return MultiplayerEvent::None;
}

MultiplayerEvent MultiplayerUi::Key(const KeyChord& chord, const Keymap& keys,
    const SessionSnapshot& snapshot, bool busy) {
    Refresh(snapshot);
    const auto action = ResolveAction(keys, InputContext::Menu, chord);
    const bool plain = !chord.ctrl && !chord.alt;
    char letter = plain && chord.key.size() == 1 ? chord.key[0] : '\0';
    if (letter >= 'A' && letter <= 'Z') { letter = static_cast<char>(letter - 'A' + 'a'); }
    const bool isShortcut = letter == 'j' || letter == 'n' || letter == 's' || letter == 'o'
        || letter == 'c' || letter == 'r' || letter == 'd' || letter == 'x' || (letter >= '1' && letter <= '9');
    const auto count = static_cast<std::size_t>(Row::Count);
    const bool adjusts = selected_ == static_cast<std::size_t>(Row::Speed)
        || selected_ == static_cast<std::size_t>(Row::RateLimit)
        || selected_ == static_cast<std::size_t>(Row::IdlePause)
        || selected_ == static_cast<std::size_t>(Row::NewCharacter);
    const bool previous = plain && (chord.key == "Up" || chord.key == "BackTab"
        || (chord.key == "Tab" && chord.shift) || (chord.key == "Left" && !adjusts));
    const bool next = plain && (chord.key == "Down" || (chord.key == "Tab" && !chord.shift)
        || (chord.key == "Right" && !adjusts));
    if (!editing_ && !isShortcut && (previous || next || action == InputAction::North || action == InputAction::South
        || action == InputAction::Next || action == InputAction::Previous
        || (plain && (chord.key == "Home" || chord.key == "End")))) {
        const auto total = count + snapshot.sessions.size();
        const bool backwards = previous || action == InputAction::North || action == InputAction::Previous;
        selected_ = plain && chord.key == "Home" ? 0 : plain && chord.key == "End" ? total - 1
            : (selected_ + total + (backwards ? -1 : 1)) % total;
        selectedInstance_ = selected_ >= count ? snapshot.sessions[selected_ - count].instanceId : 0;
        return MultiplayerEvent::None;
    }
    if (busy) { return MultiplayerEvent::None; }
    if (plain && (chord.key == "Escape" || chord.key == "F9")) {
        if (editing_) { editing_.reset(); edit_.clear(); return MultiplayerEvent::None; }
        return MultiplayerEvent::Close;
    }
    if (editing_) {
        if (plain && chord.key == "Backspace") {
            if (!edit_.empty()) {
                auto offset = edit_.size() - 1;
                while (offset > 0 && (static_cast<unsigned char>(edit_[offset]) & 0xc0) == 0x80) { --offset; }
                edit_.resize(offset);
            }
            replace_ = false;
        } else if (plain && chord.key == "Enter") { CommitEdit(); }
        return MultiplayerEvent::None;
    }
    if (plain && chord.key.size() == 1) {
        char key = chord.key[0];
        if (key >= 'A' && key <= 'Z') { key = static_cast<char>(key + ('a' - 'A')); }
        eastl::optional<Row> shortcut;
        switch (key) {
        case 'j': shortcut = Row::Address; break;
        case 'n': shortcut = Row::Name; break;
        case 's': shortcut = Row::SessionName; break;
        case 'o': shortcut = Row::OpenHost; break;
        case 'c': shortcut = Row::CloseHost; break;
        case 'r': shortcut = Row::Refresh; break;
        case 'd': shortcut = Row::Leave; break;
        case 'x': shortcut = Row::NewCharacter; break;
        default: break;
        }
        if (shortcut) {
            selected_ = static_cast<std::size_t>(*shortcut); selectedInstance_ = 0;
            const auto event = Activate(snapshot, busy);
            if (*shortcut == Row::NewCharacter) { selected_ = static_cast<std::size_t>(Row::Join); }
            return event;
        }
        if (key >= '1' && key <= '9') {
            const auto index = static_cast<std::size_t>(key - '1');
            if (index < snapshot.sessions.size()) {
                selected_ = count + index; selectedInstance_ = snapshot.sessions[index].instanceId;
                return Activate(snapshot, busy);
            }
            feedback_ = "No discovered session at that shortcut.";
            return MultiplayerEvent::None;
        }
    }
    if (plain && (chord.key == "Left" || chord.key == "Right") && selected_ < count) {
        const auto row = static_cast<Row>(selected_);
        if (row == Row::RateLimit || row == Row::IdlePause || row == Row::NewCharacter) { return Activate(snapshot, busy); }
        if (row == Row::Speed) {
            if (!Enabled(row, snapshot, busy)) { return Activate(snapshot, busy); }
            pacing_.ticksPerSecond = chord.key == "Left" ? (pacing_.ticksPerSecond > 1 ? pacing_.ticksPerSecond / 2 : 1)
                : (pacing_.ticksPerSecond < 500000 ? pacing_.ticksPerSecond * 2 : 1000000);
            return MultiplayerEvent::None;
        }
    }
    if ((plain && chord.key == "Enter") || action == InputAction::Accept) { return Activate(snapshot, busy); }
    return MultiplayerEvent::None;
}

void MultiplayerUi::Present(const SessionSnapshot& snapshot, bool busy, ScreenFrame& frame) const {
    frame.visible = true;
    frame.presentation = ScreenPresentation::Large;
    frame.followCursor = false;
    frame.cursor.reset(); frame.highlights.clear(); frame.debugImage = nullptr; frame.worldgenPreview = nullptr;
    frame.rows.clear(); frame.details.clear();
    frame.title = String("Multiplayer | ") + StateText(snapshot);
    const auto shown = snapshot.mode == SessionMode::Guest ? RuntimePacing(snapshot) : pacing_;
    const auto count = static_cast<std::size_t>(Row::Count);
    const auto selection = Selection(selected_, selectedInstance_, snapshot, count);
    frame.selected = static_cast<int>(selection);
    const auto add = [&](Row row, String label) {
        if (editing_ == row) { label += " [editing: " + edit_ + "_]"; }
        frame.rows.push_back({eastl::move(label), !Enabled(row, snapshot, busy)});
    };
    add(Row::Name, "Player name: " + connection_.playerName);
    add(Row::Address, "Join address: " + address_);
    add(Row::NewCharacter, String("New character: ") + (newCharacter_ ? "[x]" : "[ ] (use reconnect profile)"));
    add(Row::Join, "Join / reconnect");
    add(Row::Leave, "Leave session");
    add(Row::SessionName, "Host session name: " + host_.sessionName);
    add(Row::Port, "Host game port: " + Number(host_.port));
    add(Row::DiscoveryPort, "Host discovery port: " + Number(host_.discoveryPort));
    add(Row::DiscoveryAddress, "Discovery address: " + (discoveryAddress_.empty() ? String("(broadcast)") : discoveryAddress_));
    add(Row::OpenHost, "Open host to LAN");
    add(Row::CloseHost, "Close host to LAN");
    add(Row::Speed, "Game speed (ticks/s): " + Number(shown.ticksPerSecond));
    add(Row::RateLimit, String("Limit simulation speed: ") + (shown.rateLimited ? "[x]" : "[ ]"));
    add(Row::IdlePause, String("Pause when everyone is idle: ") + (shown.pauseWhenIdle ? "[x]" : "[ ]"));
    add(Row::ApplyPacing, "Apply pacing changes");
    add(Row::Refresh, "Refresh discovered sessions");
    for (std::size_t i = 0; i < snapshot.sessions.size(); ++i) {
        const auto& session = snapshot.sessions[i];
        String label = i < 9 ? Number(i + 1) + ": " : String();
        label += session.sessionName + " | " + session.hostName + " | " + session.address + ":" + Number(session.gamePort);
        label += " | " + Number(session.playerCount) + "/" + Number(session.maxPlayers);
        if (!session.compatible) { label += " (incompatible)"; }
        frame.rows.push_back({eastl::move(label), !session.compatible || !Enabled(Row::Join, snapshot, busy)});
    }
    frame.details.push_back(String("Session state: ") + StateText(snapshot));
    if (snapshot.mode == SessionMode::Host) {
        frame.details.push_back("Hosting ports: " + Number(snapshot.hostPort) + " | discovery " + Number(snapshot.discoveryPort));
    }
    std::size_t online = 0;
    for (const auto& player : snapshot.view.players) {
        if (player.state == PlayerConnectionState::Active || player.state == PlayerConnectionState::Joining) { ++online; }
    }
    frame.details.push_back("Players: " + Number(online) + "/4 (authoritative; fixed capacity)");
    frame.details.push_back("Current runtime pacing: " + PacingText(RuntimePacing(snapshot)));
    frame.details.push_back("Pacing draft: " + PacingText(shown));
    frame.details.push_back(snapshot.mode == SessionMode::Guest ? "Controlled by host; pacing and hosting are read-only."
        : pacing_ == baseline_ ? "No pending pacing changes." : "Unapplied pacing changes; Apply pacing sends the draft to the session owner.");
    frame.details.push_back("Apply/persist results are reported by the session owner.");
    frame.details.push_back("Host name and ports can be edited only in Local mode with hosting closed.");
    frame.details.push_back("Join is unavailable while hosting or connecting. Leave is available to guests in any state.");
    frame.details.push_back("Empty discovery address broadcasts. New character skips the saved reconnect profile.");
    if (selection == static_cast<std::size_t>(Row::Speed)) {
        frame.details.push_back("100 ticks = 1 game second. Enter a rate from 1 to 1000000; Left/Right halve/double it.");
        frame.details.push_back("The rate also controls input grace. Limit speed caps paid recovery; without it, recovery fast-forwards.");
    } else if (selection == static_cast<std::size_t>(Row::IdlePause)) {
        frame.details.push_back("Pause when all ready players are idle. Opening a multiplayer menu does not pause other players.");
    }
    if (selection >= count && !snapshot.sessions[selection - count].compatible) {
        frame.details.push_back("Selected session is incompatible; joining is unavailable.");
    }
    if (snapshot.sessions.empty()) { frame.details.push_back("No discovered sessions. Refresh or enter a direct join address."); }
    if (editing_) { frame.details.push_back("Enter accepts edit; Escape cancels. Typing replaces the initial value."); }
    if (busy) { frame.details.push_back("Request pending; controls are unavailable until it completes."); }
    if (!snapshot.status.empty()) { frame.details.push_back(snapshot.status); }
    if (!feedback_.empty()) { frame.details.push_back(feedback_); }
    frame.footer = "Arrows / Tab / Shift+Tab select | Enter edit/activate | F9 / Escape close\n"
        "J address | N name | S session | O open host | C close host | D leave | R refresh | X new character | 1-9 join";
}

void MultiplayerUi::Applied(PacingSettings pacing) {
    pacing_ = pacing;
    baseline_ = pacing;
    if (editing_ == Row::Speed) { editing_.reset(); edit_.clear(); replace_ = false; }
}

void MultiplayerUi::Feedback(String text) { feedback_ = eastl::move(text); }

void MultiplayerUi::ResetConnection() {
    editing_.reset();
    edit_.clear();
    replace_ = false;
    newCharacter_ = false;
    selected_ = 0;
    selectedInstance_ = 0;
}

bool MultiplayerUi::EditingText() const { return editing_.has_value(); }

}
