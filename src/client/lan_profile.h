#pragma once

#include "core/containers.h"

#include <EASTL/expected.h>
#include <EASTL/string_view.h>

#include <cstdint>

namespace cataclysmx::client {

struct LanIdentity {
    String address;
    std::uint16_t port = 0;
    std::uint64_t token = 0;
    String name;
};

enum class ProfileError { InvalidField, Full, Io, Malformed, TemporaryCollision };
template<class T> using ProfileResult = eastl::expected<T, ProfileError>;

// Bounded, versioned local identity file. A missing file represents an empty profile.
class LanProfile {
public:
    ProfileResult<void> Load(const char* path);
    ProfileResult<void> Save(const char* path) const;
    const LanIdentity* Find(eastl::string_view address, std::uint16_t port) const;
    ProfileResult<void> Set(LanIdentity identity);
    bool Forget(eastl::string_view address, std::uint16_t port);
    const Vector<LanIdentity>& Entries() const { return entries_; }
private:
    Vector<LanIdentity> entries_;
};

}
