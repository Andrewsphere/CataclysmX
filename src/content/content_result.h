#pragma once

#include <EABase/eabase.h>
#include <EASTL/expected.h>

namespace cataclysmx {
enum class ContentError { Io, InvalidData, UnsupportedVersion };
template<class T> using ContentResult = eastl::expected<T, ContentError>;
}
