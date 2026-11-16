#include "content/content.h"
#include "content/text_art.h"
#include "content_fixture.h"

#include <catch2/catch_test_macros.hpp>
#include <initializer_list>

namespace {
using namespace cataclysmx;

eastl::string_view View(const String& text) { return {text.data(), text.size()}; }

ContentResult<ContentCatalog> Load(eastl::string_view fields, Diagnostics* diagnostics = nullptr) {
    String text = "{format_version:1,definitions:[{type:'item',id:'sample',name:'sample',mass_g:1,volume_ml:1";
    text.append(fields.data(), fields.size());
    text += "}]}";
    const ContentSource source{"presentation.json5", View(text)};
    return LoadContent({&source, 1}, diagnostics);
}

void InvalidFields(eastl::string_view fields) {
    Diagnostics diagnostics;
    const auto loaded = Load(fields, &diagnostics);
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error() == ContentError::InvalidData);
    REQUIRE_FALSE(diagnostics.Empty());
    REQUIRE(diagnostics.Frames().front().location.has_value());
    CHECK(diagnostics.Frames().front().location->file == "presentation.json5");
    CHECK_FALSE(Load(fields).has_value());
}

TEST_CASE("Item presentation is optional and missing melee shares an immutable default", "[content][item-presentation]") {
    const auto loaded = Load("");
    REQUIRE(loaded.has_value());
    const auto& item = loaded->GetItem(loaded->FindItem("sample"));
    CHECK(item.description.empty());
    CHECK(item.art.lines.empty());
    CHECK(item.art.width == 0);
    CHECK_FALSE(item.melee.has_value());
    const auto& profile = ItemMelee(item);
    REQUIRE(profile.damage.size() == 1);
    CHECK(profile.damage[0] == DamagePacket{DamageType::Blunt, 400, 0});
    CHECK(profile.attackCost == 100);
    CHECK(profile.hands == 1);
    const ItemDefinition another;
    CHECK(&profile == &ItemMelee(another));
    const auto empty = Load(",description:'',art:[]");
    REQUIRE(empty.has_value());
    CHECK(item == empty->GetItem(empty->FindItem("sample")));
}

TEST_CASE("UTF-8 art preserves spaces and empty lines and resets colors per line", "[content][item-presentation]") {
    const String lines[] = {" <r>é<red>Ω<reset><< > ", "", "Ж<blue>─█"};
    const auto parsed = ParseTextArt({lines, 3});
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->lines.size() == 3);
    CHECK(parsed->width == 7);
    REQUIRE(parsed->lines[0].size() == 3);
    CHECK(parsed->lines[0][0] == ArtRun{" ", ArtColor::Default, 1});
    CHECK(parsed->lines[0][1] == ArtRun{"éΩ", ArtColor::Red, 2});
    CHECK(parsed->lines[0][2] == ArtRun{"< > ", ArtColor::Default, 4});
    CHECK(parsed->lines[1].empty());
    REQUIRE(parsed->lines[2].size() == 2);
    CHECK(parsed->lines[2][0] == ArtRun{"Ж", ArtColor::Default, 1});
    CHECK(parsed->lines[2][1] == ArtRun{"─█", ArtColor::Blue, 2});
    const String canonical[] = {" <red>éΩ<default><< > ", "", "Ж<b>─█"};
    const auto equivalent = ParseTextArt({canonical, 3});
    REQUIRE(equivalent.has_value());
    CHECK(*parsed == *equivalent);
    const String resetLines[] = {"<red>a", "b"};
    const auto reset = ParseTextArt({resetLines, 2});
    REQUIRE(reset.has_value());
    CHECK(reset->lines[0][0].color == ArtColor::Red);
    CHECK(reset->lines[1][0].color == ArtColor::Default);
}

TEST_CASE("Art palette names and aliases produce typed runs", "[content][item-presentation]") {
    const String lines[] = {"<black>a<r>b<g>c<y>d<b>e<m>f<c>g<w>h<grey>i<darkgray>j<reset>k"};
    const auto parsed = ParseTextArt({lines, 1});
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->lines[0].size() == 11);
    const ArtColor colors[] = {ArtColor::Black, ArtColor::Red, ArtColor::Green, ArtColor::Yellow,
        ArtColor::Blue, ArtColor::Magenta, ArtColor::Cyan, ArtColor::White, ArtColor::Gray,
        ArtColor::DarkGray, ArtColor::Default};
    for (std::size_t i = 0; i < 11; ++i) {
        CHECK(parsed->lines[0][i].color == colors[i]);
        CHECK(parsed->lines[0][i].cells == 1);
    }
}

TEST_CASE("Art rejects malformed tags UTF-8 and unsupported cell widths with locations", "[content][item-presentation]") {
    for (const char* bad : {"<", "<red", "<>", "<orange>x", "</red>", "<r<g>>", "<RED>",
        "\t", "\n", "\r", "\x1b[31m", "\x7f", "\xc2\x85", "\xc0\xaf", "\xc3", "\x80",
        "\xed\xa0\x80", "\xf4\x90\x80\x80", "e\xcc\x81", "\xef\xb8\x8f", "\xe2\x80\x8b",
        "\xe2\x80\xae", "\xe4\xb8\xad", "\xf0\x9f\x98\x80", "\xc2\xad", "\xd2\x83"}) {
        CAPTURE(bad);
        const String lines[] = {"valid", String("ab") + bad};
        Diagnostics diagnostics;
        const auto parsed = ParseTextArt({lines, 2}, &diagnostics);
        REQUIRE_FALSE(parsed.has_value());
        CHECK(parsed.error() == ContentError::InvalidData);
        REQUIRE_FALSE(diagnostics.Empty());
        const auto& location = diagnostics.Frames().front().location;
        REQUIRE(location.has_value());
        REQUIRE(location->line.has_value());
        REQUIRE(location->column.has_value());
        CHECK(location->line.value() == 2);
        // In e + combining accent the ordinary e is valid; the accent starts at
        // the next byte. Every other case begins with an invalid token/scalar.
        CHECK(location->column.value() == (eastl::string_view(bad) == "e\xcc\x81" ? 4 : 3));
        CHECK(location->path == "[1]");
        CHECK_FALSE(ParseTextArt({lines, 2}).has_value());
    }
    const String nul(1, '\0');
    CHECK_FALSE(ParseTextArt({&nul, 1}).has_value());
    Diagnostics diagnostics;
    CHECK_FALSE(Load(",art:['ok','ab<bad>']", &diagnostics).has_value());
    REQUIRE_FALSE(diagnostics.Empty());
    REQUIRE(diagnostics.Frames().front().location.has_value());
    CHECK(diagnostics.Frames().front().location->path == "definitions[0].art[1]");
    REQUIRE(diagnostics.Frames().front().location->column.has_value());
    CHECK(diagnostics.Frames().front().location->column.value() == 3);
    const String byteColumn = "é<bad>";
    diagnostics.Clear();
    CHECK_FALSE(ParseTextArt({&byteColumn, 1}, &diagnostics).has_value());
    REQUIRE_FALSE(diagnostics.Empty());
    REQUIRE(diagnostics.Frames().front().location.has_value());
    REQUIRE(diagnostics.Frames().front().location->column.has_value());
    CHECK(diagnostics.Frames().front().location->column.value() == 3);
}

TEST_CASE("Art load limits bound cells lines and tagged source bytes", "[content][item-presentation]") {
    Vector<String> lines(128, String(256, ' '));
    const auto maximum = ParseTextArt({lines.data(), lines.size()});
    REQUIRE(maximum.has_value());
    CHECK(maximum->width == 256);
    lines[0] += 'x';
    CHECK_FALSE(ParseTextArt({lines.data(), lines.size()}).has_value());
    lines[0].clear();
    lines.emplace_back();
    CHECK_FALSE(ParseTextArt({lines.data(), lines.size()}).has_value());
    String tagged;
    for (int i = 0; i < 1024; ++i) { tagged += "<r>"; }
    CHECK(ParseTextArt({&tagged, 1}).has_value());
    tagged.append(1025, ' ');
    CHECK_FALSE(ParseTextArt({&tagged, 1}).has_value());
    // Tags count toward source bytes even when they produce no cells.
    String tags;
    for (int i = 0; i < 1365; ++i) { tags += "<r>"; }
    lines.assign(32, tags); // 131040 bytes is within the total bound.
    CHECK(ParseTextArt({lines.data(), lines.size()}).has_value());
    lines.push_back(tags);
    CHECK_FALSE(ParseTextArt({lines.data(), lines.size()}).has_value());
}

TEST_CASE("Authored melee values and presentation participate in equality", "[content][item-presentation]") {
    const auto loaded = Load(",description:'A useful object.',art:['<g>▄'],melee:{damage:"
        "[{type:'slashing',amount:1250},{type:'blunt',amount:300,penetration:75}],attack_cost:150,hands:2}");
    REQUIRE(loaded.has_value());
    const auto& item = loaded->GetItem(loaded->FindItem("sample"));
    CHECK(item.description == "A useful object.");
    REQUIRE(item.melee.has_value());
    CHECK(&ItemMelee(item) == &*item.melee);
    CHECK(item.melee->attackCost == 150);
    CHECK(item.melee->hands == 2);
    REQUIRE(item.melee->damage.size() == 2);
    CHECK(item.melee->damage[0] == DamagePacket{DamageType::Slashing, 1250, 0});
    CHECK(item.melee->damage[1] == DamagePacket{DamageType::Blunt, 300, 75});
    auto changed = item;
    CHECK(item == changed);
    changed.description += '!';
    CHECK_FALSE(item == changed);
    changed = item;
    changed.art.lines[0][0].color = ArtColor::Blue;
    CHECK_FALSE(item == changed);
    changed = item;
    changed.melee->attackCost = 151;
    CHECK_FALSE(item == changed);
    changed = item;
    changed.melee->damage[0].amount += 1;
    CHECK_FALSE(item == changed);
    const auto defaults = Load(",melee:{damage:[{type:'blunt',amount:400}]}");
    REQUIRE(defaults.has_value());
    CHECK(ItemMelee(defaults->GetItem({1})) == ItemMelee(ItemDefinition{}));
    const auto explicitDefaults = Load(",melee:{hands:1,attack_cost:100,damage:[{penetration:0,amount:400,type:'blunt'}]}");
    REQUIRE(explicitDefaults.has_value());
    CHECK(defaults->GetItem({1}) == explicitDefaults->GetItem({1}));
}

TEST_CASE("Item presentation and melee schemas reject invalid authored values", "[content][item-presentation]") {
    for (const char* fields : {",description:5", ",description:null", ",description:'a\\u0000b'",
        ",art:null", ",art:'x'", ",art:[1]", ",art:['\\t']", ",art:['<unknown>']",
        ",melee:null", ",melee:[]", ",melee:{}", ",melee:{damage:[]}",
        ",melee:{damage:[{type:'Blunt',amount:1}]}", ",melee:{damage:[{type:'cut',amount:1}]}",
        ",melee:{damage:[{amount:1}]}", ",melee:{damage:[{type:'blunt'}]}",
        ",melee:{damage:[{type:'blunt',amount:0}]}", ",melee:{damage:[{type:'blunt',amount:-1}]}",
        ",melee:{damage:[{type:'blunt',amount:1.5}]}", ",melee:{damage:[{type:'blunt',amount:true}]}",
        ",melee:{damage:[{type:'blunt',amount:1000001}]}",
        ",melee:{damage:[{type:'blunt',amount:1,penetration:-1}]}",
        ",melee:{damage:[{type:'blunt',amount:1,penetration:1000001}]}",
        ",melee:{damage:[{type:'blunt',amount:1,extra:0}]}",
        ",melee:{damage:[{type:'blunt',amount:1,amount:1}]}",
        ",melee:{damage:[{type:'blunt',amount:1},{type:'blunt',amount:2}]}",
        ",melee:{damage:[{type:'blunt',amount:1}],attack_cost:0}",
        ",melee:{damage:[{type:'blunt',amount:1}],attack_cost:1000001}",
        ",melee:{damage:[{type:'blunt',amount:1}],attack_cost:true}",
        ",melee:{damage:[{type:'blunt',amount:1}],hands:0}",
        ",melee:{damage:[{type:'blunt',amount:1}],hands:3}",
        ",melee:{damage:[{type:'blunt',amount:1}],hands:1.5}",
        ",melee:{damage:[{type:'blunt',amount:1}],extra:1}",
        ",melee:{damage:[{type:'blunt',amount:1}],hands:1,hands:1}"}) {
        CAPTURE(fields);
        InvalidFields(fields);
    }
    const auto maximum = Load(",melee:{damage:[{type:'blunt',amount:1000000,penetration:1000000}],attack_cost:1000000,hands:2}");
    REQUIRE(maximum.has_value());
    for (const char* type : {"blunt", "slashing", "piercing", "ballistic", "heat", "chemical", "choking"}) {
        String fields = ",melee:{damage:[{type:'";
        fields += type;
        fields += "',amount:1}]}";
        CHECK(Load(View(fields)).has_value());
    }
}

TEST_CASE("Representative bundled items have descriptions art and authored melee", "[content][item-presentation]") {
    const auto& catalog = TestContent();
    for (const char* id : {"rock", "metal_pipe", "backpack"}) {
        const auto& item = catalog.GetItem(catalog.FindItem(id));
        CHECK_FALSE(item.description.empty());
        CHECK_FALSE(item.art.lines.empty());
        CHECK(item.art.width > 0);
    }
    CHECK(ItemMelee(catalog.GetItem(catalog.FindItem("rock"))).damage[0].amount == 600);
    const auto& pipe = ItemMelee(catalog.GetItem(catalog.FindItem("metal_pipe")));
    CHECK(pipe.damage[0].amount == 2000);
    CHECK(pipe.attackCost == 150);
    CHECK(pipe.hands == 2);
    CHECK_FALSE(catalog.GetItem(catalog.FindItem("backpack")).melee.has_value());
}

}
