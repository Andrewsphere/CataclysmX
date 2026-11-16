#include "content/content.h"
#include "items/item_registry.h"

#include <EASTL/utility.h>
#include <yyjson.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace cataclysmx {
namespace {
struct File { std::FILE* value = nullptr; ~File() { if (value) { std::fclose(value); } } };
struct Document { yyjson_doc* value = nullptr; ~Document() { yyjson_doc_free(value); } };
void* Allocate(void*, std::size_t size) { void* p = std::malloc(size); if (!p) { std::abort(); } return p; }
void* Reallocate(void*, void* p, std::size_t, std::size_t size) { void* q = std::realloc(p, size); if (!q && size) { std::abort(); } return q; }
void Free(void*, void* p) { std::free(p); }

ContentResult<ContentCatalog> Fail(ContentError error, Diagnostics* diagnostics, const char* file,
                                   const String& path, const char* message) {
    if (diagnostics) {
        DiagnosticFrame frame;
        frame.message = message;
        frame.location = DiagnosticLocation{String(file ? file : ""), path, {}, {}};
        diagnostics->Add(eastl::move(frame));
    }
    return eastl::unexpected(error);
}

bool Normalize(eastl::string_view input, String& output) {
    if (input.empty() || input[0] == '/' || input[0] == '\\' || std::memchr(input.data(), 0, input.size())) { return false; }
    // Reject drive-relative paths, alternate streams, and either platform's root syntax.
    if (std::memchr(input.data(), ':', input.size())) { return false; }
    std::size_t start = 0;
    while (start < input.size()) {
        std::size_t end = start;
        while (end < input.size() && input[end] != '/' && input[end] != '\\') { ++end; }
        const eastl::string_view part(input.data() + start, end - start);
        if (part == "..") { return false; }
        if (!part.empty() && part != ".") {
            if (!output.empty()) { output += '/'; }
            output.append(part.data(), part.size());
        }
        start = end + 1;
    }
    return !output.empty();
}
}

ContentResult<ContentCatalog> LoadContentPack(const char* path, Diagnostics* diagnostics) {
    ItemBehaviorRegistry registry;
    RegisterBuiltinItemBehaviors(registry);
    return LoadContentPack(path, registry, diagnostics);
}

ContentResult<ContentCatalog> LoadContentPack(const char* path, const ItemBehaviorRegistry& registry,
                                               Diagnostics* diagnostics) {
    if (!path || !*path) { return Fail(ContentError::Io, diagnostics, path, {}, "invalid manifest path"); }
    File file;
#ifdef _MSC_VER
    fopen_s(&file.value, path, "rb");
#else
    file.value = std::fopen(path, "rb");
#endif
    if (!file.value) { return Fail(ContentError::Io, diagnostics, path, {}, "could not open manifest"); }
    String text;
    char buffer[8192];
    for (;;) {
        const auto count = std::fread(buffer, 1, sizeof(buffer), file.value);
        text.append(buffer, count);
        if (std::ferror(file.value) || (!count && !std::feof(file.value))) {
            return Fail(ContentError::Io, diagnostics, path, {}, "could not read manifest");
        }
        if (std::feof(file.value)) { break; }
    }
    const int closeResult = std::fclose(file.value);
    file.value = nullptr;
    if (closeResult) { return Fail(ContentError::Io, diagnostics, path, {}, "could not close manifest"); }
    yyjson_alc allocator{Allocate, Reallocate, Free, nullptr};
    yyjson_read_err error{};
    Document doc{yyjson_read_opts(text.data(), text.size(), YYJSON_READ_JSON5, &allocator, &error)};
    if (!doc.value) { return Fail(ContentError::InvalidData, diagnostics, path, {}, error.msg ? error.msg : "invalid JSON5"); }
    auto* root = yyjson_doc_get_root(doc.value);
    if (!yyjson_is_obj(root)) { return Fail(ContentError::InvalidData, diagnostics, path, {}, "expected an object"); }
    bool hasVersion = false, hasFiles = false;
    std::size_t i, count;
    yyjson_val *key, *value;
    yyjson_obj_foreach(root, i, count, key, value) {
        const eastl::string_view name(yyjson_get_str(key), yyjson_get_len(key));
        bool* seen = name == "format_version" ? &hasVersion : name == "files" ? &hasFiles : nullptr;
        String field(name.data(), name.size());
        if (!seen) { return Fail(ContentError::InvalidData, diagnostics, path, field, "unknown field"); }
        if (*seen) { return Fail(ContentError::InvalidData, diagnostics, path, field, "duplicate field"); }
        *seen = true;
    }
    auto* version = yyjson_obj_get(root, "format_version");
    if (!yyjson_is_uint(version)) { return Fail(ContentError::InvalidData, diagnostics, path, "format_version", "expected an integer format version"); }
    if (yyjson_get_uint(version) != 1) { return Fail(ContentError::UnsupportedVersion, diagnostics, path, "format_version", "unsupported manifest format version"); }
    auto* files = yyjson_obj_get(root, "files");
    if (!yyjson_is_arr(files) || !yyjson_arr_size(files)) { return Fail(ContentError::InvalidData, diagnostics, path, "files", "expected a nonempty array"); }
    String directory(path);
    const auto slash = directory.find_last_of("/\\");
    if (slash == String::npos) { directory.clear(); } else { directory.resize(slash + 1); }
    Vector<String> normalized, paths;
    std::size_t index, size;
    yyjson_val* entry;
    yyjson_arr_foreach(files, index, size, entry) {
        char location[64];
        std::snprintf(location, sizeof(location), "files[%zu]", index);
        String item;
        if (!yyjson_is_str(entry) || !Normalize({yyjson_get_str(entry), yyjson_get_len(entry)}, item)) {
            return Fail(ContentError::InvalidData, diagnostics, path, location, "invalid relative content path");
        }
        for (const auto& previous : normalized) {
            if (previous == item) { return Fail(ContentError::InvalidData, diagnostics, path, location, "duplicate content path"); }
        }
        paths.push_back(directory + item);
        normalized.push_back(eastl::move(item));
    }
    Vector<eastl::string_view> views;
    views.reserve(paths.size());
    for (const auto& item : paths) { views.push_back({item.data(), item.size()}); }
    const eastl::string_view dataRoot = directory.empty() ? eastl::string_view(".")
        : eastl::string_view(directory.data(), directory.size());
    return LoadContentFiles({views.data(), views.size()}, registry, dataRoot, diagnostics);
}
}
