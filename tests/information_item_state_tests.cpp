#include "content/content.h"
#include "core/item_state.h"
#include <catch2/catch_test_macros.hpp>

namespace {
using namespace cataclysmx;

ContentCatalog LibraryCatalog(bool earlierPackage = false) {
    String text = R"({format_version:1,definitions:[
      {type:'item',id:'usb',name:'USB',mass_g:1,volume_ml:1,
       digital_device:'storage',supplied_information:['supplied']},
      {type:'information',id:'supplied',name:'Software',software:true},
      {type:'information',id:'transferable',name:'Other software',software:true})";
    if (earlierPackage) {
        text += R"(,{type:'information',id:'aaa',name:'Earlier software',software:true})";
    }
    text += "]}";
    const ContentSource source{"library-state", text};
    auto loaded = LoadContent({&source, 1});
    REQUIRE(loaded);
    return eastl::move(*loaded);
}
DataObject Saved(const ContentCatalog& catalog, const ItemRuntime& runtime) {
    DataObject result;
    WriteItemState(catalog, catalog.GetItem(catalog.FindItem("usb")), runtime, result);
    return result;
}
DataObject Transfer() {
    DataObject transfer;
    transfer.SetUnsigned("worker", 7);
    transfer.SetUnsigned("source_item", 8);
    transfer.SetUnsigned("source_object", 0);
    transfer.SetUnsigned("host_item", 9);
    transfer.SetString("package", "transferable");
    transfer.SetUnsigned("remaining_ticks", 37);
    return transfer;
}

TEST_CASE("Information saves use canonical names and explicit empty libraries", "[item-state][digital]") {
    const auto catalog = LibraryCatalog();
    const auto& definition = catalog.GetItem(catalog.FindItem("usb"));
    const auto initial = Saved(catalog, MakeItemState(definition));
    const auto* library = initial.Find("information_library");
    REQUIRE(library);
    REQUIRE(library->Find("packages")->Size() == 1);
    CHECK(library->Find("packages")->At(0).StringValue() == "supplied");

    const auto reordered = LibraryCatalog(true);
    CHECK(reordered.FindInformation("supplied") != catalog.FindInformation("supplied"));
    auto read = ReadItemState(reordered, reordered.GetItem(reordered.FindItem("usb")), initial);
    REQUIRE(read);
    CHECK(Saved(reordered, *read) == initial);

    auto empty = initial;
    empty.Find("information_library")->Set("packages", DataObject::List());
    read = ReadItemState(catalog, definition, empty);
    REQUIRE(read);
    CHECK(Saved(catalog, *read) == empty);
    CHECK_FALSE(ReadItemState(catalog, definition, DataObject{}));
    CHECK_FALSE(ReadItemState(definition, initial));
}

TEST_CASE("Destination transfer saves preserve remaining paid time and reject corrupt state", "[item-state][digital]") {
    const auto catalog = LibraryCatalog();
    const auto& definition = catalog.GetItem(catalog.FindItem("usb"));
    auto saved = Saved(catalog, MakeItemState(definition));
    saved.Find("information_library")->Set("transfer", Transfer());
    auto read = ReadItemState(catalog, definition, saved);
    REQUIRE(read);
    CHECK(Saved(catalog, *read) == saved);
    for (const auto* key : {"worker", "source_item"}) {
        auto invalid = saved;
        invalid.Find("information_library")->Find("transfer")->SetUnsigned(key, 0);
        CHECK_FALSE(ReadItemState(catalog, definition, invalid));
    }
    auto bothSources = saved;
    bothSources.Find("information_library")->Find("transfer")->SetUnsigned("source_object", 3);
    CHECK_FALSE(ReadItemState(catalog, definition, bothSources));
    auto duplicate = saved;
    duplicate.Find("information_library")->Find("transfer")->SetString("package", "supplied");
    CHECK_FALSE(ReadItemState(catalog, definition, duplicate));
    auto missing = saved;
    missing.Find("information_library")->Find("transfer")->SetString("package", "absent");
    CHECK_FALSE(ReadItemState(catalog, definition, missing));
    auto wrongType = saved;
    wrongType.Find("information_library")->Find("transfer")->SetString("remaining_ticks", "37");
    CHECK_FALSE(ReadItemState(catalog, definition, wrongType));
}

TEST_CASE("Libraries reject unknown duplicate and malformed entries", "[item-state][digital]") {
    const auto catalog = LibraryCatalog();
    const auto& definition = catalog.GetItem(catalog.FindItem("usb"));
    const auto saved = Saved(catalog, MakeItemState(definition));
    for (auto entry : {DataObject::Text("missing"), DataObject::Text("supplied"), DataObject::Unsigned(1)}) {
        auto invalid = saved;
        invalid.Find("information_library")->Find("packages")->Append(entry);
        CHECK_FALSE(ReadItemState(catalog, definition, invalid));
    }
    auto version = saved;
    version.Find("information_library")->SetUnsigned("version", 2);
    auto read = ReadItemState(catalog, definition, version);
    REQUIRE_FALSE(read);
    CHECK(read.error() == StateError::UnsupportedVersion);
    auto extra = saved;
    extra.Find("information_library")->SetBoolean("unexpected", true);
    CHECK_FALSE(ReadItemState(catalog, definition, extra));
}
}
