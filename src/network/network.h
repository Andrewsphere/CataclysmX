#pragma once

#include "core/server.h"

#include <EASTL/expected.h>
#include <EASTL/unique_ptr.h>

#include <cstdint>

namespace cataclysmx::network {

enum class NetworkError {
    Initialization, SocketFailure, InvalidSettings, ConnectionFailed,
    Disconnected, Rejected, IncompatibleContent, Protocol, Timeout, Full,
    UnknownToken, AlreadyConnected, InvalidCommand, SnapshotTooLarge, SlowPeer
};
template<class T> using NetworkResult = eastl::expected<T, NetworkError>;
const char* NetworkErrorMessage(NetworkError error);

// The runtime must outlive hosts, clients and discovery objects using it.
class NetworkRuntime {
public:
    static NetworkResult<eastl::unique_ptr<NetworkRuntime>> Create();
    ~NetworkRuntime();
    NetworkRuntime(const NetworkRuntime&) = delete;
    NetworkRuntime& operator=(const NetworkRuntime&) = delete;
private:
    NetworkRuntime() = default;
};

struct HostSettings {
    std::uint16_t port = 24680;
    std::uint16_t discoveryPort = 24681;
    String sessionName = "LAN game";
};
struct ConnectionSettings {
    String address = "127.0.0.1";
    std::uint16_t port = 24680;
    String playerName = "Guest";
    std::uint64_t reconnectToken = 0;
};
enum class ConnectionState { Disconnected, Connecting, Joining, Active, Rejected };

struct HostUpdate {
    Vector<PlayerCommandResult> commandResults;
    eastl::optional<SaveError> storageError;
    eastl::optional<NetworkError> networkError;
};

class LanHost {
public:
    LanHost(NetworkRuntime& runtime, Server& server, const ContentCatalog& content);
    ~LanHost();
    LanHost(const LanHost&) = delete;
    LanHost& operator=(const LanHost&) = delete;
    NetworkResult<void> Open(HostSettings settings = {});
    void Close();
    bool IsOpen() const;
    std::uint16_t GetPort() const;
    std::uint16_t GetDiscoveryPort() const;
    std::uint64_t QueueLocal(Command command);
    HostUpdate Pump(std::uint64_t elapsedMicroseconds);
private:
    class Impl;
    eastl::unique_ptr<Impl> impl_;
};

class LanClient {
public:
    LanClient(NetworkRuntime& runtime, const ContentCatalog& content);
    ~LanClient();
    LanClient(const LanClient&) = delete;
    LanClient& operator=(const LanClient&) = delete;
    NetworkResult<void> Connect(ConnectionSettings settings);
    void Disconnect();
    void Pump(std::uint64_t elapsedMicroseconds);
    NetworkResult<std::uint64_t> Submit(Command command);
    ConnectionState GetState() const;
    NetworkError GetError() const;
    bool HasView() const;
    const WorldView& View() const;
    Vector<PlayerCommandResult> TakeResults();
    std::uint64_t GetReconnectToken() const;
private:
    class Impl;
    eastl::unique_ptr<Impl> impl_;
};

struct DiscoveredSession {
    String address;
    std::uint16_t gamePort = 0;
    String sessionName;
    String hostName;
    std::uint8_t playerCount = 0;
    std::uint8_t maxPlayers = 4;
    std::uint64_t instanceId = 0;
    bool compatible = false;
};

class LanDiscovery {
public:
    LanDiscovery(NetworkRuntime& runtime, const ContentCatalog& content);
    ~LanDiscovery();
    LanDiscovery(const LanDiscovery&) = delete;
    LanDiscovery& operator=(const LanDiscovery&) = delete;
    NetworkResult<void> Open(std::uint16_t localPort = 0);
    void Close();
    // Empty address broadcasts on the local IPv4 subnet; directed queries work on loopback.
    NetworkResult<void> Refresh(std::uint16_t discoveryPort = 24681, const char* address = nullptr);
    void Pump(std::uint64_t elapsedMicroseconds);
    const Vector<DiscoveredSession>& Sessions() const;
private:
    class Impl;
    eastl::unique_ptr<Impl> impl_;
};

// Stable hash of all content that affects a networked view or simulation, including
// runtime terrain index ordering. Both endpoints must agree before joining.
std::uint64_t ContentFingerprint(const ContentCatalog& content);

}
