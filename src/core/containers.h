#pragma once

#include <EASTL/allocator_malloc.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <cstdlib>

namespace cataclysmx {

// allocator_malloc may return null, while EASTL requires successful allocation.
// This small adapter makes OOM fatal in release too, without per-TU changes to
// EASTL assertions or a separate allocation system. Current game data needs only
// ordinary alignment; unsupported aligned allocations also fail fast.
class FatalAllocator : public eastl::allocator_malloc {
public:
    using eastl::allocator_malloc::allocator_malloc;

    // Lowercase allocate is required by EASTL's allocator interface.
    void* allocate(std::size_t size, int flags = 0) {
        void* result = eastl::allocator_malloc::allocate(size, flags);
        if (!result) { std::abort(); }
        return result;
    }
    void* allocate(std::size_t size, std::size_t alignment, std::size_t offset, int flags = 0) {
        void* result = eastl::allocator_malloc::allocate(size, alignment, offset, flags);
        if (!result) { std::abort(); }
        return result;
    }
};

template<class T>
using Vector = eastl::vector<T, FatalAllocator>;

using String = eastl::basic_string<char, FatalAllocator>;

}
