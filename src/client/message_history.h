#pragma once

#include "core/containers.h"

#include <EASTL/span.h>
#include <EASTL/string_view.h>

#include <cstdint>

namespace cataclysmx {

enum class Outcome;

struct MessageHistoryEntry {
    String text;
    std::uint64_t count = 1;
};

bool ShouldRecordOutcome(Outcome outcome);
void AppendMessageHistoryEntry(String& output, const MessageHistoryEntry& entry);

// Frontend event history, oldest first, with adjacent identical text grouped.
class MessageHistory {
public:
    void SetCapacity(int capacity);
    void Add(eastl::string_view message);
    eastl::span<const MessageHistoryEntry> Entries() const;
private:
    int capacity_ = 10;
    SmallVector<MessageHistoryEntry, 10> entries_;
};

}
