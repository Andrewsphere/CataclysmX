#include "core/state_reader.h"
#include <EASTL/utility.h>

namespace cataclysmx {

eastl::unexpected<StateError> InvalidState(Diagnostics* diagnostics,
    eastl::string_view prefix, eastl::string_view field, eastl::string_view message,
    StateError error) {
    if (diagnostics) {
        DiagnosticFrame frame;
        frame.message.assign(message.data(), message.size());
        DiagnosticLocation location;
        if (!prefix.empty()) { location.path.assign(prefix.data(), prefix.size()); }
        if (!field.empty()) {
            if (!location.path.empty()) { location.path += '.'; }
            location.path.append(field.data(), field.size());
        }
        frame.location = eastl::move(location);
        diagnostics->Add(eastl::move(frame));
    }
    return eastl::unexpected(error);
}

StateReader::StateReader(const DataObject* input, Diagnostics* diagnostics, eastl::string_view prefix)
    : input_(input), diagnostics_(diagnostics), prefix_(prefix) {
    if (!input_ || input_->Type() != DataType::Object) {
        Invalid({}, "expected an object state");
    }
}

StateReader::StateReader(const DataObject& input, Diagnostics* diagnostics, eastl::string_view prefix)
    : StateReader(&input, diagnostics, prefix) {}

DataResult<const DataObject*> StateReader::Find(eastl::string_view field, DataType type, bool required) {
    if (!input_ || input_->Type() != DataType::Object) {
        return eastl::unexpected(DataError::WrongType);
    }
    const auto value = input_->Get(field, type);
    if (value) { return value; }
    if (required || value.error() != DataError::Absent) {
        Invalid(field, value.error() == DataError::Absent ? "required state field is absent"
            : "wrong state field type");
    }
    return value;
}

std::uint64_t StateReader::GetUnsignedOrDefault(eastl::string_view field, std::uint64_t fallback) {
    const auto value = Find(field, DataType::Unsigned, false);
    if (value) { return (*value)->UnsignedValue(); }
    return value.error() == DataError::Absent ? fallback : 0;
}
std::uint64_t StateReader::GetUnsignedOrError(eastl::string_view field) {
    const auto value = Find(field, DataType::Unsigned, true);
    return value ? (*value)->UnsignedValue() : 0;
}
bool StateReader::GetBooleanOrDefault(eastl::string_view field, bool fallback) {
    const auto value = Find(field, DataType::Boolean, false);
    if (value) { return (*value)->BooleanValue(); }
    return value.error() == DataError::Absent ? fallback : false;
}
bool StateReader::GetBooleanOrError(eastl::string_view field) {
    const auto value = Find(field, DataType::Boolean, true);
    return value ? (*value)->BooleanValue() : false;
}
eastl::string_view StateReader::GetStringOrDefault(eastl::string_view field, eastl::string_view fallback) {
    const auto value = Find(field, DataType::String, false);
    if (value) { return eastl::string_view((*value)->StringValue()); }
    return value.error() == DataError::Absent ? fallback : eastl::string_view{};
}
eastl::string_view StateReader::GetStringOrError(eastl::string_view field) {
    const auto value = Find(field, DataType::String, true);
    return value ? eastl::string_view((*value)->StringValue()) : eastl::string_view{};
}
const DataObject* StateReader::GetObjectOrError(eastl::string_view field) {
    const auto value = Find(field, DataType::Object, true);
    return value ? *value : nullptr;
}

StateResult<void> StateReader::Result() const { return result_; }

eastl::unexpected<StateError> StateReader::Invalid(eastl::string_view field,
    eastl::string_view message, StateError error) {
    if (result_) { result_ = cataclysmx::InvalidState(diagnostics_, prefix_, field, message, error); }
    return eastl::unexpected(result_.error());
}

}
