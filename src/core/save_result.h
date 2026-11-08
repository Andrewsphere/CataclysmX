#pragma once

#include "core/diagnostics.h"
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

}
