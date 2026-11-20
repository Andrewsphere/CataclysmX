#include "core/item_state.h"
#include "content/content.h"
#include "items/item_behavior.h"
#include "crafting/crafting_state.h"
#include <cstdlib>
#include <limits>
#include <EASTL/sort.h>

namespace cataclysmx {
ItemRuntime::ItemRuntime() = default;
ItemRuntime::~ItemRuntime() = default;
ItemRuntime::ItemRuntime(ItemRuntime&&) noexcept = default;
ItemRuntime& ItemRuntime::operator=(ItemRuntime&&) noexcept = default;

ItemRuntime MakeItemState(const ItemDefinition& definition) {
    ItemRuntime result;
    if (definition.digitalDevice != DigitalDeviceKind::None) {
        result.information_.reset(new InformationLibrary);
        result.information_->packages = definition.suppliedInformation;
    }
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
void WriteItemStateImpl(const ContentCatalog* catalog, const ItemDefinition& definition, const ItemRuntime& state, DataObject& output) {
    DataObject result;
    if (state.information_) {
        if (!catalog || definition.digitalDevice == DigitalDeviceKind::None) { std::abort(); }
        DataObject library;
        library.SetUnsigned("version", 1);
        DataObject packages = DataObject::List();
        for (auto id : state.information_->packages) {
            packages.Append(DataObject::Text(catalog->GetInformation(id).id));
        }
        library.Set("packages", eastl::move(packages));
        if (state.information_->transfer) {
            const auto& transfer = *state.information_->transfer;
            DataObject value;
            value.SetUnsigned("worker", transfer.worker);
            value.SetUnsigned("source_item", transfer.sourceItem);
            value.SetUnsigned("source_object", transfer.sourceObject);
            value.SetUnsigned("host_item", transfer.hostItem);
            value.SetString("package", transfer.packageName);
            value.SetUnsigned("remaining_ticks", transfer.remainingTicks);
            library.Set("transfer", eastl::move(value));
        }
        result.Set("information_library", eastl::move(library));
    } else if (definition.digitalDevice != DigitalDeviceKind::None) { std::abort(); }
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
StateResult<ItemRuntime> ReadItemStateImpl(const ContentCatalog* catalog, const ItemDefinition& definition, const DataObject& input,
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
            + (definition.unfinishedCraft ? 1 : 0)
            + (definition.digitalDevice != DigitalDeviceKind::None ? 1 : 0)) { return invalid(); }
    ItemRuntime result;
    if (definition.digitalDevice != DigitalDeviceKind::None) {
        if (!catalog) { return invalid(); }
        const auto* library = input.Find("information_library");
        if (!library || library->Type() != DataType::Object) { return invalid(); }
        const auto version = library->GetUnsigned("version");
        if (!version) { return invalid(); }
        if (*version != 1) { return invalid(StateError::UnsupportedVersion); }
        const auto* packages = library->Find("packages");
        const auto* transfer = library->Find("transfer");
        if (!packages || packages->Type() != DataType::List || packages->Size() > catalog->InformationCount()
            || library->Size() != (transfer ? 3 : 2)) { return invalid(); }
        result.information_.reset(new InformationLibrary);
        auto& output = result.information_->packages;
        for (std::size_t i = 0; i < packages->Size(); ++i) {
            const auto& value = packages->At(i);
            if (value.Type() != DataType::String) { return invalid(); }
            const auto id = catalog->FindInformation(value.StringValue());
            if (!id.value) { return invalid(); }
            output.push_back(id);
        }
        eastl::sort(output.begin(), output.end(), [](auto a, auto b) { return a.value < b.value; });
        for (std::size_t i = 1; i < output.size(); ++i) {
            if (output[i] == output[i - 1]) { return invalid(); }
        }
        if (transfer) {
            if (transfer->Type() != DataType::Object || transfer->Size() != 6) { return invalid(); }
            const auto worker = transfer->GetUnsigned("worker");
            const auto sourceItem = transfer->GetUnsigned("source_item");
            const auto sourceObject = transfer->GetUnsigned("source_object");
            const auto hostItem = transfer->GetUnsigned("host_item");
            const auto package = transfer->GetString("package");
            const auto remaining = transfer->GetUnsigned("remaining_ticks");
            if (!worker || !*worker || !sourceItem || !sourceObject || (!*sourceItem == !*sourceObject)
                || !hostItem || !package || !remaining
                || *remaining > UINT32_MAX) { return invalid(); }
            const auto id = catalog->FindInformation(*package);
            if (!id.value) { return invalid(); }
            for (auto present : output) { if (present == id) { return invalid(); } }
            InformationTransferState state;
            state.worker = *worker;
            state.sourceItem = *sourceItem;
            state.sourceObject = *sourceObject;
            state.hostItem = *hostItem;
            state.packageName.assign(package->data(), package->size());
            state.remainingTicks = static_cast<Tick>(*remaining);
            result.information_->transfer = eastl::move(state);
        }
    }
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
void WriteItemState(const ItemDefinition& definition, const ItemRuntime& state, DataObject& output) {
    WriteItemStateImpl(nullptr, definition, state, output);
}
void WriteItemState(const ContentCatalog& catalog, const ItemDefinition& definition,
    const ItemRuntime& state, DataObject& output) {
    WriteItemStateImpl(&catalog, definition, state, output);
}
StateResult<ItemRuntime> ReadItemState(const ItemDefinition& definition, const DataObject& input, Diagnostics* diagnostics) {
    return ReadItemStateImpl(nullptr, definition, input, diagnostics);
}
StateResult<ItemRuntime> ReadItemState(const ContentCatalog& catalog, const ItemDefinition& definition,
    const DataObject& input, Diagnostics* diagnostics) {
    return ReadItemStateImpl(&catalog, definition, input, diagnostics);
}
}
