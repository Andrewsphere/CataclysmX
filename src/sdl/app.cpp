#include "sdl/app.h"
#include "client/power_view.h"
#include "client/combat_text.h"
#include "client/message_history.h"
#include "client/crafting_menu.h"
#include "client/item_ui.h"
#include "client/item_browser.h"

#include "sdl/assets.h"
#include "sdl/gpu_renderer.h"
#include "sdl/input.h"
#include "sdl/renderer.h"
#include "sdl/session.h"
#include "sdl/screens.h"
#include "sdl/modal.h"
#include "sdl/health.h"
#include "sdl/inventory_screen.h"
#include "sdl/multiplayer_screen.h"

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
        InputAction::Drop, InputAction::Interact, InputAction::Grab, InputAction::Attack, InputAction::StyleBalanced,
        InputAction::StyleAggressive, InputAction::StyleDefensive, InputAction::CombatMenu, InputAction::CombatInfo,
        InputAction::FocusAbility, InputAction::LeapAbility, InputAction::SweepAbility,
        InputAction::Crafting, InputAction::Help, InputAction::Quit}) {
        text += BindingHint(keys, InputContext::Gameplay, action);
        text += " ";
        const auto name = ActionName(action);
        text.append(name.data(), name.size()); text += "\n";
    }
    text += "z zoom in (also Examine/Interact)\nZ zoom out (also Examine/Interact)\nInteract: directions open actions; . selects current tile\nExamine: directions move cursor; layer bindings change viewed Y\nExamine: PgUp/PgDn or Ctrl+Up/Down scroll details\n";
    text += "F9 Multiplayer settings: host / join / discover / game speed\n";
    for (auto action : {InputAction::Inventory, InputAction::Use, InputAction::AdvancedInventory, InputAction::Players, InputAction::Save}) {
        text += BindingHint(keys, InputContext::Gameplay, action); text += " ";
        const auto name = ActionName(action); text.append(name.data(), name.size()); text += '\n';
    }
    return text;
}
int SdlFailure() {
    std::fprintf(stderr, "SDL frontend: %s\n", SDL_GetError());
    return 1;
}
const char* ConnectionText(network::ConnectionState state) {
    switch (state) {
    case network::ConnectionState::Connecting: return "Connecting";
    case network::ConnectionState::Joining: return "Joining";
    case network::ConnectionState::Active: return "Guest";
    case network::ConnectionState::Rejected: return "Connection rejected";
    default: return "Disconnected";
    }
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
    if (options.fontSize) {
        auto overrides = store.Overrides(SettingsScope::Global);
        overrides.textSize = *options.fontSize;
        Diagnostics diagnostics;
        if (!store.SetOverrides(SettingsScope::Global, eastl::move(overrides), store.KeyOverrides(SettingsScope::Global), &diagnostics)) {
            std::fprintf(stderr, "Configuration: %s\n", FormatDiagnostics(diagnostics).c_str());
            return 1;
        }
    }
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
    eastl::unique_ptr<SdlText> text(new SdlText(platform.renderer,
        options.fontPath.empty() ? nullptr : options.fontPath.c_str(), static_cast<float>(settings.textSize)));
    if (!text->Ready()) { return SdlFailure(); }
    int appliedTextSize = settings.textSize;
    SdlAssets assets(&content, options.tilesetDirectory, Wake, &wakeup);
    eastl::unique_ptr<MapPreparation> mapWorker;
    MapPacket mapPacket;
    SpritePlaybackController playback;
    MapIdentity mapIdentity{1, 0, 1, 1};
    bool mapValid = false;
    SessionOptions sessionOptions = options.session;
    sessionOptions.settings = settings;
    sessionOptions.scenario = options.scenario;
    sessionOptions.globalSettings = store.Overrides(SettingsScope::Global);
    sessionOptions.worldSettings = store.Overrides(SettingsScope::World);
    sessionOptions.settingsOverridesProvided = true;
    if (options.seedOverride) { sessionOptions.settings.seed = *options.seedOverride; }
    Session session(content, 1, sessionOptions, Wake, &wakeup);
    SessionSnapshot snapshot;
    PlayerInputContext input;
    std::uint64_t inputGeneration = 0;
    std::uint64_t snapshotGeneration = 0;
    bool running = true, dirty = true, started = false;
    using Screen = ModalScreen;
    HeldKey heldKey;
    ModalController modal(heldKey);
    bool initializeScreen = false;
    ScreenFrame frame;
    OptionsUi optionsUi;
    ItemUi itemUi;
    ItemBrowser itemBrowser;
    eastl::optional<std::uint64_t> browserRequest;
    bool itemAdvanced = false, itemUse = false;
    eastl::optional<std::uint64_t> itemRequest;
    eastl::optional<std::uint64_t> modalRequest;
    eastl::optional<std::uint64_t> commandRequest;
    eastl::optional<std::uint64_t> saveRequest;
    auto& cursor = modal.Cursor();
    Vector<String> inspection;
    eastl::optional<ExaminedEnemy> examinedEnemy;
    WorldView inspectionView;
    Vector<MapItemVisual> inspectionItems;
    bool inspectionValid = false;
    eastl::optional<TileCoord> inspectedCursor;
    eastl::optional<std::uint64_t> inspectionId;
    TileCoord requestedCursor{};
    bool inspectionNeeded = false;
    Vector<InteractionChoice> actions;
    Vector<CraftingMenuRow> craftingRows;
    PowerWorkshop powerWorkshop;
    bool powerOpen = false;
    Vector<String> powerLines;
    CombatUi combatUi;
    CombatEventCursor combatEventCursor;
    Vector<String> combatMessages;
    eastl::optional<InputAction> combatHotkey;
    Vector<String> learningLines;
    eastl::optional<Command> craftingSelection;
    eastl::optional<std::uint64_t> craftingRequest;
    String craftingFeedback;
    auto& selectedAction = modal.ActionSelection();
    eastl::optional<SettingsStore> candidateSettings;
    eastl::unique_ptr<SdlText> candidateText;
    const auto prepareText = [&](Diagnostics* diagnostics) {
        candidateText.reset();
        if (candidateSettings->Effective().textSize == appliedTextSize) { return true; }
        candidateText.reset(new SdlText(platform.renderer, options.fontPath.empty() ? nullptr : options.fontPath.c_str(),
            static_cast<float>(candidateSettings->Effective().textSize)));
        if (candidateText->Ready()) { return true; }
        if (diagnostics) { diagnostics->Add(SDL_GetError()); }
        candidateText.reset();
        return false;
    };
    Localization candidateLocalization;
    eastl::optional<std::uint64_t> settingsId;
    bool saveSettings = false, reloadSettings = false, multiplayerSettings = false;
    RegionPlan debugPlan;
    WorldgenDebugView debugImage;
    Vector<String> debugDetails;
    WorldgenDebugLayer debugLayer = WorldgenDebugLayer::Roads;
    int debugScale = 8;
    bool debugAi = false, aiNeeded = false, aiValid = false;
    bool debugPower = false;
    Vector<String> powerDebugLines;
    eastl::optional<std::uint64_t> powerDebugRequest;
    AiDebugView aiView;
    eastl::optional<std::uint64_t> aiRequestId;
    std::uint64_t aiRevision = 0, requestedAiRevision = 0, requestedAiGeneration = 0;
    TileCoord requestedAiCursor{};
    std::uint64_t nextId = 1;
    unsigned int smokeStep = 0;
    bool smokeWaiting = false;
    bool smokeFailed = false;
    bool smokeExamined = false;
    bool smokeExamineDrawn = false, smokeActionsDrawn = false, smokeHealthDrawn = false;
    bool smokeInventoryDrawn = false, smokeItemExamineDrawn = false, smokeAdvancedDrawn = false;
    unsigned smokeItemStep = 0;
    unsigned smokeItemAction = 0;
    std::uint64_t smokeModalTick = 0;
    unsigned int smokeAiStep = 0;
    bool smokeBrowserListDrawn = false, smokeBrowserExamineDrawn = false;
    bool smokeBrowserGrantsVerified = false;
    std::size_t smokeBrowserRockCount = 0;
    std::uint64_t smokeBrowserTick = 0, smokeBrowserRevision = 0;
    std::uint64_t smokeOpportunity = 0;
    MessageHistory messageHistory;
    String message;
    MultiplayerUi multiplayerUi(sessionOptions.hostSettings, sessionOptions.connectionSettings, sessionOptions.discoveryAddress);
    eastl::optional<std::uint64_t> lanRequest;
    bool startupBrowserOpened = false;
    const bool multiplayerSmoke = options.smoke && (sessionOptions.host || sessionOptions.join || sessionOptions.discover);
    const bool discoverySmoke = multiplayerSmoke && sessionOptions.discover && !sessionOptions.host && !sessionOptions.join;
    const bool localSmoke = options.smoke && !multiplayerSmoke;
    enum class NetworkSmokeStep { AwaitSession, Players, PlayersClosed, ExamineCurrent, ExamineOther,
        ExamineClosed, Inventory, InventoryClosed, Multiplayer, PacingApplied, MultiplayerClosed, WaitCompleted, Linger, Done };
    auto multiplayerSmokeStep = NetworkSmokeStep::AwaitSession;
    bool multiplayerRendered = false;
    bool multiplayerCommandCompleted = false;
    eastl::optional<std::uint64_t> multiplayerCommandId;
    bool multiplayerPlayersDrawn = false, multiplayerCurrentDrawn = false, multiplayerOtherDrawn = false;
    bool multiplayerInventoryDrawn = false, discoveryDrawn = false, hostReadyAnnounced = false;
    bool multiplayerSettingsDrawn = false;
    bool hostPeerSeen = false;
    int multiplayerInitialY = 0, multiplayerOtherY = 0;
    const auto SetMessage = [&](String value) {
        message = eastl::move(value);
        messageHistory.SetCapacity(store.Effective().messageHistorySize);
        messageHistory.Add({message.data(), message.size()});
    };
    const auto RecordStorageError = [&](SaveError error) {
        SetMessage("World storage failed (error=" + NumberText(static_cast<int>(error)) + ").");
    };
    message = sessionOptions.join ? "Connecting to LAN session..." : "Starting session...";
    String hints = Hints(keys);
    auto assetState = AssetState::Loading;
    bool minimized = false;
    bool commandPending = false;
    bool reconcileMenu = false;
    bool choosingGrab = false;
    int zoom = 0;
    using Clock = std::chrono::steady_clock;
    Clock::time_point commandSubmittedAt;
    bool pendingStatusVisible = false;
    auto animationEpoch = Clock::now();
    eastl::optional<std::uint64_t> animationDeadline;
    const auto smokeDeadline = Clock::now() + std::chrono::seconds(multiplayerSmoke ? 20 : 10);
    eastl::optional<Clock::time_point> multiplayerLingerUntil;
    const auto enqueue = [&](SessionRequest request) {
        const bool isCommand = request.kind == RequestKind::Command || request.kind == RequestKind::ResumeAndCommand
            || request.kind == RequestKind::ItemCommand;
        if (isCommand && commandPending) { return false; }
        const bool newCharacter = request.command.kind == CommandKind::new_character;
        if (isCommand && (inputGeneration != snapshot.generation || (!input.opportunity && !newCharacter)
            || (snapshot.mode == SessionMode::Guest && snapshot.connectionState != network::ConnectionState::Active))) { return false; }
        request.generation = snapshot.generation; request.id = nextId;
        request.player = snapshot.view.playerId; request.opportunity = input.opportunity;
        if (session.TrySubmit(eastl::move(request)) != EnqueueResult::Accepted) {
            SetMessage("Session busy; try again."); dirty = true; return false;
        }
        if (isCommand) {
            commandPending = true;
            commandRequest = nextId;
            commandSubmittedAt = Clock::now();
        }
        ++nextId; return true;
    };
    const auto submit = [&](RequestKind kind, Command command = {}) {
        SessionRequest request; request.kind = kind; request.command = eastl::move(command);
        const auto id = nextId;
        const bool accepted = enqueue(eastl::move(request));
        if (accepted && (kind == RequestKind::Pause || kind == RequestKind::Resume || kind == RequestKind::PauseForItems)) { modalRequest = id; }
        return accepted;
    };
    const auto close = [&] {
        combatUi.CancelTarget(); combatHotkey.reset();
        itemBrowser.Close();
        heldKey.Reset();
        if (modal.Active() == Screen::Lan) { submit(RequestKind::StopDiscovery); }
        if (modal.Active() == Screen::Lan && !snapshot.menuOpen) {
            modal.Closed(); frame.visible = false; SDL_StopTextInput(platform.window); return;
        }
        if (submit(RequestKind::Resume)) { modal.BeginClose(); message = "Resuming..."; SDL_StopTextInput(platform.window);
            inspectionId.reset(); inspectionNeeded = false;
            ++aiRevision; aiNeeded = false; aiValid = false; }
    };
    const auto refreshAi = [&] {
        cursor.x = std::clamp(cursor.x,-world_coordinate_limit,world_coordinate_limit);
        cursor.z = std::clamp(cursor.z,-world_coordinate_limit,world_coordinate_limit);
        cursor.y = std::clamp(cursor.y,world_min_y,world_max_y);
        ++aiRevision; aiNeeded = true; aiValid = false; frame.detailScroll = 0;
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
    };
    const auto executeAction = [&] {
        if (selectedAction >= actions.size()) { SetMessage("Action changed; choose an action again."); return; }
        const auto& choice = actions[selectedAction];
        if (choice.reason != ObjectUseReason::None) { SetMessage(InteractionReason(localization,choice.reason)); return; }
        Command command; command.kind = CommandKind::interact_action; command.interaction = choice.request;
        if (submit(RequestKind::ResumeAndCommand,eastl::move(command))) {
            modal.Closed(); frame.visible = false; message = "Awaiting admission...";
        }
    };
    const auto openActions = [&] {
        collectActions();
        if (actions.empty()) { return; }
        modal.EnterActions(frame.scroll);
        selectedAction = std::min(selectedAction, actions.size() - 1);
        if (const auto single = SoleAvailableAction(actions)) { selectedAction = *single; executeAction(); }
    };
    const auto displayView = [&]() -> const WorldView& {
        return modal.Active() == Screen::Examine && inspectionValid ? inspectionView : snapshot.view;
    };
    const auto displayItems = [&]() -> const Vector<MapItemVisual>& {
        return modal.Active() == Screen::Examine && inspectionValid ? inspectionItems : snapshot.mapItems;
    };
    const auto displayLayerReady = [&] {
        return modal.Active() != Screen::Examine
            || (inspectionValid && inspectionView.origin.y == cursor.y);
    };
    const auto prepareMap = [&] {
        ++mapIdentity.viewRevision;
        if (mapWorker && displayLayerReady() && displayView().playerId) {
            mapWorker->Submit(displayView(), mapIdentity, displayItems());
        }
    };
    const auto refreshDiscovery = [&] {
        SessionRequest request; request.kind = RequestKind::RefreshDiscovery;
        request.hostSettings = multiplayerUi.Host();
        request.discoveryAddress = multiplayerUi.DiscoveryAddress();
        enqueue(eastl::move(request));
    };
    const auto openLan = [&] {
        SDL_StopTextInput(platform.window);
        multiplayerUi.Open(snapshot);
        frame.scroll = 0; frame.detailScroll = 0;
        heldKey.Reset(); choosingGrab = false;
        if (modal.Active() == Screen::Examine) {
            inspectionId.reset(); inspectionNeeded = false; inspectionValid = false; mapValid = false;
        }
        ++aiRevision; aiNeeded = false; aiValid = false;
        if (snapshot.menuOpen || !started || (snapshot.mode == SessionMode::Guest
            && snapshot.connectionState != network::ConnectionState::Active)) {
            modal.BeginOpen(Screen::Lan); modal.Paused(); initializeScreen = false;
        } else if (submit(RequestKind::Pause)) { modal.BeginOpen(Screen::Lan); }
        prepareMap();
        refreshDiscovery(); dirty = true;
    };
    const auto multiplayerEvent = [&](MultiplayerEvent event) {
        if (event == MultiplayerEvent::None) { return; }
        if (event == MultiplayerEvent::Close) { close(); return; }
        if (event == MultiplayerEvent::Refresh) { refreshDiscovery(); return; }
        if (lanRequest || settingsId) { return; }
        if (event == MultiplayerEvent::ApplyPacing) {
            if (snapshot.mode == SessionMode::Guest) { multiplayerUi.Feedback("Only the host can change pacing."); return; }
            const auto pacing = multiplayerUi.Pacing();
            auto overrides = store.Overrides(SettingsScope::World);
            overrides.ticksPerSecond = pacing.ticksPerSecond;
            overrides.rateLimited = pacing.rateLimited;
            overrides.pauseWhenIdle = pacing.pauseWhenIdle;
            candidateSettings = store;
            Diagnostics diagnostics;
            if (!candidateSettings->SetOverrides(SettingsScope::World, eastl::move(overrides), store.KeyOverrides(SettingsScope::World), &diagnostics)) {
                multiplayerUi.Feedback(FormatDiagnostics(diagnostics)); candidateSettings.reset(); return;
            }
            SessionRequest request; request.kind = RequestKind::ApplySettings;
            request.globalSettings = candidateSettings->Overrides(SettingsScope::Global);
            request.worldSettings = candidateSettings->Overrides(SettingsScope::World);
            const auto id = nextId;
            if (enqueue(eastl::move(request))) {
                settingsId = id; multiplayerSettings = true; saveSettings = true; reloadSettings = false;
                multiplayerUi.Feedback("Applying pacing...");
            } else { candidateSettings.reset(); multiplayerUi.Feedback(message); }
            return;
        }
        SessionRequest request;
        switch (event) {
        case MultiplayerEvent::OpenHost: request.kind = RequestKind::OpenLan; request.hostSettings = multiplayerUi.Host(); break;
        case MultiplayerEvent::CloseHost: request.kind = RequestKind::CloseLan; break;
        case MultiplayerEvent::Join:
            request.kind = RequestKind::Connect; request.connectionSettings = multiplayerUi.Connection();
            request.newCharacter = multiplayerUi.NewCharacter(); break;
        case MultiplayerEvent::Leave: request.kind = RequestKind::Disconnect; break;
        default: return;
        }
        const auto id = nextId;
        if (enqueue(eastl::move(request))) {
            lanRequest = id;
            multiplayerUi.Feedback("Request pending...");
            heldKey.Reset();
        }
    };
    const auto acceptCombat = [&] {
        combatUi.Refresh(content, snapshot.view);
        if (const auto command = combatUi.Accept()) {
            if (submit(RequestKind::ResumeAndCommand, *command)) {
                combatUi.CancelTarget(); combatHotkey.reset();
                modal.Closed(); frame.visible = false; heldKey.Reset();
                message = "Awaiting combat action admission...";
            }
        }
    };
    const auto handleEvent = [&](const SDL_Event& event) {
        if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) { running = false; return; }
        if (event.type == SDL_EVENT_WINDOW_MINIMIZED) { minimized = true; animationDeadline.reset(); }
        if (event.type == SDL_EVENT_WINDOW_RESTORED || event.type == SDL_EVENT_WINDOW_SHOWN) { minimized = false; }
        if (event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST) { dirty = true; }
        if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED || event.type == SDL_EVENT_WINDOW_RESIZED) {
            ++mapIdentity.viewportGeneration;
            mapValid = false;
            prepareMap();
            dirty = true;
        }
        if (modal.Active() == Screen::Debug && itemBrowser.IsOpen()) {
            if (event.type == SDL_EVENT_TEXT_INPUT) {
                if (!modal.Pending()) { itemBrowser.Text(event.text.text); dirty = true; }
                return;
            }
            if (event.type != SDL_EVENT_KEY_DOWN) { return; }
            if (event.key.repeat || modal.Pending()) { return; }
            const auto key = TranslateKey(event.key);
            if (!key) { return; }
            // Default q remains text in editors; only an explicit Debug binding
            // (and the physical F11 shortcut) may leave the entire debug modal.
            if (key->key == "F11" || (!itemBrowser.EditingText()
                && ResolveAction(keys, InputContext::Debug, *key) == InputAction::Debug)) {
                close(); dirty = true; return;
            }
            const auto previous = itemBrowser.Frame().screen;
            const bool editing = itemBrowser.EditingText();
            itemBrowser.Handle(*key, keys, content, localization);
            if (editing != itemBrowser.EditingText()) {
                if (itemBrowser.EditingText()) { SDL_StartTextInput(platform.window); }
                else { SDL_StopTextInput(platform.window); }
            }
            if (previous != itemBrowser.Frame().screen
                || ResolveAction(keys, InputContext::Menu, *key) == InputAction::Accept) { heldKey.Reset(); }
            if (!itemBrowser.IsOpen()) {
                debugAi = false; frame.scroll = 0; frame.detailScroll = 0;
                rebuildDebug(); heldKey.Reset();
            }
            if (!browserRequest) {
                if (const auto grant = itemBrowser.TakeGiveRequest()) {
                    SessionRequest request; request.kind = RequestKind::DebugGiveItems; request.grant = *grant;
                    const auto id = nextId;
                    if (enqueue(eastl::move(request))) { browserRequest = id; }
                    else { itemBrowser.GiveRejected("Session busy; try again."); }
                    heldKey.Reset();
                }
            }
            dirty = true; return; // Every browser chord is modal, including F9.
        }
        if (event.type == SDL_EVENT_TEXT_INPUT && modal.Active() == Screen::Options && !settingsId && !modal.Pending()) {
            optionsUi.Text(event.text.text); dirty = true; return;
        }
        if (event.type == SDL_EVENT_TEXT_INPUT && modal.Active() == Screen::Lan && !settingsId && !lanRequest && !modal.Pending()) {
            multiplayerUi.Text(event.text.text);
            dirty = true; return;
        }
        if (event.type != SDL_EVENT_KEY_DOWN) { return; }
        const auto chord = TranslateKey(event.key);
        if (!chord) { return; }
        if (chord->ctrl && chord->key == "c") {
            running = false;
            return;
        }
        if (chord->key == "F9") {
            if (event.key.repeat) { return; }
            if (chord->ctrl || chord->alt) { return; }
            if (modal.Active() == Screen::Lan && !modal.Pending()) {
                multiplayerEvent(multiplayerUi.Key(*chord, keys, snapshot, settingsId.has_value() || lanRequest.has_value()));
                if (!multiplayerUi.EditingText()) { SDL_StopTextInput(platform.window); }
                heldKey.Reset(); dirty = true;
            }
            else if (!modal.Pending() && !settingsId && !craftingRequest
                && (!commandPending || snapshot.mode != SessionMode::Local)) { openLan(); }
            return;
        }
        if (modal.Active() == Screen::Lan && !modal.Pending()) {
            dirty = true;
            if (event.key.repeat) { return; }
            const bool editing = multiplayerUi.EditingText();
            multiplayerEvent(multiplayerUi.Key(*chord, keys, snapshot, settingsId.has_value() || lanRequest.has_value()));
            if (multiplayerUi.EditingText()) { SDL_StartTextInput(platform.window); }
            else { SDL_StopTextInput(platform.window); }
            const auto navigation = ResolveAction(keys, InputContext::Menu, *chord);
            if (editing != multiplayerUi.EditingText() || (!editing && navigation != InputAction::North
                && navigation != InputAction::South && navigation != InputAction::Next && navigation != InputAction::Previous)) { heldKey.Reset(); }
            return;
        }
        const int zoomChange = MapZoomChange(*chord);
        if (zoomChange != 0 && (modal.Active() == Screen::None || modal.Active() == Screen::Examine || modal.Active() == Screen::Targets || modal.Active() == Screen::Actions) && !modal.Pending()) {
            zoom = std::clamp(zoom + zoomChange, -8, 12);
            dirty = true;
            return;
        }
        const auto context = modal.Active() == Screen::Debug ? InputContext::Debug
            : modal.CapturesInput() ? InputContext::Menu : InputContext::Gameplay;
        auto action = ResolveAction(keys, context, *chord);
        if (modal.Active() == Screen::Examine || modal.Active() == Screen::Targets
            || (modal.Active() == Screen::Combat && combatUi.Targeting())) {
            const auto gameAction = ResolveAction(keys,InputContext::Gameplay,*chord);
            if (gameAction == InputAction::Northwest || gameAction == InputAction::Northeast
                || gameAction == InputAction::Southwest || gameAction == InputAction::Southeast
                || gameAction == InputAction::MoveUp || gameAction == InputAction::MoveDown) { action = gameAction; }
        }
        dirty = true;
        if (modal.Pending() || settingsId || craftingRequest) { return; }
        if (modal.Active() == Screen::ItemInventory) {
            HandleInventoryKey(itemUi, heldKey, *chord, keys, snapshot.view, content, localization);
            return; // All contextual keys, including unbound keys, belong to ItemUi.
        }
        if (modal.Active() == Screen::Options) {
            const bool capturing = optionsUi.Capturing();
            const auto result = optionsUi.Key(*chord,store,localization,localizationDirectory);
            if (result == OptionsEvent::Multiplayer) { openLan(); return; }
            if (capturing) { heldKey.Reset(); }
            if (result == OptionsEvent::Close) { close(); }
            if (optionsUi.EditingText()) { SDL_StartTextInput(platform.window); }
            else { SDL_StopTextInput(platform.window); }
            if (result == OptionsEvent::Save) {
                candidateSettings = store;
                Diagnostics diagnostics;
                if (!optionsUi.Prepare(store,*candidateSettings,&diagnostics)
                    || !candidateLocalization.Load(localizationDirectory,candidateSettings->Effective().language,&diagnostics)
                    || !prepareText(&diagnostics)) {
                    optionsUi.Feedback(FormatDiagnostics(diagnostics)); candidateSettings.reset(); return;
                }
                SessionRequest request; request.kind = RequestKind::ApplySettings;
                request.globalSettings = candidateSettings->Overrides(SettingsScope::Global);
                request.worldSettings = candidateSettings->Overrides(SettingsScope::World);
                const auto id = nextId;
                if (enqueue(eastl::move(request))) { settingsId = id; saveSettings = result == OptionsEvent::Save; reloadSettings = false; multiplayerSettings = false; }
                else { candidateSettings.reset(); candidateText.reset(); optionsUi.Feedback(message); }
            }
            return;
        }
        if (modal.Active() == Screen::Targets && (chord->key == "." || chord->key == "Home")) {
            cursor = snapshot.view.player_position; openActions(); return;
        }
        if ((modal.Active() == Screen::Examine || modal.Active() == Screen::Help || modal.Active() == Screen::Actions || modal.Active() == Screen::Players
            || modal.Active() == Screen::Combat || modal.Active() == Screen::CombatInfo)
            && ((chord->ctrl && (chord->key == "Up" || chord->key == "Down"))
                || chord->key == "PageUp" || chord->key == "PageDown")) {
            const bool down = chord->key == "Down" || chord->key == "PageDown";
            auto& scroll = modal.Active() == Screen::Actions || modal.Active() == Screen::Combat ? frame.detailScroll : frame.scroll;
            scroll = std::max(0,scroll + (down ? 1 : -1)); return;
        }
        if (modal.Active() == Screen::Crafting && chord) {
            if (powerOpen) {
                if (chord->key == "Escape") { powerOpen = false; frame.scroll = 0; return; }
                if (chord->key == "PageDown") { frame.scroll += 5; return; }
                if (chord->key == "PageUp") { frame.scroll = std::max(0, frame.scroll - 5); return; }
                if (const auto command = powerWorkshop.Handle(*chord, snapshot.view, content)) {
                    const auto id = nextId;
                    if (submit(RequestKind::ResumeAndCommand, *command)) craftingRequest = id;
                }
                return;
            }
            if (chord->key == "p") { powerWorkshop.Open(snapshot.view); powerOpen = true; frame.scroll = 0; return; }
        }
        if (!action && modal.Active() == Screen::Debug && chord->key == "p") action = InputAction::Inspect;
        if (!action) { return; }
        if (modal.Active() != Screen::None) {
            if (*action == InputAction::Cancel || (modal.Active() == Screen::Debug && *action == InputAction::Debug)) {
                if (modal.Active() == Screen::Combat && combatUi.Targeting()) {
                    combatUi.CancelTarget(); heldKey.Reset(); frame.detailScroll = 0; return;
                }
                if (modal.Active() == Screen::Actions) { modal.BackToTargets(frame.scroll); return; }
                close(); return;
            }
            if (modal.Active() == Screen::Combat) {
                combatUi.Refresh(content, snapshot.view);
                if (combatUi.Targeting()) {
                    if (*action == InputAction::Accept) { acceptCombat(); }
                    else { combatUi.MoveTarget(*action); }
                } else if (*action == InputAction::North || *action == InputAction::South) {
                    combatUi.MoveSelection(*action == InputAction::North ? -1 : 1); frame.detailScroll = 0;
                } else if (*action == InputAction::Accept && combatUi.BeginChoice(snapshot.view.player_position)) {
                    heldKey.Reset(); frame.detailScroll = 0;
                    if (!combatUi.Targeting()) { acceptCombat(); }
                }
                return;
            }
            if (modal.Active() == Screen::Players || modal.Active() == Screen::CombatInfo) {
                if (*action == InputAction::North || *action == InputAction::South) {
                    frame.scroll = std::max(0, frame.scroll + (*action == InputAction::South ? 1 : -1));
                } else if (*action == InputAction::Accept) { close(); }
                return;
            }
            if (modal.Active() == Screen::Help) { if (*action == InputAction::Accept) { close(); } return; }
            if (modal.Active() == Screen::Crafting) {
                const auto count = craftingRows.size() + learningLines.size();
                if (count && (*action == InputAction::North || *action == InputAction::South)) {
                    selectedAction = *action == InputAction::North ? (selectedAction + count - 1) % count
                        : (selectedAction + 1) % count;
                    if (selectedAction < craftingRows.size()) { craftingSelection = craftingRows[selectedAction].command; }
                    else { craftingSelection.reset(); }
                } else if (*action == InputAction::Accept && craftingSelection) {
                    const auto id = nextId;
                    if (submit(RequestKind::ResumeAndCommand, *craftingSelection)) { craftingRequest = id; }
                }
                return;
            }
            if (modal.Active() == Screen::Debug) {
                if (chord->key == "p") {
                    debugPower = true; debugAi = false; aiNeeded = false; aiValid = false;
                    frame.scroll = 0; frame.detailScroll = 0;
                    if (!powerDebugRequest) {
                        const auto id = nextId;
                        if (submit(RequestKind::InspectPower)) powerDebugRequest = id;
                    }
                    return;
                }
                if (debugPower) {
                    if (*action == InputAction::Inspect) { debugPower = false; rebuildDebug(); }
                    else if (*action == InputAction::South || *action == InputAction::DetailsDown) ++frame.scroll;
                    else if (*action == InputAction::North || *action == InputAction::DetailsUp) frame.scroll = std::max(0, frame.scroll - 1);
                    return;
                }
                if (*action == InputAction::Inventory) {
                    itemBrowser.Open(content, localization);
                    debugAi = false; ++aiRevision; aiNeeded = false; aiValid = false;
                    frame.scroll = 0; frame.detailScroll = 0; frame.debugImage = nullptr;
                    heldKey.Reset(); return;
                }
                if (*action == InputAction::Inspect) {
                    debugAi = !debugAi; ++aiRevision; aiNeeded = false; aiValid = false;
                    frame.detailScroll = 0;
                    if (debugAi) { cursor = snapshot.view.player_position; refreshAi(); }
                    else { rebuildDebug(); }
                    return;
                }
                if (*action == InputAction::DetailsDown || *action == InputAction::DetailsUp) {
                    const auto count = debugAi ? (aiValid ? aiView.details.size() : 0) : debugDetails.size();
                    frame.detailScroll = std::clamp(frame.detailScroll + (*action == InputAction::DetailsDown ? 1 : -1),
                        0,std::max(0,static_cast<int>(count)-1)); return;
                }
                if (debugAi) {
                    switch (*action) {
                    case InputAction::West: --cursor.x; break;
                    case InputAction::East: ++cursor.x; break;
                    case InputAction::North: --cursor.z; break;
                    case InputAction::South: ++cursor.z; break;
                    case InputAction::MoveUp: cursor.y = std::min(world_max_y,cursor.y+1); break;
                    case InputAction::MoveDown: cursor.y = std::max(world_min_y,cursor.y-1); break;
                    case InputAction::Recenter: cursor = snapshot.view.player_position; break;
                    default: return;
                    }
                    refreshAi(); return;
                }
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
                default: return;
                }
                frame.detailScroll = 0; rebuildDebug(); return;
            }
            if (modal.Active() == Screen::Actions) {
                if (actions.empty()) { modal.BackToTargets(frame.scroll); return; }
                if (*action == InputAction::North) { selectedAction = selectedAction >= actions.size() ? actions.size()-1 : (selectedAction+actions.size()-1)%actions.size(); frame.detailScroll = 0; }
                if (*action == InputAction::South) { selectedAction = selectedAction >= actions.size() ? 0 : (selectedAction+1)%actions.size(); frame.detailScroll = 0; }
                if (*action == InputAction::Accept) {
                    executeAction();
                }
                return;
            }
            if (*action == InputAction::Accept && modal.Active() == Screen::Targets) {
                openActions();
                return;
            }
            TileCoord next = cursor;
            switch (*action) {
            case InputAction::Northwest: --next.x; --next.z; break;
            case InputAction::Northeast: ++next.x; --next.z; break;
            case InputAction::Southwest: --next.x; ++next.z; break;
            case InputAction::Southeast: ++next.x; ++next.z; break;
            case InputAction::MoveUp: if (modal.Active() != Screen::Examine) { return; } next.y = std::min(world_max_y,next.y+1); break;
            case InputAction::MoveDown: if (modal.Active() != Screen::Examine) { return; } next.y = std::max(world_min_y,next.y-1); break;
            case InputAction::West: --next.x; break;
            case InputAction::East: ++next.x; break;
            case InputAction::North: --next.z; break;
            case InputAction::South: ++next.z; break;
            default: return;
            }
            if (modal.Active() == Screen::Targets) {
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
            if (modal.Active() == Screen::Examine && next.y != cursor.y) {
                inspectionValid = false; mapValid = false; ++mapIdentity.viewRevision;
            }
            if (next != cursor) { inspectedCursor.reset(); }
            cursor = next;
            if (modal.Active() == Screen::Targets) { openActions(); return; }
            inspectionNeeded = true; frame.scroll = 0;
            return;
        }
        if (choosingGrab) {
            if (*action == InputAction::Cancel || *action == InputAction::Grab) {
                choosingGrab = false; return;
            }
            auto command = GameplayCommand(*action, false);
            if (!command || command->kind != CommandKind::move || command->dy) { return; }
            command->kind = CommandKind::grab;
            if (submit(RequestKind::Command, *command)) { choosingGrab = false; }
            return;
        }
        if (*action == InputAction::Quit || *action == InputAction::Cancel) { running = false; return; }
        if (*action == InputAction::CombatMenu || *action == InputAction::CombatInfo
            || *action == InputAction::FocusAbility || *action == InputAction::LeapAbility || *action == InputAction::SweepAbility) {
            if (!snapshot.view.playerId || commandPending
                || (snapshot.mode == SessionMode::Guest && snapshot.connectionState != network::ConnectionState::Active)) { return; }
            combatUi = {}; combatHotkey.reset();
            if (*action == InputAction::FocusAbility || *action == InputAction::LeapAbility || *action == InputAction::SweepAbility) { combatHotkey = *action; }
            const auto screen = *action == InputAction::CombatInfo ? Screen::CombatInfo : Screen::Combat;
            // Death removes readiness, but the session and its menu still exist.
            if (submit(RequestKind::Pause)) { modal.BeginOpen(screen); }
            heldKey.Reset(); return;
        }
        if (!started || assetState != AssetState::Ready || !mapValid
            || (snapshot.mode == SessionMode::Guest && snapshot.connectionState != network::ConnectionState::Active)) { return; }
        if (*action == InputAction::Save) {
            if (snapshot.mode == SessionMode::Guest) { SetMessage("Only the host can save this world."); }
            else if (!snapshot.hasSave) { SetMessage("Start with --save <path> to save this world."); }
            else {
                const auto id = nextId;
                if (!saveRequest && submit(RequestKind::Save)) { saveRequest = id; }
            }
            return;
        }
        if (*action == InputAction::Inventory || *action == InputAction::Use || *action == InputAction::AdvancedInventory) {
            if (commandPending) { return; }
            if (submit(RequestKind::PauseForItems)) {
                itemAdvanced = *action == InputAction::AdvancedInventory;
                itemUse = *action == InputAction::Use;
                modal.BeginOpen(Screen::ItemInventory);
                message = snapshot.mode == SessionMode::Local ? "Pausing..." : "Opening inventory (world remains live)...";
            }
            return;
        }
        if (*action == InputAction::Grab) {
            heldKey.Reset();
            if (snapshot.view.grabbedObject) {
                Command command; command.kind = CommandKind::release_grab;
                submit(RequestKind::Command, command);
            } else {
                choosingGrab = true;
            }
            return;
        }
        if (*action == InputAction::Help || *action == InputAction::Examine || *action == InputAction::Interact
            || *action == InputAction::Settings || *action == InputAction::Debug || *action == InputAction::Crafting || *action == InputAction::Players) {
            if (*action == InputAction::Debug && snapshot.mode == SessionMode::Guest) { SetMessage("Debug inspection is available to the host only."); return; }
            const auto nextScreen = *action == InputAction::Help ? Screen::Help : *action == InputAction::Examine ? Screen::Examine
                : *action == InputAction::Interact ? Screen::Targets : *action == InputAction::Settings ? Screen::Options
                : *action == InputAction::Crafting ? Screen::Crafting : *action == InputAction::Players ? Screen::Players : Screen::Debug;
            if (nextScreen == Screen::Crafting) { powerOpen = false; craftingSelection.reset(); selectedAction = 0; craftingFeedback.clear(); }
            if (submit(RequestKind::Pause)) { modal.BeginOpen(nextScreen); message = snapshot.mode == SessionMode::Local ? "Pausing..." : "Opening menu (world remains live)..."; }
            return;
        }
        const auto command = GameplayCommand(*action,false);
        if (command) { if (submit(RequestKind::Command,*command)) { message = "Awaiting admission..."; } }
        else { SetMessage("This action is not available here."); }
    };
    const auto inputEpoch = Clock::now();
    const auto inputTime = [&] { return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-inputEpoch).count()); };
    const auto physicalEvent = [&](const SDL_Event& event) {
        if (heldKey.Observe(event,inputTime())) { handleEvent(event); }
    };
    const auto resetGeneration = [&](std::uint64_t generation) {
        const bool wasBrowsing = modal.Active() == Screen::Lan;
        multiplayerUi.ResetConnection();
        snapshot.generation = generation;
        snapshotGeneration = 0;
        snapshot.view = {}; snapshot.mapItems.clear(); snapshot.interactions.clear();
        input = {}; inputGeneration = generation;
        modal.Closed(); initializeScreen = false; frame = {};
        SDL_StopTextInput(platform.window); lanRequest.reset();
        itemUi = {}; itemRequest.reset(); modalRequest.reset(); commandRequest.reset();
        itemBrowser = ItemBrowser{}; browserRequest.reset();
        commandPending = false; choosingGrab = false;
        craftingRows.clear(); learningLines.clear(); craftingSelection.reset(); craftingRequest.reset(); craftingFeedback.clear();
        combatUi = {}; combatHotkey.reset();
        actions.clear(); inspection.clear(); inspectionView = {}; inspectionItems.clear();
        examinedEnemy.reset();
        inspectionValid = false; inspectedCursor.reset(); inspectionId.reset(); inspectionNeeded = false;
        candidateSettings.reset(); candidateText.reset(); settingsId.reset(); saveSettings = false; reloadSettings = false; multiplayerSettings = false;
        saveRequest.reset(); reconcileMenu = true;
        ++aiRevision; aiRequestId.reset(); aiNeeded = false; aiValid = false; aiView = {};
        debugPower = false; powerDebugRequest.reset(); powerDebugLines.clear();
        debugAi = false; debugPlan = {}; debugImage = {}; debugDetails.clear();
        mapIdentity.session = generation; ++mapIdentity.viewRevision;
        mapPacket = {}; mapValid = false; playback = {}; animationDeadline.reset();
        multiplayerRendered = false;
        if (wasBrowsing) { submit(RequestKind::StopDiscovery); }
        dirty = true;
    };
    const auto consumeInput = [&](std::uint64_t generation, const PlayerInputContext& context) {
        if (generation != snapshot.generation) { return; }
        if (inputGeneration != generation || context.revision > input.revision
            || (!input.opportunity && context.opportunity)) {
            inputGeneration = generation; input = context;
        }
    };
    const auto consumeSnapshot = [&] {
        SessionSnapshot incoming;
        if (!session.TakeSnapshot(incoming) || incoming.generation < snapshot.generation) { return; }
        if (incoming.generation != snapshot.generation) { resetGeneration(incoming.generation); }
        const bool menuWasOpen = snapshot.menuOpen;
        const auto statusVersion = snapshot.statusVersion;
        const auto previousStorageError = snapshot.storageError;
        const bool storageErrorWasFromReply = snapshot.storageErrorFromReply;
        const bool viewChanged = incoming.view.revision != snapshot.view.revision;
        const auto previousY = snapshot.view.origin.y;
        const auto previousCharacter = snapshot.view.characterId;
        snapshot = eastl::move(incoming);
        if (previousCharacter && snapshot.view.characterId != previousCharacter) {
            combatUi = {}; combatHotkey.reset(); choosingGrab = false;
            itemUi = {}; itemRequest.reset(); commandRequest.reset(); commandPending = false;
            craftingSelection.reset(); craftingRequest.reset(); browserRequest.reset(); itemBrowser.Close();
            modalRequest.reset(); modal.Closed(); frame = {}; initializeScreen = false;
            inspectionId.reset(); inspectionNeeded = false; inspectionValid = false;
            inspection.clear(); inspectionView = {}; inspectionItems.clear(); inspectedCursor.reset(); actions.clear();
            examinedEnemy.reset();
            SDL_StopTextInput(platform.window);
            heldKey.Reset(); reconcileMenu = true;
        }
        combatEventCursor.Drain(content, snapshot.view, snapshot.generation, combatMessages);
        for (auto& combatMessage : combatMessages) { SetMessage(eastl::move(combatMessage)); }
        multiplayerUi.Refresh(snapshot);
        snapshotGeneration = snapshot.generation;
        started |= snapshot.view.playerId != 0;
        consumeInput(snapshot.generation, snapshot.input);
        if (reconcileMenu) {
            reconcileMenu = false;
            // Returning from a guest endpoint may restore the local owner's old menu.
            // The epoch reset discarded that screen, so release its input ownership.
            if (snapshot.menuOpen && modal.Active() == Screen::None && !modal.Pending()) {
                if (submit(RequestKind::Resume)) { modal.BeginClose(); }
                else { reconcileMenu = true; }
            }
        }
        if (!menuWasOpen && snapshot.menuOpen && modal.Active() == Screen::ItemInventory) { heldKey.Reset(); }
        dirty = true;
        mapIdentity.session = snapshot.generation;
        playback.SetGeneration(mapIdentity.session, mapIdentity.assetGeneration);
        if (modal.Active() == Screen::Examine) {
            // Coalesce live updates behind the in-flight query without invalidating it.
            inspectionNeeded |= viewChanged;
        } else {
            if (mapValid && snapshot.view.origin.y != previousY) { mapValid = false; }
            prepareMap();
        }
        if (modal.Active() == Screen::Debug && debugAi && viewChanged) { aiNeeded = true; }
        if (modal.Active() == Screen::Actions) {
            eastl::optional<InteractionRequest> selected;
            if (selectedAction < actions.size()) { selected = actions[selectedAction].request; }
            collectActions(); selectedAction = actions.size();
            if (selected) {
                for (std::size_t i = 0; i < actions.size(); ++i) {
                    if (actions[i].request == *selected) { selectedAction = i; break; }
                }
            }
        }
        if (snapshot.statusVersion != statusVersion && !snapshot.status.empty()) {
            if (snapshot.statusForHistory) { SetMessage(snapshot.status); }
            else { message = snapshot.status; }
        }
        if (snapshot.storageError && !snapshot.storageErrorFromReply
            && (snapshot.storageError != previousStorageError || storageErrorWasFromReply)) {
            RecordStorageError(*snapshot.storageError);
        }
    };
    const auto smokePress = [&](InputContext context, InputAction action) {
        for (const auto& binding : keys) {
            if (binding.context != context || binding.action != action) { continue; }
            for (const auto& chord : binding.keys) {
                SDL_Event event{}; event.type = SDL_EVENT_KEY_DOWN;
                event.key.key = chord.key.size() == 1 ? static_cast<SDL_Keycode>(chord.key[0])
                    : chord.key == "Enter" ? SDLK_RETURN : SDL_GetKeyFromName(chord.key.c_str());
                event.key.mod = static_cast<SDL_Keymod>((chord.ctrl ? SDL_KMOD_CTRL : 0)
                    | (chord.alt ? SDL_KMOD_ALT : 0) | (chord.shift ? SDL_KMOD_SHIFT : 0));
                const auto translated = TranslateKey(event.key);
                if (!translated || ResolveAction(keys, context, *translated) != action) { continue; }
                handleEvent(event); return;
            }
        }
        SetMessage("SDL multiplayer smoke requires a usable binding: " + String(ActionName(action).data(), ActionName(action).size()));
        smokeFailed = true;
    };
    while (running) {
        wakeup.pending.store(false);
        consumeSnapshot();
        SessionReply reply;
        while (session.TryPopReply(reply)) {
            if (reply.generation < snapshot.generation) { continue; }
            if (reply.generation > snapshot.generation) { resetGeneration(reply.generation); }
            consumeInput(reply.generation, reply.input);
            if (powerDebugRequest && reply.id == *powerDebugRequest) {
                if (reply.kind == ReplyKind::PowerInspected && modal.Active() == Screen::Debug && debugPower) {
                    DescribePowerDebug(reply.powerDebug, snapshot.view.player_position, powerDebugLines);
                }
                powerDebugRequest.reset(); dirty = true; continue;
            }
            if (browserRequest && reply.id == *browserRequest) {
                if (reply.kind == ReplyKind::ItemsGiven) { itemBrowser.GiveFinished(reply.debugGive); }
                else { itemBrowser.GiveRejected(reply.kind == ReplyKind::Stale ? "Request expired." : "Request cancelled."); }
                browserRequest.reset(); heldKey.Reset(); dirty = true;
                continue;
            }
            if (reply.kind == ReplyKind::Stale || reply.kind == ReplyKind::Cancelled || reply.kind == ReplyKind::ControlFailed) {
                if (inspectionId && reply.id == *inspectionId) {
                    if (modal.Active() == Screen::Examine && cursor == requestedCursor) {
                        inspection = reply.details;
                        examinedEnemy.reset();
                        if (reply.networkError) { inspection.push_back(network::NetworkErrorMessage(*reply.networkError)); }
                        if (inspection.empty()) { inspection.push_back("Inspection unavailable"); }
                        inspectedCursor = cursor; inspectionValid = false; mapValid = false; ++mapIdentity.viewRevision;
                        message = inspection.front();
                    }
                    inspectionId.reset();
                }
                if (lanRequest && reply.id == *lanRequest && reply.kind != ReplyKind::ControlFailed) {
                    lanRequest.reset(); multiplayerUi.Feedback("Request cancelled; try again.");
                }
                if (multiplayerCommandId && reply.id == *multiplayerCommandId) { smokeFailed = true; }
            }
            if (aiRequestId && reply.id == *aiRequestId) {
                if (reply.kind == ReplyKind::AiInspected && reply.generation == requestedAiGeneration
                    && reply.generation == snapshot.generation && modal.Active() == Screen::Debug && debugAi
                    && snapshot.menuOpen && !modal.Pending() && requestedAiRevision == aiRevision
                    && requestedAiCursor == cursor && reply.aiDebug.cursor == cursor) {
                    aiView = eastl::move(reply.aiDebug); aiValid = true; dirty = true;
                }
                aiRequestId.reset();
                continue;
            }
            dirty = true;
            switch (reply.kind) {
            case ReplyKind::Started:
                started = true; message = "Session ready.";
                if (!sessionOptions.savePath.empty()) {
                    Diagnostics diagnostics;
                    if (!store.Save(SettingsScope::World, &diagnostics)) {
                        std::fprintf(stderr, "World configuration could not be saved: %s\n", FormatDiagnostics(diagnostics).c_str());
                        return 1;
                    }
                }
                break;
            case ReplyKind::StartupFailed:
                std::fprintf(stderr,"Session startup failed (%d).\n",static_cast<int>(reply.startupError));
                for (const auto& detail : reply.details) { std::fprintf(stderr,"%s\n",detail.c_str()); }
                if (reply.networkError) { std::fprintf(stderr,"%s\n",network::NetworkErrorMessage(*reply.networkError)); }
                if (reply.profileError) { std::fprintf(stderr,"LAN profile error (%d).\n",static_cast<int>(*reply.profileError)); }
                if (reply.storageError) { std::fprintf(stderr,"World storage error (%d).\n",static_cast<int>(*reply.storageError)); }
                return 1;
            case ReplyKind::ControlCompleted:
            case ReplyKind::ControlFailed:
                if (lanRequest && reply.id == *lanRequest) {
                    String feedback = reply.kind == ReplyKind::ControlCompleted ? "Request completed." : "Request failed.";
                    if (reply.networkError) { feedback = network::NetworkErrorMessage(*reply.networkError); }
                    else if (!reply.details.empty()) { feedback = reply.details.front(); }
                    else if (reply.profileError) { feedback = "LAN profile could not be read or saved."; }
                    else if (reply.storageError) { feedback = "World checkpoint failed."; }
                    multiplayerUi.Feedback(eastl::move(feedback));
                    lanRequest.reset();
                }
                if (saveRequest && reply.id == *saveRequest) {
                    saveRequest.reset();
                    if (reply.kind == ReplyKind::ControlCompleted) { SetMessage("World saved."); }
                    else if (!reply.storageError && reply.details.empty()) { SetMessage("World save failed."); }
                }
                if (reply.storageError) {
                    String feedback = "World save failed (error=" + NumberText(static_cast<int>(*reply.storageError)) + ").";
                    for (const auto& detail : reply.details) {
                        if (!detail.empty()) { feedback += ' '; feedback += detail; }
                    }
                    SetMessage(eastl::move(feedback));
                } else {
                    for (const auto& detail : reply.details) { SetMessage(detail); }
                }
                if (reply.networkError) { SetMessage(network::NetworkErrorMessage(*reply.networkError)); }
                if (reply.profileError) { SetMessage("LAN profile could not be read or saved (" + NumberText(static_cast<int>(*reply.profileError)) + ")."); }
                break;
            case ReplyKind::Admission: message = localization.ResultText(reply.result); break;
            case ReplyKind::Completed: {
                if (multiplayerCommandId && reply.id == *multiplayerCommandId) {
                    multiplayerCommandCompleted = reply.result.outcome == Outcome::waited && !reply.result.storageError;
                    smokeFailed |= !multiplayerCommandCompleted;
                }
                if (commandRequest && reply.id == *commandRequest) { commandPending = false; commandRequest.reset(); }
                if (itemRequest && reply.id == *itemRequest) {
                    itemRequest.reset(); itemUi.CommandFinished(reply.result); heldKey.Reset();
                }
                auto feedback = localization.ResultText(reply.result);
                if (ShouldRecordOutcome(reply.result.outcome)) { SetMessage(eastl::move(feedback)); }
                else { message = eastl::move(feedback); }
                if (craftingRequest && reply.id == *craftingRequest) {
                    craftingRequest.reset();
                    craftingFeedback = message;
                    if (reply.result.storageError) { craftingFeedback += " (world storage failed)"; }
                    const auto outcome = reply.result.outcome;
                    if (outcome == Outcome::craft_started || outcome == Outcome::recycling_started
                        || outcome == Outcome::craft_resumed || outcome == Outcome::craft_paused || outcome == Outcome::craft_cancelled
                        || outcome == Outcome::study_started || outcome == Outcome::study_paused
                        || outcome == Outcome::machine_changed || outcome == Outcome::information_changed) {
                        modal.Closed(); frame.visible = false;
                    } else if (submit(RequestKind::Pause)) { modal.BeginOpen(Screen::Crafting); }
                }
                if (reply.result.storageError) { RecordStorageError(*reply.result.storageError); }
                if (localSmoke) {
                    const Outcome expected = smokeStep == 1 || smokeStep == 14 ? Outcome::moved
                        : smokeStep == 2 ? Outcome::picked_up : smokeStep == 13 ? Outcome::toggled
                        : smokeStep == 17 ? Outcome::locked
                        : smokeStep == 20 && smokeItemAction == 1 ? Outcome::wielded
                        : smokeStep == 20 && smokeItemAction == 2 ? Outcome::unwielded : Outcome::waited;
                    if (reply.result.outcome != expected || reply.result.storageError) {
                        std::fprintf(stderr,"SDL smoke step %u: outcome %d, expected %d\n",smokeStep,static_cast<int>(reply.result.outcome),static_cast<int>(expected));
                        smokeFailed = true;
                    }
                }
                smokeWaiting = false; break;
            }
            case ReplyKind::MenuOpened:
            case ReplyKind::Paused:
                if (!modalRequest || reply.id != *modalRequest) { break; }
                modalRequest.reset();
                modal.Paused(); initializeScreen = true;
                message = reply.kind == ReplyKind::Paused ? "Local session paused." : "Menu open; world remains live.";
                smokeWaiting = false; break;
            case ReplyKind::Resumed:
                if (!modalRequest || reply.id != *modalRequest) { break; }
                modalRequest.reset();
                modal.Closed(); frame.visible = false; inspectionValid = false; mapValid = false; prepareMap();
                message = snapshot.mode == SessionMode::Local ? "Local session resumed." : "Menu closed.";
                smokeWaiting = false; break;
            case ReplyKind::Inspected:
                if (inspectionId && *inspectionId == reply.id) {
                    if (modal.Active() == Screen::Examine && cursor == requestedCursor) {
                        inspection = eastl::move(reply.details);
                        examinedEnemy.reset();
                        inspectedCursor = cursor;
                        if (!reply.networkError && reply.inspectionView.playerId && reply.inspectionView.origin.y == cursor.y) {
                            examinedEnemy = eastl::move(reply.examinedEnemy);
                            if (!displayLayerReady() || displayView().origin.y != cursor.y) { mapValid = false; }
                            inspectionView = eastl::move(reply.inspectionView);
                            inspectionItems = eastl::move(reply.inspectionItems);
                            inspectionValid = true; prepareMap();
                        } else {
                            if (reply.inspectionView.playerId && reply.inspectionView.origin.y != cursor.y) {
                                inspection.clear(); inspection.push_back("Inspection unavailable for selected layer");
                            }
                            if (reply.networkError) { inspection.push_back(network::NetworkErrorMessage(*reply.networkError)); }
                            if (inspection.empty()) { inspection.push_back("Inspection unavailable"); }
                            inspectionValid = false; mapValid = false; ++mapIdentity.viewRevision;
                            message = inspection.front();
                        }
                    }
                    inspectionId.reset();
                }
                break;
            case ReplyKind::AiInspected: // Obsolete capture from a previous selection.
            case ReplyKind::PowerInspected:
            case ReplyKind::ItemsGiven: break;
            case ReplyKind::SettingsApplied:
                if (settingsId && *settingsId == reply.id && candidateSettings) {
                    store = eastl::move(*candidateSettings);
                    if (candidateText) {
                        text = eastl::move(candidateText);
                        appliedTextSize = store.Effective().textSize;
                        frame.scroll = 0; frame.detailScroll = 0;
                    }
                    if (!multiplayerSettings) { localization = eastl::move(candidateLocalization); }
                    candidateSettings.reset(); settingsId.reset(); hints = Hints(store.Keys());
                    if (multiplayerSettings) { multiplayerUi.Applied(reply.runtimeSettings.pacing); }
                    else { optionsUi.Applied(store,reply.runtimeSettings,reloadSettings); }
                    smokeWaiting = false;
                    Diagnostics diagnostics;
                    const auto scope = multiplayerSettings ? SettingsScope::World : optionsUi.Scope();
                    String feedback;
                    if (saveSettings && scope == SettingsScope::World && store.WorldDirectory().empty()) {
                        feedback = "Applied for this session; temporary world has no save directory.";
                    } else if (saveSettings && !store.Save(scope,&diagnostics)) {
                        feedback = "Applied, unsaved: " + FormatDiagnostics(diagnostics);
                    }
                    else { feedback = saveSettings ? "Applied and saved" : "Applied"; }
                    if (multiplayerSettings) { multiplayerUi.Feedback(eastl::move(feedback)); }
                    else { optionsUi.Feedback(eastl::move(feedback)); }
                    multiplayerSettings = false;
                }
                break;
            case ReplyKind::SettingsFailed:
                if (settingsId && *settingsId == reply.id) {
                    smokeFailed |= multiplayerSettings && options.smoke;
                    settingsId.reset(); candidateSettings.reset(); candidateText.reset(); smokeFailed |= localSmoke;
                    const auto feedback = reply.details.empty() ? String("Settings could not be applied") : reply.details.front();
                    if (multiplayerSettings) { multiplayerUi.Feedback(feedback); }
                    else { optionsUi.Feedback(feedback); }
                    multiplayerSettings = false;
                }
                break;
            case ReplyKind::Stale:
                if (modalRequest && reply.id == *modalRequest) { modalRequest.reset(); modal.Closed(); }
                if (commandRequest && reply.id == *commandRequest) { commandPending = false; commandRequest.reset(); }
                if (itemRequest && reply.id == *itemRequest) { itemRequest.reset(); itemUi.CommandRejected(); heldKey.Reset(); }
                SetMessage("Input expired; use the current opportunity.");
                if (craftingRequest && reply.id == *craftingRequest) {
                    craftingRequest.reset(); craftingFeedback = message;
                    if (submit(RequestKind::Pause)) { modal.BeginOpen(Screen::Crafting); }
                }
                smokeFailed |= localSmoke; smokeWaiting = false; break;
            case ReplyKind::Cancelled:
                if (modalRequest && reply.id == *modalRequest) { modalRequest.reset(); modal.Closed(); }
                if (commandRequest && reply.id == *commandRequest) { commandPending = false; commandRequest.reset(); }
                if (itemRequest && reply.id == *itemRequest) { itemRequest.reset(); itemUi.CommandRejected(); heldKey.Reset(); }
                SetMessage("Input cancelled.");
                if (craftingRequest && reply.id == *craftingRequest) {
                    craftingRequest.reset(); craftingFeedback = message;
                    if (submit(RequestKind::Pause)) { modal.BeginOpen(Screen::Crafting); }
                }
                smokeFailed |= localSmoke; smokeWaiting = false; break;
            }
        }
        consumeSnapshot();
        if (initializeScreen && snapshot.menuOpen) {
            initializeScreen = false; cursor = snapshot.view.player_position;
            if (localSmoke) { smokeModalTick = snapshot.view.tick; }
            frame.scroll = 0; frame.detailScroll = 0; inspection.clear(); inspectedCursor.reset();
            examinedEnemy.reset();
            if (modal.Active() == Screen::Combat) {
                combatUi.Refresh(content, snapshot.view);
                if (combatHotkey) {
                    const auto hotkey = *combatHotkey; combatHotkey.reset();
                    if (combatUi.SelectHotkey(hotkey, content) && combatUi.BeginChoice(snapshot.view.player_position)
                        && !combatUi.Targeting()) { acceptCombat(); }
                }
            }
            if (modal.Active() == Screen::ItemInventory) { itemUi.Open(itemAdvanced, itemUse); }
            if (modal.Active() == Screen::Options) {
                optionsUi.Open(store,snapshot.runtimeSettings,snapshot.mode == SessionMode::Guest);
                if (snapshot.mode != SessionMode::Guest && !store.Dirty(SettingsScope::Global) && !store.Dirty(SettingsScope::World)) {
                    candidateSettings = store;
                    Diagnostics diagnostics;
                    auto loaded = candidateSettings->Load(&diagnostics);
                    if (loaded && store.WorldDirectory().empty()) {
                        loaded = candidateSettings->SetOverrides(SettingsScope::World,store.Overrides(SettingsScope::World),store.KeyOverrides(SettingsScope::World),&diagnostics);
                    }
                    if (loaded && candidateLocalization.Load(localizationDirectory,candidateSettings->Effective().language,&diagnostics)
                        && prepareText(&diagnostics)) {
                        SessionRequest request; request.kind = RequestKind::ApplySettings;
                        request.globalSettings = candidateSettings->Overrides(SettingsScope::Global);
                        request.worldSettings = candidateSettings->Overrides(SettingsScope::World);
                        const auto id = nextId;
                        if (enqueue(eastl::move(request))) { settingsId = id; saveSettings = false; reloadSettings = true; multiplayerSettings = false; }
                        else { candidateSettings.reset(); candidateText.reset(); optionsUi.Feedback(message); }
                    } else { candidateSettings.reset(); candidateText.reset(); optionsUi.Feedback(FormatDiagnostics(diagnostics)); }
                }
            }
            if (modal.Active() == Screen::Examine) { inspectionNeeded = true; }
            if (modal.Active() == Screen::Debug) {
                itemBrowser.Close();
                debugPower = false; powerDebugLines.clear();
                debugAi = false; ++aiRevision; aiNeeded = false; aiValid = false; rebuildDebug();
            }
            if (modal.Active() == Screen::Targets && !snapshot.interactions.empty()) {
                cursor = snapshot.interactions.front().request.position;
                bool single = true;
                for (const auto& choice : snapshot.interactions) { single &= choice.request.position == cursor; }
                if (single) { openActions(); }
            }
        }
        if (modal.Active() == Screen::ItemInventory && !modal.Pending()) {
            const auto previous = itemUi.Frame().screen;
            itemUi.Refresh(snapshot.view, content, localization, keys);
            const auto itemScreen = itemUi.Frame().screen;
            if (itemScreen == ItemUiScreen::Inventory || itemScreen == ItemUiScreen::Advanced
                || itemScreen == ItemUiScreen::Examine || itemScreen == ItemUiScreen::Actions) {
                for (auto equipmentAction : {InputAction::HoldLeft, InputAction::HoldRight, InputAction::TwoHanded,
                    InputAction::Regrip, InputAction::Wear, InputAction::RemoveWorn, InputAction::ReleaseHand}) {
                    itemUi.Frame().footer += " | " + BindingHint(keys, InputContext::Items, equipmentAction) + " ";
                    const auto name = ActionName(equipmentAction);
                    itemUi.Frame().footer.append(name.data(), name.size());
                }
            }
            if (previous != itemUi.Frame().screen) { heldKey.Reset(); dirty = true; }
            const bool itemsReady = snapshot.menuOpen && (snapshot.mode == SessionMode::Local ? snapshot.paused
                : (snapshot.mode == SessionMode::Host || snapshot.connectionState == network::ConnectionState::Active)
                    && !snapshot.view.remainingTicks && !snapshot.view.commandQueued);
            if (!itemRequest && !commandPending && itemsReady && input.opportunity) {
                if (const auto command = itemUi.TakeCommand(snapshot.view)) {
                    const auto id = nextId;
                    if (submit(RequestKind::ItemCommand, *command)) { itemRequest = id; }
                    else { itemUi.CommandRejected(); }
                    heldKey.Reset(); dirty = true;
                } else if (!itemUi.IsOpen() && !itemUi.NeedsTime()) { close(); }
            }
        }
        if (modal.Active() == Screen::Examine && inspectionNeeded && !inspectionId && snapshot.menuOpen) {
            SessionRequest request; request.kind = RequestKind::Inspect; request.cursor = cursor;
            const auto id = nextId;
            if (enqueue(eastl::move(request))) { inspectionId = id; requestedCursor = cursor; inspectionNeeded = false; }
        }
        if (modal.Active() == Screen::Debug && debugAi && aiNeeded && !aiRequestId && snapshot.menuOpen && !modal.Pending()) {
            SessionRequest request; request.kind = RequestKind::InspectAi; request.cursor = cursor;
            const auto id = nextId;
            if (enqueue(eastl::move(request))) {
                aiRequestId = id; requestedAiCursor = cursor; requestedAiGeneration = snapshot.generation;
                requestedAiRevision = aiRevision; aiNeeded = false;
            }
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
            prepareMap();
        }
        MapPacket completedMap;
        if (mapWorker && mapWorker->Take(completedMap)
            && displayLayerReady()
            && completedMap.identity.session == mapIdentity.session
            && completedMap.identity.assetGeneration == mapIdentity.assetGeneration
            && completedMap.identity.viewportGeneration == mapIdentity.viewportGeneration
            && (!mapValid ? completedMap.identity.viewRevision == mapIdentity.viewRevision : completedMap.identity.viewRevision >= mapPacket.identity.viewRevision)) {
            eastl::swap(mapPacket, completedMap); mapValid = true; dirty = true;
        }
        if (sessionOptions.discover && !startupBrowserOpened && started && snapshotGeneration
            && assetState == AssetState::Ready && !modal.Pending()) {
            startupBrowserOpened = true; openLan();
        }
        if (multiplayerSmoke && snapshotGeneration && sessionOptions.host && snapshot.mode == SessionMode::Host) {
            if (!hostReadyAnnounced) {
                std::fprintf(stdout,"SDL LAN host ready port=%u\n",static_cast<unsigned>(snapshot.hostPort));
                std::fflush(stdout); hostReadyAnnounced = true;
            }
            for (const auto& player : snapshot.view.players) {
                if (player.id != snapshot.view.playerId && player.state == PlayerConnectionState::Active) { hostPeerSeen = true; }
            }
        }
        if (multiplayerSmoke && snapshotGeneration
            && ((sessionOptions.join && snapshot.mode != SessionMode::Guest)
                || (sessionOptions.host && snapshot.mode != SessionMode::Host))) {
            std::fprintf(stderr,"SDL multiplayer smoke lost requested %s session: %s\n",
                sessionOptions.join ? "guest" : "host",snapshot.status.c_str());
            return 1;
        }
        if (smokeFailed) {
            std::fprintf(stderr,"SDL %s smoke step %u (F11 substep %u): %s\n",multiplayerSmoke ? "multiplayer" : "local",
                multiplayerSmoke ? static_cast<unsigned>(multiplayerSmokeStep) : smokeStep,smokeAiStep,message.c_str()); return 1;
        }
        if (localSmoke && snapshot.paused && (modal.Active() == Screen::Examine || modal.Active() == Screen::Targets || modal.Active() == Screen::Actions)
            && snapshot.view.tick != smokeModalTick) { std::fputs("SDL modal advanced simulation.\n", stderr); return 1; }
        if (localSmoke && options.scenario != NewGameScenario::Demo
            && started && snapshot.view.playerId && assetState == AssetState::Ready && mapValid && smokeHealthDrawn) {
            running = false;
        }
        if (localSmoke && options.scenario == NewGameScenario::Demo && started && snapshot.view.playerId && assetState == AssetState::Ready && mapValid && !smokeWaiting && !settingsId
            && (smokeStep > 3 || snapshot.view.opportunity > smokeOpportunity)) {
            const auto press = [&](SDL_Keycode key, SDL_Keymod modifiers = SDL_KMOD_NONE) {
                SDL_Event event{}; event.type = SDL_EVENT_KEY_DOWN; event.key.key = key; event.key.mod = modifiers;
                handleEvent(event);
            };
            smokeOpportunity = snapshot.view.opportunity;
            if (smokeStep == 0) {
                const auto historySize = messageHistory.Entries().size();
                smokePress(InputContext::Gameplay, InputAction::Grab);
                if (!choosingGrab || messageHistory.Entries().size() != historySize) { smokeFailed = true; }
                smokePress(InputContext::Gameplay, InputAction::Cancel);
                if (choosingGrab || messageHistory.Entries().size() != historySize) { smokeFailed = true; }
                smokePress(InputContext::Gameplay, InputAction::East);
                smokeWaiting = commandPending;
            }
            else if (smokeStep == 1) { smokeWaiting = submit(RequestKind::Command, *GameplayCommand(InputAction::PickUp, false)); }
            else if (smokeStep == 2) {
                smokePress(InputContext::Gameplay, InputAction::Wait);
                smokeWaiting = commandPending;
            }
            else if (smokeStep == 3) {
                const auto moved = localization.OutcomeText(Outcome::moved);
                const auto waited = localization.OutcomeText(Outcome::waited);
                const auto queued = localization.OutcomeText(Outcome::queued);
                const auto pickedUp = localization.OutcomeText(Outcome::picked_up);
                bool sawPickup = false;
                for (const auto& entry : messageHistory.Entries()) {
                    if (entry.text == moved || entry.text == waited || entry.text == queued
                        || entry.text == "Awaiting admission...") { smokeFailed = true; }
                    if (entry.text == pickedUp) { sawPickup = true; }
                }
                if (!sawPickup) { smokeFailed = true; }
                smokeWaiting = submit(RequestKind::Pause);
            }
            else if (smokeStep == 4) { smokeWaiting = submit(RequestKind::Resume); }
            else if (smokeStep == 5) { press(SDLK_X); smokeWaiting = modal.Pending(); }
            else if (smokeStep == 6) {
                if (!inspectionId && !inspectionNeeded && !inspection.empty()) {
                    if (!smokeExamined) {
                        const auto before = cursor;
                        const auto requestBefore = nextId;
                        const auto previousInspection = inspection;
                        press(SDLK_F7);
                        if (nextId != requestBefore || commandPending || modal.Active() != Screen::Examine) { smokeFailed = true; }
                        press(SDLK_Z); press(SDLK_RIGHT,SDL_KMOD_SHIFT); press(SDLK_UP,SDL_KMOD_ALT);
                        if (!inspectionNeeded || inspection != previousInspection) { smokeFailed = true; }
                        smokeExamined = true;
                        if (zoom != 1 || cursor != TileCoord{before.x+1,before.z-1,before.y+1}) { smokeFailed = true; }
                    } else {
                        if (!inspectionValid || inspectionView.origin.y != cursor.y) { smokeFailed = true; }
                        press(SDLK_ESCAPE); smokeWaiting = modal.Pending();
                    }
                }
            }
            else if (smokeStep == 7) { press(SDLK_F11); smokeWaiting = modal.Pending(); }
            else if (smokeStep == 8) {
                const auto textInput = [&](const char* value) {
                    SDL_Event entry{}; entry.type = SDL_EVENT_TEXT_INPUT; entry.text.text = value;
                    handleEvent(entry);
                };
                const auto rock = content.FindItem("rock");
                const auto rockCount = [&] {
                    std::size_t count = 0;
                    for (const auto& item : snapshot.view.inventory) { if (item.type == rock) { ++count; } }
                    return count;
                };
                const auto requireSmoke = [&](bool condition, const char* reason) {
                    if (!condition) { SetMessage(reason); smokeFailed = true; }
                    return condition;
                };
                switch (smokeAiStep) {
                case 0:
                    press(SDLK_A); ++smokeAiStep; break;
                case 1: {
                    if (!aiValid || aiRequestId || aiNeeded) { break; }
                    if (!requireSmoke(debugAi && cursor == snapshot.view.player_position, "Smoke AI entry cursor mismatch.")) { break; }
                    const auto before = cursor;
                    press(SDLK_RIGHT); press(SDLK_UP,SDL_KMOD_ALT);
                    requireSmoke(cursor == TileCoord{before.x+1,before.z,std::min(world_max_y,before.y+1)} && !aiValid,
                        "Smoke AI movement did not invalidate capture.");
                    ++smokeAiStep; break;
                }
                case 2:
                    if (!aiValid || aiRequestId || aiNeeded) { break; }
                    if (!requireSmoke(aiView.cursor == cursor && aiView.tick == snapshot.view.tick, "Smoke AI capture mismatch.")) { break; }
                    press(SDLK_A);
                    requireSmoke(!debugAi && !aiValid, "Smoke AI exit retained a live capture.");
                    ++smokeAiStep; break;
                case 3:
                    press(SDLK_I);
                    requireSmoke(itemBrowser.Frame().screen == ItemBrowserScreen::List && !modal.Pending(),
                        "Smoke i did not open the debug item browser.");
                    ++smokeAiStep; break;
                case 4: {
                    if (!smokeBrowserListDrawn) { break; }
                    const auto requestBefore = nextId;
                    press(SDLK_SLASH);
                    if (!requireSmoke(itemBrowser.Frame().screen == ItemBrowserScreen::Search && itemBrowser.Frame().input.empty()
                        && SDL_TextInputActive(platform.window),
                        "Smoke search did not start with an empty text draft.")) { break; }
                    press(SDLK_Q); // The key chord and committed text must take separate routes.
                    if (!requireSmoke(itemBrowser.EditingText() && modal.Active() == Screen::Debug && !modal.Pending(),
                        "Smoke q key closed browser search.")) { break; }
                    textInput("q");
                    if (!requireSmoke(itemBrowser.Frame().screen == ItemBrowserScreen::Search && itemBrowser.Frame().input == "q",
                        "Smoke q text did not reach browser search exactly once.")) { break; }
                    press(SDLK_BACKSPACE); textInput("rock"); press(SDLK_RETURN);
                    const auto& browser = itemBrowser.Frame();
                    if (!requireSmoke(browser.screen == ItemBrowserScreen::List && browser.query == "rock"
                        && browser.matches.size() == 1 && browser.matches.front() == rock && !browserRequest
                        && !browser.busy && nextId == requestBefore && !SDL_TextInputActive(platform.window),
                        "Smoke search committed an unexpected match or give, or retained text input.")) { break; }
                    press(SDLK_E);
                    requireSmoke(itemBrowser.Frame().screen == ItemBrowserScreen::Examine
                        && itemBrowser.Frame().examination.screen == ItemUiScreen::Examine,
                        "Smoke browser examine did not open the item sheet.");
                    ++smokeAiStep; break;
                }
                case 5:
                    if (!smokeBrowserExamineDrawn) { break; }
                    press(SDLK_ESCAPE);
                    if (!requireSmoke(itemBrowser.Frame().screen == ItemBrowserScreen::List,
                        "Smoke browser examine Escape did not return to list.")) { break; }
                    smokeBrowserRockCount = rockCount(); smokeBrowserTick = snapshot.view.tick;
                    smokeBrowserRevision = snapshot.view.revision;
                    press(SDLK_RETURN);
                    requireSmoke(browserRequest.has_value() && itemBrowser.Frame().busy,
                        "Smoke browser give-one was not enqueued.");
                    ++smokeAiStep; break;
                case 6: {
                    if (browserRequest) { break; }
                    // Completion publishes first; drain its mailbox before checking quantities.
                    consumeSnapshot();
                    if (!requireSmoke(!itemBrowser.Frame().busy && snapshot.view.revision > smokeBrowserRevision
                        && rockCount() == smokeBrowserRockCount + 1 && snapshot.paused && snapshot.menuOpen
                        && snapshot.view.tick == smokeBrowserTick, "Smoke give-one snapshot/count/pause mismatch.")) { break; }
                    const auto requestBefore = nextId;
                    press(SDLK_M); textInput("0"); press(SDLK_RETURN);
                    if (!requireSmoke(itemBrowser.Frame().screen == ItemBrowserScreen::Amount && itemBrowser.Frame().input == "0"
                        && !itemBrowser.Frame().feedback.empty() && !browserRequest && !itemBrowser.Frame().busy
                        && nextId == requestBefore && SDL_TextInputActive(platform.window),
                        "Smoke invalid zero amount queued a give or left the editor.")) { break; }
                    smokeBrowserRevision = snapshot.view.revision;
                    press(SDLK_U,SDL_KMOD_CTRL);
                    if (!requireSmoke(itemBrowser.Frame().input.empty(), "Smoke Ctrl+U did not clear amount.")) { break; }
                    textInput("3"); press(SDLK_RETURN);
                    requireSmoke(browserRequest.has_value() && itemBrowser.Frame().busy
                        && itemBrowser.Frame().screen == ItemBrowserScreen::List, "Smoke browser batch-three was not enqueued.");
                    ++smokeAiStep; break;
                }
                case 7:
                    if (browserRequest) { break; }
                    consumeSnapshot();
                    if (!requireSmoke(!itemBrowser.Frame().busy && snapshot.view.revision > smokeBrowserRevision
                        && rockCount() == smokeBrowserRockCount + 4 && snapshot.paused && snapshot.menuOpen
                        && snapshot.view.tick == smokeBrowserTick, "Smoke batch-three snapshot/count/pause mismatch.")) { break; }
                    smokeBrowserGrantsVerified = true;
                    press(SDLK_ESCAPE);
                    requireSmoke(!itemBrowser.IsOpen() && modal.Active() == Screen::Debug && !modal.Pending() && !debugAi,
                        "Smoke browser list Escape did not return to worldgen.");
                    ++smokeAiStep; break;
                case 8:
                    press(SDLK_ESCAPE); smokeWaiting = modal.Pending();
                    requireSmoke(smokeWaiting, "Smoke worldgen Escape did not request modal close.");
                    ++smokeAiStep; break;
                default:
                    requireSmoke(false, "Smoke F11 reached an unexpected substep."); break;
                }
            }
            else if (smokeStep == 9) { press(SDLK_F2); smokeWaiting = modal.Pending(); }
            else if (smokeStep == 10) {
                press(SDLK_DOWN); press(SDLK_RETURN);
                SDL_Event entry{}; entry.type = SDL_EVENT_TEXT_INPUT; entry.text.text = "12"; handleEvent(entry);
                press(SDLK_RETURN);
                press(SDLK_DOWN); press(SDLK_DOWN); press(SDLK_DOWN); press(SDLK_RETURN);
                entry.text.text = "24"; handleEvent(entry); press(SDLK_RETURN);
                press(SDLK_S); smokeWaiting = settingsId.has_value();
                if (!smokeWaiting) { smokeFailed = true; }
            }
            else if (smokeStep == 11) {
                if (store.Effective().viewRadius != 12 || store.Effective().textSize != 24 || appliedTextSize != 24) { smokeFailed = true; }
                press(SDLK_ESCAPE); smokeWaiting = modal.Pending();
            }
            else if (smokeStep == 12) { press(SDLK_E); smokeWaiting = modal.Pending(); }
            else if (smokeStep == 13) { smokeWaiting = submit(RequestKind::Command,*GameplayCommand(InputAction::East,false)); }
            else if (smokeStep == 14) { press(SDLK_E); smokeWaiting = modal.Pending(); }
            else if (smokeStep == 15) {
                press(SDLK_RIGHT);
                if (modal.Active() != Screen::Actions) { smokeFailed = true; }
                press(SDLK_ESCAPE);
                if (modal.Active() != Screen::Targets || commandPending) { smokeFailed = true; }
                press(SDLK_RIGHT);
                if (modal.Active() != Screen::Actions) { smokeFailed = true; }
                ++smokeStep;
            }
            else if (smokeStep == 16) {
                press(SDLK_DOWN); press(SDLK_DOWN); press(SDLK_RETURN);
                smokeWaiting = commandPending;
                if (!smokeWaiting) { smokeFailed = true; }
            }
            else if (smokeStep == 17) { press(SDLK_I); smokeWaiting = modal.Pending(); }
            else if (smokeStep == 18) {
                if (smokeItemStep == 0 && smokeInventoryDrawn) {
                    const auto requestBefore = nextId;
                    press(SDLK_F7); press(SDLK_Z); press(SDLK_1);
                    if (nextId != requestBefore || modal.Active() != Screen::ItemInventory || commandPending) { smokeFailed = true; }
                    press(SDLK_E); ++smokeItemStep;
                } else if (smokeItemStep == 1 && smokeItemExamineDrawn) {
                    press(SDLK_ESCAPE); ++smokeItemStep;
                } else if (smokeItemStep == 2) {
                    press(SDLK_ESCAPE); ++smokeItemStep;
                } else if (smokeItemStep == 3 && modal.Active() == Screen::None) { ++smokeStep; }
            }
            else if (smokeStep == 19) { press(SDLK_SLASH); smokeWaiting = modal.Pending(); }
            else if (smokeStep == 20 && smokeAdvancedDrawn) {
                if (smokeItemAction == 0) {
                    if (snapshot.view.wieldedItem || itemUi.Frame().activePane != 0
                        || itemUi.Frame().panes[0].rows.empty()) { smokeFailed = true; }
                    press(SDLK_W); smokeItemAction = 1;
                    if (!itemUi.NeedsTime()) { smokeFailed = true; }
                } else if (!itemRequest && !commandPending && snapshot.paused && !itemUi.NeedsTime()) {
                    if (smokeItemAction == 1) {
                        if (!snapshot.view.wieldedItem || !itemUi.IsOpen()) { smokeFailed = true; }
                        press(SDLK_W); smokeItemAction = 2;
                        if (!itemUi.NeedsTime()) { smokeFailed = true; }
                    } else {
                        if (snapshot.view.wieldedItem || !itemUi.IsOpen()) { smokeFailed = true; }
                        press(SDLK_TAB); press(SDLK_5); press(SDLK_ESCAPE); ++smokeStep;
                    }
                }
            }
            else if (smokeStep == 21 && modal.Active() == Screen::None) {
                press(SDLK_X); smokeWaiting = modal.Pending();
            }
            else if (smokeStep == 22 && !inspectionId && !inspectionNeeded && !inspection.empty()) {
                if (modal.Active() != Screen::Examine || itemUi.IsOpen()) { smokeFailed = true; }
                press(SDLK_ESCAPE); smokeWaiting = modal.Pending();
            }
            else if (smokeStep > 22 && modal.Active() == Screen::None) {
                if (!smokeExamineDrawn || !smokeActionsDrawn || !smokeHealthDrawn
                    || !smokeInventoryDrawn || !smokeItemExamineDrawn || !smokeAdvancedDrawn
                    || !smokeBrowserListDrawn || !smokeBrowserExamineDrawn || !smokeBrowserGrantsVerified) { smokeFailed = true; }
                running = false;
            }
            if (smokeWaiting) { ++smokeStep; }
        }
        if (discoverySmoke && discoveryDrawn && modal.Active() == Screen::Lan && !modal.Pending()) {
            multiplayerSmokeStep = NetworkSmokeStep::Done; running = false;
        }
        if (multiplayerSmoke && !discoverySmoke && started && snapshotGeneration && snapshot.view.playerId
            && assetState == AssetState::Ready
            && (snapshot.mode == SessionMode::Host || snapshot.connectionState == network::ConnectionState::Active)) {
            switch (multiplayerSmokeStep) {
            case NetworkSmokeStep::AwaitSession:
                if (modal.Active() == Screen::Lan && !modal.Pending()) { smokePress(InputContext::Menu, InputAction::Cancel); break; }
                if (modal.Active() == Screen::None && !modal.Pending() && mapValid && multiplayerRendered
                    && !commandPending && (!sessionOptions.host || hostPeerSeen)) {
                    smokePress(InputContext::Gameplay, InputAction::Players);
                    multiplayerSmokeStep = NetworkSmokeStep::Players;
                }
                break;
            case NetworkSmokeStep::Players:
                if (multiplayerPlayersDrawn && !modal.Pending() && snapshot.menuOpen && modal.Active() == Screen::Players) {
                    smokePress(InputContext::Menu, InputAction::Cancel); multiplayerSmokeStep = NetworkSmokeStep::PlayersClosed;
                }
                break;
            case NetworkSmokeStep::PlayersClosed:
                if (!modal.Pending() && modal.Active() == Screen::None && mapValid) {
                    smokePress(InputContext::Gameplay, InputAction::Examine); multiplayerSmokeStep = NetworkSmokeStep::ExamineCurrent;
                }
                break;
            case NetworkSmokeStep::ExamineCurrent:
                if (multiplayerCurrentDrawn && mapValid && inspectionValid && !modal.Pending()) {
                    multiplayerInitialY = cursor.y;
                    const auto direction = cursor.y < world_max_y ? InputAction::MoveUp : InputAction::MoveDown;
                    multiplayerOtherY = cursor.y + (direction == InputAction::MoveUp ? 1 : -1);
                    smokePress(InputContext::Gameplay, direction);
                    // Exercise resize while the new layer is pending: no old-layer map may be submitted or accepted.
                    SDL_Event resized{}; resized.type = SDL_EVENT_WINDOW_RESIZED; handleEvent(resized);
                    if (cursor.y != multiplayerOtherY || displayLayerReady() || mapValid) {
                        SetMessage("Smoke layer switch reused an obsolete inspection map."); smokeFailed = true;
                    }
                    multiplayerSmokeStep = NetworkSmokeStep::ExamineOther;
                }
                break;
            case NetworkSmokeStep::ExamineOther:
                if (multiplayerOtherDrawn && mapValid && inspectionValid && inspectionView.origin.y == multiplayerOtherY
                    && cursor.y == multiplayerOtherY && !modal.Pending()) {
                    smokePress(InputContext::Menu, InputAction::Cancel); multiplayerSmokeStep = NetworkSmokeStep::ExamineClosed;
                }
                break;
            case NetworkSmokeStep::ExamineClosed:
                if (!modal.Pending() && modal.Active() == Screen::None && mapValid) {
                    smokePress(InputContext::Gameplay, InputAction::Inventory); multiplayerSmokeStep = NetworkSmokeStep::Inventory;
                }
                break;
            case NetworkSmokeStep::Inventory:
                if (multiplayerInventoryDrawn && !modal.Pending() && modal.Active() == Screen::ItemInventory) {
                    smokePress(InputContext::Menu, InputAction::Cancel); multiplayerSmokeStep = NetworkSmokeStep::InventoryClosed;
                }
                break;
            case NetworkSmokeStep::InventoryClosed:
                if (!modal.Pending() && modal.Active() == Screen::None && !snapshot.menuOpen && mapValid) {
                    openLan(); multiplayerSmokeStep = NetworkSmokeStep::Multiplayer;
                }
                break;
            case NetworkSmokeStep::Multiplayer:
                if (multiplayerSettingsDrawn && modal.Active() == Screen::Lan && !modal.Pending() && snapshot.menuOpen) {
                    const auto press = [&](SDL_Keycode key) {
                        SDL_Event event{}; event.type = SDL_EVENT_KEY_DOWN; event.key.key = key; handleEvent(event);
                    };
                    const auto select = [&](const char* prefix) {
                        for (std::size_t i = 0; i < frame.rows.size(); ++i) {
                            if (frame.rows[i].text.find(prefix) != 0) { continue; }
                            press(SDLK_HOME);
                            for (std::size_t n = 0; n < i; ++n) { press(SDLK_DOWN); }
                            return true;
                        }
                        smokeFailed = true; return false;
                    };
                    if (!select("Game speed")) { break; }
                    press(SDLK_RETURN);
                    if (snapshot.mode == SessionMode::Guest) {
                        if (multiplayerUi.EditingText()) { smokeFailed = true; }
                        close(); multiplayerSmokeStep = NetworkSmokeStep::MultiplayerClosed;
                    } else {
                        if (!multiplayerUi.EditingText()) { smokeFailed = true; break; }
                        SDL_Event textEvent{}; textEvent.type = SDL_EVENT_TEXT_INPUT; textEvent.text.text = "200";
                        handleEvent(textEvent); press(SDLK_RETURN);
                        if (!multiplayerUi.Pacing().rateLimited && select("Limit simulation speed")) { press(SDLK_RETURN); }
                        if (!select("Apply pacing")) { break; }
                        press(SDLK_RETURN);
                        if (!settingsId) { smokeFailed = true; }
                        multiplayerSmokeStep = NetworkSmokeStep::PacingApplied;
                    }
                }
                break;
            case NetworkSmokeStep::PacingApplied:
                if (!settingsId && snapshot.runtimeSettings.pacing.ticksPerSecond == 200 && snapshot.runtimeSettings.pacing.rateLimited) {
                    if (store.Effective().pacing != snapshot.runtimeSettings.pacing) { smokeFailed = true; }
                    close(); multiplayerSmokeStep = NetworkSmokeStep::MultiplayerClosed;
                }
                break;
            case NetworkSmokeStep::MultiplayerClosed:
                if (!modal.Pending() && modal.Active() == Screen::None && !snapshot.menuOpen && mapValid && input.opportunity) {
                    smokePress(InputContext::Gameplay, InputAction::Wait);
                    multiplayerCommandId = commandRequest;
                    if (!multiplayerCommandId) { SetMessage("Smoke wait was not submitted."); smokeFailed = true; }
                    multiplayerSmokeStep = NetworkSmokeStep::WaitCompleted;
                }
                break;
            case NetworkSmokeStep::WaitCompleted:
                if (!commandPending && multiplayerCommandCompleted) {
                    multiplayerLingerUntil = Clock::now() + std::chrono::seconds(sessionOptions.host ? 4 : 0);
                    multiplayerSmokeStep = NetworkSmokeStep::Linger;
                }
                break;
            case NetworkSmokeStep::Linger:
                if (multiplayerLingerUntil && Clock::now() >= *multiplayerLingerUntil) {
                    multiplayerSmokeStep = NetworkSmokeStep::Done; running = false;
                }
                break;
            case NetworkSmokeStep::Done: break;
            }
        }
        const auto now = Clock::now();
        const auto elapsedMilliseconds = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now - animationEpoch).count());
        const bool showPending = commandPending && now - commandSubmittedAt >= pendingCommandStatusDelay;
        if (showPending != pendingStatusVisible) {
            pendingStatusVisible = showPending;
            dirty = true;
        }
        if (assetState == AssetState::Ready) { playback.RetireCompleted(assets.Definition(), elapsedMilliseconds); }
        if (animationDeadline && elapsedMilliseconds >= *animationDeadline) { dirty = true; }
        if (dirty && !minimized) {
            messageHistory.SetCapacity(store.Effective().messageHistorySize);
            String display = snapshot.mode == SessionMode::Host ? "Host :" + UnsignedText(snapshot.hostPort)
                : snapshot.mode == SessionMode::Guest ? String(ConnectionText(snapshot.connectionState)) : String("Local");
            if (snapshot.menuOpen) { display += snapshot.paused ? " | Paused" : " | Menu open; world live"; }
            if (modal.Pending()) { display += " | Updating menu..."; }
            if (showPending) {
                display += " | ";
                display += snapshot.view.commandQueued ? localization.Text("game.ui.queued", "Queued")
                    : localization.Text("game.ui.awaiting", "Pending");
            }
            display += '\n';
            if (choosingGrab) {
                display += localization.Text("grab.direction", "Grab where? Choose one of eight directions; Escape cancels.");
                display += '\n';
            }
            if (mapValid) {
                const auto combat = CombatSummaryText(snapshot.view.combat);
                const auto feedback = CombatFeedbackText(snapshot.view.combat);
                if (!combat.empty()) { display += combat; }
                if (!feedback.empty()) { if (!display.empty()) { display += '\n'; } display += feedback; }
            }
            frame.visible = modal.Active() != Screen::None && modal.Active() != Screen::ItemInventory;
            frame.compactTitle.clear(); frame.compactFooter.clear();
            frame.presentation = modal.Active() == Screen::Examine || modal.Active() == Screen::Targets || modal.Active() == Screen::Actions
                ? ScreenPresentation::Anchored : ScreenPresentation::Large;
            frame.followCursor = modal.Active() == Screen::Examine;
            frame.scrollText = false;
            frame.cursor.reset(); frame.highlights.clear(); frame.debugImage = nullptr;
            frame.rows.clear(); frame.details.clear(); frame.selected = -1;
            if (modal.Active() == Screen::Options) { optionsUi.Present(store,localization,frame); }
            if (modal.Active() == Screen::Combat) { combatUi.Refresh(content, snapshot.view); combatUi.Present(frame); }
            if (modal.Active() == Screen::CombatInfo) { PresentCombatInfo(content, snapshot.view, frame); }
            if (modal.Active() == Screen::Lan) {
                multiplayerUi.Present(snapshot, lanRequest.has_value() || settingsId.has_value(), frame);
            }
            if (modal.Active() == Screen::Players) {
                frame.title = snapshot.paused ? "Players | Local session paused" : "Players | World live";
                for (const auto& player : snapshot.view.players) {
                    String row = player.name + (player.id == snapshot.view.playerId ? " (you)" : "");
                    row += player.state == PlayerConnectionState::Active ? " | Active" : player.state == PlayerConnectionState::Joining ? " | Joining" : " | Parked";
                    row += player.ready ? " | Ready" : " | Recovering";
                    if (player.commandQueued) { row += " | Command queued"; }
                    row += " | Grace " + UnsignedText(player.graceMicroseconds / 1000) + " ms";
                    frame.rows.push_back({eastl::move(row)});
                }
                frame.footer = "Escape / Enter: close | F9: LAN";
            }
            if (modal.Active() == Screen::Crafting && powerOpen) {
                frame.title = "Power workshop | Availability checked by server";
                powerWorkshop.Describe(snapshot.view, content, powerLines);
                for (const auto& line : powerLines) frame.rows.push_back({line});
                if (!craftingFeedback.empty()) frame.details.push_back(craftingFeedback);
                frame.footer = "Enter: install | p: wire/place | Tab: kit | g: equipment | w/W,d/D: size | Arrows: position | Escape: back";
            } else if (modal.Active() == Screen::Crafting) {
                frame.title = "Crafting, recycling and learning | p: Power workshop";
                CollectCraftingMenu(snapshot.view, content, localization, craftingRows);
                CollectLearningSummary(snapshot.view, content, learningLines);
                if (!craftingSelection && selectedAction == 0 && !craftingRows.empty()) { craftingSelection = craftingRows.front().command; }
                for (std::size_t i = 0; i < craftingRows.size(); ++i) {
                    frame.rows.push_back({craftingRows[i].label});
                    if (craftingSelection && SameCraftingChoice(*craftingSelection, craftingRows[i].command)) {
                        selectedAction = i; frame.selected = static_cast<int>(i);
                    }
                }
                for (std::size_t i = 0; i < learningLines.size(); ++i) {
                    frame.rows.push_back({"Learning | " + learningLines[i]});
                    if (!craftingSelection && selectedAction == craftingRows.size() + i) { frame.selected = static_cast<int>(selectedAction); }
                }
                if (frame.rows.empty()) { frame.rows.push_back({"No recipes, reachable jobs, recyclable items, or learning information."}); }
                if (!craftingFeedback.empty()) { frame.details.push_back(craftingFeedback); }
                frame.footer = BindingHint(keys, InputContext::Menu, InputAction::North) + "/"
                    + BindingHint(keys, InputContext::Menu, InputAction::South) + " choose | "
                    + BindingHint(keys, InputContext::Menu, InputAction::Accept) + " submit | "
                    + BindingHint(keys, InputContext::Menu, InputAction::Cancel) + " close";
            }
            if (modal.Active() == Screen::Help) {
                frame.title = snapshot.paused ? "Controls | Local session paused" : "Controls | World live";
                std::size_t start = 0;
                for (std::size_t i = 0; i <= hints.size(); ++i) {
                    if (i == hints.size() || hints[i] == '\n') {
                        frame.rows.push_back({String(hints.data()+start,i-start)}); start = i+1;
                    }
                }
                frame.footer = "PgUp/PgDn: scroll | Escape / Enter: close | F9: LAN";
            }
            if (modal.Active() == Screen::Examine) {
                frame.title = localization.Text("sdl.ui.examine", "Examine") + " | " + NumberText(cursor.x) + "," + NumberText(cursor.z) + " Y " + NumberText(cursor.y);
                frame.cursor = cursor;
                if (inspectedCursor && *inspectedCursor == cursor) {
                    if (examinedEnemy) { AppendEnemyExamine(*examinedEnemy, localization, frame); }
                    for (const auto& line : inspection) { frame.rows.push_back({line}); }
                } else { frame.rows.push_back({"Loading inspection..."}); }
                frame.footer = BindingHint(keys, InputContext::Menu, InputAction::Cancel) + " " + localization.Text("sdl.ui.close", "close")
                    + " | PgUp/PgDn " + localization.Text("sdl.ui.scroll", "scroll");
            }
            if (modal.Active() == Screen::Targets || modal.Active() == Screen::Actions) {
                frame.cursor = modal.Active() == Screen::Targets ? snapshot.view.player_position : cursor;
                for (const auto& choice : snapshot.interactions) { frame.highlights.push_back(choice.request.position); }
                frame.title = localization.Text("sdl.ui.interact", "Interact");
                if (modal.Active() == Screen::Targets) {
                    String directions;
                    for (const auto direction : {InputAction::North, InputAction::South, InputAction::West, InputAction::East}) {
                        if (!directions.empty()) { directions += '/'; }
                        directions += BindingHint(keys, InputContext::Menu, direction);
                    }
                    frame.rows.push_back({snapshot.interactions.empty() ? localization.Text("sdl.ui.no_targets", "No reachable targets")
                        : directions + " " + localization.Text("sdl.ui.target_directions", "choose tile; . / Home selects self")});
                } else {
                    frame.selected = static_cast<int>(selectedAction);
                    bool sameTarget = !actions.empty();
                    for (const auto& choice : actions) { sameTarget &= choice.target == actions.front().target; }
                    if (sameTarget) { frame.title += " | "; frame.title += actions.front().target; }
                    for (const auto& choice : actions) {
                        String actionLabel = choice.label;
                        if (choice.request.kind == InteractionKind::Lock) { actionLabel = localization.Text("interaction.action.lock",choice.label); }
                        if (choice.request.kind == InteractionKind::Unlock) { actionLabel = localization.Text("interaction.action.unlock",choice.label); }
                        String label = sameTarget ? actionLabel : choice.target + ": " + actionLabel;
                        frame.rows.push_back({eastl::move(label),choice.reason != ObjectUseReason::None});
                    }
                    if (selectedAction < actions.size() && actions[selectedAction].reason != ObjectUseReason::None) {
                        frame.details.push_back(InteractionReason(localization,actions[selectedAction].reason));
                    }
                }
                frame.footer = BindingHint(keys, InputContext::Menu, InputAction::Cancel) + " "
                    + localization.Text("sdl.ui.back", "back");
                if (modal.Active() == Screen::Actions) {
                    frame.footer += " | " + BindingHint(keys, InputContext::Menu, InputAction::Accept);
                    frame.footer += "\n" + BindingHint(keys, InputContext::Menu, InputAction::North) + "/"
                        + BindingHint(keys, InputContext::Menu, InputAction::South) + " " + localization.Text("sdl.ui.choose", "choose");
                }
            }
            if (modal.Active() == Screen::Debug) {
                if (itemBrowser.IsOpen()) {
                    const auto& browser = itemBrowser.Frame();
                    frame.title = String(snapshot.paused ? "Item browser | Local session paused" : "Item browser | Host world live");
                    frame.title += "\n" + UnsignedText(browser.matches.empty() ? 0 : browser.selected + 1)
                        + " / " + UnsignedText(browser.matches.size()) + " matches | Search: " + browser.query;
                    frame.scroll = browser.scroll;
                    frame.selected = browser.matches.empty() ? -1 : static_cast<int>(browser.selected);
                    for (const auto type : browser.matches) { frame.rows.push_back({itemBrowser.Label(type)}); }
                    if (browser.matches.empty()) { frame.rows.push_back({"No matching items."}); }
                    if (browser.screen == ItemBrowserScreen::Search) { frame.details.push_back("Search: " + browser.input); }
                    if (browser.screen == ItemBrowserScreen::Amount) { frame.details.push_back("Amount (1-1000): " + browser.input); }
                    if (browser.busy) { frame.details.push_back("Giving items..."); }
                    if (!browser.feedback.empty()) { frame.details.push_back(browser.feedback); }
                    frame.footer = "/: search | e: examine | Enter: give 1 | m: amount\nArrows: choose | Escape: back | F11: close debug";
                    if (browser.screen == ItemBrowserScreen::Search || browser.screen == ItemBrowserScreen::Amount) {
                        frame.footer = "Type text | Enter: accept | Escape: list | F11: close debug";
                    }
                    if (browser.screen == ItemBrowserScreen::Examine) { frame.visible = false; }
                } else if (debugPower) {
                    frame.title = "DEBUG power / chunk residency (read-only owner-thread snapshot)";
                    for (const auto& line : powerDebugLines) frame.rows.push_back({line});
                    if (powerDebugLines.empty()) frame.rows.push_back({"Loading diagnostic snapshot..."});
                    frame.footer = "p: refresh | a: worldgen | Arrows/PgUp/PgDn: scroll | Escape/F11: close";
                } else if (debugAi) {
                    frame.title = String(snapshot.paused ? "AI inspector | Local session paused\nX " : "AI inspector | Host world live\nX ") + NumberText(cursor.x)
                        + " Z " + NumberText(cursor.z) + " Y " + NumberText(cursor.y);
                    if (aiValid) { frame.title += " | Tick " + UnsignedText(aiView.tick); }
                    frame.title += "\nLast-decision snapshots, not fresh scans";
                    if (aiValid) { frame.debugImage = &aiView.image; frame.cursor = cursor; frame.details = aiView.details; }
                    else { frame.rows.push_back({"Loading live resident AI capture..."}); }
                    frame.footer = "p: power/chunks | i: item browser | a: worldgen | Escape/F11: close\nGreen: candidates | Red: blocked | Yellow: chosen";
                    frame.footer += "\nBlue: origin | Purple: goal | Orange: backtracking";
                    frame.footer += "\nArrows: tile | Alt+Up/Down, >/<: Y\nHome/c: player | PgUp/PgDn: details";
                } else {
                    frame.title = "World generation | Seed " + UnsignedText(snapshot.seed) + " | Version " + UnsignedText(snapshot.generationVersion);
                    if (snapshot.generationVersion != world_generation_version) { frame.rows.push_back({"Preview unavailable for this generation version"}); }
                    else {
                        frame.title += "\n"; frame.title += WorldgenDebugLayerName(debugLayer);
                        frame.title += " | Scale "; frame.title += NumberText(debugScale);
                        frame.title += " | X "; frame.title += NumberText(cursor.x); frame.title += " Z "; frame.title += NumberText(cursor.z);
                        frame.title += "\nSurface baseline: excludes saved edits and entities";
                        frame.debugImage = &debugImage; frame.cursor = cursor; frame.details = debugDetails;
                    }
                    frame.footer = "p: power/chunks | i: item browser | a: AI inspector | Escape/F11: close\n";
                    frame.footer += WorldgenDebugLegend(debugLayer);
                    frame.footer += "\nArrows: move | Shift+arrows: region | Home: recenter | 1-5/Tab: layers | +/-: zoom | PgUp/PgDn: details";
                }
            }
            const String sessionLabel = snapshot.mode == SessionMode::Host ? "Host :" + UnsignedText(snapshot.hostPort)
                : snapshot.mode == SessionMode::Guest ? String(ConnectionText(snapshot.connectionState)) : String("Local");
            if (!DrawFrame(platform.renderer, *text, assets, mapValid ? &displayView() : nullptr,
                content, display.c_str(), hints.c_str(), elapsedMilliseconds, mapValid ? &mapPacket : nullptr,
                zoom, options.uiScale, {}, &playback, &shader, &frame,
                {&messageHistory, &store.Keys(), &localization, sessionLabel},
                modal.Active() == Screen::ItemInventory ? &itemUi.Frame()
                    : modal.Active() == Screen::Debug && itemBrowser.Frame().screen == ItemBrowserScreen::Examine
                        ? &itemBrowser.Frame().examination : nullptr)) { return SdlFailure(); }
            if (modal.Active() == Screen::Debug && itemBrowser.IsOpen()
                && itemBrowser.Frame().screen != ItemBrowserScreen::Examine) { itemBrowser.Frame().scroll = frame.scroll; }
            if (multiplayerSmoke && mapValid) { multiplayerRendered = true; }
            if (multiplayerSmoke && !modal.Pending() && !initializeScreen) {
                multiplayerPlayersDrawn |= modal.Active() == Screen::Players && frame.visible;
                multiplayerSettingsDrawn |= modal.Active() == Screen::Lan && frame.visible;
                multiplayerInventoryDrawn |= modal.Active() == Screen::ItemInventory && itemUi.Frame().screen == ItemUiScreen::Inventory;
                const bool inspectedMapDrawn = modal.Active() == Screen::Examine && mapValid && displayLayerReady()
                    && inspectedCursor && *inspectedCursor == cursor && inspectionView.origin.y == cursor.y;
                multiplayerCurrentDrawn |= multiplayerSmokeStep == NetworkSmokeStep::ExamineCurrent && inspectedMapDrawn;
                multiplayerOtherDrawn |= multiplayerSmokeStep == NetworkSmokeStep::ExamineOther && inspectedMapDrawn
                    && cursor.y == multiplayerOtherY && cursor.y != multiplayerInitialY;
                if (discoverySmoke && modal.Active() == Screen::Lan && frame.visible) {
                    for (const auto& found : snapshot.sessions) { discoveryDrawn |= found.compatible; }
                }
            }
            if (options.smoke) {
                smokeHealthDrawn |= mapValid && text->Width(62) > 0;
                smokeExamineDrawn |= modal.Active() == Screen::Examine && frame.presentation == ScreenPresentation::Anchored && frame.cursor.has_value();
                smokeActionsDrawn |= modal.Active() == Screen::Actions && frame.presentation == ScreenPresentation::Anchored && frame.selected >= 0;
                smokeInventoryDrawn |= modal.Active() == Screen::ItemInventory && itemUi.Frame().screen == ItemUiScreen::Inventory;
                smokeItemExamineDrawn |= modal.Active() == Screen::ItemInventory && itemUi.Frame().screen == ItemUiScreen::Examine;
                smokeAdvancedDrawn |= modal.Active() == Screen::ItemInventory && itemUi.Frame().screen == ItemUiScreen::Advanced;
                smokeBrowserListDrawn |= modal.Active() == Screen::Debug && !modal.Pending() && frame.visible
                    && itemBrowser.Frame().screen == ItemBrowserScreen::List && !frame.rows.empty() && !frame.debugImage;
                smokeBrowserExamineDrawn |= modal.Active() == Screen::Debug && !modal.Pending()
                    && itemBrowser.Frame().screen == ItemBrowserScreen::Examine
                    && itemBrowser.Frame().examination.screen == ItemUiScreen::Examine && !frame.visible;
            }
            dirty = false;
            animationDeadline.reset();
            if (mapValid) {
                int outputWidth = 0, outputHeight = 0;
                if (!SDL_GetCurrentRenderOutputSize(platform.renderer, &outputWidth, &outputHeight)) { return SdlFailure(); }
                const auto delay = VisibleAnimationDelay(mapPacket, assets.Definition(), elapsedMilliseconds,
                    outputWidth, outputHeight, zoom, options.uiScale, &playback,
                    modal.Active() == Screen::Examine ? SDL_Point{cursor.x - displayView().player_position.x, cursor.z - displayView().player_position.z} : SDL_Point{},
                    text->LineHeight());
                if (delay) { animationDeadline = elapsedMilliseconds + *delay; }
            }
        }
        if (options.smoke && std::chrono::steady_clock::now() >= smokeDeadline) {
            std::fprintf(stderr,"SDL %s smoke timed out at step %u (F11 substep %u): %s\n",
                discoverySmoke ? "discovery" : multiplayerSmoke ? "multiplayer" : "local",
                multiplayerSmoke ? static_cast<unsigned>(multiplayerSmokeStep) : smokeStep,smokeAiStep,message.c_str()); return 1;
        }
        consumeSnapshot();
        if (const auto repeated = heldKey.Poll(inputTime(),store.Effective().keyboardRepeatRate)) {
            SDL_Event held{}; held.type = SDL_EVENT_KEY_DOWN; held.key = *repeated;
            const String committed = heldKey.Text();
            handleEvent(held);
            if (!committed.empty() && !(repeated->mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI))
                && ((modal.Active() == Screen::Options && optionsUi.EditingText() && !settingsId)
                    || (modal.Active() == Screen::Lan && multiplayerUi.EditingText() && !settingsId && !lanRequest)
                    || (modal.Active() == Screen::Debug && itemBrowser.EditingText() && !modal.Pending()))) {
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
            consumeSnapshot();
            physicalEvent(event);
        }
    }
    const auto shutdownError = session.Stop();
    if (shutdownError) { std::fprintf(stderr,"Session shutdown save failed (%d).\n",static_cast<int>(*shutdownError)); return 1; }
    SessionReply finalReply;
    while (session.TryPopReply(finalReply)) {
        if (finalReply.result.storageError) { std::fputs("Session shutdown: world storage failed.\n", stderr); }
    }
    if (options.smoke && smokeFailed) { std::fputs("SDL smoke did not render all UI states.\n", stderr); return 1; }
    if (multiplayerSmoke) {
        if (!discoverySmoke && ((sessionOptions.join && snapshot.mode != SessionMode::Guest)
            || (sessionOptions.host && snapshot.mode != SessionMode::Host))) {
            std::fputs("SDL multiplayer smoke ended outside its requested session role.\n",stderr); return 1;
        }
        if (multiplayerSmokeStep != NetworkSmokeStep::Done) {
            std::fputs("SDL multiplayer smoke ended before required UI steps completed.\n",stderr); return 1;
        }
        if (discoverySmoke) {
            std::fprintf(stdout,"SDL discovery smoke: compatible session discovered, LAN browser rendered: ok (sessions=%zu)\n",snapshot.sessions.size());
        } else {
            std::fprintf(stdout,"SDL multiplayer smoke: %s active, Players, Examine Y=%d/%d, Inventory and Multiplayer settings rendered, authoritative wait completed%s: ok (player=%llu opportunity=%llu)\n",
                sessionOptions.host ? "host with remote peer" : "guest", multiplayerInitialY, multiplayerOtherY,
                sessionOptions.host ? ", pacing applied, peer flush 4s" : ", host settings read-only",
                static_cast<unsigned long long>(snapshot.view.playerId),static_cast<unsigned long long>(input.opportunity));
        }
        std::fflush(stdout);
    } else if (options.smoke) {
        if (options.scenario == NewGameScenario::Demo) {
            std::fputs("SDL item browser smoke: list_drawn=1 examine_drawn=1 search_q=ok give_one=1 give_batch=3 paused_tick=unchanged\n", stdout);
        }
        std::fputs("SDL smoke: ok\n", stdout);
    }
    return 0;
}

}
