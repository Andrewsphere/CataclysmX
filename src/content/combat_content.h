// Internal loader implementation; included once inside content.cpp's anonymous namespace.
// Deliberately shares the strict JSON readers and source diagnostics at that boundary.
struct PendingCombat {
    String kind, id, json;
    Context context;
    std::size_t inputOrder = 0;
};

bool CombatUInt(yyjson_val* object, const char* field, std::uint32_t& out,
                const Context& context, std::uint32_t maximum = 1000000, std::uint32_t minimum = 0) {
    auto* value = yyjson_obj_get(object, field);
    return !value || ReadBoundedInteger(value, minimum, maximum, out, context.Child(field));
}
bool CombatTick(yyjson_val* object, const char* field, Tick& out, const Context& context) {
    std::uint32_t value = static_cast<std::uint32_t>(out);
    if (!CombatUInt(object, field, value, context, UINT32_MAX)) { return false; }
    out = value;
    return true;
}
bool CombatBool(yyjson_val* object, const char* field, bool& out, const Context& context) {
    auto* value = yyjson_obj_get(object, field);
    return !value || ReadBool(value, out, context.Child(field));
}
bool CombatSigned(yyjson_val* object, const char* field, std::int32_t& out, const Context& context) {
    auto* value = yyjson_obj_get(object, field);
    if (!value) { return true; }
    if (!yyjson_is_int(value) || (yyjson_is_uint(value) ? yyjson_get_uint(value) > 1000000
            : yyjson_get_sint(value) < -1000000 || yyjson_get_sint(value) > 1000000)) {
        return context.Child(field).Fail("expected a rating in [-1000000,1000000]");
    }
    out = static_cast<std::int32_t>(yyjson_get_sint(value));
    return true;
}
template<class T>
bool CombatEnum(yyjson_val* value, T& out, std::initializer_list<eastl::string_view> names,
                const Context& context) {
    if (!value) { return true; }
    if (!yyjson_is_str(value)) { return context.Fail("expected an enum name"); }
    unsigned index = 0;
    for (auto name : names) {
        if (JsonString(value) == name) { out = static_cast<T>(index); return true; }
        ++index;
    }
    return context.Fail("unsupported enum name");
}
bool ParseContact(yyjson_val* value, ContactRegion& out, const Context& context) {
    if (!value) { return context.Fail("missing contact region"); }
    return CombatEnum(value, out, {"head", "eyes", "mouth", "ears", "torso", "left_arm", "right_arm",
        "left_hand", "right_hand", "left_leg", "right_leg", "left_foot", "right_foot",
        "attack_apparatus", "locomotion", "core"}, context);
}
bool ParseContactMask(yyjson_val* array, ContactMask& out, const Context& context) {
    if (!yyjson_is_arr(array) || !yyjson_arr_size(array)) { return context.Fail("expected nonempty unique contact regions"); }
    out = 0;
    std::size_t i, n; yyjson_val* value;
    yyjson_arr_foreach(array, i, n, value) {
        ContactRegion region{};
        if (!ParseContact(value, region, ArrayElement(context, i))) { return false; }
        const auto bit = ContactMask{1} << static_cast<unsigned>(region);
        if (out & bit) { return context.Fail("duplicate contact region"); }
        out |= bit;
    }
    return true;
}
bool CombatHeightAllows(ReachHeight height, ContactRegion region) {
    const bool lower = region == ContactRegion::LeftLeg || region == ContactRegion::RightLeg
        || region == ContactRegion::LeftFoot || region == ContactRegion::RightFoot || region == ContactRegion::Locomotion;
    return height == ReachHeight::Any || (height == ReachHeight::Lower ? lower : !lower);
}
bool ParseReach(yyjson_val* object, ReachProfile& out, const Context& context) {
    if (!object) { return true; }
    std::uint32_t distance = out.distance;
    if (!CheckObject(object, {"distance", "height", "regions"}, context)
        || !CombatUInt(object, "distance", distance, context, 2, 1)
        || !CombatEnum(yyjson_obj_get(object, "height"), out.height, {"any", "lower", "upper"}, context.Child("height"))) { return false; }
    out.distance = static_cast<std::uint8_t>(distance);
    if (auto* regions = yyjson_obj_get(object, "regions")) {
        if (!ParseContactMask(regions, out.regions, context.Child("regions"))) { return false; }
    }
    for (std::size_t i = 0; i < contactRegionCount; ++i) {
        if ((out.regions & (ContactMask{1} << i)) && CombatHeightAllows(out.height, static_cast<ContactRegion>(i))) { return true; }
    }
    return context.Fail("reach regions do not intersect its height filter");
}
bool ParseDefenseMask(yyjson_val* array, DefenseMask& out, const Context& context) {
    if (!array) { return true; }
    if (!yyjson_is_arr(array)) { return context.Fail("expected defense names"); }
    unsigned mask = 0;
    std::size_t i, n; yyjson_val* value;
    yyjson_arr_foreach(array, i, n, value) {
        DefenseKind kind{};
        if (!CombatEnum(value, kind, {"none", "dodge", "parry", "block"}, ArrayElement(context, i))
            || kind == DefenseKind::None) { return context.Fail("expected dodge, parry or block"); }
        unsigned bit = 1u << (static_cast<unsigned>(kind) - 1);
        if (mask & bit) { return context.Fail("duplicate defense"); }
        mask |= bit;
    }
    out = static_cast<DefenseMask>(mask);
    return true;
}
bool ParseCombatDamage(yyjson_val* array, SmallVector<DamagePacket, 1>& out, const Context& context) {
    if (!array) { return true; }
    if (!yyjson_is_arr(array) || !yyjson_arr_size(array) || yyjson_arr_size(array) > damage_type_count) {
        return context.Fail("expected nonempty unique damage packets");
    }
    std::size_t i, n; yyjson_val* value;
    yyjson_arr_foreach(array, i, n, value) {
        auto child = ArrayElement(context, i);
        DamagePacket packet;
        if (!CheckObject(value, {"type", "amount", "penetration"}, child)) { return false; }
        if (!yyjson_obj_get(value, "type")) { return child.Child("type").Fail("missing damage type"); }
        if (!CombatEnum(yyjson_obj_get(value, "type"), packet.type,
                {"blunt", "slashing", "piercing", "ballistic", "heat", "chemical", "choking"}, child.Child("type"))
            || !ReadBoundedInteger(yyjson_obj_get(value, "amount"), 1, 1000000, packet.amount, child.Child("amount"))
            || !CombatUInt(value, "penetration", packet.penetration, child)) { return false; }
        for (const auto& previous : out) { if (previous.type == packet.type) { return child.Fail("duplicate damage type"); } }
        out.push_back(packet);
    }
    return true;
}
bool ParseGrip(yyjson_val* value, GripDefinition& out, const Context& context) {
    if (!CheckObject(value, {"kind", "damage_ratio", "cost_ratio", "stamina_ratio", "block_ratio", "rating_bonus100"}, context)) { return false; }
    if (!yyjson_obj_get(value, "kind")) { return context.Child("kind").Fail("missing grip kind"); }
    if (!CombatEnum(yyjson_obj_get(value, "kind"), out.kind, {"one_handed", "two_handed"}, context.Child("kind"))) { return false; }
    return CombatUInt(value, "damage_ratio", out.damageRatio, context, 100000, 1)
        && CombatUInt(value, "cost_ratio", out.costRatio, context, 100000, 1)
        && CombatUInt(value, "stamina_ratio", out.staminaRatio, context, 100000, 1)
        && CombatUInt(value, "block_ratio", out.blockRatio, context, 100000)
        && CombatSigned(value, "rating_bonus100", out.ratingBonus100, context);
}
bool ParseArmor(yyjson_val* object, ArmorDefinition& out, const Context& context) {
    if (!CheckObject(object, {"layer", "regions", "protection", "rigid_regions", "wear_cost", "remove_cost"}, context)
        || !CombatEnum(yyjson_obj_get(object, "layer"), out.layer, {"under", "normal", "outer", "strapped"}, context.Child("layer"))
        || !CombatUInt(object, "wear_cost", out.wearCost, context, 1000000, 1)
        || !CombatUInt(object, "remove_cost", out.removeCost, context, 1000000, 1)) { return false; }
    auto* regions = yyjson_obj_get(object, "regions");
    if (!yyjson_is_arr(regions) || !yyjson_arr_size(regions)) { return context.Child("regions").Fail("expected covered regions"); }
    ContactMask covered = 0;
    std::size_t i, n; yyjson_val* value;
    yyjson_arr_foreach(regions, i, n, value) {
        auto child = ArrayElement(context.Child("regions"), i);
        ArmorRegionDefinition region;
        std::uint32_t encumbrance = 0;
        if (!CheckObject(value, {"region", "coverage", "encumbrance", "overlap_exempt"}, child)
            || !ParseContact(yyjson_obj_get(value, "region"), region.region, child.Child("region"))
            || !CombatUInt(value, "coverage", region.coverage, child, ratioScale)
            || !CombatUInt(value, "encumbrance", encumbrance, child, UINT16_MAX)
            || !CombatBool(value, "overlap_exempt", region.overlapExempt, child)) { return false; }
        auto bit = ContactMask{1} << static_cast<unsigned>(region.region);
        if (covered & bit) { return child.Fail("duplicate armor contact"); }
        covered |= bit;
        region.encumbrance = static_cast<std::uint16_t>(encumbrance);
        out.regions.push_back(region);
    }
    if (auto* rigid = yyjson_obj_get(object, "rigid_regions")) {
        if (!ParseContactMask(rigid, out.rigidRegions, context.Child("rigid_regions"))
            || (out.rigidRegions & ~covered)) { return context.Fail("rigid regions must be covered by the garment"); }
        for (const auto& region : out.regions) {
            if (region.coverage == 0 && (out.rigidRegions & (ContactMask{1} << static_cast<unsigned>(region.region)))) {
                return context.Fail("rigid regions require positive coverage");
            }
        }
    }
    if (auto* protection = yyjson_obj_get(object, "protection")) {
        if (!CheckObject(protection, {"blunt", "slashing", "piercing", "ballistic", "heat", "chemical", "choking"}, context.Child("protection"))) { return false; }
        const char* names[] = {"blunt", "slashing", "piercing", "ballistic", "heat", "chemical", "choking"};
        for (std::size_t j = 0; j < damage_type_count; ++j) {
            if (!CombatUInt(protection, names[j], out.protection[j], context.Child("protection"))) { return false; }
        }
    }
    return true;
}
bool ParseSourceRequirement(yyjson_val* object, SourceRequirement& out, const Context& context) {
    if (!object) { return context.Fail("missing attack source"); }
    if (!CheckObject(object, {"kind", "hand", "free_hands", "functional_hands", "functional_legs", "grip", "requires_natural_apparatus"}, context)) { return false; }
    if (!yyjson_obj_get(object, "kind")) { return context.Child("kind").Fail("missing source kind"); }
    if (!CombatEnum(yyjson_obj_get(object, "kind"), out.kind, {"held_item", "empty_hand", "kick", "natural"}, context.Child("kind"))
        || !CombatEnum(yyjson_obj_get(object, "hand"), out.hand, {"either", "left", "right", "both"}, context.Child("hand"))
        || !CombatEnum(yyjson_obj_get(object, "free_hands"), out.freeHands, {"none", "left", "right", "both"}, context.Child("free_hands"))
        || !CombatEnum(yyjson_obj_get(object, "functional_hands"), out.functionalHands, {"none", "left", "right", "both"}, context.Child("functional_hands"))
        || !CombatEnum(yyjson_obj_get(object, "functional_legs"), out.functionalLegs, {"none", "left", "right", "both"}, context.Child("functional_legs"))) { return false; }
    if (auto* apparatus = yyjson_obj_get(object, "requires_natural_apparatus")) {
        if (out.kind != AttackSourceKind::Natural) { return context.Child("requires_natural_apparatus").Fail("requires a natural source"); }
        if (!ReadBool(apparatus, out.requiresNaturalApparatus, context.Child("requires_natural_apparatus"))) { return false; }
    }
    if (auto* grip = yyjson_obj_get(object, "grip")) {
        out.requiresGrip = true;
        if (out.kind != AttackSourceKind::HeldItem
            || !CombatEnum(grip, out.grip, {"one_handed", "two_handed"}, context.Child("grip"))) { return context.Fail("grip requires a held item source"); }
    }
    if ((out.kind == AttackSourceKind::Kick || out.kind == AttackSourceKind::Natural) && out.hand != SourceHand::Either) {
        return context.Fail("kick/natural sources cannot select a hand");
    }
    if (out.kind == AttackSourceKind::HeldItem && out.hand == SourceHand::Both
        && (!out.requiresGrip || out.grip != GripKind::TwoHanded)) { return context.Fail("both hands require a two-handed grip"); }
    if (out.kind == AttackSourceKind::Kick && out.functionalLegs == LegMask::None) {
        return context.Fail("kick source must require functional legs");
    }
    if (out.kind == AttackSourceKind::HeldItem) {
        const auto free = static_cast<unsigned>(out.freeHands);
        if (out.freeHands == HandMask::Both || (out.requiresGrip && out.grip == GripKind::TwoHanded && free)
            || (out.hand == SourceHand::Left && (free & 1)) || (out.hand == SourceHand::Right && (free & 2))) {
            return context.Fail("held source cannot require its occupied hand to be free");
        }
    }
    return true;
}
bool ParseActionCost(yyjson_val* object, ActionCost& out, const Context& context) {
    if (!object) { return true; }
    return CheckObject(object, {"windup", "recovery", "stamina"}, context)
        && CombatUInt(object, "windup", out.windupCost, context)
        && CombatUInt(object, "recovery", out.recoveryCost, context, 1000000, 1)
        && CombatUInt(object, "stamina", out.staminaCost, context, 100);
}
bool ParseTraining(yyjson_val* object, CombatTrainingDefinition& out, const Context& context) {
    if (!object) { return true; }
    std::uint32_t work = static_cast<std::uint32_t>(out.work);
    if (!CheckObject(object, {"work", "melee_share", "handling_share", "art_xp_per_work"}, context)
        || !CombatUInt(object, "work", work, context, UINT32_MAX)
        || !CombatUInt(object, "melee_share", out.meleeShare, context, ratioScale)
        || !CombatUInt(object, "handling_share", out.handlingShare, context, ratioScale)
        || !CombatUInt(object, "art_xp_per_work", out.artXpPerWork, context, ratioScale)
        || out.meleeShare + out.handlingShare != ratioScale) { return context.Fail("training shares must total 10000"); }
    out.work = work;
    return true;
}
bool ResolveCombatSkill(yyjson_val* value, SkillId& out, const ContentCatalog& catalog, const Context& context) {
    String name;
    if (!ReadId(value, name, context)) { return false; }
    out = catalog.FindSkill(View(name));
    if (!out.value || catalog.GetSkill(out).category != SkillCategory::Combat) { return context.Fail("unknown combat skill reference"); }
    return true;
}
bool ParseUnlock(yyjson_val* object, MoveUnlock& out, const ContentCatalog& catalog, const Context& context) {
    if (!object) { return true; }
    if (!CheckObject(object, {"skills", "art_xp"}, context)) { return false; }
    if (auto* xp = yyjson_obj_get(object, "art_xp")) {
        if (!yyjson_is_uint(xp)) { return context.Child("art_xp").Fail("expected unsigned XP"); }
        out.artXp = yyjson_get_uint(xp);
    }
    if (auto* array = yyjson_obj_get(object, "skills")) {
        if (!yyjson_is_arr(array)) { return context.Child("skills").Fail("expected skill requirements"); }
        std::size_t i, n; yyjson_val* value;
        yyjson_arr_foreach(array, i, n, value) {
            auto child = ArrayElement(context.Child("skills"), i);
            SkillRequirement skill;
            std::uint32_t level = 0;
            if (!CheckObject(value, {"skill", "level"}, child)
                || !ResolveCombatSkill(yyjson_obj_get(value, "skill"), skill.skill, catalog, child.Child("skill"))
                || !ReadBoundedInteger(yyjson_obj_get(value, "level"), 0, maximumSkillLevel, level, child.Child("level"))) { return false; }
            if (!ValidateSkillMaximum(level, catalog.GetSkill(skill.skill), child.Child("level"))) { return false; }
            skill.level = static_cast<std::uint8_t>(level);
            for (const auto& previous : out.skills) { if (previous.skill == skill.skill) { return child.Fail("duplicate skill requirement"); } }
            out.skills.push_back(skill);
        }
    }
    return true;
}
bool CaptureCombat(yyjson_val* value, const Context& context, Vector<PendingCombat>& output) {
    if (output.size() >= UINT32_MAX) { return context.Fail("too many combat definitions"); }
    PendingCombat pending;
    pending.context = context;
    if (!ReadId(yyjson_obj_get(value, "id"), pending.id, context.Child("id"))) { return false; }
    pending.kind.assign(yyjson_get_str(yyjson_obj_get(value, "type")));
    for (const auto& previous : output) {
        if (previous.id == pending.id) { return context.Child("id").Fail("duplicate combat definition id"); }
    }
    std::size_t size = 0;
    char* text = yyjson_val_write(value, 0, &size);
    if (!text) { std::abort(); }
    pending.json.assign(text, size);
    std::free(text);
    output.push_back(eastl::move(pending));
    return true;
}

bool ParseCombatMove(yyjson_val* object, CombatMoveDefinition& out, const ContentCatalog& catalog, const Context& context) {
    if (!CheckObject(object, {"type", "id", "name", "trigger", "execution", "requirements", "unlock", "cost", "cooldown",
            "strikes", "defense", "preparation", "follow_through", "training"}, context)
        || !ReadString(yyjson_obj_get(object, "name"), out.name, context.Child("name"))
        || !CombatEnum(yyjson_obj_get(object, "trigger"), out.trigger, {"attack", "incoming_strike", "counter_opening"}, context.Child("trigger"))
        || !CombatEnum(yyjson_obj_get(object, "execution"), out.execution, {"strikes", "dodge", "parry", "block"}, context.Child("execution"))
        || !ParseUnlock(yyjson_obj_get(object, "unlock"), out.unlock, catalog, context.Child("unlock"))
        || !ParseActionCost(yyjson_obj_get(object, "cost"), out.cost, context.Child("cost"))
        || !CombatTick(object, "cooldown", out.cooldown, context)
        || !ParseTraining(yyjson_obj_get(object, "training"), out.training, context.Child("training"))) { return false; }
    auto* req = yyjson_obj_get(object, "requirements");
    auto child = context.Child("requirements");
    if (!CheckObject(req, {"primary", "secondary", "paired", "handling_skills", "requires_awareness", "requires_opening",
            "allowed_during_recovery", "allowed_during_windup", "exhausted_only"}, child)
        || !ParseSourceRequirement(yyjson_obj_get(req, "primary"), out.requirements.primary, child.Child("primary"))
        || !CombatBool(req, "paired", out.requirements.paired, child)
        || !CombatBool(req, "requires_awareness", out.requirements.requiresAwareness, child)
        || !CombatBool(req, "requires_opening", out.requirements.requiresOpening, child)
        || !CombatBool(req, "allowed_during_recovery", out.requirements.allowedDuringRecovery, child)
        || !CombatBool(req, "allowed_during_windup", out.requirements.allowedDuringWindup, child)
        || !CombatBool(req, "exhausted_only", out.requirements.exhaustedOnly, child)) { return false; }
    if (out.requirements.paired) {
        if (!ParseSourceRequirement(yyjson_obj_get(req, "secondary"), out.requirements.secondary, child.Child("secondary"))) { return false; }
        for (const auto* source : {&out.requirements.primary, &out.requirements.secondary}) {
            if ((source->kind != AttackSourceKind::HeldItem && source->kind != AttackSourceKind::EmptyHand)
                || source->hand == SourceHand::Both || (source->requiresGrip && source->grip == GripKind::TwoHanded)) {
                return child.Fail("paired sources require distinct one-handed or empty-hand sources");
            }
        }
        if (out.requirements.primary.hand != SourceHand::Either
            && out.requirements.primary.hand == out.requirements.secondary.hand) { return child.Fail("paired sources cannot bind the same hand"); }
    } else if (yyjson_obj_get(req, "secondary")) { return child.Fail("secondary source requires paired=true"); }
    if (auto* skills = yyjson_obj_get(req, "handling_skills")) {
        if (!yyjson_is_arr(skills)) { return child.Child("handling_skills").Fail("expected combat skills"); }
        std::size_t i, n; yyjson_val* value;
        yyjson_arr_foreach(skills, i, n, value) {
            SkillId skill;
            if (!ResolveCombatSkill(value, skill, catalog, child.Child("handling_skills"))) { return false; }
            for (auto previous : out.requirements.handlingSkills) { if (previous == skill) { return child.Fail("duplicate handling skill"); } }
            out.requirements.handlingSkills.push_back(skill);
        }
    }
    if (auto* strikes = yyjson_obj_get(object, "strikes")) {
        if (!yyjson_is_arr(strikes)) { return context.Child("strikes").Fail("expected strikes array"); }
        std::size_t i, n; yyjson_val* value;
        yyjson_arr_foreach(strikes, i, n, value) {
            child = ArrayElement(context.Child("strikes"), i);
            StrikeDefinition strike;
            if (!CheckObject(value, {"source", "reach", "contacts", "damage", "damage_ratio", "rating_bonus100", "intrinsic_miss", "defenses"}, child)
                || !CombatEnum(yyjson_obj_get(value, "source"), strike.source, {"primary", "secondary"}, child.Child("source"))
                || !ParseReach(yyjson_obj_get(value, "reach"), strike.reach, child.Child("reach"))
                || !ParseCombatDamage(yyjson_obj_get(value, "damage"), strike.damage, child.Child("damage"))
                || !CombatUInt(value, "damage_ratio", strike.damageRatio, child, 100000, 1)
                || !CombatSigned(value, "rating_bonus100", strike.ratingBonus100, child)
                || !CombatUInt(value, "intrinsic_miss", strike.intrinsicMiss, child, ratioScale)
                || !ParseDefenseMask(yyjson_obj_get(value, "defenses"), strike.defenses, child.Child("defenses"))) { return false; }
            if (strike.source == StrikeSource::Secondary && !out.requirements.paired) { return child.Fail("secondary strike requires paired sources"); }
            if (auto* contacts = yyjson_obj_get(value, "contacts")) {
                if (!yyjson_is_arr(contacts) || !yyjson_arr_size(contacts)) { return child.Fail("expected weighted contacts"); }
                ContactMask seen = 0; std::uint64_t total = 0;
                std::size_t j, m; yyjson_val* contact;
                yyjson_arr_foreach(contacts, j, m, contact) {
                    auto cc = ArrayElement(child.Child("contacts"), j);
                    ContactWeight entry;
                    if (!CheckObject(contact, {"region", "weight"}, cc)
                        || !ParseContact(yyjson_obj_get(contact, "region"), entry.region, cc.Child("region"))
                        || !CombatUInt(contact, "weight", entry.weight, cc, UINT32_MAX, 1)) { return false; }
                    auto bit = ContactMask{1} << static_cast<unsigned>(entry.region);
                    if ((seen & bit) || !(strike.reach.regions & bit) || !CombatHeightAllows(strike.reach.height, entry.region)) { return cc.Fail("duplicate or unreachable contact"); }
                    seen |= bit; total += entry.weight;
                    if (total > UINT32_MAX) { return cc.Fail("contact weights overflow"); }
                    strike.contacts.push_back(entry);
                }
            }
            out.strikes.push_back(eastl::move(strike));
        }
    }
    if (out.execution == MoveExecution::Strikes) {
        if (out.strikes.empty() || out.trigger == CombatTrigger::IncomingStrike) { return context.Fail("strike moves require strikes and an attack/counter trigger"); }
        if (yyjson_obj_get(object, "defense")) { return context.Fail("strike moves cannot define a defense"); }
    } else {
        if (!out.strikes.empty() || out.trigger != CombatTrigger::IncomingStrike) { return context.Fail("defenses require incoming_strike and no strikes"); }
        out.defense.kind = static_cast<DefenseKind>(out.execution);
    }
    if (auto* defense = yyjson_obj_get(object, "defense")) {
        child = context.Child("defense");
        if (!CheckObject(defense, {"rating_bonus100", "block_capacity", "uses_source_capacity", "permits_armed_attack"}, child)
            || !CombatSigned(defense, "rating_bonus100", out.defense.ratingBonus100, child)
            || !CombatUInt(defense, "block_capacity", out.defense.blockCapacity, child)
            || !CombatBool(defense, "uses_source_capacity", out.defense.usesSourceCapacity, child)
            || !CombatBool(defense, "permits_armed_attack", out.defense.permitsArmedAttack, child)) { return false; }
    }
    if (auto* prep = yyjson_obj_get(object, "preparation")) {
        if (!CheckObject(prep, {"expose_opening"}, context.Child("preparation"))
            || !CombatTick(prep, "expose_opening", out.preparation.exposeOpening, context.Child("preparation"))) { return false; }
    }
    if (auto* follow = yyjson_obj_get(object, "follow_through")) {
        child = context.Child("follow_through");
        std::uint32_t knockback = 0;
        if (!CheckObject(follow, {"condition", "stagger", "knockback_tiles", "counter_opening"}, child)
            || !CombatEnum(yyjson_obj_get(follow, "condition"), out.followThrough.condition,
                {"always", "miss", "defense_succeeded", "contact", "damaging_contact", "killed"}, child.Child("condition"))
            || !CombatTick(follow, "stagger", out.followThrough.stagger, child)
            || !CombatTick(follow, "counter_opening", out.followThrough.counterOpening, child)
            || !CombatUInt(follow, "knockback_tiles", knockback, child, 2)) { return false; }
        out.followThrough.knockbackTiles = static_cast<std::uint8_t>(knockback);
    }
    if (out.trigger == CombatTrigger::CounterOpening && !out.requirements.requiresOpening) { return context.Fail("counter moves must require an opening"); }
    if (out.trigger == CombatTrigger::CounterOpening && out.followThrough.counterOpening) { return context.Fail("counter cannot grant another counter opening"); }
    if (out.requirements.exhaustedOnly && (out.cost.staminaCost || out.trigger != CombatTrigger::Attack)) { return context.Fail("exhausted fallback must be a zero-stamina attack"); }
    return true;
}

bool ParseCombatStyle(yyjson_val* object, CombatStyleDefinition& out, const ContentCatalog& catalog, const Context& context) {
    if (!CheckObject(object, {"type", "id", "name", "art", "moves", "switch_cost", "exclude_attack_handling_skills"}, context)
        || !ReadString(yyjson_obj_get(object, "name"), out.name, context.Child("name"))
        || !CombatUInt(object, "switch_cost", out.switchCost, context, 1000000, 1)) { return false; }
    if (auto* art = yyjson_obj_get(object, "art")) {
        String name;
        if (!ReadId(art, name, context.Child("art"))) { return false; }
        out.art = catalog.FindCombatArt(View(name));
        if (!out.art.value) { return context.Child("art").Fail("unknown combat art"); }
    }
    if (auto* skills = yyjson_obj_get(object, "exclude_attack_handling_skills")) {
        const auto child = context.Child("exclude_attack_handling_skills");
        if (!yyjson_is_arr(skills)) { return child.Fail("expected combat skills"); }
        std::size_t i, n; yyjson_val* value;
        yyjson_arr_foreach(skills, i, n, value) {
            SkillId skill;
            const auto element = ArrayElement(child, i);
            if (!ResolveCombatSkill(value, skill, catalog, element)) { return false; }
            for (auto previous : out.excludedAttackHandlingSkills) {
                if (previous == skill) { return element.Fail("duplicate excluded attack handling skill"); }
            }
            out.excludedAttackHandlingSkills.push_back(skill);
        }
        // Skill indices are assigned in lexical order; set order has no gameplay meaning.
        eastl::sort(out.excludedAttackHandlingSkills.begin(), out.excludedAttackHandlingSkills.end(),
            [](SkillId a, SkillId b) { return a.value < b.value; });
    }
    auto* moves = yyjson_obj_get(object, "moves");
    if (!yyjson_is_arr(moves) || !yyjson_arr_size(moves)) { return context.Child("moves").Fail("expected weighted moves"); }
    std::uint64_t total = 0;
    std::size_t i, n; yyjson_val* value;
    yyjson_arr_foreach(moves, i, n, value) {
        auto child = ArrayElement(context.Child("moves"), i);
        WeightedCombatMove entry; String name;
        if (!CheckObject(value, {"move", "weight"}, child)
            || !ReadId(yyjson_obj_get(value, "move"), name, child.Child("move"))
            || !CombatUInt(value, "weight", entry.weight, child, UINT32_MAX, 1)) { return false; }
        entry.move = catalog.FindCombatMove(View(name));
        if (!entry.move.value) { return child.Fail("unknown combat move"); }
        for (const auto& previous : out.moves) { if (previous.move == entry.move) { return child.Fail("duplicate weighted move"); } }
        total += entry.weight;
        if (total > UINT32_MAX) { return child.Fail("move weights overflow"); }
        out.moves.push_back(entry);
    }
    eastl::sort(out.moves.begin(), out.moves.end(), [&catalog](const auto& a, const auto& b) {
        return catalog.GetCombatMove(a.move).id < catalog.GetCombatMove(b.move).id;
    });
    return true;
}
bool ParseCombatArt(yyjson_val* object, CombatArtDefinition& out, const ContentCatalog& catalog, const Context& context) {
    if (!CheckObject(object, {"type", "id", "name", "styles", "abilities"}, context)
        || !ReadString(yyjson_obj_get(object, "name"), out.name, context.Child("name"))) { return false; }
    Vector<String> styles;
    if (!ParseUniqueIds(yyjson_obj_get(object, "styles"), styles, context.Child("styles")) || styles.empty()) { return context.Fail("art requires styles"); }
    for (const auto& name : styles) {
        auto id = catalog.FindCombatStyle(View(name));
        if (!id.value) { return context.Child("styles").Fail("unknown combat style"); }
        out.styles.push_back(id);
    }
    if (auto* grants = yyjson_obj_get(object, "abilities")) {
        if (!yyjson_is_arr(grants)) { return context.Child("abilities").Fail("expected ability grants"); }
        std::size_t i, n; yyjson_val* value;
        yyjson_arr_foreach(grants, i, n, value) {
            auto child = ArrayElement(context.Child("abilities"), i);
            ArtAbilityGrant grant; String name;
            if (!CheckObject(value, {"ability", "unlock"}, child)
                || !ReadId(yyjson_obj_get(value, "ability"), name, child.Child("ability"))
                || !ParseUnlock(yyjson_obj_get(value, "unlock"), grant.unlock, catalog, child.Child("unlock"))) { return false; }
            grant.ability = catalog.FindAbility(View(name));
            if (!grant.ability.value) { return child.Fail("unknown ability"); }
            for (const auto& previous : out.abilities) { if (previous.ability == grant.ability) { return child.Fail("duplicate ability grant"); } }
            out.abilities.push_back(eastl::move(grant));
        }
    }
    return true;
}
bool ParseAbility(yyjson_val* object, AbilityDefinition& out, const ContentCatalog& catalog, const Context& context) {
    if (!CheckObject(object, {"type", "id", "name", "execution", "targeting", "cost", "cooldown", "interrupt_on_damage",
            "interrupt_on_forced_movement", "focus_duration", "focus_rating_bonus100", "leap_distance", "sweep_move", "training"}, context)
        || !ReadString(yyjson_obj_get(object, "name"), out.name, context.Child("name"))
        || !CombatEnum(yyjson_obj_get(object, "execution"), out.execution, {"focus", "leap", "sweep"}, context.Child("execution"))
        || !CombatEnum(yyjson_obj_get(object, "targeting"), out.targeting, {"self", "actor", "tile", "cardinal_direction"}, context.Child("targeting"))
        || !ParseActionCost(yyjson_obj_get(object, "cost"), out.cost, context.Child("cost"))
        || !CombatTick(object, "cooldown", out.cooldown, context)
        || !CombatBool(object, "interrupt_on_damage", out.interruptOnDamage, context)
        || !CombatBool(object, "interrupt_on_forced_movement", out.interruptOnForcedMovement, context)
        || !ParseTraining(yyjson_obj_get(object, "training"), out.training, context.Child("training"))) { return false; }
    if (out.execution == AbilityExecution::Focus) {
        if (out.targeting != AbilityTargetKind::Self || yyjson_obj_get(object, "sweep_move") || yyjson_obj_get(object, "leap_distance")
            || !CombatTick(object, "focus_duration", out.focusDuration, context)
            || !CombatSigned(object, "focus_rating_bonus100", out.focusRatingBonus100, context)
            || !out.focusDuration || out.focusRatingBonus100 <= 0) { return context.Fail("focus requires self targeting, positive duration and bonus"); }
    } else {
        if (yyjson_obj_get(object, "focus_duration") || yyjson_obj_get(object, "focus_rating_bonus100")) { return context.Fail("focus fields require focus execution"); }
        if (out.execution == AbilityExecution::Leap) {
            std::uint32_t distance = 2;
            if (out.targeting != AbilityTargetKind::Tile || yyjson_obj_get(object, "sweep_move")
                || !CombatUInt(object, "leap_distance", distance, context, 2, 1)) { return context.Fail("leap requires tile targeting and distance 1..2"); }
            out.leapDistance = static_cast<std::uint8_t>(distance);
        } else {
            String name;
            if (out.targeting != AbilityTargetKind::CardinalDirection || yyjson_obj_get(object, "leap_distance")
                || !ReadId(yyjson_obj_get(object, "sweep_move"), name, context.Child("sweep_move"))) { return context.Fail("sweep requires cardinal_direction and sweep_move"); }
            out.sweepMove = catalog.FindCombatMove(View(name));
            const auto* move = catalog.TryGetCombatMove(out.sweepMove);
            if (!move || move->execution != MoveExecution::Strikes || move->requirements.requiresOpening
                || move->trigger != CombatTrigger::Attack || move->requirements.exhaustedOnly) { return context.Fail("invalid sweep strike move"); }
        }
    }
    return true;
}

void InstallBasicCombat(Vector<CombatMoveDefinition>& moves, Vector<CombatStyleDefinition>& styles, CombatSkillIds skills) {
    struct Basic { const char* id; AttackSourceKind source; std::uint32_t stamina; std::uint32_t recovery; };
    for (const auto& basic : {Basic{"basic_weapon", AttackSourceKind::HeldItem, 5, 100},
            Basic{"basic_fist", AttackSourceKind::EmptyHand, 4, 90}, Basic{"basic_kick", AttackSourceKind::Kick, 7, 120},
            Basic{"basic_heavy", AttackSourceKind::HeldItem, 12, 160}, Basic{"basic_paired", AttackSourceKind::HeldItem, 10, 140},
            Basic{"basic_exhausted_weapon", AttackSourceKind::HeldItem, 0, 180},
            Basic{"basic_exhausted_fist", AttackSourceKind::EmptyHand, 0, 180},
            Basic{"basic_exhausted_kick", AttackSourceKind::Kick, 0, 200},
            Basic{"basic_natural", AttackSourceKind::Natural, 5, 100}, Basic{"basic_bite", AttackSourceKind::Natural, 8, 130},
            Basic{"basic_exhausted_natural", AttackSourceKind::Natural, 0, 200},
            Basic{"basic_counter", AttackSourceKind::HeldItem, 6, 90}}) {
        CombatMoveDefinition move;
        move.id = basic.id; move.name = basic.id;
        move.requirements.primary.kind = basic.source;
        if (basic.source == AttackSourceKind::Kick) { move.requirements.primary.functionalLegs = LegMask::Both; }
        move.cost.staminaCost = basic.stamina; move.cost.recoveryCost = basic.recovery;
        move.requirements.exhaustedOnly = basic.stamina == 0;
        StrikeDefinition strike;
        if (basic.source == AttackSourceKind::Natural) { strike.reach.distance = 2; }
        if (basic.stamina == 0) { strike.damageRatio = 5000; strike.ratingBonus100 = -100; }
        if (basic.source == AttackSourceKind::EmptyHand) { strike.damageRatio = basic.stamina ? 4000 : 2000; }
        if (basic.source == AttackSourceKind::Kick) { strike.damageRatio = basic.stamina ? 5000 : 2500; }
        if (move.id == "basic_heavy") { move.cost.windupCost = 50; strike.damageRatio = 15000; move.preparation.exposeOpening = 60; move.cooldown = 200; }
        if (move.id == "basic_bite" || move.id == "basic_exhausted_natural") {
            move.requirements.primary.requiresNaturalApparatus = false;
            strike.damage.push_back({DamageType::Piercing, 700, 0});
        }
        if (move.id == "basic_paired") {
            move.requirements.paired = true;
            move.requirements.primary.requiresGrip = true;
            move.requirements.secondary = move.requirements.primary;
            move.strikes.push_back(strike); strike.source = StrikeSource::Secondary;
        }
        if (move.id == "basic_counter") { move.trigger = CombatTrigger::CounterOpening; move.requirements.requiresOpening = true; }
        move.strikes.push_back(strike);
        moves.push_back(eastl::move(move));
    }
    for (auto execution : {MoveExecution::Dodge, MoveExecution::Parry, MoveExecution::Block}) {
        CombatMoveDefinition move;
        move.id = execution == MoveExecution::Dodge ? "basic_dodge" : execution == MoveExecution::Parry ? "basic_parry" : "basic_block";
        move.name = move.id;
        move.trigger = CombatTrigger::IncomingStrike; move.execution = execution;
        move.defense.kind = static_cast<DefenseKind>(execution);
        move.requirements.primary.kind = execution == MoveExecution::Dodge ? AttackSourceKind::Natural : AttackSourceKind::HeldItem;
        move.cost = {0, 30, 3}; move.cooldown = 30;
        move.defense.permitsArmedAttack = execution != MoveExecution::Dodge;
        move.followThrough.condition = EffectCondition::DefenseSucceeded; move.followThrough.counterOpening = 100;
        moves.push_back(eastl::move(move));
    }
    eastl::sort(moves.begin(), moves.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    for (const char* id : {"basic_aggressive", "basic_balanced", "basic_defensive"}) {
        CombatStyleDefinition style; style.id = id; style.name = id;
        if (skills.shields.value) { style.excludedAttackHandlingSkills.push_back(skills.shields); }
        for (std::size_t i = 0; i < moves.size(); ++i) {
            const auto& move = moves[i];
            std::uint32_t weight = 10;
            if (move.trigger == CombatTrigger::IncomingStrike) { weight = style.id == "basic_defensive" ? 25 : 10; }
            if (move.id == "basic_heavy") { weight = style.id == "basic_aggressive" ? 20 : 5; }
            style.moves.push_back({CombatMoveId{static_cast<std::uint32_t>(i + 1)}, weight});
        }
        styles.push_back(eastl::move(style));
    }
}

bool ValidateBasicCombat(const ContentCatalog& catalog, const Context& context) {
    struct Role {
        const char* id;
        AttackSourceKind source;
        MoveExecution execution = MoveExecution::Strikes;
        bool exhausted = false;
        bool paired = false;
        bool counter = false;
    };
    const Role roles[] = {
        {"basic_weapon", AttackSourceKind::HeldItem}, {"basic_fist", AttackSourceKind::EmptyHand},
        {"basic_kick", AttackSourceKind::Kick}, {"basic_heavy", AttackSourceKind::HeldItem},
        {"basic_paired", AttackSourceKind::HeldItem, MoveExecution::Strikes, false, true},
        {"basic_exhausted_weapon", AttackSourceKind::HeldItem, MoveExecution::Strikes, true},
        {"basic_exhausted_fist", AttackSourceKind::EmptyHand, MoveExecution::Strikes, true},
        {"basic_exhausted_kick", AttackSourceKind::Kick, MoveExecution::Strikes, true},
        {"basic_exhausted_natural", AttackSourceKind::Natural, MoveExecution::Strikes, true},
        {"basic_natural", AttackSourceKind::Natural}, {"basic_bite", AttackSourceKind::Natural},
        {"basic_dodge", AttackSourceKind::Natural, MoveExecution::Dodge},
        {"basic_parry", AttackSourceKind::HeldItem, MoveExecution::Parry},
        {"basic_block", AttackSourceKind::HeldItem, MoveExecution::Block},
        {"basic_counter", AttackSourceKind::HeldItem, MoveExecution::Strikes, false, false, true},
    };
    for (const char* styleName : {"basic_balanced", "basic_aggressive", "basic_defensive"}) {
        const auto* style = catalog.TryGetCombatStyle(catalog.FindCombatStyle(styleName));
        if (!style || style->art.value) { return context.Fail("missing unlearned basic style role"); }
        for (const auto& role : roles) {
            const auto id = catalog.FindCombatMove(role.id);
            const auto* move = catalog.TryGetCombatMove(id);
            const auto trigger = role.execution != MoveExecution::Strikes ? CombatTrigger::IncomingStrike
                : role.counter ? CombatTrigger::CounterOpening : CombatTrigger::Attack;
            if (!move || move->execution != role.execution || move->trigger != trigger
                || move->requirements.primary.kind != role.source || move->requirements.exhaustedOnly != role.exhausted
                || move->requirements.paired != role.paired || move->requirements.requiresOpening != role.counter
                || !move->unlock.skills.empty() || move->unlock.artXp || !move->requirements.handlingSkills.empty()) {
                return context.Child(role.id).Fail("missing or incompatible basic combat role");
            }
            if (role.exhausted && (move->cooldown || move->cost.staminaCost)) {
                return context.Child(role.id).Fail("basic exhausted fallback cannot require stamina or cooldown");
            }
            bool included = false;
            for (auto weighted : style->moves) { if (weighted.move == id) { included = true; } }
            if (!included) { return context.Child(styleName).Fail("basic style must supply every baseline role"); }
        }
    }
    return true;
}
