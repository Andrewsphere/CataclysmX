#include "client/lan_profile.h"

#include <EASTL/utility.h>

#include <charconv>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#elif __has_include(<unistd.h>)
#include <unistd.h>
#endif

namespace cataclysmx::client {
namespace {

std::FILE* OpenFile(const char* path, const char* mode) {
#ifdef _WIN32
    std::FILE* file = nullptr;
    fopen_s(&file, path, mode);
    return file;
#else
    return std::fopen(path, mode);
#endif
}

bool ValidField(const String& text, std::size_t maximum, bool emptyAllowed = false) {
    if (text.size() > maximum || (!emptyAllowed && text.empty())) { return false; }
    for (unsigned char c : text) {
        if (c < 32 || c == 127) { return false; }
    }
    return true;
}

template<class T>
bool Parse(eastl::string_view text, T& value) {
    if (text.empty()) { return false; }
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}

bool Valid(const LanIdentity& identity) {
    return ValidField(identity.address, 255) && ValidField(identity.name, 64, true)
        && identity.port != 0 && identity.token != 0;
}

}

const LanIdentity* LanProfile::Find(eastl::string_view address, std::uint16_t port) const {
    for (const auto& entry : entries_) {
        if (entry.address == address && entry.port == port) { return &entry; }
    }
    return nullptr;
}

ProfileResult<void> LanProfile::Set(LanIdentity identity) {
    if (!Valid(identity)) { return eastl::unexpected(ProfileError::InvalidField); }
    for (auto& entry : entries_) {
        if (entry.address == identity.address && entry.port == identity.port) {
            entry = eastl::move(identity);
            return {};
        }
    }
    if (entries_.size() >= 64) { return eastl::unexpected(ProfileError::Full); }
    entries_.push_back(eastl::move(identity));
    return {};
}

bool LanProfile::Forget(eastl::string_view address, std::uint16_t port) {
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
        if (it->address == address && it->port == port) { entries_.erase(it); return true; }
    }
    return false;
}

ProfileResult<void> LanProfile::Load(const char* path) {
    if (!path || !*path) { return eastl::unexpected(ProfileError::InvalidField); }
    std::FILE* file = OpenFile(path, "rb");
    if (!file) {
        if (errno != ENOENT) { return eastl::unexpected(ProfileError::Io); }
        entries_.clear();
        return {};
    }
    LanProfile loaded;
    char buffer[512];
    bool ok = std::fgets(buffer, sizeof(buffer), file) && std::strcmp(buffer, "CataclysmX-LAN-1\n") == 0;
    bool ioError = false;
    while (ok && std::fgets(buffer, sizeof(buffer), file)) {
        const std::size_t length = std::strlen(buffer);
        if (length == 0 || buffer[length - 1] != '\n') { ok = false; break; }
        eastl::string_view line(buffer, length - 1);
        const auto a = line.find('\t');
        const auto b = a == line.npos ? line.npos : line.find('\t', a + 1);
        const auto c = b == line.npos ? line.npos : line.find('\t', b + 1);
        if (c == line.npos || line.find('\t', c + 1) != line.npos) { ok = false; break; }
        LanIdentity identity;
        identity.address.assign(line.data(), a);
        identity.name.assign(line.data() + c + 1, line.size() - c - 1);
        if (!Parse(line.substr(a + 1, b - a - 1), identity.port)
            || !Parse(line.substr(b + 1, c - b - 1), identity.token)
            || loaded.Find(identity.address, identity.port) || !loaded.Set(eastl::move(identity))) {
            ok = false;
        }
    }
    if (std::ferror(file)) { ioError = true; }
    if (std::fclose(file) != 0) { ioError = true; }
    if (ioError) { return eastl::unexpected(ProfileError::Io); }
    if (!ok) { return eastl::unexpected(ProfileError::Malformed); }
    entries_ = eastl::move(loaded.entries_);
    return {};
}

ProfileResult<void> LanProfile::Save(const char* path) const {
    if (!path || !*path) { return eastl::unexpected(ProfileError::InvalidField); }
    for (const auto& entry : entries_) {
        if (!Valid(entry)) { return eastl::unexpected(ProfileError::InvalidField); }
    }
    // Exclusive creation prevents simultaneous writers from sharing a temporary file.
    std::string temporary;
    std::FILE* file = nullptr;
    for (int i = 0; i < 32 && !file; ++i) {
        temporary = std::string(path) + ".tmp" + std::to_string(i);
        file = OpenFile(temporary.c_str(), "wbx");
        if (!file && errno != EEXIST) { return eastl::unexpected(ProfileError::Io); }
    }
    if (!file) { return eastl::unexpected(ProfileError::TemporaryCollision); }
    bool ok = std::fputs("CataclysmX-LAN-1\n", file) >= 0;
    for (const auto& entry : entries_) {
        if (std::fprintf(file, "%s\t%u\t%llu\t%s\n", entry.address.c_str(),
                static_cast<unsigned>(entry.port), static_cast<unsigned long long>(entry.token),
                entry.name.c_str()) < 0) { ok = false; break; }
    }
    if (std::fflush(file) != 0) { ok = false; }
    if (ok) {
#ifdef _WIN32
        if (_commit(_fileno(file)) != 0) { ok = false; }
#elif __has_include(<unistd.h>)
        if (fsync(fileno(file)) != 0) { ok = false; }
#endif
    }
    if (std::fclose(file) != 0) { ok = false; }
    if (ok) {
#ifdef _WIN32
        ok = MoveFileExA(temporary.c_str(), path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
        ok = std::rename(temporary.c_str(), path) == 0;
#endif
    }
    if (!ok) { std::remove(temporary.c_str()); }
    if (!ok) { return eastl::unexpected(ProfileError::Io); }
    return {};
}

}
