#include "sdl/app.h"
#include "client/combat_text.h"

#include "sdl/assets.h"
#include "sdl/gpu_renderer.h"
#include "sdl/input.h"
#include "sdl/renderer.h"
#include "sdl/session.h"
#include "sdl/screens.h"

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
String InteractionReason(const Localization& localization, ObjectUseReason reason) {
    const char* key = nullptr;
    switch (reason) {
    case ObjectUseReason::Locked: key = "interaction.reason.locked"; break;
    case ObjectUseReason::AlreadyLocked: key = "interaction.reason.already_locked"; break;
    case ObjectUseReason::AlreadyUnlocked: key = "interaction.reason.already_unlocked"; break;
    case ObjectUseReason::MustClose: key = "interaction.reason.must_close"; break;
    default: return ObjectReasonText(reason);
    }
    return localization.Text(key,ObjectReasonText(reason));
}
String Hints(const Keymap& keys) {
    String text;
    for (auto action : {InputAction::West, InputAction::East, InputAction::North, InputAction::South,
        InputAction::Northwest, InputAction::Northeast, InputAction::Southwest, InputAction::Southeast,
        InputAction::MoveDown, InputAction::MoveUp, InputAction::Examine, InputAction::Settings, InputAction::Debug, InputAction::Wait, InputAction::PickUp,
        InputAction::Drop, InputAction::Interact, InputAction::Attack, InputAction::StyleBalanced,
        InputAction::StyleAggressive, InputAction::StyleDefensive, InputAction::Help, InputAction::Quit}) {
        text += BindingHint(keys, InputContext::Gameplay, action);
        text += " ";
        const auto name = ActionName(action);
        text.append(name.data(), name.size()); text += "\n";
    }
    text += "z zoom in (also Examine/Interact)\nZ zoom out (also Examine/Interact)\nInteract: directions open actions; . selects current tile\nExamine: directions move cursor; layer bindings change viewed Y\nExamine: PgUp/PgDn or Ctrl+Up/Down scroll details\nClick bulb: toggle / blink\n";
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
    case InputAction::Northwest: command.dx = -1; command.dz = -1; break;
    case InputAction::Northeast: command.dx = 1; command.dz = -1; break;
    case InputAction::Southwest: command.dx = -1; command.dz = 1; break;
    case InputAction::Southeast: command.dx = 1; command.dz = 1; break;
    case InputAction::West: command.dx = -1; break;
    case InputAction::East: command.dx = 1; break;
    case InputAction::North: command.dz = -1; break;
    case InputAction::South: command.dz = 1; break;
    case InputAction::MoveDown: command.dy = -1; break;
    case InputAction::MoveUp: command.dy = 1; break;
    case InputAction::Wait: command.kind = CommandKind::wait; break;
    case InputAction::Attack: command.kind = CommandKind::attack; break;
    case InputAction::StyleBalanced: command.kind = CommandKind::combat_style; command.style = CombatStyle::Balanced; break;
    case InputAction::StyleAggressive: command.kind = CommandKind::combat_style; command.style = CombatStyle::Aggressive; break;
    case InputAction::StyleDefensive: command.kind = CommandKind::combat_style; command.style = CombatStyle::Defensive; break;
    case InputAction::PickUp: command.kind = CommandKind::pick_up; break;
    case InputAction::Drop: command.kind = CommandKind::drop; break;
    default: return eastl::nullopt;
    }
    return command;
}

int RunApp(const ContentCatalog& content, SettingsStore& store, Localization& localization,
    const char* localizationDirectory, AppOptions options) {
    const auto& settings = store.Effective();
    const auto& keys = store.Keys();
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
    bool running = true, dirty = true, started = false;
    enum class Screen { None, Help, Examine, Targets, Actions, Options, Debug };
    Screen screen = Screen::None, pendingScreen = Screen::Help;
    bool modalPending = false, initializeScreen = false;
    ScreenFrame frame;
    OptionsUi optionsUi;
    TileCoord cursor{};
    Vector<String> inspection;
    eastl::optional<std::uint64_t> inspectionId;
    TileCoord requestedCursor{};
    bool inspectionNeeded = false;
    Vector<InteractionChoice> actions;
    std::size_t selectedAction = 0;
    eastl::optional<SettingsStore> candidateSettings;
    Localization candidateLocalization;
    eastl::optional<std::uint64_t> settingsId;
    bool saveSettings = false, reloadSettings = false;
    RegionPlan debugPlan;
    WorldgenDebugView debugImage;
    Vector<String> debugDetails;
    WorldgenDebugLayer debugLayer = WorldgenDebugLayer::Roads;
    int debugScale = 8;
    std::uint64_t nextId = 1;
    unsigned int smokeStep = 0;
    bool smokeWaiting = false;
    bool smokeFailed = false;
    bool smokeExamined = false;
    std::uint64_t smokeOpportunity = 0;
    String message = "Starting local session...";
    String hints = Hints(keys);
    auto assetState = AssetState::Loading;
    bool minimized = false;
    HeldKey heldKey;
    bool commandPending = false;
    int zoom = 0;
    using Clock = std::chrono::steady_clock;
    auto animationEpoch = Clock::now();
    eastl::optional<std::uint64_t> animationDeadline;
    const auto smokeDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    const auto enqueue = [&](SessionRequest request) {
        const bool isCommand = request.kind == RequestKind::Command || request.kind == RequestKind::ResumeAndCommand;
        if (isCommand && commandPending) { return false; }
        request.generation = snapshot.generation; request.id = nextId;
        request.player = snapshot.view.playerId; request.opportunity = snapshot.view.opportunity;
        if (session.TrySubmit(eastl::move(request)) != EnqueueResult::Accepted) {
            message = "Session busy; try again."; dirty = true; return false;
        }
        if (isCommand) { commandPending = true; }
        ++nextId; return true;
    };
    const auto submit = [&](RequestKind kind, Command command = {}) {
        SessionRequest request; request.kind = kind; request.command = eastl::move(command);
        return enqueue(eastl::move(request));
    };
    const auto close = [&] {
        if (submit(RequestKind::Resume)) { modalPending = true; message = "Resuming..."; SDL_StopTextInput(platform.window);
            inspectionId.reset(); inspectionNeeded = false; }
    };
    const auto rebuildDebug = [&] {
        cursor.x = std::clamp(cursor.x,-world_coordinate_limit,world_coordinate_limit);
        cursor.z = std::clamp(cursor.z,-world_coordinate_limit,world_coordinate_limit);
        cursor.y = 0;
        if (snapshot.generationVersion != world_generation_version) { return; }
        debugPlan = GenerateRegionPlan(content,snapshot.seed,RegionOf(cursor));
        debugImage = BuildWorldgenDebugView(content,debugPlan,debugLayer,debugScale);
        debugDetails = WorldgenDebugDetails(content,debugPlan,cursor,&localization);
    };
    const auto collectActions = [&] {
        actions.clear();
        for (const auto& choice : snapshot.interactions) { if (choice.request.position == cursor) { actions.push_back(choice); } }
        selectedAction = 0;
    };
    const auto executeAction = [&] {
        const auto& choice = actions[selectedAction];
        if (choice.reason != ObjectUseReason::None) { message = InteractionReason(localization,choice.reason); return; }
        Command command; command.kind = CommandKind::interact_action; command.interaction = choice.request;
        if (submit(RequestKind::ResumeAndCommand,eastl::move(command))) {
            screen = Screen::None; frame.visible = false; message = "Awaiting admission...";
        }
    };
    const auto openActions = [&] {
        collectActions();
        if (actions.empty()) { return; }
        screen = Screen::Actions; frame.scroll = 0;
        if (const auto single = SoleAvailableAction(actions)) { selectedAction = *single; executeAction(); }
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
                screen != Screen::None || modalPending || minimized || !started || !mapValid,
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
        if (event.type == SDL_EVENT_TEXT_INPUT && screen == Screen::Options && !settingsId) {
            optionsUi.Text(event.text.text); dirty = true; return;
        }
        if (event.type != SDL_EVENT_KEY_DOWN) { return; }
        const auto chord = TranslateKey(event.key);
        if (!chord) { return; }
        if (chord->ctrl && chord->key == "c") {
            running = false;
            return;
        }
        const int zoomChange = MapZoomChange(*chord);
        if (zoomChange != 0 && (screen == Screen::None || screen == Screen::Examine || screen == Screen::Targets || screen == Screen::Actions) && !modalPending) {
            zoom = std::clamp(zoom + zoomChange, -8, 12);
            dirty = true;
            return;
        }
        const auto context = screen == Screen::Debug ? InputContext::Debug
            : screen != Screen::None || modalPending ? InputContext::Menu : InputContext::Gameplay;
        auto action = ResolveAction(keys, context, *chord);
        if (screen == Screen::Examine || screen == Screen::Targets) {
            const auto gameAction = ResolveAction(keys,InputContext::Gameplay,*chord);
            if (gameAction == InputAction::Northwest || gameAction == InputAction::Northeast
                || gameAction == InputAction::Southwest || gameAction == InputAction::Southeast
                || gameAction == InputAction::MoveUp || gameAction == InputAction::MoveDown) { action = gameAction; }
        }
        dirty = true;
        if (modalPending || settingsId) { return; }
        if (screen == Screen::Options) {
            const bool capturing = optionsUi.Capturing();
            const auto result = optionsUi.Key(*chord,store,localization,localizationDirectory);
            if (capturing) { heldKey.Reset(); }
            if (result == OptionsEvent::Close) { close(); }
            if (optionsUi.EditingText()) { SDL_StartTextInput(platform.window); }
            else { SDL_StopTextInput(platform.window); }
            if (result == OptionsEvent::Save) {
                candidateSettings = store;
                Diagnostics diagnostics;
                if (!optionsUi.Prepare(store,*candidateSettings,&diagnostics)
                    || !candidateLocalization.Load(localizationDirectory,candidateSettings->Effective().language,&diagnostics)) {
                    optionsUi.Feedback(FormatDiagnostics(diagnostics)); candidateSettings.reset(); return;
                }
                SessionRequest request; request.kind = RequestKind::ApplySettings;
                request.globalSettings = candidateSettings->Overrides(SettingsScope::Global);
                request.worldSettings = candidateSettings->Overrides(SettingsScope::World);
                const auto id = nextId;
                if (enqueue(eastl::move(request))) { settingsId = id; saveSettings = result == OptionsEvent::Save; reloadSettings = false; }
                else { candidateSettings.reset(); optionsUi.Feedback(message); }
            }
            return;
        }
        if (screen == Screen::Targets && (chord->key == "." || chord->key == "Home")) {
            cursor = snapshot.view.player_position; openActions(); return;
        }
        if ((screen == Screen::Examine || screen == Screen::Help)
            && ((chord->ctrl && (chord->key == "Up" || chord->key == "Down"))
                || chord->key == "PageUp" || chord->key == "PageDown")) {
            const bool down = chord->key == "Down" || chord->key == "PageDown";
            frame.scroll = std::max(0,frame.scroll + (down ? 1 : -1)); return;
        }
        if (!action) { return; }
        if (screen != Screen::None) {
            if (*action == InputAction::Cancel || (screen == Screen::Debug && *action == InputAction::Debug)) {
                if (screen == Screen::Actions) { screen = Screen::Targets; frame.scroll = 0; return; }
                close(); return;
            }
            if (screen == Screen::Help) { if (*action == InputAction::Accept) { close(); } return; }
            if (screen == Screen::Debug) {
                switch (*action) {
                case InputAction::West: cursor.x -= debugScale; break;
                case InputAction::East: cursor.x += debugScale; break;
                case InputAction::North: cursor.z -= debugScale; break;
                case InputAction::South: cursor.z += debugScale; break;
                case InputAction::JumpWest: cursor.x -= region_size; break;
                case InputAction::JumpEast: cursor.x += region_size; break;
                case InputAction::JumpNorth: cursor.z -= region_size; break;
                case InputAction::JumpSouth: cursor.z += region_size; break;
                case InputAction::Recenter: cursor = snapshot.view.player_position; break;
                case InputAction::ZoomIn: debugScale = std::max(1,debugScale/2); break;
                case InputAction::ZoomOut: debugScale = std::min(16,debugScale*2); break;
                case InputAction::Next: debugLayer = static_cast<WorldgenDebugLayer>((static_cast<unsigned>(debugLayer)+1)%5); break;
                case InputAction::Previous: debugLayer = static_cast<WorldgenDebugLayer>((static_cast<unsigned>(debugLayer)+4)%5); break;
                case InputAction::Layer1: case InputAction::Layer2: case InputAction::Layer3: case InputAction::Layer4: case InputAction::Layer5:
                    debugLayer = static_cast<WorldgenDebugLayer>(static_cast<unsigned>(*action)-static_cast<unsigned>(InputAction::Layer1)); break;
                case InputAction::DetailsDown: frame.detailScroll = std::min(frame.detailScroll+1,std::max(0,static_cast<int>(debugDetails.size())-1)); return;
                case InputAction::DetailsUp: frame.detailScroll = std::max(0,frame.detailScroll-1); return;
                default: return;
                }
                frame.detailScroll = 0; rebuildDebug(); return;
            }
            if (screen == Screen::Actions) {
                if (actions.empty()) { screen = Screen::Targets; return; }
                if (*action == InputAction::North) { selectedAction = (selectedAction+actions.size()-1)%actions.size(); }
                if (*action == InputAction::South) { selectedAction = (selectedAction+1)%actions.size(); }
                if (*action == InputAction::Accept) {
                    executeAction();
                }
                return;
            }
            if (*action == InputAction::Accept && screen == Screen::Targets) {
                openActions();
                return;
            }
            TileCoord next = cursor;
            switch (*action) {
            case InputAction::Northwest: --next.x; --next.z; break;
            case InputAction::Northeast: ++next.x; --next.z; break;
            case InputAction::Southwest: --next.x; ++next.z; break;
            case InputAction::Southeast: ++next.x; ++next.z; break;
            case InputAction::MoveUp: if (screen != Screen::Examine) { return; } next.y = std::min(world_max_y,next.y+1); break;
            case InputAction::MoveDown: if (screen != Screen::Examine) { return; } next.y = std::max(world_min_y,next.y-1); break;
            case InputAction::West: --next.x; break;
            case InputAction::East: ++next.x; break;
            case InputAction::North: --next.z; break;
            case InputAction::South: ++next.z; break;
            default: return;
            }
            if (screen == Screen::Targets) {
                // Directions select an adjacent target relative to the player.
                const auto p = snapshot.view.player_position;
                next = {p.x + next.x-cursor.x,p.z + next.z-cursor.z,p.y};
                bool found = false;
                for (const auto& choice : snapshot.interactions) { found |= choice.request.position == next; }
                if (!found) { return; }
            } else {
                next.x = std::clamp(next.x,snapshot.view.origin.x,snapshot.view.origin.x+snapshot.view.width-1);
                next.z = std::clamp(next.z,snapshot.view.origin.z,snapshot.view.origin.z+snapshot.view.height-1);
            }
            cursor = next;
            if (screen == Screen::Targets) { openActions(); return; }
            inspectionNeeded = screen == Screen::Examine; frame.scroll = 0;
            if (inspectionNeeded) { inspection.clear(); inspection.push_back("Loading tile details..."); }
            return;
        }
        if (*action == InputAction::Quit || *action == InputAction::Cancel) { running = false; return; }
        if (!started || assetState != AssetState::Ready || !mapValid) { return; }
        if (*action == InputAction::Help || *action == InputAction::Examine || *action == InputAction::Interact
            || *action == InputAction::Settings || *action == InputAction::Debug) {
            pendingScreen = *action == InputAction::Help ? Screen::Help : *action == InputAction::Examine ? Screen::Examine
                : *action == InputAction::Interact ? Screen::Targets : *action == InputAction::Settings ? Screen::Options : Screen::Debug;
            if (submit(RequestKind::Pause)) { modalPending = true; message = "Pausing..."; }
            return;
        }
        const auto command = GameplayCommand(*action,false);
        if (command) { if (submit(RequestKind::Command,*command)) { message = "Awaiting admission..."; } }
        else { message = "This screen is not available in the local prototype."; }
    };
    const auto inputEpoch = Clock::now();
    const auto inputTime = [&] { return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-inputEpoch).count()); };
    const auto physicalEvent = [&](const SDL_Event& event) {
        if (heldKey.Observe(event,inputTime())) { handleEvent(event); }
    };
    while (running) {
        wakeup.pending.store(false);
        SessionReply reply;
        while (session.TryPopReply(reply)) {
            if (reply.generation != snapshot.generation && reply.generation != 1) { continue; }
            dirty = true;
            switch (reply.kind) {
            case ReplyKind::Started: started = true; message = "Local session ready."; break;
            case ReplyKind::StartupFailed: std::fputs("Session startup failed.\n", stderr); return 1;
            case ReplyKind::Admission: message = localization.ResultText(reply.result); break;
            case ReplyKind::Completed:
                commandPending = false;
                message = localization.ResultText(reply.result);
                if (reply.result.storageError) { message += " (world storage failed)"; }
                if (options.smoke) {
                    const Outcome expected = smokeStep == 1 || smokeStep == 14 ? Outcome::moved
                        : smokeStep == 2 ? Outcome::picked_up : smokeStep == 13 ? Outcome::toggled
                        : smokeStep == 17 ? Outcome::locked : Outcome::waited;
                    if (reply.result.outcome != expected || reply.result.storageError) {
                        std::fprintf(stderr,"SDL smoke step %u: outcome %d, expected %d\n",smokeStep,static_cast<int>(reply.result.outcome),static_cast<int>(expected));
                        smokeFailed = true;
                    }
                }
                smokeWaiting = false; break;
            case ReplyKind::Paused:
                modalPending = false; screen = pendingScreen; initializeScreen = true;
                message = "Local session paused.";
                smokeWaiting = false; break;
            case ReplyKind::Resumed:
                modalPending = false; screen = Screen::None; frame.visible = false; message = "Local session resumed.";
                smokeWaiting = false; break;
            case ReplyKind::Inspected:
                if (inspectionId && *inspectionId == reply.id) {
                    if (screen == Screen::Examine && cursor == requestedCursor) {
                        inspection = eastl::move(reply.details);
                        if (snapshot.view.origin.y != reply.inspectionView.origin.y) { mapValid = false; }
                        snapshot.view = eastl::move(reply.inspectionView);
                        snapshot.mapItems = eastl::move(reply.inspectionItems);
                        ++mapIdentity.viewRevision;
                        if (mapWorker) { mapWorker->Submit(snapshot.view,mapIdentity,snapshot.mapItems); }
                    }
                    inspectionId.reset();
                }
                break;
            case ReplyKind::SettingsApplied:
                if (settingsId && *settingsId == reply.id && candidateSettings) {
                    store = eastl::move(*candidateSettings); localization = eastl::move(candidateLocalization);
                    candidateSettings.reset(); settingsId.reset(); hints = Hints(store.Keys());
                    optionsUi.Applied(store,reply.runtimeSettings,reloadSettings);
                    smokeWaiting = false;
                    Diagnostics diagnostics;
                    if (saveSettings && optionsUi.Scope() == SettingsScope::World && store.WorldDirectory().empty()) {
                        optionsUi.Feedback("Applied for this session; temporary world has no save directory.");
                    } else if (saveSettings && !store.Save(optionsUi.Scope(),&diagnostics)) {
                        optionsUi.Feedback("Applied, unsaved: " + FormatDiagnostics(diagnostics));
                    }
                    else { optionsUi.Feedback(saveSettings ? "Saved" : "Applied"); }
                }
                break;
            case ReplyKind::SettingsFailed:
                if (settingsId && *settingsId == reply.id) {
                    settingsId.reset(); candidateSettings.reset(); smokeFailed = options.smoke;
                    optionsUi.Feedback(reply.details.empty() ? String("Settings could not be applied") : reply.details.front());
                }
                break;
            case ReplyKind::Stale: commandPending = false; message = "Input expired; use the current opportunity."; smokeFailed = options.smoke; smokeWaiting = false; break;
            case ReplyKind::Cancelled: commandPending = false; message = "Input cancelled."; smokeFailed = options.smoke; smokeWaiting = false; break;
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
        if (initializeScreen && snapshot.paused) {
            initializeScreen = false; cursor = snapshot.view.player_position;
            frame.scroll = 0; frame.detailScroll = 0; inspection.clear();
            if (screen == Screen::Options) {
                optionsUi.Open(store,snapshot.runtimeSettings);
                if (!store.Dirty(SettingsScope::Global) && !store.Dirty(SettingsScope::World)) {
                    candidateSettings = store;
                    Diagnostics diagnostics;
                    auto loaded = candidateSettings->Load(&diagnostics);
                    if (loaded && store.WorldDirectory().empty()) {
                        loaded = candidateSettings->SetOverrides(SettingsScope::World,store.Overrides(SettingsScope::World),store.KeyOverrides(SettingsScope::World),&diagnostics);
                    }
                    if (loaded && candidateLocalization.Load(localizationDirectory,candidateSettings->Effective().language,&diagnostics)) {
                        SessionRequest request; request.kind = RequestKind::ApplySettings;
                        request.globalSettings = candidateSettings->Overrides(SettingsScope::Global);
                        request.worldSettings = candidateSettings->Overrides(SettingsScope::World);
                        const auto id = nextId;
                        if (enqueue(eastl::move(request))) { settingsId = id; saveSettings = false; reloadSettings = true; }
                        else { candidateSettings.reset(); optionsUi.Feedback(message); }
                    } else { candidateSettings.reset(); optionsUi.Feedback(FormatDiagnostics(diagnostics)); }
                }
            }
            if (screen == Screen::Examine) { inspectionNeeded = true; }
            if (screen == Screen::Debug) { rebuildDebug(); }
            if (screen == Screen::Targets && !snapshot.interactions.empty()) {
                cursor = snapshot.interactions.front().request.position;
                bool single = true;
                for (const auto& choice : snapshot.interactions) { single &= choice.request.position == cursor; }
                if (single) { openActions(); }
            }
        }
        if (screen == Screen::Examine && inspectionNeeded && !inspectionId && snapshot.paused) {
            SessionRequest request; request.kind = RequestKind::Inspect; request.cursor = cursor;
            const auto id = nextId;
            if (enqueue(eastl::move(request))) { inspectionId = id; requestedCursor = cursor; inspectionNeeded = false; }
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
            && (!mapValid ? completedMap.identity.viewRevision == mapIdentity.viewRevision : completedMap.identity.viewRevision >= mapPacket.identity.viewRevision)) {
            eastl::swap(mapPacket, completedMap); mapValid = true; dirty = true;
        }
        if (smokeFailed) { std::fprintf(stderr,"SDL smoke step %u: %s\n",smokeStep,message.c_str()); return 1; }
        if (options.smoke && started && snapshot.view.playerId && assetState == AssetState::Ready && mapValid && !smokeWaiting && !settingsId
            && (smokeStep > 3 || snapshot.view.opportunity > smokeOpportunity)) {
            const auto press = [&](SDL_Keycode key, SDL_Keymod modifiers = SDL_KMOD_NONE) {
                SDL_Event event{}; event.type = SDL_EVENT_KEY_DOWN; event.key.key = key; event.key.mod = modifiers;
                handleEvent(event);
            };
            smokeOpportunity = snapshot.view.opportunity;
            if (smokeStep == 0) { smokeWaiting = submit(RequestKind::Command, *GameplayCommand(InputAction::East, false)); }
            else if (smokeStep == 1) { smokeWaiting = submit(RequestKind::Command, *GameplayCommand(InputAction::PickUp, false)); }
            else if (smokeStep == 2) { smokeWaiting = submit(RequestKind::Command, *GameplayCommand(InputAction::Wait, false)); }
            else if (smokeStep == 3) { smokeWaiting = submit(RequestKind::Pause); }
            else if (smokeStep == 4) { smokeWaiting = submit(RequestKind::Resume); }
            else if (smokeStep == 5) { press(SDLK_X); smokeWaiting = modalPending; }
            else if (smokeStep == 6) {
                if (!inspectionId && !inspectionNeeded && !inspection.empty()) {
                    if (!smokeExamined) {
                        const auto before = cursor;
                        press(SDLK_Z); press(SDLK_RIGHT,SDL_KMOD_SHIFT); press(SDLK_UP,SDL_KMOD_ALT);
                        smokeExamined = true;
                        if (zoom != 1 || cursor != TileCoord{before.x+1,before.z-1,before.y+1}) { smokeFailed = true; }
                    } else {
                        if (snapshot.view.origin.y != cursor.y) { smokeFailed = true; }
                        press(SDLK_ESCAPE); smokeWaiting = modalPending;
                    }
                }
            }
            else if (smokeStep == 7) { press(SDLK_F11); smokeWaiting = modalPending; }
            else if (smokeStep == 8) { press(SDLK_ESCAPE); smokeWaiting = modalPending; }
            else if (smokeStep == 9) { press(SDLK_F2); smokeWaiting = modalPending; }
            else if (smokeStep == 10) {
                press(SDLK_DOWN); press(SDLK_RETURN);
                SDL_Event entry{}; entry.type = SDL_EVENT_TEXT_INPUT; entry.text.text = "12"; handleEvent(entry);
                press(SDLK_RETURN); press(SDLK_S); smokeWaiting = settingsId.has_value();
                if (!smokeWaiting) { smokeFailed = true; }
            }
            else if (smokeStep == 11) {
                if (store.Effective().viewRadius != 12) { smokeFailed = true; }
                press(SDLK_ESCAPE); smokeWaiting = modalPending;
            }
            else if (smokeStep == 12) { press(SDLK_E); smokeWaiting = modalPending; }
            else if (smokeStep == 13) { smokeWaiting = submit(RequestKind::Command,*GameplayCommand(InputAction::East,false)); }
            else if (smokeStep == 14) { press(SDLK_E); smokeWaiting = modalPending; }
            else if (smokeStep == 15) {
                press(SDLK_RIGHT);
                if (screen != Screen::Actions) { smokeFailed = true; }
                ++smokeStep;
            }
            else if (smokeStep == 16) {
                press(SDLK_DOWN); press(SDLK_DOWN); press(SDLK_RETURN);
                smokeWaiting = commandPending;
                if (!smokeWaiting) { smokeFailed = true; }
            }
            else { running = false; }
            if (smokeWaiting) { ++smokeStep; }
        }
        const auto now = Clock::now();
        const auto elapsedMilliseconds = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now - animationEpoch).count());
        if (assetState == AssetState::Ready) { playback.RetireCompleted(assets.Definition(), elapsedMilliseconds); }
        if (animationDeadline && elapsedMilliseconds >= *animationDeadline) { dirty = true; }
        if (dirty && !minimized) {
            String display = message;
            if (mapValid) {
                const auto combat = CombatStatusText(snapshot.view.combat);
                const auto feedback = CombatFeedbackText(snapshot.view.combat);
                if (!combat.empty()) { display += '\n'; display += combat; }
                if (!feedback.empty()) { display += '\n'; display += feedback; }
            }
            frame.visible = screen != Screen::None;
            frame.full = screen == Screen::Options || screen == Screen::Debug || screen == Screen::Help;
            frame.followCursor = screen == Screen::Examine;
            frame.cursor.reset(); frame.highlights.clear(); frame.debugImage = nullptr;
            frame.rows.clear(); frame.details.clear(); frame.selected = -1;
            if (screen == Screen::Options) { optionsUi.Present(store,localization,frame); }
            if (screen == Screen::Help) {
                frame.title = "Controls | Local session paused";
                std::size_t start = 0;
                for (std::size_t i = 0; i <= hints.size(); ++i) {
                    if (i == hints.size() || hints[i] == '\n') {
                        frame.rows.push_back({String(hints.data()+start,i-start)}); start = i+1;
                    }
                }
                frame.footer = "PgUp/PgDn: scroll | Escape / Enter: resume | Prototype worlds are temporary";
            }
            if (screen == Screen::Examine) {
                frame.title = "Examine | X " + NumberText(cursor.x) + " Z " + NumberText(cursor.z) + " Y " + NumberText(cursor.y);
                frame.cursor = cursor;
                for (const auto& line : inspection) { frame.rows.push_back({line}); }
                frame.footer = "Directions: cursor | Alt+Up/Down: layer | z/Z: zoom | PgUp/PgDn: scroll | Escape: resume";
            }
            if (screen == Screen::Targets || screen == Screen::Actions) {
                frame.cursor = cursor;
                for (const auto& choice : snapshot.interactions) { frame.highlights.push_back(choice.request.position); }
                frame.title = screen == Screen::Targets ? "Interact | Choose a highlighted tile" : "Interact | Choose an action";
                if (screen == Screen::Targets) {
                    frame.rows.push_back({snapshot.interactions.empty() ? "No reachable targets" : "Direction: actions | .: current tile | Escape: resume"});
                } else {
                    frame.selected = static_cast<int>(selectedAction);
                    for (const auto& choice : actions) {
                        String actionLabel = choice.label;
                        if (choice.request.kind == InteractionKind::Lock) { actionLabel = localization.Text("interaction.action.lock",choice.label); }
                        if (choice.request.kind == InteractionKind::Unlock) { actionLabel = localization.Text("interaction.action.unlock",choice.label); }
                        String label = choice.target + ": " + actionLabel;
                        frame.rows.push_back({eastl::move(label),choice.reason != ObjectUseReason::None});
                    }
                    if (selectedAction < actions.size() && actions[selectedAction].reason != ObjectUseReason::None) {
                        frame.details.push_back(InteractionReason(localization,actions[selectedAction].reason));
                    }
                }
                frame.footer = "Up/Down: choose | Enter: confirm | Escape: back / resume";
            }
            if (screen == Screen::Debug) {
                frame.title = "World generation | Seed " + UnsignedText(snapshot.seed) + " | Version " + UnsignedText(snapshot.generationVersion);
                if (snapshot.generationVersion != world_generation_version) { frame.rows.push_back({"Preview unavailable for this generation version"}); }
                else {
                    frame.title += "\n"; frame.title += WorldgenDebugLayerName(debugLayer);
                    frame.title += " | Scale "; frame.title += NumberText(debugScale);
                    frame.title += " | X "; frame.title += NumberText(cursor.x); frame.title += " Z "; frame.title += NumberText(cursor.z);
                    frame.title += "\nSurface baseline: excludes saved edits and entities";
                    frame.debugImage = &debugImage; frame.cursor = cursor; frame.details = debugDetails;
                }
                frame.footer = WorldgenDebugLegend(debugLayer);
                frame.footer += "\nArrows/hjkl: move | HJKL: region | Home: recenter | 1-5/Tab: layers | +/-: zoom | PgUp/PgDn: details | Escape: close";
            }
            if (!DrawFrame(platform.renderer, text, assets, mapValid ? &snapshot.view : nullptr,
                content, display.c_str(), hints.c_str(), elapsedMilliseconds, mapValid ? &mapPacket : nullptr,
                zoom, options.uiScale, {}, &playback, &shader, &frame)) { return SdlFailure(); }
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
        if (const auto repeated = heldKey.Poll(inputTime(),store.Effective().keyboardRepeatRate)) {
            SDL_Event held{}; held.type = SDL_EVENT_KEY_DOWN; held.key = *repeated;
            const String committed = heldKey.Text();
            handleEvent(held);
            if (!committed.empty() && !(repeated->mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI))
                && screen == Screen::Options && optionsUi.EditingText() && !settingsId) {
                held.type = SDL_EVENT_TEXT_INPUT; held.text.text = committed.c_str(); handleEvent(held);
            }
        }
        SDL_Event event;
        int waitMilliseconds = assetState == AssetState::Loading ? 1 : 20;
        if (animationDeadline && !minimized) {
            const auto currentMilliseconds = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - animationEpoch).count());
            const auto remaining = *animationDeadline > currentMilliseconds ? *animationDeadline - currentMilliseconds : 0;
            waitMilliseconds = static_cast<int>(std::min<std::uint64_t>(remaining, waitMilliseconds));
        }
        if (const auto delay = heldKey.Delay(inputTime())) { waitMilliseconds = static_cast<int>(std::min<std::uint64_t>(*delay,waitMilliseconds)); }
        if (running && SDL_WaitEventTimeout(&event, waitMilliseconds)) {
            physicalEvent(event);
            for (int count = 0; count < 127 && SDL_PollEvent(&event); ++count) { physicalEvent(event); }
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
