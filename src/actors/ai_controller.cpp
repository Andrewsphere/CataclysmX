#include "actors/ai_controller.h"
#include <cstdlib>

#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
#error Engine must compile without exceptions
#endif

namespace cataclysmx {
namespace {

eastl::unexpected<StateError> InvalidState(Diagnostics* diagnostics,
    eastl::string_view path, StateError error = StateError::InvalidData) {
    if (diagnostics) {
        DiagnosticFrame frame;
        frame.message = "Invalid actor controller state";
        DiagnosticLocation location;
        location.path.assign(path.data(), path.size());
        frame.location = eastl::move(location);
        diagnostics->Add(eastl::move(frame));
    }
    return eastl::unexpected(error);
}

class WanderController final : public AiController {
public:
    AiControllerKind Kind() const override { return AiControllerKind::Wander; }
    ActorIntent ChooseAction(const ActorReadContext&, std::uint32_t& rng) override {
        // Portable modulo-2^32 legacy draw and direction mapping. A blocked
        // attempt still consumes this draw; execution is the caller's job.
        rng = rng * 1'664'525U + 1'013'904'223U;
        constexpr TileCoord directions[] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
        return {ActorIntentKind::Move, directions[rng >> 30], 100};
    }
    DataObject SaveState() const override {
        DataObject state;
        state.SetUnsigned("version", 1);
        return state;
    }
};

}

ActorReadContext::ActorReadContext(const World& world, ActorHandle self)
    : world_(world), self_(self) {}
const World& ActorReadContext::GetWorld() const { return world_; }
ActorHandle ActorReadContext::Self() const { return self_; }
AiController::~AiController() = default;

eastl::string_view AiControllerKindName(AiControllerKind kind) {
    switch (kind) {
    case AiControllerKind::None: return "none";
    case AiControllerKind::Wander: return "wander";
    }
    return {};
}

StateResult<AiControllerKind> ReadAiControllerKind(eastl::string_view name,
    Diagnostics* diagnostics) {
    if (name == "none") { return AiControllerKind::None; }
    if (name == "wander") { return AiControllerKind::Wander; }
    return InvalidState(diagnostics, "actors.controller.type");
}

StateResult<void> ValidateAiControllerState(AiControllerKind kind, const DataObject& state,
    Diagnostics* diagnostics) {
    if (state.Type() != DataType::Object) {
        return InvalidState(diagnostics, "actors.controller.state");
    }
    switch (kind) {
    case AiControllerKind::None:
        if (state.Size() != 0) { return InvalidState(diagnostics, "actors.controller.state"); }
        return {};
    case AiControllerKind::Wander: {
        const auto version = state.GetUnsigned("version");
        if (!version) { return InvalidState(diagnostics, "actors.controller.state.version"); }
        if (*version != 1) {
            return InvalidState(diagnostics, "actors.controller.state.version", StateError::UnsupportedVersion);
        }
        if (state.Size() != 1) { return InvalidState(diagnostics, "actors.controller.state"); }
        return {};
    }
    }
    return InvalidState(diagnostics, "actors.controller.type");
}

StateResult<eastl::unique_ptr<AiController>> MakeAiController(AiControllerKind kind,
    const DataObject* state, Diagnostics* diagnostics) {
    if (state) {
        const auto valid = cataclysmx::ValidateAiControllerState(kind, *state, diagnostics);
        if (!valid) { return eastl::unexpected(valid.error()); }
    }
    switch (kind) {
    case AiControllerKind::None: return eastl::unique_ptr<AiController>{};
    case AiControllerKind::Wander: return eastl::unique_ptr<AiController>(new WanderController());
    }
    return InvalidState(diagnostics, "actors.controller.type");
}

DataObject CaptureAiController(const AiController* controller) {
    DataObject envelope;
    const AiControllerKind kind = controller ? controller->Kind() : AiControllerKind::None;
    const auto name = cataclysmx::AiControllerKindName(kind);
    // None is represented only by nullptr; invalid runtime kinds are invariants.
    if (name.empty() || (controller && kind == AiControllerKind::None)) { std::abort(); }
    envelope.SetString("type", name);
    envelope.Set("state", controller ? controller->SaveState() : DataObject{});
    return envelope;
}

StateResult<eastl::unique_ptr<AiController>> ReadAiController(const DataObject& envelope,
    Diagnostics* diagnostics) {
    if (envelope.Type() != DataType::Object || envelope.Size() != 2) {
        return InvalidState(diagnostics, "actors.controller");
    }
    const auto name = envelope.GetString("type");
    if (!name) { return InvalidState(diagnostics, "actors.controller.type"); }
    const auto kind = cataclysmx::ReadAiControllerKind(*name, diagnostics);
    if (!kind) { return eastl::unexpected(kind.error()); }
    const auto* state = envelope.Find("state");
    if (!state) { return InvalidState(diagnostics, "actors.controller.state"); }
    return cataclysmx::MakeAiController(*kind, state, diagnostics);
}

}
