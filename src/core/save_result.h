#pragma once

#include "core/containers.h"
#include <EASTL/expected.h>

namespace cataclysmx {

enum class SaveError {
    Io,
    Busy,
    InvalidData,
    UnsupportedVersion,
    AlreadyExists,
    InvalidState,
    NotOpen
};

template<class T>
using SaveResult = eastl::expected<T, SaveError>;

struct Diagnostics {
    String message;
};

}
