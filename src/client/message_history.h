#pragma once

#include "core/containers.h"

#include <EASTL/span.h>
#include <EASTL/string_view.h>

namespace cataclysmx {

// Frontend event history, oldest first. Repeated messages are distinct events.
class MessageHistory {
public:
    void SetCapacity(int capacity);
    void Add(eastl::string_view message);
    eastl::span<const String> Entries() const;
private:
    int capacity_ = 10;
    SmallVector<String, 10> entries_;
};

}
