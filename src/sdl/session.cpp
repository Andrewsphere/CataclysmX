#include "sdl/session.h"

#include <EASTL/utility.h>
#include <chrono>

namespace cataclysmx::sdl {
Session::Session(const ContentCatalog& content, std::uint64_t generation, SessionOptions options,
    Wake wake, void* userdata) : wake_(wake), userdata_(userdata),
    thread_([this, &content, generation, options = eastl::move(options)] { Run(content, generation, options); }) {}
Session::~Session() { Stop(); }
EnqueueResult Session::TrySubmit(SessionRequest request) { return channels_.TrySubmit(eastl::move(request)); }
bool Session::TryPopReply(SessionReply& reply) { return channels_.TryPopReply(reply); }
bool Session::TakeSnapshot(SessionSnapshot& current) { return channels_.TakeSnapshot(current); }
void Session::Stop() { channels_.Stop(); if (thread_.joinable()) { thread_.join(); } }
void Session::Notify() { if (wake_) { wake_(userdata_); } }

void Session::Run(const ContentCatalog& content, std::uint64_t generation, SessionOptions options) {
    if (!ValidateServerContent(content)) {
        channels_.PublishReply({ReplyKind::StartupFailed, generation, 0, {}, StartupError::InvalidContent});
        Notify();
        channels_.Stop();
        SessionRequest request;
        while (channels_.TakeRequest(request)) {
            channels_.PublishReply({ReplyKind::Cancelled, generation, request.id}, true);
        }
        Notify();
        return;
    }
    Server server(content, options.settings.seed);
    if (!ApplySettings(server, options.settings)) {
        channels_.PublishReply({ReplyKind::StartupFailed, generation, 0, {}, StartupError::InvalidSettings});
        channels_.Stop();
        SessionRequest request;
        while (channels_.TakeRequest(request)) {
            channels_.PublishReply({ReplyKind::Cancelled, generation, request.id}, true);
        }
        Notify();
        return;
    }
    SessionSnapshot building;
    bool paused = false;
    std::uint64_t sequence = 0;
    eastl::optional<std::uint64_t> pendingId;
    std::uint64_t pendingSequence = 0;
    std::uint64_t publishedRevision = 0;
    bool publishedPaused = false;
    eastl::optional<SaveError> storageError;
    eastl::optional<SaveError> publishedStorageError;
    auto publish = [&] {
        const auto revision = server.GetRevision();
        if (publishedRevision == revision && publishedPaused == paused && publishedStorageError == storageError) { return; }
        building.generation = generation;
        building.paused = paused;
        building.storageError = storageError;
        building.view = server.View(options.settings.viewRadius);
        building.mapItems = CaptureMapItems(server.GetWorld(), building.view);
        channels_.PublishSnapshot(building);
        publishedRevision = revision;
        publishedPaused = paused;
        publishedStorageError = storageError;
        Notify();
    };
    auto complete = [&](const SessionUpdate& update) {
        if (update.storageError) { storageError = update.storageError; }
        for (const auto& result : update.commandResults) {
            if (pendingId && result.sequence == pendingSequence && result.player == server.GetHostPlayerId()) {
                channels_.PublishReply({ReplyKind::Completed, generation, *pendingId, result.result});
                pendingId.reset();
                Notify();
            }
        }
    };
    channels_.PublishReply({ReplyKind::Started, generation});
    publish();
    using Clock = std::chrono::steady_clock;
    auto baseline = Clock::now();
    auto settle = [&] {
        const auto now = Clock::now();
        if (!paused) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - baseline).count();
            if (elapsed > 0) {
                complete(server.Update(static_cast<std::uint64_t>(elapsed)));
                baseline += std::chrono::microseconds(elapsed);
            }
        } else { baseline = now; }
    };
    for (;;) {
        SessionRequest request;
        while (channels_.TakeRequest(request)) {
            settle();
            if (channels_.Stopping()) {
                channels_.PublishReply({ReplyKind::Cancelled, generation, request.id}, true);
            } else if (request.generation != generation) {
                channels_.PublishReply({ReplyKind::Stale, generation, request.id}, true);
            } else if (request.kind == RequestKind::Pause || request.kind == RequestKind::Resume) {
                paused = request.kind == RequestKind::Pause;
                baseline = Clock::now();
                publish();
                channels_.PublishReply({paused ? ReplyKind::Paused : ReplyKind::Resumed, generation, request.id}, true);
            } else if (paused || request.player != server.GetHostPlayerId()) {
                channels_.PublishReply({ReplyKind::Cancelled, generation, request.id}, true);
            } else {
                const auto result = server.Submit(request.player, {++sequence, request.opportunity, eastl::move(request.command)});
                if (result.outcome == Outcome::queued) {
                    if (pendingId) { channels_.PublishReply({ReplyKind::Cancelled, generation, *pendingId}); }
                    pendingId = request.id;
                    pendingSequence = sequence;
                    channels_.PublishReply({ReplyKind::Admission, generation, request.id, result});
                    complete(server.Update(0));
                } else {
                    channels_.PublishReply({ReplyKind::Completed, generation, request.id, result}, true);
                }
            }
            publish();
            Notify();
        }
        if (channels_.Stopping()) { break; }
        settle();
        publish();
        channels_.Wait();
    }
    if (pendingId) { channels_.PublishReply({ReplyKind::Cancelled, generation, *pendingId}); }
    Notify();
}
}
