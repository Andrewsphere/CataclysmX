#include "content/content.h"
#include "items/item_registry.h"

#include <EASTL/utility.h>
#include <yyjson.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

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

DataObject Numbers(std::initializer_list<std::uint64_t> values) {
    DataObject out = DataObject::List();
    for (auto value : values) { out.Append(DataObject::Unsigned(value)); }
    return out;
}
DataObject WriteReach(const ReachProfile& value) {
    return Numbers({value.distance, static_cast<unsigned>(value.height), value.regions});
}
DataObject WriteSource(const SourceRequirement& value) {
    return Numbers({static_cast<unsigned>(value.kind), static_cast<unsigned>(value.hand),
        static_cast<unsigned>(value.freeHands), static_cast<unsigned>(value.functionalHands),
        static_cast<unsigned>(value.functionalLegs), value.requiresGrip, static_cast<unsigned>(value.grip), value.requiresNaturalApparatus});
}
DataObject WriteCost(const ActionCost& value) {
    return Numbers({value.windupCost, value.recoveryCost, value.staminaCost});
}
DataObject WriteCombatTraining(const CombatTrainingDefinition& value) {
    return Numbers({value.work, value.meleeShare, value.handlingShare, value.artXpPerWork});
}
DataObject WriteUnlock(const MoveUnlock& value, const ContentCatalog& catalog) {
    DataObject out;
    out.SetUnsigned("art_xp", value.artXp);
    DataObject skills = DataObject::List();
    for (const auto& skill : value.skills) {
        DataObject entry;
        entry.SetString("skill", catalog.GetSkill(skill.skill).id); entry.SetUnsigned("level", skill.level);
        skills.Append(eastl::move(entry));
    }
    out.Set("skills", eastl::move(skills));
    return out;
}
DataObject WriteDamage(eastl::span<const DamagePacket> packets) {
    DataObject out = DataObject::List();
    for (const auto& packet : packets) { out.Append(Numbers({static_cast<unsigned>(packet.type), packet.amount, packet.penetration})); }
    return out;
}
DataObject WriteGrip(const GripDefinition& value) {
    DataObject out;
    out.Set("ratios", Numbers({static_cast<unsigned>(value.kind), value.damageRatio, value.costRatio, value.staminaRatio, value.blockRatio}));
    out.SetSigned("rating_bonus100", value.ratingBonus100);
    return out;
}
}

DataObject WriteCombatContent(const ContentCatalog& catalog) {
    DataObject out;
    out.SetUnsigned("version", 3);
    DataObject skills = DataObject::List();
    const auto bindings = catalog.GetCombatSkills();
    for (auto skill : {bindings.melee, bindings.unarmed, bindings.knives, bindings.swords, bindings.clubsHammers,
            bindings.axes, bindings.polearms, bindings.shields, bindings.dodge}) {
        skills.Append(DataObject::Text(skill.value ? eastl::string_view(catalog.GetSkill(skill).id) : eastl::string_view{}));
    }
    out.Set("skills", eastl::move(skills));
    DataObject moves = DataObject::List();
    for (std::uint32_t i = 1; i <= catalog.CombatMoveCount(); ++i) {
        const auto& move = catalog.GetCombatMove({i});
        DataObject entry;
        entry.SetString("id", move.id); entry.SetString("name", move.name);
        entry.SetUnsigned("trigger", static_cast<unsigned>(move.trigger)); entry.SetUnsigned("execution", static_cast<unsigned>(move.execution));
        const auto& req = move.requirements;
        entry.Set("primary", WriteSource(req.primary)); entry.Set("secondary", WriteSource(req.secondary));
        entry.Set("requirements", Numbers({req.paired, req.requiresAwareness, req.requiresOpening,
            req.allowedDuringRecovery, req.allowedDuringWindup, req.exhaustedOnly}));
        DataObject handling = DataObject::List();
        for (auto skill : req.handlingSkills) { handling.Append(DataObject::Text(catalog.GetSkill(skill).id)); }
        entry.Set("handling_skills", eastl::move(handling)); entry.Set("unlock", WriteUnlock(move.unlock, catalog));
        entry.Set("cost", WriteCost(move.cost)); entry.SetUnsigned("cooldown", move.cooldown);
        DataObject strikes = DataObject::List();
        for (const auto& strike : move.strikes) {
            DataObject s;
            s.SetUnsigned("source", static_cast<unsigned>(strike.source)); s.Set("reach", WriteReach(strike.reach));
            DataObject contacts = DataObject::List();
            for (const auto& contact : strike.contacts) { contacts.Append(Numbers({static_cast<unsigned>(contact.region), contact.weight})); }
            s.Set("contacts", eastl::move(contacts)); s.Set("damage", WriteDamage({strike.damage.data(), strike.damage.size()}));
            s.SetUnsigned("damage_ratio", strike.damageRatio); s.SetSigned("rating_bonus100", strike.ratingBonus100);
            s.SetUnsigned("intrinsic_miss", strike.intrinsicMiss); s.SetUnsigned("defenses", static_cast<unsigned>(strike.defenses));
            strikes.Append(eastl::move(s));
        }
        entry.Set("strikes", eastl::move(strikes));
        entry.Set("defense", Numbers({static_cast<unsigned>(move.defense.kind), move.defense.blockCapacity,
            move.defense.usesSourceCapacity, move.defense.permitsArmedAttack}));
        entry.SetSigned("defense_rating_bonus100", move.defense.ratingBonus100);
        entry.SetUnsigned("expose_opening", move.preparation.exposeOpening);
        entry.Set("follow_through", Numbers({static_cast<unsigned>(move.followThrough.condition), move.followThrough.stagger,
            move.followThrough.knockbackTiles, move.followThrough.counterOpening}));
        entry.Set("training", WriteCombatTraining(move.training)); moves.Append(eastl::move(entry));
    }
    out.Set("moves", eastl::move(moves));
    DataObject styles = DataObject::List();
    for (std::uint32_t i = 1; i <= catalog.CombatStyleCount(); ++i) {
        const auto& style = catalog.GetCombatStyle({i});
        DataObject entry;
        entry.SetString("id", style.id); entry.SetString("name", style.name);
        entry.SetString("art", style.art.value ? eastl::string_view(catalog.GetCombatArt(style.art).id) : eastl::string_view{});
        entry.SetUnsigned("switch_cost", style.switchCost);
        DataObject excluded = DataObject::List();
        for (auto skill : style.excludedAttackHandlingSkills) { excluded.Append(DataObject::Text(catalog.GetSkill(skill).id)); }
        entry.Set("exclude_attack_handling_skills", eastl::move(excluded));
        DataObject weighted = DataObject::List();
        for (const auto& move : style.moves) {
            DataObject m; m.SetString("move", catalog.GetCombatMove(move.move).id); m.SetUnsigned("weight", move.weight); weighted.Append(eastl::move(m));
        }
        entry.Set("moves", eastl::move(weighted)); styles.Append(eastl::move(entry));
    }
    out.Set("styles", eastl::move(styles));
    DataObject arts = DataObject::List();
    for (std::uint32_t i = 1; i <= catalog.CombatArtCount(); ++i) {
        const auto& art = catalog.GetCombatArt({i}); DataObject entry;
        entry.SetString("id", art.id); entry.SetString("name", art.name);
        DataObject styleNames = DataObject::List();
        for (auto style : art.styles) { styleNames.Append(DataObject::Text(catalog.GetCombatStyle(style).id)); }
        entry.Set("styles", eastl::move(styleNames));
        DataObject grants = DataObject::List();
        for (const auto& grant : art.abilities) {
            DataObject g; g.SetString("ability", catalog.GetAbility(grant.ability).id); g.Set("unlock", WriteUnlock(grant.unlock, catalog)); grants.Append(eastl::move(g));
        }
        entry.Set("abilities", eastl::move(grants)); arts.Append(eastl::move(entry));
    }
    out.Set("arts", eastl::move(arts));
    DataObject abilities = DataObject::List();
    for (std::uint32_t i = 1; i <= catalog.AbilityCount(); ++i) {
        const auto& ability = catalog.GetAbility({i}); DataObject entry;
        entry.SetString("id", ability.id); entry.SetString("name", ability.name);
        entry.Set("properties", Numbers({static_cast<unsigned>(ability.execution), static_cast<unsigned>(ability.targeting),
            ability.cooldown, ability.interruptOnDamage, ability.interruptOnForcedMovement, ability.focusDuration, ability.leapDistance}));
        entry.SetSigned("focus_rating_bonus100", ability.focusRatingBonus100); entry.Set("cost", WriteCost(ability.cost));
        entry.SetString("sweep_move", ability.sweepMove.value ? eastl::string_view(catalog.GetCombatMove(ability.sweepMove).id) : eastl::string_view{});
        entry.Set("training", WriteCombatTraining(ability.training)); abilities.Append(eastl::move(entry));
    }
    out.Set("abilities", eastl::move(abilities));
    DataObject items = DataObject::List();
    for (std::uint32_t i = 1; i <= catalog.ItemCount(); ++i) {
        const auto& item = catalog.GetItem({i}); DataObject entry; entry.SetString("id", item.id);
        entry.SetBoolean("grid_meter", item.gridMeter);
        entry.SetUnsigned("wiring_tiles_per_material", item.wiringTilesPerMaterial);
        if (item.wiringMaterial.value) {
            entry.SetString("wiring_material", catalog.GetItem(item.wiringMaterial).id);
        }
        if (item.placedObject.value) {
            entry.SetString("placed_object", catalog.GetObject(item.placedObject).id);
        }
        if (item.craftingEnergyPerWork != 0) {
            DataObject power;
            if (item.craftingGridPower) { power.SetString("store", "grid"); }
            else {
                const auto* capability = TryGetItemCapability(item, item.craftingPower);
                power.SetString("store", capability ? eastl::string_view(capability->id) : eastl::string_view{});
            }
            power.SetUnsigned("energy_per_work", item.craftingEnergyPerWork);
            entry.Set("crafting_power", eastl::move(power));
        }
        entry.SetBoolean("has_melee", item.melee.has_value()); entry.SetBoolean("has_armor", item.armor.has_value());
        if (item.melee) {
            const auto& melee = *item.melee; DataObject m;
            m.Set("damage", WriteDamage({melee.damage.data(), melee.damage.size()}));
            m.Set("properties", Numbers({melee.attackCost, melee.hands, melee.blockCapacity, static_cast<unsigned>(melee.allowedResponses)}));
            m.SetString("handling_skill", melee.handlingSkill.value ? eastl::string_view(catalog.GetSkill(melee.handlingSkill).id) : eastl::string_view{});
            DataObject grips = DataObject::List(); for (const auto& grip : melee.grips) { grips.Append(WriteGrip(grip)); }
            m.Set("grips", eastl::move(grips)); entry.Set("melee", eastl::move(m));
        }
        if (item.armor) {
            const auto& armor = *item.armor; DataObject a;
            a.Set("properties", Numbers({static_cast<unsigned>(armor.layer), armor.rigidRegions, armor.wearCost, armor.removeCost}));
            DataObject protection = DataObject::List(); for (auto p : armor.protection) { protection.Append(DataObject::Unsigned(p)); }
            a.Set("protection", eastl::move(protection)); DataObject regions = DataObject::List();
            for (const auto& r : armor.regions) { regions.Append(Numbers({static_cast<unsigned>(r.region), r.coverage, r.encumbrance, r.overlapExempt})); }
            a.Set("regions", eastl::move(regions)); entry.Set("armor", eastl::move(a));
        }
        items.Append(eastl::move(entry));
    }
    out.Set("items", eastl::move(items));
    DataObject objects = DataObject::List();
    for (std::uint32_t i = 1; i <= catalog.ObjectCount(); ++i) {
        const auto& object = catalog.GetObject({i});
        DataObject entry;
        entry.SetString("id", object.id);
        entry.SetString("name", object.name);
        entry.SetBoolean("blocks_movement", object.blocksMovement);
        entry.SetBoolean("blocks_sight", object.blocksSight);
        entry.SetBoolean("movable", object.movable);
        entry.SetUnsigned("move_cost", object.moveCost);
        DataObject traits = DataObject::List();
        for (const auto& trait : object.toolTraits) {
            DataObject value;
            value.SetString("id", catalog.GetToolTrait(trait.trait).id);
            value.SetUnsigned("tier", trait.tier);
            traits.Append(eastl::move(value));
        }
        entry.Set("tool_traits", eastl::move(traits));
        if (object.packingItem.value) { entry.SetString("packing_item", catalog.GetItem(object.packingItem).id); }
        if (object.capacityVolumeMl) { entry.SetUnsigned("capacity_volume_ml", *object.capacityVolumeMl); }
        switch (object.kind) {
        case ObjectKind::None: std::abort();
        case ObjectKind::Generic: entry.SetString("kind", "generic"); break;
        case ObjectKind::Chair: entry.SetString("kind", "chair"); break;
        case ObjectKind::Table: entry.SetString("kind", "table"); break;
        case ObjectKind::Lamp: entry.SetString("kind", "lamp"); break;
        }
        DataObject light;
        light.SetUnsigned("intensity", object.light.intensity);
        light.SetUnsigned("range", object.light.range);
        entry.Set("light", eastl::move(light));
        switch (object.behavior) {
        case ObjectBehaviorKind::None:
            entry.SetString("behavior", "none");
            break;
        case ObjectBehaviorKind::GridBattery:
            entry.SetString("behavior", "grid_battery");
            break;
        case ObjectBehaviorKind::Generator: {
            entry.SetString("behavior", "generator");
            DataObject generator;
            generator.SetString("fuel_item", catalog.GetItem(object.generator.fuelItem).id);
            generator.SetUnsigned("fuel_capacity", object.generator.fuelCapacity);
            generator.SetUnsigned("energy_per_fuel", object.generator.energyPerFuel);
            generator.SetUnsigned("output_per_tick", object.generator.outputPerTick);
            entry.Set("generator", eastl::move(generator));
            break;
        }
        case ObjectBehaviorKind::CraftingMachine: {
            entry.SetString("behavior", "crafting_machine");
            DataObject machine;
            machine.SetString("input", catalog.GetItem(object.machine.input).id);
            machine.SetUnsigned("input_count", object.machine.inputCount);
            machine.SetString("output", catalog.GetItem(object.machine.output).id);
            machine.SetUnsigned("output_count", object.machine.outputCount);
            machine.SetUnsigned("duration_ticks", object.machine.duration);
            machine.SetUnsigned("energy_capacity", object.machine.energyCapacity);
            machine.SetUnsigned("energy_per_tick", object.machine.energyPerTick);
            machine.SetBoolean("grid_powered", object.machine.gridPowered);
            entry.Set("machine", eastl::move(machine));
            break;
        }
        }
        objects.Append(eastl::move(entry));
    }
    out.Set("objects", eastl::move(objects));
    DataObject books = DataObject::List();
    for (std::uint32_t i = 1; i <= catalog.BookCount(); ++i) {
        const auto& book = catalog.GetBook({i}); DataObject entry; entry.SetString("id", book.id);
        DataObject manuals = DataObject::List();
        for (const auto& manual : book.artManuals) {
            DataObject m; m.SetString("art", catalog.GetCombatArt(manual.art).id); m.SetString("skill", catalog.GetSkill(manual.prerequisite.skill).id);
            m.Set("properties", Numbers({manual.prerequisite.level, manual.successChance, manual.baseDuration})); manuals.Append(eastl::move(m));
        }
        entry.Set("art_manuals", eastl::move(manuals)); books.Append(eastl::move(entry));
    }
    out.Set("books", eastl::move(books));
    DataObject monsters = DataObject::List();
    for (std::uint32_t i = 1; i <= catalog.MonsterCount(); ++i) {
        const auto& monster = catalog.GetMonster({i}); DataObject entry;
        entry.SetString("id", monster.id); entry.SetString("combat_style", catalog.GetCombatStyle(monster.combatStyle).id);
        entry.Set("properties", Numbers({monster.authoredTrainingRanks, monster.offensiveTrainingRank, monster.defensiveTrainingRank, monster.inexhaustible}));
        entry.SetSigned("attack_rating100", monster.attackRating100); entry.SetSigned("defense_rating100", monster.defenseRating100);
        entry.Set("reach", WriteReach(monster.reach)); monsters.Append(eastl::move(entry));
    }
    out.Set("monsters", eastl::move(monsters));
    const auto* corpse = catalog.TryGetItem(catalog.CorpseItem());
    out.SetString("corpse_item", corpse ? eastl::string_view(corpse->id) : eastl::string_view{});
    return out;
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
