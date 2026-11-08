// JSON and filesystem integration boundary for application preferences and text.
// No parser types cross into the client or simulation interfaces.
#include "client/localization.h"

#include <EASTL/sort.h>
#include <EASTL/utility.h>
#include <yyjson.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <initializer_list>
#include <iterator>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace cataclysmx {
namespace {

namespace fs = std::filesystem;
eastl::string_view View(const String& value) { return {value.data(), value.size()}; }
fs::path Path(eastl::string_view value) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(value.data()), value.size()));
}
String Utf8(const fs::path& path) {
    const auto text = path.u8string();
    return String(reinterpret_cast<const char*>(text.data()), text.size());
}
void Report(Diagnostics* diagnostics, eastl::string_view message, eastl::string_view file = {}, eastl::string_view path = {}) {
    if (!diagnostics) { return; }
    DiagnosticFrame frame;
    frame.message.assign(message.data(), message.size());
    frame.location = DiagnosticLocation{String(file.data() ? file.data() : "", file.size()),
        String(path.data() ? path.data() : "", path.size()), {}, {}};
    diagnostics->Add(eastl::move(frame));
}
void* Allocate(void*, std::size_t size) {
    void* memory = std::malloc(size); if (!memory) { std::abort(); } return memory;
}
void* Reallocate(void*, void* memory, std::size_t, std::size_t size) {
    void* result = std::realloc(memory, size); if (!result) { std::abort(); } return result;
}
void Free(void*, void* memory) { std::free(memory); }
struct Document {
    yyjson_doc* value = nullptr;
    ~Document() { yyjson_doc_free(value); }
};
struct MutableDocument {
    yyjson_mut_doc* value;
    MutableDocument() {
        yyjson_alc allocator{Allocate, Reallocate, Free, nullptr};
        value = yyjson_mut_doc_new(&allocator);
        if (!value) { std::abort(); }
    }
    ~MutableDocument() { yyjson_mut_doc_free(value); }
};
eastl::string_view JsonText(yyjson_val* value) { return {yyjson_get_str(value), yyjson_get_len(value)}; }
bool StringValue(yyjson_val* value) {
    return yyjson_is_str(value) && !std::memchr(yyjson_get_str(value), 0, yyjson_get_len(value));
}
bool Object(yyjson_val* object, std::initializer_list<eastl::string_view> allowed, Diagnostics* diagnostics) {
    if (!yyjson_is_obj(object)) { Report(diagnostics, "expected an object"); return false; }
    std::size_t index, count;
    yyjson_val* key; yyjson_val* value;
    yyjson_obj_foreach(object, index, count, key, value) {
        (void)value;
        if (!StringValue(key)) { Report(diagnostics, "invalid object key"); return false; }
        const auto name = JsonText(key);
        if (allowed.size() != 0) {
            bool known = false;
            for (const auto candidate : allowed) { known |= candidate == name; }
            if (!known) { Report(diagnostics, "unknown field", {}, name); return false; }
        }
        std::size_t previousIndex, previousCount;
        yyjson_val* previousKey; yyjson_val* previousValue;
        yyjson_obj_foreach(object, previousIndex, previousCount, previousKey, previousValue) {
            (void)previousValue;
            if (previousIndex == index) { break; }
            if (JsonText(previousKey) == name) { Report(diagnostics, "duplicate field", {}, name); return false; }
        }
    }
    return true;
}
SettingsResult<void> ParseDocument(eastl::string_view text, Document& document, Diagnostics* diagnostics) {
    yyjson_alc allocator{Allocate, Reallocate, Free, nullptr};
    yyjson_read_err error{};
    document.value = yyjson_read_opts(const_cast<char*>(text.data()), text.size(), YYJSON_READ_JSON5, &allocator, &error);
    if (!document.value) {
        Report(diagnostics, error.msg ? error.msg : "invalid JSON5");
        return eastl::unexpected(SettingsError::InvalidData);
    }
    return {};
}
SettingsResult<void> Version(yyjson_val* root, Diagnostics* diagnostics) {
    const auto* version = yyjson_obj_get(root, "format_version");
    if (!yyjson_is_uint(version)) {
        Report(diagnostics, "expected unsigned format_version");
        return eastl::unexpected(SettingsError::InvalidData);
    }
    if (yyjson_get_uint(version) != 1) {
        Report(diagnostics, "unsupported configuration/catalog version");
        return eastl::unexpected(SettingsError::UnsupportedVersion);
    }
    return {};
}
SettingsResult<eastl::optional<String>> Read(const char* file, Diagnostics* diagnostics) {
    const auto path = Path(file);
    std::error_code error;
    const bool exists = fs::exists(path, error);
    if (error) { Report(diagnostics, "could not inspect file", file); return eastl::unexpected(SettingsError::Io); }
    if (!exists) { return eastl::optional<String>{}; }
#ifdef _WIN32
    std::FILE* stream = nullptr;
    (void)_wfopen_s(&stream, path.c_str(), L"rb");
#else
    std::FILE* stream = std::fopen(path.c_str(), "rb");
#endif
    if (!stream) { Report(diagnostics, "could not open file", file); return eastl::unexpected(SettingsError::Io); }
    String text;
    char buffer[4096];
    bool failed = false;
    for (;;) {
        const std::size_t count = std::fread(buffer, 1, sizeof(buffer), stream);
        text.append(buffer, count);
        if (text.size() > 4 * 1024 * 1024) { failed = true; break; }
        if (count != sizeof(buffer)) { failed = std::ferror(stream) != 0; break; }
    }
    if (std::fclose(stream) != 0) { failed = true; }
    if (failed) { Report(diagnostics, "file read failed or exceeds 4 MiB", file); return eastl::unexpected(SettingsError::Io); }
    return eastl::optional<String>(eastl::move(text));
}
SettingsResult<void> WriteAtomic(const char* file, eastl::string_view contents, Diagnostics* diagnostics) {
    const auto target = Path(file);
    std::error_code error;
    if (!target.parent_path().empty()) { fs::create_directories(target.parent_path(), error); }
    if (error) { Report(diagnostics, "could not create configuration directory", file); return eastl::unexpected(SettingsError::Io); }
    fs::path temporary;
    bool wrote = false;
    for (unsigned attempt = 0; attempt < 64; ++attempt) {
        temporary = target;
        temporary += ".tmp" + std::to_string(attempt);
#ifdef _WIN32
        HANDLE handle = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            if (GetLastError() == ERROR_FILE_EXISTS || GetLastError() == ERROR_ALREADY_EXISTS) { continue; }
            break;
        }
        DWORD count = 0;
        wrote = contents.size() <= MAXDWORD
            && WriteFile(handle, contents.data(), static_cast<DWORD>(contents.size()), &count, nullptr)
            && count == contents.size() && FlushFileBuffers(handle);
        if (!CloseHandle(handle)) { wrote = false; }
#else
        const int descriptor = open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (descriptor < 0) { if (errno == EEXIST) { continue; } break; }
        std::size_t offset = 0;
        while (offset < contents.size()) {
            const auto count = write(descriptor, contents.data() + offset, contents.size() - offset);
            if (count < 0 && errno == EINTR) { continue; }
            if (count <= 0) { break; }
            offset += static_cast<std::size_t>(count);
        }
        wrote = offset == contents.size() && fsync(descriptor) == 0;
        if (close(descriptor) != 0) { wrote = false; }
#endif
        if (wrote) {
#ifdef _WIN32
            wrote = MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
            wrote = rename(temporary.c_str(), target.c_str()) == 0;
#endif
        }
        // Only remove the exclusive temporary file created by this call.
        if (!wrote) { fs::remove(temporary, error); }
        if (wrote) { return {}; }
        break;
    }
    Report(diagnostics, "atomic configuration write failed; original file retained", file);
    return eastl::unexpected(SettingsError::Io);
}
SettingsResult<void> WriteDocument(const char* path, yyjson_mut_doc* document, Diagnostics* diagnostics) {
    std::size_t length = 0;
    char* json = yyjson_mut_write(document, YYJSON_WRITE_PRETTY | YYJSON_WRITE_NEWLINE_AT_END, &length);
    if (!json) { std::abort(); }
    const auto result = WriteAtomic(path, {json, length}, diagnostics);
    std::free(json);
    return result;
}
void AddString(yyjson_mut_doc* doc, yyjson_mut_val* object, const char* name, eastl::string_view text) {
    if (!yyjson_mut_obj_add(object, yyjson_mut_strcpy(doc, name), yyjson_mut_strncpy(doc, text.data(), text.size()))) { std::abort(); }
}

}

String JoinConfigurationPath(eastl::string_view directory, eastl::string_view name) { return Utf8(Path(directory) / Path(name)); }
String WorldDirectoryForSave(const char* savePath) {
    String result(savePath); result += ".world"; return result;
}
String DefaultUserRoot() {
#ifdef _WIN32
    wchar_t buffer[32768];
    const DWORD size = GetEnvironmentVariableW(L"APPDATA", buffer, static_cast<DWORD>(std::size(buffer)));
    if (size > 0 && size < std::size(buffer)) { return Utf8(fs::path(buffer) / L"CataclysmX"); }
#else
    if (const char* data = std::getenv("XDG_DATA_HOME"); data && *data) { return Utf8(fs::path(data) / "CataclysmX"); }
    if (const char* userHome = std::getenv("HOME"); userHome && *userHome) { return Utf8(fs::path(userHome) / ".local/share/CataclysmX"); }
#endif
    return "userdata";
}
SettingsResult<SettingsOverrides> ParseSettings(eastl::string_view text, Diagnostics* diagnostics) {
    Document document;
    const auto parsed = ParseDocument(text, document, diagnostics);
    if (!parsed) { return eastl::unexpected(parsed.error()); }
    auto* root = yyjson_doc_get_root(document.value);
    if (!Object(root, {"format_version", "settings"}, diagnostics)) { return eastl::unexpected(SettingsError::InvalidData); }
    const auto version = Version(root, diagnostics);
    if (!version) { return eastl::unexpected(version.error()); }
    auto* settings = yyjson_obj_get(root, "settings");
    if (!Object(settings, {}, diagnostics)) { return eastl::unexpected(SettingsError::InvalidData); }
    SettingsOverrides overrides;
    std::size_t index, count;
    yyjson_val* key; yyjson_val* value;
    yyjson_obj_foreach(settings, index, count, key, value) {
        const auto field = ParseSettingName(JsonText(key));
        if (!field) { Report(diagnostics, "unknown setting", {}, JsonText(key)); return eastl::unexpected(SettingsError::InvalidData); }
        String encoded;
        if (*field == SettingsField::Language && StringValue(value)) { encoded.assign(yyjson_get_str(value), yyjson_get_len(value)); }
        else if ((*field == SettingsField::RateLimited || *field == SettingsField::PauseWhenIdle) && yyjson_is_bool(value)) {
            encoded = yyjson_get_bool(value) ? "true" : "false";
        } else if (*field != SettingsField::Language && *field != SettingsField::RateLimited
            && *field != SettingsField::PauseWhenIdle && yyjson_is_int(value)) {
            encoded = yyjson_is_uint(value) ? UnsignedText(yyjson_get_uint(value)) : NumberText(yyjson_get_sint(value));
        } else { Report(diagnostics, "invalid setting type", {}, JsonText(key)); return eastl::unexpected(SettingsError::InvalidData); }
        if (!SetOverride(overrides, *field, View(encoded), diagnostics)) { return eastl::unexpected(SettingsError::InvalidData); }
    }
    // Validate individual values here; relationships are validated after layering.
    auto individual = ResolveSettings({}, overrides);
    if (overrides.activeRadius && !overrides.residentRadius) { individual.residentRadius = 8; }
    if (overrides.residentRadius && !overrides.activeRadius) { individual.activeRadius = 0; }
    if (!ValidateSettings(individual, diagnostics)) { return eastl::unexpected(SettingsError::InvalidData); }
    return overrides;
}
SettingsResult<Keymap> ParseKeymap(eastl::string_view text, Diagnostics* diagnostics) {
    Document document;
    const auto parsed = ParseDocument(text, document, diagnostics);
    if (!parsed) { return eastl::unexpected(parsed.error()); }
    auto* root = yyjson_doc_get_root(document.value);
    if (!Object(root, {"format_version", "bindings"}, diagnostics)) { return eastl::unexpected(SettingsError::InvalidData); }
    const auto version = Version(root, diagnostics);
    if (!version) { return eastl::unexpected(version.error()); }
    auto* bindings = yyjson_obj_get(root, "bindings");
    if (!yyjson_is_arr(bindings)) { Report(diagnostics, "expected bindings array"); return eastl::unexpected(SettingsError::InvalidData); }
    Keymap map;
    std::size_t index, count;
    yyjson_val* binding;
    yyjson_arr_foreach(bindings, index, count, binding) {
        if (!Object(binding, {"context", "action", "keys"}, diagnostics)) { return eastl::unexpected(SettingsError::InvalidData); }
        auto* contextName = yyjson_obj_get(binding, "context");
        auto* actionName = yyjson_obj_get(binding, "action");
        auto* keys = yyjson_obj_get(binding, "keys");
        if (!StringValue(contextName) || !StringValue(actionName) || !yyjson_is_arr(keys)) {
            Report(diagnostics, "expected context, action and keys"); return eastl::unexpected(SettingsError::InvalidData);
        }
        const auto context = ParseContext(JsonText(contextName));
        const auto action = ParseAction(JsonText(actionName));
        if (!context || !action) { Report(diagnostics, "unknown binding context or action"); return eastl::unexpected(SettingsError::InvalidData); }
        InputBinding entry{*context, *action, {}};
        std::size_t keyIndex, keyCount;
        yyjson_val* key;
        yyjson_arr_foreach(keys, keyIndex, keyCount, key) {
            if (!Object(key, {"key", "ctrl"}, diagnostics)) { return eastl::unexpected(SettingsError::InvalidData); }
            auto* name = yyjson_obj_get(key, "key");
            auto* ctrl = yyjson_obj_get(key, "ctrl");
            if (!StringValue(name) || (ctrl && !yyjson_is_bool(ctrl))) {
                Report(diagnostics, "expected key string and optional ctrl boolean"); return eastl::unexpected(SettingsError::InvalidData);
            }
            entry.keys.push_back({String(yyjson_get_str(name), yyjson_get_len(name)), ctrl && yyjson_get_bool(ctrl)});
        }
        map.push_back(eastl::move(entry));
    }
    if (!ValidateKeymap(map, diagnostics)) { return eastl::unexpected(SettingsError::InvalidData); }
    return map;
}
SettingsResult<SettingsOverrides> LoadSettingsFile(const char* path, Diagnostics* diagnostics) {
    auto file = Read(path, diagnostics);
    if (!file) { return eastl::unexpected(file.error()); }
    if (!*file) { return SettingsOverrides{}; }
    auto parsed = ParseSettings(View(**file), diagnostics);
    if (!parsed) { Report(diagnostics, "while loading settings", path); }
    return parsed;
}
SettingsResult<Keymap> LoadKeymapFile(const char* path, Diagnostics* diagnostics) {
    auto file = Read(path, diagnostics);
    if (!file) { return eastl::unexpected(file.error()); }
    if (!*file) { return Keymap{}; }
    auto parsed = ParseKeymap(View(**file), diagnostics);
    if (!parsed) { Report(diagnostics, "while loading keybindings", path); }
    return parsed;
}
SettingsResult<void> SaveSettingsFile(const char* path, const SettingsOverrides& overrides, Diagnostics* diagnostics) {
    auto individual = ResolveSettings({}, overrides);
    if (overrides.activeRadius && !overrides.residentRadius) { individual.residentRadius = 8; }
    if (overrides.residentRadius && !overrides.activeRadius) { individual.activeRadius = 0; }
    if (!ValidateSettings(individual, diagnostics)) { return eastl::unexpected(SettingsError::InvalidData); }
    const auto existing = LoadSettingsFile(path, diagnostics);
    if (!existing) { return eastl::unexpected(existing.error()); }
    MutableDocument document;
    auto* root = yyjson_mut_obj(document.value);
    yyjson_mut_doc_set_root(document.value, root);
    yyjson_mut_obj_add_uint(document.value, root, "format_version", 1);
    auto* settings = yyjson_mut_obj_add_obj(document.value, root, "settings");
    const auto resolved = ResolveSettings({}, overrides);
    for (unsigned i = 0; i < static_cast<unsigned>(SettingsField::Count); ++i) {
        const auto field = static_cast<SettingsField>(i);
        if (!HasOverride(overrides, field)) { continue; }
        const auto name = SettingName(field);
        // Write integers directly, preserving the full uint64 range.
        switch (field) {
        case SettingsField::Language: AddString(document.value, settings, name.data(), View(resolved.language)); break;
        case SettingsField::ViewRadius: yyjson_mut_obj_add_sint(document.value, settings, name.data(), resolved.viewRadius); break;
        case SettingsField::AutosaveInterval: yyjson_mut_obj_add_uint(document.value, settings, name.data(), resolved.autosaveInterval); break;
        case SettingsField::ActiveRadius: yyjson_mut_obj_add_sint(document.value, settings, name.data(), resolved.activeRadius); break;
        case SettingsField::ResidentRadius: yyjson_mut_obj_add_sint(document.value, settings, name.data(), resolved.residentRadius); break;
        case SettingsField::Seed: yyjson_mut_obj_add_uint(document.value, settings, name.data(), resolved.seed); break;
        case SettingsField::TicksPerSecond: yyjson_mut_obj_add_uint(document.value, settings, name.data(), resolved.pacing.ticksPerSecond); break;
        case SettingsField::RateLimited: yyjson_mut_obj_add_bool(document.value, settings, name.data(), resolved.pacing.rateLimited); break;
        case SettingsField::PauseWhenIdle: yyjson_mut_obj_add_bool(document.value, settings, name.data(), resolved.pacing.pauseWhenIdle); break;
        case SettingsField::Count: std::abort();
        }
    }
    return WriteDocument(path, document.value, diagnostics);
}
SettingsResult<void> SaveKeymapFile(const char* path, const Keymap& overrides, Diagnostics* diagnostics) {
    if (!ValidateKeymap(overrides, diagnostics)) { return eastl::unexpected(SettingsError::InvalidData); }
    const auto existing = LoadKeymapFile(path, diagnostics);
    if (!existing) { return eastl::unexpected(existing.error()); }
    MutableDocument document;
    auto* root = yyjson_mut_obj(document.value);
    yyjson_mut_doc_set_root(document.value, root);
    yyjson_mut_obj_add_uint(document.value, root, "format_version", 1);
    auto* bindings = yyjson_mut_obj_add_arr(document.value, root, "bindings");
    for (const auto& binding : overrides) {
        auto* entry = yyjson_mut_arr_add_obj(document.value, bindings);
        AddString(document.value, entry, "context", ContextName(binding.context));
        AddString(document.value, entry, "action", ActionName(binding.action));
        auto* keys = yyjson_mut_obj_add_arr(document.value, entry, "keys");
        for (const auto& key : binding.keys) {
            auto* chord = yyjson_mut_arr_add_obj(document.value, keys);
            AddString(document.value, chord, "key", View(key.key));
            yyjson_mut_obj_add_bool(document.value, chord, "ctrl", key.ctrl);
        }
    }
    return WriteDocument(path, document.value, diagnostics);
}
SettingsResult<LanguageCatalog> ParseLanguageCatalog(eastl::string_view text, Diagnostics* diagnostics) {
    Document document;
    const auto parsed = ParseDocument(text, document, diagnostics);
    if (!parsed) { return eastl::unexpected(parsed.error()); }
    auto* root = yyjson_doc_get_root(document.value);
    if (!Object(root, {"format_version", "messages"}, diagnostics)) { return eastl::unexpected(SettingsError::InvalidData); }
    const auto version = Version(root, diagnostics);
    if (!version) { return eastl::unexpected(version.error()); }
    auto* messages = yyjson_obj_get(root, "messages");
    if (!Object(messages, {}, diagnostics)) { return eastl::unexpected(SettingsError::InvalidData); }
    LanguageCatalog catalog;
    std::size_t index, count;
    yyjson_val* key; yyjson_val* value;
    yyjson_obj_foreach(messages, index, count, key, value) {
        LocalizedMessage message;
        message.key.assign(yyjson_get_str(key), yyjson_get_len(key));
        if (message.key.empty()) { return eastl::unexpected(SettingsError::InvalidData); }
        if (StringValue(value)) { message.one.assign(yyjson_get_str(value), yyjson_get_len(value)); }
        else {
            if (!Object(value, {"one", "other"}, diagnostics)) { return eastl::unexpected(SettingsError::InvalidData); }
            auto* one = yyjson_obj_get(value, "one"); auto* other = yyjson_obj_get(value, "other");
            if (!StringValue(one) || !StringValue(other)) { Report(diagnostics, "expected one/other strings"); return eastl::unexpected(SettingsError::InvalidData); }
            message.one.assign(yyjson_get_str(one), yyjson_get_len(one));
            message.other = String(yyjson_get_str(other), yyjson_get_len(other));
        }
        catalog.push_back(eastl::move(message));
    }
    if (!ValidateLanguageCatalog(catalog, {}, diagnostics)) { return eastl::unexpected(SettingsError::InvalidData); }
    return catalog;
}
SettingsResult<LanguageCatalog> LoadLanguageCatalog(const char* path, Diagnostics* diagnostics) {
    auto file = Read(path, diagnostics);
    if (!file) { return eastl::unexpected(file.error()); }
    if (!*file) { return LanguageCatalog{}; }
    auto parsed = ParseLanguageCatalog(View(**file), diagnostics);
    if (!parsed) { Report(diagnostics, "while loading localization", path); }
    return parsed;
}
Vector<String> AvailableLanguages(const char* directory) {
    Vector<String> languages{"en"};
    std::error_code error;
    fs::directory_iterator iterator(Path(directory), error);
    const fs::directory_iterator end;
    while (!error && iterator != end) {
        if (iterator->is_regular_file(error) && iterator->path().extension() == ".json5") {
            const auto name = Utf8(iterator->path().stem());
            Settings check; check.language = name;
            if (name != "en" && ValidateSettings(check)) { languages.push_back(name); }
        }
        iterator.increment(error);
    }
    eastl::sort(languages.begin(), languages.end());
    return languages;
}

}
