#include "sdl/channels.h"

#include <EASTL/utility.h>
#include <chrono>
#include <cstdlib>

namespace cataclysmx::sdl {
EnqueueResult SessionChannels::TrySubmit(SessionRequest request) {
    std::lock_guard lock(mutex_);
    if (stopping_) { return EnqueueResult::Stopped; }
    if (requestCount_ == requestCapacity || reserved_ + 2 > replyCapacity) { return EnqueueResult::Busy; }
    reserved_ += 2;
    eastl::swap(requests_[(requestHead_ + requestCount_) % requestCapacity], request);
    ++requestCount_;
    condition_.notify_one();
    return EnqueueResult::Accepted;
}
bool SessionChannels::TryPopReply(SessionReply& reply) {
    SessionReply retired;
    eastl::swap(retired, reply);
    std::lock_guard lock(mutex_);
    if (replyCount_ == 0) { eastl::swap(retired, reply); return false; }
    eastl::swap(reply, replies_[replyHead_]);
    replyHead_ = (replyHead_ + 1) % replyCapacity;
    --replyCount_;
    --reserved_;
    return true;
}
bool SessionChannels::TakeSnapshot(SessionSnapshot& current) {
    std::lock_guard lock(mutex_);
    if (!fresh_) { return false; }
    eastl::swap(current, pending_);
    fresh_ = false;
    return true;
}
void SessionChannels::Stop() {
    std::lock_guard lock(mutex_);
    stopping_ = true;
    condition_.notify_one();
}
bool SessionChannels::TakeRequest(SessionRequest& request) {
    SessionRequest retired;
    eastl::swap(retired, request);
    std::lock_guard lock(mutex_);
    if (requestCount_ == 0) { eastl::swap(retired, request); return false; }
    eastl::swap(request, requests_[requestHead_]);
    requestHead_ = (requestHead_ + 1) % requestCapacity;
    --requestCount_;
    return true;
}
void SessionChannels::PublishReply(SessionReply reply, bool releasesUnusedSlot) {
    std::lock_guard lock(mutex_);
    if (replyCount_ >= replyCapacity || reserved_ == 0) { std::abort(); }
    eastl::swap(replies_[(replyHead_ + replyCount_) % replyCapacity], reply);
    ++replyCount_;
    if (releasesUnusedSlot) { --reserved_; }
}
void SessionChannels::PublishSnapshot(SessionSnapshot& building) {
    std::lock_guard lock(mutex_);
    eastl::swap(pending_, building);
    fresh_ = true;
}
bool SessionChannels::Wait() {
    std::unique_lock lock(mutex_);
    condition_.wait_for(lock, std::chrono::milliseconds(20), [this] { return stopping_ || requestCount_ != 0; });
    return !stopping_;
}
bool SessionChannels::Stopping() {
    std::lock_guard lock(mutex_);
    return stopping_;
}
}
