#pragma once

#include "sdl/channels.h"
#include "client/settings.h"

#include <thread>

namespace cataclysmx::sdl {

struct SessionOptions {
    Settings settings;
    NewGameScenario scenario = NewGameScenario::Demo;
    SettingsOverrides globalSettings;
    SettingsOverrides worldSettings;
    bool settingsOverridesProvided = false;
    String savePath;
    String loadPath;
    String profilePath = "cataclysmx-lan-profile.txt";
    bool host = false;
    bool discover = false;
    bool join = false;
    bool newCharacter = false;
    network::HostSettings hostSettings;
    network::ConnectionSettings connectionSettings;
    String discoveryAddress;
};

// The immutable catalog and wake callback's userdata must outlive the session.
class Session {
public:
    using Wake = void (*)(void*);
    Session(const ContentCatalog& content, std::uint64_t generation, SessionOptions options = {},
        Wake wake = nullptr, void* userdata = nullptr);
    ~Session();
    EnqueueResult TrySubmit(SessionRequest request);
    bool TryPopReply(SessionReply& reply);
    bool TakeSnapshot(SessionSnapshot& current);
    // Stops admission, resolves or cancels accepted work, and joins; replies remain readable.
    eastl::optional<SaveError> Stop();
private:
    void Run(const ContentCatalog& content, std::uint64_t generation, SessionOptions options);
    void Notify();
    SessionChannels channels_;
    Wake wake_;
    void* userdata_;
    // Written by the owner, read only after joining.
    eastl::optional<SaveError> shutdownError_;
    std::jthread thread_;
};
}
