#pragma once

#include "core/containers.h"
#include <EASTL/expected.h>

namespace cataclysmx {

enum class SaveError {
    io,
    busy,
    invalid_data,
    unsupported_version,
    already_exists,
    invalid_state,
    not_open
};

template<class T>
using SaveResult = eastl::expected<T, SaveError>;

struct Diagnostics {
    String message;
};

}
