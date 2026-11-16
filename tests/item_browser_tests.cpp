#include "client/item_browser.h"

#include <catch2/catch_test_macros.hpp>
#include <EASTL/algorithm.h>
#include <EASTL/utility.h>

namespace {
using namespace cataclysmx;

ContentCatalog BrowserContent() {
    const ContentSource source{"item-browser.json5", R"({format_version:1,definitions:[
        {type:'flag',id:'waterproof',name:'Waterproof',description:'Keeps water out',kind:'property',domains:['item']},
        {type:'item',id:'z_tool',name:'Alpha Hammer',mass_g:500,volume_ml:250,
         description:'An unfamiliar but useful hammer.',art:['<g>H'],flags:['waterproof'],
         melee:{damage:[{type:'blunt',amount:600,penetration:125}],attack_cost:150,hands:2}},
        {type:'item',id:'a_supply',name:'Zulu Dressing',mass_g:20,volume_ml:50},
        {type:'recipe',id:'make',inputs:[],result:{item:'a_supply',count:1},work:10}
    ]})"};
    auto content = LoadContent({&source, 1});
    REQUIRE(content);
    return eastl::move(*content);
}

struct BrowserFixture {
    ContentCatalog content = BrowserContent();
    Localization localization; // Unknown localization keys use the authored fallback.
    Keymap keys = DefaultKeymap();
    ItemBrowser browser;

    BrowserFixture() { browser.Open(content, localization); }
    void Key(KeyChord key) { browser.Handle(key, keys, content, localization); }
    void Search(eastl::string_view text) {
        Key({"/"});
        Key({"u", true});
        browser.Text(text);
    }
};

TEST_CASE("Item browser debug binding lists stable IDs and omits unfinished craft wrappers", "[item-browser]") {
    BrowserFixture f;
    CHECK(ResolveAction(f.keys, InputContext::Debug, {"i"}) == InputAction::Inventory);
    const auto supply = f.content.FindItem("a_supply");
    const auto tool = f.content.FindItem("z_tool");
    REQUIRE(f.content.FindItem("unfinished_craft").value != 0);
    CHECK(f.browser.Frame().matches == Vector<ItemTypeId>{supply, tool});
    CHECK(supply.value < tool.value);
    CHECK(f.browser.Label(tool) == "Alpha Hammer [z_tool]");
    CHECK(f.browser.Label(supply) == "Zulu Dressing [a_supply]");
}

TEST_CASE("Item browser searches names and IDs live and cancels back to the committed query", "[item-browser]") {
    BrowserFixture f;
    const auto tool = f.content.FindItem("z_tool");
    f.Search("aLpHa");
    CHECK(f.browser.Frame().matches == Vector<ItemTypeId>{tool});
    f.Key({"Enter"});
    CHECK(f.browser.Frame().query == "aLpHa");
    CHECK(f.browser.Frame().screen == ItemBrowserScreen::List);
    CHECK_FALSE(f.browser.TakeGiveRequest());

    f.Search("A_SUPPLY");
    CHECK(f.browser.Frame().matches == Vector<ItemTypeId>{f.content.FindItem("a_supply")});
    f.browser.Text("-no-match");
    CHECK(f.browser.Frame().matches.empty());
    CHECK(f.browser.Frame().selected == 0);
    f.Key({"Escape"});
    CHECK(f.browser.Frame().query == "aLpHa");
    CHECK(f.browser.Frame().matches == Vector<ItemTypeId>{tool});

    f.Search("nothing-matches");
    f.Key({"Enter"});
    for (const auto* key : {"Up", "Down", "End", "PageDown", "e", "m", "Enter"}) { f.Key({key}); }
    CHECK(f.browser.Frame().screen == ItemBrowserScreen::List);
    CHECK(f.browser.Frame().selected == 0);
    CHECK_FALSE(f.browser.TakeGiveRequest());
    CHECK_FALSE(f.browser.Frame().busy);
    CHECK_FALSE(f.browser.Frame().feedback.empty());
    f.Search("");
    CHECK(f.browser.Frame().matches.size() == 2);
    CHECK(f.browser.Frame().selected < f.browser.Frame().matches.size());
}

TEST_CASE("Item browser consumes separate text events and edits whole UTF8 scalars", "[item-browser]") {
    BrowserFixture f;
    f.Key({"/"});
    f.Key({"a"});
    CHECK(f.browser.Frame().input.empty());
    f.browser.Text("a\xc3\xa9\xe7\x8c\xab\xf0\x9f\x98\x80");
    f.Key({"Backspace"});
    CHECK(f.browser.Frame().input == "a\xc3\xa9\xe7\x8c\xab");
    f.Key({"Backspace"});
    CHECK(f.browser.Frame().input == "a\xc3\xa9");
    f.Key({"Backspace"});
    CHECK(f.browser.Frame().input == "a");
    f.Key({"u", true});
    CHECK(f.browser.Frame().input.empty());
    CHECK(f.browser.Frame().matches.size() == 2);
    f.Key({"Backspace"});
    CHECK(f.browser.Frame().input.empty());
}

TEST_CASE("Item browser enforces UTF8 byte bounds without accepting partial text", "[item-browser]") {
    BrowserFixture f;
    f.Key({"/"});
    String text(126, 'a');
    f.browser.Text({text.data(), text.size()});
    f.browser.Text("\xc3\xa9");
    REQUIRE(f.browser.Frame().input.size() == 128);
    const auto full = f.browser.Frame().input;
    f.browser.Text("x");
    CHECK(f.browser.Frame().input == full);
    CHECK_FALSE(f.browser.Frame().feedback.empty());
    f.Key({"Backspace"});
    CHECK(f.browser.Frame().input == text);
    f.browser.Text("\xe7\x8c\xab"); // Only two bytes remain; reject the entire scalar.
    CHECK(f.browser.Frame().input == text);
    f.Key({"u", true});
    for (const auto* invalid : {"\xc3", "\x80", "\xc0\xaf", "hello\n", "\x7f"}) {
        f.browser.Text(invalid);
        CHECK(f.browser.Frame().input.empty());
    }
    f.Key({"Escape"});
    f.Key({"m"});
    String amount(14, '1');
    f.browser.Text({amount.data(), amount.size()});
    f.browser.Text("\xc3\xa9");
    REQUIRE(f.browser.Frame().input.size() == 16);
    const auto fullAmount = f.browser.Frame().input;
    f.browser.Text("1");
    CHECK(f.browser.Frame().input == fullAmount);
    f.Key({"Backspace"});
    CHECK(f.browser.Frame().input == amount);
    f.Key({"u", true});
    CHECK(f.browser.Frame().input.empty());
}

TEST_CASE("Item browser examination borrows canonical art and shows definition details without giving", "[item-browser]") {
    BrowserFixture f;
    const auto type = f.content.FindItem("z_tool");
    f.Search("z_tool");
    f.Key({"Enter"});
    f.Key({"e"});
    REQUIRE(f.browser.Frame().screen == ItemBrowserScreen::Examine);
    const auto& sheet = f.browser.Frame().examination.examination;
    CHECK(sheet.type == type);
    CHECK(sheet.name == "Alpha Hammer");
    CHECK(sheet.description == "An unfamiliar but useful hammer.");
    CHECK(sheet.art == &f.content.GetItem(type).art);
    for (const auto* line : {"Flag: waterproof | Keeps water out", "ID: z_tool",
        "Attack cost: 150 ticks | Hands: 2",
        "Blunt damage: 6.00 HP | Penetration: 1.25 HP | Normal player damage: 6.00 HP"}) {
        CHECK(eastl::find(sheet.stats.begin(), sheet.stats.end(), String(line)) != sheet.stats.end());
    }
    CHECK_FALSE(f.browser.TakeGiveRequest());
    f.Key({"Enter"});
    CHECK(f.browser.Frame().screen == ItemBrowserScreen::List);
    CHECK_FALSE(f.browser.TakeGiveRequest());
    CHECK_FALSE(f.browser.Frame().busy);
}

TEST_CASE("Item browser queues Enter once and blocks duplicates until completion", "[item-browser]") {
    BrowserFixture f;
    const auto type = f.browser.Frame().matches.front();
    f.Key({"Enter"});
    f.Key({"Down"});
    f.Key({"Enter"});
    f.Key({"m"});
    CHECK(f.browser.Frame().screen == ItemBrowserScreen::List);
    REQUIRE(f.browser.Frame().busy);
    const auto request = f.browser.TakeGiveRequest();
    REQUIRE(request);
    CHECK(request->type == type);
    CHECK(request->count == 1);
    f.Key({"Enter"});
    CHECK_FALSE(f.browser.TakeGiveRequest());
    f.browser.GiveFinished({DebugGiveOutcome::Given, 1});
    CHECK_FALSE(f.browser.Frame().busy);
    CHECK(f.browser.Frame().feedback == "Gave 1 item.");
    f.Key({"Enter"});
    REQUIRE(f.browser.TakeGiveRequest());
    f.browser.GiveRejected("Host unavailable");
    CHECK_FALSE(f.browser.Frame().busy);
    CHECK(f.browser.Frame().feedback == "Host unavailable");
}

TEST_CASE("Item browser amount accepts only whole counts from one through one thousand", "[item-browser]") {
    BrowserFixture f;
    for (const auto* invalid : {"", "0", "1001", "9999999999999999", "4294967296", "1x", "1.5", "-1", "+1", " 1", "1 "}) {
        CAPTURE(invalid);
        f.Key({"m"});
        f.browser.Text(invalid);
        f.Key({"Enter"});
        CHECK(f.browser.Frame().screen == ItemBrowserScreen::Amount);
        CHECK(f.browser.Frame().input == invalid);
        CHECK_FALSE(f.browser.Frame().feedback.empty());
        CHECK_FALSE(f.browser.TakeGiveRequest());
        CHECK_FALSE(f.browser.Frame().busy);
        f.Key({"Escape"});
        CHECK(f.browser.Frame().screen == ItemBrowserScreen::List);
    }
    for (const std::uint32_t count : {1u, 1000u}) {
        f.Key({"m"});
        const auto text = UnsignedText(count);
        f.browser.Text({text.data(), text.size()});
        f.Key({"Enter"});
        const auto request = f.browser.TakeGiveRequest();
        REQUIRE(request);
        CHECK(request->count == count);
        CHECK(f.browser.Frame().screen == ItemBrowserScreen::List);
        f.browser.GiveFinished({DebugGiveOutcome::Given, count});
    }
    f.Key({"m"});
    f.browser.Text("42");
    f.Key({"Escape"});
    CHECK_FALSE(f.browser.TakeGiveRequest());
    CHECK(f.browser.Frame().input.empty());
    f.Key({"Escape"});
    CHECK_FALSE(f.browser.IsOpen());
}

TEST_CASE("Item browser close cancels unsent grants but retains outstanding work across reopen", "[item-browser]") {
    BrowserFixture f;
    f.Key({"Enter"});
    f.browser.Close();
    CHECK_FALSE(f.browser.TakeGiveRequest());
    f.browser.Open(f.content, f.localization);
    CHECK_FALSE(f.browser.Frame().busy);
    f.Key({"Enter"});
    REQUIRE(f.browser.TakeGiveRequest());
    f.browser.Close();
    f.browser.Open(f.content, f.localization);
    CHECK(f.browser.Frame().busy);
    f.Key({"Enter"});
    CHECK_FALSE(f.browser.TakeGiveRequest());
    f.browser.GiveFinished({DebugGiveOutcome::CapacityExceeded});
    CHECK_FALSE(f.browser.Frame().busy);
    CHECK(f.browser.Frame().feedback.find("capacity") != String::npos);
    f.Key({"m"});
    f.browser.Text("2");
    f.Key({"Enter"});
    REQUIRE(f.browser.TakeGiveRequest());
    f.browser.GiveFinished({DebugGiveOutcome::Given, 2});
    CHECK(f.browser.Frame().feedback == "Gave 2 items.");
}
}
