#include "combat/combat_types.h"

#include <cstdlib>
#include <initializer_list>

namespace cataclysmx {
namespace {

bool Fields(const DataObject& data, std::initializer_list<const char*> names) {
    if (data.Type() != DataType::Object || data.Size() != names.size()) { return false; }
    for (const char* name : names) {
        if (!data.Find(name)) { return false; }
    }
    return true;
}

bool Number(const DataObject& data, const char* name, std::uint64_t maximum, std::uint64_t& value) {
    const auto number = data.GetUnsigned(name);
    if (!number || *number > maximum) { return false; }
    value = *number;
    return true;
}

bool Number32(const DataObject& data, const char* name, std::uint32_t& value) {
    std::uint64_t number = 0;
    if (!Number(data, name, UINT32_MAX, number)) { return false; }
    value = static_cast<std::uint32_t>(number);
    return true;
}

}

CombatState MakeCombatState(bool player) {
    CombatState state;
    state.body = MakeBody(player ? BodyKind::Detailed : BodyKind::Simple);
    state.faction = player ? CombatFaction::Survivors : CombatFaction::Neutral;
    state.injury.regeneration = player;
    return state;
}

bool Hostile(CombatFaction a, CombatFaction b) {
    return (a == CombatFaction::Survivors && b == CombatFaction::Undead)
        || (a == CombatFaction::Undead && b == CombatFaction::Survivors);
}

const char* CombatStyleName(CombatStyle style) {
    switch (style) {
    case CombatStyle::Balanced: return "Balanced";
    case CombatStyle::Aggressive: return "Aggressive";
    case CombatStyle::Defensive: return "Defensive";
    }
    std::abort();
}

bool ValidateCombatState(const CombatState& state) {
    if (!ValidateBody(state.body, state.injury) || !ValidateAction(state.action)
        || state.attackDamage > 100000 || state.accuracy > 100 || state.stamina > 100
        || state.staminaElapsed >= 10 || (state.stamina == 100 && state.staminaElapsed != 0)) {
        return false;
    }
    switch (state.style) {
    case CombatStyle::Balanced:
    case CombatStyle::Aggressive:
    case CombatStyle::Defensive: break;
    default: return false;
    }
    switch (state.faction) {
    case CombatFaction::Neutral:
    case CombatFaction::Survivors:
    case CombatFaction::Undead: break;
    default: return false;
    }
    return state.action.kind != ActorActionKind::MeleeWindup
        || state.action.move == static_cast<std::uint32_t>(CombatMove::HeavyAttack);
}

DataObject CaptureCombatState(const CombatState& state) {
    if (!ValidateCombatState(state)) { std::abort(); }
    DataObject data;
    data.SetUnsigned("version", 1);
    DataObject body;
    body.SetUnsigned("kind", static_cast<std::uint8_t>(state.body.kind));
    DataObject parts = DataObject::List();
    for (const auto& part : state.body.parts) {
        DataObject value;
        value.SetUnsigned("hp", part.hp);
        value.SetUnsigned("maxLoss", part.maxLoss);
        value.SetUnsigned("mendingElapsed", part.mendingElapsed);
        value.SetUnsigned("healingElapsed", part.healingElapsed);
        value.SetUnsigned("maxHealingElapsed", part.maxHealingElapsed);
        value.SetBoolean("broken", part.broken);
        parts.Append(eastl::move(value));
    }
    body.Set("parts", eastl::move(parts));
    data.Set("body", eastl::move(body));
    DataObject profile;
    profile.SetUnsigned("mendingDuration", state.injury.mendingDuration);
    profile.SetUnsigned("healingInterval", state.injury.healingInterval);
    profile.SetUnsigned("maxHealingInterval", state.injury.maxHealingInterval);
    profile.SetUnsigned("mendedHp", state.injury.mendedHp);
    profile.SetUnsigned("maxLossConversionPercent", state.injury.maxLossConversionPercent);
    profile.SetUnsigned("maxLossCapPercent", state.injury.maxLossCapPercent);
    profile.SetBoolean("regeneration", state.injury.regeneration);
    data.Set("profile", eastl::move(profile));
    DataObject action;
    action.SetUnsigned("kind", static_cast<unsigned>(state.action.kind));
    action.SetUnsigned("target", state.action.target);
    action.SetUnsigned("move", state.action.move);
    action.SetUnsigned("recoveryTicks", state.action.recoveryTicks);
    action.SetUnsigned("accuracyPenalty", state.action.accuracyPenalty);
    data.Set("action", eastl::move(action));
    data.SetUnsigned("style", static_cast<std::uint8_t>(state.style));
    data.SetUnsigned("faction", static_cast<std::uint8_t>(state.faction));
    DataObject protection = DataObject::List();
    for (const auto& part : state.protection) {
        DataObject values = DataObject::List();
        for (const auto amount : part) { values.Append(DataObject::Unsigned(amount)); }
        protection.Append(eastl::move(values));
    }
    data.Set("protection", eastl::move(protection));
    data.SetUnsigned("attackDamage", state.attackDamage);
    data.SetUnsigned("accuracy", state.accuracy);
    data.SetUnsigned("stamina", state.stamina);
    data.SetUnsigned("staminaElapsed", state.staminaElapsed);
    data.SetUnsigned("reactionCooldown", state.reactionCooldown);
    data.SetUnsigned("heavyCooldown", state.heavyCooldown);
    return data;
}

StateResult<CombatState> ReadCombatState(const DataObject& data, Diagnostics* diagnostics) {
    const auto invalid = [diagnostics](StateError error = StateError::InvalidData) {
        if (diagnostics) { diagnostics->Add("Invalid or unsupported combat state"); }
        return eastl::unexpected(error);
    };
    const auto version = data.GetUnsigned("version");
    if (!version) { return invalid(); }
    if (*version != 1) { return invalid(StateError::UnsupportedVersion); }
    if (!Fields(data, {"version", "body", "profile", "action", "style", "faction", "protection",
        "attackDamage", "accuracy", "stamina", "staminaElapsed", "reactionCooldown", "heavyCooldown"})) {
        return invalid();
    }
    CombatState state;
    const auto& body = *data.Find("body");
    std::uint64_t number = 0;
    if (!Fields(body, {"kind", "parts"}) || !Number(body, "kind", 1, number)) { return invalid(); }
    state.body.kind = static_cast<BodyKind>(number);
    const auto parts = body.Get("parts", DataType::List);
    if (!parts || (*parts)->Size() != body_part_count) { return invalid(); }
    for (std::size_t i = 0; i < body_part_count; ++i) {
        const auto& value = (*parts)->At(i);
        auto& part = state.body.parts[i];
        if (!Fields(value, {"hp", "maxLoss", "mendingElapsed", "healingElapsed", "maxHealingElapsed", "broken"})
            || !Number32(value, "hp", part.hp) || !Number32(value, "maxLoss", part.maxLoss)
            || !Number(value, "mendingElapsed", UINT64_MAX, part.mendingElapsed)
            || !Number(value, "healingElapsed", UINT64_MAX, part.healingElapsed)
            || !Number(value, "maxHealingElapsed", UINT64_MAX, part.maxHealingElapsed)) { return invalid(); }
        const auto broken = value.GetBoolean("broken");
        if (!broken) { return invalid(); }
        part.broken = *broken;
    }
    const auto& profile = *data.Find("profile");
    if (!Fields(profile, {"mendingDuration", "healingInterval", "maxHealingInterval", "mendedHp",
        "maxLossConversionPercent", "maxLossCapPercent", "regeneration"})
        || !Number(profile, "mendingDuration", UINT64_MAX, state.injury.mendingDuration)
        || !Number(profile, "healingInterval", UINT64_MAX, state.injury.healingInterval)
        || !Number(profile, "maxHealingInterval", UINT64_MAX, state.injury.maxHealingInterval)
        || !Number32(profile, "mendedHp", state.injury.mendedHp)
        || !Number32(profile, "maxLossConversionPercent", state.injury.maxLossConversionPercent)
        || !Number32(profile, "maxLossCapPercent", state.injury.maxLossCapPercent)) { return invalid(); }
    const auto regeneration = profile.GetBoolean("regeneration");
    if (!regeneration) { return invalid(); }
    state.injury.regeneration = *regeneration;
    const auto& action = *data.Find("action");
    if (!Fields(action, {"kind", "target", "move", "recoveryTicks", "accuracyPenalty"})
        || !Number(action, "kind", static_cast<unsigned>(ActorActionKind::DisruptionRecovery), number)) { return invalid(); }
    state.action.kind = static_cast<ActorActionKind>(number);
    if (!Number(action, "target", UINT64_MAX, state.action.target)
        || !Number32(action, "move", state.action.move)
        || !Number(action, "recoveryTicks", UINT64_MAX, state.action.recoveryTicks)
        || !Number(action, "accuracyPenalty", 30, number)) { return invalid(); }
    state.action.accuracyPenalty = static_cast<std::uint8_t>(number);
    if (!Number(data, "style", 2, number)) { return invalid(); }
    state.style = static_cast<CombatStyle>(number);
    if (!Number(data, "faction", 2, number)) { return invalid(); }
    state.faction = static_cast<CombatFaction>(number);
    const auto protection = data.Get("protection", DataType::List);
    if (!protection || (*protection)->Size() != body_part_count) { return invalid(); }
    for (std::size_t i = 0; i < body_part_count; ++i) {
        const auto& values = (*protection)->At(i);
        if (values.Type() != DataType::List || values.Size() != damage_type_count) { return invalid(); }
        for (std::size_t j = 0; j < damage_type_count; ++j) {
            const auto& value = values.At(j);
            if (value.Type() != DataType::Unsigned || value.UnsignedValue() > UINT32_MAX) { return invalid(); }
            state.protection[i][j] = static_cast<std::uint32_t>(value.UnsignedValue());
        }
    }
    if (!Number32(data, "attackDamage", state.attackDamage) || !Number(data, "accuracy", 100, number)) { return invalid(); }
    state.accuracy = static_cast<std::uint8_t>(number);
    if (!Number32(data, "stamina", state.stamina)
        || !Number(data, "staminaElapsed", UINT64_MAX, state.staminaElapsed)
        || !Number(data, "reactionCooldown", UINT64_MAX, state.reactionCooldown)
        || !Number(data, "heavyCooldown", UINT64_MAX, state.heavyCooldown)
        || !ValidateCombatState(state)) { return invalid(); }
    return state;
}

}
