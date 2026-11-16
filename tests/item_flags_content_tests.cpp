#include "content/content.h"
#include "content/definition_flags.h"

#include <catch2/catch_test_macros.hpp>
#include <initializer_list>

namespace {
using namespace cataclysmx;

eastl::string_view View(const String& text) { return {text.data(), text.size()}; }

ContentResult<ContentCatalog> Load(eastl::string_view definitions, Diagnostics* diagnostics = nullptr) {
    String text = "{format_version:1,definitions:[";
    text.append(definitions.data(), definitions.size());
    text += "]}";
    const ContentSource source{"flags.json5", View(text)};
    return LoadContent({&source, 1}, diagnostics);
}

String Item(eastl::string_view fields = {}) {
    String text = "{type:'item',id:'armor',name:'Armor',mass_g:1,volume_ml:1";
    if (!fields.empty()) { text.append(fields.data(), fields.size()); }
    text += '}';
    return text;
}

constexpr char Vocabulary[] =
    "{type:'flag',id:'armor',name:'Armor category',description:'Protective equipment',kind:'category',domains:['item']},"
    "{type:'flag',id:'waterproof',name:'Waterproof',description:'Keeps water out',kind:'property',domains:['object','item','terrain','monster']}";

void Invalid(eastl::string_view definitions) {
    Diagnostics diagnostics;
    const auto loaded = Load(definitions, &diagnostics);
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error() == ContentError::InvalidData);
    REQUIRE_FALSE(diagnostics.Empty());
    REQUIRE(diagnostics.Frames().front().location.has_value());
    CHECK(diagnostics.Frames().front().location->file == "flags.json5");
    CHECK_FALSE(diagnostics.Frames().front().location->path.empty());
    const auto silent = Load(definitions);
    REQUIRE_FALSE(silent.has_value());
    CHECK(silent.error() == ContentError::InvalidData);
}

TEST_CASE("Item flags resolve cross-source forward references in their own namespace", "[content][item-flags]") {
    const ContentSource sources[] = {
        {"items.json5", "{format_version:1,definitions:[{type:'item',id:'armor',name:'Armor',mass_g:1,volume_ml:1,flags:['waterproof','armor']}] }"},
        {"flags.json5", R"({format_version:1,definitions:[
            {type:'flag',id:'waterproof',name:'Waterproof',description:'Keeps water out',kind:'property',domains:['object','item','terrain','monster']},
            {type:'flag',id:'armor',name:'Armor category',description:'Protective equipment',kind:'category',domains:['item']}
        ]})"}
    };
    const auto loaded = LoadContent({sources, 2});
    REQUIRE(loaded.has_value());
    const ContentSource reversed[] = {sources[1], sources[0]};
    const auto reordered = LoadContent({reversed, 2});
    REQUIRE(reordered.has_value());
    CHECK(loaded->GetItem({1}) == reordered->GetItem({1}));
    CHECK(loaded->GetFlag({1}) == reordered->GetFlag({1}));
    CHECK(loaded->GetFlag({2}) == reordered->GetFlag({2}));
    CHECK(loaded->ItemCount() == 1);
    CHECK(loaded->FlagCount() == 2);
    CHECK(loaded->FindItem("armor") == ItemTypeId{1});
    CHECK(loaded->FindFlag("armor") == FlagId{1});
    CHECK(loaded->FindFlag("waterproof") == FlagId{2});
    CHECK(loaded->FindFlag("missing") == FlagId{});
    CHECK(loaded->TryGetFlag({}) == nullptr);
    CHECK(loaded->TryGetFlag({3}) == nullptr);
    CHECK(loaded->TryGetFlag({0xffffffff}) == nullptr);
    const auto& category = loaded->GetFlag({1});
    CHECK(loaded->TryGetFlag({1}) == &category);
    CHECK(category.id == "armor");
    CHECK(category.name == "Armor category");
    CHECK(category.description == "Protective equipment");
    CHECK(category.kind == FlagKind::Category);
    CHECK(category.domains.Contains(FlagDomain::Item));
    CHECK_FALSE(category.domains.Contains(FlagDomain::Monster));
    const auto& property = loaded->GetFlag({2});
    CHECK(property.kind == FlagKind::Property);
    for (auto domain : {FlagDomain::Item, FlagDomain::Monster, FlagDomain::Terrain, FlagDomain::Object}) {
        CHECK(property.domains.Contains(domain));
    }
    const auto& item = loaded->GetItem(loaded->FindItem("armor"));
    REQUIRE(item.flags.Size() == 2);
    CHECK(item.flags.Ids()[0] == FlagId{1});
    CHECK(item.flags.Ids()[1] == FlagId{2});
    CHECK(item.flags.Contains(loaded->FindFlag("armor")));
    CHECK_FALSE(item.flags.Contains({}));
    CHECK_FALSE(item.flags.Contains({3}));
    // Flags are descriptive metadata, not implicit capabilities.
    CHECK(item.capabilities.empty());
    CHECK(item.behaviors.empty());
    CHECK(item.energyStores.empty());
    CHECK(ItemContainer(item) == nullptr);
}

TEST_CASE("Flag vocabulary and attachment order produce canonical catalog values", "[content][item-flags]") {
    String first = Vocabulary;
    first += ',';
    first += Item(",flags:['waterproof','armor'],capabilities:[{type:'container',id:'bag',capacity_volume_ml:9}]");
    const auto loaded = Load(View(first));
    const auto reordered = Load(R"(
        {type:'item',id:'armor',name:'Armor',mass_g:1,volume_ml:1,flags:['armor','waterproof'],capabilities:[{type:'container',id:'bag',capacity_volume_ml:9}]},
        {type:'flag',id:'waterproof',name:'Waterproof',description:'Keeps water out',kind:'property',domains:['monster','terrain','item','object']},
        {type:'flag',id:'armor',name:'Armor category',description:'Protective equipment',kind:'category',domains:['item']}
    )");
    REQUIRE(loaded.has_value());
    REQUIRE(reordered.has_value());
    CHECK(loaded->GetFlag({1}) == reordered->GetFlag({1}));
    CHECK(loaded->GetFlag({2}) == reordered->GetFlag({2}));
    const auto& item = loaded->GetItem({1});
    CHECK(item == reordered->GetItem({1}));
    REQUIRE(ItemContainer(item) != nullptr);
    CHECK(ItemContainer(item)->capacityVolumeMl == 9);
    auto withoutFlags = item;
    withoutFlags.flags = FlagSet{};
    CHECK_FALSE(item == withoutFlags);
    CHECK(item.capabilities == withoutFlags.capabilities);
    CHECK(item.container == withoutFlags.container);
}

TEST_CASE("Absent and empty item flags have identical empty values", "[content][item-flags]") {
    const auto absent = Load(View(Item()));
    const auto empty = Load(View(Item(",flags:[]")));
    REQUIRE(absent.has_value());
    REQUIRE(empty.has_value());
    CHECK(absent->FlagCount() == 0);
    CHECK(absent->GetItem({1}).flags.Empty());
    CHECK(absent->GetItem({1}).flags.Ids().empty());
    CHECK(absent->GetItem({1}) == empty->GetItem({1}));
    const auto vocabularyOnly = Load(Vocabulary);
    REQUIRE(vocabularyOnly.has_value());
    CHECK(vocabularyOnly->FlagCount() == 2);
    CHECK(vocabularyOnly->ItemCount() == 0);
}

TEST_CASE("Item flag attachments reject unknown ineligible duplicate and malformed names", "[content][item-flags]") {
    for (const char* fields : {",flags:['missing']", ",flags:['armor','armor']", ",flags:null",
        ",flags:{}", ",flags:'armor'", ",flags:[1]", ",flags:[null]", ",flags:['']",
        ",flags:['Armor']", ",flags:['armor'],flags:[]"}) {
        CAPTURE(fields);
        String definitions = Vocabulary;
        definitions += ',';
        definitions += Item(fields);
        Invalid(View(definitions));
    }
    for (const char* domain : {"monster", "terrain", "object"}) {
        CAPTURE(domain);
        String definitions = "{type:'flag',id:'armor',name:'Armor',description:'Other domain',kind:'property',domains:['";
        definitions += domain;
        definitions += "']},";
        definitions += Item(",flags:['armor']");
        Invalid(View(definitions));
        // The vocabulary itself accepts domains with no attachment consumers yet.
        const auto itemStart = definitions.find(",{type:'item'");
        REQUIRE(itemStart != String::npos);
        definitions.erase(itemStart);
        const auto standalone = Load(View(definitions));
        REQUIRE(standalone.has_value());
        CHECK(standalone->FlagCount() == 1);
    }
}

TEST_CASE("Flag definitions reject malformed metadata and duplicate vocabulary IDs", "[content][item-flags]") {
    for (const char* definition : {
        "{type:'flag',id:'x',name:'X',description:'D',kind:'property',domains:['item'],extra:1}",
        "{type:'flag',id:'x',name:'X',name:'X',description:'D',kind:'property',domains:['item']}",
        "{type:'flag',id:'x',name:'X',description:'D',description:'D',kind:'property',domains:['item']}",
        "{type:'flag',id:'x',name:'X',description:'D',kind:'property',kind:'category',domains:['item']}",
        "{type:'flag',id:'x',name:'X',description:'D',kind:'property',domains:['item'],domains:['item']}",
        "{type:'flag',name:'X',description:'D',kind:'property',domains:['item']}",
        "{type:'flag',id:1,name:'X',description:'D',kind:'property',domains:['item']}",
        "{type:'flag',id:'Bad',name:'X',description:'D',kind:'property',domains:['item']}",
        "{type:'flag',id:'x',description:'D',kind:'property',domains:['item']}",
        "{type:'flag',id:'x',name:1,description:'D',kind:'property',domains:['item']}",
        "{type:'flag',id:'x',name:'',description:'D',kind:'property',domains:['item']}",
        "{type:'flag',id:'x',name:'X',kind:'property',domains:['item']}",
        "{type:'flag',id:'x',name:'X',description:null,kind:'property',domains:['item']}",
        "{type:'flag',id:'x',name:'X',description:'',kind:'property',domains:['item']}",
        "{type:'flag',id:'x',name:'X',description:'D',domains:['item']}",
        "{type:'flag',id:'x',name:'X',description:'D',kind:1,domains:['item']}",
        "{type:'flag',id:'x',name:'X',description:'D',kind:'Property',domains:['item']}",
        "{type:'flag',id:'x',name:'X',description:'D',kind:'unknown',domains:['item']}",
        "{type:'flag',id:'x',name:'X',description:'D',kind:'property'}",
        "{type:'flag',id:'x',name:'X',description:'D',kind:'property',domains:[]}",
        "{type:'flag',id:'x',name:'X',description:'D',kind:'property',domains:'item'}",
        "{type:'flag',id:'x',name:'X',description:'D',kind:'property',domains:null}",
        "{type:'flag',id:'x',name:'X',description:'D',kind:'property',domains:[1]}",
        "{type:'flag',id:'x',name:'X',description:'D',kind:'property',domains:['Item']}",
        "{type:'flag',id:'x',name:'X',description:'D',kind:'property',domains:['unknown']}",
        "{type:'flag',id:'x',name:'X',description:'D',kind:'property',domains:['item','item']}"
    }) {
        CAPTURE(definition);
        Invalid(definition);
    }
    String duplicates = Vocabulary;
    duplicates += ',';
    duplicates += Vocabulary;
    Invalid(View(duplicates));
    const ContentSource duplicateSources[] = {
        {"first.json5", "{format_version:1,definitions:[{type:'flag',id:'same',name:'Same',description:'D',kind:'property',domains:['item']}] }"},
        {"second.json5", "{format_version:1,definitions:[{type:'flag',id:'same',name:'Same',description:'D',kind:'property',domains:['item']}] }"}
    };
    const auto duplicate = LoadContent({duplicateSources, 2});
    REQUIRE_FALSE(duplicate.has_value());
    CHECK(duplicate.error() == ContentError::InvalidData);
}

TEST_CASE("A bad flag reference prevents publication and does not contaminate later loads", "[content][item-flags]") {
    String definitions = Vocabulary;
    definitions += ',';
    definitions += Item(",flags:['armor']");
    definitions += ",{type:'item',id:'bad',name:'Bad',mass_g:1,volume_ml:1,flags:['missing']}";
    Invalid(View(definitions));
    definitions.erase(definitions.find(",{type:'item',id:'bad'"));
    const auto valid = Load(View(definitions));
    REQUIRE(valid.has_value());
    CHECK(valid->ItemCount() == 1);
    CHECK(valid->FlagCount() == 2);
    CHECK(valid->FindItem("bad") == ItemTypeId{});
}

}
