#pragma once

#include "client/settings.h"

#include <initializer_list>

namespace cataclysmx {

struct LocalizedMessage {
    String key;
    String one;
    eastl::optional<String> other;
};
using LanguageCatalog = Vector<LocalizedMessage>;
struct TextArgument {
    eastl::string_view name;
    String value;
};

SettingsResult<LanguageCatalog> ParseLanguageCatalog(eastl::string_view text,
    Diagnostics* diagnostics = nullptr);
SettingsResult<LanguageCatalog> LoadLanguageCatalog(const char* path,
    Diagnostics* diagnostics = nullptr);
SettingsResult<void> ValidateLanguageCatalog(const LanguageCatalog& catalog,
    const LanguageCatalog& english, Diagnostics* diagnostics = nullptr);
Vector<String> AvailableLanguages(const char* directory);
String NumberText(std::int64_t number);
String UnsignedText(std::uint64_t number);

class Localization {
public:
    // English is required. An absent translation falls back to English; a broken
    // existing translation is an error. Failed loads preserve this object.
    SettingsResult<void> Load(const char* directory, eastl::string_view language,
        Diagnostics* diagnostics = nullptr);
    SettingsResult<void> SetCatalogs(LanguageCatalog english, LanguageCatalog translated,
        Diagnostics* diagnostics = nullptr);
    String Text(eastl::string_view key, eastl::string_view fallback = {}) const;
    String Format(eastl::string_view key, std::initializer_list<TextArgument> arguments,
        std::uint64_t count = 1) const;
    String OutcomeText(Outcome outcome) const;
    String ResultText(const CommandResult& result) const;
    String ItemName(const ItemDefinition& definition) const;
    String ItemName(const ContentCatalog& content, ItemTypeId type) const;
    String RenderItemText(const ItemText& text) const;
    String ItemActionText(const ItemActionInfo& action) const;
    String ItemRefusal(ItemUseOutcome outcome, const ItemUseReason& reason) const;
    String TerrainName(const TerrainDefinition& terrain) const;
private:
    const LocalizedMessage* Find(eastl::string_view key) const;
    LanguageCatalog english_;
    LanguageCatalog translated_;
};

}
