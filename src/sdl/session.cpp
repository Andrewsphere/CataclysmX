#include "sdl/session.h"
#include "client/tile_inspection.h"
#include "core/first_house_scenario.h"

#include <EASTL/unique_ptr.h>
#include <EASTL/algorithm.h>
#include <EASTL/utility.h>
#include <chrono>

namespace cataclysmx::sdl {
namespace {
using Clock = std::chrono::steady_clock;

// The transport sends the maximum filtered view. A guest's viewport preference
// crops presentation only, keeping input context and authoritative positions intact.
void CropView(WorldView& view, Vector<MapItemVisual>& items, int radius) {
    const int width = 2 * radius + 1;
    if (view.width <= width || view.height <= width) { return; }
    const int x = view.player_position.x - radius - view.origin.x;
    const int z = view.player_position.z - radius - view.origin.z;
    for (int row = 0; row < width; ++row) {
        for (int column = 0; column < width; ++column) {
            const auto source = static_cast<std::size_t>((z + row) * view.width + x + column);
            const auto destination = static_cast<std::size_t>(row * width + column);
            view.cells[destination] = view.cells[source];
            items[destination] = items[source];
        }
    }
    view.cells.resize(static_cast<std::size_t>(width * width));
    items.resize(view.cells.size());
    view.origin.x += x; view.origin.z += z;
    view.width = view.height = width;
}

// All mutable simulation, transport and identity state belongs to this stack
// object on the owner thread. A guest does not need a local Server until return.
class SessionOwner {
public:
    SessionOwner(SessionChannels& channels, const ContentCatalog& content, std::uint64_t generation,
        SessionOptions options, Session::Wake wake, void* userdata, eastl::optional<SaveError>& shutdownError)
        : channels_(channels), content_(content), generation_(generation), options_(eastl::move(options)),
          wake_(wake), userdata_(userdata), shutdownError_(shutdownError) {}

    void Run();

private:
    struct Pending {
        std::uint64_t id;
        std::uint64_t generation;
        std::uint64_t sequence;
        bool admitted = false;
    };
    void Notify() { if (wake_) { wake_(userdata_); } }
    PlayerInputContext Input() const {
        if (mode_ == SessionMode::Guest) { return guest_->GetInputContext(); }
        return server_ ? server_->GetInputContext(server_->GetHostPlayerId()) : PlayerInputContext{};
    }
    void Reply(SessionReply reply, bool terminal = true) {
        reply.input = Input();
        channels_.PublishReply(eastl::move(reply), terminal);
        Notify();
    }
    void Status(const char* text) { status_ = text; ++statusVersion_; forcePublish_ = true; }
    bool Local(SessionReply& failure, bool fallback = false);
    bool Runtime(SessionReply& failure);
    bool Connect(network::ConnectionSettings settings, bool newCharacter, SessionReply& reply);
    bool Open(network::HostSettings settings, SessionReply& reply);
    bool Discover(network::HostSettings settings, String address, SessionReply& reply);
    void CancelPending();
    bool ReturnLocal();
    void Save(SessionReply& reply);
    void Publish();
    void Receipt(const PlayerCommandResult& result);
    void FinishItemRecovery();
    void Settle();
    void Request(SessionRequest request);
    void Gameplay(SessionRequest request);
    void Inspection(PlayerInspection inspection, const Pending& pending);

    SessionChannels& channels_;
    const ContentCatalog& content_;
    std::uint64_t generation_;
    SessionOptions options_;
    Session::Wake wake_;
    void* userdata_;
    eastl::optional<SaveError>& shutdownError_;
    // Destruction order keeps the runtime and server alive through transport teardown.
    eastl::unique_ptr<Server> server_;
    eastl::unique_ptr<network::NetworkRuntime> runtime_;
    eastl::unique_ptr<network::LanHost> host_;
    eastl::unique_ptr<network::LanClient> guest_;
    eastl::unique_ptr<network::LanDiscovery> discovery_;
    client::LanProfile profile_;
    eastl::optional<client::ProfileError> profileError_;
    eastl::optional<network::NetworkError> networkError_;
    eastl::optional<SaveError> storageError_;
    network::ConnectionSettings connection_;
    std::uint64_t persistedToken_ = 0;
    SessionMode mode_ = SessionMode::Local;
    bool paused_ = false;
    bool menuOpen_ = false;
    bool savedLocalPause_ = false;
    bool savedLocalMenu_ = false;
    bool hasSave_ = false;
    bool itemRecovery_ = false;
    PacingSettings itemPacing_{};
    eastl::optional<std::uint64_t> itemPauseId_;
    eastl::optional<Pending> pending_;
    eastl::optional<Pending> inspection_;
    std::uint64_t sequence_ = 0;
    std::uint64_t inspectionSequence_ = 0;
    bool discoveryActive_ = false;
    Vector<network::DiscoveredSession> discoveredSessions_;
    std::uint16_t discoveryPort_ = 24681;
    String discoveryAddress_;
    std::uint64_t discoveryElapsed_ = 0;
    std::uint64_t statusVersion_ = 0;
    String status_;
    bool forcePublish_ = true;
    std::uint64_t publishedRevision_ = 0;
    PlayerInputContext publishedInput_{};
    Clock::time_point baseline_ = Clock::now();
    SessionSnapshot building_;
    PlayerPresentation presentation_;
    bool presentationOverflow_ = false;
};

bool SessionOwner::Local(SessionReply& failure, bool fallback) {
    if (server_) { return true; }
    Diagnostics diagnostics;
    auto scenario = options_.scenario;
    const auto validStart = [&] {
        switch (scenario) {
        case NewGameScenario::FirstHouse: return FindFirstHouseStart(content_, options_.settings.seed).has_value();
        case NewGameScenario::Town: return FindTownStart(content_, options_.settings.seed).has_value();
        case NewGameScenario::Demo:
        case NewGameScenario::ZombieShowcase: return true;
        }
        return false;
    };
    if (options_.loadPath.empty() && !ValidateServerContent(content_, &diagnostics)) {
        failure.startupError = StartupError::InvalidContent;
        failure.details.push_back(FormatDiagnostics(diagnostics));
        return false;
    }
    if (options_.loadPath.empty() && !validStart()) {
        if (!fallback) {
            failure.startupError = StartupError::InvalidContent;
            failure.details.push_back("Selected local scenario has no valid start for this content and seed");
            return false;
        }
        scenario = NewGameScenario::Demo;
        String message = status_;
        if (!message.empty()) { message += "; "; }
        message += "Selected local scenario is unavailable; returning to a Demo fallback";
        Status(message.c_str());
    }
    // Loading does not require the selected creation scenario's generated start.
    if (!options_.loadPath.empty()) {
        auto loaded = Server::LoadSave(content_, options_.loadPath.c_str(), &diagnostics);
        if (!loaded) {
            failure.startupError = StartupError::Storage;
            failure.storageError = loaded.error();
            failure.details.push_back(FormatDiagnostics(diagnostics));
            server_.reset();
            return false;
        }
        server_ = eastl::move(*loaded);
        hasSave_ = true;
        if (!options_.settingsOverridesProvided) {
            const auto residency = ResolveSettingsForServer(*server_, {}, {});
            options_.settings.activeRadius = residency.activeRadius;
            options_.settings.residentRadius = residency.residentRadius;
            options_.settings.residencyMode = residency.residencyMode;
            options_.settings.residentRadiusTiles = residency.residentRadiusTiles;
        }
    } else { server_.reset(new Server(content_, options_.settings.seed, scenario)); }
    const auto applied = options_.settingsOverridesProvided
        ? ApplySettings(*server_, options_.globalSettings, options_.worldSettings, &diagnostics)
        : ApplySettings(*server_, options_.settings, &diagnostics);
    if (!applied) {
        failure.startupError = StartupError::InvalidSettings;
        failure.details.push_back(FormatDiagnostics(diagnostics));
        server_.reset();
        return false;
    }
    if (options_.settingsOverridesProvided) {
        options_.settings = ResolveSettingsForServer(*server_, options_.globalSettings, options_.worldSettings);
        options_.settings.seed = server_->GetWorld().GetSeed();
    }
    if (!options_.savePath.empty()) {
        if (auto created = server_->CreateSave(options_.savePath.c_str(), &diagnostics); !created) {
            failure.startupError = StartupError::Storage;
            failure.storageError = created.error();
            failure.details.push_back(FormatDiagnostics(diagnostics));
            server_.reset();
            return false;
        }
        hasSave_ = true;
    }
    return true;
}

bool SessionOwner::Runtime(SessionReply& failure) {
    if (runtime_) { return true; }
    auto created = network::NetworkRuntime::Create();
    if (!created) {
        failure.startupError = StartupError::Network;
        failure.networkError = created.error();
        return false;
    }
    runtime_ = eastl::move(*created);
    guest_.reset(new network::LanClient(*runtime_, content_));
    discovery_.reset(new network::LanDiscovery(*runtime_, content_));
    return true;
}

bool SessionOwner::Open(network::HostSettings settings, SessionReply& reply) {
    if (mode_ == SessionMode::Guest || itemRecovery_ || pending_) {
        reply.networkError = network::NetworkError::Busy; return false;
    }
    if (!Local(reply) || !Runtime(reply)) { return false; }
    if (!host_) { host_.reset(new network::LanHost(*runtime_, *server_, content_)); }
    if (auto opened = host_->Open(eastl::move(settings)); !opened) {
        reply.networkError = opened.error(); return false;
    }
    mode_ = SessionMode::Host;
    paused_ = false;
    baseline_ = Clock::now();
    Status("LAN hosting open");
    return true;
}

bool SessionOwner::Connect(network::ConnectionSettings settings, bool newCharacter, SessionReply& reply) {
    if (mode_ == SessionMode::Host || itemRecovery_ || (mode_ == SessionMode::Local && pending_)) {
        reply.networkError = network::NetworkError::Busy; return false;
    }
    // Reload before joining so a corrected profile can be used without restarting.
    if (auto loaded = profile_.Load(options_.profilePath.c_str()); !loaded) {
        profileError_ = loaded.error(); reply.profileError = loaded.error();
        Status("Cannot join: LAN profile could not be loaded"); return false;
    }
    profileError_.reset();
    if (!Runtime(reply)) { return false; }
    const auto* identity = profile_.Find(settings.address, settings.port);
    settings.reconnectToken = !newCharacter && identity ? identity->token : 0;
    if (mode_ != SessionMode::Guest) { savedLocalPause_ = paused_; savedLocalMenu_ = menuOpen_; }
    CancelPending();
    guest_->Disconnect();
    ++generation_;
    mode_ = SessionMode::Guest;
    paused_ = false; menuOpen_ = false;
    connection_ = settings;
    persistedToken_ = 0;
    networkError_.reset();
    presentationOverflow_ = false;
    if (auto connected = guest_->Connect(eastl::move(settings)); !connected) {
        reply.networkError = connected.error(); networkError_ = connected.error();
        Status(network::NetworkErrorMessage(connected.error()));
        ReturnLocal(); return false;
    }
    baseline_ = Clock::now();
    Status("Connecting to LAN game");
    return true;
}

bool SessionOwner::Discover(network::HostSettings settings, String address, SessionReply& reply) {
    if (!Runtime(reply)) { return false; }
    if (!discoveryActive_) {
        if (auto opened = discovery_->Open(); !opened) { reply.networkError = opened.error(); return false; }
    }
    discoveryActive_ = true;
    discoveryPort_ = settings.discoveryPort;
    discoveryAddress_ = eastl::move(address);
    discoveryElapsed_ = 0;
    forcePublish_ = true;
    if (auto refreshed = discovery_->Refresh(discoveryPort_, discoveryAddress_.empty() ? nullptr : discoveryAddress_.c_str()); !refreshed) {
        reply.networkError = refreshed.error(); return false;
    }
    return true;
}

void SessionOwner::Save(SessionReply& reply) {
    reply.kind = ReplyKind::ControlFailed;
    if (!server_ || !hasSave_ || mode_ == SessionMode::Guest) {
        reply.storageError = SaveError::NotOpen;
        reply.details.push_back("No local save is open"); return;
    }
    Diagnostics diagnostics;
    if (auto saved = server_->Save(&diagnostics); !saved) {
        storageError_ = saved.error(); reply.storageError = saved.error();
        reply.details.push_back(FormatDiagnostics(diagnostics));
    } else { storageError_.reset(); reply.kind = ReplyKind::ControlCompleted; }
    forcePublish_ = true;
}

void SessionOwner::CancelPending() {
    if (pending_) {
        Reply({ReplyKind::Cancelled, pending_->generation, pending_->id}, !pending_->admitted);
        pending_.reset();
    }
    if (inspection_) {
        SessionReply reply{ReplyKind::Inspected, inspection_->generation, inspection_->id};
        reply.networkError = network::NetworkError::Disconnected;
        reply.details.push_back("Inspection disconnected");
        Reply(eastl::move(reply)); inspection_.reset();
    }
}

bool SessionOwner::ReturnLocal() {
    CancelPending();
    guest_->Disconnect();
    mode_ = SessionMode::Local;
    ++generation_;
    SessionReply failure;
    const bool restored = Local(failure, true);
    if (!restored) {
        storageError_ = failure.storageError;
        String message = "Local fallback could not be created; session stopped";
        for (const auto& detail : failure.details) {
            if (!detail.empty()) { message += ": "; message += detail; }
        }
        Status(message.c_str());
        channels_.Stop();
    }
    paused_ = restored ? savedLocalPause_ : true;
    menuOpen_ = restored ? savedLocalMenu_ : true;
    baseline_ = Clock::now();
    forcePublish_ = true;
    return restored;
}

void SessionOwner::Publish() {
    const auto input = Input();
    const auto revision = mode_ == SessionMode::Guest
        ? (guest_->HasView() ? guest_->View().revision : 0) : (server_ ? server_->GetRevision() : 0);
    if (!forcePublish_ && revision == publishedRevision_ && input.opportunity == publishedInput_.opportunity
        && input.lastInputSequence == publishedInput_.lastInputSequence && input.revision == publishedInput_.revision) { return; }
    forcePublish_ = false;
    // PublishSnapshot swaps storage with the mailbox: populate every field, even
    // when the previous buffer belonged to a different world or connection.
    building_.generation = generation_;
    building_.mode = mode_;
    building_.paused = mode_ == SessionMode::Local && paused_;
    building_.menuOpen = menuOpen_;
    building_.input = input;
    building_.runtimeSettings = options_.settings;
    building_.storageError = storageError_;
    building_.networkError = networkError_;
    building_.profileError = profileError_;
    building_.hasSave = hasSave_ && mode_ != SessionMode::Guest;
    building_.connectionState = mode_ == SessionMode::Guest ? guest_->GetState() : network::ConnectionState::Disconnected;
    building_.hostPort = mode_ == SessionMode::Host ? host_->GetPort() : 0;
    building_.discoveryPort = mode_ == SessionMode::Host ? host_->GetDiscoveryPort() : discoveryPort_;
    if (discoveryActive_) { building_.sessions = discoveredSessions_; }
    else { building_.sessions.clear(); }
    building_.statusVersion = statusVersion_; building_.status = status_;
    building_.seed = 0; building_.generationVersion = 0;
    building_.interactions.clear(); building_.mapItems.clear();
    if (mode_ == SessionMode::Guest) {
        building_.view = guest_->HasView() ? guest_->View() : WorldView{};
        building_.runtimeSettings.pacing = building_.view.pacing;
        if (guest_->HasView()) {
            building_.interactions = guest_->Presentation().interactions;
            building_.mapItems = guest_->Presentation().mapItems;
            CropView(building_.view, building_.mapItems, options_.settings.viewRadius);
        }
    } else if (server_) {
        building_.seed = server_->GetWorld().GetSeed();
        building_.generationVersion = server_->GetWorld().GetGenerationVersion();
        const auto runtime = ResolveSettingsForServer(*server_, {}, {});
        building_.runtimeSettings.activeRadius = runtime.activeRadius;
        building_.runtimeSettings.residencyMode = runtime.residencyMode;
        building_.runtimeSettings.residentRadius = runtime.residentRadius;
        building_.runtimeSettings.residentRadiusTiles = runtime.residentRadiusTiles;
        building_.runtimeSettings.seed = building_.seed;
        building_.runtimeSettings.pacing = server_->GetPacing();
        building_.view = server_->View(options_.settings.viewRadius);
        if (CapturePlayerPresentation(server_->GetWorld(), building_.view, presentation_)) {
            building_.interactions = presentation_.interactions;
            building_.mapItems = presentation_.mapItems;
            if (presentationOverflow_) {
                presentationOverflow_ = false;
                if (networkError_ == network::NetworkError::SnapshotTooLarge) { networkError_.reset(); }
                Status("Local presentation is available again");
                Status("Player presentation available");
            }
        } else {
            networkError_ = network::NetworkError::SnapshotTooLarge;
            if (!presentationOverflow_) {
                presentationOverflow_ = true;
                Status("Player presentation exceeds its size budget; map visuals and interactions unavailable");
            }
        }
    } else { building_.view = {}; }
    building_.networkError = networkError_;
    building_.statusVersion = statusVersion_; building_.status = status_;
    channels_.PublishSnapshot(building_);
    publishedRevision_ = revision; publishedInput_ = input;
    Notify();
}

void SessionOwner::Receipt(const PlayerCommandResult& result) {
    if (!pending_ || pending_->sequence != result.sequence) { return; }
    if (mode_ != SessionMode::Guest && result.player != server_->GetHostPlayerId()) { return; }
    if (result.result.outcome == Outcome::queued) {
        if (!pending_->admitted) {
            Reply({ReplyKind::Admission, pending_->generation, pending_->id, result.result}, false);
            pending_->admitted = true;
        }
    } else {
        const auto pending = *pending_;
        pending_.reset();
        if (mode_ == SessionMode::Local) { FinishItemRecovery(); }
        forcePublish_ = true;
        Publish(); // Completion must expose authoritative context before releasing input ownership.
        Reply({ReplyKind::Completed, pending.generation, pending.id, result.result}, !pending.admitted);
    }
}

void SessionOwner::FinishItemRecovery() {
    if (!itemRecovery_ || pending_) { return; }
    bool ready = false;
    for (const auto& player : server_->GetPlayers()) {
        if (player.id == server_->GetHostPlayerId()) { ready = player.ready; }
    }
    if (!ready) { return; }
    itemRecovery_ = false; paused_ = true; menuOpen_ = true;
    server_->SetPacing(itemPacing_);
    forcePublish_ = true;
    if (itemPauseId_) {
        Publish(); Reply({ReplyKind::Paused, generation_, *itemPauseId_}); itemPauseId_.reset();
    }
}

void SessionOwner::Inspection(PlayerInspection inspection, const Pending& pending) {
    SessionReply reply{ReplyKind::Inspected, pending.generation, pending.id};
    switch (inspection.status) {
    case InspectionStatus::Complete:
        DescribeTile(content_, inspection.view, inspection.cursor, inspection.details, reply.details);
        reply.inspectionView = eastl::move(inspection.view);
        reply.inspectionItems = eastl::move(inspection.mapItems);
        break;
    case InspectionStatus::TooLarge:
        reply.networkError = network::NetworkError::SnapshotTooLarge;
        reply.details.push_back("Inspection too large"); break;
    case InspectionStatus::Unavailable:
        reply.details.push_back("Inspection unavailable"); break;
    case InspectionStatus::Disconnected:
        reply.networkError = network::NetworkError::Disconnected;
        reply.details.push_back("Inspection disconnected"); break;
    }
    Reply(eastl::move(reply));
}

void SessionOwner::Settle() {
    const auto now = Clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - baseline_).count();
    baseline_ = now;
    const auto delta = static_cast<std::uint64_t>(elapsed > 0 ? elapsed : 0);
    if (discoveryActive_) {
        discovery_->Pump(delta);
        discoveryElapsed_ += delta;
        if (discoveryElapsed_ >= 2000000) {
            discoveryElapsed_ = 0;
            if (auto refreshed = discovery_->Refresh(discoveryPort_, discoveryAddress_.empty() ? nullptr : discoveryAddress_.c_str()); !refreshed) {
                networkError_ = refreshed.error(); Status(network::NetworkErrorMessage(refreshed.error()));
            }
        }
        if (discoveredSessions_ != discovery_->Sessions()) {
            discoveredSessions_ = discovery_->Sessions();
            forcePublish_ = true;
        }
    }
    if (mode_ == SessionMode::Guest) {
        const auto before = guest_->GetState();
        guest_->Pump(delta);
        const auto state = guest_->GetState();
        if (before != state) {
            Status(state == network::ConnectionState::Active ? "Joined LAN game" : "Joining LAN game");
        }
        const auto token = guest_->GetReconnectToken();
        if (token && token != persistedToken_ && (state == network::ConnectionState::Joining || state == network::ConnectionState::Active)) {
            const auto* saved = profile_.Find(connection_.address, connection_.port);
            if (!saved || saved->token != token) {
                auto updated = profile_;
                auto result = updated.Set({connection_.address, connection_.port, token, connection_.playerName});
                if (result) { result = updated.Save(options_.profilePath.c_str()); }
                if (!result) {
                    profileError_ = result.error();
                    Status("LAN identity could not be saved; local game restored");
                    ReturnLocal(); return;
                }
                profile_ = eastl::move(updated);
            }
            persistedToken_ = token;
        }
        for (const auto& result : guest_->TakeResults()) { Receipt(result); }
        PlayerInspection inspected;
        if (guest_->TryTakeInspection(inspected) && inspection_) {
            Inspection(eastl::move(inspected), *inspection_); inspection_.reset();
        }
        if (state == network::ConnectionState::Disconnected || state == network::ConnectionState::Rejected) {
            networkError_ = guest_->GetError();
            Status(network::NetworkErrorMessage(*networkError_));
            ReturnLocal();
        }
    } else if (server_ && (mode_ == SessionMode::Host || !paused_)) {
        if (mode_ == SessionMode::Host) {
            const auto update = host_->Pump(delta);
            if (update.storageError) { storageError_ = update.storageError; forcePublish_ = true; }
            if (update.networkError) { networkError_ = update.networkError; Status(network::NetworkErrorMessage(*networkError_)); }
            for (const auto& result : update.commandResults) { Receipt(result); }
        } else {
            const auto update = server_->Update(delta);
            if (update.storageError) { storageError_ = update.storageError; forcePublish_ = true; }
            for (const auto& result : update.commandResults) { Receipt(result); }
            FinishItemRecovery();
        }
    }
}

void SessionOwner::Gameplay(SessionRequest request) {
    const bool lan = mode_ != SessionMode::Local;
    const auto player = mode_ == SessionMode::Guest
        ? (guest_->HasView() ? guest_->View().playerId : 0) : server_->GetHostPlayerId();
    if (pending_ || itemRecovery_ || request.player != player || !player
        || (request.kind == RequestKind::ItemCommand && !(lan ? menuOpen_ : paused_))
        || (!lan && paused_ && request.kind != RequestKind::ResumeAndCommand && request.kind != RequestKind::ItemCommand)) {
        Reply({ReplyKind::Cancelled, generation_, request.id}); return;
    }
    if (!lan && request.kind == RequestKind::ItemCommand) {
        itemPacing_ = server_->GetPacing(); auto pacing = itemPacing_; pacing.pauseWhenIdle = true;
        server_->SetPacing(pacing); itemRecovery_ = true;
    }
    if (request.kind == RequestKind::ResumeAndCommand || request.kind == RequestKind::ItemCommand) {
        paused_ = false;
        if (request.kind == RequestKind::ResumeAndCommand) { menuOpen_ = false; }
        if (!lan) { baseline_ = Clock::now(); }
        forcePublish_ = true;
    }
    if (mode_ == SessionMode::Host) {
        const auto sequence = host_->QueueLocal(eastl::move(request.command), request.opportunity);
        if (!sequence) {
            CommandResult result; result.outcome = request.opportunity ? Outcome::unavailable : Outcome::stale_input;
            Reply({ReplyKind::Completed, generation_, request.id, result}); return;
        }
        pending_ = Pending{request.id, generation_, sequence};
        // Pump at the next owner boundary, with that interval's elapsed exactly once.
    } else if (mode_ == SessionMode::Guest) {
        const auto submitted = guest_->Submit(eastl::move(request.command), request.opportunity);
        if (!submitted) {
            SessionReply reply{ReplyKind::Completed, generation_, request.id};
            reply.networkError = submitted.error();
            reply.result.outcome = request.opportunity ? Outcome::unavailable : Outcome::stale_input;
            Reply(eastl::move(reply)); return;
        }
        pending_ = Pending{request.id, generation_, *submitted};
    } else {
        sequence_ = eastl::max(sequence_, Input().lastInputSequence);
        const auto result = server_->Submit(player, {++sequence_, request.opportunity, eastl::move(request.command)});
        pending_ = Pending{request.id, generation_, sequence_};
        Receipt({player, sequence_, request.opportunity, result});
        if (result.outcome == Outcome::queued) {
            const auto update = server_->Update(0);
            for (const auto& completed : update.commandResults) { Receipt(completed); }
        }
        FinishItemRecovery();
    }
}

void SessionOwner::Request(SessionRequest request) {
    if (channels_.Stopping()) { Reply({ReplyKind::Cancelled, request.generation, request.id}); return; }
    if (request.generation != generation_) { Reply({ReplyKind::Stale, request.generation, request.id}); return; }
    const bool lan = mode_ != SessionMode::Local;
    SessionReply reply{ReplyKind::ControlFailed, generation_, request.id};
    switch (request.kind) {
    case RequestKind::OpenLan:
        if (Open(eastl::move(request.hostSettings), reply)) { reply.kind = ReplyKind::ControlCompleted; }
        break;
    case RequestKind::CloseLan:
        if (pending_) { reply.networkError = network::NetworkError::Busy; break; }
        if (mode_ == SessionMode::Host) {
            // Closing parks remote players before the checkpoint is captured.
            CancelPending(); host_->Close(); mode_ = SessionMode::Local;
            paused_ = menuOpen_; forcePublish_ = true;
            reply.kind = ReplyKind::ControlCompleted;
            if (hasSave_) { Save(reply); }
            Status("LAN hosting closed");
        }
        break;
    case RequestKind::Connect:
        if (Connect(eastl::move(request.connectionSettings), request.newCharacter, reply)) { reply.kind = ReplyKind::ControlCompleted; }
        reply.generation = generation_;
        break;
    case RequestKind::Disconnect:
        if (mode_ == SessionMode::Guest) {
            Status("Disconnected; local game restored");
            reply.kind = ReturnLocal() ? ReplyKind::ControlCompleted : ReplyKind::ControlFailed;
            reply.generation = generation_;
        }
        break;
    case RequestKind::RefreshDiscovery:
        if (Discover(request.hostSettings, eastl::move(request.discoveryAddress), reply)) { reply.kind = ReplyKind::ControlCompleted; }
        break;
    case RequestKind::StopDiscovery:
        if (discovery_) { discovery_->Close(); }
        discoveredSessions_.clear();
        discoveryActive_ = false; forcePublish_ = true; reply.kind = ReplyKind::ControlCompleted; break;
    case RequestKind::Save: Save(reply); break;
    case RequestKind::InspectAi:
        reply.kind = mode_ != SessionMode::Guest && (paused_ || mode_ == SessionMode::Host) ? ReplyKind::AiInspected : ReplyKind::Cancelled;
        if (reply.kind == ReplyKind::AiInspected) { CaptureAiDebugView(server_->GetWorld(), request.cursor, reply.aiDebug); }
        break;
    case RequestKind::Inspect:
        if (mode_ == SessionMode::Guest) {
            if (inspection_) { reply.networkError = network::NetworkError::Busy; reply.kind = ReplyKind::Inspected; break; }
            const auto result = guest_->Inspect(++inspectionSequence_, request.cursor, options_.settings.viewRadius);
            if (!result) { reply.kind = ReplyKind::Inspected; reply.networkError = result.error(); break; }
            inspection_ = Pending{request.id, generation_, inspectionSequence_}; return;
        }
        reply.kind = ReplyKind::Inspected;
        if (paused_ || mode_ == SessionMode::Host) {
            PlayerInspection inspected;
            CapturePlayerInspection(*server_, server_->GetHostPlayerId(), request.id, request.cursor, options_.settings.viewRadius, inspected);
            Inspection(eastl::move(inspected), {request.id, generation_, 0}); return;
        }
        break;
    case RequestKind::ApplySettings: {
        reply.kind = ReplyKind::SettingsFailed;
        Diagnostics diagnostics;
        if (mode_ == SessionMode::Guest) {
            const auto effective = ResolveSettings(request.globalSettings, request.worldSettings);
            auto permitted = options_.settings;
            permitted.language = effective.language; permitted.viewRadius = effective.viewRadius;
            permitted.keyboardRepeatRate = effective.keyboardRepeatRate; permitted.messageHistorySize = effective.messageHistorySize;
            auto oldGlobal = options_.globalSettings;
            auto oldWorld = options_.worldSettings;
            auto newGlobal = request.globalSettings;
            auto newWorld = request.worldSettings;
            for (auto* overrides : {&oldGlobal, &oldWorld, &newGlobal, &newWorld}) {
                overrides->language.reset(); overrides->viewRadius.reset();
                overrides->keyboardRepeatRate.reset(); overrides->messageHistorySize.reset();
            }
            const bool unchangedControls = oldGlobal == newGlobal && oldWorld == newWorld;
            if ((unchangedControls || effective == permitted) && ValidateSettings(permitted, &diagnostics)
                && ValidateOverrides(request.globalSettings, &diagnostics) && ValidateOverrides(request.worldSettings, &diagnostics)) {
                options_.settings = permitted; options_.globalSettings = request.globalSettings; options_.worldSettings = request.worldSettings;
                reply.kind = ReplyKind::SettingsApplied;
                reply.runtimeSettings = permitted;
                if (guest_->HasView()) { reply.runtimeSettings.pacing = guest_->View().pacing; }
                forcePublish_ = true;
            } else { reply.details.push_back("Guests may change presentation settings only"); }
        } else if ((paused_ || mode_ == SessionMode::Host) && !itemRecovery_
            && ApplySettings(*server_, request.globalSettings, request.worldSettings, &diagnostics)) {
            options_.settings = ResolveSettingsForServer(*server_, request.globalSettings, request.worldSettings);
            options_.globalSettings = request.globalSettings; options_.worldSettings = request.worldSettings;
            reply.kind = ReplyKind::SettingsApplied; reply.runtimeSettings = options_.settings; forcePublish_ = true;
        } else { reply.details.push_back(FormatDiagnostics(diagnostics)); }
        break;
    }
    case RequestKind::PauseForItems:
        if (!lan) {
            if (itemRecovery_ || pending_ || request.player != server_->GetHostPlayerId()) { reply.kind = ReplyKind::Cancelled; break; }
            itemPacing_ = server_->GetPacing(); auto pacing = itemPacing_; pacing.pauseWhenIdle = true;
            server_->SetPacing(pacing); itemRecovery_ = true; itemPauseId_ = request.id;
            paused_ = false; menuOpen_ = true; baseline_ = Clock::now(); FinishItemRecovery(); return;
        }
        [[fallthrough]];
    case RequestKind::Pause:
    case RequestKind::Resume:
        if (itemRecovery_ || (!lan && pending_ && request.kind == RequestKind::Pause)) {
            // A buffered intent must retain a running owner until it completes.
            reply.kind = ReplyKind::Cancelled; break;
        }
        menuOpen_ = request.kind != RequestKind::Resume;
        paused_ = !lan && menuOpen_;
        if (!lan) { baseline_ = Clock::now(); }
        forcePublish_ = true;
        reply.kind = !menuOpen_ ? ReplyKind::Resumed : lan ? ReplyKind::MenuOpened : ReplyKind::Paused;
        break;
    case RequestKind::Command:
    case RequestKind::ItemCommand:
    case RequestKind::ResumeAndCommand:
        Gameplay(eastl::move(request)); return;
    }
    if (reply.networkError) { networkError_ = reply.networkError; Status(network::NetworkErrorMessage(*reply.networkError)); }
    if (reply.storageError) { storageError_ = reply.storageError; forcePublish_ = true; }
    Publish(); Reply(eastl::move(reply));
}

void SessionOwner::Run() {
    SessionReply started{ReplyKind::Started, generation_};
    Diagnostics diagnostics;
    bool okay = true;
    if ((options_.host && options_.join) || (!options_.savePath.empty() && !options_.loadPath.empty())
        || (options_.join && (!options_.savePath.empty() || !options_.loadPath.empty())) || (options_.newCharacter && !options_.join)) {
        okay = false; started.startupError = StartupError::InvalidSettings;
        started.details.push_back("Incompatible session startup flags");
    } else if (!ValidateSettings(options_.settings, &diagnostics)
        || (options_.settingsOverridesProvided && (!ValidateOverrides(options_.globalSettings, &diagnostics)
            || !ValidateOverrides(options_.worldSettings, &diagnostics)))) {
        okay = false; started.startupError = StartupError::InvalidSettings;
        started.details.push_back(FormatDiagnostics(diagnostics));
    }
    if (auto loaded = profile_.Load(options_.profilePath.c_str()); !loaded) {
        profileError_ = loaded.error(); Status("LAN profile could not be loaded; local play and hosting are available");
    }
    if (okay && options_.join) {
        okay = Connect(options_.connectionSettings, options_.newCharacter, started);
        if (!okay && started.startupError == StartupError::None) { started.startupError = StartupError::Network; }
    } else if (okay) { okay = Local(started); }
    if (okay && options_.host) { okay = Open(options_.hostSettings, started); }
    if (okay && options_.discover) { okay = Discover(options_.hostSettings, options_.discoveryAddress, started); }
    if (!okay) {
        started.kind = ReplyKind::StartupFailed;
        if (started.startupError == StartupError::None) { started.startupError = StartupError::Network; }
        channels_.PublishReply(eastl::move(started));
        channels_.Stop();
        SessionRequest request;
        while (channels_.TakeRequest(request)) { Reply({ReplyKind::Cancelled, request.generation, request.id}); }
        if (host_) { host_->Close(); }
        if (server_ && hasSave_) {
            if (auto saved = server_->Save(); !saved) { shutdownError_ = saved.error(); }
        }
        Notify(); return;
    }
    // Startup joins publish the connection epoch just like explicit Connect.
    started.generation = generation_;
    channels_.PublishReply(eastl::move(started));
    Publish();
    baseline_ = Clock::now();
    for (;;) {
        SessionRequest request;
        while (channels_.TakeRequest(request)) {
            if (!channels_.Stopping()) { Settle(); }
            Request(eastl::move(request)); Publish();
        }
        if (channels_.Stopping()) { break; }
        Settle(); Publish(); channels_.Wait();
    }
    CancelPending();
    if (itemPauseId_) { Reply({ReplyKind::Cancelled, generation_, *itemPauseId_}); itemPauseId_.reset(); }
    if (guest_) { guest_->Disconnect(); }
    if (host_) { host_->Close(); }
    if (itemRecovery_ && server_) { server_->SetPacing(itemPacing_); }
    if (server_ && hasSave_) {
        if (auto saved = server_->Save(); !saved) { shutdownError_ = saved.error(); }
    }
    Notify();
}
}

Session::Session(const ContentCatalog& content, std::uint64_t generation, SessionOptions options,
    Wake wake, void* userdata) : wake_(wake), userdata_(userdata),
    thread_([this, &content, generation, options = eastl::move(options)] { Run(content, generation, options); }) {}
Session::~Session() { Stop(); }
EnqueueResult Session::TrySubmit(SessionRequest request) { return channels_.TrySubmit(eastl::move(request)); }
bool Session::TryPopReply(SessionReply& reply) { return channels_.TryPopReply(reply); }
bool Session::TakeSnapshot(SessionSnapshot& current) { return channels_.TakeSnapshot(current); }
eastl::optional<SaveError> Session::Stop() {
    channels_.Stop();
    if (thread_.joinable()) { thread_.join(); }
    return shutdownError_;
}
void Session::Notify() { if (wake_) { wake_(userdata_); } }
void Session::Run(const ContentCatalog& content, std::uint64_t generation, SessionOptions options) {
    SessionOwner owner(channels_, content, generation, eastl::move(options), wake_, userdata_, shutdownError_);
    owner.Run();
}
}
