#pragma once

#include "sdl/channels.h"
#include "client/settings.h"

#include <thread>

namespace cataclysmx::sdl {

struct SessionOptions { Settings settings; };

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
    void Stop();
private:
    void Run(const ContentCatalog& content, std::uint64_t generation, SessionOptions options);
    void Notify();
    SessionChannels channels_;
    Wake wake_;
    void* userdata_;
    std::jthread thread_;
};
}
