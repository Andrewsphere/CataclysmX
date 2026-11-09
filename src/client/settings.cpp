#include "client/settings.h"

#include <EASTL/utility.h>

#include <charconv>
#include <cstdlib>
#include <iterator>

namespace cataclysmx {
namespace {

constexpr eastl::string_view fieldNames[] = {"language", "view_radius", "autosave_interval",
    "active_radius", "resident_radius", "seed", "ticks_per_second", "rate_limited", "pause_when_idle"};

void Overlay(Settings& settings, const SettingsOverrides& overrides) {
    if (overrides.language) { settings.language = *overrides.language; }
    if (overrides.viewRadius) { settings.viewRadius = *overrides.viewRadius; }
    if (overrides.autosaveInterval) { settings.autosaveInterval = *overrides.autosaveInterval; }
    if (overrides.activeRadius) { settings.activeRadius = *overrides.activeRadius; }
    if (overrides.residentRadius) { settings.residentRadius = *overrides.residentRadius; }
    if (overrides.seed) { settings.seed = *overrides.seed; }
    if (overrides.ticksPerSecond) { settings.pacing.ticksPerSecond = *overrides.ticksPerSecond; }
    if (overrides.rateLimited) { settings.pacing.rateLimited = *overrides.rateLimited; }
    if (overrides.pauseWhenIdle) { settings.pacing.pauseWhenIdle = *overrides.pauseWhenIdle; }
}
template<class T> String Decimal(T number) {
    char buffer[32];
    const auto converted = std::to_chars(buffer, buffer + sizeof(buffer), number);
    if (converted.ec != std::errc{}) { std::abort(); }
    return String(buffer, static_cast<std::size_t>(converted.ptr - buffer));
}
template<class T> bool ParseDecimal(eastl::string_view text, T& number) {
    const auto converted = std::from_chars(text.data(), text.data() + text.size(), number);
    return converted.ec == std::errc{} && converted.ptr == text.data() + text.size();
}
SettingsResult<void> Invalid(Diagnostics* diagnostics, eastl::string_view message) {
    if (diagnostics) { diagnostics->Add(message); }
    return eastl::unexpected(SettingsError::InvalidData);
}

}

eastl::string_view SettingName(SettingsField field) {
    const auto index = static_cast<unsigned>(field);
    if (index >= std::size(fieldNames)) { std::abort(); }
    return fieldNames[index];
}
eastl::optional<SettingsField> ParseSettingName(eastl::string_view name) {
    for (unsigned i = 0; i < std::size(fieldNames); ++i) {
        if (fieldNames[i] == name) { return static_cast<SettingsField>(i); }
    }
    return {};
}
Settings ResolveSettings(const SettingsOverrides& global, const SettingsOverrides& world) {
    Settings settings;
    Overlay(settings, global); Overlay(settings, world);
    return settings;
}
SettingsResult<void> ValidateSettings(const Settings& settings, Diagnostics* diagnostics) {
    if (settings.language.empty() || settings.language.size() > 32) { return Invalid(diagnostics, "invalid language identifier"); }
    for (char ch : settings.language) {
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_')) {
            return Invalid(diagnostics, "language identifier must contain only letters, digits, '-' or '_'");
        }
    }
    if (settings.viewRadius < 0 || settings.viewRadius > 64) { return Invalid(diagnostics, "view_radius must be in [0,64]"); }
    if (settings.activeRadius < 0 || settings.residentRadius < settings.activeRadius || settings.residentRadius > 8) {
        return Invalid(diagnostics, "expected 0 <= active_radius <= resident_radius <= 8");
    }
    if (settings.pacing.ticksPerSecond == 0 || settings.pacing.ticksPerSecond > 1000000) {
        return Invalid(diagnostics, "ticks_per_second must be in [1,1000000]");
    }
    return {};
}
SettingsResult<void> ApplySettings(Server& server, const Settings& settings, Diagnostics* diagnostics) {
    const auto valid = ValidateSettings(settings, diagnostics);
    if (!valid) { return valid; }
    if (!server.SetBubbleRadii(settings.activeRadius, settings.residentRadius, diagnostics)) {
        return eastl::unexpected(SettingsError::ApplyFailed);
    }
    // These cannot fail after ValidateSettings. No command or elapsed update occurs.
    if (!server.SetPacing(settings.pacing)) { std::abort(); }
    server.SetAutosaveInterval(settings.autosaveInterval);
    return {};
}
bool HasOverride(const SettingsOverrides& overrides, SettingsField field) {
    switch (field) {
    case SettingsField::Language: return overrides.language.has_value();
    case SettingsField::ViewRadius: return overrides.viewRadius.has_value();
    case SettingsField::AutosaveInterval: return overrides.autosaveInterval.has_value();
    case SettingsField::ActiveRadius: return overrides.activeRadius.has_value();
    case SettingsField::ResidentRadius: return overrides.residentRadius.has_value();
    case SettingsField::Seed: return overrides.seed.has_value();
    case SettingsField::TicksPerSecond: return overrides.ticksPerSecond.has_value();
    case SettingsField::RateLimited: return overrides.rateLimited.has_value();
    case SettingsField::PauseWhenIdle: return overrides.pauseWhenIdle.has_value();
    case SettingsField::Count: break;
    }
    std::abort();
}
void ResetOverride(SettingsOverrides& overrides, SettingsField field) {
    switch (field) {
    case SettingsField::Language: overrides.language.reset(); return;
    case SettingsField::ViewRadius: overrides.viewRadius.reset(); return;
    case SettingsField::AutosaveInterval: overrides.autosaveInterval.reset(); return;
    case SettingsField::ActiveRadius: overrides.activeRadius.reset(); return;
    case SettingsField::ResidentRadius: overrides.residentRadius.reset(); return;
    case SettingsField::Seed: overrides.seed.reset(); return;
    case SettingsField::TicksPerSecond: overrides.ticksPerSecond.reset(); return;
    case SettingsField::RateLimited: overrides.rateLimited.reset(); return;
    case SettingsField::PauseWhenIdle: overrides.pauseWhenIdle.reset(); return;
    case SettingsField::Count: break;
    }
    std::abort();
}
String SettingValue(const Settings& settings, SettingsField field) {
    switch (field) {
    case SettingsField::Language: return settings.language;
    case SettingsField::ViewRadius: return Decimal(settings.viewRadius);
    case SettingsField::AutosaveInterval: return Decimal(settings.autosaveInterval);
    case SettingsField::ActiveRadius: return Decimal(settings.activeRadius);
    case SettingsField::ResidentRadius: return Decimal(settings.residentRadius);
    case SettingsField::Seed: return Decimal(settings.seed);
    case SettingsField::TicksPerSecond: return Decimal(settings.pacing.ticksPerSecond);
    case SettingsField::RateLimited: return settings.pacing.rateLimited ? "true" : "false";
    case SettingsField::PauseWhenIdle: return settings.pacing.pauseWhenIdle ? "true" : "false";
    case SettingsField::Count: break;
    }
    std::abort();
}
SettingsResult<void> SetOverride(SettingsOverrides& overrides, SettingsField field,
    eastl::string_view value, Diagnostics* diagnostics) {
    int signedNumber = 0;
    std::uint64_t number = 0;
    switch (field) {
    case SettingsField::Language:
        overrides.language = String(value.data(), value.size()); return {};
    case SettingsField::ViewRadius: case SettingsField::ActiveRadius: case SettingsField::ResidentRadius:
        if (!ParseDecimal(value, signedNumber)) { break; }
        if (field == SettingsField::ViewRadius) { overrides.viewRadius = signedNumber; }
        else if (field == SettingsField::ActiveRadius) { overrides.activeRadius = signedNumber; }
        else { overrides.residentRadius = signedNumber; }
        return {};
    case SettingsField::Seed: case SettingsField::TicksPerSecond: case SettingsField::AutosaveInterval:
        if (!ParseDecimal(value, number)) { break; }
        if (field == SettingsField::AutosaveInterval) { overrides.autosaveInterval = number; return {}; }
        if (number > UINT32_MAX) { break; }
        if (field == SettingsField::Seed) { overrides.seed = static_cast<std::uint32_t>(number); }
        else { overrides.ticksPerSecond = static_cast<std::uint32_t>(number); }
        return {};
    case SettingsField::RateLimited: case SettingsField::PauseWhenIdle:
        if (value != "true" && value != "false") { break; }
        if (field == SettingsField::RateLimited) { overrides.rateLimited = value == "true"; }
        else { overrides.pauseWhenIdle = value == "true"; }
        return {};
    case SettingsField::Count: std::abort();
    }
    return Invalid(diagnostics, "invalid setting value");
}

SettingsStore::SettingsStore(String userRoot, String worldDirectory)
    : userRoot_(eastl::move(userRoot)), worldDirectory_(eastl::move(worldDirectory)), keys_(DefaultKeymap()) {}
SettingsResult<void> SettingsStore::Load(Diagnostics* diagnostics) {
    SettingsOverrides global;
    Keymap globalKeys;
    if (!userRoot_.empty()) {
        const auto config = JoinConfigurationPath({userRoot_.data(), userRoot_.size()}, "config");
        auto loaded = LoadSettingsFile(JoinConfigurationPath({config.data(), config.size()}, "settings.json5").c_str(), diagnostics);
        if (!loaded) { return eastl::unexpected(loaded.error()); }
        auto keys = LoadKeymapFile(JoinConfigurationPath({config.data(), config.size()}, "keybinds.json5").c_str(), diagnostics);
        if (!keys) { return eastl::unexpected(keys.error()); }
        global = eastl::move(*loaded); globalKeys = eastl::move(*keys);
    }
    SettingsOverrides world;
    Keymap worldKeys;
    if (!worldDirectory_.empty()) {
        const eastl::string_view directory(worldDirectory_.data(), worldDirectory_.size());
        auto loaded = LoadSettingsFile(JoinConfigurationPath(directory, "settings.json5").c_str(), diagnostics);
        if (!loaded) { return eastl::unexpected(loaded.error()); }
        auto keys = LoadKeymapFile(JoinConfigurationPath(directory, "keybinds.json5").c_str(), diagnostics);
        if (!keys) { return eastl::unexpected(keys.error()); }
        world = eastl::move(*loaded); worldKeys = eastl::move(*keys);
    }
    SettingsStore staged(userRoot_, worldDirectory_);
    auto applied = staged.SetOverrides(SettingsScope::Global, eastl::move(global), eastl::move(globalKeys), diagnostics);
    if (!applied) { return applied; }
    applied = staged.SetOverrides(SettingsScope::World, eastl::move(world), eastl::move(worldKeys), diagnostics);
    if (!applied) { return applied; }
    staged.globalDirty_ = false; staged.worldDirty_ = false;
    *this = eastl::move(staged);
    return {};
}
SettingsResult<void> SettingsStore::SetOverrides(SettingsScope scope, SettingsOverrides settings,
    Keymap keys, Diagnostics* diagnostics) {
    const auto& global = scope == SettingsScope::Global ? settings : global_;
    const auto& world = scope == SettingsScope::World ? settings : world_;
    const auto globalValid = ValidateSettings(ResolveSettings(global, {}), diagnostics);
    if (!globalValid) { return globalValid; }
    const auto resolved = ResolveSettings(global, world);
    const auto valid = ValidateSettings(resolved, diagnostics);
    if (!valid) { return valid; }
    if (!ValidateKeymap(keys, diagnostics)) { return eastl::unexpected(SettingsError::InvalidData); }
    const auto& globalKeys = scope == SettingsScope::Global ? keys : globalKeys_;
    const auto& worldKeys = scope == SettingsScope::World ? keys : worldKeys_;
    if (!ValidateKeymap(ResolveKeymap(globalKeys, {}), diagnostics)) { return eastl::unexpected(SettingsError::InvalidData); }
    auto resolvedKeys = ResolveKeymap(globalKeys, worldKeys);
    if (!ValidateKeymap(resolvedKeys, diagnostics)) { return eastl::unexpected(SettingsError::InvalidData); }
    if (scope == SettingsScope::Global) {
        globalDirty_ |= global_ != settings || globalKeys_ != keys;
        global_ = eastl::move(settings); globalKeys_ = eastl::move(keys);
    } else {
        worldDirty_ |= world_ != settings || worldKeys_ != keys;
        world_ = eastl::move(settings); worldKeys_ = eastl::move(keys);
    }
    effective_ = resolved; keys_ = eastl::move(resolvedKeys);
    return {};
}
SettingsResult<void> SettingsStore::Save(SettingsScope scope, Diagnostics* diagnostics) {
    if (scope == SettingsScope::Global && userRoot_.empty()) {
        if (diagnostics) { diagnostics->Add("no global configuration directory selected"); }
        return eastl::unexpected(SettingsError::Io);
    }
    const auto directory = scope == SettingsScope::Global
        ? JoinConfigurationPath({userRoot_.data(), userRoot_.size()}, "config") : worldDirectory_;
    if (directory.empty()) {
        if (diagnostics) { diagnostics->Add("world overrides are in memory until a save/world directory is selected"); }
        return eastl::unexpected(SettingsError::Io);
    }
    auto result = SaveSettingsFile(JoinConfigurationPath({directory.data(), directory.size()}, "settings.json5").c_str(), Overrides(scope), diagnostics);
    if (!result) { return result; }
    result = SaveKeymapFile(JoinConfigurationPath({directory.data(), directory.size()}, "keybinds.json5").c_str(), KeyOverrides(scope), diagnostics);
    if (result) {
        if (scope == SettingsScope::Global) { globalDirty_ = false; } else { worldDirty_ = false; }
    }
    return result;
}
bool SettingsStore::Dirty(SettingsScope scope) const { return scope == SettingsScope::Global ? globalDirty_ : worldDirty_; }
const Settings& SettingsStore::Effective() const { return effective_; }
const Keymap& SettingsStore::Keys() const { return keys_; }
const SettingsOverrides& SettingsStore::Overrides(SettingsScope scope) const { return scope == SettingsScope::Global ? global_ : world_; }
const Keymap& SettingsStore::KeyOverrides(SettingsScope scope) const { return scope == SettingsScope::Global ? globalKeys_ : worldKeys_; }
const String& SettingsStore::UserRoot() const { return userRoot_; }
const String& SettingsStore::WorldDirectory() const { return worldDirectory_; }
SettingsSource SettingsStore::Source(SettingsField field) const {
    if (HasOverride(world_, field)) { return SettingsSource::World; }
    return HasOverride(global_, field) ? SettingsSource::Global : SettingsSource::BuiltIn;
}

}
