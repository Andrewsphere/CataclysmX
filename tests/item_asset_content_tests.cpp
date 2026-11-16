#include "content/content.h"
#include "content/content_assets.h"
#include "items/item_registry.h"
#include "network/network.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <string>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
using namespace cataclysmx;
namespace fs = std::filesystem;

eastl::string_view View(const String& text) { return {text.data(), text.size()}; }
eastl::string_view View(const std::string& text) { return {text.data(), text.size()}; }

constexpr char Asset[] = "@text-art 1\n@color ember #DC5353\n---\n <ember>MM<reset>! ";

String Document(eastl::string_view art = "'/art/items/sample.txt'") {
    String text = "{format_version:1,definitions:[{type:'item',id:'sample',name:'Sample',mass_g:1,volume_ml:1,art:";
    text.append(art.data(), art.size());
    text += "}]}";
    return text;
}

struct MemoryAsset {
    String text = Asset;
    Vector<String> paths;
    bool fail = false;

    static ContentResult<String> Read(void* context, eastl::string_view path, Diagnostics* diagnostics) {
        auto& self = *static_cast<MemoryAsset*>(context);
        self.paths.emplace_back(path.data(), path.size());
        if (self.fail) {
            if (diagnostics) { diagnostics->Add("fixture asset unavailable"); }
            return eastl::unexpected(ContentError::Io);
        }
        return self.text;
    }

    ContentAssetReader Reader() { return {this, Read}; }
};

ContentResult<ContentCatalog> Load(eastl::string_view text, const ContentAssetReader& reader,
    Diagnostics* diagnostics = nullptr) {
    const ContentSource source{"nested/items.json5", text};
    return LoadContent({&source, 1}, reader, diagnostics);
}

// std filesystem/streams are confined to this file integration fixture.
struct AssetFiles {
    fs::path directory;

    AssetFiles() {
        std::error_code error;
        const auto parent = fs::temp_directory_path(error);
        REQUIRE_FALSE(error);
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (int attempt = 0; attempt < 100; ++attempt) {
            const auto candidate = parent / ("cataclysmx-item-assets-" + std::to_string(stamp) + "-" + std::to_string(attempt));
            if (fs::create_directory(candidate, error)) {
                directory = candidate;
                return;
            }
            REQUIRE_FALSE(error);
        }
        FAIL("could not create unique item asset fixture directory");
    }

    ~AssetFiles() {
        std::error_code ignored;
        if (!directory.empty()) { fs::remove_all(directory, ignored); }
    }

    AssetFiles(const AssetFiles&) = delete;
    AssetFiles& operator=(const AssetFiles&) = delete;

    void Write(const char* name, eastl::string_view bytes) const {
        const auto path = directory / name;
        std::error_code error;
        fs::create_directories(path.parent_path(), error);
        REQUIRE_FALSE(error);
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        REQUIRE(file.is_open());
        file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        REQUIRE(file.good());
        file.close();
        REQUIRE_FALSE(file.fail());
    }

    std::string Path(const char* name) const { return (directory / name).generic_string(); }
    std::string Root() const { return directory.generic_string(); }
};

TEST_CASE("External item art is cached per load and owned by each item", "[content][item-assets]") {
    MemoryAsset asset;
    const ContentSource sources[] = {
        {"one.json5", "{format_version:1,definitions:[{type:'item',id:'one',name:'One',mass_g:1,volume_ml:1,art:'/art/items/sample.txt'}]}"},
        {"two.json5", "{format_version:1,definitions:[{type:'item',id:'two',name:'Two',mass_g:1,volume_ml:1,art:'/art/items/sample.txt'}]}"}
    };
    auto loaded = LoadContent({sources, 2}, asset.Reader());
    REQUIRE(loaded.has_value());
    REQUIRE(asset.paths.size() == 1);
    CHECK(asset.paths[0] == "/art/items/sample.txt");
    const auto& one = loaded->GetItem(loaded->FindItem("one"));
    const auto& two = loaded->GetItem(loaded->FindItem("two"));
    CHECK(one.art == two.art);
    REQUIRE(one.art.lines.size() == 1);
    REQUIRE(one.art.lines[0].size() == 3);
    CHECK(one.art.width == 5);
    CHECK(one.art.lines[0][0] == ArtRun{" ", {}, 1});
    CHECK(one.art.lines[0][1] == ArtRun{"MM", ArtColor::Rgb(220, 83, 83), 2});
    CHECK(one.art.lines[0][2] == ArtRun{"! ", {}, 2});
    // Cached consumers receive independent value storage, not borrowed runs.
    CHECK(one.art.lines.data() != two.art.lines.data());
    CHECK(one.art.lines[0].data() != two.art.lines[0].data());
    auto changed = one;
    changed.art.lines[0][1].text = "NN";
    CHECK(two.art.lines[0][1].text == "MM");
    CHECK_FALSE(changed.art == two.art);
    asset.text = "@text-art 1\n---\nnew";
    const auto again = LoadContent({sources, 2}, asset.Reader());
    REQUIRE(again.has_value());
    CHECK(asset.paths.size() == 2);
    CHECK(again->GetItem({1}).art.lines[0][0].text == "new");
    CHECK(two.art.lines[0][1].text == "MM");
}

TEST_CASE("External art requires an explicit reader while inline art remains supported", "[content][item-assets]") {
    const auto text = Document();
    const ContentSource source{"items.json5", View(text)};
    Diagnostics diagnostics;
    const auto missing = LoadContent({&source, 1}, &diagnostics);
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error() == ContentError::InvalidData);
    REQUIRE_FALSE(diagnostics.Empty());
    const ContentAssetReader emptyReader;
    const auto empty = Load(View(text), emptyReader);
    REQUIRE_FALSE(empty.has_value());
    CHECK(empty.error() == ContentError::InvalidData);
    MemoryAsset asset;
    ItemBehaviorRegistry registry;
    RegisterBuiltinItemBehaviors(registry);
    const auto explicitRegistry = LoadContent({&source, 1}, registry, asset.Reader());
    REQUIRE(explicitRegistry.has_value());
    CHECK(asset.paths.size() == 1);
    const auto inlineText = Document("['<red>MM']");
    const ContentSource inlineSource{"inline.json5", View(inlineText)};
    CHECK(LoadContent({&inlineSource, 1}).has_value());
    CHECK(LoadContent({&inlineSource, 1}, asset.Reader()).has_value());
    CHECK(asset.paths.size() == 1);
    const auto registryWithoutReader = LoadContent({&source, 1}, registry);
    REQUIRE_FALSE(registryWithoutReader.has_value());
    CHECK(registryWithoutReader.error() == ContentError::InvalidData);
    const ContentSource behaviorSource{"behavior.json5", R"({format_version:1,definitions:[
        {type:'item',id:'sample',name:'Sample',mass_g:1,volume_ml:1,art:'/art/items/sample.txt',
         capabilities:[{type:'door_controller',id:'timer',delay_ticks:7}]}
    ]})"};
    const auto behavior = LoadContent({&behaviorSource, 1}, registry, asset.Reader());
    REQUIRE(behavior.has_value());
    CHECK(behavior->GetItem({1}).behaviors.size() == 1);
    CHECK(FindItemCapability(behavior->GetItem({1}), "timer") == ItemCapabilityId{1});
    ItemBehaviorRegistry noBehaviors;
    const auto unregistered = LoadContent({&behaviorSource, 1}, noBehaviors, asset.Reader());
    REQUIRE_FALSE(unregistered.has_value());
    CHECK(unregistered.error() == ContentError::InvalidData);
}

TEST_CASE("External item art validates reference types before reading assets", "[content][item-assets]") {
    for (const char* art : {"null", "1", "true", "{}", "{file:'/art/items/sample.txt'}", "[1]"}) {
        CAPTURE(art);
        MemoryAsset asset;
        const auto loaded = Load(View(Document(art)), asset.Reader());
        REQUIRE_FALSE(loaded.has_value());
        CHECK(loaded.error() == ContentError::InvalidData);
        CHECK(asset.paths.empty());
    }
}

TEST_CASE("Invalid external art paths are rejected before invoking the reader", "[content][item-assets]") {
    // These strings are JSON5 encodings, including escaped path controls.
    for (const char* path : {"", "/", "art/a.txt", "./art/a.txt", "../a.txt", "//art/a.txt",
        "/art//a.txt", "/art/./a.txt", "/art/../a.txt", "/art/a.txt/", "/art/a./file",
        "/art/a /file", "/art/a.txt.", "/art/a.txt ", "C:/art/a.txt", "/art/a:b.txt",
        "/art\\\\a.txt", "/art/a\\n.txt", "/art/a\\t.txt", "/art/a\\u0000.txt", "/art/a\\u007f.txt"}) {
        CAPTURE(path);
        String reference = "'";
        reference += path;
        reference += '\'';
        MemoryAsset asset;
        Diagnostics diagnostics;
        const auto loaded = Load(View(Document(View(reference))), asset.Reader(), &diagnostics);
        REQUIRE_FALSE(loaded.has_value());
        CHECK(loaded.error() == ContentError::InvalidData);
        CHECK(asset.paths.empty());
        REQUIRE_FALSE(diagnostics.Empty());
        REQUIRE(diagnostics.Frames().front().location.has_value());
        CHECK(diagnostics.Frames().front().location->file == "nested/items.json5");
        CHECK(diagnostics.Frames().front().location->path == "definitions[0].art");
    }
    CHECK(ValidContentAssetPath("/foo/art.txt"));
    CHECK(ValidContentAssetPath("/foo-bar/art_v2.txt"));
}

TEST_CASE("External art failures retain asset physical locations before item reference context", "[content][item-assets]") {
    MemoryAsset asset;
    asset.text = "@text-art 1\n@color ember #DC5353\n---\nvalid\nab<missing>";
    Diagnostics diagnostics;
    const auto loaded = Load(View(Document()), asset.Reader(), &diagnostics);
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error() == ContentError::InvalidData);
    REQUIRE(diagnostics.Frames().size() >= 2);
    const auto& first = diagnostics.Frames().front().location;
    REQUIRE(first.has_value());
    CHECK(first->file == "/art/items/sample.txt");
    REQUIRE(first->line.has_value());
    CHECK(*first->line == 5);
    REQUIRE(first->column.has_value());
    CHECK(*first->column == 3);
    bool foundReference = false;
    for (std::size_t i = 1; i < diagnostics.Frames().size(); ++i) {
        const auto& location = diagnostics.Frames()[i].location;
        if (location && location->file == "nested/items.json5" && location->path == "definitions[0].art") {
            foundReference = true;
        }
    }
    CHECK(foundReference);
    CHECK_FALSE(Load(View(Document()), asset.Reader()).has_value());
}

TEST_CASE("External asset callback and version errors preserve typed failure and publish no catalog", "[content][item-assets]") {
    MemoryAsset asset;
    asset.fail = true;
    Diagnostics diagnostics;
    const auto io = Load(View(Document()), asset.Reader(), &diagnostics);
    REQUIRE_FALSE(io.has_value());
    CHECK(io.error() == ContentError::Io);
    REQUIRE_FALSE(diagnostics.Empty());
    CHECK(diagnostics.Frames().front().message == "fixture asset unavailable");
    const auto silentIo = Load(View(Document()), asset.Reader());
    REQUIRE_FALSE(silentIo.has_value());
    CHECK(silentIo.error() == ContentError::Io);
    asset.fail = false;
    asset.text = "@text-art 2\n---\nM";
    const auto version = Load(View(Document()), asset.Reader());
    REQUIRE_FALSE(version.has_value());
    CHECK(version.error() == ContentError::UnsupportedVersion);
    // A preceding valid definition cannot turn a failed load into a partial result.
    const ContentSource sources[] = {
        {"valid.json5", "{format_version:1,definitions:[{type:'item',id:'valid',name:'Valid',mass_g:1,volume_ml:1}]}"},
        {"bad.json5", "{format_version:1,definitions:[{type:'item',id:'bad',name:'Bad',mass_g:1,volume_ml:1,art:'/art/items/sample.txt'}]}"}
    };
    const auto partial = LoadContent({sources, 2}, asset.Reader());
    REQUIRE_FALSE(partial.has_value());
    CHECK(partial.error() == ContentError::UnsupportedVersion);
    asset.text = Asset;
    const auto recovered = LoadContent({sources, 2}, asset.Reader());
    REQUIRE(recovered.has_value());
    CHECK(recovered->ItemCount() == 2);
}

TEST_CASE("External art fingerprint depends on resolved art rather than path palette names or line endings", "[content][item-assets][fingerprint]") {
    MemoryAsset asset;
    const auto baseline = Load(View(Document()), asset.Reader());
    REQUIRE(baseline.has_value());
    const auto fingerprint = network::ContentFingerprint(*baseline);
    asset.text += '\n';
    const auto terminated = Load(View(Document()), asset.Reader());
    REQUIRE(terminated.has_value());
    CHECK(terminated->GetItem({1}) == baseline->GetItem({1}));
    CHECK(network::ContentFingerprint(*terminated) == fingerprint);
    asset.text = "@text-art 1\r\n@color ember #DC5353\r\n---\r\n <ember>MM<reset>! ";
    const auto crlf = Load(View(Document()), asset.Reader());
    REQUIRE(crlf.has_value());
    CHECK(network::ContentFingerprint(*crlf) == fingerprint);
    asset.text = "@text-art 1\n@color renamed #dc5353\n---\n <renamed>MM<reset>! ";
    const auto renamed = Load(View(Document("'/other/renamed.txt'")), asset.Reader());
    REQUIRE(renamed.has_value());
    CHECK(network::ContentFingerprint(*renamed) == fingerprint);
    CHECK(renamed->GetItem({1}) == baseline->GetItem({1}));
    asset.text = "@text-art 1\n@color renamed #DC5354\n---\n <renamed>MM<reset>! ";
    const auto recolored = Load(View(Document()), asset.Reader());
    REQUIRE(recolored.has_value());
    CHECK(network::ContentFingerprint(*recolored) != fingerprint);
}

TEST_CASE("Content packs root item art at the manifest instead of nested definition files", "[content][item-assets][pack]") {
    AssetFiles files;
    files.Write("pack.json5", "{format_version:1,files:['definitions/nested/items.json5']}");
    files.Write("definitions/nested/items.json5", View(Document()));
    files.Write("art/items/sample.txt", Asset);
    files.Write("definitions/nested/art/items/sample.txt", "@text-art 1\n---\nwrong");
    const auto loaded = LoadContentPack(files.Path("pack.json5").c_str());
    REQUIRE(loaded.has_value());
    CHECK(loaded->GetItem({1}).art.lines[0][1].text == "MM");
    ItemBehaviorRegistry registry;
    RegisterBuiltinItemBehaviors(registry);
    const auto explicitRegistry = LoadContentPack(files.Path("pack.json5").c_str(), registry);
    REQUIRE(explicitRegistry.has_value());
    CHECK(explicitRegistry->GetItem({1}) == loaded->GetItem({1}));
    files.Write("definitions/nested/items.json5", View(Document("'/art/items/missing.txt'")));
    const auto missing = LoadContentPack(files.Path("pack.json5").c_str());
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error() == ContentError::Io);
}

TEST_CASE("Definition file asset loading requires an explicit data root", "[content][item-assets][files]") {
    AssetFiles files;
    files.Write("definitions/items.json5", View(Document()));
    files.Write("art/items/sample.txt", Asset);
    const auto path = files.Path("definitions/items.json5");
    const auto root = files.Root();
    const eastl::string_view paths[] = {View(path)};
    const auto loaded = LoadContentFiles({paths, 1}, View(root));
    REQUIRE(loaded.has_value());
    CHECK(loaded->GetItem({1}).art.lines[0][1].color == ArtColor::Rgb(220, 83, 83));
    ItemBehaviorRegistry registry;
    RegisterBuiltinItemBehaviors(registry);
    const auto explicitRegistry = LoadContentFiles({paths, 1}, registry, View(root));
    REQUIRE(explicitRegistry.has_value());
    CHECK(explicitRegistry->GetItem({1}) == loaded->GetItem({1}));
    const auto noRoot = LoadContentFiles({paths, 1});
    REQUIRE_FALSE(noRoot.has_value());
    CHECK(noRoot.error() == ContentError::InvalidData);
    const auto single = LoadContentFile(path.c_str());
    REQUIRE_FALSE(single.has_value());
    CHECK(single.error() == ContentError::InvalidData);
    const auto noRegistryRoot = LoadContentFiles({paths, 1}, registry);
    REQUIRE_FALSE(noRegistryRoot.has_value());
    CHECK(noRegistryRoot.error() == ContentError::InvalidData);
}

TEST_CASE("File asset reader bounds byte reads and requires a regular contained file", "[content][item-assets][files]") {
    AssetFiles files;
    const auto root = files.Root();
    files.Write("art/a.txt", Asset);
    const auto read = ReadContentAssetFile(View(root), "/art/a.txt");
    REQUIRE(read.has_value());
    CHECK(*read == Asset);
    const auto missing = ReadContentAssetFile(View(root), "/art/missing.txt");
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error() == ContentError::Io);
    const auto directory = ReadContentAssetFile(View(root), "/art");
    REQUIRE_FALSE(directory.has_value());
    CHECK(directory.error() == ContentError::Io);
    const auto invalid = ReadContentAssetFile(View(root), "/art/../a.txt");
    REQUIRE_FALSE(invalid.has_value());
    CHECK(invalid.error() == ContentError::InvalidData);
    String bytes(147456, 'x');
    files.Write("art/limit.txt", View(bytes));
    const auto maximum = ReadContentAssetFile(View(root), "/art/limit.txt");
    REQUIRE(maximum.has_value());
    CHECK(maximum->size() == 147456);
    bytes += 'x';
    files.Write("art/limit.txt", View(bytes));
    Diagnostics diagnostics;
    const auto oversized = ReadContentAssetFile(View(root), "/art/limit.txt", &diagnostics);
    REQUIRE_FALSE(oversized.has_value());
    CHECK(oversized.error() == ContentError::InvalidData);
    REQUIRE_FALSE(diagnostics.Empty());
}

TEST_CASE("File asset reader resolves symlinks before checking root containment", "[content][item-assets][files]") {
    AssetFiles files;
    // Sibling root names deliberately share a prefix: containment is by components.
    files.Write("data/art/inside.txt", Asset);
    files.Write("data-other/outside.txt", Asset);
    std::error_code error;
    fs::create_symlink(files.directory / "data-other/outside.txt", files.directory / "data/art/escape.txt", error);
    if (error) { SKIP("file symlinks unavailable on this host: " << error.message()); }
    const auto root = files.Path("data");
    const auto escaped = ReadContentAssetFile(View(root), "/art/escape.txt");
    REQUIRE_FALSE(escaped.has_value());
    CHECK(escaped.error() == ContentError::InvalidData);
    fs::create_symlink(files.directory / "data/art/inside.txt", files.directory / "data/art/alias.txt", error);
    REQUIRE_FALSE(error);
    const auto contained = ReadContentAssetFile(View(root), "/art/alias.txt");
    REQUIRE(contained.has_value());
    CHECK(*contained == Asset);
}

TEST_CASE("Malformed UTF-8 asset paths reject before filesystem access or callbacks", "[content][item-assets]") {
    for (const char* bytes : {"\x80", "\xc0\xaf", "\xc2", "\xe0\x80\xaf", "\xed\xa0\x80",
        "\xf0\x80\x80\xaf", "\xf4\x90\x80\x80", "\xf5\x80\x80\x80", "\xe2\x28\xa1"}) {
        String path = "/art/";
        path += bytes;
        CHECK_FALSE(ValidContentAssetPath(View(path)));
        const auto direct = ReadContentAssetFile(".", View(path));
        REQUIRE_FALSE(direct);
        CHECK(direct.error() == ContentError::InvalidData);
        String reference = "'" + path + "'";
        MemoryAsset asset;
        const auto loaded = Load(View(Document(View(reference))), asset.Reader());
        REQUIRE_FALSE(loaded);
        CHECK(loaded.error() == ContentError::InvalidData);
        CHECK(asset.paths.empty());
    }
    CHECK(ValidContentAssetPath("/art/\xc3\xa9/\xe4\xb8\xad/\xf0\x9f\x90\x88.txt"));
}

TEST_CASE("Invalid host roots return typed IO errors", "[content][item-assets][files]") {
    const char embeddedNull[] = {'a', '\0', 'b'};
    for (const auto root : {eastl::string_view{}, eastl::string_view(embeddedNull, sizeof(embeddedNull))}) {
        const auto loaded = ReadContentAssetFile(root, "/art/a.txt");
        REQUIRE_FALSE(loaded);
        CHECK(loaded.error() == ContentError::Io);
    }
}

#ifdef _WIN32
TEST_CASE("Malformed native Windows root encoding returns IO without throwing", "[content][item-assets][files]") {
    char invalid = '\0';
    if (GetACP() == CP_UTF8) { invalid = static_cast<char>(0xff); }
    else {
        for (unsigned byte = 0x80; byte <= 0xff; ++byte) {
            if (IsDBCSLeadByteEx(CP_ACP, static_cast<BYTE>(byte))) {
                invalid = static_cast<char>(byte);
                break;
            }
        }
    }
    if (!invalid) { SKIP("active single-byte code page has no malformed byte sequence"); }
    const auto loaded = ReadContentAssetFile({&invalid, 1}, "/art/a.txt");
    REQUIRE_FALSE(loaded);
    CHECK(loaded.error() == ContentError::Io);
}

TEST_CASE("Windows pack roots preserve native encoding while art references use UTF-8", "[content][item-assets][pack]") {
    AssetFiles files;
    std::wstring hostRoot;
    std::string nativeRoot;
    for (const wchar_t* name : {L"\u00e9", L"\u0416", L"\u4e2d"}) {
        const auto candidate = files.directory / name;
        const auto& wide = candidate.native();
        BOOL substituted = FALSE;
        const bool utf8 = GetACP() == CP_UTF8;
        const DWORD flags = utf8 ? 0 : WC_NO_BEST_FIT_CHARS;
        BOOL* usedDefault = utf8 ? nullptr : &substituted;
        const int size = static_cast<int>(wide.size());
        const int length = WideCharToMultiByte(CP_ACP, flags, wide.data(), size, nullptr, 0, nullptr, usedDefault);
        if (!length || substituted) { continue; }
        std::string bytes(static_cast<std::size_t>(length), '\0');
        if (WideCharToMultiByte(CP_ACP, flags, wide.data(), size, bytes.data(), length, nullptr, usedDefault) != length
            || substituted) { continue; }
        hostRoot = wide;
        nativeRoot = bytes;
        break;
    }
    if (hostRoot.empty()) { SKIP("active code page cannot represent a non-ASCII fixture directory"); }
    const fs::path root(hostRoot);
    std::error_code error;
    fs::create_directories(root / L"art", error);
    REQUIRE_FALSE(error);
    const auto write = [](const fs::path& path, eastl::string_view bytes) {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        REQUIRE(file.is_open());
        file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        file.close();
        REQUIRE_FALSE(file.fail());
    };
    write(root / L"pack.json5", "{format_version:1,files:['items.json5']}");
    write(root / L"items.json5", View(Document("'/art/\xe4\xb8\xad.txt'")));
    write(root / L"art" / L"\u4e2d.txt", Asset);
    const auto loaded = LoadContentPack((nativeRoot + "/pack.json5").c_str());
    REQUIRE(loaded);
    CHECK(loaded->GetItem({1}).art.lines[0][1].text == "MM");
    const auto direct = ReadContentAssetFile(View(nativeRoot), "/art/\xe4\xb8\xad.txt");
    REQUIRE(direct);
    CHECK(*direct == Asset);
}
#endif

}
