#include "content/content.h"
#include "core/ratio.h"
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

bool ReadBoundedInteger(yyjson_val* value, std::uint32_t minimum, std::uint32_t maximum,
                        std::uint32_t& output, const Context& context);

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
    String looksLike, opensTo, closesTo, locksTo, unlocksTo;
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
                               "looks_like", "opens_to", "closes_to", "locks_to", "unlocks_to", "open_space", "stairs", "light"}, context)) { return false; }
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
    if (auto* openSpace = yyjson_obj_get(object, "open_space")) {
        if (!ReadBool(openSpace, terrain.definition.openSpace, context.Child("open_space"))) { return false; }
    }
    if (auto* stairs = yyjson_obj_get(object, "stairs")) {
        if (!ReadBool(stairs, terrain.definition.stairs, context.Child("stairs"))) { return false; }
    }
    if (auto* light = yyjson_obj_get(object, "light")) {
        const auto lightContext = context.Child("light");
        std::uint32_t intensity = 0, range = 0;
        if (!CheckObject(light, {"intensity", "range"}, lightContext)
            || !ReadBoundedInteger(yyjson_obj_get(light, "intensity"), 0,
                                   std::numeric_limits<std::uint8_t>::max(), intensity, lightContext.Child("intensity"))
            || !ReadBoundedInteger(yyjson_obj_get(light, "range"), 0, maxLightRange, range,
                                   lightContext.Child("range"))) { return false; }
        const LightEmission emission{static_cast<std::uint8_t>(intensity), static_cast<std::uint8_t>(range)};
        if (!ValidLightEmission(emission)) {
            return lightContext.Fail("intensity and range must both be zero or both be positive");
        }
        terrain.definition.light = emission;
    }
    if (terrain.definition.openSpace && (!terrain.definition.passable
        || terrain.definition.blocksSight || terrain.definition.stairs)) {
        return context.Child("open_space").Fail("open_space requires passable, non-sight-blocking terrain without stairs");
    }
    if (terrain.definition.stairs && (!terrain.definition.passable || terrain.definition.blocksSight)) {
        return context.Child("stairs").Fail("stairs requires passable, non-sight-blocking terrain without open_space");
    }
    auto* looksLike = yyjson_obj_get(object, "looks_like");
    auto* opensTo = yyjson_obj_get(object, "opens_to");
    auto* closesTo = yyjson_obj_get(object, "closes_to");
    auto* locksTo = yyjson_obj_get(object, "locks_to");
    auto* unlocksTo = yyjson_obj_get(object, "unlocks_to");
    if (locksTo && unlocksTo) { return context.Fail("locks_to and unlocks_to are mutually exclusive"); }
    if (unlocksTo && (opensTo || closesTo || terrain.definition.passable)) {
        return context.Fail("locked terrain must be impassable without open/close transitions");
    }
    if (opensTo && closesTo) { return context.Fail("opens_to and closes_to are mutually exclusive"); }
    if ((looksLike && !ReadId(looksLike, terrain.looksLike, context.Child("looks_like")))
        || (opensTo && !ReadId(opensTo, terrain.opensTo, context.Child("opens_to")))
        || (closesTo && !ReadId(closesTo, terrain.closesTo, context.Child("closes_to")))
        || (locksTo && !ReadId(locksTo, terrain.locksTo, context.Child("locks_to")))
        || (unlocksTo && !ReadId(unlocksTo, terrain.unlocksTo, context.Child("unlocks_to")))) { return false; }
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

struct PendingTrait {
    String id;
    std::uint8_t tier = 0;
};

struct PendingInput {
    String item;
    std::uint8_t count = 0;
    RecipeInputFlags flags = RecipeInputFlags::None;
};

struct PendingOutput {
    String item;
    std::uint32_t count = 0;
};

struct PendingToolTrait {
    ToolTraitDefinition definition;
    std::size_t inputOrder = 0;
    Context context;
};

struct PendingCrafting {
    CraftingRecipeDefinition definition;
    Vector<PendingInput> inputs;
    Vector<PendingTrait> traits;
    PendingOutput result;
    std::size_t inputOrder = 0;
    Context context;
};

struct PendingRecycling {
    RecyclingRecipeDefinition definition;
    String target;
    String generatedFrom;
    std::uint32_t efficiency = 0;
    Vector<PendingInput> inputs;
    Vector<PendingTrait> traits;
    Vector<PendingOutput> returns;
    std::size_t inputOrder = 0;
    Context context;
};

bool ParseTraits(yyjson_val* array, Vector<PendingTrait>& output, const Context& context) {
    if (!array) { return true; }
    if (!yyjson_is_arr(array) || yyjson_arr_size(array) > maxRecipeTraits) {
        return context.Fail("expected an array of at most 4 tool traits");
    }
    std::size_t index, count;
    yyjson_val* value;
    yyjson_arr_foreach(array, index, count, value) {
        const auto child = ArrayElement(context, index);
        PendingTrait trait;
        std::uint32_t tier = 0;
        if (!CheckObject(value, {"id", "tier"}, child)
            || !ReadId(yyjson_obj_get(value, "id"), trait.id, child.Child("id"))
            || !ReadBoundedInteger(yyjson_obj_get(value, "tier"), 1, 255, tier, child.Child("tier"))) { return false; }
        trait.tier = static_cast<std::uint8_t>(tier);
        for (const auto& previous : output) {
            if (previous.id == trait.id) { return child.Child("id").Fail("duplicate tool trait id"); }
        }
        output.push_back(eastl::move(trait));
    }
    return true;
}

bool ParseInputs(yyjson_val* array, Vector<PendingInput>& output, const Context& context, bool required) {
    if (!array && !required) { return true; }
    if (!yyjson_is_arr(array) || yyjson_arr_size(array) > maxRecipeInputs) {
        return context.Fail("expected an array of at most 8 input item types including retained tools");
    }
    std::size_t index, count;
    yyjson_val* value;
    yyjson_arr_foreach(array, index, count, value) {
        const auto child = ArrayElement(context, index);
        PendingInput input;
        std::uint32_t amount = 0;
        bool retained = false, recoverable = true;
        if (!CheckObject(value, {"item", "count", "retained", "recoverable"}, child)
            || !ReadId(yyjson_obj_get(value, "item"), input.item, child.Child("item"))
            || !ReadBoundedInteger(yyjson_obj_get(value, "count"), 1, 255, amount, child.Child("count"))) { return false; }
        if (auto* field = yyjson_obj_get(value, "retained")) {
            if (!ReadBool(field, retained, child.Child("retained"))) { return false; }
        }
        if (auto* field = yyjson_obj_get(value, "recoverable")) {
            if (!ReadBool(field, recoverable, child.Child("recoverable"))) { return false; }
        }
        input.count = static_cast<std::uint8_t>(amount);
        input.flags = static_cast<RecipeInputFlags>((retained ? 1 : 0) | (recoverable ? 0 : 2));
        for (const auto& previous : output) {
            if (previous.item == input.item) { return child.Child("item").Fail("duplicate input item type"); }
        }
        output.push_back(eastl::move(input));
    }
    return true;
}

bool ParseOutput(yyjson_val* object, PendingOutput& output, const Context& context) {
    return CheckObject(object, {"item", "count"}, context)
        && ReadId(yyjson_obj_get(object, "item"), output.item, context.Child("item"))
        && ReadBoundedInteger(yyjson_obj_get(object, "count"), 1, 255, output.count, context.Child("count"));
}

bool ParseRecipeName(yyjson_val* object, String& name, const String& id, const Context& context) {
    name = id;
    auto* value = yyjson_obj_get(object, "name");
    return !value || ReadString(value, name, context.Child("name"));
}

bool ParseRecycling(yyjson_val* object, PendingRecycling& pending, bool generated) {
    const auto& context = pending.context;
    auto& definition = pending.definition;
    if (generated) {
        if (!CheckObject(object, {"id", "work", "efficiency", "inputs", "tool_traits"}, context)
            || !ReadBoundedInteger(yyjson_obj_get(object, "efficiency"), 0, ratioScale,
                                   pending.efficiency, context.Child("efficiency"))) { return false; }
    } else {
        if (!CheckObject(object, {"type", "id", "name", "target", "inputs", "tool_traits", "returns", "work"}, context)
            || !ReadId(yyjson_obj_get(object, "target"), pending.target, context.Child("target"))) { return false; }
        auto* returns = yyjson_obj_get(object, "returns");
        if (!yyjson_is_arr(returns) || yyjson_arr_size(returns) > maxRecipeInputs) {
            return context.Child("returns").Fail("expected an array of at most 8 return item types");
        }
        std::size_t index, count;
        yyjson_val* value;
        yyjson_arr_foreach(returns, index, count, value) {
            PendingOutput output;
            const auto child = ArrayElement(context.Child("returns"), index);
            if (!ParseOutput(value, output, child)) { return false; }
            for (const auto& previous : pending.returns) {
                if (previous.item == output.item) { return child.Child("item").Fail("duplicate return item type"); }
            }
            pending.returns.push_back(eastl::move(output));
        }
    }
    return ReadId(yyjson_obj_get(object, "id"), definition.id, context.Child("id"))
        && ParseRecipeName(object, definition.name, definition.id, context)
        && ReadBoundedInteger(yyjson_obj_get(object, "work"), 1, UINT32_MAX, definition.work, context.Child("work"))
        && ParseInputs(yyjson_obj_get(object, "inputs"), pending.inputs, context.Child("inputs"), false)
        && ParseTraits(yyjson_obj_get(object, "tool_traits"), pending.traits, context.Child("tool_traits"));
}

bool ParseCrafting(yyjson_val* object, PendingCrafting& pending) {
    auto& definition = pending.definition;
    const auto& context = pending.context;
    if (!CheckObject(object, {"type", "id", "name", "crafting_enabled", "inputs", "tool_traits", "result", "work", "recycling"}, context)
        || !ReadId(yyjson_obj_get(object, "id"), definition.id, context.Child("id"))
        || !ParseRecipeName(object, definition.name, definition.id, context)
        || !ReadBoundedInteger(yyjson_obj_get(object, "work"), 1, UINT32_MAX, definition.work, context.Child("work"))
        || !ParseInputs(yyjson_obj_get(object, "inputs"), pending.inputs, context.Child("inputs"), true)
        || !ParseTraits(yyjson_obj_get(object, "tool_traits"), pending.traits, context.Child("tool_traits"))
        || !ParseOutput(yyjson_obj_get(object, "result"), pending.result, context.Child("result"))) { return false; }
    auto* enabled = yyjson_obj_get(object, "crafting_enabled");
    return !enabled || ReadBool(enabled, definition.enabled, context.Child("crafting_enabled"));
}

struct PendingItem {
    ItemDefinition definition;
    Vector<PendingTrait> traits;
    String powerStore;
    std::size_t inputOrder = 0;
    Context context;
};

struct PendingObject {
    ObjectDefinition definition;
    String input;
    String output;
    std::size_t inputOrder = 0;
    Context context;
};

struct PendingAiProfile {
    AiProfile definition;
    std::size_t inputOrder = 0;
    Context context;
};

bool ParseAiProfile(yyjson_val* object, PendingAiProfile& pending) {
    const auto& context = pending.context;
    auto& profile = pending.definition;
    if (!CheckObject(object, {"id", "sight_range", "dim_range", "dark_range", "scan_interval_ticks",
                              "memory_ticks", "search_ticks", "candidate_budget", "bucket_budget",
                              "switch_advantage"}, context)
        || !ReadId(yyjson_obj_get(object, "id"), profile.id, context.Child("id"))) { return false; }
    struct Field { const char* name; std::uint32_t minimum, maximum; std::uint32_t* value; };
    const Field fields[] = {
        {"sight_range", 1, 32, &profile.sightRange}, {"dim_range", 0, 32, &profile.dimRange},
        {"dark_range", 0, 32, &profile.darkRange}, {"scan_interval_ticks", 1, 60000, &profile.scanIntervalTicks},
        {"memory_ticks", 1, 60000, &profile.memoryTicks}, {"search_ticks", 1, 60000, &profile.searchTicks},
        {"candidate_budget", 1, 32, &profile.candidateBudget}, {"bucket_budget", 1, 64, &profile.bucketBudget},
        {"switch_advantage", 0, 32, &profile.switchAdvantage},
    };
    for (const auto& field : fields) {
        if (auto* value = yyjson_obj_get(object, field.name)) {
            if (!ReadBoundedInteger(value, field.minimum, field.maximum, *field.value, context.Child(field.name))) {
                return false;
            }
        }
    }
    if (profile.dimRange > profile.sightRange) { return context.Child("dim_range").Fail("dim_range must not exceed sight_range"); }
    if (profile.darkRange > profile.dimRange) { return context.Child("dark_range").Fail("dark_range must not exceed dim_range"); }
    if (profile.searchTicks > profile.memoryTicks) { return context.Child("search_ticks").Fail("search_ticks must not exceed memory_ticks"); }
    return ValidAiProfile(profile);
}

bool ParseObject(yyjson_val* object, PendingObject& pending) {
    const auto& context = pending.context;
    auto& definition = pending.definition;
    if (!CheckObject(object, {"id", "name", "behavior", "machine"}, context)
        || !ReadId(yyjson_obj_get(object, "id"), definition.id, context.Child("id"))
        || !ReadString(yyjson_obj_get(object, "name"), definition.name, context.Child("name"))) { return false; }
    auto* behavior = yyjson_obj_get(object, "behavior");
    if (!yyjson_is_str(behavior)) { return context.Child("behavior").Fail("expected a behavior kind string"); }
    if (JsonString(behavior) == "none") {
        if (yyjson_obj_get(object, "machine")) { return context.Child("machine").Fail("none behavior has no machine config"); }
        return true;
    }
    if (JsonString(behavior) != "crafting_machine") { return context.Child("behavior").Fail("unknown object behavior kind"); }
    definition.behavior = ObjectBehaviorKind::CraftingMachine;
    auto* machine = yyjson_obj_get(object, "machine");
    const auto machineContext = context.Child("machine");
    auto& config = definition.machine;
    std::uint32_t duration = 0;
    if (!CheckObject(machine, {"input", "input_count", "output", "output_count", "duration_ticks", "energy_capacity", "energy_per_tick"}, machineContext)
        || !ReadId(yyjson_obj_get(machine, "input"), pending.input, machineContext.Child("input"))
        || !ReadId(yyjson_obj_get(machine, "output"), pending.output, machineContext.Child("output"))
        || !ReadBoundedInteger(yyjson_obj_get(machine, "input_count"), 1, 1024, config.inputCount, machineContext.Child("input_count"))
        || !ReadBoundedInteger(yyjson_obj_get(machine, "output_count"), 1, 1024, config.outputCount, machineContext.Child("output_count"))
        || !ReadBoundedInteger(yyjson_obj_get(machine, "duration_ticks"), 1, std::numeric_limits<std::uint32_t>::max(), duration, machineContext.Child("duration_ticks"))
        || !ReadBoundedInteger(yyjson_obj_get(machine, "energy_per_tick"), 1, std::numeric_limits<std::uint32_t>::max(), config.energyPerTick, machineContext.Child("energy_per_tick"))) { return false; }
    config.duration = duration;
    auto* capacity = yyjson_obj_get(machine, "energy_capacity");
    if (!yyjson_is_uint(capacity) || yyjson_get_uint(capacity) < config.energyPerTick
        || yyjson_get_uint(capacity) > std::numeric_limits<std::uint64_t>::max() / 2) {
        return machineContext.Child("energy_capacity").Fail("expected energy_per_tick <= capacity <= UINT64_MAX / 2");
    }
    config.energyCapacity = yyjson_get_uint(capacity);
    return true;
}

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
    if (!CheckObject(object, {"type", "id", "name", "mass_g", "volume_ml", "capabilities", "tool_traits", "recovery_allowed", "crafting_power"}, context)
        || !ReadId(yyjson_obj_get(object, "id"), definition.id, context.Child("id"))
        || !ReadString(yyjson_obj_get(object, "name"), definition.name, context.Child("name"))
        || !ReadItemInteger(object, "mass_g", 0, definition.massGrams, context)
        || !ReadItemInteger(object, "volume_ml", 1, definition.volumeMl, context)) { return false; }
    if (!ParseTraits(yyjson_obj_get(object, "tool_traits"), item.traits, context.Child("tool_traits"))) { return false; }
    if (auto* recovery = yyjson_obj_get(object, "recovery_allowed")) {
        if (!ReadBool(recovery, definition.recoveryAllowed, context.Child("recovery_allowed"))) { return false; }
    }
    if (auto* power = yyjson_obj_get(object, "crafting_power")) {
        const auto child = context.Child("crafting_power");
        if (!CheckObject(power, {"store", "energy_per_work"}, child)
            || !ReadId(yyjson_obj_get(power, "store"), item.powerStore, child.Child("store"))
            || !ReadBoundedInteger(yyjson_obj_get(power, "energy_per_work"), 1, UINT32_MAX,
                                   definition.craftingEnergyPerWork, child.Child("energy_per_work"))) { return false; }
    }
    auto* capabilities = yyjson_obj_get(object, "capabilities");
    if (!capabilities) {
        return item.powerStore.empty() || context.Child("crafting_power").Child("store").Fail("crafting power store must name this item's energy store");
    }
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
    if (!item.powerStore.empty()) {
        definition.craftingPower = FindItemCapability(definition, View(item.powerStore));
        if (!ItemEnergyStore(definition, definition.craftingPower)) {
            return context.Child("crafting_power").Child("store").Fail("crafting power store must name this item's energy store");
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
                                Vector<PendingObject>& objects, Vector<PendingAiProfile>& aiProfiles,
                                Vector<PendingCrafting>& recipes, Vector<PendingRecycling>& recyclers,
                                Vector<PendingToolTrait>& traits,
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
    if (!CheckObject(root, {"format_version", "definitions", "objects", "ai_profiles"}, context)) {
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
    auto* profileArray = yyjson_obj_get(root, "ai_profiles");
    if (profileArray) {
        if (!yyjson_is_arr(profileArray)) {
            context.Child("ai_profiles").Fail("expected an array");
            return eastl::unexpected(ContentError::InvalidData);
        }
        std::size_t index, count;
        yyjson_val* value;
        yyjson_arr_foreach(profileArray, index, count, value) {
            PendingAiProfile profile;
            profile.context = ArrayElement(context.Child("ai_profiles"), index);
            profile.inputOrder = pending.size() + patterns.size() + items.size() + objects.size() + aiProfiles.size()
                + recipes.size() + recyclers.size() + traits.size();
            if (!ParseAiProfile(value, profile)) {
                profile.context.Fail("while loading AI profile definition");
                return eastl::unexpected(ContentError::InvalidData);
            }
            aiProfiles.push_back(eastl::move(profile));
        }
    }
    auto* objectArray = yyjson_obj_get(root, "objects");
    if (objectArray) {
        if (!yyjson_is_arr(objectArray)) {
            context.Child("objects").Fail("expected an array");
            return eastl::unexpected(ContentError::InvalidData);
        }
        std::size_t objectIndex, objectCount;
        yyjson_val* value;
        yyjson_arr_foreach(objectArray, objectIndex, objectCount, value) {
            PendingObject object;
            object.context = ArrayElement(context.Child("objects"), objectIndex);
            object.inputOrder = pending.size() + patterns.size() + items.size() + objects.size() + aiProfiles.size()
                + recipes.size() + recyclers.size() + traits.size();
            if (!ParseObject(value, object)) {
                object.context.Fail("while loading object definition");
                return eastl::unexpected(ContentError::InvalidData);
            }
            objects.push_back(eastl::move(object));
        }
    }
    auto* definitions = yyjson_obj_get(root, "definitions");
    if (!definitions && (objectArray || profileArray)) { return true; }
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
        terrain.inputOrder = pending.size() + patterns.size() + items.size() + objects.size() + aiProfiles.size()
            + recipes.size() + recyclers.size() + traits.size();
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
            } else if (JsonString(type) == "tool_trait") {
                PendingToolTrait trait;
                trait.context = definitionContext;
                trait.inputOrder = terrain.inputOrder;
                if (!CheckObject(value, {"type", "id", "name"}, trait.context)
                    || !ReadId(yyjson_obj_get(value, "id"), trait.definition.id, trait.context.Child("id"))
                    || !ReadString(yyjson_obj_get(value, "name"), trait.definition.name, trait.context.Child("name"))) {
                    return eastl::unexpected(ContentError::InvalidData);
                }
                traits.push_back(eastl::move(trait));
                continue;
            } else if (JsonString(type) == "recipe") {
                PendingCrafting recipe;
                recipe.context = definitionContext;
                recipe.inputOrder = terrain.inputOrder;
                if (!ParseCrafting(value, recipe)) { return eastl::unexpected(ContentError::InvalidData); }
                if (auto* recycling = yyjson_obj_get(value, "recycling")) {
                    PendingRecycling recycler;
                    recycler.context = definitionContext.Child("recycling");
                    recycler.inputOrder = recipe.inputOrder + 1;
                    recycler.generatedFrom = recipe.definition.id;
                    if (!ParseRecycling(recycling, recycler, true)) { return eastl::unexpected(ContentError::InvalidData); }
                    if (recipe.result.count != 1) {
                        recycler.context.Fail("generated recycling requires result.count == 1");
                        return eastl::unexpected(ContentError::InvalidData);
                    }
                    recyclers.push_back(eastl::move(recycler));
                }
                recipes.push_back(eastl::move(recipe));
                continue;
            } else if (JsonString(type) == "recycling_recipe") {
                PendingRecycling recycler;
                recycler.context = definitionContext;
                recycler.inputOrder = terrain.inputOrder;
                if (!ParseRecycling(value, recycler, false)) { return eastl::unexpected(ContentError::InvalidData); }
                recyclers.push_back(eastl::move(recycler));
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
                message += "' (expected terrain, building_pattern, item, tool_trait, recipe, or recycling_recipe)";
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
        if (!terrain.locksTo.empty()) {
            const auto target = FindPending(pending, View(terrain.locksTo));
            if (target == absent) { return MissingReference(terrain, "locks_to", terrain.locksTo); }
            terrain.definition.locksTo = {static_cast<std::uint32_t>(target + 1)};
        }
        if (!terrain.unlocksTo.empty()) {
            const auto target = FindPending(pending, View(terrain.unlocksTo));
            if (target == absent) { return MissingReference(terrain, "unlocks_to", terrain.unlocksTo); }
            terrain.definition.unlocksTo = {static_cast<std::uint32_t>(target + 1)};
        }
        if (!terrain.closesTo.empty()) {
            const std::size_t target = FindPending(pending, View(terrain.closesTo));
            if (target == absent) { return MissingReference(terrain, "closes_to", terrain.closesTo); }
            terrain.definition.closesTo = {static_cast<std::uint32_t>(target + 1)};
        }
    }
    for (std::size_t i = 0; i < pending.size(); ++i) {
        const auto& terrain = pending[i];
        const auto& definition = terrain.definition;
        if (definition.locksTo.value) {
            const auto& locked = pending[definition.locksTo.value - 1].definition;
            const auto closed = definition.closesTo.value ? definition.closesTo : TerrainId{static_cast<std::uint32_t>(i + 1)};
            if (locked.unlocksTo != closed) { return terrain.context.Fail("locks_to must unlock back to this door's closed state"); }
        }
        if (definition.unlocksTo.value) {
            const auto& unlocked = pending[definition.unlocksTo.value - 1].definition;
            if (unlocked.locksTo.value != i + 1 || !unlocked.opensTo.value || unlocked.passable) {
                return terrain.context.Fail("unlocks_to must name a closed openable door that locks back to this state");
            }
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
    bool object = false;
    bool aiProfile = false;
    bool craftingContent = false;
};

bool CheckDefinitionIds(const Vector<PendingTerrain>& terrains, const Vector<PendingPattern>& patterns,
                          const Vector<PendingItem>& items, const Vector<PendingObject>& objects,
                          const Vector<PendingAiProfile>& aiProfiles, const Vector<PendingCrafting>& recipes,
                          const Vector<PendingRecycling>& recyclers, const Vector<PendingToolTrait>& traits,
                          Diagnostics* diagnostics) {
    Vector<DefinitionIdentity> identities;
    identities.reserve(terrains.size() + patterns.size() + items.size() + objects.size() + aiProfiles.size());
    for (const auto& terrain : terrains) {
        identities.push_back({View(terrain.definition.id), &terrain.context, terrain.inputOrder, false});
    }
    for (const auto& pattern : patterns) {
        identities.push_back({View(pattern.definition.id), &pattern.context, pattern.inputOrder, true});
    }
    for (const auto& item : items) {
        identities.push_back({View(item.definition.id), &item.context, item.inputOrder, false, true});
    }
    for (const auto& object : objects) {
        identities.push_back({View(object.definition.id), &object.context, object.inputOrder, false, false, true});
    }
    for (const auto& profile : aiProfiles) {
        identities.push_back({View(profile.definition.id), &profile.context, profile.inputOrder, false, false, false, true});
    }
    for (const auto& recipe : recipes) { identities.push_back({View(recipe.definition.id), &recipe.context, recipe.inputOrder, false, false, false, false, true}); }
    for (const auto& recycler : recyclers) { identities.push_back({View(recycler.definition.id), &recycler.context, recycler.inputOrder, false, false, false, false, true}); }
    for (const auto& trait : traits) { identities.push_back({View(trait.definition.id), &trait.context, trait.inputOrder, false, false, false, false, true}); }
    eastl::sort(identities.begin(), identities.end(), [](const DefinitionIdentity& left, const DefinitionIdentity& right) {
        if (left.id == right.id) { return left.inputOrder < right.inputOrder; }
        return left.id < right.id;
    });
    for (std::size_t i = 1; i < identities.size(); ++i) {
        const auto& current = identities[i];
        const auto& previous = identities[i - 1];
        if (previous.id != current.id) { continue; }
        if (diagnostics) {
            String message = current.craftingContent ? "duplicate content definition '" : current.aiProfile ? "duplicate AI profile definition '" : current.object ? "duplicate object definition '" : current.item ? "duplicate item definition '" :
                (current.pattern ? "duplicate building pattern definition '" : "duplicate terrain definition '");
            message.append(current.id.data(), current.id.size());
            message += '\'';
            current.context->Child("id").Fail(View(message));
            previous.context->Child("id").Fail("previous definition is here");
            if (current.craftingContent) { current.context->Fail("while loading crafting content definition"); }
            else if (current.aiProfile) { current.context->Fail("while loading AI profile definition"); }
            else if (current.object) { current.context->Fail("while loading object definition"); }
            else if (current.item) { AddItemContext(*current.context, current.id); }
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

template<class Definition>
std::size_t FindDefinition(const Vector<Definition>& definitions, eastl::string_view id) {
    std::size_t begin = 0, end = definitions.size();
    while (begin < end) {
        const auto middle = begin + (end - begin) / 2;
        if (View(definitions[middle].id) < id) { begin = middle + 1; }
        else { end = middle; }
    }
    return begin < definitions.size() && View(definitions[begin].id) == id ? begin + 1 : 0;
}

bool ResolveTraits(const Vector<PendingTrait>& pending, StaticList<TraitRequirement, maxRecipeTraits>& output,
                   const ContentCatalog& catalog, const Context& context) {
    for (std::size_t i = 0; i < pending.size(); ++i) {
        const auto& trait = pending[i];
        const auto id = catalog.FindToolTrait(View(trait.id));
        if (!id.value) {
            if (context.diagnostics) {
                String message = "unknown tool trait '";
                message += trait.id;
                message += '\'';
                ArrayElement(context, i).Child("id").Fail(View(message));
            }
            return false;
        }
        if (!output.PushBack({id, trait.tier})) { std::abort(); }
    }
    eastl::sort(output.begin(), output.end(), [](const TraitRequirement& left, const TraitRequirement& right) {
        return left.trait.value < right.trait.value;
    });
    return true;
}

bool ResolveItem(const String& name, ItemTypeId& output, const ContentCatalog& catalog, const Context& context) {
    output = catalog.FindItem(View(name));
    if (output.value && output.value <= maxRecipeItemId) {
        if (catalog.GetItem(output).unfinishedCraft) {
            return context.Fail("internal item 'unfinished_craft' cannot be referenced by authored recipes");
        }
        return true;
    }
    if (context.diagnostics) {
        String message = "unknown or out-of-range recipe item '";
        message += name;
        message += '\'';
        context.Fail(View(message));
    }
    return false;
}

bool ResolveInputs(const Vector<PendingInput>& pending, StaticList<RecipeInput, maxRecipeInputs>& output,
                   const ContentCatalog& catalog, const Context& context) {
    for (std::size_t i = 0; i < pending.size(); ++i) {
        const auto& input = pending[i];
        ItemTypeId item;
        if (!ResolveItem(input.item, item, catalog, ArrayElement(context, i).Child("item"))) { return false; }
        if (!output.PushBack(RecipeInput{item, input.count, input.flags})) { std::abort(); }
    }
    eastl::sort(output.begin(), output.end(), [](const RecipeInput& left, const RecipeInput& right) {
        return left.Item().value < right.Item().value;
    });
    return true;
}

bool ResolveCrafting(PendingCrafting& pending, const ContentCatalog& catalog) {
    auto& definition = pending.definition;
    definition.result.count = pending.result.count;
    return ResolveInputs(pending.inputs, definition.inputs, catalog, pending.context.Child("inputs"))
        && ResolveTraits(pending.traits, definition.toolTraits, catalog, pending.context.Child("tool_traits"))
        && ResolveItem(pending.result.item, definition.result.item, catalog, pending.context.Child("result").Child("item"));
}

bool ResolveRecycling(PendingRecycling& pending, const ContentCatalog& catalog) {
    auto& definition = pending.definition;
    if (!ResolveInputs(pending.inputs, definition.inputs, catalog, pending.context.Child("inputs"))
        || !ResolveTraits(pending.traits, definition.toolTraits, catalog, pending.context.Child("tool_traits"))) { return false; }
    if (!pending.generatedFrom.empty()) {
        const auto& recipe = catalog.GetCraftingRecipe(catalog.FindCraftingRecipe(View(pending.generatedFrom)));
        definition.target = recipe.result.item;
        for (const auto& input : recipe.inputs) {
            if (input.Retained() || input.NoRecovery() || !catalog.GetItem(input.Item()).recoveryAllowed) { continue; }
            const auto amount = static_cast<std::uint32_t>(ScaleRatioFloor(input.Count(), pending.efficiency));
            if (amount && !definition.returns.PushBack({input.Item(), amount})) { std::abort(); }
        }
    } else {
        if (!ResolveItem(pending.target, definition.target, catalog, pending.context.Child("target"))) { return false; }
        for (std::size_t i = 0; i < pending.returns.size(); ++i) {
            const auto& output = pending.returns[i];
            const auto child = ArrayElement(pending.context.Child("returns"), i);
            ItemTypeId item;
            if (!ResolveItem(output.item, item, catalog, child.Child("item"))) { return false; }
            if (!catalog.GetItem(item).recoveryAllowed) {
                return child.Child("item").Fail("explicit recycling return violates item's recovery_allowed:false");
            }
            if (!definition.returns.PushBack({item, output.count})) { std::abort(); }
        }
    }
    eastl::sort(definition.returns.begin(), definition.returns.end(), [](const ItemOutput& left, const ItemOutput& right) {
        return left.item.value < right.item.value;
    });
    return true;
}

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

ObjectTypeId ContentCatalog::FindObject(eastl::string_view id) const {
    std::size_t begin = 0, end = objects_.size();
    while (begin < end) {
        const std::size_t middle = begin + (end - begin) / 2;
        if (View(objects_[middle].id) < id) { begin = middle + 1; }
        else { end = middle; }
    }
    if (begin == objects_.size() || View(objects_[begin].id) != id) { return {}; }
    return {static_cast<std::uint32_t>(begin + 1)};
}

const ObjectDefinition* ContentCatalog::TryGetObject(ObjectTypeId id) const {
    if (id.value == 0 || id.value > objects_.size()) { return nullptr; }
    return &objects_[id.value - 1];
}

const ObjectDefinition& ContentCatalog::GetObject(ObjectTypeId id) const {
    const auto* definition = TryGetObject(id);
    if (!definition) { std::abort(); }
    return *definition;
}

std::size_t ContentCatalog::ObjectCount() const { return objects_.size(); }

AiProfileId ContentCatalog::FindAiProfile(eastl::string_view id) const {
    std::size_t begin = 0, end = aiProfiles_.size();
    while (begin < end) {
        const std::size_t middle = begin + (end - begin) / 2;
        if (View(aiProfiles_[middle].id) < id) { begin = middle + 1; }
        else { end = middle; }
    }
    if (begin == aiProfiles_.size() || View(aiProfiles_[begin].id) != id) { return {}; }
    return {static_cast<std::uint32_t>(begin + 1)};
}

const AiProfile* ContentCatalog::TryGetAiProfile(AiProfileId id) const {
    if (id.value == 0 || id.value > aiProfiles_.size()) { return nullptr; }
    return &aiProfiles_[id.value - 1];
}

const AiProfile& ContentCatalog::GetAiProfile(AiProfileId id) const {
    const auto* profile = TryGetAiProfile(id);
    if (!profile) { std::abort(); }
    return *profile;
}

std::size_t ContentCatalog::AiProfileCount() const { return aiProfiles_.size(); }

CraftingRecipeId ContentCatalog::FindCraftingRecipe(eastl::string_view id) const {
    return {static_cast<std::uint32_t>(FindDefinition(craftingRecipes_, id))};
}
const CraftingRecipeDefinition* ContentCatalog::TryGetCraftingRecipe(CraftingRecipeId id) const {
    if (!id.value || id.value > craftingRecipes_.size()) { return nullptr; }
    return &craftingRecipes_[id.value - 1];
}
const CraftingRecipeDefinition& ContentCatalog::GetCraftingRecipe(CraftingRecipeId id) const {
    const auto* definition = TryGetCraftingRecipe(id);
    if (!definition) { std::abort(); }
    return *definition;
}
std::size_t ContentCatalog::CraftingRecipeCount() const { return craftingRecipes_.size(); }

RecyclingRecipeId ContentCatalog::FindRecyclingRecipe(eastl::string_view id) const {
    return {static_cast<std::uint32_t>(FindDefinition(recyclingRecipes_, id))};
}
const RecyclingRecipeDefinition* ContentCatalog::TryGetRecyclingRecipe(RecyclingRecipeId id) const {
    if (!id.value || id.value > recyclingRecipes_.size()) { return nullptr; }
    return &recyclingRecipes_[id.value - 1];
}
const RecyclingRecipeDefinition& ContentCatalog::GetRecyclingRecipe(RecyclingRecipeId id) const {
    const auto* definition = TryGetRecyclingRecipe(id);
    if (!definition) { std::abort(); }
    return *definition;
}
std::size_t ContentCatalog::RecyclingRecipeCount() const { return recyclingRecipes_.size(); }
RecyclingRecipeId ContentCatalog::FindRecyclingFor(ItemTypeId item) const {
    for (std::size_t i = 0; i < recyclingRecipes_.size(); ++i) {
        if (recyclingRecipes_[i].target == item) { return {static_cast<std::uint32_t>(i + 1)}; }
    }
    return {};
}

ToolTraitId ContentCatalog::FindToolTrait(eastl::string_view id) const {
    return {static_cast<std::uint8_t>(FindDefinition(toolTraits_, id))};
}
const ToolTraitDefinition* ContentCatalog::TryGetToolTrait(ToolTraitId id) const {
    if (!id.value || id.value > toolTraits_.size()) { return nullptr; }
    return &toolTraits_[id.value - 1];
}
const ToolTraitDefinition& ContentCatalog::GetToolTrait(ToolTraitId id) const {
    const auto* definition = TryGetToolTrait(id);
    if (!definition) { std::abort(); }
    return *definition;
}
std::size_t ContentCatalog::ToolTraitCount() const { return toolTraits_.size(); }
ItemTypeId ContentCatalog::UnfinishedCraftItem() const { return unfinishedCraftItem_; }

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
    Vector<PendingObject> objects;
    Vector<PendingAiProfile> aiProfiles;
    Vector<PendingCrafting> recipes;
    Vector<PendingRecycling> recyclers;
    Vector<PendingToolTrait> traits;
    for (const auto& source : sources) {
        auto result = ParseSource(source, pending, patterns, items, objects, aiProfiles, recipes, recyclers, traits, registry, diagnostics);
        if (!result) {
            Context{diagnostics, source.name, {}}.Fail("could not load content source");
            return eastl::unexpected(result.error());
        }
    }
    if (!recipes.empty() || !recyclers.empty()) {
        PendingItem internal;
        internal.context = Context{diagnostics, "<generated unfinished craft>", {}};
        internal.inputOrder = pending.size() + patterns.size() + items.size() + objects.size()
            + aiProfiles.size() + recipes.size() + recyclers.size() + traits.size();
        auto& definition = internal.definition;
        definition.id = "unfinished_craft";
        definition.name = "unfinished craft";
        definition.volumeMl = 1;
        definition.unfinishedCraft = true;
        definition.container = ItemContainerDefinition{{1}, {UINT32_MAX}};
        ItemCapabilityDefinition capability;
        capability.id = "container";
        capability.type = "container";
        capability.config.SetString("id", "container");
        capability.config.SetString("type", "container");
        capability.config.SetUnsigned("capacity_volume_ml", UINT32_MAX);
        capability.config = CanonicalContentData(capability.config);
        definition.capabilities.push_back(eastl::move(capability));
        items.push_back(eastl::move(internal));
    }
    if (pending.size() > std::numeric_limits<std::uint32_t>::max()
        || patterns.size() > std::numeric_limits<std::uint32_t>::max()
        || items.size() > std::numeric_limits<std::uint32_t>::max()
        || objects.size() > std::numeric_limits<std::uint32_t>::max()
        || aiProfiles.size() > std::numeric_limits<std::uint32_t>::max()
        || recipes.size() > UINT32_MAX || recyclers.size() > UINT32_MAX || traits.size() > 255
        || ((!recipes.empty() || !recyclers.empty()) && items.size() > maxRecipeItemId)) {
        Context{diagnostics, {}, {}}.Fail("too many content definitions");
        Context{diagnostics, {}, {}}.Fail("could not load content catalog");
        return eastl::unexpected(ContentError::InvalidData);
    }
    eastl::sort(pending.begin(), pending.end(), [](const PendingTerrain& left, const PendingTerrain& right) {
        // Retain input order for equal IDs so duplicate diagnostics identify the prior source reliably.
        if (left.definition.id == right.definition.id) { return left.inputOrder < right.inputOrder; }
        return left.definition.id < right.definition.id;
    });
    if (!CheckDefinitionIds(pending, patterns, items, objects, aiProfiles, recipes, recyclers, traits, diagnostics)) { return eastl::unexpected(ContentError::InvalidData); }
    if (!Resolve(pending)) {
        Context{diagnostics, {}, {}}.Fail("could not load content catalog");
        return eastl::unexpected(ContentError::InvalidData);
    }
    ContentCatalog catalog;
    eastl::sort(traits.begin(), traits.end(), [](const PendingToolTrait& left, const PendingToolTrait& right) {
        return left.definition.id < right.definition.id;
    });
    for (auto& trait : traits) { catalog.toolTraits_.push_back(eastl::move(trait.definition)); }
    eastl::sort(aiProfiles.begin(), aiProfiles.end(), [](const PendingAiProfile& left, const PendingAiProfile& right) {
        return left.definition.id < right.definition.id;
    });
    catalog.aiProfiles_.reserve(aiProfiles.size());
    for (auto& profile : aiProfiles) { catalog.aiProfiles_.push_back(eastl::move(profile.definition)); }
    eastl::sort(items.begin(), items.end(), [](const PendingItem& left, const PendingItem& right) {
        return left.definition.id < right.definition.id;
    });
    catalog.items_.reserve(items.size());
    for (auto& item : items) {
        if (!ResolveTraits(item.traits, item.definition.toolTraits, catalog, item.context.Child("tool_traits"))) {
            return eastl::unexpected(ContentError::InvalidData);
        }
        catalog.items_.push_back(eastl::move(item.definition));
    }
    if (!recipes.empty() || !recyclers.empty()) { catalog.unfinishedCraftItem_ = catalog.FindItem("unfinished_craft"); }
    eastl::sort(recipes.begin(), recipes.end(), [](const PendingCrafting& left, const PendingCrafting& right) {
        return left.definition.id < right.definition.id;
    });
    for (auto& recipe : recipes) {
        if (!ResolveCrafting(recipe, catalog)) { return eastl::unexpected(ContentError::InvalidData); }
        catalog.craftingRecipes_.push_back(eastl::move(recipe.definition));
    }
    eastl::sort(recyclers.begin(), recyclers.end(), [](const PendingRecycling& left, const PendingRecycling& right) {
        return left.definition.id < right.definition.id;
    });
    for (std::size_t i = 0; i < recyclers.size(); ++i) {
        auto& recycler = recyclers[i];
        if (!ResolveRecycling(recycler, catalog)) { return eastl::unexpected(ContentError::InvalidData); }
        const auto previous = catalog.FindRecyclingFor(recycler.definition.target);
        if (previous.value) {
            if (diagnostics) {
                String message = "recycling target conflict between '";
                message += catalog.GetRecyclingRecipe(previous).id;
                message += "' and '";
                message += recycler.definition.id;
                message += "' for item '";
                message += catalog.GetItem(recycler.definition.target).id;
                message += '\'';
                recycler.context.Fail(View(message));
                recyclers[previous.value - 1].context.Fail("previous recycler is here");
            }
            return eastl::unexpected(ContentError::InvalidData);
        }
        catalog.recyclingRecipes_.push_back(eastl::move(recycler.definition));
    }
    eastl::sort(objects.begin(), objects.end(), [](const PendingObject& left, const PendingObject& right) {
        return left.definition.id < right.definition.id;
    });
    catalog.objects_.reserve(objects.size());
    for (auto& object : objects) {
        if (object.definition.behavior == ObjectBehaviorKind::CraftingMachine) {
            object.definition.machine.input = catalog.FindItem(View(object.input));
            object.definition.machine.output = catalog.FindItem(View(object.output));
            if (object.definition.machine.input == ItemTypeId{}) {
                object.context.Child("machine").Child("input").Fail("unknown input item reference");
                return eastl::unexpected(ContentError::InvalidData);
            }
            if (object.definition.machine.output == ItemTypeId{}) {
                object.context.Child("machine").Child("output").Fail("unknown output item reference");
                return eastl::unexpected(ContentError::InvalidData);
            }
        }
        if (!ValidObjectDefinition(object.definition)) {
            object.context.Fail("invalid object definition");
            return eastl::unexpected(ContentError::InvalidData);
        }
        catalog.objects_.push_back(eastl::move(object.definition));
    }
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
