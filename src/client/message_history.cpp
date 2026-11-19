#include "client/message_history.h"

#include "client/localization.h"
#include "core/server.h"

#include <EASTL/utility.h>

#include <cstdlib>
#include <limits>

namespace cataclysmx {

bool ShouldRecordOutcome(Outcome outcome) {
    switch (outcome) {
    case Outcome::moved:
    case Outcome::waited:
    case Outcome::queued:
    case Outcome::attacked:
    case Outcome::attack_missed:
    case Outcome::attack_parried:
    case Outcome::attack_blocked:
    case Outcome::target_defeated:
        return false;
    default:
        return true;
    }
}

void AppendMessageHistoryEntry(String& output, const MessageHistoryEntry& entry) {
    output += entry.text;
    if (entry.count > 1) {
        output += " ×";
        output += UnsignedText(entry.count);
    }
}

void MessageHistory::SetCapacity(int capacity) {
    if (capacity < 1 || capacity > 100) { std::abort(); }
    capacity_ = capacity;
    if (entries_.size() > static_cast<std::size_t>(capacity_)) {
        entries_.erase(entries_.begin(), entries_.end() - capacity_);
    }
}

void MessageHistory::Add(eastl::string_view message) {
    if (message.empty()) { return; }
    if (!entries_.empty()) {
        auto& newest = entries_.back();
        if (message == eastl::string_view(newest.text.data(), newest.text.size())) {
            if (newest.count < std::numeric_limits<std::uint64_t>::max()) { ++newest.count; }
            return;
        }
    }
    MessageHistoryEntry entry{String(message.data(), message.size())};
    if (entries_.size() == static_cast<std::size_t>(capacity_)) { entries_.erase(entries_.begin()); }
    entries_.push_back(eastl::move(entry));
}

eastl::span<const MessageHistoryEntry> MessageHistory::Entries() const { return {entries_.data(), entries_.size()}; }

}
