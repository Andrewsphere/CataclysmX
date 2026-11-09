#pragma once

#include <EASTL/expected.h>

namespace cataclysmx {
enum class WorldError {
    invalid_position, invalid_region, invalid_handle, chunk_unavailable,
    blocked, occupied, containment_cycle, not_container, invalid_request,
    storage_failure, capacity_exceeded, incompatible_item
};
template<class T> using WorldResult = eastl::expected<T, WorldError>;
}
