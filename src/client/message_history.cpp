#include "client/message_history.h"

#include <cstdlib>

namespace cataclysmx {

void MessageHistory::SetCapacity(int capacity) {
    if (capacity < 1 || capacity > 100) { std::abort(); }
    capacity_ = capacity;
    if (entries_.size() > static_cast<std::size_t>(capacity_)) {
        entries_.erase(entries_.begin(), entries_.end() - capacity_);
    }
}

void MessageHistory::Add(eastl::string_view message) {
    if (message.empty()) { return; }
    if (entries_.size() == static_cast<std::size_t>(capacity_)) { entries_.erase(entries_.begin()); }
    entries_.emplace_back(message.data(), message.size());
}

eastl::span<const String> MessageHistory::Entries() const { return {entries_.data(), entries_.size()}; }

}
