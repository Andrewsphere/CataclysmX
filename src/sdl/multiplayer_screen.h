#pragma once

#include "sdl/channels.h"
#include "sdl/screens.h"

namespace cataclysmx::sdl {

enum class MultiplayerEvent { None, Close, OpenHost, CloseHost, Join, Leave, Refresh, ApplyPacing };

// Main-thread editor. Events describe intent; the session owner applies all changes.
class MultiplayerUi {
public:
    MultiplayerUi(network::HostSettings host, network::ConnectionSettings connection, String discoveryAddress);
    void Open(const SessionSnapshot& snapshot);
    void Refresh(const SessionSnapshot& snapshot);
    MultiplayerEvent Key(const KeyChord& chord, const Keymap& keys, const SessionSnapshot& snapshot, bool busy);
    void Text(eastl::string_view text);
    void Present(const SessionSnapshot& snapshot, bool busy, ScreenFrame& frame) const;
    void Applied(PacingSettings pacing);
    void Feedback(String text);
    void ResetConnection();
    bool EditingText() const;
    const network::HostSettings& Host() const { return host_; }
    const network::ConnectionSettings& Connection() const { return connection_; }
    const String& DiscoveryAddress() const { return discoveryAddress_; }
    bool NewCharacter() const { return newCharacter_; }
    PacingSettings Pacing() const { return pacing_; }
private:
    enum class Row { Name, Address, NewCharacter, Join, Leave, SessionName, Port, DiscoveryPort,
        DiscoveryAddress, OpenHost, CloseHost, Speed, RateLimit, IdlePause, ApplyPacing, Refresh, Count };
    bool Enabled(Row row, const SessionSnapshot& snapshot, bool busy) const;
    void BeginEdit(Row row);
    bool CommitEdit();
    MultiplayerEvent Activate(const SessionSnapshot& snapshot, bool busy);
    network::HostSettings host_;
    network::ConnectionSettings connection_;
    String address_;
    String discoveryAddress_;
    PacingSettings pacing_{};
    PacingSettings baseline_{};
    std::size_t selected_ = 0;
    std::uint64_t selectedInstance_ = 0;
    eastl::optional<Row> editing_;
    String edit_;
    String feedback_;
    bool replace_ = false;
    bool newCharacter_ = false;
};

}
