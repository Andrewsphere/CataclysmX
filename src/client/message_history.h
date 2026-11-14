#pragma once

#include "core/containers.h"

#include <EASTL/string_view.h>

namespace cataclysmx {

// Frontend event history, oldest first. Repeated messages are distinct events.
class MessageHistory {
public:
    void SetCapacity(int capacity);
    void Add(eastl::string_view message);
    const Vector<String>& Entries() const;
private:
    int capacity_ = 10;
    Vector<String> entries_;
};

}
