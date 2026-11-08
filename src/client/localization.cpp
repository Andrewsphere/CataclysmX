#include "client/localization.h"

#include <EASTL/sort.h>
#include <EASTL/utility.h>

#include <charconv>
#include <cstdlib>

namespace cataclysmx {
namespace {

eastl::string_view View(const String& text) { return {text.data(), text.size()}; }
String Missing(eastl::string_view key) {
    String result = "["; result.append(key.data(), key.size()); result += ']'; return result;
}
bool Placeholders(eastl::string_view text, Vector<String>& names) {
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '{') {
            if (i + 1 < text.size() && text[i + 1] == '{') { ++i; continue; }
            const auto end = text.find('}', i + 1);
            if (end == eastl::string_view::npos || end == i + 1) { return false; }
            const auto name = text.substr(i + 1, end - i - 1);
            for (const char ch : name) {
                if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_')) { return false; }
            }
            const String owned(name.data(), name.size());
            bool found = false;
            for (const auto& current : names) { found |= current == owned; }
            if (!found) { names.push_back(owned); }
            i = end;
        } else if (text[i] == '}') {
            if (i + 1 >= text.size() || text[i + 1] != '}') { return false; }
            ++i;
        }
    }
    eastl::sort(names.begin(), names.end());
    return true;
}
const LocalizedMessage* FindMessage(const LanguageCatalog& catalog, eastl::string_view key) {
    for (const auto& message : catalog) { if (View(message.key) == key) { return &message; } }
    return nullptr;
}
template<class T> String Decimal(T number) {
    char buffer[32];
    const auto converted = std::to_chars(buffer, buffer + sizeof(buffer), number);
    if (converted.ec != std::errc{}) { std::abort(); }
    return String(buffer, static_cast<std::size_t>(converted.ptr - buffer));
}

}

String NumberText(std::int64_t number) { return Decimal(number); }
String UnsignedText(std::uint64_t number) { return Decimal(number); }
SettingsResult<void> ValidateLanguageCatalog(const LanguageCatalog& catalog,
    const LanguageCatalog& english, Diagnostics* diagnostics) {
    const auto fail = [&](const LocalizedMessage& message) -> SettingsResult<void> {
        if (diagnostics) {
            DiagnosticFrame frame;
            frame.message = "duplicate message or invalid/mismatched placeholders";
            frame.location = DiagnosticLocation{{}, message.key, {}, {}};
            diagnostics->Add(eastl::move(frame));
        }
        return eastl::unexpected(SettingsError::InvalidData);
    };
    for (std::size_t i = 0; i < catalog.size(); ++i) {
        const auto& message = catalog[i];
        for (std::size_t j = 0; j < i; ++j) { if (catalog[j].key == message.key) { return fail(message); } }
        Vector<String> names;
        if (!Placeholders(View(message.one), names)) { return fail(message); }
        if (message.other) {
            Vector<String> otherNames;
            if (!Placeholders(View(*message.other), otherNames) || names != otherNames) { return fail(message); }
        }
        if (const auto* base = FindMessage(english, View(message.key))) {
            Vector<String> baseNames;
            if (!Placeholders(View(base->one), baseNames) || names != baseNames
                || message.other.has_value() != base->other.has_value()) { return fail(message); }
        }
    }
    return {};
}
SettingsResult<void> Localization::SetCatalogs(LanguageCatalog english, LanguageCatalog translated,
    Diagnostics* diagnostics) {
    auto result = ValidateLanguageCatalog(english, {}, diagnostics);
    if (!result) { return result; }
    result = ValidateLanguageCatalog(translated, english, diagnostics);
    if (!result) { return result; }
    english_ = eastl::move(english); translated_ = eastl::move(translated);
    return {};
}
SettingsResult<void> Localization::Load(const char* directory, eastl::string_view language, Diagnostics* diagnostics) {
    Settings check; check.language.assign(language.data(), language.size());
    if (!ValidateSettings(check, diagnostics)) { return eastl::unexpected(SettingsError::InvalidData); }
    auto english = LoadLanguageCatalog(JoinConfigurationPath(directory, "en.json5").c_str(), diagnostics);
    if (!english) { return eastl::unexpected(english.error()); }
    LanguageCatalog translated;
    if (language != "en") {
        String file(language.data(), language.size()); file += ".json5";
        auto loaded = LoadLanguageCatalog(JoinConfigurationPath(directory, View(file)).c_str(), diagnostics);
        // Missing catalogs are represented by an empty catalog; English itself
        // must contain at least one message, checked below.
        if (!loaded) { return eastl::unexpected(loaded.error()); }
        translated = eastl::move(*loaded);
    }
    if (english->empty()) {
        if (diagnostics) { diagnostics->Add("English localization catalog is missing or empty"); }
        return eastl::unexpected(SettingsError::InvalidData);
    }
    return SetCatalogs(eastl::move(*english), eastl::move(translated), diagnostics);
}
const LocalizedMessage* Localization::Find(eastl::string_view key) const {
    if (const auto* found = FindMessage(translated_, key)) { return found; }
    return FindMessage(english_, key);
}
String Localization::Text(eastl::string_view key, eastl::string_view fallback) const {
    if (const auto* found = Find(key)) { return found->one; }
    return fallback.empty() ? Missing(key) : String(fallback.data(), fallback.size());
}
String Localization::Format(eastl::string_view key, std::initializer_list<TextArgument> arguments,
    std::uint64_t count) const {
    const auto* message = Find(key);
    if (!message) { return Missing(key); }
    const auto& source = count != 1 && message->other ? *message->other : message->one;
    String result;
    for (std::size_t i = 0; i < source.size(); ++i) {
        if (source[i] == '{') {
            if (i + 1 < source.size() && source[i + 1] == '{') { result += '{'; ++i; continue; }
            const auto end = source.find('}', i + 1);
            if (end == String::npos) { return Missing(key); }
            const eastl::string_view name(source.data() + i + 1, end - i - 1);
            bool found = false;
            for (const auto& argument : arguments) {
                if (argument.name == name) { result += argument.value; found = true; break; }
            }
            if (!found) { return Missing(key); }
            i = end;
        } else if (source[i] == '}' && i + 1 < source.size() && source[i + 1] == '}') {
            result += '}'; ++i;
        } else { result += source[i]; }
    }
    return result;
}
String Localization::OutcomeText(Outcome outcome) const {
    switch (outcome) {
    case Outcome::waited: return Text("outcome.waited");
    case Outcome::moved: return Text("outcome.moved");
    case Outcome::picked_up: return Text("outcome.picked_up");
    case Outcome::dropped: return Text("outcome.dropped");
    case Outcome::opened: return Text("outcome.opened");
    case Outcome::closed: return Text("outcome.closed");
    case Outcome::toggled: return Text("outcome.toggled");
    case Outcome::blocked: return Text("outcome.blocked");
    case Outcome::occupied: return Text("outcome.occupied");
    case Outcome::nothing_here: return Text("outcome.nothing_here");
    case Outcome::invalid_command: return Text("outcome.invalid_command");
    case Outcome::not_carried: return Text("outcome.not_carried");
    case Outcome::unavailable: return Text("outcome.unavailable");
    case Outcome::not_ready: return Text("outcome.not_ready");
    case Outcome::queued: return Text("outcome.queued");
    }
    std::abort();
}
String Localization::ItemName(ItemKind kind) const {
    switch (kind) {
    case ItemKind::rock: return Text("item.rock.name");
    case ItemKind::backpack: return Text("item.backpack.name");
    case ItemKind::bandage: return Text("item.bandage.name");
    }
    std::abort();
}
String Localization::TerrainName(const TerrainDefinition& terrain) const {
    String key = "terrain."; key += terrain.id; key += ".name";
    return Text(View(key), View(terrain.name));
}

}
