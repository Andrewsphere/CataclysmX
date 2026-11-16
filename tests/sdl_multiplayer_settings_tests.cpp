#include "sdl/multiplayer_screen.h"

#include <catch2/catch_test_macros.hpp>

using namespace cataclysmx;
using namespace cataclysmx::sdl;

namespace {

struct MultiplayerEditor {
    SessionSnapshot snapshot;
    Keymap keys = DefaultKeymap();
    MultiplayerUi ui{{}, {}, ""};

    MultiplayerEditor() { ui.Open(snapshot); }

    MultiplayerEvent Key(const char* key, bool busy = false) {
        return ui.Key({String(key)}, keys, snapshot, busy);
    }

    ScreenFrame Frame(bool busy = false) const {
        ScreenFrame frame;
        ui.Present(snapshot, busy, frame);
        return frame;
    }

    void Select(const char* prefix) {
        const auto frame = Frame();
        std::size_t index = 0;
        for (; index < frame.rows.size(); ++index) {
            if (frame.rows[index].text.find(prefix) == 0) { break; }
        }
        REQUIRE(index < frame.rows.size());
        Key("Home");
        for (std::size_t i = 0; i < index; ++i) { Key("Down"); }
        CHECK(Frame().selected == static_cast<int>(index));
    }

    void Edit(const char* prefix, eastl::string_view text) {
        Select(prefix);
        CHECK(Key("Enter") == MultiplayerEvent::None);
        REQUIRE(ui.EditingText());
        ui.Text(text);
    }

    bool HasDetail(const char* text) const {
        const auto frame = Frame();
        for (const auto& detail : frame.details) {
            if (detail.find(text) != String::npos) { return true; }
        }
        return false;
    }
};

network::DiscoveredSession Session(std::uint64_t id, const char* name, const char* address,
    std::uint16_t port, bool compatible = true) {
    network::DiscoveredSession session;
    session.instanceId = id;
    session.sessionName = name;
    session.address = address;
    session.gamePort = port;
    session.compatible = compatible;
    return session;
}

}

TEST_CASE("SDL multiplayer rows expose all settings and navigate without rendering", "[sdl][multiplayer][settings]") {
    MultiplayerEditor editor;
    const char* labels[] = {"Player name", "Join address", "New character", "Join / reconnect", "Leave session",
        "Host session name", "Host game port", "Host discovery port", "Discovery address", "Open host",
        "Close host", "Game speed (ticks/s)", "Limit simulation speed", "Pause when everyone is idle",
        "Apply pacing", "Refresh discovered"};
    auto frame = editor.Frame();
    REQUIRE(frame.rows.size() == 16);
    CHECK(frame.visible);
    CHECK(frame.presentation == ScreenPresentation::Large);
    for (const auto* label : labels) {
        frame = editor.Frame();
        CHECK(frame.rows[frame.selected].text.find(label) == 0);
        editor.Key("Tab");
    }
    CHECK(editor.Frame().selected == 0);
    editor.Key("BackTab");
    CHECK(editor.Frame().selected == 15);
    editor.ui.Key({"Tab", false, false, true}, editor.keys, editor.snapshot, false);
    CHECK(editor.Frame().selected == 14);
    CHECK(editor.HasDetail("/4 (authoritative; fixed capacity)"));
}

TEST_CASE("SDL multiplayer edits endpoint using the configured default port and emits join intent", "[sdl][multiplayer][settings]") {
    network::ConnectionSettings connection;
    connection.port = 31234;
    connection.playerName = "Constructor name";
    network::HostSettings host;
    host.sessionName = "Constructor session";
    SessionSnapshot snapshot;
    MultiplayerUi ui(host, connection, "");
    const auto keys = DefaultKeymap();
    ui.Open(snapshot);
    const auto key = [&](const char* name) { return ui.Key({String(name)}, keys, snapshot, false); };
    CHECK(ui.Connection().playerName == "Constructor name");
    CHECK(ui.Host().sessionName == "Constructor session");
    CHECK_FALSE(ui.NewCharacter());
    key("J"); REQUIRE(ui.EditingText()); ui.Text("example.test"); key("Enter");
    CHECK_FALSE(ui.EditingText());
    CHECK(ui.Connection().address == "example.test");
    CHECK(ui.Connection().port == 31234);
    CHECK(key("Enter") == MultiplayerEvent::Join);
    key("j"); ui.Text("127.0.0.2:65535"); key("Enter");
    CHECK(ui.Connection().address == "127.0.0.2");
    CHECK(ui.Connection().port == 65535);
    CHECK(key("Enter") == MultiplayerEvent::Join);
    key("j"); ui.Text("other.test"); key("Enter");
    CHECK(ui.Connection().port == 65535);
    ui.Key({"j", true}, keys, snapshot, false);
    CHECK_FALSE(ui.EditingText());
    ui.Key({"n", false, true}, keys, snapshot, false);
    CHECK_FALSE(ui.EditingText());
}

TEST_CASE("SDL multiplayer endpoint and port validation retains the editor and committed values", "[sdl][multiplayer][settings]") {
    MultiplayerEditor editor;
    for (const auto* invalid : {"", "host name:123", "host:0", "host:65536", "host:12tail", "host:-1",
        "host:", ":123", "host:1:2", "host\nname"}) {
        editor.Edit("Join address", invalid);
        editor.Key("Enter");
        CHECK(editor.ui.EditingText());
        CHECK(editor.ui.Connection().address == "127.0.0.1");
        CHECK(editor.HasDetail("hostname or IP[:port]"));
        editor.Key("Escape");
    }
    String longAddress(256, 'a');
    editor.Edit("Join address", longAddress); editor.Key("Enter");
    CHECK(editor.ui.EditingText()); editor.Key("Escape");
    for (const auto* label : {"Host game port", "Host discovery port"}) {
        for (const auto* invalid : {"0", "65536", "-2", "+3", "1 ", "123x"}) {
            editor.Edit(label, invalid); editor.Key("Enter");
            CHECK(editor.ui.EditingText());
            CHECK(editor.HasDetail("1..65535"));
            editor.Key("Escape");
        }
        editor.Edit(label, "65535"); editor.Key("Enter"); CHECK_FALSE(editor.ui.EditingText());
    }
    CHECK(editor.ui.Host().port == 65535);
    CHECK(editor.ui.Host().discoveryPort == 65535);
    editor.Edit("Discovery address", "127.0.0.1"); editor.Key("Enter");
    CHECK(editor.ui.DiscoveryAddress() == "127.0.0.1");
    editor.Edit("Discovery address", "bad:port"); editor.Key("Enter");
    CHECK(editor.ui.EditingText()); editor.Key("Escape");
    // Backspace edits the initial value, rather than triggering replace-on-first-text.
    editor.Select("Discovery address"); editor.Key("Enter");
    for (int i = 0; i < 9; ++i) { editor.Key("Backspace"); }
    editor.Key("Enter");
    CHECK(editor.ui.DiscoveryAddress().empty());
}

TEST_CASE("SDL multiplayer name edits validate UTF-8 and backspace whole scalars", "[sdl][multiplayer][settings]") {
    MultiplayerEditor editor;
    const String name = "Zo\xc3\xab\xf0\x9f\x90\x88";
    editor.Edit("Player name", name);
    editor.Key("Backspace"); editor.Key("Enter");
    CHECK(editor.ui.Connection().playerName == "Zo\xc3\xab");
    editor.Key("n"); editor.Key("Backspace"); editor.Key("Enter");
    CHECK(editor.ui.Connection().playerName == "Zo");
    for (const auto& invalid : {String(""), String("\n"), String("\x7f"), String("\xc2\x85"), String("\xc0\xaf"),
        String("\xed\xa0\x80"), String("\xf4\x90\x80\x80"), String("\xe2\x82"), String(33, 'n')}) {
        editor.Edit("Player name", invalid); editor.Key("Enter");
        CHECK(editor.ui.EditingText());
        CHECK(editor.ui.Connection().playerName == "Zo");
        CHECK(editor.HasDetail("1..32 bytes"));
        editor.Key("Escape");
    }
    editor.Edit("Player name", String(32, 'p')); editor.Key("Enter");
    CHECK(editor.ui.Connection().playerName.size() == 32);
    editor.Edit("Host session name", String(65, 's')); editor.Key("Enter");
    CHECK(editor.ui.EditingText()); editor.Key("Escape");
    editor.Edit("Host session name", String(64, 's')); editor.Key("Enter");
    CHECK(editor.ui.Host().sessionName.size() == 64);
    editor.Key("s"); editor.ui.Text(String(262, 'x'));
    CHECK(editor.HasDetail("Text is too long"));
    editor.Key("Escape");
}

TEST_CASE("SDL multiplayer host intents use edited drafts and current authority", "[sdl][multiplayer][settings]") {
    MultiplayerEditor editor;
    editor.Edit("Host session name", "Evening game"); editor.Key("Enter");
    editor.Edit("Host game port", "33333"); editor.Key("Enter");
    editor.Edit("Host discovery port", "33334"); editor.Key("Enter");
    editor.Edit("Discovery address", "192.168.1.255"); editor.Key("Enter");
    CHECK(editor.Key("O") == MultiplayerEvent::OpenHost);
    CHECK(editor.ui.Host().sessionName == "Evening game");
    CHECK(editor.ui.Host().port == 33333);
    CHECK(editor.ui.Host().discoveryPort == 33334);
    CHECK(editor.ui.DiscoveryAddress() == "192.168.1.255");
    editor.snapshot.mode = SessionMode::Host;
    CHECK(editor.Key("j") == MultiplayerEvent::None); editor.ui.Text("elsewhere:42"); editor.Key("Enter");
    CHECK(editor.Key("Enter") == MultiplayerEvent::None); // Address commit selects disabled Join.
    CHECK(editor.ui.Connection().address == "elsewhere");
    CHECK(editor.Key("o") == MultiplayerEvent::None);
    editor.Key("s"); CHECK_FALSE(editor.ui.EditingText());
    CHECK(editor.Key("c") == MultiplayerEvent::CloseHost);
    editor.snapshot.mode = SessionMode::Guest;
    for (const auto state : {network::ConnectionState::Connecting, network::ConnectionState::Joining,
        network::ConnectionState::Active, network::ConnectionState::Rejected, network::ConnectionState::Disconnected}) {
        editor.snapshot.connectionState = state;
        CHECK(editor.Key("d") == MultiplayerEvent::Leave);
        editor.Select("Join / reconnect");
        CHECK(editor.Key("Enter") == ((state == network::ConnectionState::Connecting || state == network::ConnectionState::Joining)
            ? MultiplayerEvent::None : MultiplayerEvent::Join));
    }
    editor.snapshot.connectionState = network::ConnectionState::Active;
    editor.Edit("Player name", "Active guest"); editor.Key("Enter");
    CHECK(editor.ui.Connection().playerName == "Active guest");
    CHECK(editor.Key("o") == MultiplayerEvent::None);
    CHECK(editor.Key("c") == MultiplayerEvent::None);
}

TEST_CASE("SDL multiplayer guests display live remote pacing without emitting owner actions", "[sdl][multiplayer][settings]") {
    MultiplayerEditor editor;
    editor.snapshot.mode = SessionMode::Guest;
    editor.snapshot.view.playerId = 17;
    editor.snapshot.view.pacing = {321, true, false};
    editor.snapshot.runtimeSettings.pacing = {999, false, true};
    for (const auto* label : {"Game speed (ticks/s)", "Limit simulation speed", "Pause when everyone is idle", "Apply pacing"}) {
        editor.Select(label);
        auto frame = editor.Frame();
        CHECK(frame.rows[frame.selected].unavailable);
        CHECK(editor.Key("Enter") == MultiplayerEvent::None);
        CHECK_FALSE(editor.ui.EditingText());
        CHECK(editor.Key("Right") == MultiplayerEvent::None);
    }
    CHECK(editor.ui.Pacing() == editor.snapshot.view.pacing);
    editor.snapshot.view.pacing.ticksPerSecond = 654;
    editor.Select("Game speed (ticks/s)");
    CHECK(editor.Frame().rows[editor.Frame().selected].text.find("654") != String::npos);
    CHECK(editor.HasDetail("Current runtime pacing: 654"));
    CHECK(editor.HasDetail("Controlled by host"));
    CHECK(editor.Key("o") == MultiplayerEvent::None);
    editor.snapshot.view.playerId = 0;
    editor.ui.Refresh(editor.snapshot);
    CHECK(editor.ui.Pacing() == editor.snapshot.runtimeSettings.pacing);
    CHECK(editor.HasDetail("Current runtime pacing: 999"));
}

TEST_CASE("SDL multiplayer pacing drafts survive runtime refresh and clear only on applied acknowledgement", "[sdl][multiplayer][settings]") {
    MultiplayerEditor editor;
    for (const auto* invalid : {"0", "1000001", "100tail", "-1"}) {
        editor.Edit("Game speed (ticks/s)", invalid); editor.Key("Enter");
        CHECK(editor.ui.EditingText()); editor.Key("Escape");
    }
    editor.Edit("Game speed (ticks/s)", "1000000"); editor.Key("Enter");
    editor.Select("Limit simulation speed"); editor.Key("Right");
    editor.Select("Pause when everyone is idle"); editor.Key("Left");
    const auto draft = editor.ui.Pacing();
    CHECK(draft == PacingSettings{1000000, true, false});
    editor.snapshot.mode = SessionMode::Host;
    editor.snapshot.runtimeSettings.pacing = {200, false, true};
    editor.ui.Refresh(editor.snapshot);
    CHECK(editor.ui.Pacing() == draft);
    CHECK(editor.HasDetail("Unapplied pacing changes"));
    CHECK(editor.HasDetail("Current runtime pacing: 200"));
    editor.Select("Apply pacing");
    CHECK(editor.Key("Enter") == MultiplayerEvent::ApplyPacing);
    CHECK(editor.HasDetail("Unapplied pacing changes"));
    editor.ui.Feedback("Persistence failed; draft retained");
    CHECK(editor.HasDetail("Persistence failed"));
    CHECK(editor.ui.Pacing() == draft);
    // Even when runtime catches up independently, the draft remains dirty until acknowledged.
    editor.snapshot.runtimeSettings.pacing = draft;
    editor.ui.Refresh(editor.snapshot);
    CHECK(editor.HasDetail("Unapplied pacing changes"));
    editor.ui.Applied(draft);
    CHECK(editor.HasDetail("No pending pacing changes"));
    CHECK(editor.HasDetail("Persistence failed")); // Applied and persistence feedback are separate.
    editor.ui.Feedback("Applied and saved");
    CHECK(editor.HasDetail("Applied and saved"));
    editor.snapshot.runtimeSettings.pacing = {42, true, true};
    editor.ui.Refresh(editor.snapshot);
    CHECK(editor.ui.Pacing() == editor.snapshot.runtimeSettings.pacing);
    editor.Edit("Game speed (ticks/s)", "88");
    editor.ui.Applied(editor.snapshot.runtimeSettings.pacing);
    CHECK_FALSE(editor.ui.EditingText());
    editor.ui.Open(editor.snapshot);
    CHECK(editor.ui.Pacing() == editor.snapshot.runtimeSettings.pacing);
}

TEST_CASE("SDL multiplayer epoch reset retains connection fields and resets transient editor state", "[sdl][multiplayer][settings]") {
    MultiplayerEditor editor;
    editor.Edit("Player name", "Retained"); editor.Key("Enter");
    editor.Edit("Join address", "retained.test:34567"); editor.Key("Enter");
    editor.Key("X"); REQUIRE(editor.ui.NewCharacter());
    editor.Key("n"); editor.ui.Text("Uncommitted");
    editor.ui.ResetConnection();
    CHECK_FALSE(editor.ui.EditingText());
    CHECK_FALSE(editor.ui.NewCharacter());
    CHECK(editor.ui.Connection().playerName == "Retained");
    CHECK(editor.ui.Connection().address == "retained.test");
    CHECK(editor.ui.Connection().port == 34567);
    CHECK(editor.Frame().selected == 0);
}

TEST_CASE("SDL multiplayer discovery selection follows identity and removal falls back to refresh", "[sdl][multiplayer][settings]") {
    MultiplayerEditor editor;
    const auto first = Session(11, "First", "10.0.0.1", 10001);
    const auto second = Session(22, "Second", "10.0.0.2", 10002);
    const auto incompatible = Session(33, "Incompatible", "10.0.0.3", 10003, false);
    editor.snapshot.sessions = {first, second, incompatible};
    editor.Select("2: Second");
    editor.snapshot.sessions = {second, incompatible, first};
    editor.ui.Refresh(editor.snapshot);
    auto frame = editor.Frame();
    CHECK(frame.rows[frame.selected].text.find("1: Second") == 0);
    CHECK(editor.Key("Enter") == MultiplayerEvent::Join);
    CHECK(editor.ui.Connection().address == "10.0.0.2");
    CHECK(editor.ui.Connection().port == 10002);
    editor.Select("Join address");
    CHECK(editor.Frame().rows[editor.Frame().selected].text.find("10.0.0.2:10002") != String::npos);
    editor.Select("1: Second");
    editor.snapshot.sessions = {first, incompatible};
    editor.ui.Refresh(editor.snapshot);
    frame = editor.Frame();
    CHECK(frame.rows[frame.selected].text.find("Refresh discovered") == 0);
    CHECK(editor.Key("Enter") == MultiplayerEvent::Refresh);
    CHECK(editor.Key("2") == MultiplayerEvent::None);
    CHECK(editor.ui.Connection().address == "10.0.0.2");
    CHECK(editor.HasDetail("incompatible"));
    editor.snapshot.mode = SessionMode::Host;
    CHECK(editor.Key("1") == MultiplayerEvent::None);
    editor.snapshot.mode = SessionMode::Local;
    CHECK(editor.Key("1") == MultiplayerEvent::Join);
    CHECK(editor.ui.Connection().address == "10.0.0.1");
}

TEST_CASE("SDL multiplayer pending requests allow navigation but suppress editing and actions", "[sdl][multiplayer][settings]") {
    MultiplayerEditor editor;
    for (const auto* key : {"o", "c", "r", "d", "x", "n", "j", "s", "Enter", "Escape", "F9"}) {
        CHECK(editor.Key(key, true) == MultiplayerEvent::None);
        CHECK_FALSE(editor.ui.EditingText());
    }
    CHECK_FALSE(editor.ui.NewCharacter());
    editor.Key("Down", true);
    CHECK(editor.Frame().selected == 1);
    const auto frame = editor.Frame(true);
    for (const auto& row : frame.rows) { CHECK(row.unavailable); }
    editor.Edit("Player name", "Pending edit");
    editor.Key("Enter", true);
    CHECK(editor.ui.Connection().playerName == "Guest");
    editor.Key("Escape");
    // An authority change during editing is checked against the new snapshot.
    editor.Edit("Game speed (ticks/s)", "123");
    editor.snapshot.mode = SessionMode::Guest;
    CHECK(editor.Key("Enter") == MultiplayerEvent::None);
    CHECK_FALSE(editor.ui.EditingText());
    CHECK(editor.ui.Pacing() == editor.snapshot.runtimeSettings.pacing);
}
