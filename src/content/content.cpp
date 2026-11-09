#include "content/content.h"
#include "items/item_registry.h"

#include <EASTL/optional.h>
#include <EASTL/sort.h>
#include <EASTL/utility.h>
#include <yyjson.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <limits>

#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
#error Engine must compile without exceptions
#endif

namespace cataclysmx {
namespace {

eastl::string_view View(const String& text) { return {text.data(), text.size()}; }
eastl::string_view JsonString(yyjson_val* value) {
    return {yyjson_get_str(value), yyjson_get_len(value)};
}

struct Context {
    Diagnostics* diagnostics;
    eastl::string_view file;
    String path;

    Context Child(eastl::string_view field) const {
        Context child{diagnostics, file, {}};
        if (diagnostics) {
            child.path = path;
            if (!child.path.empty()) { child.path += '.'; }
            child.path.append(field.data(), field.size());
        }
        return child;
    }
    bool Fail(eastl::string_view message) const {
        if (diagnostics) {
            DiagnosticFrame frame;
            frame.message.assign(message.data(), message.size());
            frame.location = DiagnosticLocation{String(file.empty() ? "" : file.data(), file.size()), path, {}, {}};
            diagnostics->Add(eastl::move(frame));
        }
        return false;
    }
};

void AddTerrainContext(const Context& context, eastl::string_view id) {
    if (!context.diagnostics) { return; }
    String message = "while loading terrain";
    if (!id.empty()) {
        message += " '";
        message.append(id.data(), id.size());
        message += '\'';
    } else {
        message += " definition";
    }
    context.Fail(View(message));
}

void AddPatternContext(const Context& context, eastl::string_view id) {
    if (!context.diagnostics) { return; }
    String message = "while loading building pattern";
    if (!id.empty()) {
        message += " '";
        message.append(id.data(), id.size());
        message += '\'';
    } else { message += " definition"; }
    context.Fail(View(message));
}

bool CheckObject(yyjson_val* object, std::initializer_list<eastl::string_view> allowed,
                 const Context& context) {
    if (!yyjson_is_obj(object)) { return context.Fail("expected an object"); }
    // The schemas are small closed sets. Comparing earlier keys avoids a hash table.
    std::size_t index, count;
    yyjson_val* key;
    yyjson_val* value;
    yyjson_obj_foreach(object, index, count, key, value) {
        (void)value;
        const auto name = JsonString(key);
        bool known = false;
        for (auto candidate : allowed) { if (name == candidate) { known = true; break; } }
        if (!known) { return context.Child(name).Fail("unknown field"); }
        std::size_t previousIndex, previousCount;
        yyjson_val* previousKey;
        yyjson_val* previousValue;
        yyjson_obj_foreach(object, previousIndex, previousCount, previousKey, previousValue) {
            (void)previousValue;
            if (previousIndex == index) { break; }
            if (JsonString(previousKey) == name) { return context.Child(name).Fail("duplicate field"); }
        }
    }
    return true;
}

bool ReadString(yyjson_val* value, String& output, const Context& context) {
    if (!yyjson_is_str(value) || yyjson_get_len(value) == 0) {
        return context.Fail("expected a nonempty string");
    }
    const auto text = JsonString(value);
    if (std::memchr(text.data(), 0, text.size())) { return context.Fail("embedded NUL is not allowed"); }
    output.assign(text.data(), text.size());
    return true;
}

bool ValidId(eastl::string_view id) {
    if (id.empty() || id[0] < 'a' || id[0] > 'z') { return false; }
    for (char ch : id) {
        if ((ch < 'a' || ch > 'z') && (ch < '0' || ch > '9') && ch != '_') { return false; }
    }
    return true;
}

bool ReadId(yyjson_val* value, String& output, const Context& context) {
    if (!ReadString(value, output, context)) { return false; }
    return ValidId(View(output)) || context.Fail("expected an id matching [a-z][a-z0-9_]*");
}

bool ReadBool(yyjson_val* value, bool& output, const Context& context) {
    if (!yyjson_is_bool(value)) { return context.Fail("expected a boolean"); }
    output = yyjson_get_bool(value);
    return true;
}

bool ValidSymbol(eastl::string_view text) {
    if (text.empty()) { return false; }
    const auto first = static_cast<unsigned char>(text[0]);
    std::size_t length = 0;
    std::uint32_t scalar = 0;
    std::uint32_t minimum = 0;
    if (first < 0x80) { length = 1; scalar = first; }
    else if (first >= 0xc2 && first <= 0xdf) { length = 2; scalar = first & 0x1f; minimum = 0x80; }
    else if (first >= 0xe0 && first <= 0xef) { length = 3; scalar = first & 0x0f; minimum = 0x800; }
    else if (first >= 0xf0 && first <= 0xf4) { length = 4; scalar = first & 7; minimum = 0x10000; }
    else { return false; }
    if (text.size() != length) { return false; }
    for (std::size_t i = 1; i < length; ++i) {
        const auto byte = static_cast<unsigned char>(text[i]);
        if ((byte & 0xc0) != 0x80) { return false; }
        scalar = (scalar << 6) | (byte & 0x3f);
    }
    return scalar >= minimum && scalar <= 0x10ffff && !(scalar >= 0xd800 && scalar <= 0xdfff)
        && scalar >= 0x20 && !(scalar >= 0x7f && scalar <= 0x9f);
}

int HexDigit(char ch) {
    if (ch >= '0' && ch <= '9') { return ch - '0'; }
    if (ch >= 'a' && ch <= 'f') { return ch - 'a' + 10; }
    if (ch >= 'A' && ch <= 'F') { return ch - 'A' + 10; }
    return -1;
}

bool ReadColor(yyjson_val* value, RgbColor& output, const Context& context) {
    if (!yyjson_is_str(value)) { return context.Fail("expected a color name or #RRGGBB"); }
    const auto text = JsonString(value);
    struct NamedColor { eastl::string_view name; RgbColor color; };
    static constexpr NamedColor colors[] = {
        {"black", {0, 0, 0}}, {"red", {128, 0, 0}}, {"green", {0, 128, 0}},
        {"yellow", {128, 128, 0}}, {"blue", {0, 0, 128}}, {"magenta", {128, 0, 128}},
        {"brown", {128, 128, 0}},
        {"cyan", {0, 128, 128}}, {"light_gray", {192, 192, 192}},
        {"dark_gray", {128, 128, 128}}, {"light_red", {255, 0, 0}},
        {"light_green", {0, 255, 0}}, {"light_yellow", {255, 255, 0}},
        {"light_blue", {0, 0, 255}}, {"light_magenta", {255, 0, 255}},
        {"light_cyan", {0, 255, 255}}, {"white", {255, 255, 255}},
    };
    for (const auto& color : colors) { if (text == color.name) { output = color.color; return true; } }
    if (text.size() == 7 && text[0] == '#') {
        std::uint8_t channels[3]{};
        for (std::size_t i = 0; i < 3; ++i) {
            const int high = HexDigit(text[1 + 2 * i]);
            const int low = HexDigit(text[2 + 2 * i]);
            if (high < 0 || low < 0) { return context.Fail("invalid hexadecimal color"); }
            channels[i] = static_cast<std::uint8_t>(high * 16 + low);
        }
        output = {channels[0], channels[1], channels[2]};
        return true;
    }
    return context.Fail("unknown color; expected a documented name or #RRGGBB");
}

struct PendingTerrain {
    TerrainDefinition definition;
    String looksLike, opensTo, closesTo;
    bool hasSymbol = false, hasForeground = false, hasBackground = false;
    std::size_t parent = 0;
    std::size_t inputOrder = 0;
    std::uint8_t state = 0;
    Context context;
};

bool MissingReference(const PendingTerrain& terrain, eastl::string_view field,
                      const String& target) {
    if (terrain.context.diagnostics) {
        String message = "unknown terrain reference '";
        message += target;
        message += "' from terrain '";
        message += terrain.definition.id;
        message += '\'';
        terrain.context.Child(field).Fail(View(message));
        AddTerrainContext(terrain.context, View(terrain.definition.id));
    }
    return false;
}

bool ParseTerrain(yyjson_val* object, PendingTerrain& terrain) {
    const auto& context = terrain.context;
    if (!CheckObject(object, {"type", "id", "name", "movement", "blocks_sight", "appearance",
                               "looks_like", "opens_to", "closes_to"}, context)) { return false; }
    if (!ReadId(yyjson_obj_get(object, "id"), terrain.definition.id, context.Child("id"))
        || !ReadString(yyjson_obj_get(object, "name"), terrain.definition.name, context.Child("name"))) {
        return false;
    }
    auto* movement = yyjson_obj_get(object, "movement");
    if (!CheckObject(movement, {"passable"}, context.Child("movement"))
        || !ReadBool(yyjson_obj_get(movement, "passable"), terrain.definition.passable,
                     context.Child("movement").Child("passable"))
        || !ReadBool(yyjson_obj_get(object, "blocks_sight"), terrain.definition.blocksSight,
                     context.Child("blocks_sight"))) { return false; }
    auto* looksLike = yyjson_obj_get(object, "looks_like");
    auto* opensTo = yyjson_obj_get(object, "opens_to");
    auto* closesTo = yyjson_obj_get(object, "closes_to");
    if (opensTo && closesTo) { return context.Fail("opens_to and closes_to are mutually exclusive"); }
    if ((looksLike && !ReadId(looksLike, terrain.looksLike, context.Child("looks_like")))
        || (opensTo && !ReadId(opensTo, terrain.opensTo, context.Child("opens_to")))
        || (closesTo && !ReadId(closesTo, terrain.closesTo, context.Child("closes_to")))) { return false; }
    if (auto* appearance = yyjson_obj_get(object, "appearance")) {
        const auto appearanceContext = context.Child("appearance");
        if (!CheckObject(appearance, {"symbol", "foreground", "background"}, appearanceContext)) { return false; }
        if (auto* symbol = yyjson_obj_get(appearance, "symbol")) {
            if (!ReadString(symbol, terrain.definition.appearance.symbol, appearanceContext.Child("symbol"))) {
                return false;
            }
            if (!ValidSymbol(View(terrain.definition.appearance.symbol))) {
                return appearanceContext.Child("symbol").Fail("expected exactly one non-control Unicode scalar");
            }
            terrain.hasSymbol = true;
        }
        if (auto* foreground = yyjson_obj_get(appearance, "foreground")) {
            if (!ReadColor(foreground, terrain.definition.appearance.foreground,
                           appearanceContext.Child("foreground"))) { return false; }
            terrain.hasForeground = true;
        }
        if (auto* background = yyjson_obj_get(appearance, "background")) {
            if (!ReadColor(background, terrain.definition.appearance.background,
                           appearanceContext.Child("background"))) { return false; }
            terrain.hasBackground = true;
        }
    }
    return true;
}

struct PaletteEntry {
    char glyph = 0;
    String terrain;
    TerrainId resolved;
};

struct PendingPattern {
    BuildingPattern definition;
    Vector<String> rows;
    Vector<PaletteEntry> palette;
    std::size_t inputOrder = 0;
    Context context;
};

bool PrintableAscii(eastl::string_view text) {
    for (unsigned char ch : text) {
        if (ch < 0x20 || ch > 0x7e) { return false; }
    }
    return true;
}

Context ArrayElement(const Context& context, std::size_t index) {
    Context element = context;
    if (context.diagnostics) {
        char suffix[32];
        std::snprintf(suffix, sizeof(suffix), "[%zu]", index);
        element.path += suffix;
    }
    return element;
}

bool ReadBoundedInteger(yyjson_val* value, std::uint32_t minimum, std::uint32_t maximum,
                        std::uint32_t& output, const Context& context) {
    if (!yyjson_is_uint(value) || yyjson_get_uint(value) < minimum || yyjson_get_uint(value) > maximum) {
        return context.Fail("expected an integer in the allowed range");
    }
    output = static_cast<std::uint32_t>(yyjson_get_uint(value));
    return true;
}

struct PendingItem {
    ItemDefinition definition;
    std::size_t inputOrder = 0;
    Context context;
};

void AddItemContext(const Context& context, eastl::string_view id) {
    if (!context.diagnostics) { return; }
    String message = "while loading item '";
    if (!id.empty()) { message.append(id.data(), id.size()); }
    message += '\'';
    context.Fail(View(message));
}

bool ReadItemInteger(yyjson_val* object, const char* field, std::uint32_t minimum,
                     std::uint32_t& output, const Context& context) {
    return ReadBoundedInteger(yyjson_obj_get(object, field), minimum,
                              std::numeric_limits<std::uint32_t>::max(), output, context.Child(field));
}

struct PendingCapability {
    ItemCapabilityDefinition definition;
    Context context;
};

// Content uses ordinary JSON integer semantics, never the archive's tagged codec.
bool ReadContentData(yyjson_val* value, DataObject& output, const Context& context, unsigned depth = 0) {
    if (depth > 64) { return context.Fail("content value exceeds maximum depth"); }
    if (yyjson_is_bool(value)) { output = DataObject::Boolean(yyjson_get_bool(value)); return true; }
    if (yyjson_is_uint(value)) { output = DataObject::Unsigned(yyjson_get_uint(value)); return true; }
    if (yyjson_is_sint(value)) { output = DataObject::Signed(yyjson_get_sint(value)); return true; }
    if (yyjson_is_str(value)) {
        const auto text = JsonString(value);
        if (std::memchr(text.data(), 0, text.size())) { return context.Fail("embedded NUL is not allowed"); }
        output = DataObject::Text(text);
        return true;
    }
    if (yyjson_is_arr(value)) {
        output = DataObject::List();
        std::size_t index, count;
        yyjson_val* child;
        yyjson_arr_foreach(value, index, count, child) {
            DataObject data;
            if (!ReadContentData(child, data, ArrayElement(context, index), depth + 1)) { return false; }
            output.Append(eastl::move(data));
        }
        return true;
    }
    if (yyjson_is_obj(value)) {
        output = DataObject{};
        std::size_t index, count;
        yyjson_val* key;
        yyjson_val* child;
        yyjson_obj_foreach(value, index, count, key, child) {
            const auto name = JsonString(key);
            const auto childContext = context.Child(name);
            if (std::memchr(name.data(), 0, name.size())) { return childContext.Fail("embedded NUL is not allowed"); }
            if (output.Find(name)) { return childContext.Fail("duplicate field"); }
            DataObject data;
            if (!ReadContentData(child, data, childContext, depth + 1)) { return false; }
            output.Set(name, eastl::move(data));
        }
        return true;
    }
    return context.Fail("expected a boolean, integer, string, list, or object");
}

bool CheckDataFields(const DataObject& object, std::initializer_list<eastl::string_view> allowed,
                     const Context& context) {
    for (std::size_t i = 0; i < object.Size(); ++i) {
        const auto name = View(object.Name(i));
        bool known = false;
        for (auto field : allowed) { if (name == field) { known = true; break; } }
        if (!known) { return context.Child(name).Fail("unknown field"); }
    }
    return true;
}

bool ReadDataInteger(const DataObject& object, eastl::string_view field, std::uint32_t minimum,
                     std::uint32_t maximum, std::uint32_t& output, const Context& context) {
    const auto value = object.GetUnsigned(field);
    if (!value || *value < minimum || *value > maximum) {
        return context.Child(field).Fail("expected an integer in the allowed range");
    }
    output = static_cast<std::uint32_t>(*value);
    return true;
}

DataObject CanonicalContentData(const DataObject& input) {
    if (input.Type() == DataType::List) {
        DataObject output = DataObject::List();
        for (std::size_t i = 0; i < input.Size(); ++i) { output.Append(CanonicalContentData(input.At(i))); }
        return output;
    }
    if (input.Type() != DataType::Object) { return input; }
    Vector<std::size_t> fields;
    fields.reserve(input.Size());
    for (std::size_t i = 0; i < input.Size(); ++i) { fields.push_back(i); }
    eastl::sort(fields.begin(), fields.end(), [&input](std::size_t left, std::size_t right) {
        return input.Name(left) < input.Name(right);
    });
    DataObject output;
    for (auto field : fields) { output.Set(View(input.Name(field)), CanonicalContentData(input.At(field))); }
    return output;
}

// Factories have no JSON/source dependency. Supply the caller's source location
// to their inner-first diagnostic frames at this boundary.
void AppendBehaviorDiagnostics(const Diagnostics& details, const Context& context) {
    if (!context.diagnostics) { return; }
    for (const auto& detail : details.Frames()) {
        DiagnosticFrame frame = detail;
        if (!frame.location) {
            frame.location = DiagnosticLocation{String(context.file.data(), context.file.size()), context.path, {}, {}};
        } else {
            if (frame.location->file.empty()) { frame.location->file.assign(context.file.data(), context.file.size()); }
            String path = context.path;
            if (!frame.location->path.empty()) {
                if (!path.empty() && frame.location->path[0] != '[') { path += '.'; }
                path += frame.location->path;
            }
            frame.location->path = eastl::move(path);
        }
        context.diagnostics->Add(eastl::move(frame));
    }
}

bool ParseSharedCapability(PendingCapability& pending, ItemDefinition& item, ItemCapabilityId self) {
    auto& capability = pending.definition;
    const auto& input = capability.config;
    const auto& context = pending.context;
    constexpr auto maximum = std::numeric_limits<std::uint32_t>::max();
    DataObject config;
    config.SetString("id", View(capability.id));
    config.SetString("type", View(capability.type));
    if (capability.type == "container") {
        ContainerDefinition data;
        if (!CheckDataFields(input, {"id", "type", "capacity_volume_ml"}, context)
            || !ReadDataInteger(input, "capacity_volume_ml", 1, maximum, data.capacityVolumeMl, context)) { return false; }
        if (item.container) { return context.Fail("at most one container is allowed"); }
        item.container = ItemContainerDefinition{self, data};
        config.SetUnsigned("capacity_volume_ml", data.capacityVolumeMl);
    } else {
        EnergyStoreDefinition data;
        if (!CheckDataFields(input, {"id", "type", "capacity_energy", "initial_energy", "rechargeable", "battery_class"}, context)
            || !ReadDataInteger(input, "capacity_energy", 1, maximum, data.capacityEnergy, context)) { return false; }
        const auto rechargeable = input.GetBoolean("rechargeable");
        if (!rechargeable) { return context.Child("rechargeable").Fail("expected a boolean"); }
        data.rechargeable = *rechargeable;
        data.initialEnergy = data.capacityEnergy;
        if (input.Find("initial_energy")
            && !ReadDataInteger(input, "initial_energy", 0, data.capacityEnergy, data.initialEnergy, context)) { return false; }
        if (input.Find("battery_class")) {
            const auto batteryClass = input.GetString("battery_class");
            if (!batteryClass || !ValidId(*batteryClass)) {
                return context.Child("battery_class").Fail("expected an id matching [a-z][a-z0-9_]*");
            }
            data.batteryClass.assign(batteryClass->data(), batteryClass->size());
        }
        config.SetUnsigned("capacity_energy", data.capacityEnergy);
        config.SetUnsigned("initial_energy", data.initialEnergy);
        config.SetBoolean("rechargeable", data.rechargeable);
        config.SetString("battery_class", View(data.batteryClass));
        item.energyStores.push_back({self, eastl::move(data)});
    }
    capability.config = CanonicalContentData(config);
    return true;
}

bool ParseItem(yyjson_val* object, PendingItem& item, const ItemBehaviorRegistry& registry) {
    const auto& context = item.context;
    auto& definition = item.definition;
    if (!CheckObject(object, {"type", "id", "name", "mass_g", "volume_ml", "capabilities"}, context)
        || !ReadId(yyjson_obj_get(object, "id"), definition.id, context.Child("id"))
        || !ReadString(yyjson_obj_get(object, "name"), definition.name, context.Child("name"))
        || !ReadItemInteger(object, "mass_g", 0, definition.massGrams, context)
        || !ReadItemInteger(object, "volume_ml", 1, definition.volumeMl, context)) { return false; }
    auto* capabilities = yyjson_obj_get(object, "capabilities");
    if (!capabilities) { return true; }
    const auto capabilitiesContext = context.Child("capabilities");
    if (!yyjson_is_arr(capabilities) || yyjson_arr_size(capabilities) > maxItemCapabilities) {
        return capabilitiesContext.Fail("expected an array of at most 16 capabilities");
    }
    Vector<PendingCapability> pending;
    std::size_t index, count;
    yyjson_val* value;
    yyjson_arr_foreach(capabilities, index, count, value) {
        PendingCapability capability;
        capability.context = ArrayElement(capabilitiesContext, index);
        if (!yyjson_is_obj(value)) { return capability.context.Fail("expected a capability object"); }
        if (!ReadId(yyjson_obj_get(value, "id"), capability.definition.id, capability.context.Child("id"))
            || !ReadId(yyjson_obj_get(value, "type"), capability.definition.type, capability.context.Child("type"))
            || !ReadContentData(value, capability.definition.config, capability.context)) { return false; }
        pending.push_back(eastl::move(capability));
    }
    eastl::sort(pending.begin(), pending.end(), [](const PendingCapability& left, const PendingCapability& right) {
        return left.definition.id < right.definition.id;
    });
    for (std::size_t i = 0; i < pending.size(); ++i) {
        auto& capability = pending[i];
        if (i > 0 && pending[i - 1].definition.id == capability.definition.id) {
            return capability.context.Child("id").Fail("duplicate capability id");
        }
        definition.capabilities.push_back(capability.definition);
    }
    for (std::size_t i = 0; i < pending.size(); ++i) {
        auto& capability = pending[i];
        if (capability.definition.type != "container" && capability.definition.type != "energy_store") { continue; }
        if (!ParseSharedCapability(capability, definition, {static_cast<std::uint16_t>(i + 1)})) { return false; }
        definition.capabilities[i].config = capability.definition.config;
    }
    for (const auto& store : definition.energyStores) {
        if (!store.config.batteryClass.empty() && definition.energyStores.size() != 1) {
            return capabilitiesContext.Fail("an installable battery requires a sole energy store");
        }
    }
    std::size_t declaredActions = 0;
    for (std::size_t i = 0; i < pending.size(); ++i) {
        const auto& capability = pending[i];
        if (capability.definition.type == "container" || capability.definition.type == "energy_store") { continue; }
        const auto parser = registry.Find(View(capability.definition.type));
        if (!parser) { return capability.context.Child("type").Fail("unsupported item capability type"); }
        const ItemCapabilityId self{static_cast<std::uint16_t>(i + 1)};
        Diagnostics details;
        auto parsed = parser(capability.definition.config, definition, self, context.diagnostics ? &details : nullptr);
        AppendBehaviorDiagnostics(details, capability.context);
        if (!parsed) { return capability.context.Fail("could not parse item behavior"); }
        if (!parsed->definition || parsed->config.Type() != DataType::Object) {
            return capability.context.Fail("behavior factory returned an invalid definition");
        }
        const auto& schema = parsed->definition->Schema();
        if (!ValidItemBehaviorSchema(schema)) {
            return capability.context.Child("schema").Fail("invalid item behavior schema");
        }
        if (schema.actions.size() > maxItemActionsPerItem - declaredActions) {
            return capability.context.Child("schema").Child("actions").Fail("at most 64 declared actions per item are allowed");
        }
        declaredActions += schema.actions.size();
        definition.capabilities[i].config = CanonicalContentData(parsed->config);
        definition.behaviors.push_back({self, eastl::move(parsed->definition)});
    }
    for (const auto& binding : definition.behaviors) {
        const auto& behaviorContext = pending[binding.capability.value - 1].context;
        Diagnostics details;
        const auto result = binding.definition->ValidateComposition(definition, binding.capability,
            context.diagnostics ? &details : nullptr);
        AppendBehaviorDiagnostics(details, behaviorContext);
        if (!result) {
            return behaviorContext.Fail("invalid item behavior composition");
        }
    }
    return true;
}

bool ParsePattern(yyjson_val* object, PendingPattern& pattern) {
    const auto& context = pattern.context;
    auto& definition = pattern.definition;
    if (!CheckObject(object, {"type", "id", "rows", "palette", "entrance", "weight"}, context)
        || !ReadId(yyjson_obj_get(object, "id"), definition.id, context.Child("id"))) { return false; }
    auto* rows = yyjson_obj_get(object, "rows");
    const auto rowsContext = context.Child("rows");
    if (!yyjson_is_arr(rows) || yyjson_arr_size(rows) == 0 || yyjson_arr_size(rows) > 64) {
        return rowsContext.Fail("expected between 1 and 64 rows");
    }
    std::size_t index, count;
    yyjson_val* row;
    yyjson_arr_foreach(rows, index, count, row) {
        String text;
        const auto rowContext = ArrayElement(rowsContext, index);
        if (!ReadString(row, text, rowContext)) { return false; }
        if (text.size() > 64 || !PrintableAscii(View(text))) {
            return rowContext.Fail("expected between 1 and 64 printable ASCII glyphs");
        }
        if (index == 0) { definition.width = static_cast<int>(text.size()); }
        if (text.size() != static_cast<std::size_t>(definition.width)) {
            return rowContext.Fail("rows must have equal widths");
        }
        pattern.rows.push_back(eastl::move(text));
    }
    definition.height = static_cast<int>(pattern.rows.size());
    auto* palette = yyjson_obj_get(object, "palette");
    const auto paletteContext = context.Child("palette");
    if (!yyjson_is_obj(palette)) { return paletteContext.Fail("expected an object"); }
    yyjson_val* key;
    yyjson_val* value;
    yyjson_obj_foreach(palette, index, count, key, value) {
        const auto glyph = JsonString(key);
        const auto entryContext = paletteContext.Child(glyph);
        if (glyph.size() != 1 || !PrintableAscii(glyph)) {
            return entryContext.Fail("expected a single printable ASCII palette key");
        }
        for (const auto& entry : pattern.palette) {
            if (entry.glyph == glyph[0]) { return entryContext.Fail("duplicate field"); }
        }
        PaletteEntry entry;
        entry.glyph = glyph[0];
        if (!ReadId(value, entry.terrain, entryContext)) { return false; }
        pattern.palette.push_back(eastl::move(entry));
    }
    auto* entrance = yyjson_obj_get(object, "entrance");
    const auto entranceContext = context.Child("entrance");
    if (!CheckObject(entrance, {"x", "z"}, entranceContext)) { return false; }
    std::uint32_t x = 0, z = 0;
    if (!ReadBoundedInteger(yyjson_obj_get(entrance, "x"), 0, definition.width - 1, x,
                            entranceContext.Child("x"))
        || !ReadBoundedInteger(yyjson_obj_get(entrance, "z"), 0, definition.height - 1, z,
                               entranceContext.Child("z"))) { return false; }
    definition.entrance = {static_cast<int>(x), static_cast<int>(z)};
    if (x != 0 && z != 0 && x != static_cast<std::uint32_t>(definition.width - 1)
        && z != static_cast<std::uint32_t>(definition.height - 1)) {
        return entranceContext.Fail("entrance must lie on the boundary");
    }
    if (auto* weight = yyjson_obj_get(object, "weight")) {
        if (!ReadBoundedInteger(weight, 1, 1000000, definition.weight, context.Child("weight"))) { return false; }
    }
    return true;
}

void* JsonMalloc(void*, std::size_t size) {
    void* memory = std::malloc(size);
    if (!memory) { std::abort(); }
    return memory;
}
void* JsonRealloc(void*, void* memory, std::size_t, std::size_t size) {
    void* result = std::realloc(memory, size);
    if (!result) { std::abort(); }
    return result;
}
void JsonFree(void*, void* memory) { std::free(memory); }

struct Document {
    yyjson_doc* value = nullptr;
    ~Document() { yyjson_doc_free(value); }
};

ContentResult<bool> ParseSource(const ContentSource& source, Vector<PendingTerrain>& pending,
                                Vector<PendingPattern>& patterns, Vector<PendingItem>& items,
                                const ItemBehaviorRegistry& registry, Diagnostics* diagnostics) {
    Context context{diagnostics, source.name, {}};
    yyjson_alc allocator{JsonMalloc, JsonRealloc, JsonFree, nullptr};
    yyjson_read_err error{};
    Document document;
    document.value = yyjson_read_opts(const_cast<char*>(source.text.data()), source.text.size(),
                                     YYJSON_READ_JSON5, &allocator, &error);
    if (!document.value) {
        if (error.code == YYJSON_READ_ERROR_MEMORY_ALLOCATION) { std::abort(); }
        if (diagnostics) {
            std::uint64_t line = 1, column = 1;
            for (std::size_t i = 0; i < error.pos && i < source.text.size(); ++i) {
                if (source.text[i] == '\n') { ++line; column = 1; } else { ++column; }
            }
            DiagnosticFrame frame;
            frame.message = error.msg ? error.msg : "invalid JSON5";
            frame.location = DiagnosticLocation{
                String(source.name.empty() ? "" : source.name.data(), source.name.size()), {}, line, column};
            diagnostics->Add(eastl::move(frame));
        }
        return eastl::unexpected(ContentError::InvalidData);
    }
    auto* root = yyjson_doc_get_root(document.value);
    if (!CheckObject(root, {"format_version", "definitions"}, context)) {
        return eastl::unexpected(ContentError::InvalidData);
    }
    auto* version = yyjson_obj_get(root, "format_version");
    if (!yyjson_is_uint(version)) {
        context.Child("format_version").Fail("expected a positive integer format version");
        return eastl::unexpected(ContentError::InvalidData);
    }
    if (yyjson_get_uint(version) != 1) {
        context.Child("format_version").Fail("unsupported content format version");
        return eastl::unexpected(ContentError::UnsupportedVersion);
    }
    auto* definitions = yyjson_obj_get(root, "definitions");
    if (!yyjson_is_arr(definitions)) {
        context.Child("definitions").Fail("expected an array");
        return eastl::unexpected(ContentError::InvalidData);
    }
    std::size_t index, count;
    yyjson_val* value;
    yyjson_arr_foreach(definitions, index, count, value) {
        Context definitionContext{diagnostics, source.name, {}};
        if (diagnostics) {
            char path[64];
            std::snprintf(path, sizeof(path), "definitions[%zu]", index);
            definitionContext.path = path;
        }
        PendingTerrain terrain;
        terrain.context = definitionContext;
        terrain.inputOrder = pending.size() + patterns.size() + items.size();
        bool parsed = false;
        // Dispatch definition kinds at the document boundary; individual parsers own their schemas.
        if (!yyjson_is_obj(value)) {
            terrain.context.Fail("expected a definition object");
        } else {
            auto* type = yyjson_obj_get(value, "type");
            if (!yyjson_is_str(type)) {
                terrain.context.Child("type").Fail("expected a definition type string");
            } else if (JsonString(type) == "terrain") {
                parsed = ParseTerrain(value, terrain);
            } else if (JsonString(type) == "item") {
                PendingItem item;
                item.context = definitionContext;
                item.inputOrder = terrain.inputOrder;
                if (!ParseItem(value, item, registry)) {
                    auto* id = yyjson_obj_get(value, "id");
                    AddItemContext(item.context, yyjson_is_str(id) ? JsonString(id) : eastl::string_view{});
                    return eastl::unexpected(ContentError::InvalidData);
                }
                items.push_back(eastl::move(item));
                continue;
            } else if (JsonString(type) == "building_pattern") {
                PendingPattern pattern;
                pattern.context = definitionContext;
                pattern.inputOrder = terrain.inputOrder;
                if (!ParsePattern(value, pattern)) {
                    auto* id = yyjson_obj_get(value, "id");
                    AddPatternContext(pattern.context, yyjson_is_str(id) ? JsonString(id) : eastl::string_view{});
                    return eastl::unexpected(ContentError::InvalidData);
                }
                patterns.push_back(eastl::move(pattern));
                continue;
            } else if (diagnostics) {
                String message = "unsupported definition type '";
                const auto typeName = JsonString(type);
                message.append(typeName.data(), typeName.size());
                message += "' (expected terrain, building_pattern, or item)";
                terrain.context.Child("type").Fail(View(message));
            }
        }
        if (!parsed) {
            if (diagnostics) {
                auto* type = yyjson_is_obj(value) ? yyjson_obj_get(value, "type") : nullptr;
                if (yyjson_is_str(type) && JsonString(type) == "terrain") {
                    auto* id = yyjson_obj_get(value, "id");
                    AddTerrainContext(terrain.context, yyjson_is_str(id) ? JsonString(id) : eastl::string_view{});
                } else {
                    terrain.context.Fail("while loading content definition");
                }
            }
            return eastl::unexpected(ContentError::InvalidData);
        }
        pending.push_back(eastl::move(terrain));
    }
    return true;
}

std::size_t FindPending(const Vector<PendingTerrain>& pending, eastl::string_view id) {
    std::size_t begin = 0, end = pending.size();
    while (begin < end) {
        const std::size_t middle = begin + (end - begin) / 2;
        if (View(pending[middle].definition.id) < id) { begin = middle + 1; }
        else { end = middle; }
    }
    return begin < pending.size() && View(pending[begin].definition.id) == id ? begin : pending.size();
}

bool Resolve(Vector<PendingTerrain>& pending) {
    const std::size_t absent = pending.size();
    for (auto& terrain : pending) {
        terrain.parent = absent;
        if (!terrain.looksLike.empty()) {
            terrain.parent = FindPending(pending, View(terrain.looksLike));
            if (terrain.parent == absent) { return MissingReference(terrain, "looks_like", terrain.looksLike); }
        }
        if (!terrain.opensTo.empty()) {
            const std::size_t target = FindPending(pending, View(terrain.opensTo));
            if (target == absent) { return MissingReference(terrain, "opens_to", terrain.opensTo); }
            terrain.definition.opensTo = {static_cast<std::uint32_t>(target + 1)};
        }
        if (!terrain.closesTo.empty()) {
            const std::size_t target = FindPending(pending, View(terrain.closesTo));
            if (target == absent) { return MissingReference(terrain, "closes_to", terrain.closesTo); }
            terrain.definition.closesTo = {static_cast<std::uint32_t>(target + 1)};
        }
    }
    Vector<std::size_t> chain;
    for (std::size_t start = 0; start < pending.size(); ++start) {
        std::size_t current = start;
        while (current != absent && pending[current].state != 2) {
            auto& terrain = pending[current];
            if (terrain.state == 1) {
                if (terrain.context.diagnostics) {
                    String message = "appearance inheritance cycle involving terrain '";
                    message += terrain.definition.id;
                    message += '\'';
                    terrain.context.Child("looks_like").Fail(View(message));
                    AddTerrainContext(terrain.context, View(terrain.definition.id));
                }
                return false;
            }
            terrain.state = 1;
            chain.push_back(current);
            current = terrain.parent;
        }
        while (!chain.empty()) {
            auto& terrain = pending[chain.back()];
            chain.pop_back();
            auto& appearance = terrain.definition.appearance;
            if (terrain.parent != absent) {
                const auto& parent = pending[terrain.parent];
                if (!terrain.hasSymbol) { appearance.symbol = parent.definition.appearance.symbol; }
                if (!terrain.hasForeground) { appearance.foreground = parent.definition.appearance.foreground; }
                if (!terrain.hasBackground) { appearance.background = parent.definition.appearance.background; }
            } else if (!terrain.hasSymbol || !terrain.hasForeground) {
                terrain.context.Child("appearance").Fail("resolved appearance requires symbol and foreground");
                AddTerrainContext(terrain.context, View(terrain.definition.id));
                return false;
            }
            // Unspecified root background is value-initialized black. Only now may it be inherited.
            terrain.state = 2;
        }
    }
    return true;
}

bool ResolvePattern(PendingPattern& pattern, const ContentCatalog& catalog) {
    for (auto& entry : pattern.palette) {
        entry.resolved = catalog.FindTerrain(View(entry.terrain));
        if (entry.resolved == TerrainId{}) {
            if (pattern.context.diagnostics) {
                String message = "unknown terrain reference '";
                message += entry.terrain;
                message += "' from building pattern '";
                message += pattern.definition.id;
                message += '\'';
                pattern.context.Child("palette").Child({&entry.glyph, 1}).Fail(View(message));
            }
            return false;
        }
    }
    auto& definition = pattern.definition;
    definition.tiles.reserve(static_cast<std::size_t>(definition.width * definition.height));
    for (std::size_t z = 0; z < pattern.rows.size(); ++z) {
        for (char glyph : pattern.rows[z]) {
            TerrainId terrain;
            for (const auto& entry : pattern.palette) {
                if (entry.glyph == glyph) { terrain = entry.resolved; break; }
            }
            if (terrain == TerrainId{}) {
                return ArrayElement(pattern.context.Child("rows"), z).Fail("row glyph is absent from palette");
            }
            definition.tiles.push_back(terrain);
        }
    }
    const auto entranceIndex = static_cast<std::size_t>(definition.entrance.z * definition.width
                                                       + definition.entrance.x);
    const auto& entrance = catalog.GetTerrain(definition.tiles[entranceIndex]);
    const auto* opened = catalog.TryGetTerrain(entrance.opensTo);
    if (!entrance.passable && (!opened || !opened->passable)) {
        return pattern.context.Child("entrance").Fail("entrance terrain must be passable or open to passable terrain");
    }
    return true;
}

struct DefinitionIdentity {
    eastl::string_view id;
    const Context* context = nullptr;
    std::size_t inputOrder = 0;
    bool pattern = false;
    bool item = false;
};

bool CheckDefinitionIds(const Vector<PendingTerrain>& terrains, const Vector<PendingPattern>& patterns,
                         const Vector<PendingItem>& items, Diagnostics* diagnostics) {
    Vector<DefinitionIdentity> identities;
    identities.reserve(terrains.size() + patterns.size() + items.size());
    for (const auto& terrain : terrains) {
        identities.push_back({View(terrain.definition.id), &terrain.context, terrain.inputOrder, false});
    }
    for (const auto& pattern : patterns) {
        identities.push_back({View(pattern.definition.id), &pattern.context, pattern.inputOrder, true});
    }
    for (const auto& item : items) {
        identities.push_back({View(item.definition.id), &item.context, item.inputOrder, false, true});
    }
    eastl::sort(identities.begin(), identities.end(), [](const DefinitionIdentity& left, const DefinitionIdentity& right) {
        if (left.id == right.id) { return left.inputOrder < right.inputOrder; }
        return left.id < right.id;
    });
    for (std::size_t i = 1; i < identities.size(); ++i) {
        const auto& current = identities[i];
        const auto& previous = identities[i - 1];
        if (previous.id != current.id) { continue; }
        if (diagnostics) {
            String message = current.item ? "duplicate item definition '" :
                (current.pattern ? "duplicate building pattern definition '" : "duplicate terrain definition '");
            message.append(current.id.data(), current.id.size());
            message += '\'';
            current.context->Child("id").Fail(View(message));
            previous.context->Child("id").Fail("previous definition is here");
            if (current.item) { AddItemContext(*current.context, current.id); }
            else if (current.pattern) { AddPatternContext(*current.context, current.id); }
            else { AddTerrainContext(*current.context, current.id); }
            Context{diagnostics, {}, {}}.Fail("could not load content catalog");
        }
        return false;
    }
    return true;
}

struct File {
    std::FILE* value = nullptr;
    ~File() { if (value) { std::fclose(value); } }
    bool Close() {
        std::FILE* closing = value;
        value = nullptr;
        return std::fclose(closing) == 0;
    }
};

eastl::unexpected<ContentError> FileFailure(const Context& context, eastl::string_view message) {
    context.Fail(message);
    Context{context.diagnostics, context.file, {}}.Fail("while loading content files");
    return eastl::unexpected(ContentError::Io);
}

}

TerrainId ContentCatalog::FindTerrain(eastl::string_view id) const {
    std::size_t begin = 0, end = definitions_.size();
    while (begin < end) {
        const std::size_t middle = begin + (end - begin) / 2;
        if (View(definitions_[middle].id) < id) { begin = middle + 1; }
        else { end = middle; }
    }
    if (begin == definitions_.size() || View(definitions_[begin].id) != id) { return {}; }
    return {static_cast<std::uint32_t>(begin + 1)};
}

const TerrainDefinition* ContentCatalog::TryGetTerrain(TerrainId id) const {
    if (id.value == 0 || id.value > definitions_.size()) { return nullptr; }
    return &definitions_[id.value - 1];
}

const TerrainDefinition& ContentCatalog::GetTerrain(TerrainId id) const {
    const auto* definition = TryGetTerrain(id);
    if (!definition) { std::abort(); }
    return *definition;
}

std::size_t ContentCatalog::TerrainCount() const { return definitions_.size(); }

BuildingPatternId ContentCatalog::FindBuildingPattern(eastl::string_view id) const {
    std::size_t begin = 0, end = patterns_.size();
    while (begin < end) {
        const std::size_t middle = begin + (end - begin) / 2;
        if (View(patterns_[middle].id) < id) { begin = middle + 1; }
        else { end = middle; }
    }
    if (begin == patterns_.size() || View(patterns_[begin].id) != id) { return {}; }
    return {static_cast<std::uint32_t>(begin + 1)};
}

const BuildingPattern* ContentCatalog::TryGetBuildingPattern(BuildingPatternId id) const {
    if (id.value == 0 || id.value > patterns_.size()) { return nullptr; }
    return &patterns_[id.value - 1];
}

const BuildingPattern& ContentCatalog::GetBuildingPattern(BuildingPatternId id) const {
    const auto* pattern = TryGetBuildingPattern(id);
    if (!pattern) { std::abort(); }
    return *pattern;
}

std::size_t ContentCatalog::BuildingPatternCount() const { return patterns_.size(); }

ItemTypeId ContentCatalog::FindItem(eastl::string_view id) const {
    std::size_t begin = 0, end = items_.size();
    while (begin < end) {
        const std::size_t middle = begin + (end - begin) / 2;
        if (View(items_[middle].id) < id) { begin = middle + 1; }
        else { end = middle; }
    }
    if (begin == items_.size() || View(items_[begin].id) != id) { return {}; }
    return {static_cast<std::uint32_t>(begin + 1)};
}

const ItemDefinition* ContentCatalog::TryGetItem(ItemTypeId id) const {
    if (id.value == 0 || id.value > items_.size()) { return nullptr; }
    return &items_[id.value - 1];
}

const ItemDefinition& ContentCatalog::GetItem(ItemTypeId id) const {
    const auto* definition = TryGetItem(id);
    if (!definition) { std::abort(); }
    return *definition;
}

std::size_t ContentCatalog::ItemCount() const { return items_.size(); }

ContentResult<ContentCatalog> LoadContent(eastl::span<const ContentSource> sources, Diagnostics* diagnostics) {
    ItemBehaviorRegistry registry;
    RegisterBuiltinItemBehaviors(registry);
    return LoadContent(sources, registry, diagnostics);
}

ContentResult<ContentCatalog> LoadContent(eastl::span<const ContentSource> sources,
    const ItemBehaviorRegistry& registry, Diagnostics* diagnostics) {
    Vector<PendingTerrain> pending;
    Vector<PendingPattern> patterns;
    Vector<PendingItem> items;
    for (const auto& source : sources) {
        auto result = ParseSource(source, pending, patterns, items, registry, diagnostics);
        if (!result) {
            Context{diagnostics, source.name, {}}.Fail("could not load content source");
            return eastl::unexpected(result.error());
        }
    }
    if (pending.size() > std::numeric_limits<std::uint32_t>::max()
        || patterns.size() > std::numeric_limits<std::uint32_t>::max()
        || items.size() > std::numeric_limits<std::uint32_t>::max()) {
        Context{diagnostics, {}, {}}.Fail("too many content definitions");
        Context{diagnostics, {}, {}}.Fail("could not load content catalog");
        return eastl::unexpected(ContentError::InvalidData);
    }
    eastl::sort(pending.begin(), pending.end(), [](const PendingTerrain& left, const PendingTerrain& right) {
        // Retain input order for equal IDs so duplicate diagnostics identify the prior source reliably.
        if (left.definition.id == right.definition.id) { return left.inputOrder < right.inputOrder; }
        return left.definition.id < right.definition.id;
    });
    if (!CheckDefinitionIds(pending, patterns, items, diagnostics)) { return eastl::unexpected(ContentError::InvalidData); }
    if (!Resolve(pending)) {
        Context{diagnostics, {}, {}}.Fail("could not load content catalog");
        return eastl::unexpected(ContentError::InvalidData);
    }
    ContentCatalog catalog;
    eastl::sort(items.begin(), items.end(), [](const PendingItem& left, const PendingItem& right) {
        return left.definition.id < right.definition.id;
    });
    catalog.items_.reserve(items.size());
    for (auto& item : items) { catalog.items_.push_back(eastl::move(item.definition)); }
    catalog.definitions_.reserve(pending.size());
    for (auto& terrain : pending) { catalog.definitions_.push_back(eastl::move(terrain.definition)); }
    eastl::sort(patterns.begin(), patterns.end(), [](const PendingPattern& left, const PendingPattern& right) {
        return left.definition.id < right.definition.id;
    });
    catalog.patterns_.reserve(patterns.size());
    for (auto& pattern : patterns) {
        if (!ResolvePattern(pattern, catalog)) {
            AddPatternContext(pattern.context, View(pattern.definition.id));
            Context{diagnostics, {}, {}}.Fail("could not load content catalog");
            return eastl::unexpected(ContentError::InvalidData);
        }
        catalog.patterns_.push_back(eastl::move(pattern.definition));
    }
    return catalog;
}

ContentResult<ContentCatalog> LoadContentFiles(eastl::span<const eastl::string_view> paths,
                                             Diagnostics* diagnostics) {
    ItemBehaviorRegistry registry;
    RegisterBuiltinItemBehaviors(registry);
    return LoadContentFiles(paths, registry, diagnostics);
}

ContentResult<ContentCatalog> LoadContentFiles(eastl::span<const eastl::string_view> paths,
    const ItemBehaviorRegistry& registry, Diagnostics* diagnostics) {
    Vector<String> texts;
    texts.reserve(paths.size());
    for (auto path : paths) {
        Context context{diagnostics, path, {}};
        if (path.empty() || std::memchr(path.data(), 0, path.size())) {
            return FileFailure(context, "invalid content file path");
        }
        const String terminatedPath(path.data(), path.size());
        File file;
#ifdef _MSC_VER
        fopen_s(&file.value, terminatedPath.c_str(), "rb");
#else
        file.value = std::fopen(terminatedPath.c_str(), "rb");
#endif
        if (!file.value) {
            return FileFailure(context, "could not open content file");
        }
        String text;
        char buffer[8192];
        for (;;) {
            const std::size_t count = std::fread(buffer, 1, sizeof(buffer), file.value);
            text.append(buffer, count);
            if (std::ferror(file.value)) {
                return FileFailure(context, "could not read content file");
            }
            if (std::feof(file.value)) { break; }
            if (count == 0) {
                return FileFailure(context, "content file read made no progress");
            }
        }
        if (!file.Close()) {
            return FileFailure(context, "could not close content file");
        }
        texts.push_back(eastl::move(text));
    }
    Vector<ContentSource> sources;
    sources.reserve(paths.size());
    for (std::size_t i = 0; i < paths.size(); ++i) { sources.push_back({paths[i], View(texts[i])}); }
    return LoadContent({sources.data(), sources.size()}, registry, diagnostics);
}

ContentResult<ContentCatalog> LoadContentFile(const char* path, Diagnostics* diagnostics) {
    ItemBehaviorRegistry registry;
    RegisterBuiltinItemBehaviors(registry);
    return LoadContentFile(path, registry, diagnostics);
}

ContentResult<ContentCatalog> LoadContentFile(const char* path,
    const ItemBehaviorRegistry& registry, Diagnostics* diagnostics) {
    const eastl::string_view view = path ? eastl::string_view(path) : eastl::string_view{};
    return LoadContentFiles({&view, 1}, registry, diagnostics);
}

}
