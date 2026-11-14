#include "core/item_state.h"
#include "items/item_behavior.h"
#include "crafting/crafting_state.h"
#include <cstdlib>

namespace cataclysmx {
ItemRuntime::ItemRuntime() = default;
ItemRuntime::~ItemRuntime() = default;
ItemRuntime::ItemRuntime(ItemRuntime&&) noexcept = default;
ItemRuntime& ItemRuntime::operator=(ItemRuntime&&) noexcept = default;

ItemRuntime MakeItemState(const ItemDefinition& definition) {
    ItemRuntime result;
    if (definition.unfinishedCraft) { result.crafting_.reset(new CraftingJobState); }
    for (const auto& store : definition.energyStores) {
        result.energy_.push_back({store.capability, store.config.initialEnergy});
    }
    for (const auto& binding : definition.behaviors) {
        auto behavior = binding.definition->Create(binding.capability, nullptr, nullptr);
        if (!behavior) { std::abort(); }
        result.behaviors_.push_back(eastl::move(*behavior));
    }
    return result;
}
void WriteItemState(const ItemDefinition& definition, const ItemRuntime& state, DataObject& output) {
    DataObject result;
    if (state.crafting_) { result.Set("crafting_job", WriteCraftingState(*state.crafting_)); }
    for (std::size_t i = 0; i < definition.capabilities.size(); ++i) {
        const ItemCapabilityId id{static_cast<std::uint16_t>(i + 1)};
        for (const auto& store : state.energy_) {
            if (store.capability != id) { continue; }
            DataObject value;
            value.SetUnsigned("version", 1);
            value.SetUnsigned("energy", store.energy);
            result.Set(definition.capabilities[i].id, eastl::move(value));
        }
        for (const auto& behavior : state.behaviors_) {
            if (behavior->Capability() == id) {
                result.Set(definition.capabilities[i].id, behavior->SaveState());
            }
        }
    }
    output = eastl::move(result);
}
StateResult<ItemRuntime> ReadItemState(const ItemDefinition& definition, const DataObject& input,
    Diagnostics* diagnostics) {
    const auto invalid = [diagnostics, &definition](StateError error = StateError::InvalidData) -> eastl::unexpected<StateError> {
        if (diagnostics) {
            DiagnosticFrame frame;
            frame.message = "Invalid item capability state";
            DiagnosticLocation location;
            location.path = "items."; location.path += definition.id; location.path += ".state";
            frame.location = eastl::move(location);
            diagnostics->Add(eastl::move(frame));
        }
        return eastl::unexpected(error);
    };
    if (input.Type() != DataType::Object
        || input.Size() != definition.energyStores.size() + definition.behaviors.size()
            + (definition.unfinishedCraft ? 1 : 0)) { return invalid(); }
    ItemRuntime result;
    if (definition.unfinishedCraft) {
        const auto* entry = input.Find("crafting_job");
        if (!entry) { return invalid(); }
        auto state = ReadCraftingState(*entry);
        if (!state) { return invalid(state.error()); }
        result.crafting_.reset(new CraftingJobState(eastl::move(*state)));
    }
    for (const auto& store : definition.energyStores) {
        const auto* cap = TryGetItemCapability(definition, store.capability);
        const auto* value = input.Find(cap->id);
        if (!value || value->Type() != DataType::Object) { return invalid(); }
        const auto version = value->GetUnsigned("version");
        if (!version) { return invalid(); }
        if (*version != 1) { return invalid(StateError::UnsupportedVersion); }
        const auto energy = value->GetUnsigned("energy");
        if (!energy || *energy > store.config.capacityEnergy || value->Size() != 2) { return invalid(); }
        result.energy_.push_back({store.capability, static_cast<std::uint32_t>(*energy)});
    }
    for (const auto& binding : definition.behaviors) {
        const auto* cap = TryGetItemCapability(definition, binding.capability);
        const auto* value = input.Find(cap->id);
        if (!value) { return invalid(); }
        auto behavior = binding.definition->Create(binding.capability, value, diagnostics);
        if (!behavior) { return invalid(behavior.error()); }
        result.behaviors_.push_back(eastl::move(*behavior));
    }
    return result;
}
}
