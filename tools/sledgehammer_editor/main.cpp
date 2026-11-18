#include "workspace.h"

#include "content/content.h"
#include "content/tileset.h"
#include "worldgen/worldgen.h"

#include <yyjson.h>

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

namespace cataclysmx::editor {
namespace {

using Value = yyjson_mut_val;
using Document = std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)>;

constexpr std::size_t maxRequestBytes = 16 * 1024 * 1024;
constexpr std::string_view defaultTileset = "assets/tiles/gen1_32x32";

std::string_view Text(const yyjson_val* value) {
    return yyjson_is_str(value) ? std::string_view(yyjson_get_str(value), yyjson_get_len(value)) : std::string_view{};
}

std::string_view Text(const String& value) {
    return {value.data(), value.size()};
}

struct Reply {
    std::unique_ptr<yyjson_mut_doc, decltype(&yyjson_mut_doc_free)> document{yyjson_mut_doc_new(nullptr), yyjson_mut_doc_free};
    Value* root = Object();

    Reply() { yyjson_mut_doc_set_root(document.get(), root); }

    Value* Object() { return yyjson_mut_obj(document.get()); }

    Value* Array() { return yyjson_mut_arr(document.get()); }

    Value* Array(Value* object, const char* name) {
        auto* array = Array();
        yyjson_mut_obj_add_val(document.get(), object, name, array);
        return array;
    }

    void Push(Value* array, Value* value) { yyjson_mut_arr_append(array, value); }

    void Push(Value* array, int value) { yyjson_mut_arr_append(array, yyjson_mut_sint(document.get(), value)); }

    void Push(Value* array, std::string_view text) {
        yyjson_mut_arr_append(array, yyjson_mut_strncpy(document.get(), text.data(), text.size()));
    }

    void Set(Value* object, const char* key, std::string_view text) {
        yyjson_mut_obj_add_strncpy(document.get(), object, key, text.data(), text.size());
    }

    void Number(Value* object, const char* key, int value) {
        yyjson_mut_obj_add_sint(document.get(), object, key, value);
    }

    void Bool(Value* object, const char* key, bool value) {
        yyjson_mut_obj_add_bool(document.get(), object, key, value);
    }

    void Error(std::string_view message) { Set(root, "error", message); }

    void DiagnosticsFrom(const Diagnostics& diagnostics, const fs::path& sourceRoot) {
        auto* array = Array(root, "diagnostics");
        const auto prefix = PathText(sourceRoot) + '/';
        for (const auto& frame : diagnostics.Frames()) {
            auto* entry = Object();
            Set(entry, "message", Text(frame.message));
            if (frame.location) {
                auto file = Text(frame.location->file);
                if (file.starts_with(prefix)) { file.remove_prefix(prefix.size()); }
                Set(entry, "file", file);
                Set(entry, "path", Text(frame.location->path));
                if (frame.location->line) { Number(entry, "line", static_cast<int>(*frame.location->line)); }
                if (frame.location->column) { Number(entry, "column", static_cast<int>(*frame.location->column)); }
            }
            Push(array, entry);
        }
    }

    void Write() {
        std::size_t length = 0;
        char* text = yyjson_mut_write(document.get(), 0, &length);
        if (!text) { std::abort(); }
        std::fwrite(text, 1, length, stdout);
        std::fputc('\n', stdout);
        std::free(text);
    }
};

Document Parse(const std::string& text, yyjson_read_flag flags = YYJSON_READ_JSON5) {
    return {yyjson_read(text.data(), text.size(), flags), yyjson_doc_free};
}

std::string ManifestSource(std::string_view path) {
    std::string normalized = "data";
    while (!path.empty()) {
        const auto slash = path.find_first_of("/\\");
        const auto part = path.substr(0, slash);
        if (!part.empty() && part != ".") {
            normalized += '/';
            normalized += part;
        }
        if (slash == std::string_view::npos) { break; }
        path.remove_prefix(slash + 1);
    }
    return normalized;
}

struct Manifest {
    std::string path;
    std::string text;
    std::vector<std::string> files;
};

bool ReadManifest(const fs::path& root, std::string_view path, Manifest& manifest) {
    if (!path.starts_with("data/") || !path.ends_with(".json5") || path.find('/', 5) != std::string_view::npos
        || !SafePath(root, path) || !ReadFile(root / fs::u8path(path), manifest.text)) { return false; }
    const auto doc = Parse(manifest.text);
    auto* files = yyjson_obj_get(yyjson_doc_get_root(doc.get()), "files");
    if (!yyjson_is_arr(files)) { return false; }
    manifest.path = path;
    std::size_t index, size;
    yyjson_val* value;
    yyjson_arr_foreach(files, index, size, value) {
        const auto raw = Text(value);
        if (raw.empty() || raw.front() == '/' || raw.front() == '\\') { return false; }
        auto normalized = ManifestSource(raw);
        if (!SafePath(root, normalized) || normalized == "data") { return false; }
        manifest.files.push_back(std::move(normalized));
    }
    return true;
}

std::vector<Manifest> ReadManifests(const fs::path& root) {
    std::vector<Manifest> manifests;
    if (!SafePath(root, "data")) { return manifests; }
    std::error_code error;
    for (fs::directory_iterator it(root / "data", error), end; !error && it != end; it.increment(error)) {
        Manifest manifest;
        const auto relative = PathText(it->path().lexically_relative(root));
        if (ReadManifest(root, relative, manifest)) { manifests.push_back(std::move(manifest)); }
    }
    std::ranges::sort(manifests, {}, &Manifest::path);
    return manifests;
}

void AddSource(Reply& reply, Value* array, std::string_view path, std::string_view text, bool editable) {
    auto* file = reply.Object();
    reply.Set(file, "path", path);
    reply.Set(file, "text", text);
    reply.Bool(file, "editable", editable);
    reply.Push(array, file);
}

bool OpenTileset(const fs::path& root, std::string_view directory, Reply& reply) {
    const auto manifest = std::string(directory) + "/tileset.json5";
    std::string text;
    if (!SafePath(root, manifest) || !ReadFile(root / fs::u8path(manifest), text)) {
        reply.Error("Could not read " + manifest);
        return false;
    }
    reply.Set(reply.root, "tileset", manifest);
    auto* metadata = reply.Array(reply.root, "metadata");
    AddSource(reply, metadata, manifest, text, true);
    auto* images = reply.Array(reply.root, "images");
    const auto doc = Parse(text);
    auto* sources = yyjson_obj_get(yyjson_doc_get_root(doc.get()), "source_directories");
    std::vector<std::string> sourceDirs;
    if (sources) {
        std::size_t index, size;
        yyjson_val* value;
        yyjson_arr_foreach(sources, index, size, value) {
            if (RelativePath(Text(value))) { sourceDirs.emplace_back(Text(value)); }
        }
    } else {
        sourceDirs = {"tile", "sprite"};
    }
    const auto base = root / fs::u8path(directory);
    std::vector<std::string> paths;
    for (const auto& part : sourceDirs) {
        if (!SafePath(base, part)) { continue; }
        std::error_code error;
        const auto source = base / fs::u8path(part);
        if (!fs::is_directory(source, error)) { continue; }
        for (fs::recursive_directory_iterator it(source, error), end; !error && it != end; it.increment(error)) {
            if (fs::is_regular_file(it->symlink_status(error))) {
                paths.push_back(PathText(it->path().lexically_relative(base)));
            }
        }
    }
    std::ranges::sort(paths);
    for (const auto& path : paths) {
        if (path.ends_with(".png")) {
            auto* image = reply.Object();
            auto id = path.substr(0, path.size() - 4);
            std::ranges::replace(id, '/', '.');
            reply.Set(image, "id", id);
            reply.Set(image, "path", path);
            reply.Push(images, image);
        } else if (path.ends_with(".meta") && !path.ends_with(".png.meta")
                   && ReadFile(base / fs::u8path(path), text)) {
            AddSource(reply, metadata, std::string(directory) + '/' + path, text, true);
        }
    }
    return true;
}

void Open(const fs::path& root, std::string_view pack, std::string_view tileset, Reply& reply) {
    auto manifests = ReadManifests(root);
    const auto selected = std::ranges::find(manifests, pack, &Manifest::path);
    if (selected == manifests.end()) {
        reply.Error("Could not read the selected content manifest.");
        return;
    }
    auto* manifestFiles = reply.Array(reply.root, "manifests");
    for (const auto& manifest : manifests) {
        AddSource(reply, manifestFiles, manifest.path, manifest.text, true);
    }
    auto* files = reply.Array(reply.root, "sources");
    for (const auto& path : selected->files) {
        std::string text;
        if (!ReadFile(root / fs::u8path(path), text)) {
            reply.Error("Could not read " + path);
            return;
        }
        AddSource(reply, files, path, text, EditablePath(path, tileset));
    }
    if (OpenTileset(root, tileset, reply)) { reply.Bool(reply.root, "ok", true); }
}

bool ReadEdits(yyjson_val* request, std::string_view tileset, std::vector<SourceEdit>& edits) {
    auto* files = yyjson_obj_get(request, "edits");
    if (!yyjson_is_arr(files) || yyjson_arr_size(files) > 512) { return false; }
    std::size_t index, size;
    yyjson_val* file;
    yyjson_arr_foreach(files, index, size, file) {
        const auto path = Text(yyjson_obj_get(file, "path"));
        auto* before = yyjson_obj_get(file, "before");
        auto* after = yyjson_obj_get(file, "text");
        if (!EditablePath(path, tileset) || !(yyjson_is_str(before) || yyjson_is_null(before)) ||
            !yyjson_is_str(after) || Text(after).empty() ||
            std::ranges::find(edits, path, &SourceEdit::path) != edits.end()) { return false; }
        SourceEdit edit{std::string(path), {}, std::string(Text(after))};
        if (yyjson_is_str(before)) { edit.before = Text(before); }
        if (!edit.before || *edit.before != edit.text) { edits.push_back(std::move(edit)); }
    }
    return true;
}

bool Affects(const Manifest& manifest, std::span<const SourceEdit> edits, std::string_view tileset) {
    return std::ranges::any_of(edits, [&](const SourceEdit& edit) {
        return edit.path == manifest.path || edit.path.starts_with(tileset)
            || std::ranges::find(manifest.files, edit.path) != manifest.files.end();
    });
}

ContentResult<ContentCatalog> Load(const fs::path& candidate, std::string_view pack,
                                   std::string_view tileset, Diagnostics& diagnostics) {
    const auto path = PathText(candidate / fs::u8path(pack));
    auto content = LoadContentPack(path.c_str(), &diagnostics);
    if (!content) { return content; }
    auto world = ValidateWorldgenContent(*content, &diagnostics);
    if (!world) { return eastl::unexpected(world.error()); }
    if (!tileset.empty()) {
        const auto metadata = PathText(candidate / fs::u8path(tileset) / "tileset.json5");
        auto graphics = LoadTilesetFile(metadata.c_str(), *content, &diagnostics);
        if (!graphics) { return eastl::unexpected(graphics.error()); }
    }
    return content;
}

bool ReadInteger(yyjson_val* request, const char* key, int min, int max, int& output) {
    auto* value = yyjson_obj_get(request, key);
    if (!yyjson_is_int(value) || (yyjson_is_uint(value) && yyjson_get_uint(value) > static_cast<unsigned>(max))) {
        return false;
    }
    const auto number = yyjson_get_sint(value);
    if (number < min || number > max) { return false; }
    output = static_cast<int>(number);
    return true;
}

int FloorChunk(int coordinate) {
    return coordinate / chunk_size - (coordinate % chunk_size < 0 ? 1 : 0);
}

void Position(Reply& reply, Value* entry, TileCoord position) {
    reply.Number(entry, "x", position.x);
    reply.Number(entry, "z", position.z);
    reply.Number(entry, "y", position.y);
}

void Rect(Reply& reply, Value* entry, const TileRect& bounds) {
    auto* array = reply.Array(entry, "bounds");
    for (int value : {bounds.min.x, bounds.min.z, bounds.max.x, bounds.max.z}) { reply.Push(array, value); }
}

void Placements(Reply& reply, Value* array, const ContentCatalog& content, const ChunkPlacements& placements) {
    for (const auto& object : placements.furniture) {
        auto* entry = reply.Object();
        Position(reply, entry, object.position);
        reply.Set(entry, "kind", "furniture");
        reply.Set(entry, "id", Text(content.GetObject(object.type).id));
        reply.Number(entry, "facing", static_cast<int>(object.facing));
        reply.Push(array, entry);
    }
    for (const auto& item : placements.items) {
        auto* entry = reply.Object();
        Position(reply, entry, item.position);
        reply.Set(entry, "kind", "loot");
        reply.Set(entry, "id", Text(content.GetItem(item.type).id));
        reply.Set(entry, "destination", item.destination == LootDestination::Ground ? "ground" : "furniture");
        reply.Push(array, entry);
    }
    for (const auto& monster : placements.monsters) {
        auto* entry = reply.Object();
        Position(reply, entry, monster.position);
        reply.Set(entry, "kind", "monster");
        reply.Set(entry, "id", Text(content.GetMonster(monster.type).id));
        reply.Push(array, entry);
    }
}

void Color(Reply& reply, Value* entry, const char* key, RgbColor color) {
    char text[8];
    std::snprintf(text, sizeof(text), "#%02x%02x%02x",
        static_cast<unsigned>(color.red), static_cast<unsigned>(color.green), static_cast<unsigned>(color.blue));
    reply.Set(entry, key, text);
}

void Preview(const ContentCatalog& content, yyjson_val* request, Reply& reply) {
    std::uint64_t seed = 0;
    const auto seedText = Text(yyjson_obj_get(request, "seed"));
    if (seedText.empty()) { reply.Error("Enter an unsigned 64-bit seed."); return; }
    const auto parsedSeed = std::from_chars(seedText.data(), seedText.data() + seedText.size(), seed);
    if (parsedSeed.ec != std::errc{} || parsedSeed.ptr != seedText.data() + seedText.size()) {
        reply.Error("Enter an unsigned 64-bit seed."); return;
    }
    int y = 0;
    RegionCoord region;
    constexpr int minRegion = -world_coordinate_limit / region_size - 1;
    constexpr int maxRegion = world_coordinate_limit / region_size;
    if (!ReadInteger(request, "regionX", minRegion, maxRegion, region.x)
        || !ReadInteger(request, "regionZ", minRegion, maxRegion, region.z)
        || !ReadInteger(request, "y", world_min_y, world_max_y, y)) {
        reply.Error("Region coordinates or elevation are outside the world."); return;
    }
    const auto plan = GenerateRegionPlan(content, seed, region);
    Rect(reply, reply.root, plan.bounds);
    reply.Number(reply.root, "version", plan.generationVersion);
    reply.Number(reply.root, "profile", static_cast<int>(plan.townProfile));

    auto* parcels = reply.Array(reply.root, "parcels");
    for (const auto& parcel : plan.parcels) {
        auto* entry = reply.Object();
        Rect(reply, entry, parcel.bounds);
        reply.Number(entry, "site", static_cast<int>(parcel.site));
        reply.Push(parcels, entry);
    }

    auto* buildings = reply.Array(reply.root, "buildings");
    for (const auto& building : plan.buildings) {
        const auto& pattern = content.GetBuildingPattern(building.pattern);
        auto* entry = reply.Object();
        Rect(reply, entry, building.bounds);
        reply.Set(entry, "id", Text(pattern.id));
        reply.Number(entry, "rotation", building.quarterTurns * 90);
        reply.Number(entry, "entranceX", building.entrance.x);
        reply.Number(entry, "entranceZ", building.entrance.z);
        reply.Bool(entry, "abandoned", building.abandoned);
        if (building.basementVariant) {
            reply.Set(entry, "basement", Text(pattern.basements[building.basementVariant - 1].id));
        }
        reply.Push(buildings, entry);
    }

    auto* terrain = reply.Array(reply.root, "terrain");
    reply.Push(terrain, reply.Object());
    for (std::uint32_t index = 1; index <= content.TerrainCount(); ++index) {
        const auto& definition = content.GetTerrain({index});
        auto* entry = reply.Object();
        reply.Set(entry, "id", Text(definition.id));
        reply.Set(entry, "symbol", Text(definition.appearance.symbol));
        Color(reply, entry, "foreground", definition.appearance.foreground);
        Color(reply, entry, "background", definition.appearance.background);
        reply.Push(terrain, entry);
    }

    const int width = plan.bounds.max.x - plan.bounds.min.x;
    const int height = plan.bounds.max.z - plan.bounds.min.z;
    std::vector<int> tiles(width * height);
    auto* placements = reply.Array(reply.root, "placements");
    ChunkPlacements generated;
    for (int cz = FloorChunk(plan.bounds.min.z); cz <= FloorChunk(plan.bounds.max.z - 1); ++cz) {
        for (int cx = FloorChunk(plan.bounds.min.x); cx <= FloorChunk(plan.bounds.max.x - 1); ++cx) {
            const ChunkCoord chunk{cx, cz, y};
            const auto result = GenerateChunk(content, seed, plan, chunk);
            GenerateChunkPlacements(content, seed, plan, chunk, generated);
            Placements(reply, placements, content, generated);
            for (int lz = 0; lz < chunk_size; ++lz) {
                for (int lx = 0; lx < chunk_size; ++lx) {
                    const int x = cx * chunk_size + lx - plan.bounds.min.x;
                    const int z = cz * chunk_size + lz - plan.bounds.min.z;
                    if (x >= 0 && x < width && z >= 0 && z < height) {
                        tiles[z * width + x] = static_cast<int>(result[lz * chunk_size + lx].terrain.value);
                    }
                }
            }
        }
    }
    auto* tileArray = reply.Array(reply.root, "tiles");
    auto* biomes = reply.Array(reply.root, "biomes");
    auto* roads = reply.Array(reply.root, "roads");
    for (int z = 0; z < height; ++z) {
        for (int x = 0; x < width; ++x) {
            const TileCoord position{plan.bounds.min.x + x, plan.bounds.min.z + z, y};
            reply.Push(tileArray, tiles[z * width + x]);
            reply.Push(biomes, SampleEnvironment(seed, position).biome == Biome::Woodland ? 1 : 0);
            reply.Push(roads, IsRoad(plan, position) ? 1 : 0);
        }
    }
    reply.Bool(reply.root, "ok", true);
}

void Run(const fs::path& root, const fs::path& candidate, std::string_view tileset,
         yyjson_val* request, Reply& reply) {
    const auto operation = Text(yyjson_obj_get(request, "operation"));
    std::string error;
    if (operation == "recover") {
        if (RecoverSave(root, error)) { reply.Bool(reply.root, "ok", true); }
        else { reply.Error(error); }
        return;
    }
    const auto pack = Text(yyjson_obj_get(request, "pack"));
    Manifest manifest;
    if (!ReadManifest(root, pack, manifest)) {
        reply.Error("Select a content manifest inside data/.");
        return;
    }
    if (operation == "open") { Open(root, pack, tileset, reply); return; }
    if (operation != "preview" && operation != "save" && operation != "validate") {
        reply.Error("Unknown editor operation.");
        return;
    }
    std::vector<SourceEdit> edits;
    if (!ReadEdits(request, tileset, edits)) { reply.Error("Invalid source edits."); return; }
    if (!MatchesCheckout(root, edits, error) || !CopySources(root, candidate, tileset, error) ||
        !WriteCandidate(candidate, edits, error)) {
        reply.Error(error);
        return;
    }
    Diagnostics diagnostics;
    auto content = Load(candidate, pack, tileset, diagnostics);
    if (!content) {
        reply.Error("Content validation failed for " + std::string(pack));
        reply.DiagnosticsFrom(diagnostics, candidate);
        return;
    }
    if (operation == "preview") { Preview(*content, request, reply); return; }

    const auto known = ReadManifests(root);
    const auto current = ReadManifests(candidate);
    for (const auto& prior : known) {
        if (prior.path == pack) { continue; }
        const auto changed = std::ranges::find(current, prior.path, &Manifest::path);
        if (!Affects(prior, edits, tileset) && (changed == current.end() || !Affects(*changed, edits, tileset))) {
            continue;
        }
        diagnostics.Clear();
        if (!Load(candidate, prior.path, tileset == defaultTileset ? tileset : std::string_view{}, diagnostics)) {
            reply.Error("Content validation failed for " + prior.path);
            reply.DiagnosticsFrom(diagnostics, candidate);
            return;
        }
    }
    if (operation == "save" && !SaveSources(root, edits, error)) { reply.Error(error); return; }
    reply.Number(reply.root, "files", static_cast<int>(edits.size()));
    reply.Bool(reply.root, "ok", true);
}

}
}

int main(int argc, char** argv) {
    using namespace cataclysmx::editor;
    Reply reply;
    if (argc != 4 || !std::string_view(argv[3]).starts_with("assets/tiles/") || !RelativePath(argv[3])) {
        reply.Error("Expected checkout, candidate directory, and relative tileset directory.");
    } else {
        std::string request;
        char buffer[8192];
        while (std::cin && request.size() <= maxRequestBytes) {
            std::cin.read(buffer, sizeof(buffer));
            request.append(buffer, static_cast<std::size_t>(std::cin.gcount()));
        }
        const auto doc = request.size() <= maxRequestBytes ? Parse(request, 0) : Document(nullptr, yyjson_doc_free);
        auto* root = yyjson_doc_get_root(doc.get());
        if (!yyjson_is_obj(root)) { reply.Error("Invalid editor request."); }
        else { Run(fs::u8path(argv[1]), fs::u8path(argv[2]), argv[3], root, reply); }
    }
    reply.Write();
    return 0;
}
