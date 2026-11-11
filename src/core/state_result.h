#pragma once

#include <EASTL/expected.h>

namespace cataclysmx {

enum class StateError { InvalidData, UnsupportedVersion };
template<class T> using StateResult = eastl::expected<T, StateError>;

}
