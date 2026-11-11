#include "sdl/app.h"

#include "sdl/assets.h"
#include "sdl/gpu_renderer.h"
#include "sdl/input.h"
#include "sdl/renderer.h"
#include "sdl/session.h"

#include <SDL3/SDL.h>
#include <EASTL/unique_ptr.h>

#include <atomic>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>

namespace cataclysmx::sdl {
namespace {
struct Platform {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    ~Platform() {
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
    }
};
struct Wakeup {
    Uint32 event = 0;
    std::atomic<bool> pending{false};
};
void Wake(void* userdata) {
    auto& wakeup = *static_cast<Wakeup*>(userdata);
    if (!wakeup.pending.exchange(true)) {
        SDL_Event event{};
        event.type = wakeup.event;
        // The bounded event timeout also discovers data if SDL rejects the wake.
        SDL_PushEvent(&event);
    }
}
String Hints(const Keymap& keys) {
    String text;
    for (auto action : {InputAction::West, InputAction::East, InputAction::North, InputAction::South,
        InputAction::MoveDown, InputAction::MoveUp, InputAction::Wait, InputAction::PickUp,
        InputAction::Drop, InputAction::Interact, InputAction::Help, InputAction::Quit}) {
        text += BindingHint(keys, InputContext::Gameplay, action);
        text += " ";
        const auto name = ActionName(action);
        text.append(name.data(), name.size()); text += "\n";
    }
    text += "z zoom in\nZ zoom out\nClick bulb: toggle / blink\n";
    return text;
}
int SdlFailure() {
    std::fprintf(stderr, "SDL frontend: %s\n", SDL_GetError());
    return 1;
}
}

eastl::optional<Command> GameplayCommand(InputAction action, bool interacting) {
    Command command;
    command.kind = interacting ? CommandKind::interact : CommandKind::move;
    switch (action) {
    case InputAction::West: command.dx = -1; break;
    case InputAction::East: command.dx = 1; break;
    case InputAction::North: command.dz = -1; break;
    case InputAction::South: command.dz = 1; break;
    case InputAction::MoveDown: command.dy = -1; break;
    case InputAction::MoveUp: command.dy = 1; break;
    case InputAction::Wait: command.kind = CommandKind::wait; break;
    case InputAction::PickUp: command.kind = CommandKind::pick_up; break;
    case InputAction::Drop: command.kind = CommandKind::drop; break;
    default: return eastl::nullopt;
    }
    return command;
}

int RunApp(const ContentCatalog& content, const Settings& settings,
    const Keymap& keys, const Localization& localization, AppOptions options) {
    if (!SDL_IsMainThread()) { std::abort(); }
    Platform platform;
    if (!SDL_Init(SDL_INIT_VIDEO)) { return SdlFailure(); }
    const SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY
        | (options.smoke ? SDL_WINDOW_HIDDEN : 0);
    platform.window = SDL_CreateWindow("CataclysmX", 1120, 780, flags);
    if (!platform.window) { return SdlFailure(); }
    platform.renderer = CreateVulkanRenderer(platform.window);
    if (!platform.renderer) { return SdlFailure(); }
    SpriteShader shader(platform.renderer);
    if (!shader.Ready()) { return SdlFailure(); }
    if (!SDL_SetWindowMinimumSize(platform.window, 480, 360)) { return SdlFailure(); }
    const auto eventType = SDL_RegisterEvents(1);
    if (!eventType) { return SdlFailure(); }
    Wakeup wakeup{eventType};
    SdlText text(platform.renderer, options.fontPath.empty() ? nullptr : options.fontPath.c_str(), options.fontSize);
    if (!text.Ready()) { return SdlFailure(); }
    SdlAssets assets(&content, options.tilesetDirectory, Wake, &wakeup);
    eastl::unique_ptr<MapPreparation> mapWorker;
    MapPacket mapPacket;
    SpritePlaybackController playback;
    MapIdentity mapIdentity{1, 0, 1, 1};
    bool mapValid = false;
    Session session(content, 1, SessionOptions{settings}, Wake, &wakeup);
    SessionSnapshot snapshot;
    bool running = true, dirty = true, started = false, interacting = false;
    bool modalPending = false, modalOpen = false;
    std::uint64_t nextId = 1;
    unsigned int smokeStep = 0;
    bool smokeWaiting = false;
    bool smokeFailed = false;
    std::uint64_t smokeOpportunity = 0;
    String message = "Starting local session...";
    const String hints = Hints(keys);
    auto assetState = AssetState::Loading;
    bool minimized = false;
    int zoom = 0;
    using Clock = std::chrono::steady_clock;
    auto animationEpoch = Clock::now();
    eastl::optional<std::uint64_t> animationDeadline;
    const auto smokeDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    const auto submit = [&](RequestKind kind, Command command = {}) {
        SessionRequest request;
        request.kind = kind; request.generation = snapshot.generation;
        request.id = nextId; request.player = snapshot.view.playerId;
        request.opportunity = snapshot.view.opportunity; request.command = eastl::move(command);
        if (session.TrySubmit(eastl::move(request)) != EnqueueResult::Accepted) {
            message = "Session busy; try again."; dirty = true; return false;
        }
        ++nextId;
        return true;
    };
    const auto handleEvent = [&](const SDL_Event& event) {
        if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) { running = false; return; }
        if (event.type == SDL_EVENT_WINDOW_MINIMIZED) { minimized = true; animationDeadline.reset(); }
        if (event.type == SDL_EVENT_WINDOW_RESTORED || event.type == SDL_EVENT_WINDOW_SHOWN) { minimized = false; }
        if (event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST) { dirty = true; }
        if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED || event.type == SDL_EVENT_WINDOW_RESIZED) {
            ++mapIdentity.viewportGeneration;
            mapValid = false;
            if (mapWorker && snapshot.view.playerId) { mapWorker->Submit(snapshot.view, mapIdentity, snapshot.mapItems); }
            dirty = true;
        }
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
            if (assetState != AssetState::Ready) { return; }
            const auto time = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - animationEpoch).count());
            const auto clicked = ActivateMapItem(platform.renderer, event.button,
                modalOpen || modalPending || interacting || minimized || !started || !mapValid,
                mapPacket, assets.Definition(), playback, time, zoom, options.uiScale);
            if (!clicked) { return; }
            message = content.GetItem(clicked->item.type).name;
            switch (clicked->activation) {
            case SpriteActivationResult::Unavailable: message += ": no visual activation."; break;
            case SpriteActivationResult::LoopStarted: message += ": blinking started."; break;
            case SpriteActivationResult::Stopped: message += ": blinking stopped."; break;
            case SpriteActivationResult::PlayedOnce: message += ": blinking once."; break;
            }
            dirty = true;
            return;
        }
        if (event.type != SDL_EVENT_KEY_DOWN) { return; }
        const auto chord = TranslateKey(event.key);
        if (!chord) { return; }
        if (chord->ctrl && chord->key == "c") {
            if (!event.key.repeat) { running = false; }
            return;
        }
        const int zoomChange = MapZoomChange(*chord);
        if (zoomChange != 0 && !modalOpen && !modalPending) {
            if (!event.key.repeat) {
                zoom = std::clamp(zoom + zoomChange, -8, 16);
                dirty = true;
            }
            return;
        }
        const auto context = modalOpen || modalPending ? InputContext::Menu
            : interacting ? InputContext::Interaction : InputContext::Gameplay;
        const auto action = ResolveAction(keys, context, *chord);
        if (!action) { return; }
        if (event.key.repeat && !AllowsKeyRepeat(context, *action)) { return; }
        dirty = true;
        if (modalPending) { return; }
        if (modalOpen) {
            if (*action == InputAction::Cancel || *action == InputAction::Accept) {
                if (submit(RequestKind::Resume)) { modalPending = true; message = "Resuming..."; }
            }
            return;
        }
        if (*action == InputAction::Quit || (*action == InputAction::Cancel && !interacting)) { running = false; return; }
        if (*action == InputAction::Cancel) { interacting = false; message = "Interaction cancelled."; return; }
        if (!started || assetState != AssetState::Ready || !mapValid) { return; }
        if (*action == InputAction::Help) {
            if (submit(RequestKind::Pause)) { modalPending = true; interacting = false; message = "Pausing..."; }
            return;
        }
        if (*action == InputAction::Interact) { interacting = true; message = "Interact: choose a direction (Escape cancels)."; return; }
        const auto command = GameplayCommand(*action, interacting);
        if (command) {
            if (submit(RequestKind::Command, *command)) { interacting = false; message = "Awaiting admission..."; }
        } else { message = "This screen is not available in the local prototype."; }
    };
    while (running) {
        wakeup.pending.store(false);
        SessionReply reply;
        while (session.TryPopReply(reply)) {
            dirty = true;
            switch (reply.kind) {
            case ReplyKind::Started: started = true; message = "Local session ready."; break;
            case ReplyKind::StartupFailed: std::fputs("Session startup failed.\n", stderr); return 1;
            case ReplyKind::Admission: message = localization.ResultText(reply.result); break;
            case ReplyKind::Completed:
                message = localization.ResultText(reply.result);
                if (reply.result.storageError) { message += " (world storage failed)"; }
                if (options.smoke) {
                    const Outcome expected = smokeStep == 1 ? Outcome::moved
                        : smokeStep == 2 ? Outcome::picked_up : Outcome::waited;
                    if (reply.result.outcome != expected || reply.result.storageError) { smokeFailed = true; }
                }
                smokeWaiting = false; break;
            case ReplyKind::Paused:
                modalPending = false; modalOpen = true;
                message = "HELP | Local session paused\nUse the controls on the right.\nEscape or Enter resumes.\nPrototype worlds are temporary.";
                smokeWaiting = false; break;
            case ReplyKind::Resumed:
                modalPending = false; modalOpen = false; message = "Local session resumed.";
                smokeWaiting = false; break;
            case ReplyKind::Stale: message = "Input expired; use the current opportunity."; smokeFailed = options.smoke; smokeWaiting = false; break;
            case ReplyKind::Cancelled: message = "Input cancelled."; smokeFailed = options.smoke; smokeWaiting = false; break;
            }
        }
        if (session.TakeSnapshot(snapshot)) {
            dirty = true;
            mapIdentity.session = snapshot.generation;
            playback.SetGeneration(mapIdentity.session, mapIdentity.assetGeneration);
            ++mapIdentity.viewRevision;
            if (mapWorker) { mapWorker->Submit(snapshot.view, mapIdentity, snapshot.mapItems); }
            if (snapshot.storageError) { message = "World storage failed; see session state."; }
        }
        const auto newAssetState = assets.Poll(platform.renderer);
        dirty = newAssetState != assetState || dirty;
        assetState = newAssetState;
        if (assetState == AssetState::Failed) {
            std::fprintf(stderr, "Tileset: %s\n", FormatDiagnostics(assets.Report()).c_str()); return 1;
        }
        if (assetState == AssetState::Ready && !mapWorker) {
            animationEpoch = Clock::now();
            mapWorker.reset(new MapPreparation(content, assets.Definition(), Wake, &wakeup));
            if (snapshot.view.playerId) { mapWorker->Submit(snapshot.view, mapIdentity, snapshot.mapItems); }
        }
        MapPacket completedMap;
        if (mapWorker && mapWorker->Take(completedMap)
            && completedMap.identity.session == mapIdentity.session
            && completedMap.identity.assetGeneration == mapIdentity.assetGeneration
            && completedMap.identity.viewportGeneration == mapIdentity.viewportGeneration
            && (!mapValid || completedMap.identity.viewRevision >= mapPacket.identity.viewRevision)) {
            eastl::swap(mapPacket, completedMap); mapValid = true; dirty = true;
        }
        if (smokeFailed) { std::fputs("SDL smoke: unexpected command result\n", stderr); return 1; }
        if (options.smoke && started && snapshot.view.playerId && assetState == AssetState::Ready && mapValid && !smokeWaiting
            && (smokeStep > 3 || snapshot.view.opportunity > smokeOpportunity)) {
            smokeOpportunity = snapshot.view.opportunity;
            if (smokeStep == 0) { smokeWaiting = submit(RequestKind::Command, *GameplayCommand(InputAction::East, false)); }
            else if (smokeStep == 1) { smokeWaiting = submit(RequestKind::Command, *GameplayCommand(InputAction::PickUp, false)); }
            else if (smokeStep == 2) { smokeWaiting = submit(RequestKind::Command, *GameplayCommand(InputAction::Wait, false)); }
            else if (smokeStep == 3) { smokeWaiting = submit(RequestKind::Pause); }
            else if (smokeStep == 4) { smokeWaiting = submit(RequestKind::Resume); }
            else { running = false; }
            if (smokeWaiting) { ++smokeStep; }
        }
        const auto now = Clock::now();
        const auto elapsedMilliseconds = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now - animationEpoch).count());
        if (assetState == AssetState::Ready) { playback.RetireCompleted(assets.Definition(), elapsedMilliseconds); }
        if (animationDeadline && elapsedMilliseconds >= *animationDeadline) { dirty = true; }
        if (dirty && !minimized) {
            if (!DrawFrame(platform.renderer, text, assets, mapValid ? &snapshot.view : nullptr,
                content, message.c_str(), hints.c_str(), elapsedMilliseconds, mapValid ? &mapPacket : nullptr,
                zoom, options.uiScale, {}, &playback, &shader)) { return SdlFailure(); }
            dirty = false;
            animationDeadline.reset();
            if (mapValid) {
                int outputWidth = 0, outputHeight = 0;
                if (!SDL_GetCurrentRenderOutputSize(platform.renderer, &outputWidth, &outputHeight)) { return SdlFailure(); }
                const auto delay = VisibleAnimationDelay(mapPacket, assets.Definition(), elapsedMilliseconds,
                    outputWidth, outputHeight, zoom, options.uiScale, &playback);
                if (delay) { animationDeadline = elapsedMilliseconds + *delay; }
            }
        }
        if (options.smoke && std::chrono::steady_clock::now() >= smokeDeadline) {
            std::fputs("SDL smoke timed out.\n", stderr); return 1;
        }
        SDL_Event event;
        int waitMilliseconds = assetState == AssetState::Loading ? 1 : 20;
        if (animationDeadline && !minimized) {
            const auto currentMilliseconds = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - animationEpoch).count());
            const auto remaining = *animationDeadline > currentMilliseconds ? *animationDeadline - currentMilliseconds : 0;
            waitMilliseconds = static_cast<int>(std::min<std::uint64_t>(remaining, waitMilliseconds));
        }
        if (running && SDL_WaitEventTimeout(&event, waitMilliseconds)) {
            handleEvent(event);
            for (int count = 0; count < 127 && SDL_PollEvent(&event); ++count) { handleEvent(event); }
        }
    }
    session.Stop();
    SessionReply finalReply;
    while (session.TryPopReply(finalReply)) {
        if (finalReply.result.storageError) { std::fputs("Session shutdown: world storage failed.\n", stderr); }
    }
    if (options.smoke) { std::fputs("SDL smoke: ok\n", stdout); }
    return 0;
}

}
