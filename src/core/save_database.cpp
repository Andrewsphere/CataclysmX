#include "core/save_database.h"
#include "core/data_object.h"
#include "items/item_behavior.h"
#include "items/item_snapshot.h"
#include "content/content.h"

#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <sqlite3.h>
#include <yyjson.h>

#include <cerrno>
#include <charconv>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
#error Engine must compile without exceptions
#endif

namespace cataclysmx {
namespace {

// A backpack chain may contain at most 64 items, including its root item.
constexpr unsigned maxItemDepth = 64;

eastl::unexpected<SaveError> Failure(SaveError error, const char* message, Diagnostics* diagnostics) {
    if (diagnostics) { diagnostics->Add(message); }
    return eastl::unexpected(error);
}

SaveError SqlError(int code) {
    if (code == SQLITE_IOERR_NOMEM) { std::abort(); }
    switch (code & 255) {
    case SQLITE_NOMEM: std::abort();
    case SQLITE_BUSY:
    case SQLITE_LOCKED: return SaveError::Busy;
    case SQLITE_CORRUPT:
    case SQLITE_NOTADB:
    case SQLITE_SCHEMA:
    case SQLITE_ERROR:
    case SQLITE_CONSTRAINT:
    case SQLITE_MISMATCH: return SaveError::InvalidData;
    default: return SaveError::Io;
    }
}

struct Connection {
    sqlite3* value = nullptr;
    ~Connection() { if (value) { sqlite3_close_v2(value); } }
};

struct Statement {
    sqlite3_stmt* value = nullptr;
    ~Statement() { sqlite3_finalize(value); }
    int Prepare(sqlite3* db, const char* sql) {
        return sqlite3_prepare_v2(db, sql, -1, &value, nullptr);
    }
};

void* JsonMalloc(void*, std::size_t size) {
    void* result = std::malloc(size);
    if (!result) { std::abort(); }
    return result;
}
void* JsonRealloc(void*, void* pointer, std::size_t, std::size_t size) {
    void* result = std::realloc(pointer, size);
    if (!result) { std::abort(); }
    return result;
}
void JsonFree(void*, void* pointer) { std::free(pointer); }

yyjson_alc JsonAllocator() { return {JsonMalloc, JsonRealloc, JsonFree, nullptr}; }

struct Document {
    yyjson_doc* value = nullptr;
    ~Document() { yyjson_doc_free(value); }
    bool Parse(const char* text, std::size_t length) {
        yyjson_alc allocator = JsonAllocator();
        yyjson_read_err error{};
        value = yyjson_read_opts(const_cast<char*>(text), length, 0, &allocator, &error);
        if (error.code == YYJSON_READ_ERROR_MEMORY_ALLOCATION) { std::abort(); }
        return value != nullptr;
    }
};

struct JsonWriter {
    yyjson_mut_doc* doc;
    JsonWriter() {
        yyjson_alc allocator = JsonAllocator();
        doc = yyjson_mut_doc_new(&allocator);
        if (!doc) { std::abort(); }
    }
    ~JsonWriter() { yyjson_mut_doc_free(doc); }
    yyjson_mut_val* Object() { return yyjson_mut_obj(doc); }
    yyjson_mut_val* Array() { return yyjson_mut_arr(doc); }
    void Add(yyjson_mut_val* object, const char* key, yyjson_mut_val* value) {
        if (!yyjson_mut_obj_add_val(doc, object, key, value)) { std::abort(); }
    }
    void Append(yyjson_mut_val* array, yyjson_mut_val* value) {
        if (!yyjson_mut_arr_add_val(array, value)) { std::abort(); }
    }
    void Text(yyjson_mut_val* object, const char* key, const char* value) {
        Add(object, key, yyjson_mut_strcpy(doc, value));
    }
    void Counter(yyjson_mut_val* object, const char* key, std::uint64_t value) {
        char buffer[32];
        const auto result = std::to_chars(buffer, buffer + sizeof(buffer) - 1, value);
        *result.ptr = '\0';
        Text(object, key, buffer);
    }
    void Integer(yyjson_mut_val* object, const char* key, std::int64_t value) {
        Add(object, key, yyjson_mut_sint(doc, value));
    }
    void Boolean(yyjson_mut_val* object, const char* key, bool value) {
        Add(object, key, yyjson_mut_bool(doc, value));
    }
    void Position(yyjson_mut_val* object, TileCoord position) {
        auto* value = Object();
        Integer(value, "x", position.x);
        Integer(value, "z", position.z);
        Integer(value, "y", position.y);
        Add(object, "position", value);
    }
    String Finish(yyjson_mut_val* root) {
        yyjson_mut_doc_set_root(doc, root);
        yyjson_alc allocator = JsonAllocator();
        std::size_t length = 0;
        char* text = yyjson_mut_write_opts(doc, 0, &allocator, &length, nullptr);
        if (!text) { std::abort(); }
        String result(text, length);
        std::free(text);
        return result;
    }
};

// Exact field sets reject unknown fields and duplicate keys, including two copies
// of a known key replacing a different required field.
bool Fields(yyjson_val* object, std::initializer_list<const char*> names) {
    if (!yyjson_is_obj(object) || yyjson_obj_size(object) != names.size()) { return false; }
    for (const char* name : names) {
        unsigned count = 0;
        yyjson_obj_iter iterator = yyjson_obj_iter_with(object);
        while (yyjson_val* key = yyjson_obj_iter_next(&iterator)) {
            if (yyjson_equals_str(key, name)) { ++count; }
        }
        if (count != 1) { return false; }
    }
    return true;
}

yyjson_val* Get(yyjson_val* object, const char* key) { return yyjson_obj_get(object, key); }

bool Counter(yyjson_val* value, std::uint64_t& result) {
    if (!yyjson_is_str(value)) { return false; }
    const char* text = yyjson_get_str(value);
    const std::size_t length = yyjson_get_len(value);
    if (length == 0 || (length > 1 && text[0] == '0')) { return false; }
    std::uint64_t number = 0;
    for (std::size_t i = 0; i < length; ++i) {
        if (text[i] < '0' || text[i] > '9') { return false; }
        const unsigned digit = static_cast<unsigned>(text[i] - '0');
        if (number > (UINT64_MAX - digit) / 10) { return false; }
        number = number * 10 + digit;
    }
    result = number;
    return true;
}

bool Integer(yyjson_val* value, int& result) {
    if (!yyjson_is_int(value)) { return false; }
    if (yyjson_is_uint(value) && yyjson_get_uint(value) > INT_MAX) { return false; }
    const std::int64_t number = yyjson_get_sint(value);
    if (number < INT_MIN || number > INT_MAX) { return false; }
    result = static_cast<int>(number);
    return true;
}

bool Unsigned32(yyjson_val* value, std::uint32_t& result) {
    if (!yyjson_is_uint(value) || yyjson_get_uint(value) > UINT32_MAX) { return false; }
    result = static_cast<std::uint32_t>(yyjson_get_uint(value));
    return true;
}

bool Position(yyjson_val* value, TileCoord& position) {
    return Fields(value, {"x", "z", "y"}) && Integer(Get(value, "x"), position.x)
        && Integer(Get(value, "z"), position.z) && Integer(Get(value, "y"), position.y)
        && ValidPosition(position);
}

bool Byte(yyjson_val* value, std::uint8_t& result) {
    int number = 0;
    if (!Integer(value, number) || number < 0 || number > 255) { return false; }
    result = static_cast<std::uint8_t>(number);
    return true;
}

bool Light(yyjson_val* value, LightEmission& light) {
    return Fields(value, {"intensity", "range"})
        && Byte(Get(value, "intensity"), light.intensity)
        && Byte(Get(value, "range"), light.range) && ValidLightEmission(light);
}

void EncodeLight(JsonWriter& writer, yyjson_mut_val* value, LightEmission light) {
    auto* emission = writer.Object();
    writer.Integer(emission, "intensity", light.intensity);
    writer.Integer(emission, "range", light.range);
    writer.Add(value, "light", emission);
}

bool Appearance(yyjson_val* value, ObjectAppearance& appearance) {
    int kind = 0;
    int facing = 0;
    if (!Integer(Get(value, "kind"), kind) || kind < 1 || kind > 4
        || !Integer(Get(value, "facing"), facing) || facing < 0 || facing > 3) { return false; }
    appearance.kind = static_cast<ObjectKind>(kind);
    appearance.facing = static_cast<ObjectFacing>(facing);
    return true;
}

// Positive JSON integers are unsigned. Signed integers use a tagged decimal
// string, preserving even signed zero and INT64_MAX without a floating cast.
yyjson_mut_val* EncodeValue(JsonWriter& writer, const DataObject& value, bool escape = true) {
    switch (value.Type()) {
    case DataType::Boolean: return yyjson_mut_bool(writer.doc, value.BooleanValue());
    case DataType::Unsigned: return yyjson_mut_uint(writer.doc, value.UnsignedValue());
    case DataType::Signed: {
        auto* result = writer.Object();
        char buffer[32];
        auto converted = std::to_chars(buffer, buffer + sizeof(buffer) - 1, value.SignedValue());
        *converted.ptr = '\0';
        writer.Text(result, "$i64", buffer);
        return result;
    }
    case DataType::String: return yyjson_mut_strncpy(writer.doc, value.StringValue().data(), value.StringValue().size());
    case DataType::List: {
        auto* result = writer.Array();
        for (std::size_t i = 0; i < value.Size(); ++i) { writer.Append(result, EncodeValue(writer, value.At(i))); }
        return result;
    }
    case DataType::Object: {
        if (escape && (value.Find("$i64") || value.Find("$object"))) {
            auto* result = writer.Object();
            writer.Add(result, "$object", EncodeValue(writer, value, false));
            return result;
        }
        auto* result = writer.Object();
        for (std::size_t i = 0; i < value.Size(); ++i) {
            auto* key = yyjson_mut_strncpy(writer.doc, value.Name(i).data(), value.Name(i).size());
            if (!yyjson_mut_obj_add(result, key, EncodeValue(writer, value.At(i)))) { std::abort(); }
        }
        return result;
    }
    }
    std::abort();
}

DataResult<DataObject> DecodeValue(yyjson_val* value, unsigned depth = 0, bool escaped = false) {
    if (depth > 64) { return eastl::unexpected(DataError::DepthLimit); }
    if (yyjson_is_bool(value)) { return DataObject::Boolean(yyjson_get_bool(value)); }
    if (yyjson_is_uint(value)) { return DataObject::Unsigned(yyjson_get_uint(value)); }
    if (yyjson_is_sint(value)) { return DataObject::Signed(yyjson_get_sint(value)); }
    if (yyjson_is_str(value)) { return DataObject::Text({yyjson_get_str(value), yyjson_get_len(value)}); }
    if (yyjson_is_arr(value)) {
        DataObject result = DataObject::List();
        yyjson_arr_iter iterator = yyjson_arr_iter_with(value);
        while (auto* child = yyjson_arr_iter_next(&iterator)) {
            auto decoded = DecodeValue(child, depth + 1);
            if (!decoded) { return eastl::unexpected(decoded.error()); }
            result.Append(eastl::move(*decoded));
        }
        return result;
    }
    if (!yyjson_is_obj(value)) { return eastl::unexpected(DataError::InvalidData); }
    if (!escaped && Fields(value, {"$i64"})) {
        auto* number = Get(value, "$i64");
        if (!yyjson_is_str(number)) { return eastl::unexpected(DataError::InvalidData); }
        const char* first = yyjson_get_str(number);
        const char* last = first + yyjson_get_len(number);
        std::int64_t parsed = 0;
        const auto converted = std::from_chars(first, last, parsed);
        char canonical[32];
        auto written = std::to_chars(canonical, canonical + sizeof(canonical), parsed);
        if (converted.ec != std::errc{} || converted.ptr != last
            || static_cast<std::size_t>(written.ptr - canonical) != yyjson_get_len(number)
            || std::memcmp(canonical, first, yyjson_get_len(number)) != 0) { return eastl::unexpected(DataError::InvalidData); }
        return DataObject::Signed(parsed);
    }
    if (!escaped && Fields(value, {"$object"})) {
        auto* object = Get(value, "$object");
        if (!yyjson_is_obj(object)) { return eastl::unexpected(DataError::InvalidData); }
        return DecodeValue(object, depth, true);
    }
    DataObject result;
    yyjson_obj_iter iterator = yyjson_obj_iter_with(value);
    while (auto* key = yyjson_obj_iter_next(&iterator)) {
        const eastl::string_view name(yyjson_get_str(key), yyjson_get_len(key));
        if (result.Find(name)) { return eastl::unexpected(DataError::InvalidData); }
        auto decoded = DecodeValue(yyjson_obj_iter_get_val(key), depth + 1);
        if (!decoded) { return eastl::unexpected(decoded.error()); }
        result.Set(name, eastl::move(*decoded));
    }
    return result;
}

bool ValidChunk(ChunkCoord position) {
    constexpr int limit = world_coordinate_limit / chunk_size;
    return position.x >= -limit && position.x <= limit
        && position.z >= -limit && position.z <= limit
        && position.y >= world_min_y && position.y <= world_max_y;
}

bool Unique(Vector<PersistentId>& ids);

bool ValidSnapshot(const SaveSnapshot& snapshot) {
    const auto& session = snapshot.session;
    if (session.pacing.ticksPerSecond == 0 || snapshot.nextId == 0 || session.player >= snapshot.nextId
        || !ValidPosition(session.playerPosition) || session.activeRadius < 0
        || session.residentRadius < session.activeRadius || session.residentRadius > 8
        || session.players.size() > 32
        || (session.circleResidency && (session.circleResidency->radiusTiles < 0
            || session.circleResidency->radiusTiles > maxCircleResidencyRadius))) {
        return false;
    }
    Vector<PersistentId> actorIds;
    Vector<std::uint64_t> tokens;
    bool hostFound = false;
    for (const SavedPlayer& player : session.players) {
        if (player.actor == 0 || player.actor >= snapshot.nextId || !ValidPosition(player.position)
            || player.name.empty() || player.name.size() > 32
            || std::memchr(player.name.data(), '\0', player.name.size())
            || (player.actor == session.player
                && (player.position != session.playerPosition || player.reconnectToken != 0))
            || (player.actor != session.player && player.reconnectToken == 0)) { return false; }
        hostFound = hostFound || player.actor == session.player;
        actorIds.push_back(player.actor);
        if (player.reconnectToken != 0) { tokens.push_back(player.reconnectToken); }
    }
    eastl::sort(tokens.begin(), tokens.end());
    if (!Unique(actorIds) || eastl::adjacent_find(tokens.begin(), tokens.end()) != tokens.end()
        || (session.player != 0 && !session.players.empty() && !hostFound)) { return false; }
    const auto center = ChunkOf(session.playerPosition);
    constexpr int limit = world_coordinate_limit / chunk_size;
    const int radius = session.circleResidency ? session.activeRadius : session.residentRadius;
    if (session.circleResidency) {
        const auto x = static_cast<std::int64_t>(session.playerPosition.x);
        const auto z = static_cast<std::int64_t>(session.playerPosition.z);
        const auto circleRadius = static_cast<std::int64_t>(session.circleResidency->radiusTiles);
        if (x - circleRadius < -world_coordinate_limit || x + circleRadius > world_coordinate_limit
            || z - circleRadius < -world_coordinate_limit || z + circleRadius > world_coordinate_limit) {
            return false;
        }
    }
    return center.x >= -limit + radius && center.x <= limit - radius
        && center.z >= -limit + radius && center.z <= limit - radius;
}

void ChunkContext(Diagnostics* diagnostics, ChunkCoord position, const char* operation) {
    if (!diagnostics) { return; }
    DiagnosticFrame frame;
    frame.message = operation;
    DiagnosticLocation location;
    char path[128];
    std::snprintf(path, sizeof(path), "chunks[%d,%d,%d]", position.x, position.z, position.y);
    location.path = path;
    frame.location = eastl::move(location);
    diagnostics->Add(eastl::move(frame));
}

void TerrainDiagnostic(Diagnostics* diagnostics, ChunkCoord position, std::size_t index,
    eastl::string_view id) {
    if (!diagnostics) { return; }
    DiagnosticFrame frame;
    frame.message = "Unknown terrain: ";
    frame.message.append(id.data(), id.size());
    DiagnosticLocation location;
    char path[128];
    std::snprintf(path, sizeof(path), "chunks[%d,%d,%d].tiles[%zu].terrain", position.x, position.z, position.y, index);
    location.path = path;
    frame.location = eastl::move(location);
    diagnostics->Add(eastl::move(frame));
}
bool Unique(Vector<PersistentId>& ids) {
    eastl::sort(ids.begin(), ids.end());
    return eastl::adjacent_find(ids.begin(), ids.end()) == ids.end();
}

bool ChunkLess(ChunkCoord a, ChunkCoord b) {
    if (a.x != b.x) { return a.x < b.x; }
    if (a.z != b.z) { return a.z < b.z; }
    return a.y < b.y;
}

void MemoryContext(Diagnostics* diagnostics, PersistentId player, ChunkCoord position) {
    if (!diagnostics) { return; }
    DiagnosticFrame frame;
    frame.message = "Validating player memory";
    DiagnosticLocation location;
    char path[160];
    std::snprintf(path, sizeof(path), "player_memories[%llu][%d,%d,%d]",
        static_cast<unsigned long long>(player), position.x, position.z, position.y);
    location.path = path;
    frame.location = eastl::move(location);
    diagnostics->Add(eastl::move(frame));
}

bool ValidMemories(const ContentCatalog& content, const SaveSnapshot& snapshot, Diagnostics* diagnostics) {
    Vector<PersistentId> players;
    for (const auto& memory : snapshot.memories) {
        if (memory.player == 0 || memory.player >= snapshot.nextId
            || (!players.empty() && players.back() >= memory.player)) { return false; }
        players.push_back(memory.player);
        for (std::size_t i = 0; i < memory.chunks.size(); ++i) {
            const auto& chunk = memory.chunks[i];
            if (!ValidChunk(chunk.position)
                || (i != 0 && !ChunkLess(memory.chunks[i - 1].position, chunk.position))) { return false; }
            bool observed = false;
            for (std::size_t tile = 0; tile < chunk.terrain.size(); ++tile) {
                const auto terrain = chunk.terrain[tile];
                observed |= terrain != TerrainId{};
                const TileCoord position{chunk.position.x * chunk_size + static_cast<int>(tile % chunk_size),
                    chunk.position.z * chunk_size + static_cast<int>(tile / chunk_size), chunk.position.y};
                if (terrain != TerrainId{} && (!content.TryGetTerrain(terrain) || !ValidPosition(position))) {
                    MemoryContext(diagnostics, memory.player, chunk.position);
                    return false;
                }
            }
            if (!observed) {
                MemoryContext(diagnostics, memory.player, chunk.position);
                return false;
            }
            for (std::size_t j = 0; j < chunk.objects.size(); ++j) {
                const auto& object = chunk.objects[j];
                if (chunk.terrain[object.tileIndex] == TerrainId{}
                    || object.appearance.kind == ObjectKind::None || !ValidObjectAppearance(object.appearance)
                    || (j != 0 && chunk.objects[j - 1].tileIndex >= object.tileIndex)) { return false; }
            }
        }
    }
    return Unique(players);
}

String EncodeMemory(const ContentCatalog& content, const MemoryChunk& chunk) {
    JsonWriter writer;
    auto* root = writer.Object();
    auto* palette = writer.Array();
    Vector<TerrainId> terrains{TerrainId{}};
    writer.Append(palette, yyjson_mut_null(writer.doc));
    for (auto terrain : chunk.terrain) {
        if (eastl::find(terrains.begin(), terrains.end(), terrain) == terrains.end()) {
            terrains.push_back(terrain);
            writer.Append(palette, yyjson_mut_strcpy(writer.doc, content.GetTerrain(terrain).id.c_str()));
        }
    }
    writer.Add(root, "palette", palette);
    auto* runs = writer.Array();
    for (std::size_t start = 0; start < chunk.terrain.size();) {
        std::size_t end = start + 1;
        while (end < chunk.terrain.size() && chunk.terrain[end] == chunk.terrain[start]) { ++end; }
        auto* run = writer.Array();
        const auto index = eastl::find(terrains.begin(), terrains.end(), chunk.terrain[start]) - terrains.begin();
        writer.Append(run, yyjson_mut_uint(writer.doc, static_cast<std::uint64_t>(index)));
        writer.Append(run, yyjson_mut_uint(writer.doc, end - start));
        writer.Append(runs, run);
        start = end;
    }
    writer.Add(root, "runs", runs);
    auto* objects = writer.Array();
    for (const auto& object : chunk.objects) {
        auto* value = writer.Object();
        writer.Integer(value, "tile", object.tileIndex);
        writer.Integer(value, "kind", static_cast<int>(object.appearance.kind));
        writer.Integer(value, "facing", static_cast<int>(object.appearance.facing));
        writer.Boolean(value, "active", object.appearance.active);
        writer.Boolean(value, "broken", object.appearance.broken);
        writer.Append(objects, value);
    }
    writer.Add(root, "objects", objects);
    return writer.Finish(root);
}

SaveResult<MemoryChunk> DecodeMemory(const ContentCatalog& content, const char* text,
    std::size_t length, ChunkCoord position, Diagnostics* diagnostics) {
    Document document;
    if (!document.Parse(text, length)) { return Failure(SaveError::InvalidData, "Invalid memory JSON", diagnostics); }
    auto* root = yyjson_doc_get_root(document.value);
    auto* palette = Get(root, "palette");
    auto* runs = Get(root, "runs");
    auto* objects = Get(root, "objects");
    if (!Fields(root, {"palette", "runs", "objects"}) || !yyjson_is_arr(palette)
        || yyjson_arr_size(palette) < 1 || yyjson_arr_size(palette) > 257
        || !yyjson_is_null(yyjson_arr_get(palette, 0))
        || !yyjson_is_arr(runs) || yyjson_arr_size(runs) > 256
        || !yyjson_is_arr(objects) || yyjson_arr_size(objects) > 256) {
        return Failure(SaveError::InvalidData, "Invalid memory palette, runs or objects", diagnostics);
    }
    Vector<TerrainId> terrains{TerrainId{}};
    for (std::size_t i = 1; i < yyjson_arr_size(palette); ++i) {
        auto* entry = yyjson_arr_get(palette, i);
        if (!yyjson_is_str(entry)) { return Failure(SaveError::InvalidData, "Invalid memory terrain", diagnostics); }
        const eastl::string_view id(yyjson_get_str(entry), yyjson_get_len(entry));
        const auto terrain = content.FindTerrain(id);
        if (terrain == TerrainId{}) {
            if (diagnostics) {
                String message = "Unknown remembered terrain: ";
                message.append(id.data(), id.size());
                diagnostics->Add(message.c_str());
            }
            return eastl::unexpected(SaveError::InvalidData);
        }
        if (eastl::find(terrains.begin(), terrains.end(), terrain) != terrains.end()) {
            return Failure(SaveError::InvalidData, "Duplicate memory palette terrain", diagnostics);
        }
        terrains.push_back(terrain);
    }
    MemoryChunk chunk;
    chunk.position = position;
    std::size_t offset = 0;
    yyjson_arr_iter iterator = yyjson_arr_iter_with(runs);
    while (auto* run = yyjson_arr_iter_next(&iterator)) {
        int index = 0;
        int count = 0;
        if (!yyjson_is_arr(run) || yyjson_arr_size(run) != 2
            || !Integer(yyjson_arr_get(run, 0), index) || index < 0
            || static_cast<std::size_t>(index) >= terrains.size()
            || !Integer(yyjson_arr_get(run, 1), count) || count <= 0
            || static_cast<std::size_t>(count) > chunk.terrain.size() - offset) {
            return Failure(SaveError::InvalidData, "Invalid memory run", diagnostics);
        }
        for (int i = 0; i < count; ++i) { chunk.terrain[offset++] = terrains[index]; }
    }
    if (offset != chunk.terrain.size()) { return Failure(SaveError::InvalidData, "Incomplete memory coverage", diagnostics); }
    iterator = yyjson_arr_iter_with(objects);
    while (auto* value = yyjson_arr_iter_next(&iterator)) {
        RememberedObject object;
        if (!Fields(value, {"tile", "kind", "facing", "active", "broken"})
            || !Byte(Get(value, "tile"), object.tileIndex) || !Appearance(value, object.appearance)
            || !yyjson_is_bool(Get(value, "active")) || !yyjson_is_bool(Get(value, "broken"))) {
            return Failure(SaveError::InvalidData, "Invalid remembered object", diagnostics);
        }
        object.appearance.active = yyjson_get_bool(Get(value, "active"));
        object.appearance.broken = yyjson_get_bool(Get(value, "broken"));
        chunk.objects.push_back(object);
    }
    return chunk;
}

void ItemDiagnostic(Diagnostics* diagnostics, PersistentId id, const char* field, const char* message,
    eastl::string_view detail = {}) {
    if (!diagnostics) { return; }
    DiagnosticFrame frame;
    frame.message = message;
    if (!detail.empty()) { frame.message += ": "; frame.message.append(detail.data(), detail.size()); }
    DiagnosticLocation location;
    char path[128];
    std::snprintf(path, sizeof(path), "items[%llu].%s", static_cast<unsigned long long>(id), field);
    location.path = path;
    frame.location = eastl::move(location);
    diagnostics->Add(eastl::move(frame));
}

bool ValidateChunk(const ContentCatalog& content, const ChunkArchive& chunk,
    PersistentId nextId, Vector<PersistentId>& ids, Diagnostics* diagnostics) {
    if (!ValidChunk(chunk.position) || nextId == 0) { return false; }
    for (std::size_t index = 0; index < chunk.tiles.size(); ++index) {
        if (!content.TryGetTerrain(chunk.tiles[index].terrain)) {
            if (diagnostics) {
                DiagnosticFrame frame;
                frame.message = "Invalid runtime terrain ID";
                DiagnosticLocation location;
                char path[128];
                std::snprintf(path, sizeof(path), "chunks[%d,%d,%d].tiles[%zu].terrain",
                    chunk.position.x, chunk.position.z, chunk.position.y, index);
                location.path = path;
                frame.location = eastl::move(location);
                diagnostics->Add(eastl::move(frame));
            }
            return false;
        }
    }
    Vector<PersistentId> localIds;
    const auto addId = [&localIds, nextId](PersistentId id) {
        if (id == 0 || id >= nextId) { return false; }
        localIds.push_back(id);
        return true;
    };
    for (std::size_t i = 0; i < chunk.actors.size(); ++i) {
        const auto& actor = chunk.actors[i];
        if (!ValidLightEmission(actor.light) || actor.speed == 0 || (actor.parked && !actor.player)
            || !addId(actor.id) || !ValidPosition(actor.position)
            || ChunkOf(actor.position) != chunk.position) { return false; }
        for (std::size_t j = 0; j < i; ++j) {
            if (!actor.parked && !chunk.actors[j].parked
                && actor.position == chunk.actors[j].position) { return false; }
        }
    }
    for (std::size_t i = 0; i < chunk.objects.size(); ++i) {
        const auto& object = chunk.objects[i];
        if (!ValidLightEmission(object.light) || object.kind == ObjectKind::None
            || !ValidObjectAppearance({object.kind, object.facing, object.enabled, object.broken})
            || object.updateInterval == 0 || object.remainingTicks > object.updateInterval
            || !addId(object.id) || !ValidPosition(object.position)
            || ChunkOf(object.position) != chunk.position) { return false; }
        for (std::size_t j = 0; j < i; ++j) {
            if (object.position == chunk.objects[j].position) { return false; }
        }
    }
    for (const auto& item : chunk.items) {
        if (!addId(item.id)) { return false; }
    }
    if (!Unique(localIds)) { return false; }
    ids.insert(ids.end(), localIds.begin(), localIds.end());
    return true;
}

String EncodeSnapshot(const SaveSnapshot& snapshot) {
    JsonWriter writer;
    auto* root = writer.Object();
    writer.Integer(root, "seed", snapshot.seed);
    writer.Integer(root, "generation_version", snapshot.generationVersion);
    writer.Counter(root, "next_id", snapshot.nextId);
    writer.Integer(root, "sky_light", snapshot.skyLight);
    auto* session = writer.Object();
    writer.Counter(session, "tick", snapshot.session.tick);
    writer.Integer(session, "rng_state", snapshot.session.rngState);
    writer.Counter(session, "player", snapshot.session.player);
    writer.Position(session, snapshot.session.playerPosition);
    writer.Integer(session, "active_radius", snapshot.session.activeRadius);
    writer.Integer(session, "resident_radius", snapshot.session.residentRadius);
    writer.Counter(session, "autosave_interval", snapshot.session.autosaveInterval);
    auto* pacing = writer.Object();
    writer.Integer(pacing, "ticks_per_second", snapshot.session.pacing.ticksPerSecond);
    writer.Boolean(pacing, "rate_limited", snapshot.session.pacing.rateLimited);
    writer.Boolean(pacing, "pause_when_idle", snapshot.session.pacing.pauseWhenIdle);
    writer.Add(session, "pacing", pacing);
    auto* players = writer.Array();
    for (const SavedPlayer& player : snapshot.session.players) {
        auto* entry = writer.Object();
        writer.Counter(entry, "actor", player.actor);
        writer.Position(entry, player.position);
        writer.Text(entry, "name", player.name.c_str());
        writer.Counter(entry, "reconnect_token", player.reconnectToken);
        writer.Append(players, entry);
    }
    writer.Add(session, "players", players);
    if (snapshot.session.circleResidency) {
        auto* circle = writer.Object();
        writer.Integer(circle, "radius_tiles", snapshot.session.circleResidency->radiusTiles);
        writer.Add(session, "circle_residency", circle);
    } else {
        writer.Add(session, "circle_residency", yyjson_mut_null(writer.doc));
    }
    writer.Add(root, "session", session);
    return writer.Finish(root);
}

SaveResult<SaveSnapshot> DecodeSnapshot(const char* text, std::size_t length,
    int formatVersion, Diagnostics* diagnostics) {
    Document document;
    SaveSnapshot snapshot;
    if (!document.Parse(text, length)) {
        return Failure(SaveError::InvalidData, "Invalid world_state JSON", diagnostics);
    }
    auto* root = yyjson_doc_get_root(document.value);
    if (!Fields(root, {"seed", "generation_version", "next_id", "sky_light", "session"})
        || !Byte(Get(root, "sky_light"), snapshot.skyLight)
        || !Unsigned32(Get(root, "seed"), snapshot.seed)
        || !Unsigned32(Get(root, "generation_version"), snapshot.generationVersion)
        || !Counter(Get(root, "next_id"), snapshot.nextId)) {
        return Failure(SaveError::InvalidData, "Invalid world_state fields", diagnostics);
    }
    if (snapshot.generationVersion < 1 || snapshot.generationVersion > 3) {
        return Failure(SaveError::UnsupportedVersion, "Unsupported generation version", diagnostics);
    }
    auto* session = Get(root, "session");
    auto& state = snapshot.session;
    auto* pacing = Get(session, "pacing");
    const bool sessionFields = formatVersion == 7
        ? Fields(session, {"tick", "rng_state", "player", "position", "active_radius",
            "resident_radius", "autosave_interval", "pacing", "players"})
        : Fields(session, {"tick", "rng_state", "player", "position", "active_radius",
            "resident_radius", "autosave_interval", "pacing", "players", "circle_residency"});
    if (!sessionFields
        || !Fields(pacing, {"ticks_per_second", "rate_limited", "pause_when_idle"})
        || !Unsigned32(Get(pacing, "ticks_per_second"), state.pacing.ticksPerSecond)
        || !yyjson_is_bool(Get(pacing, "rate_limited"))
        || !yyjson_is_bool(Get(pacing, "pause_when_idle"))
        || !Counter(Get(session, "tick"), state.tick)
        || !Unsigned32(Get(session, "rng_state"), state.rngState)
        || !Counter(Get(session, "player"), state.player)
        || !Position(Get(session, "position"), state.playerPosition)
        || !Integer(Get(session, "active_radius"), state.activeRadius)
        || !Integer(Get(session, "resident_radius"), state.residentRadius)
        || !Counter(Get(session, "autosave_interval"), state.autosaveInterval)) {
        return Failure(SaveError::InvalidData, "Invalid session or next_id", diagnostics);
    }
    if (formatVersion == 8) {
        auto* circle = Get(session, "circle_residency");
        if (!yyjson_is_null(circle)) {
            CircleResidency residency;
            if (!Fields(circle, {"radius_tiles"})
                || !Integer(Get(circle, "radius_tiles"), residency.radiusTiles)) {
                return Failure(SaveError::InvalidData, "Invalid circle residency", diagnostics);
            }
            state.circleResidency = residency;
        }
    }
    auto* players = Get(session, "players");
    if (!yyjson_is_arr(players) || yyjson_arr_size(players) > 32) {
        return Failure(SaveError::InvalidData, "Invalid player roster", diagnostics);
    }
    yyjson_arr_iter playerIterator = yyjson_arr_iter_with(players);
    while (auto* entry = yyjson_arr_iter_next(&playerIterator)) {
        SavedPlayer player;
        auto* name = Get(entry, "name");
        if (!Fields(entry, {"actor", "position", "name", "reconnect_token"})
            || !Counter(Get(entry, "actor"), player.actor)
            || !Position(Get(entry, "position"), player.position)
            || !Counter(Get(entry, "reconnect_token"), player.reconnectToken)
            || !yyjson_is_str(name) || yyjson_get_len(name) == 0 || yyjson_get_len(name) > 32
            || std::memchr(yyjson_get_str(name), '\0', yyjson_get_len(name))) {
            return Failure(SaveError::InvalidData, "Invalid player roster entry", diagnostics);
        }
        player.name.assign(yyjson_get_str(name), yyjson_get_len(name));
        state.players.push_back(eastl::move(player));
    }
    if (!ValidSnapshot(snapshot)) {
        return Failure(SaveError::InvalidData, "Invalid player roster or session", diagnostics);
    }
    state.pacing.rateLimited = yyjson_get_bool(Get(pacing, "rate_limited"));
    state.pacing.pauseWhenIdle = yyjson_get_bool(Get(pacing, "pause_when_idle"));
    return snapshot;
}

String EncodeChunk(const ContentCatalog& content, const ChunkArchive& chunk) {
    JsonWriter writer;
    auto* root = writer.Object();
    auto* tiles = writer.Array();
    for (Tile tile : chunk.tiles) {
        auto* value = writer.Object();
        writer.Text(value, "terrain", content.GetTerrain(tile.terrain).id.c_str());
        writer.Integer(value, "damage", tile.damage);
        writer.Append(tiles, value);
    }
    writer.Add(root, "tiles", tiles);
    auto* actors = writer.Array();
    Vector<yyjson_mut_val*> inventories;
    for (const auto& actor : chunk.actors) {
        auto* value = writer.Object();
        writer.Counter(value, "id", actor.id);
        writer.Position(value, actor.position);
        writer.Boolean(value, "player", actor.player);
        writer.Boolean(value, "parked", actor.parked);
        writer.Counter(value, "turns", actor.turns);
        writer.Integer(value, "speed", actor.speed);
        writer.Counter(value, "remaining_ticks", actor.remainingTicks);
        writer.Counter(value, "carry_capacity_g", actor.carryCapacityGrams);
        EncodeLight(writer, value, actor.light);
        auto* inventory = writer.Array();
        inventories.push_back(inventory);
        writer.Add(value, "inventory", inventory);
        writer.Append(actors, value);
    }
    writer.Add(root, "actors", actors);
    auto* objects = writer.Array();
    Vector<yyjson_mut_val*> storage;
    for (const auto& object : chunk.objects) {
        auto* value = writer.Object();
        writer.Counter(value, "id", object.id);
        writer.Position(value, object.position);
        writer.Boolean(value, "enabled", object.enabled);
        writer.Integer(value, "kind", static_cast<int>(object.kind));
        writer.Integer(value, "facing", static_cast<int>(object.facing));
        writer.Boolean(value, "broken", object.broken);
        EncodeLight(writer, value, object.light);
        writer.Counter(value, "updates", object.updates);
        writer.Integer(value, "update_interval", object.updateInterval);
        writer.Counter(value, "remaining_ticks", object.remainingTicks);
        auto* contents = writer.Array();
        storage.push_back(contents);
        writer.Add(value, "contents", contents);
        writer.Append(objects, value);
    }
    writer.Add(root, "objects", objects);
    auto* ground = writer.Array();
    writer.Add(root, "ground_items", ground);
    Vector<yyjson_mut_val*> contents;
    for (std::size_t i = 0; i < chunk.items.size(); ++i) {
        const auto& item = chunk.items[i];
        auto* value = writer.Object();
        writer.Counter(value, "id", item.id);
        const auto& definition = content.GetItem(item.type);
        writer.Text(value, "type", definition.id.c_str());
        writer.Add(value, "state", EncodeValue(writer, item.state));
        const char* installation = "";
        if (item.installation.value) {
            for (std::size_t j = 0; j < i; ++j) {
                if (chunk.items[j].id == item.owner) {
                    installation = TryGetItemCapability(content.GetItem(chunk.items[j].type), item.installation)->id.c_str();
                    break;
                }
            }
        }
        writer.Text(value, "installation", installation);
        auto* children = writer.Array();
        writer.Add(value, "contents", children);
        contents.push_back(children);
        yyjson_mut_val* destination = nullptr;
        switch (item.location) {
        case LocationKind::ground:
            writer.Position(value, item.position);
            destination = ground;
            break;
        case LocationKind::actor:
            for (std::size_t j = 0; j < chunk.actors.size(); ++j) {
                if (chunk.actors[j].id == item.owner) { destination = inventories[j]; break; }
            }
            break;
        case LocationKind::object:
            for (std::size_t j = 0; j < chunk.objects.size(); ++j) {
                if (chunk.objects[j].id == item.owner) { destination = storage[j]; break; }
            }
            break;
        case LocationKind::item:
            for (std::size_t j = 0; j < i; ++j) {
                if (chunk.items[j].id == item.owner) { destination = contents[j]; break; }
            }
            break;
        }
        writer.Append(destination, value); // Ownership was validated before encoding.
    }
    return writer.Finish(root);
}

SaveResult<void> DecodeItems(const ContentCatalog& content, yyjson_val* array, LocationKind location,
    PersistentId owner, unsigned depth, ChunkArchive& chunk, Diagnostics* diagnostics,
    const ItemDefinition* parent = nullptr) {
    if (!yyjson_is_arr(array) || (yyjson_arr_size(array) != 0 && depth > maxItemDepth)) {
        return Failure(SaveError::InvalidData, "Invalid item array or excessive item depth", diagnostics);
    }
    yyjson_arr_iter iterator = yyjson_arr_iter_with(array);
    while (auto* value = yyjson_arr_iter_next(&iterator)) {
        const bool ground = location == LocationKind::ground;
        if (ground ? !Fields(value, {"id", "type", "state", "installation", "contents", "position"})
                   : !Fields(value, {"id", "type", "state", "installation", "contents"})) {
            return Failure(SaveError::InvalidData, "Invalid item fields (type, state, installation and contents required)", diagnostics);
        }
        SavedItem item{};
        item.location = location;
        item.owner = owner;
        if (!Counter(Get(value, "id"), item.id)) { return Failure(SaveError::InvalidData, "Invalid item ID", diagnostics); }
        auto* type = Get(value, "type");
        if (yyjson_is_str(type)) {
            item.type = content.FindItem({yyjson_get_str(type), yyjson_get_len(type)});
        }
        const auto* definition = content.TryGetItem(item.type);
        if (!definition) {
            ItemDiagnostic(diagnostics, item.id, "type", "Unknown or invalid stable item type",
                yyjson_is_str(type) ? eastl::string_view(yyjson_get_str(type), yyjson_get_len(type)) : eastl::string_view{});
            return eastl::unexpected(SaveError::InvalidData);
        }
        auto* installation = Get(value, "installation");
        if (!yyjson_is_str(installation)) {
            ItemDiagnostic(diagnostics, item.id, "installation", "Installation must be a stable parent capability name");
            return eastl::unexpected(SaveError::InvalidData);
        }
        const eastl::string_view mount(yyjson_get_str(installation), yyjson_get_len(installation));
        if (!mount.empty()) {
            item.installation = parent ? FindItemCapability(*parent, mount) : ItemCapabilityId{};
            const auto* behavior = parent ? ItemBehaviorConfig(*parent, item.installation) : nullptr;
            if (!behavior || !behavior->AcceptsInstallation(*definition)) {
                ItemDiagnostic(diagnostics, item.id, "installation", "Unknown or incompatible parent installation capability", mount);
                return eastl::unexpected(SaveError::InvalidData);
            }
        }
        auto tree = DecodeValue(Get(value, "state"));
        if (!tree) {
            ItemDiagnostic(diagnostics, item.id, "state", "Invalid state value, duplicate key or excessive depth");
            return eastl::unexpected(SaveError::InvalidData);
        }
        item.state = eastl::move(*tree);
        if (ground && !Position(Get(value, "position"), item.position)) {
            return Failure(SaveError::InvalidData, "Invalid ground item position", diagnostics);
        }
        auto* children = Get(value, "contents");
        const auto id = item.id;
        chunk.items.push_back(eastl::move(item));
        auto decoded = DecodeItems(content, children, LocationKind::item, id, depth + 1, chunk, diagnostics, definition);
        if (!decoded) { return decoded; }
    }
    return {};
}

SaveResult<ChunkArchive> DecodeChunk(const ContentCatalog& content, const char* text, std::size_t length,
    ChunkCoord position, PersistentId nextId, Diagnostics* diagnostics) {
    Document document;
    if (!document.Parse(text, length)) {
        return Failure(SaveError::InvalidData, "Invalid chunk JSON", diagnostics);
    }
    auto* root = yyjson_doc_get_root(document.value);
    if (!Fields(root, {"tiles", "actors", "objects", "ground_items"})) {
        return Failure(SaveError::InvalidData, "Invalid chunk fields", diagnostics);
    }
    ChunkArchive chunk{};
    chunk.position = position;
    auto* tiles = Get(root, "tiles");
    if (!yyjson_is_arr(tiles) || yyjson_arr_size(tiles) != chunk.tiles.size()) {
        return Failure(SaveError::InvalidData, "Chunk must have exactly 256 tiles", diagnostics);
    }
    yyjson_arr_iter tileIterator = yyjson_arr_iter_with(tiles);
    std::size_t index = 0;
    while (auto* value = yyjson_arr_iter_next(&tileIterator)) {
        Tile tile;
        int damage = 0;
        if (!Fields(value, {"terrain", "damage"}) || !Integer(Get(value, "damage"), damage)
            || damage < 0 || damage > 255) {
            return Failure(SaveError::InvalidData, "Invalid tile", diagnostics);
        }
        auto* terrain = Get(value, "terrain");
        if (!yyjson_is_str(terrain)) {
            return Failure(SaveError::InvalidData, "Tile terrain must be a string", diagnostics);
        }
        const eastl::string_view id(yyjson_get_str(terrain), yyjson_get_len(terrain));
        tile.terrain = content.FindTerrain(id);
        if (tile.terrain == TerrainId{}) {
            TerrainDiagnostic(diagnostics, position, index, id);
            return eastl::unexpected(SaveError::InvalidData);
        }
        tile.damage = static_cast<std::uint8_t>(damage);
        chunk.tiles[index++] = tile;
    }
    auto* actors = Get(root, "actors");
    auto* objects = Get(root, "objects");
    if (!yyjson_is_arr(actors) || !yyjson_is_arr(objects)) {
        return Failure(SaveError::InvalidData, "Invalid entity arrays", diagnostics);
    }
    yyjson_arr_iter actorIterator = yyjson_arr_iter_with(actors);
    while (auto* value = yyjson_arr_iter_next(&actorIterator)) {
        SavedActor actor{};
        if (!Fields(value, {"id", "position", "player", "parked", "turns", "inventory", "speed", "remaining_ticks", "carry_capacity_g", "light"})
            || !Light(Get(value, "light"), actor.light)
            || !Counter(Get(value, "carry_capacity_g"), actor.carryCapacityGrams)
            || !Unsigned32(Get(value, "speed"), actor.speed)
            || !Counter(Get(value, "remaining_ticks"), actor.remainingTicks)
            || !Counter(Get(value, "id"), actor.id) || !Position(Get(value, "position"), actor.position)
            || !yyjson_is_bool(Get(value, "player")) || !yyjson_is_bool(Get(value, "parked"))
            || !Counter(Get(value, "turns"), actor.turns)) {
            return Failure(SaveError::InvalidData, "Invalid actor", diagnostics);
        }
        actor.player = yyjson_get_bool(Get(value, "player"));
        actor.parked = yyjson_get_bool(Get(value, "parked"));
        chunk.actors.push_back(actor);
        auto decoded = DecodeItems(content, Get(value, "inventory"), LocationKind::actor, actor.id, 1, chunk, diagnostics);
        if (!decoded) { return eastl::unexpected(decoded.error()); }
    }
    yyjson_arr_iter objectIterator = yyjson_arr_iter_with(objects);
    while (auto* value = yyjson_arr_iter_next(&objectIterator)) {
        SavedObject object{};
        ObjectAppearance appearance;
        if (!Fields(value, {"id", "position", "enabled", "updates", "contents", "update_interval", "remaining_ticks", "kind", "facing", "broken", "light"})
            || !Appearance(value, appearance) || !Light(Get(value, "light"), object.light)
            || !yyjson_is_bool(Get(value, "broken"))
            || !Unsigned32(Get(value, "update_interval"), object.updateInterval)
            || !Counter(Get(value, "remaining_ticks"), object.remainingTicks)
            || !Counter(Get(value, "id"), object.id) || !Position(Get(value, "position"), object.position)
            || !yyjson_is_bool(Get(value, "enabled")) || !Counter(Get(value, "updates"), object.updates)) {
            return Failure(SaveError::InvalidData, "Invalid object", diagnostics);
        }
        object.enabled = yyjson_get_bool(Get(value, "enabled"));
        object.kind = appearance.kind;
        object.facing = appearance.facing;
        object.broken = yyjson_get_bool(Get(value, "broken"));
        chunk.objects.push_back(object);
        auto decoded = DecodeItems(content, Get(value, "contents"), LocationKind::object, object.id, 1, chunk, diagnostics);
        if (!decoded) { return eastl::unexpected(decoded.error()); }
    }
    Vector<PersistentId> ids;
    auto decoded = DecodeItems(content, Get(root, "ground_items"), LocationKind::ground, 0, 1, chunk, diagnostics);
    if (!decoded) { return eastl::unexpected(decoded.error()); }
    const auto validated = ValidateItemSnapshot(content, chunk, diagnostics);
    if (!validated) {
        return eastl::unexpected(validated.error() == StateError::UnsupportedVersion
            ? SaveError::UnsupportedVersion : SaveError::InvalidData);
    }
    if (!ValidateChunk(content, chunk, nextId, ids, diagnostics)) {
        return Failure(SaveError::InvalidData, "Invalid chunk ownership, IDs, positions or item depth", diagnostics);
    }
    return chunk;
}

bool TextColumn(sqlite3_stmt* statement, int column, const char*& text, std::size_t& length) {
    if (sqlite3_column_type(statement, column) != SQLITE_TEXT) { return false; }
    text = reinterpret_cast<const char*>(sqlite3_column_text(statement, column));
    if (!text) { std::abort(); }
    length = static_cast<std::size_t>(sqlite3_column_bytes(statement, column));
    return true;
}

SaveResult<void> Execute(sqlite3* db, const char* sql, Diagnostics* diagnostics) {
    const int code = sqlite3_exec(db, sql, nullptr, nullptr, nullptr);
    if (code != SQLITE_OK) { return Failure(SqlError(code), sqlite3_errmsg(db), diagnostics); }
    return {};
}

SaveResult<void> Connect(Connection& connection, const char* path, Diagnostics* diagnostics) {
    // Never allow SQLite's special temporary/in-memory filenames or URI handling.
    if (!path || !*path || std::strcmp(path, ":memory:") == 0) {
        return Failure(SaveError::Io, "A filesystem save path is required", diagnostics);
    }
    const int code = sqlite3_open_v2(path, &connection.value, SQLITE_OPEN_READWRITE, nullptr);
    if (code != SQLITE_OK) { return Failure(SqlError(code), "Cannot open save database", diagnostics); }
    sqlite3_extended_result_codes(connection.value, 1);
    return Execute(connection.value, "PRAGMA journal_mode=DELETE; PRAGMA synchronous=FULL;", diagnostics);
}

// Only Create owns cleanup rights, and only until its first checkpoint commits.
// Check file identity as well as the name before removing an abandoned save.
struct CreatedFile {
#ifdef _WIN32
    Vector<wchar_t> path;
    BY_HANDLE_FILE_INFORMATION identity{};
#else
    String path;
    struct stat identity{};
#endif

    void Keep() { path.clear(); }

    ~CreatedFile() {
        if (path.empty()) { return; }
#ifdef _WIN32
        const HANDLE file = CreateFileW(path.data(), DELETE | FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (file == INVALID_HANDLE_VALUE) { return; }
        BY_HANDLE_FILE_INFORMATION current{};
        if (GetFileInformationByHandle(file, &current)
            && current.dwVolumeSerialNumber == identity.dwVolumeSerialNumber
            && current.nFileIndexHigh == identity.nFileIndexHigh
            && current.nFileIndexLow == identity.nFileIndexLow) {
            // Delete this opened file, rather than resolving its path again.
            FILE_DISPOSITION_INFO disposition{TRUE};
            SetFileInformationByHandle(file, FileDispositionInfo, &disposition, sizeof(disposition));
        }
        CloseHandle(file);
#else
        struct stat current{};
        if (::lstat(path.c_str(), &current) == 0
            && current.st_dev == identity.st_dev && current.st_ino == identity.st_ino) {
            ::unlink(path.c_str());
        }
#endif
    }
};

SaveResult<void> ReservePath(const char* path, CreatedFile& createdFile, Diagnostics* diagnostics) {
    if (!path || !*path || std::strcmp(path, ":memory:") == 0) {
        return Failure(SaveError::Io, "A filesystem save path is required", diagnostics);
    }
    // Exclusive OS creation refuses existing paths, including empty files and
    // symlinks. SQLite subsequently opens this reserved file without CREATE.
#ifdef _WIN32
    // sqlite3_open_v2 takes UTF-8; use the same path interpretation for reservation.
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, nullptr, 0);
    if (length == 0) { return Failure(SaveError::Io, "Invalid UTF-8 save path", diagnostics); }
    Vector<wchar_t> widePath(static_cast<std::size_t>(length));
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, widePath.data(), length) == 0) {
        return Failure(SaveError::Io, "Invalid UTF-8 save path", diagnostics);
    }
    const HANDLE file = CreateFileW(widePath.data(), GENERIC_READ | GENERIC_WRITE | DELETE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        const bool exists = error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS
            || (error == ERROR_ACCESS_DENIED && GetFileAttributesW(widePath.data()) != INVALID_FILE_ATTRIBUTES);
        return Failure(exists ? SaveError::AlreadyExists : SaveError::Io,
            "Cannot exclusively create save path", diagnostics);
    }
    if (!GetFileInformationByHandle(file, &createdFile.identity)) {
        FILE_DISPOSITION_INFO disposition{TRUE};
        SetFileInformationByHandle(file, FileDispositionInfo, &disposition, sizeof(disposition));
        CloseHandle(file);
        return Failure(SaveError::Io, "Cannot identify newly created save file", diagnostics);
    }
    createdFile.path = eastl::move(widePath);
    CloseHandle(file);
#else
    const int descriptor = ::open(path, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (descriptor < 0) {
        return Failure(errno == EEXIST ? SaveError::AlreadyExists : SaveError::Io,
            "Cannot exclusively create save path", diagnostics);
    }
    if (::fstat(descriptor, &createdFile.identity) != 0) {
        ::close(descriptor);
        ::unlink(path);
        return Failure(SaveError::Io, "Cannot identify newly created save file", diagnostics);
    }
    createdFile.path = path;
    ::close(descriptor);
#endif
    return {};
}

// Inspect column names/types as well as version, so a foreign/extended schema
// cannot accidentally be treated as the current version merely by supplying metadata.
SaveResult<void> CheckTable(sqlite3* db, const char* pragma,
    std::initializer_list<const char*> names, std::initializer_list<const char*> types,
    std::initializer_list<int> required, std::initializer_list<int> primaryKeys,
    Diagnostics* diagnostics) {
    Statement statement;
    int code = statement.Prepare(db, pragma);
    if (code != SQLITE_OK) { return Failure(SqlError(code), sqlite3_errmsg(db), diagnostics); }
    auto type = types.begin();
    auto notNull = required.begin();
    auto primaryKey = primaryKeys.begin();
    for (const char* name : names) {
        code = sqlite3_step(statement.value);
        if (code != SQLITE_ROW) {
            return Failure(code == SQLITE_DONE ? SaveError::InvalidData : SqlError(code),
                "Missing save schema column", diagnostics);
        }
        const char* columnName = reinterpret_cast<const char*>(sqlite3_column_text(statement.value, 1));
        const char* columnType = reinterpret_cast<const char*>(sqlite3_column_text(statement.value, 2));
        if (!columnName || !columnType || std::strcmp(columnName, name) != 0
            || std::strcmp(columnType, *type++) != 0
            || sqlite3_column_int(statement.value, 3) != *notNull++
            || sqlite3_column_int(statement.value, 5) != *primaryKey++) {
            return Failure(SaveError::InvalidData, "Unknown save schema", diagnostics);
        }
    }
    code = sqlite3_step(statement.value);
    if (code != SQLITE_DONE) {
        return Failure(code == SQLITE_ROW ? SaveError::InvalidData : SqlError(code),
            "Unexpected save schema column", diagnostics);
    }
    return {};
}

SaveResult<int> ReadFormatVersion(sqlite3* db, Diagnostics* diagnostics) {
    Statement metadata;
    int code = metadata.Prepare(db, "SELECT singleton,format_version,game_version FROM metadata");
    if (code != SQLITE_OK) { return Failure(SqlError(code), sqlite3_errmsg(db), diagnostics); }
    code = sqlite3_step(metadata.value);
    if (code != SQLITE_ROW) {
        return Failure(code == SQLITE_DONE ? SaveError::InvalidData : SqlError(code),
            "Missing save metadata", diagnostics);
    }
    if (sqlite3_column_type(metadata.value, 0) != SQLITE_INTEGER
        || sqlite3_column_int64(metadata.value, 0) != 1
        || sqlite3_column_type(metadata.value, 1) != SQLITE_INTEGER
        || sqlite3_column_type(metadata.value, 2) != SQLITE_TEXT) {
        return Failure(SaveError::InvalidData, "Invalid save metadata", diagnostics);
    }
    const auto version = sqlite3_column_int64(metadata.value, 1);
    if (version != 7 && version != save_format_version) {
        return Failure(SaveError::UnsupportedVersion, "Unsupported save format version", diagnostics);
    }
    code = sqlite3_step(metadata.value);
    if (code != SQLITE_DONE) {
        return Failure(code == SQLITE_ROW ? SaveError::InvalidData : SqlError(code),
            "Metadata must contain one singleton", diagnostics);
    }
    return static_cast<int>(version);
}

SaveResult<int> CheckSchema(sqlite3* db, Diagnostics* diagnostics) {
    auto result = CheckTable(db, "PRAGMA table_info(metadata)",
        {"singleton", "format_version", "game_version"}, {"INTEGER", "INTEGER", "TEXT"},
        {0, 1, 1}, {1, 0, 0}, diagnostics);
    if (!result) { return eastl::unexpected(result.error()); }
    auto version = ReadFormatVersion(db, diagnostics);
    if (!version) { return version; }
    result = CheckTable(db, "PRAGMA table_info(world_state)",
        {"singleton", "payload"}, {"INTEGER", "TEXT"}, {0, 1}, {1, 0}, diagnostics);
    if (!result) { return eastl::unexpected(result.error()); }
    result = CheckTable(db, "PRAGMA table_info(chunks)",
        {"x", "z", "y", "payload"}, {"INTEGER", "INTEGER", "INTEGER", "TEXT"},
        {1, 1, 1, 1}, {1, 2, 3, 0}, diagnostics);
    if (!result) { return eastl::unexpected(result.error()); }
    result = CheckTable(db, "PRAGMA table_info(player_memories)",
        {"player_id", "x", "z", "y", "payload"}, {"TEXT", "INTEGER", "INTEGER", "INTEGER", "TEXT"},
        {1, 1, 1, 1, 1}, {1, 2, 3, 4, 0}, diagnostics);
    if (!result) { return eastl::unexpected(result.error()); }
    Statement tables;
    int code = tables.Prepare(db,
        "SELECT count(*) FROM sqlite_schema WHERE name NOT LIKE 'sqlite_%' AND "
        "(type != 'table' OR name NOT IN ('metadata','world_state','chunks','player_memories'))");
    if (code != SQLITE_OK) { return Failure(SqlError(code), sqlite3_errmsg(db), diagnostics); }
    code = sqlite3_step(tables.value);
    if (code != SQLITE_ROW) { return Failure(SqlError(code), sqlite3_errmsg(db), diagnostics); }
    if (sqlite3_column_int64(tables.value, 0) != 0) {
        return Failure(SaveError::InvalidData, "Unknown save schema objects", diagnostics);
    }
    return version;
}

SaveResult<void> ReadMemories(const ContentCatalog& content, sqlite3* db,
    SaveSnapshot& snapshot, Diagnostics* diagnostics) {
    Statement statement;
    int code = statement.Prepare(db, "SELECT player_id,x,z,y,payload FROM player_memories ORDER BY player_id,x,z,y");
    if (code != SQLITE_OK) { return Failure(SqlError(code), sqlite3_errmsg(db), diagnostics); }
    while ((code = sqlite3_step(statement.value)) == SQLITE_ROW) {
        const char* text = nullptr;
        std::size_t length = 0;
        PersistentId player = 0;
        if (!TextColumn(statement.value, 0, text, length)) {
            return Failure(SaveError::InvalidData, "Memory player ID must be canonical text", diagnostics);
        }
        const auto converted = std::from_chars(text, text + length, player);
        if (length == 0 || (length > 1 && text[0] == '0') || text[0] < '0' || text[0] > '9'
            || converted.ec != std::errc{} || converted.ptr != text + length
            || player == 0 || player >= snapshot.nextId) {
            return Failure(SaveError::InvalidData, "Invalid memory player ID", diagnostics);
        }
        ChunkCoord position{};
        int* coordinates[] = {&position.x, &position.z, &position.y};
        for (int i = 0; i < 3; ++i) {
            const auto number = sqlite3_column_int64(statement.value, i + 1);
            if (sqlite3_column_type(statement.value, i + 1) != SQLITE_INTEGER || number < INT_MIN || number > INT_MAX) {
                return Failure(SaveError::InvalidData, "Invalid memory chunk coordinate", diagnostics);
            }
            *coordinates[i] = static_cast<int>(number);
        }
        if (!ValidChunk(position) || !TextColumn(statement.value, 4, text, length)) {
            return Failure(SaveError::InvalidData, "Invalid memory row", diagnostics);
        }
        auto chunk = DecodeMemory(content, text, length, position, diagnostics);
        if (!chunk) {
            MemoryContext(diagnostics, player, position);
            return eastl::unexpected(chunk.error());
        }
        if (snapshot.memories.empty() || snapshot.memories.back().player != player) {
            snapshot.memories.push_back(PlayerMemory{player, {}});
        }
        snapshot.memories.back().chunks.push_back(eastl::move(*chunk));
    }
    if (code != SQLITE_DONE) { return Failure(SqlError(code), sqlite3_errmsg(db), diagnostics); }
    eastl::sort(snapshot.memories.begin(), snapshot.memories.end(),
        [](const PlayerMemory& a, const PlayerMemory& b) { return a.player < b.player; });
    if (!ValidMemories(content, snapshot, diagnostics)) {
        return Failure(SaveError::InvalidData, "Invalid memory ordering, duplicate entries or unseen objects", diagnostics);
    }
    return {};
}

SaveResult<SaveSnapshot> ReadSnapshot(const ContentCatalog& content, sqlite3* db,
    int formatVersion, Diagnostics* diagnostics) {
    Statement statement;
    int code = statement.Prepare(db, "SELECT singleton,payload FROM world_state");
    if (code != SQLITE_OK) { return Failure(SqlError(code), sqlite3_errmsg(db), diagnostics); }
    code = sqlite3_step(statement.value);
    if (code != SQLITE_ROW) {
        return Failure(code == SQLITE_DONE ? SaveError::InvalidData : SqlError(code),
            "Missing world_state", diagnostics);
    }
    const char* text = nullptr;
    std::size_t length = 0;
    if (sqlite3_column_type(statement.value, 0) != SQLITE_INTEGER
        || sqlite3_column_int64(statement.value, 0) != 1
        || !TextColumn(statement.value, 1, text, length)) {
        return Failure(SaveError::InvalidData, "Invalid world_state row", diagnostics);
    }
    auto snapshot = DecodeSnapshot(text, length, formatVersion, diagnostics);
    if (!snapshot) { return snapshot; }
    code = sqlite3_step(statement.value);
    if (code != SQLITE_DONE) {
        return Failure(code == SQLITE_ROW ? SaveError::InvalidData : SqlError(code),
            "world_state must contain one singleton", diagnostics);
    }
    auto memories = ReadMemories(content, db, *snapshot, diagnostics);
    if (!memories) { return eastl::unexpected(memories.error()); }
    return snapshot;
}

SaveResult<void> ValidateDatabase(const ContentCatalog& content, sqlite3* db,
    const SaveSnapshot& snapshot, Diagnostics* diagnostics) {
    Statement statement;
    int code = statement.Prepare(db, "SELECT x,z,y,payload FROM chunks ORDER BY x,z,y");
    if (code != SQLITE_OK) { return Failure(SqlError(code), sqlite3_errmsg(db), diagnostics); }
    Vector<PersistentId> ids;
    bool playerFound = snapshot.session.player == 0;
    Vector<PersistentId> rosterFound;
    Vector<PersistentId> memoryOwners;
    const bool authoritative = snapshot.session.player != 0 || !snapshot.session.players.empty();
    bool havePrevious = false;
    ChunkCoord previous{};
    while ((code = sqlite3_step(statement.value)) == SQLITE_ROW) {
        if (sqlite3_column_type(statement.value, 0) != SQLITE_INTEGER
            || sqlite3_column_type(statement.value, 1) != SQLITE_INTEGER
            || sqlite3_column_type(statement.value, 2) != SQLITE_INTEGER) {
            return Failure(SaveError::InvalidData, "Invalid chunk coordinates", diagnostics);
        }
        const auto x = sqlite3_column_int64(statement.value, 0);
        const auto z = sqlite3_column_int64(statement.value, 1);
        const auto y = sqlite3_column_int64(statement.value, 2);
        constexpr int limit = world_coordinate_limit / chunk_size;
        if (x < -limit || x > limit || z < -limit || z > limit
            || y < world_min_y || y > world_max_y) {
            return Failure(SaveError::InvalidData, "Chunk coordinates out of range", diagnostics);
        }
        const ChunkCoord position{static_cast<int>(x), static_cast<int>(z), static_cast<int>(y)};
        if (havePrevious && previous == position) {
            return Failure(SaveError::InvalidData, "Duplicate chunk coordinates", diagnostics);
        }
        previous = position;
        havePrevious = true;
        const char* text = nullptr;
        std::size_t length = 0;
        if (!TextColumn(statement.value, 3, text, length)) {
            return Failure(SaveError::InvalidData, "Chunk payload must be text", diagnostics);
        }
        auto chunk = DecodeChunk(content, text, length, position, snapshot.nextId, diagnostics);
        if (!chunk) {
            ChunkContext(diagnostics, position, "Validating saved chunk");
            return eastl::unexpected(chunk.error());
        }
        for (const auto& actor : chunk->actors) {
            ids.push_back(actor.id);
            if (actor.player) { memoryOwners.push_back(actor.id); }
            if (actor.id == snapshot.session.player && actor.player
                && !actor.parked && actor.position == snapshot.session.playerPosition) {
                playerFound = true;
            }
            if (actor.player && authoritative
                && actor.id != snapshot.session.player
                && eastl::none_of(snapshot.session.players.begin(), snapshot.session.players.end(),
                    [&actor](const SavedPlayer& player) { return player.actor == actor.id; })) {
                return Failure(SaveError::InvalidData, "Player actor missing from roster", diagnostics);
            }
            for (const SavedPlayer& player : snapshot.session.players) {
                if (actor.id == player.actor && actor.player && actor.position == player.position
                    && (actor.id != snapshot.session.player || !actor.parked)) {
                    rosterFound.push_back(player.actor);
                }
            }
        }
        for (const auto& object : chunk->objects) { ids.push_back(object.id); }
        for (const auto& item : chunk->items) { ids.push_back(item.id); }
    }
    if (code != SQLITE_DONE) { return Failure(SqlError(code), sqlite3_errmsg(db), diagnostics); }
    if (!Unique(ids) || !playerFound || rosterFound.size() != snapshot.session.players.size()
        || !Unique(rosterFound)) {
        return Failure(SaveError::InvalidData, "Duplicate persistent ID or missing session player", diagnostics);
    }
    for (const auto& memory : snapshot.memories) {
        if (eastl::find(memoryOwners.begin(), memoryOwners.end(), memory.player) == memoryOwners.end()) {
            return Failure(SaveError::InvalidData, "Memory owner is not a durable player actor", diagnostics);
        }
    }
    return {};
}

int BindText(sqlite3_stmt* statement, int index, const String& text) {
    return sqlite3_bind_text64(statement, index, text.data(), text.size(), SQLITE_TRANSIENT, SQLITE_UTF8);
}

} // namespace

String EncodeDataObject(const DataObject& value) {
    JsonWriter writer;
    return writer.Finish(EncodeValue(writer, value));
}
DataResult<DataObject> DecodeDataObject(eastl::string_view text, Diagnostics* diagnostics) {
    Document document;
    if (!document.Parse(text.data(), text.size())) {
        if (diagnostics) { diagnostics->Add("Invalid structured value JSON"); }
        return eastl::unexpected(DataError::InvalidData);
    }
    auto result = DecodeValue(yyjson_doc_get_root(document.value));
    if (!result && diagnostics) { diagnostics->Add("Invalid structured value, duplicate key or excessive depth"); }
    return result;
}

class SaveDatabase::Impl {
public:
    // Member order closes SQLite (including its journal) before file cleanup.
    CreatedFile createdFile;
    Connection connection;
    bool transaction = false;
    int formatVersion = save_format_version; // Durable version; changed only after commit.
    bool snapshotWritten = false;
    Vector<PersistentId> emptyMemoryOwners;

    void Rollback() {
        snapshotWritten = false;
        emptyMemoryOwners.clear();
        if (transaction) {
            const int code = sqlite3_exec(connection.value, "ROLLBACK", nullptr, nullptr, nullptr);
            if ((code & 255) == SQLITE_NOMEM || code == SQLITE_IOERR_NOMEM) { std::abort(); }
            transaction = false;
        }
    }
    ~Impl() { Rollback(); }

    SaveResult<void> WriteFailure(int code, Diagnostics* diagnostics) {
        auto error = Failure(SqlError(code), sqlite3_errmsg(connection.value), diagnostics);
        Rollback();
        return error;
    }
};

SaveDatabase::SaveDatabase(const ContentCatalog& content) : content_(content) {}
SaveDatabase::~SaveDatabase() = default;

SaveResult<void> SaveDatabase::Create(const char* path, Diagnostics* diagnostics) {
    if (impl_) { return Failure(SaveError::InvalidState, "Database is already open", diagnostics); }
    auto candidate = eastl::make_unique<Impl>();
    auto reserved = ReservePath(path, candidate->createdFile, diagnostics);
    if (!reserved) {
        if (diagnostics) { diagnostics->Add("Creating save: reserving path"); }
        return reserved;
    }
    auto connected = Connect(candidate->connection, path, diagnostics);
    if (!connected) {
        if (diagnostics) { diagnostics->Add("Creating save: opening database"); }
        return connected;
    }
    auto created = Execute(candidate->connection.value,
        "BEGIN IMMEDIATE;"
        "CREATE TABLE metadata(singleton INTEGER PRIMARY KEY CHECK(singleton=1),"
        "format_version INTEGER NOT NULL,game_version TEXT NOT NULL);"
        "INSERT INTO metadata VALUES(1,8,'0.1.0');"
        "CREATE TABLE world_state(singleton INTEGER PRIMARY KEY CHECK(singleton=1),payload TEXT NOT NULL);"
        "CREATE TABLE chunks(x INTEGER NOT NULL,z INTEGER NOT NULL,y INTEGER NOT NULL,payload TEXT NOT NULL,PRIMARY KEY(x,z,y));"
        "CREATE TABLE player_memories(player_id TEXT NOT NULL,x INTEGER NOT NULL,z INTEGER NOT NULL,y INTEGER NOT NULL,payload TEXT NOT NULL,PRIMARY KEY(player_id,x,z,y));"
        "COMMIT;", diagnostics);
    if (!created) {
        (void)Execute(candidate->connection.value, "ROLLBACK", nullptr);
        if (diagnostics) { diagnostics->Add("Creating save: initializing schema"); }
        return created;
    }
    impl_ = eastl::move(candidate);
    return {};
}

SaveResult<SaveSnapshot> SaveDatabase::Open(const char* path, Diagnostics* diagnostics) {
    if (impl_) { return Failure(SaveError::InvalidState, "Database is already open", diagnostics); }
    auto candidate = eastl::make_unique<Impl>();
    auto connected = Connect(candidate->connection, path, diagnostics);
    if (!connected) {
        if (diagnostics) { diagnostics->Add("Opening save database"); }
        return eastl::unexpected(connected.error());
    }
    auto begun = Execute(candidate->connection.value, "BEGIN", diagnostics);
    if (!begun) {
        if (diagnostics) { diagnostics->Add("Opening save: starting validation transaction"); }
        return eastl::unexpected(begun.error());
    }
    candidate->transaction = true;
    auto schema = CheckSchema(candidate->connection.value, diagnostics);
    if (!schema) {
        if (diagnostics) { diagnostics->Add("Opening save: validating schema"); }
        return eastl::unexpected(schema.error());
    }
    auto snapshot = ReadSnapshot(content_, candidate->connection.value, *schema, diagnostics);
    if (!snapshot) {
        if (diagnostics) { diagnostics->Add("Opening save: reading world state"); }
        return snapshot;
    }
    auto valid = ValidateDatabase(content_, candidate->connection.value, *snapshot, diagnostics);
    if (!valid) {
        if (diagnostics) { diagnostics->Add("Opening save: validating saved world"); }
        return eastl::unexpected(valid.error());
    }
    auto committed = Execute(candidate->connection.value, "COMMIT", diagnostics);
    if (!committed) {
        if (diagnostics) { diagnostics->Add("Opening save: completing validation transaction"); }
        return eastl::unexpected(committed.error());
    }
    candidate->transaction = false;
    candidate->formatVersion = *schema;
    impl_ = eastl::move(candidate);
    return snapshot;
}

SaveResult<eastl::optional<ChunkArchive>> SaveDatabase::ReadChunk(ChunkCoord position,
    PersistentId nextId, Diagnostics* diagnostics) {
    if (!impl_) { return Failure(SaveError::NotOpen, "Database is not open", diagnostics); }
    if (!ValidChunk(position) || nextId == 0) {
        return Failure(SaveError::InvalidData, "Invalid chunk position or next_id", diagnostics);
    }
    auto* db = impl_->connection.value;
    Statement statement;
    int code = statement.Prepare(db, "SELECT payload FROM chunks WHERE x=?1 AND z=?2 AND y=?3");
    if (code == SQLITE_OK) { code = sqlite3_bind_int(statement.value, 1, position.x); }
    if (code == SQLITE_OK) { code = sqlite3_bind_int(statement.value, 2, position.z); }
    if (code == SQLITE_OK) { code = sqlite3_bind_int(statement.value, 3, position.y); }
    if (code != SQLITE_OK) { return Failure(SqlError(code), sqlite3_errmsg(db), diagnostics); }
    code = sqlite3_step(statement.value);
    if (code == SQLITE_DONE) { return eastl::optional<ChunkArchive>{}; }
    if (code != SQLITE_ROW) { return Failure(SqlError(code), sqlite3_errmsg(db), diagnostics); }
    const char* text = nullptr;
    std::size_t length = 0;
    if (!TextColumn(statement.value, 0, text, length)) {
        return Failure(SaveError::InvalidData, "Chunk payload must be text", diagnostics);
    }
    auto chunk = DecodeChunk(content_, text, length, position, nextId, diagnostics);
    if (!chunk) {
        ChunkContext(diagnostics, position, "Reading saved chunk");
        return eastl::unexpected(chunk.error());
    }
    code = sqlite3_step(statement.value);
    if (code != SQLITE_DONE) {
        return Failure(code == SQLITE_ROW ? SaveError::InvalidData : SqlError(code),
            "Duplicate chunk row or read error", diagnostics);
    }
    return eastl::optional<ChunkArchive>(eastl::move(*chunk));
}

SaveResult<void> SaveDatabase::Begin(Diagnostics* diagnostics) {
    if (!impl_) { return Failure(SaveError::NotOpen, "Database is not open", diagnostics); }
    if (impl_->transaction) { return Failure(SaveError::InvalidState, "Transaction already active", diagnostics); }
    auto result = Execute(impl_->connection.value, "BEGIN IMMEDIATE", diagnostics);
    if (result) { impl_->transaction = true; }
    else if (diagnostics) { diagnostics->Add("Beginning save checkpoint"); }
    return result;
}

SaveResult<void> SaveDatabase::WriteChunk(const ChunkArchive& chunk, Diagnostics* diagnostics) {
    if (!impl_) { return Failure(SaveError::NotOpen, "Database is not open", diagnostics); }
    if (!impl_->transaction) { return Failure(SaveError::InvalidState, "No active transaction", diagnostics); }
    Vector<PersistentId> ids;
    const auto itemsValid = ValidateItemSnapshot(content_, chunk, diagnostics);
    if (!itemsValid || !ValidateChunk(content_, chunk, UINT64_MAX, ids, diagnostics)) {
        impl_->Rollback();
        const auto code = !itemsValid && itemsValid.error() == StateError::UnsupportedVersion
            ? SaveError::UnsupportedVersion : SaveError::InvalidData;
        auto error = Failure(code,
            "Invalid chunk, ownership, IDs or excessive item depth", diagnostics);
        ChunkContext(diagnostics, chunk.position, "Writing saved chunk");
        return error;
    }
    const String payload = EncodeChunk(content_, chunk);
    auto* db = impl_->connection.value;
    Statement statement;
    int code = statement.Prepare(db,
        "INSERT INTO chunks(x,z,y,payload) VALUES(?1,?2,?3,?4) ON CONFLICT(x,z,y) DO UPDATE SET payload=excluded.payload");
    if (code == SQLITE_OK) { code = sqlite3_bind_int(statement.value, 1, chunk.position.x); }
    if (code == SQLITE_OK) { code = sqlite3_bind_int(statement.value, 2, chunk.position.z); }
    if (code == SQLITE_OK) { code = sqlite3_bind_int(statement.value, 3, chunk.position.y); }
    if (code == SQLITE_OK) { code = BindText(statement.value, 4, payload); }
    if (code != SQLITE_OK) { return impl_->WriteFailure(code, diagnostics); }
    code = sqlite3_step(statement.value);
    if (code != SQLITE_DONE) { return impl_->WriteFailure(code, diagnostics); }
    return {};
}

SaveResult<void> SaveDatabase::WriteSnapshot(const SaveSnapshot& snapshot, Diagnostics* diagnostics) {
    if (!impl_) { return Failure(SaveError::NotOpen, "Database is not open", diagnostics); }
    if (!impl_->transaction) { return Failure(SaveError::InvalidState, "No active transaction", diagnostics); }
    const bool supported = snapshot.generationVersion >= 1 && snapshot.generationVersion <= 3;
    if (!supported || !ValidSnapshot(snapshot) || !ValidMemories(content_, snapshot, diagnostics)) {
        impl_->Rollback();
        return Failure(!supported ? SaveError::UnsupportedVersion : SaveError::InvalidData,
            "Invalid snapshot or unsupported generation version", diagnostics);
    }
    const String payload = EncodeSnapshot(snapshot);
    auto* db = impl_->connection.value;
    Statement statement;
    int code = statement.Prepare(db,
        "INSERT INTO world_state(singleton,payload) VALUES(1,?1) ON CONFLICT(singleton) DO UPDATE SET payload=excluded.payload");
    if (code == SQLITE_OK) { code = BindText(statement.value, 1, payload); }
    if (code != SQLITE_OK) { return impl_->WriteFailure(code, diagnostics); }
    code = sqlite3_step(statement.value);
    if (code != SQLITE_DONE) { return impl_->WriteFailure(code, diagnostics); }
    auto cleared = Execute(db, "DELETE FROM player_memories", diagnostics);
    if (!cleared) { impl_->Rollback(); return cleared; }
    Statement memoryStatement;
    code = memoryStatement.Prepare(db, "INSERT INTO player_memories(player_id,x,z,y,payload) VALUES(?1,?2,?3,?4,?5)");
    if (code != SQLITE_OK) { return impl_->WriteFailure(code, diagnostics); }
    for (const auto& memory : snapshot.memories) {
        char player[32];
        const auto converted = std::to_chars(player, player + sizeof(player), memory.player);
        for (const auto& chunk : memory.chunks) {
            const String memoryPayload = EncodeMemory(content_, chunk);
            code = sqlite3_reset(memoryStatement.value);
            if (code == SQLITE_OK) { code = sqlite3_bind_text(memoryStatement.value, 1, player, static_cast<int>(converted.ptr - player), SQLITE_TRANSIENT); }
            if (code == SQLITE_OK) { code = sqlite3_bind_int(memoryStatement.value, 2, chunk.position.x); }
            if (code == SQLITE_OK) { code = sqlite3_bind_int(memoryStatement.value, 3, chunk.position.z); }
            if (code == SQLITE_OK) { code = sqlite3_bind_int(memoryStatement.value, 4, chunk.position.y); }
            if (code == SQLITE_OK) { code = BindText(memoryStatement.value, 5, memoryPayload); }
            if (code != SQLITE_OK) { return impl_->WriteFailure(code, diagnostics); }
            code = sqlite3_step(memoryStatement.value);
            if (code != SQLITE_DONE) { return impl_->WriteFailure(code, diagnostics); }
        }
    }
    impl_->emptyMemoryOwners.clear();
    for (const auto& memory : snapshot.memories) {
        if (memory.chunks.empty()) { impl_->emptyMemoryOwners.push_back(memory.player); }
    }
    impl_->snapshotWritten = true;
    return {};
}

SaveResult<void> SaveDatabase::Commit(Diagnostics* diagnostics) {
    if (!impl_) { return Failure(SaveError::NotOpen, "Database is not open", diagnostics); }
    if (!impl_->transaction) { return Failure(SaveError::InvalidState, "No active transaction", diagnostics); }
    auto version = ReadFormatVersion(impl_->connection.value, diagnostics);
    if (!version || *version != impl_->formatVersion) {
        impl_->Rollback();
        if (!version) { return eastl::unexpected(version.error()); }
        return Failure(SaveError::InvalidData, "Save format changed while open", diagnostics);
    }
    auto snapshot = ReadSnapshot(content_, impl_->connection.value,
        impl_->snapshotWritten ? save_format_version : impl_->formatVersion, diagnostics);
    if (!snapshot) {
        impl_->Rollback();
        if (diagnostics) { diagnostics->Add("Committing save: reading world state"); }
        return eastl::unexpected(snapshot.error());
    }
    for (PersistentId player : impl_->emptyMemoryOwners) {
        snapshot->memories.push_back(PlayerMemory{player, {}});
    }
    auto valid = ValidateDatabase(content_, impl_->connection.value, *snapshot, diagnostics);
    if (!valid) {
        impl_->Rollback();
        if (diagnostics) { diagnostics->Add("Committing save: validating saved world"); }
        return valid;
    }
    if (impl_->formatVersion == 7) {
        // Even a chunk-only checkpoint upgrades the untouched legacy session.
        // Do not rewrite memory or chunk payloads as part of this migration.
        if (!impl_->snapshotWritten) {
            Statement statement;
            int code = statement.Prepare(impl_->connection.value, "UPDATE world_state SET payload=?1 WHERE singleton=1");
            const String payload = EncodeSnapshot(*snapshot);
            if (code == SQLITE_OK) { code = BindText(statement.value, 1, payload); }
            if (code == SQLITE_OK) { code = sqlite3_step(statement.value); }
            if (code != SQLITE_DONE) { return impl_->WriteFailure(code, diagnostics); }
        }
        auto upgraded = Execute(impl_->connection.value,
            "UPDATE metadata SET format_version=8 WHERE singleton=1", diagnostics);
        if (!upgraded) { impl_->Rollback(); return upgraded; }
    }
    auto result = Execute(impl_->connection.value, "COMMIT", diagnostics);
    if (!result) {
        impl_->Rollback();
        if (diagnostics) { diagnostics->Add("Committing save checkpoint"); }
        return result;
    }
    impl_->transaction = false;
    impl_->formatVersion = save_format_version;
    impl_->snapshotWritten = false;
    impl_->emptyMemoryOwners.clear();
    impl_->createdFile.Keep();
    return {};
}

void SaveDatabase::Rollback() { if (impl_) { impl_->Rollback(); } }

} // namespace cataclysmx
