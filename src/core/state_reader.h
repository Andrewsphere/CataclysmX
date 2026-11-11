#pragma once

#include "core/data_object.h"
#include "core/state_result.h"

namespace cataclysmx {

// Formats a diagnostic only when requested. Path and message are borrowed.
eastl::unexpected<StateError> InvalidState(Diagnostics* diagnostics,
    eastl::string_view prefix, eastl::string_view field, eastl::string_view message,
    StateError error = StateError::InvalidData);

// Borrows input and prefix; check Result() before semantic validation or commit.
// OrDefault uses fallback only for absence. Ignore getter values after failure.
class StateReader {
public:
    StateReader(const DataObject* input, Diagnostics* diagnostics = nullptr,
        eastl::string_view prefix = {});
    StateReader(const DataObject& input, Diagnostics* diagnostics = nullptr,
        eastl::string_view prefix = {});

    std::uint64_t GetUnsignedOrDefault(eastl::string_view field, std::uint64_t fallback);
    std::uint64_t GetUnsignedOrError(eastl::string_view field);
    bool GetBooleanOrDefault(eastl::string_view field, bool fallback);
    bool GetBooleanOrError(eastl::string_view field);
    eastl::string_view GetStringOrDefault(eastl::string_view field, eastl::string_view fallback);
    eastl::string_view GetStringOrError(eastl::string_view field);
    const DataObject* GetObjectOrError(eastl::string_view field);

    StateResult<void> Result() const;
    eastl::unexpected<StateError> Invalid(eastl::string_view field, eastl::string_view message,
        StateError error = StateError::InvalidData);

private:
    DataResult<const DataObject*> Find(eastl::string_view field, DataType type, bool required);
    const DataObject* input_;
    Diagnostics* diagnostics_;
    eastl::string_view prefix_;
    StateResult<void> result_;
};

}
