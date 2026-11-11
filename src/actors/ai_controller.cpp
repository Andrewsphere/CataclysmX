#include "actors/ai_controller.h"
#include "core/state_reader.h"
#include <cstdlib>

#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
#error Engine must compile without exceptions
#endif

namespace cataclysmx {
namespace {

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
    return cataclysmx::InvalidState(diagnostics, "actors.controller", "type", "unknown controller kind");
}

StateResult<void> ValidateAiControllerState(AiControllerKind kind, const DataObject& state,
    Diagnostics* diagnostics) {
    StateReader reader(state, diagnostics, "actors.controller.state");
    switch (kind) {
    case AiControllerKind::None: {
        const auto result = reader.Result();
        if (!result) { return result; }
        if (state.Size() != 0) { return reader.Invalid({}, "none controller state must be empty"); }
        return {};
    }
    case AiControllerKind::Wander: {
        const auto version = reader.GetUnsignedOrError("version");
        const auto result = reader.Result();
        if (!result) { return result; }
        if (version != 1) {
            return reader.Invalid("version", "unsupported controller state version", StateError::UnsupportedVersion);
        }
        if (state.Size() != 1) { return reader.Invalid({}, "unknown controller state field"); }
        return {};
    }
    }
    return cataclysmx::InvalidState(diagnostics, "actors.controller", "type", "unknown controller kind");
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
    return cataclysmx::InvalidState(diagnostics, "actors.controller", "type", "unknown controller kind");
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
    StateReader reader(envelope, diagnostics, "actors.controller");
    const auto name = reader.GetStringOrError("type");
    const auto* state = reader.GetObjectOrError("state");
    const auto result = reader.Result();
    if (!result) { return eastl::unexpected(result.error()); }
    if (envelope.Size() != 2) { return reader.Invalid({}, "unknown controller envelope field"); }
    const auto kind = cataclysmx::ReadAiControllerKind(name, diagnostics);
    if (!kind) { return eastl::unexpected(kind.error()); }
    return cataclysmx::MakeAiController(*kind, state, diagnostics);
}

}
