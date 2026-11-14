#pragma once

#include "client/input.h"
#include "core/server.h"

namespace cataclysmx {

enum class SettingsError { Io, InvalidData, UnsupportedVersion, ApplyFailed };
template<class T> using SettingsResult = eastl::expected<T, SettingsError>;
enum class SettingsScope { Global, World };
enum class SettingsSource { BuiltIn, Global, World };
enum class ResidencyMode { Square, Circle };
enum class SettingsField {
    Language, ViewRadius, AutosaveInterval, ActiveRadius, ResidentRadius, Seed,
    TicksPerSecond, RateLimited, PauseWhenIdle, ResidencyMode, ResidentRadiusTiles, KeyboardRepeatRate,
    MessageHistorySize, Count
};
struct Settings {
    String language = "en";
    int viewRadius = 64;
    std::uint32_t keyboardRepeatRate = 10;
    int messageHistorySize = 10;
    std::uint64_t autosaveInterval = 1000;
    int activeRadius = 1;
    int residentRadius = 5;
    std::uint32_t seed = 42;
    PacingSettings pacing{};
    ResidencyMode residencyMode = ResidencyMode::Square;
    int residentRadiusTiles = CircleResidency{}.radiusTiles;
    bool operator==(const Settings&) const = default;
};
struct SettingsOverrides {
    eastl::optional<String> language;
    eastl::optional<int> viewRadius;
    eastl::optional<std::uint32_t> keyboardRepeatRate;
    eastl::optional<int> messageHistorySize;
    eastl::optional<std::uint64_t> autosaveInterval;
    eastl::optional<int> activeRadius;
    eastl::optional<int> residentRadius;
    eastl::optional<std::uint32_t> seed;
    eastl::optional<std::uint32_t> ticksPerSecond;
    eastl::optional<bool> rateLimited;
    eastl::optional<bool> pauseWhenIdle;
    eastl::optional<ResidencyMode> residencyMode;
    eastl::optional<int> residentRadiusTiles;
    bool operator==(const SettingsOverrides&) const = default;
};

eastl::string_view SettingName(SettingsField field);
eastl::optional<SettingsField> ParseSettingName(eastl::string_view name);
Settings ResolveSettings(const SettingsOverrides& global, const SettingsOverrides& world);
// Runtime baseline supplies residency only; all other defaults remain Settings defaults.
Settings ResolveSettingsForServer(const Server& server, const SettingsOverrides& global,
    const SettingsOverrides& world);
bool HasResidencyOverride(const SettingsOverrides& overrides);
SettingsResult<void> ValidateOverrides(const SettingsOverrides& overrides, Diagnostics* diagnostics = nullptr);
SettingsResult<void> ValidateSettings(const Settings& settings, Diagnostics* diagnostics = nullptr);
// Application boundary only. Validation precedes the one fallible world update.
// Seed is a creation default and is never applied to an existing Server.
SettingsResult<void> ApplySettings(Server& server, const Settings& settings,
    Diagnostics* diagnostics = nullptr);
bool HasOverride(const SettingsOverrides& overrides, SettingsField field);
void ResetOverride(SettingsOverrides& overrides, SettingsField field);
String SettingValue(const Settings& settings, SettingsField field);
SettingsResult<void> SetOverride(SettingsOverrides& overrides, SettingsField field,
    eastl::string_view value, Diagnostics* diagnostics = nullptr);

SettingsResult<SettingsOverrides> ParseSettings(eastl::string_view text, Diagnostics* diagnostics = nullptr);
SettingsResult<Keymap> ParseKeymap(eastl::string_view text, Diagnostics* diagnostics = nullptr);
SettingsResult<SettingsOverrides> LoadSettingsFile(const char* path, Diagnostics* diagnostics = nullptr);
SettingsResult<Keymap> LoadKeymapFile(const char* path, Diagnostics* diagnostics = nullptr);
SettingsResult<void> SaveSettingsFile(const char* path, const SettingsOverrides& overrides,
    Diagnostics* diagnostics = nullptr);
SettingsResult<void> SaveKeymapFile(const char* path, const Keymap& overrides,
    Diagnostics* diagnostics = nullptr);
String DefaultUserRoot();
String WorldDirectoryForSave(const char* savePath);
String JoinConfigurationPath(eastl::string_view directory, eastl::string_view name);

class SettingsStore {
public:
    explicit SettingsStore(String userRoot, String worldDirectory = {});
    // All four files are staged. Failure preserves this store, including overrides.
    SettingsResult<void> Load(Diagnostics* diagnostics = nullptr);
    SettingsResult<void> SetOverrides(SettingsScope scope, SettingsOverrides settings, Keymap keys,
        Diagnostics* diagnostics = nullptr);
    SettingsResult<void> Save(SettingsScope scope, Diagnostics* diagnostics = nullptr);
    bool Dirty(SettingsScope scope) const;
    const Settings& Effective() const;
    const Keymap& Keys() const;
    const SettingsOverrides& Overrides(SettingsScope scope) const;
    const Keymap& KeyOverrides(SettingsScope scope) const;
    const String& UserRoot() const;
    const String& WorldDirectory() const;
    SettingsSource Source(SettingsField field) const;
private:
    String userRoot_;
    String worldDirectory_;
    SettingsOverrides global_;
    SettingsOverrides world_;
    Keymap globalKeys_;
    Keymap worldKeys_;
    Settings effective_;
    Keymap keys_;
    bool globalDirty_ = false;
    bool worldDirty_ = false;
};

// Absent residency overrides preserve the server's saved/default policy.
SettingsResult<void> ApplySettings(Server& server, const SettingsOverrides& global,
    const SettingsOverrides& world, Diagnostics* diagnostics = nullptr);
SettingsResult<void> ApplySettings(Server& server, const SettingsStore& store,
    Diagnostics* diagnostics = nullptr);

}
