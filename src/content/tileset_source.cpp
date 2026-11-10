#include "content/tileset_source.h"

#include <EASTL/sort.h>
#include <yyjson.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

namespace cataclysmx {
namespace {
namespace fs = std::filesystem;
constexpr std::uint32_t missing = UINT32_MAX;

struct Document {
    yyjson_doc* value = nullptr;
    ~Document() { yyjson_doc_free(value); }
};
void* Allocate(void*, size_t size) { auto* p = std::malloc(size); if (!p) { std::abort(); } return p; }
void* Reallocate(void*, void* p, size_t, size_t size) {
    auto* result = std::realloc(p, size); if (!result && size) { std::abort(); } return result;
}
void Free(void*, void* p) { std::free(p); }
bool Fail(Diagnostics* diagnostics, const String& file, const char* message) {
    if (diagnostics) {
        DiagnosticFrame frame;
        frame.message = message;
        frame.location = DiagnosticLocation{file, {}, {}, {}};
        diagnostics->Add(eastl::move(frame));
    }
    return false;
}
eastl::string_view View(yyjson_val* v) { return {yyjson_get_str(v), yyjson_get_len(v)}; }
bool Text(yyjson_val* v, String& result) {
    if (!yyjson_is_str(v) || !yyjson_get_len(v) || yyjson_get_len(v) > 1024
        || std::memchr(yyjson_get_str(v), 0, yyjson_get_len(v))) { return false; }
    result.assign(yyjson_get_str(v), yyjson_get_len(v)); return true;
}
bool Object(yyjson_val* v, std::initializer_list<eastl::string_view> fields, bool arbitrary = false) {
    if (!yyjson_is_obj(v) || yyjson_obj_size(v) > MaxTilesetEntries) { return false; }
    size_t i, n; yyjson_val *key, *value;
    yyjson_obj_foreach(v, i, n, key, value) {
        (void)value;
        bool known = arbitrary;
        for (auto field : fields) { known = known || field == View(key); }
        if (!known) { return false; }
        size_t j, m; yyjson_val *previous, *unused;
        yyjson_obj_foreach(v, j, m, previous, unused) {
            (void)unused;
            if (j == i) { break; }
            if (View(previous) == View(key)) { return false; }
        }
    }
    return true;
}
bool UInt(yyjson_val* v, std::uint32_t& out, bool positive = false) {
    if (!yyjson_is_uint(v) || yyjson_get_uint(v) > UINT32_MAX || (positive && !yyjson_get_uint(v))) { return false; }
    out = static_cast<std::uint32_t>(yyjson_get_uint(v)); return true;
}
bool ReadJson(eastl::string_view text, Document& document, Diagnostics* diagnostics, const String& file) {
    if (text.size() > MaxTilesetMetadataBytes) { return Fail(diagnostics, file, "metadata exceeds 4 MiB limit"); }
    yyjson_alc allocator{Allocate, Reallocate, Free, nullptr};
    yyjson_read_err error{};
    document.value = yyjson_read_opts(const_cast<char*>(text.data()), text.size(), YYJSON_READ_JSON5, &allocator, &error);
    return document.value || Fail(diagnostics, file, error.msg ? error.msg : "invalid JSON5");
}
ContentResult<bool> ValidateVersion(yyjson_val* root, Diagnostics* diagnostics, const String& file) {
    auto* version = yyjson_obj_get(root, "format_version");
    if (!yyjson_is_int(version)) {
        Fail(diagnostics, file, "tileset format_version must be an integer");
        return eastl::unexpected(ContentError::InvalidData);
    }
    if (!yyjson_is_uint(version) || yyjson_get_uint(version) != 2) {
        Fail(diagnostics, file, "unsupported tileset format version; only version 2 source packs are supported");
        return eastl::unexpected(ContentError::UnsupportedVersion);
    }
    return true;
}
bool Segment(eastl::string_view text) {
    if (text.empty()) { return false; }
    for (char c : text) {
        if (!(c >= 'a' && c <= 'z') && !(c >= '0' && c <= '9') && c != '_' && c != '-') { return false; }
    }
    return true;
}
bool PackagePath(eastl::string_view path, String& id) {
    if (path.empty() || path.size() > 512) { return false; }
    std::size_t start = 0;
    for (std::size_t i = 0; i <= path.size(); ++i) {
        if (i != path.size() && path[i] != '/') { continue; }
        if (!Segment(path.substr(start, i - start))) { return false; }
        if (!id.empty()) { id += '.'; }
        id.append(path.data() + start, i - start); start = i + 1;
    }
    return true;
}
bool Ends(eastl::string_view path, eastl::string_view suffix) {
    return path.size() >= suffix.size() && path.substr(path.size() - suffix.size()) == suffix;
}
template<class T> std::uint32_t Find(const Vector<T>& entries, eastl::string_view id) {
    for (std::uint32_t i = 0; i < entries.size(); ++i) { if (entries[i].id == id) { return i; } }
    return missing;
}
bool Reference(yyjson_val* v, const TilesetDefinition& d, TilesetMapping& mapping, bool groups) {
    if (!yyjson_is_str(v)) { return false; }
    const auto id = View(v);
    std::size_t start = 0;
    for (std::size_t i = 0; i <= id.size(); ++i) {
        if (i != id.size() && id[i] != '.') { continue; }
        if (!Segment(id.substr(start, i - start))) { return false; }
        start = i + 1;
    }
    if (const auto i = Find(d.sprites, id); i != missing) { mapping.kind = TilesetMappingKind::Sprite; mapping.index = i; return true; }
    if (const auto i = Find(d.animations, id); i != missing) { mapping.kind = TilesetMappingKind::Animation; mapping.index = i; return true; }
    if (groups) {
        if (const auto i = Find(d.visuals, id); i != missing && d.visuals[i].kind == VisualGroupKind::Variants) {
            mapping.kind = TilesetMappingKind::Visual; mapping.index = i; return true;
        }
    }
    return false;
}
bool Transform(yyjson_val* object, SpriteTransform& t) {
    if (auto* v = yyjson_obj_get(object, "rotation")) {
        std::uint32_t degrees = 0;
        if (!UInt(v, degrees) || degrees > 270 || degrees % 90) { return false; }
        t.quarterTurns = static_cast<std::uint8_t>(degrees / 90);
    }
    if (auto* v = yyjson_obj_get(object, "flip_x")) { if (!yyjson_is_bool(v)) { return false; } t.flipX = yyjson_get_bool(v); }
    if (auto* v = yyjson_obj_get(object, "flip_y")) { if (!yyjson_is_bool(v)) { return false; } t.flipY = yyjson_get_bool(v); }
    return true;
}
bool Alternative(yyjson_val* v, const TilesetDefinition& d, VisualAlternative& out, bool groups, bool weighted) {
    if (!Object(v, weighted ? std::initializer_list<eastl::string_view>{"visual", "weight", "rotation", "flip_x", "flip_y"}
        : std::initializer_list<eastl::string_view>{"visual", "rotation", "flip_x", "flip_y"})
        || !Reference(yyjson_obj_get(v, "visual"), d, out.mapping, groups) || !Transform(v, out.mapping.transform)) { return false; }
    if (auto* w = yyjson_obj_get(v, "weight")) { if (!UInt(w, out.weight, true)) { return false; } }
    return true;
}
bool Roots(yyjson_val* root, Vector<String>& roots) {
    auto* value = yyjson_obj_get(root, "source_directories");
    if (!value) { roots = {"tile", "sprite"}; return true; }
    if (!yyjson_is_arr(value) || yyjson_arr_size(value) > 64 || !yyjson_arr_size(value)) { return false; }
    size_t i, n; yyjson_val* entry;
    yyjson_arr_foreach(value, i, n, entry) {
        String path, id;
        if (!Text(entry, path) || !PackagePath(path, id)) { return false; }
        for (const auto& previous : roots) {
            if (path == previous || path.find((previous + '/').c_str()) == 0 || previous.find((path + '/').c_str()) == 0) { return false; }
        }
        roots.push_back(eastl::move(path));
    }
    return true;
}
bool Pack(TilesetDefinition& d) {
    std::uint32_t current = missing, x = 0, y = 0, rowHeight = 0;
    for (auto& sprite : d.sprites) {
        const auto w = sprite.width + 2, h = sprite.height + 2;
        if (w > 8192 || h > 8192) { return false; }
        if (w > 2048 || h > 2048) {
            sprite.atlasIndex = static_cast<std::uint32_t>(d.atlases.size());
            d.atlases.push_back({{}, w, h}); sprite.x = sprite.y = 1;
        } else {
            if (current != missing && x + w > 2048) { y += rowHeight; x = 0; rowHeight = 0; }
            if (current == missing || y + h > 2048) {
                current = static_cast<std::uint32_t>(d.atlases.size()); d.atlases.push_back({});
                x = y = rowHeight = 0;
            }
            auto& page = d.atlases[current];
            sprite.atlasIndex = current; sprite.x = x + 1; sprite.y = y + 1;
            x += w; rowHeight = eastl::max(rowHeight, h);
            page.width = eastl::max(page.width, x); page.height = eastl::max(page.height, y + h);
        }
        if (d.atlases.size() > MaxTilesetAtlases) { return false; }
    }
    std::uint64_t bytes = 0;
    for (std::size_t i = 0; i < d.atlases.size(); ++i) {
        char id[32]; std::snprintf(id, sizeof(id), "page_%zu", i); d.atlases[i].id = id;
        bytes += std::uint64_t(d.atlases[i].width) * d.atlases[i].height * 4;
    }
    return bytes <= 64 * 1024 * 1024;
}
String Utf8(const fs::path& path) { auto s = path.generic_u8string(); return String(reinterpret_cast<const char*>(s.data()), s.size()); }
bool Contained(const fs::path& root, const fs::path& path) {
    auto p = path.begin();
    for (auto r = root.begin(); r != root.end(); ++r, ++p) { if (p == path.end() || *p != *r) { return false; } }
    return true;
}
ContentResult<bool> ReadText(const fs::path& path, String& text, Diagnostics* diagnostics) {
    const String file = Utf8(path);
    FILE* f = std::fopen(file.c_str(), "rb");
    if (!f) {
        Fail(diagnostics, file, "cannot open metadata"); return eastl::unexpected(ContentError::Io);
    }
    char buffer[4096]; std::size_t n = 0;
    while ((n = std::fread(buffer, 1, sizeof(buffer), f)) != 0) {
        if (text.size() + n > MaxTilesetMetadataBytes) {
            std::fclose(f); Fail(diagnostics, file, "metadata exceeds 4 MiB limit");
            return eastl::unexpected(ContentError::InvalidData);
        }
        text.append(buffer, n);
    }
    const bool ok = !std::ferror(f); std::fclose(f);
    if (!ok) {
        Fail(diagnostics, file, "cannot read metadata"); return eastl::unexpected(ContentError::Io);
    }
    return true;
}
}

ContentResult<TilesetDefinition> ParseSourceTileset(eastl::string_view manifest,
    eastl::span<const SpriteSourceFile> input, const ContentCatalog& content, Diagnostics* diagnostics) {
    String context = "tileset.json5";
    auto invalid = [&]() -> ContentResult<TilesetDefinition> {
        Fail(diagnostics, context, "invalid sprite-pack metadata, dimensions, or reference");
        return eastl::unexpected(ContentError::InvalidData);
    };
    Document document;
    if (!ReadJson(manifest, document, diagnostics, context)) { return invalid(); }
    auto* root = yyjson_doc_get_root(document.value);
    auto version = ValidateVersion(root, diagnostics, context);
    if (!version) { return eastl::unexpected(version.error()); }
    if (!Object(root, {"format_version", "id", "name", "cell_width", "cell_height", "source_directories", "terrain", "items", "generic"})) { return invalid(); }
    TilesetDefinition d;
    if (!Text(yyjson_obj_get(root, "id"), d.id) || !Segment(d.id) || !Text(yyjson_obj_get(root, "name"), d.name)
        || !UInt(yyjson_obj_get(root, "cell_width"), d.cellWidth, true) || !UInt(yyjson_obj_get(root, "cell_height"), d.cellHeight, true)
        || d.cellWidth > 16384 || d.cellHeight > 16384 || input.size() > MaxTilesetEntries) { return invalid(); }
    Vector<String> roots;
    if (!Roots(root, roots)) { return invalid(); }
    Vector<String> ids;
    Vector<std::uint32_t> order;
    std::size_t metadataBytes = manifest.size();
    for (const auto& f : input) {
        context = f.path;
        metadataBytes += f.text.size();
        const eastl::string_view path(f.path.data(), f.path.size());
        const bool png = Ends(path, ".png");
        String id;
        bool inRoot = false;
        for (const auto& directory : roots) { inRoot = inRoot || f.path.find((directory + '/').c_str()) == 0; }
        if (!inRoot || metadataBytes > MaxTilesetMetadataBytes || (!png && !Ends(path, ".meta"))
            || !PackagePath(path.substr(0, path.size() - (png ? 4 : 5)), id)) { return invalid(); }
        for (const auto& previous : ids) { if (previous == id) { return invalid(); } }
        order.push_back(static_cast<std::uint32_t>(ids.size())); ids.push_back(eastl::move(id));
    }
    eastl::sort(order.begin(), order.end(), [&](auto a, auto b) { return ids[a] < ids[b]; });
    for (auto i : order) {
        const auto& f = input[i]; const auto& id = ids[i];
        context = f.path;
        if (!Ends(f.path, ".png")) { continue; }
        if (!f.width || !f.height || f.width > 8192 || f.height > 8192) { return invalid(); }
        TilesetSourceImage source{f.path, f.width, f.height, f.width, f.height, static_cast<std::uint32_t>(d.sprites.size()), 0};
        float anchorX = 0, anchorY = 0;
        float cellAnchorX = -1, cellAnchorY = -1;
        bool explicitAnchor = false;
        SpriteLayer layer = f.path.find("sprite/") == 0 ? SpriteLayer::Upright : SpriteLayer::Ground;
        if (!f.text.empty()) {
            Document meta;
            if (!ReadJson(f.text, meta, diagnostics, context)) { return invalid(); }
            auto* value = yyjson_doc_get_root(meta.value);
            if (!Object(value, {"description", "anchor", "cell_anchor", "layer", "frame_width", "frame_height"})) { return invalid(); }
            if (auto* description = yyjson_obj_get(value, "description")) { String text; if (!Text(description, text)) { return invalid(); } }
            auto* fw = yyjson_obj_get(value, "frame_width"); auto* fh = yyjson_obj_get(value, "frame_height");
            if ((fw != nullptr) != (fh != nullptr) || (fw && (!UInt(fw, source.frameWidth, true) || !UInt(fh, source.frameHeight, true)))) { return invalid(); }
            if (auto* anchor = yyjson_obj_get(value, "anchor")) {
                if (!yyjson_is_arr(anchor) || yyjson_arr_size(anchor) != 2) { return invalid(); }
                auto* ax = yyjson_arr_get(anchor, 0); auto* ay = yyjson_arr_get(anchor, 1);
                if (!yyjson_is_int(ax) || !yyjson_is_int(ay) || yyjson_get_sint(ax) < -8192 || yyjson_get_sint(ax) > 8192
                    || yyjson_get_sint(ay) < -8192 || yyjson_get_sint(ay) > 8192) { return invalid(); }
                anchorX = static_cast<float>(yyjson_get_sint(ax)); anchorY = static_cast<float>(yyjson_get_sint(ay)); explicitAnchor = true;
            }
            if (auto* v = yyjson_obj_get(value, "layer")) {
                if (!yyjson_is_str(v) || (View(v) != "ground" && View(v) != "upright")) { return invalid(); }
                layer = View(v) == "ground" ? SpriteLayer::Ground : SpriteLayer::Upright;
            }
            if (auto* anchor = yyjson_obj_get(value, "cell_anchor")) {
                std::uint32_t x = 0, y = 0;
                if (!yyjson_is_arr(anchor) || yyjson_arr_size(anchor) != 2
                    || !UInt(yyjson_arr_get(anchor, 0), x) || !UInt(yyjson_arr_get(anchor, 1), y)
                    || x > d.cellWidth || y > d.cellHeight) { return invalid(); }
                cellAnchorX = static_cast<float>(x); cellAnchorY = static_cast<float>(y);
            }
        }
        if (source.frameWidth > f.width || source.frameHeight > f.height || f.width % source.frameWidth || f.height % source.frameHeight) { return invalid(); }
        source.frameCount = (f.width / source.frameWidth) * (f.height / source.frameHeight);
        if (source.frameCount > MaxTilesetEntries - d.sprites.size()) { return invalid(); }
        for (std::uint32_t frame = 0; frame < source.frameCount; ++frame) {
            String frameId = id;
            if (frame) { char suffix[32]; std::snprintf(suffix, sizeof(suffix), "@%u", frame); frameId += suffix; }
            d.sprites.push_back({eastl::move(frameId), 0, 0, 0, source.frameWidth, source.frameHeight,
                explicitAnchor ? anchorX : source.frameWidth / 2.0f, explicitAnchor ? anchorY : float(source.frameHeight), layer, cellAnchorX, cellAnchorY});
        }
        d.sourceImages.push_back(eastl::move(source));
    }
    // Compile in dependency order; the allowed reference shapes prohibit cycles.
    const char* kinds[] = {"animation", "variants", "connections"};
    for (unsigned phase = 0; phase < 3; ++phase) {
        for (auto i : order) {
            const auto& f = input[i]; if (Ends(f.path, ".png")) { continue; }
            context = f.path;
            Document meta;
            if (!ReadJson(f.text, meta, diagnostics, context)) { return invalid(); }
            auto* v = yyjson_doc_get_root(meta.value); auto* kind = yyjson_obj_get(v, "kind");
            if (!yyjson_is_str(kind) || (View(kind) != "animation" && View(kind) != "variants" && View(kind) != "connections")) { return invalid(); }
            if (View(kind) != kinds[phase]) { continue; }
            if (auto* description = yyjson_obj_get(v, "description")) { String text; if (!Text(description, text)) { return invalid(); } }
            if (phase == 0) {
                if (!Object(v, {"kind", "description", "frames"})) { return invalid(); }
                auto* frames = yyjson_obj_get(v, "frames");
                if (!yyjson_is_arr(frames) || !yyjson_arr_size(frames) || yyjson_arr_size(frames) > MaxTilesetEntries) { return invalid(); }
                AnimationClip clip; clip.id = ids[i];
                size_t j, n; yyjson_val* frame;
                yyjson_arr_foreach(frames, j, n, frame) {
                    if (!Object(frame, {"sprite", "frame", "duration_ms"})) { return invalid(); }
                    auto* sprite = yyjson_obj_get(frame, "sprite");
                    if (!yyjson_is_str(sprite)) { return invalid(); }
                    const auto first = Find(d.sprites, View(sprite));
                    std::uint32_t number = 0;
                    if (auto* value = yyjson_obj_get(frame, "frame")) { if (!UInt(value, number)) { return invalid(); } }
                    const TilesetSourceImage* image = nullptr;
                    for (const auto& candidate : d.sourceImages) { if (candidate.firstSprite == first) { image = &candidate; break; } }
                    auto* duration = yyjson_obj_get(frame, "duration_ms");
                    if (!image || number >= image->frameCount || !yyjson_is_uint(duration) || !yyjson_get_uint(duration)) { return invalid(); }
                    const auto ms = yyjson_get_uint(duration);
                    if (ms > UINT64_MAX / 1000000 || clip.totalMilliseconds > UINT64_MAX / 1000000 - ms) { return invalid(); }
                    clip.totalMilliseconds += ms; clip.frames.push_back({first + number, clip.totalMilliseconds});
                }
                // A clip must keep its drawing layer throughout playback.
                const auto layer = d.sprites[clip.frames.front().spriteIndex].layer;
                for (const auto& clipFrame : clip.frames) { if (d.sprites[clipFrame.spriteIndex].layer != layer) { return invalid(); } }
                d.animations.push_back(eastl::move(clip));
            } else {
                VisualGroup group; group.id = ids[i];
                group.kind = phase == 1 ? VisualGroupKind::Variants : VisualGroupKind::Connections;
                if (phase == 1) {
                    if (!Object(v, {"kind", "description", "alternatives"})) { return invalid(); }
                    auto* alternatives = yyjson_obj_get(v, "alternatives");
                    if (!yyjson_is_arr(alternatives) || !yyjson_arr_size(alternatives) || yyjson_arr_size(alternatives) > MaxTilesetEntries) { return invalid(); }
                    size_t j, n; yyjson_val* a;
                    yyjson_arr_foreach(alternatives, j, n, a) {
                        VisualAlternative alternative;
                        if (!Alternative(a, d, alternative, false, true)) { return invalid(); }
                        group.totalWeight += alternative.weight; group.alternatives.push_back(alternative);
                    }
                } else {
                    if (!Object(v, {"kind", "description", "connects_to", "shapes"})) { return invalid(); }
                    auto* connects = yyjson_obj_get(v, "connects_to"); auto* shapes = yyjson_obj_get(v, "shapes");
                    if (!yyjson_is_arr(connects) || !yyjson_arr_size(connects) || yyjson_arr_size(connects) > MaxTilesetEntries
                        || !Object(shapes, {"isolated", "end", "straight", "corner", "tee", "cross"})) { return invalid(); }
                    size_t j, n; yyjson_val* terrain;
                    yyjson_arr_foreach(connects, j, n, terrain) {
                        if (!yyjson_is_str(terrain)) { return invalid(); }
                        const auto id = content.FindTerrain(View(terrain)); if (!id.value) { return invalid(); }
                        group.connectsTo.push_back(id);
                    }
                    const char* names[] = {"isolated", "end", "straight", "corner", "tee", "cross"};
                    const std::uint8_t canonical[] = {0, 1, 5, 3, 11, 15};
                    group.alternatives.resize(16);
                    for (unsigned shape = 0; shape < 6; ++shape) {
                        VisualAlternative a;
                        if (!Alternative(yyjson_obj_get(shapes, names[shape]), d, a, true, false)) { return invalid(); }
                        std::uint8_t mask = canonical[shape];
                        for (unsigned turn = 0; turn < 4; ++turn) {
                            if (group.alternatives[mask].mapping.kind == TilesetMappingKind::Missing) {
                                auto rotated = a; rotated.mapping.transform = ComposeSpriteTransforms(a.mapping.transform, {static_cast<std::uint8_t>(turn)});
                                group.alternatives[mask] = rotated;
                            }
                            mask = static_cast<std::uint8_t>(((mask << 1) | (mask >> 3)) & 15);
                        }
                    }
                }
                d.visuals.push_back(eastl::move(group));
            }
        }
    }
    context = "tileset.json5";
    d.terrain.resize(content.TerrainCount() + 1); d.items.resize(content.ItemCount() + 1);
    d.terrainGround.resize(content.TerrainCount() + 1);
    const char* categories[] = {"terrain", "items", "generic"};
    const char* generic[] = {"player", "other_player", "npc", "ground_items", "object_enabled", "object_disabled"};
    for (unsigned category = 0; category < 3; ++category) {
        auto* mappings = yyjson_obj_get(root, categories[category]); if (!mappings) { continue; }
        if (!Object(mappings, {}, true)) { return invalid(); }
        size_t i, n; yyjson_val *key, *value;
        yyjson_obj_foreach(mappings, i, n, key, value) {
            TilesetMapping* mapping = nullptr;
            if (category == 0) { const auto id = content.FindTerrain(View(key)); if (id.value) { mapping = &d.terrain[id.value]; } }
            else if (category == 1) { const auto id = content.FindItem(View(key)); if (id.value) { mapping = &d.items[id.value]; } }
            else { for (unsigned j = 0; j < 6; ++j) { if (View(key) == generic[j]) { mapping = &d.generic[j]; } } }
            if (!mapping || !Object(value, category == 0 ? std::initializer_list<eastl::string_view>{"visual", "activation", "ground"}
                : std::initializer_list<eastl::string_view>{"visual", "activation"})) { return invalid(); }
            auto* visual = yyjson_obj_get(value, "visual");
            if (!Reference(visual, d, *mapping, true)) {
                if (!yyjson_is_str(visual)) { return invalid(); }
                const auto group = Find(d.visuals, View(visual)); if (group == missing || category != 0) { return invalid(); }
                mapping->kind = TilesetMappingKind::Visual; mapping->index = group;
            }
            if (category != 0 && mapping->kind == TilesetMappingKind::Visual && d.visuals[mapping->index].kind == VisualGroupKind::Connections) { return invalid(); }
            if (auto* ground = yyjson_obj_get(value, "ground")) {
                auto& underlay = d.terrainGround[content.FindTerrain(View(key)).value];
                if (!Reference(ground, d, underlay, false) || underlay.kind != TilesetMappingKind::Sprite
                    || d.sprites[underlay.index].layer != SpriteLayer::Ground) { return invalid(); }
            }
            if (auto* activation = yyjson_obj_get(value, "activation")) {
                if (!Object(activation, {"animation", "mode"})) { return invalid(); }
                auto* animation = yyjson_obj_get(activation, "animation"); auto* mode = yyjson_obj_get(activation, "mode");
                if (!yyjson_is_str(animation) || !yyjson_is_str(mode)) { return invalid(); }
                const auto clip = Find(d.animations, View(animation));
                if (clip == missing || (View(mode) != "toggle_loop" && View(mode) != "once")) { return invalid(); }
                mapping->activation = SpriteActivationBinding{clip, View(mode) == "once" ? SpriteActivation::PlayOnce : SpriteActivation::ToggleLoop};
            }
        }
    }
    if (!Pack(d)) { return invalid(); }
    return d;
}

ContentResult<TilesetDefinition> LoadTilesetFile(const char* path, const ContentCatalog& content, Diagnostics* diagnostics) {
    auto failure = []() -> ContentResult<TilesetDefinition> { return eastl::unexpected(ContentError::InvalidData); };
    std::error_code error;
    const auto supplied = fs::absolute(fs::u8path(path), error);
    if (error) { Fail(diagnostics, String(path), "cannot resolve pack filename"); return eastl::unexpected(ContentError::Io); }
    const auto root = fs::canonical(supplied.parent_path(), error);
    if (error) { Fail(diagnostics, String(path), "cannot resolve pack directory"); return eastl::unexpected(ContentError::Io); }
    const auto resolvedManifest = fs::canonical(supplied, error);
    if (error) { Fail(diagnostics, String(path), "cannot resolve tileset metadata"); return eastl::unexpected(ContentError::Io); }
    if (!Contained(root, resolvedManifest)) { Fail(diagnostics, String(path), "manifest escapes pack directory"); return failure(); }
    String manifest;
    auto manifestRead = ReadText(fs::u8path(path), manifest, diagnostics);
    if (!manifestRead) { return eastl::unexpected(manifestRead.error()); }
    Document document;
    if (!ReadJson(manifest, document, diagnostics, String(path))) { return failure(); }
    auto version = ValidateVersion(yyjson_doc_get_root(document.value), diagnostics, String(path));
    if (!version) { return eastl::unexpected(version.error()); }
    Vector<String> roots;
    if (!Roots(yyjson_doc_get_root(document.value), roots)) { Fail(diagnostics, String(path), "invalid or overlapping source_directories"); return failure(); }
    Vector<SpriteSourceFile> files;
    std::size_t metadataBytes = manifest.size();
    for (const auto& directory : roots) {
        auto sourceRoot = root / fs::u8path(directory.c_str());
        if (!fs::exists(sourceRoot, error) && !error) { continue; }
        const auto canonical = fs::canonical(sourceRoot, error);
        if (error || !Contained(root, canonical)) { Fail(diagnostics, directory, "source directory escapes pack"); return failure(); }
        const auto rootStatus = fs::symlink_status(sourceRoot, error);
        if (error || fs::is_symlink(rootStatus)) { Fail(diagnostics, directory, "directory symlinks are not supported"); return failure(); }
        fs::recursive_directory_iterator it(sourceRoot, error), end;
        while (!error && it != end) {
            const auto filePath = it->path();
            const auto resolved = fs::canonical(filePath, error);
            if (error || !Contained(root, resolved)) { Fail(diagnostics, Utf8(filePath), "source escapes pack directory"); return failure(); }
            const auto linkStatus = it->symlink_status(error);
            if (error) { Fail(diagnostics, Utf8(filePath), "cannot inspect source entry"); return failure(); }
            const auto targetStatus = it->status(error);
            if (error) { Fail(diagnostics, Utf8(filePath), "cannot inspect source target"); return failure(); }
            if (fs::is_symlink(linkStatus) && fs::is_directory(targetStatus)) { Fail(diagnostics, Utf8(filePath), "directory symlinks are not supported"); return failure(); }
            if (fs::is_regular_file(targetStatus)) {
                SpriteSourceFile file; file.path = Utf8(filePath.lexically_relative(root));
                if (Ends(file.path, ".png.meta")) {
                    auto png = filePath; png.replace_extension();
                    if (!fs::is_regular_file(png, error)) { Fail(diagnostics, file.path, "orphan PNG sidecar"); return failure(); }
                } else if (Ends(file.path, ".png") || Ends(file.path, ".meta")) {
                    if (files.size() >= MaxTilesetEntries) { Fail(diagnostics, file.path, "too many source files"); return failure(); }
                    if (Ends(file.path, ".png")) {
                        FILE* f = std::fopen(Utf8(filePath).c_str(), "rb");
                        unsigned char header[33]{};
                        const bool read = f && std::fread(header, 1, sizeof(header), f) == sizeof(header);
                        if (f) { std::fclose(f); }
                        constexpr unsigned char signature[] = {137,80,78,71,13,10,26,10};
                        const auto length = fs::file_size(filePath, error);
                        if (!read || error || length > 16 * 1024 * 1024 || std::memcmp(header, signature, 8)
                            || header[8] || header[9] || header[10] || header[11] != 13 || std::memcmp(header + 12, "IHDR", 4)) {
                            Fail(diagnostics, file.path, "invalid, truncated, or oversized PNG"); return failure();
                        }
                        auto big = [&](unsigned offset) { return (std::uint32_t(header[offset]) << 24) | (std::uint32_t(header[offset+1]) << 16)
                            | (std::uint32_t(header[offset+2]) << 8) | header[offset+3]; };
                        file.width = big(16); file.height = big(20);
                        auto sidecar = filePath; sidecar += ".meta";
                        const bool hasSidecar = fs::exists(sidecar, error);
                        if (error) { Fail(diagnostics, file.path, "cannot inspect PNG sidecar"); return failure(); }
                        if (hasSidecar) {
                            const auto resolvedMeta = fs::canonical(sidecar, error);
                            if (error || !Contained(root, resolvedMeta) || !ReadText(sidecar, file.text, diagnostics)) { Fail(diagnostics, file.path, "invalid PNG sidecar"); return failure(); }
                        }
                    } else if (!ReadText(filePath, file.text, diagnostics)) { return failure(); }
                    metadataBytes += file.text.size();
                    if (metadataBytes > MaxTilesetMetadataBytes) { Fail(diagnostics, file.path, "pack metadata exceeds 4 MiB limit"); return failure(); }
                    files.push_back(eastl::move(file));
                }
            }
            it.increment(error);
        }
        if (error) { Fail(diagnostics, directory, "cannot enumerate sprite directory"); return failure(); }
    }
    return ParseSourceTileset(manifest, {files.data(), files.size()}, content, diagnostics);
}

SpriteTransform ComposeSpriteTransforms(SpriteTransform inner, SpriteTransform outer) {
    // D4: move the outer reflection through the inner rotation.
    const int turns = int(outer.quarterTurns) + ((outer.flipX != outer.flipY) ? -int(inner.quarterTurns) : int(inner.quarterTurns));
    return {static_cast<std::uint8_t>((turns + 4) % 4), inner.flipX != outer.flipX, inner.flipY != outer.flipY};
}

TilesetMapping ResolveVisual(const TilesetDefinition& d, TilesetMapping mapping,
    int worldX, int worldZ, int worldY, std::uint64_t entity, std::uint8_t connections) {
    // FNV-1a over UTF-8 group ID followed by explicitly little-endian identity bytes.
    const auto activation = mapping.activation;
    for (unsigned depth = 0; mapping.kind == TilesetMappingKind::Visual; ++depth) {
        if (depth >= 2 || mapping.index >= d.visuals.size()) { std::abort(); }
        const auto& group = d.visuals[mapping.index];
        const VisualAlternative* chosen = nullptr;
        if (group.kind == VisualGroupKind::Connections) { chosen = &group.alternatives[connections & 15]; }
        else {
            std::uint64_t hash = 14695981039346656037ULL;
            auto byte = [&](std::uint8_t b) { hash = (hash ^ b) * 1099511628211ULL; };
            for (char c : group.id) { byte(static_cast<std::uint8_t>(c)); }
            byte(0); byte(entity ? 1 : 0);
            auto integer = [&](std::uint64_t v, unsigned bytes) { for (unsigned i = 0; i < bytes; ++i) { byte(static_cast<std::uint8_t>(v >> (i * 8))); } };
            if (entity) { integer(entity, 8); }
            else { integer(static_cast<std::uint32_t>(worldX), 4); integer(static_cast<std::uint32_t>(worldZ), 4); integer(static_cast<std::uint32_t>(worldY), 4); }
            if (!group.totalWeight) { std::abort(); }
            auto choice = hash % group.totalWeight;
            for (const auto& alternative : group.alternatives) {
                if (choice < alternative.weight) { chosen = &alternative; break; }
                choice -= alternative.weight;
            }
        }
        if (!chosen) { std::abort(); }
        const auto transform = ComposeSpriteTransforms(chosen->mapping.transform, mapping.transform);
        mapping = chosen->mapping; mapping.transform = transform;
    }
    mapping.activation = activation;
    return mapping;
}
}
