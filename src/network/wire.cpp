#include "network/wire.h"
#include "core/ratio.h"
#include "items/item_behavior.h"

#include <EASTL/utility.h>

namespace cataclysmx::network::wire {
namespace {
inline constexpr std::size_t combatEventWireSize = 133;
bool ValidStationResult(const CommandResult& v) {
    if (v.stationOutcome > StationActionOutcome::FixedFixture) return false;
    const bool success = v.stationOutcome == StationActionOutcome::Placed || v.stationOutcome == StationActionOutcome::Packed;
    if (v.outcome == Outcome::station_changed) return success && v.advanced && v.cost;
    if (v.outcome == Outcome::station_refused) return !success && !v.advanced && !v.cost;
    return v.stationOutcome == StationActionOutcome::Unavailable;
}
bool ValidStations(const WorldView& view) {
    if (view.stations.size() > 289) return false;
    PersistentId previous = 0;
    for (const auto& station : view.stations) {
        if (station.id <= previous || !station.type.value || !ValidPosition(station.position)
            || station.position.y != view.player_position.y
            || std::abs(std::int64_t{station.position.x} - view.player_position.x) > 8
            || std::abs(std::int64_t{station.position.z} - view.player_position.z) > 8
            || ((!station.enabled || station.broken) && station.worker)
            || station.packing > StationActionOutcome::FixedFixture || station.packing == StationActionOutcome::Placed) return false;
        previous = station.id;
    }
    return true;
}
bool ValidPowerResult(const CommandResult& v) {
    if (v.powerOutcome > PowerActionOutcome::NoNewCoverage) return false;
    if (v.outcome == Outcome::power_changed) return v.powerOutcome == PowerActionOutcome::Succeeded && v.advanced && v.cost;
    if (v.outcome == Outcome::power_refused) return v.powerOutcome != PowerActionOutcome::Succeeded && !v.advanced && !v.cost;
    return v.powerOutcome == PowerActionOutcome::Unavailable;
}
bool ValidPowerView(const WorldView& v) {
    if (!ValidPowerSettings(v.powerSettings)) return false;
    if (v.currentPower) {
        const auto& p = *v.currentPower;
        if ((p.grid && !p.grid->value) || p.batteryEnergy > p.batteryCapacity) return false;
        if (!p.grid && (p.mainsAvailable || p.batteryEnergy || p.batteryCapacity || p.generatorOutputAvailable
            || p.batteries || p.runningGenerators || p.runningMachines || p.waitingMachines || p.unloadedDevices)) return false;
    } else {
        for (const auto& cell : v.cells) if (cell.powerGridId) return false;
    }
    return true;
}
void WritePower(Writer& w, const WorldView& v) {
    w.I32(v.powerSettings.maxWiringWidth); w.I32(v.powerSettings.maxWiringDepth);
    w.U8(v.currentPower.has_value());
    if (!v.currentPower) return;
    const auto& p = *v.currentPower;
    w.U64(p.grid ? p.grid->value : 0); w.U8(p.mainsAvailable);
    w.U64(p.batteryEnergy); w.U64(p.batteryCapacity); w.U64(p.generatorOutputAvailable);
    w.U32(p.batteries); w.U32(p.runningGenerators); w.U32(p.runningMachines); w.U32(p.waitingMachines); w.U32(p.unloadedDevices);
}
bool ReadBool(Reader& r, bool& value) {
    std::uint8_t byte;
    if (!r.U8(byte) || byte > 1) return false;
    value = byte != 0; return true;
}
template<class T> bool ReadEnum(Reader& r, T& value, T maximum) {
    std::uint8_t byte;
    if (!r.U8(byte) || byte > static_cast<unsigned>(maximum)) return false;
    value = static_cast<T>(byte); return true;
}
bool ValidGrant(const AbilityGrantKey& s) {
    return s.kind <= AbilityGrantSourceKind::Innate
        && (s.kind == AbilityGrantSourceKind::Art ? s.art.value != 0 : s.art.value == 0);
}
void WriteGrant(Writer& w, const AbilityGrantKey& s) { w.U8(static_cast<std::uint8_t>(s.kind)); w.U32(s.art.value); }
bool ReadGrant(Reader& r, AbilityGrantKey& s) {
    return ReadEnum(r, s.kind, AbilityGrantSourceKind::Innate) && r.U32(s.art.value);
}
bool ValidStudyResult(const CommandResult& r) {
    if (r.studyOutcome > StudyOutcome::OutsideTeachingRange) return false;
    switch (r.outcome) {
    case Outcome::study_started: return r.studyOutcome == StudyOutcome::Started && r.advanced && r.cost;
    case Outcome::study_paused:
        return (r.studyOutcome == StudyOutcome::PauseRequested || r.studyOutcome == StudyOutcome::Paused) && !r.advanced && !r.cost;
    case Outcome::study_refused: return r.studyOutcome >= StudyOutcome::ActorUnavailable && !r.advanced && !r.cost;
    default: return r.studyOutcome == StudyOutcome::ActorUnavailable;
    }
}
bool ValidLearning(const LearningView& v) {
    if (v.skills.size() > maxPacket / 61 || v.recipes.size() > maxPacket / 11
        || v.books.size() > maxPacket / 31 || (!v.activeBook.value && (v.studyRemaining || v.studyPauseRequested))) return false;
    std::uint32_t last = 0;
    for (const auto& s : v.skills) {
        const auto& p = s.progress;
        if (p.skill.value <= last || p.level > maximumSkillLevel || s.effectiveLevel < 0
            || p.multiplier < ratioScale || p.multiplier > 40000 || p.workRemainder >= ratioScale
            || p.boostRemainder >= ratioScale || p.directBookXp > p.xp
            || (p.multiplier == ratioScale && p.boostRemainder)) return false;
        if (p.level == maximumSkillLevel) {
            if (s.requiredXp || p.xp || p.theory || p.practice || p.directBookXp
                || p.workRemainder || p.boostRemainder || p.multiplier != ratioScale) return false;
        } else if (!s.requiredXp || s.requiredXp > std::uint64_t{1000000000} * ratioScale
            || p.xp >= s.requiredXp || p.theory > s.requiredXp - p.xp
            || p.practice > s.requiredXp - p.xp || (p.theory && p.practice)) return false;
        last = p.skill.value;
    }
    last = 0;
    for (const auto& r : v.recipes) {
        if (r.recipe.value <= last || r.learningRatio > ratioScale || (r.known != (r.learningRatio == ratioScale))) return false;
        last = r.recipe.value;
    }
    last = 0;
    bool activeFound = !v.activeBook.value;
    for (const auto& b : v.books) {
        if (b.book.value <= last || (b.used && b.eligible)) return false;
        if (b.book == v.activeBook) { if (b.used) return false; activeFound = true; }
        last = b.book.value;
    }
    return activeFound;
}
void WriteLearning(Writer& w, const LearningView& v) {
    w.U32(static_cast<std::uint32_t>(v.skills.size()));
    for (const auto& s : v.skills) {
        const auto& p = s.progress;
        w.U32(p.skill.value); w.U8(p.level); w.U64(p.xp); w.U64(p.theory); w.U64(p.practice);
        w.U64(p.directBookXp); w.U32(p.multiplier); w.U32(p.workRemainder); w.U32(p.boostRemainder);
        w.I32(s.effectiveLevel); w.U64(s.requiredXp);
    }
    w.U32(static_cast<std::uint32_t>(v.recipes.size()));
    for (const auto& r : v.recipes) {
        w.U32(r.recipe.value); w.U8(r.known); w.U8(r.referenceAvailable); w.U8(r.requirementsMet); w.U32(r.learningRatio);
    }
    w.U32(static_cast<std::uint32_t>(v.books.size()));
    for (const auto& b : v.books) {
        w.U32(b.book.value); w.U8(b.accessible); w.U8(b.used); w.U8(b.eligible);
        w.U64(b.completedWork); w.U64(b.cappedDirectXp); w.U64(b.cappedTheoryXp);
    }
    w.U32(v.activeBook.value); w.U64(v.studyRemaining); w.U8(v.studyPauseRequested);
}
bool ReadLearning(Reader& r, LearningView& v) {
    v = {};
    std::uint32_t count;
    if (!r.U32(count) || count > maxPacket / 61) return false;
    for (std::uint32_t i = 0; i < count; ++i) {
        SkillView s; auto& p = s.progress;
        if (!r.U32(p.skill.value) || !r.U8(p.level) || !r.U64(p.xp) || !r.U64(p.theory) || !r.U64(p.practice)
            || !r.U64(p.directBookXp) || !r.U32(p.multiplier) || !r.U32(p.workRemainder)
            || !r.U32(p.boostRemainder) || !r.I32(s.effectiveLevel) || !r.U64(s.requiredXp)) return false;
        v.skills.push_back(s);
    }
    if (!r.U32(count) || count > maxPacket / 11) return false;
    for (std::uint32_t i = 0; i < count; ++i) {
        RecipeKnowledgeView value; std::uint8_t known, reference, met;
        if (!r.U32(value.recipe.value) || !r.U8(known) || known > 1 || !r.U8(reference) || reference > 1
            || !r.U8(met) || met > 1 || !r.U32(value.learningRatio)) return false;
        value.known = known; value.referenceAvailable = reference; value.requirementsMet = met;
        v.recipes.push_back(value);
    }
    if (!r.U32(count) || count > maxPacket / 31) return false;
    for (std::uint32_t i = 0; i < count; ++i) {
        BookStudyView b; std::uint8_t accessible, used, eligible;
        if (!r.U32(b.book.value) || !r.U8(accessible) || accessible > 1 || !r.U8(used) || used > 1
            || !r.U8(eligible) || eligible > 1 || !r.U64(b.completedWork)
            || !r.U64(b.cappedDirectXp) || !r.U64(b.cappedTheoryXp)) return false;
        b.accessible = accessible; b.used = used; b.eligible = eligible; v.books.push_back(b);
    }
    std::uint8_t pause;
    if (!r.U32(v.activeBook.value) || !r.U64(v.studyRemaining) || !r.U8(pause) || pause > 1) return false;
    v.studyPauseRequested = pause;
    return ValidLearning(v);
}
void WriteStorage(Writer& w, const ItemStorage& storage) {
    w.U8(static_cast<std::uint8_t>(storage.kind));
    w.I32(storage.position.x); w.I32(storage.position.z); w.I32(storage.position.y); w.U64(storage.id);
}
bool ReadStorage(Reader& r, ItemStorage& storage) {
    std::uint8_t kind;
    if (!r.U8(kind) || !r.I32(storage.position.x) || !r.I32(storage.position.z)
        || !r.I32(storage.position.y) || !r.U64(storage.id)) return false;
    storage.kind = static_cast<ItemStorageKind>(kind);
    return ValidItemStorage(storage);
}
bool ValidInventoryResult(const CommandResult& result) {
    if (static_cast<unsigned>(result.inventoryOutcome) > static_cast<unsigned>(InventoryOutcome::Busy)) return false;
    if (result.outcome == Outcome::inventory_refused)
        return result.inventoryOutcome != InventoryOutcome::Succeeded && !result.advanced && !result.cost;
    if (result.inventoryOutcome != InventoryOutcome::Succeeded) return false;
    if (result.outcome == Outcome::wielded || result.outcome == Outcome::unwielded || result.outcome == Outcome::transferred)
        return result.advanced && result.cost != 0;
    return true;
}
// Semantic object kinds are fixed protocol constants, never content indices.
static_assert(static_cast<unsigned>(ObjectKind::None) == 0 && static_cast<unsigned>(ObjectKind::Generic) == 1 &&
    static_cast<unsigned>(ObjectKind::Chair) == 2 && static_cast<unsigned>(ObjectKind::Table) == 3 &&
    static_cast<unsigned>(ObjectKind::Lamp) == 4);
bool ValidCombatView(const CombatView& v) {
    const auto& r = v.feedback.result;
    if (static_cast<unsigned>(v.bodyKind) > static_cast<unsigned>(BodyKind::Simple) ||
        static_cast<unsigned>(v.style) > static_cast<unsigned>(CombatStyle::Defensive) ||
        static_cast<unsigned>(v.action) > static_cast<unsigned>(ActorActionKind::AbilityWindup) ||
        v.stamina > 100 || v.accuracyPenalty > 30 ||
        static_cast<unsigned>(r.outcome) > static_cast<unsigned>(AttackOutcome::Defeated) ||
        static_cast<std::uint32_t>(r.move) > static_cast<std::uint32_t>(CombatMove::Parry) ||
        static_cast<unsigned>(r.part) > static_cast<unsigned>(BodyPart::Count) ||
        static_cast<unsigned>(r.disruption) > static_cast<unsigned>(DisruptionResponse::Canceled) ||
        v.life > ActorLifeState::Dead || r.reason > CombatRefusal::InvalidDefinition || r.resolvedStrikes > r.strikeCount) return false;
    for (std::size_t i = 0; i < body_part_count; ++i) {
        const auto part = static_cast<BodyPart>(i);
        const auto& p = v.parts[i];
        if (p.hp > p.maximum || p.maximum > UnreducedMax(part) ||
            (!HasPart(v.bodyKind, part) && (p.hp || p.maximum || p.broken))) return false;
    }
    return true;
}
bool ValidEnemyHealth(const WorldView& view, const EnemyHealthView& health) {
    if (health.cellIndex >= view.cells.size() || health.bodyKind > BodyKind::Simple) return false;
    const auto& cell = view.cells[health.cellIndex];
    if (cell.overlayVisibility != Visibility::Visible || !cell.actor || cell.player) return false;
    bool injured = false;
    for (std::size_t p = 0; p < body_part_count; ++p) {
        const auto part = static_cast<BodyPart>(p);
        const auto ratio = health.partRatios[p];
        if (HasPart(health.bodyKind, part)) {
            const bool vital = part == BodyPart::Head || part == BodyPart::Torso || part == BodyPart::Core;
            if (!ValidUnitRatio(ratio) || (vital && !ratio)) return false;
            injured |= ratio < ratioScale;
        } else if (ratio) { return false; }
    }
    return injured;
}

bool WriteEnemyHealth(Writer& w, const WorldView& view) {
    if (view.enemyHealth.size() > view.cells.size()) return false;
    w.U16(static_cast<std::uint16_t>(view.enemyHealth.size()));
    int previous = -1;
    for (const auto& health : view.enemyHealth) {
        if (health.cellIndex <= previous || !ValidEnemyHealth(view, health)) return false;
        previous = health.cellIndex;
        w.U16(health.cellIndex);
        w.U8(static_cast<std::uint8_t>(health.bodyKind));
        for (std::size_t p = 0; p < body_part_count; ++p) {
            if (HasPart(health.bodyKind, static_cast<BodyPart>(p))) w.U16(health.partRatios[p]);
        }
    }
    return true;
}

bool ReadEnemyHealth(Reader& r, WorldView& view) {
    constexpr std::size_t minimumRecordSize = 2 + 1 + 3 * 2;
    std::uint16_t count;
    if (!r.U16(count) || count > view.cells.size() || count > (r.bytes.size() - r.offset) / minimumRecordSize) return false;
    view.enemyHealth.reserve(count);
    int previous = -1;
    for (unsigned i = 0; i < count; ++i) {
        EnemyHealthView health;
        if (!r.U16(health.cellIndex) || health.cellIndex <= previous || !ReadEnum(r, health.bodyKind, BodyKind::Simple)) return false;
        for (std::size_t p = 0; p < body_part_count; ++p) {
            if (HasPart(health.bodyKind, static_cast<BodyPart>(p)) && !r.U16(health.partRatios[p])) return false;
        }
        if (!ValidEnemyHealth(view, health)) return false;
        previous = health.cellIndex;
        view.enemyHealth.push_back(health);
    }
    return true;
}

void WriteCombat(Writer& w, const CombatView& v) {
    w.U8(static_cast<std::uint8_t>(v.bodyKind)); w.U8(static_cast<std::uint8_t>(v.style));
    w.U8(static_cast<std::uint8_t>(v.action)); w.U32(v.stamina); w.U8(v.accuracyPenalty);
    for (const auto& p : v.parts) { w.U32(p.hp); w.U32(p.maximum); w.U8(p.broken ? 1 : 0); }
    const auto& r = v.feedback.result;
    w.U8(static_cast<std::uint8_t>(r.outcome)); w.U32(static_cast<std::uint32_t>(r.move));
    w.U64(r.target); w.U8(static_cast<std::uint8_t>(r.part)); w.U32(r.injury); w.U64(r.cost);
    w.U8(r.committed ? 1 : 0); w.U8(r.contact ? 1 : 0); w.U8(r.lostCapability ? 1 : 0);
    w.U8(static_cast<std::uint8_t>(r.disruption)); w.U8(v.feedback.incoming ? 1 : 0); w.U64(v.feedback.tick);
    w.U32(r.moveId.value); w.U8(static_cast<std::uint8_t>(r.reason)); w.U32(r.strikeCount); w.U32(r.resolvedStrikes); w.U64(r.totalHpLoss);
    w.U32(v.styleId.value); w.U8(static_cast<std::uint8_t>(v.life)); w.U8(v.inexhaustible);
    w.U64(v.staggerRemaining); w.U64(v.focusRemaining);
}
bool ReadCombat(Reader& r, CombatView& v) {
    std::uint8_t body, style, action;
    if (!r.U8(body) || !r.U8(style) || !r.U8(action) || !r.U32(v.stamina) || !r.U8(v.accuracyPenalty)) return false;
    v.bodyKind = static_cast<BodyKind>(body); v.style = static_cast<CombatStyle>(style);
    v.action = static_cast<ActorActionKind>(action);
    for (auto& p : v.parts) {
        std::uint8_t broken;
        if (!r.U32(p.hp) || !r.U32(p.maximum) || !r.U8(broken) || broken > 1) return false;
        p.broken = broken != 0;
    }
    auto& a = v.feedback.result;
    std::uint8_t outcome, part, committed, contact, lost, disruption, incoming;
    std::uint32_t move;
    if (!r.U8(outcome) || !r.U32(move) || !r.U64(a.target) || !r.U8(part) || !r.U32(a.injury) || !r.U64(a.cost) ||
        !r.U8(committed) || committed > 1 || !r.U8(contact) || contact > 1 || !r.U8(lost) || lost > 1 ||
        !r.U8(disruption) || !r.U8(incoming) || incoming > 1 || !r.U64(v.feedback.tick)) return false;
    a.outcome = static_cast<AttackOutcome>(outcome); a.move = static_cast<CombatMove>(move);
    a.part = static_cast<BodyPart>(part); a.committed = committed != 0; a.contact = contact != 0;
    a.lostCapability = lost != 0; a.disruption = static_cast<DisruptionResponse>(disruption);
    v.feedback.incoming = incoming != 0;
    if (!r.U32(a.moveId.value) || !ReadEnum(r, a.reason, CombatRefusal::InvalidDefinition)
        || !r.U32(a.strikeCount) || !r.U32(a.resolvedStrikes) || !r.U64(a.totalHpLoss)
        || !r.U32(v.styleId.value) || !ReadEnum(r, v.life, ActorLifeState::Dead) || !ReadBool(r, v.inexhaustible)
        || !r.U64(v.staggerRemaining) || !r.U64(v.focusRemaining)) return false;
    return ValidCombatView(v);
}
bool ValidAvailability(const CombatAvailability& v) {
    // Effective costs include grip, injury and encumbrance scaling; authored
    // definition limits are not limits on these derived values.
    return v.reason <= CombatRefusal::InvalidDefinition && v.cost.recoveryCost;
}
void WriteAvailability(Writer& w, const CombatAvailability& v) {
    w.U8(static_cast<std::uint8_t>(v.reason)); w.U32(v.cost.windupCost); w.U32(v.cost.recoveryCost); w.U32(v.cost.staminaCost);
}
bool ReadAvailability(Reader& r, CombatAvailability& v) {
    return ReadEnum(r, v.reason, CombatRefusal::InvalidDefinition) && r.U32(v.cost.windupCost)
        && r.U32(v.cost.recoveryCost) && r.U32(v.cost.staminaCost) && ValidAvailability(v);
}
bool ValidCombatInfo(const CombatInfoView& v) {
    if (!ValidCombatView(v.combat) || v.equipment.worn.size() > maxItemEntries
        || v.moves.size() > maxPacket / 26 || v.abilities.size() > maxPacket / 31
        || v.arts.size() > maxPacket / 12 || v.manuals.size() > maxPacket / 30) return false;
    for (const auto& h : v.equipment.hands)
        if (h.grip > GripKind::TwoHanded || (!h.item && (h.usable || h.grip != GripKind::OneHanded))) return false;
    const auto& left = v.equipment.hands[0];
    const auto& right = v.equipment.hands[1];
    if (left.grip == GripKind::TwoHanded || right.grip == GripKind::TwoHanded) {
        if (!left.item || left.item != right.item || left.grip != right.grip || left.usable != right.usable) return false;
    } else if (left.item && left.item == right.item) return false;
    for (std::size_t i = 0; i < v.equipment.worn.size(); ++i) {
        if (!v.equipment.worn[i]) return false;
        for (std::size_t j = 0; j < i; ++j) if (v.equipment.worn[i] == v.equipment.worn[j]) return false;
    }
    for (std::size_t i = 0; i < v.moves.size(); ++i)
        if (!v.moves[i].move.value || !ValidAvailability(v.moves[i].availability)
            || (i && v.moves[i - 1].move.value >= v.moves[i].move.value)) return false;
    for (std::size_t i = 0; i < v.abilities.size(); ++i) {
        const auto& a = v.abilities[i];
        if (!a.ability.value || !ValidGrant(a.source) || !ValidAvailability(a.availability)
            || a.targeting > AbilityTargetKind::CardinalDirection) return false;
        for (std::size_t j = 0; j < i; ++j)
            if (a.ability == v.abilities[j].ability && a.source == v.abilities[j].source) return false;
    }
    for (std::size_t i = 0; i < v.arts.size(); ++i)
        if (!v.arts[i].art.value || (i && v.arts[i - 1].art.value >= v.arts[i].art.value)) return false;
    unsigned active = 0;
    for (std::size_t i = 0; i < v.manuals.size(); ++i) {
        const auto& m = v.manuals[i];
        if (!m.book.value || !m.art.value || m.reason > CombatRefusal::InvalidDefinition
            || m.successChance > ratioScale || m.remaining > m.duration || (m.active && ++active > 1)) return false;
        for (std::size_t j = 0; j < i; ++j)
            if (m.book == v.manuals[j].book && m.art == v.manuals[j].art) return false;
    }
    return true;
}
void WriteCombatInfo(Writer& w, const CombatInfoView& v) {
    WriteCombat(w, v.combat);
    for (const auto& h : v.equipment.hands) { w.U64(h.item); w.U8(static_cast<std::uint8_t>(h.grip)); w.U8(h.usable); }
    w.U32(static_cast<std::uint32_t>(v.equipment.worn.size()));
    for (auto id : v.equipment.worn) w.U64(id);
    for (auto value : v.equipment.encumbrance) w.U32(value);
    w.U32(static_cast<std::uint32_t>(v.moves.size()));
    for (const auto& m : v.moves) { w.U32(m.move.value); WriteAvailability(w, m.availability); w.U64(m.cooldownRemaining); w.U8(m.unlocked); }
    w.U32(static_cast<std::uint32_t>(v.abilities.size()));
    for (const auto& a : v.abilities) {
        w.U32(a.ability.value); WriteGrant(w, a.source); WriteAvailability(w, a.availability);
        w.U64(a.cooldownRemaining); w.U8(static_cast<std::uint8_t>(a.targeting));
    }
    w.U32(static_cast<std::uint32_t>(v.arts.size()));
    for (const auto& a : v.arts) { w.U32(a.art.value); w.U64(a.xp); }
    w.U32(static_cast<std::uint32_t>(v.manuals.size()));
    for (const auto& m : v.manuals) {
        w.U32(m.book.value); w.U32(m.art.value); w.U8(static_cast<std::uint8_t>(m.reason));
        w.U32(m.successChance); w.U64(m.duration); w.U64(m.remaining); w.U8(m.active);
    }
}
bool ReadCount(Reader& r, std::uint32_t& count, std::size_t minimum) {
    return r.U32(count) && count <= maxPacket / minimum && count <= (r.bytes.size() - r.offset) / minimum;
}
bool ReadCombatInfo(Reader& r, CombatInfoView& v) {
    v = {};
    if (!ReadCombat(r, v.combat)) return false;
    for (auto& h : v.equipment.hands)
        if (!r.U64(h.item) || !ReadEnum(r, h.grip, GripKind::TwoHanded) || !ReadBool(r, h.usable)) return false;
    std::uint32_t count;
    if (!ReadCount(r, count, 8) || count > maxItemEntries) return false;
    for (std::uint32_t i = 0; i < count; ++i) { PersistentId id; if (!r.U64(id)) return false; v.equipment.worn.push_back(id); }
    for (auto& value : v.equipment.encumbrance) if (!r.U32(value)) return false;
    if (!ReadCount(r, count, 26)) return false;
    for (std::uint32_t i = 0; i < count; ++i) {
        CombatMoveView m;
        if (!r.U32(m.move.value) || !ReadAvailability(r, m.availability) || !r.U64(m.cooldownRemaining) || !ReadBool(r, m.unlocked)) return false;
        v.moves.push_back(m);
    }
    if (!ReadCount(r, count, 31)) return false;
    for (std::uint32_t i = 0; i < count; ++i) {
        AbilityView a;
        if (!r.U32(a.ability.value) || !ReadGrant(r, a.source) || !ReadAvailability(r, a.availability)
            || !r.U64(a.cooldownRemaining) || !ReadEnum(r, a.targeting, AbilityTargetKind::CardinalDirection)) return false;
        v.abilities.push_back(a);
    }
    if (!ReadCount(r, count, 12)) return false;
    for (std::uint32_t i = 0; i < count; ++i) { ArtProgress a; if (!r.U32(a.art.value) || !r.U64(a.xp)) return false; v.arts.push_back(a); }
    if (!ReadCount(r, count, 30)) return false;
    for (std::uint32_t i = 0; i < count; ++i) {
        ArtManualView m;
        if (!r.U32(m.book.value) || !r.U32(m.art.value) || !ReadEnum(r, m.reason, CombatRefusal::InvalidDefinition)
            || !r.U32(m.successChance) || !r.U64(m.duration) || !r.U64(m.remaining) || !ReadBool(r, m.active)) return false;
        v.manuals.push_back(m);
    }
    return ValidCombatInfo(v);
}
bool ValidEvent(const CombatEvent& e) {
    const auto& s = e.strike;
    return e.kind <= CombatEventKind::ActorDied && e.reason <= CombatRefusal::InvalidDefinition
        && s.outcome <= StrikeOutcome::Contact && s.reason <= CombatRefusal::InvalidDefinition
        && s.response <= DefenseKind::Block && s.region <= ContactRegion::Count && s.injuryPart <= BodyPart::Count
        && s.disruption <= DisruptionResponse::Canceled && s.postProtectionDamage <= s.incomingDamage;
}
bool ValidEventHistory(eastl::span<const CombatEvent> events) {
    if (events.size() > maxPacket / combatEventWireSize) return false;
    std::uint64_t previous = 0;
    for (const auto& e : events) {
        if (!ValidEvent(e) || e.serial <= previous) return false;
        previous = e.serial;
    }
    return true;
}
void WriteEvent(Writer& w, const CombatEvent& e) {
    const auto& s = e.strike;
    w.U64(e.tick); w.U64(s.attacker); w.U64(s.target); w.U32(s.move.value);
    w.U8(static_cast<std::uint8_t>(s.outcome)); w.U8(static_cast<std::uint8_t>(s.reason)); w.U8(static_cast<std::uint8_t>(s.response));
    w.U32(s.responseMove.value); w.U8(s.defenseSucceeded); w.U8(s.armorStopped); w.U8(s.killed); w.U8(s.counter);
    w.U8(static_cast<std::uint8_t>(s.region)); w.U8(static_cast<std::uint8_t>(s.injuryPart));
    w.U64(s.blockAbsorbed); w.U64(s.armorAbsorbed); w.U64(s.incomingDamage); w.U64(s.postProtectionDamage);
    w.U32(s.hpLoss); w.U32(s.maxHpLoss); w.U8(static_cast<std::uint8_t>(s.disruption));
    w.U8(static_cast<std::uint8_t>(e.kind)); w.U64(e.actor); w.U64(e.opponent); w.U32(e.move.value);
    w.U32(e.ability.value); w.U32(e.art.value); w.U8(static_cast<std::uint8_t>(e.reason));
    w.U64(e.cost); w.U32(e.staminaSpent); w.U8(e.learnedArt);
    w.U64(e.serial);
}
bool ReadEvent(Reader& r, CombatEvent& e) {
    auto& s = e.strike;
    return r.U64(e.tick) && r.U64(s.attacker) && r.U64(s.target) && r.U32(s.move.value)
        && ReadEnum(r, s.outcome, StrikeOutcome::Contact) && ReadEnum(r, s.reason, CombatRefusal::InvalidDefinition)
        && ReadEnum(r, s.response, DefenseKind::Block) && r.U32(s.responseMove.value)
        && ReadBool(r, s.defenseSucceeded) && ReadBool(r, s.armorStopped) && ReadBool(r, s.killed) && ReadBool(r, s.counter)
        && ReadEnum(r, s.region, ContactRegion::Count) && ReadEnum(r, s.injuryPart, BodyPart::Count)
        && r.U64(s.blockAbsorbed) && r.U64(s.armorAbsorbed) && r.U64(s.incomingDamage) && r.U64(s.postProtectionDamage)
        && r.U32(s.hpLoss) && r.U32(s.maxHpLoss) && ReadEnum(r, s.disruption, DisruptionResponse::Canceled)
        && ReadEnum(r, e.kind, CombatEventKind::ActorDied) && r.U64(e.actor) && r.U64(e.opponent)
        && r.U32(e.move.value) && r.U32(e.ability.value) && r.U32(e.art.value)
        && ReadEnum(r, e.reason, CombatRefusal::InvalidDefinition) && r.U64(e.cost) && r.U32(e.staminaSpent)
        && ReadBool(r, e.learnedArt) && r.U64(e.serial) && ValidEvent(e);
}
bool ValidViewBounds(const WorldView& view, bool inspection = false) {
    return view.width >= 1 && view.width <= maxViewDimension && view.height == view.width && (view.width & 1) != 0 &&
        ValidPosition(view.player_position) && view.origin.y >= world_min_y && view.origin.y <= world_max_y &&
        (inspection || view.origin.y == view.player_position.y) &&
        static_cast<std::int64_t>(view.origin.x) + view.width / 2 == view.player_position.x &&
        static_cast<std::int64_t>(view.origin.z) + view.height / 2 == view.player_position.z &&
        static_cast<std::int64_t>(view.origin.x) + view.width - 1 <= INT32_MAX &&
        static_cast<std::int64_t>(view.origin.z) + view.height - 1 <= INT32_MAX;
}
bool ValidCell(const ViewCell& cell, const WorldView& view, std::size_t index) {
    if (cell.terrainVisibility > Visibility::Visible || cell.overlayVisibility > Visibility::Visible ||
        cell.terrainLight > LightBand::Bright || cell.overlayLight > LightBand::Bright ||
        !ValidObjectAppearance(cell.objectAppearance)) return false;
    const bool terrain = cell.terrainVisibility != Visibility::Unknown;
    const bool overlay = cell.overlayVisibility != Visibility::Unknown;
    const bool object = cell.objectAppearance.kind != ObjectKind::None;
    if (cell.monsterType.value &&
        (!cell.actor || cell.player || cell.overlayVisibility != Visibility::Visible)) return false;
    if (!terrain && (cell.tile != Tile{} || cell.terrainDepth || cell.terrainLight != LightBand::Dark)) return false;
    if (cell.terrainVisibility == Visibility::Remembered &&
        (cell.tile.damage || cell.terrainLight != LightBand::Dark)) return false;
    if (!overlay && (cell.actor || cell.player || cell.ground_items || cell.object ||
        cell.objectAppearance != ObjectAppearance{} || cell.overlayDepth || cell.overlayLight != LightBand::Dark)) return false;
    if (overlay && !(cell.actor || cell.ground_items || object)) return false;
    if (cell.overlayVisibility == Visibility::Remembered &&
        (cell.actor || cell.player || cell.ground_items || cell.object || !object || cell.overlayLight != LightBand::Dark)) return false;
    if (cell.overlayVisibility == Visibility::Visible && ((cell.object != 0) != object)) return false;
    if (!terrain && !overlay) return true;
    const auto maxDepth = static_cast<unsigned>(view.origin.y - world_min_y);
    return (!terrain || cell.tile.terrain.value != 0) && cell.terrainDepth < world_layer_count &&
        cell.overlayDepth < world_layer_count && cell.terrainDepth <= maxDepth && cell.overlayDepth <= maxDepth &&
        (overlay || cell.overlayDepth == 0) && (!cell.player || cell.actor != 0) &&
        ValidPosition({view.origin.x + static_cast<int>(index % view.width),
            view.origin.z + static_cast<int>(index / view.width), view.origin.y});
}
bool ValidCapability(ItemCapabilityId id) {
    return id.value != 0 && id.value <= maxItemCapabilities;
}
bool WriteText(Writer& writer, const ItemText& text) {
    return writer.Text(text.key, maxItemText) && writer.Text(text.fallback, maxItemText);
}
bool ReadText(Reader& reader, ItemText& text) {
    return reader.Text(text.key, maxItemText) && reader.Text(text.fallback, maxItemText);
}
bool WriteReason(Writer& writer, const ItemUseReason& reason) {
    return writer.Text(reason.code, maxItemActionName) && WriteText(writer, reason.message);
}
bool ReadReason(Reader& reader, ItemUseReason& reason) {
    return reader.Text(reason.code, maxItemActionName) && ReadText(reader, reason.message);
}
bool ValidAction(const ItemActionInfo& action) {
    return ValidCapability(action.capability) && action.baseCost && ValidItemActionId(action.action) &&
        action.argument <= ItemActionArgument::Direction && action.category <= ItemActionCategory::ManageContents &&
        ValidItemText(action.label) && ValidItemUseReason(action.availability, action.reason);
}
const InventoryEntry* FindEntry(const Vector<InventoryEntry>& entries, PersistentId id) {
    std::size_t begin = 0, end = entries.size();
    while (begin < end) {
        const auto middle = begin + (end - begin) / 2;
        if (entries[middle].id < id) begin = middle + 1;
        else end = middle;
    }
    return begin < entries.size() && entries[begin].id == id ? &entries[begin] : nullptr;
}
bool ValidEntries(const Vector<InventoryEntry>& entries) {
    if (entries.size() > maxItemEntries) return false;
    PersistentId last = 0;
    for (const auto& entry : entries) {
        if (entry.heldHands > HandMask::Both || (entry.worn && entry.heldHands != HandMask::None)
            || ((entry.worn || entry.heldHands != HandMask::None) && (entry.parent || entry.storage.kind != ItemStorageKind::Inventory))
            || (entry.stackable && (entry.worn || entry.heldHands != HandMask::None)) || !ValidItemStorage(entry.storage)
            || (entry.parent && (entry.storage.kind != ItemStorageKind::Container || entry.storage.id != entry.parent))
            || (!entry.parent && entry.storage.kind == ItemStorageKind::Container)
            || (entry.wielded && (entry.parent || entry.storage.kind != ItemStorageKind::Inventory))
            || (entry.stackable && (entry.wielded || entry.contained_items || entry.capacityVolumeMl
                || !entry.energy.empty() || !entry.activities.empty() || !entry.actions.empty()))) return false;
        if (entry.id <= last || !entry.type.value || entry.contained_items > maxItemEntries ||
            entry.installation.value > maxItemCapabilities || (!entry.parent && entry.installation.value) ||
            entry.usedVolumeMl > entry.capacityVolumeMl || entry.energy.size() > maxItemCapabilities ||
            entry.activities.size() > maxItemCapabilities || entry.actions.size() > maxItemActions) return false;
        last = entry.id;
        std::uint16_t lastCapability = 0;
        for (const auto& energy : entry.energy) {
            if (!ValidCapability(energy.capability) || energy.capability.value <= lastCapability ||
                energy.energy > energy.capacity) return false;
            lastCapability = energy.capability.value;
        }
        lastCapability = 0;
        for (const auto& activity : entry.activities) {
            if (!ValidCapability(activity.capability) || activity.capability.value <= lastCapability ||
                !ValidItemText(activity.status)) return false;
            lastCapability = activity.capability.value;
        }
        for (std::size_t i = 0; i < entry.actions.size(); ++i) {
            if (!ValidAction(entry.actions[i])) return false;
            if (i && (entry.actions[i].capability.value < entry.actions[i - 1].capability.value ||
                (entry.actions[i].capability == entry.actions[i - 1].capability &&
                 entry.actions[i].action <= entry.actions[i - 1].action))) return false;
        }
    }
    for (const auto& entry : entries) {
        const InventoryEntry* current = &entry;
        unsigned depth = 0;
        while (current->parent) {
            if (++depth >= 64) return false;
            current = FindEntry(entries, current->parent);
            if (!current || current == &entry) return false;
        }
    }
    return true;
}
bool ValidLists(const WorldView& view) {
    if (!ValidEntries(view.inventory) || !ValidEntries(view.groundItems) || !ValidEntries(view.nearbyItems)
        || view.itemStorages.size() > maxItemStorages) return false;
    PersistentId wielded = 0;
    for (const auto& entry : view.inventory) {
        if (entry.wielded) { if (wielded) return false; wielded = entry.id; }
        if (!entry.parent && entry.storage.kind != ItemStorageKind::Inventory) return false;
        if (FindEntry(view.nearbyItems, entry.id)) return false;
    }
    if (wielded != view.wieldedItem) return false;
    for (const auto& entry : view.groundItems) {
        if (entry.wielded || (!entry.parent && (entry.storage.kind != ItemStorageKind::Ground
            || entry.storage.position != view.player_position))) return false;
        const auto* nearby = FindEntry(view.nearbyItems, entry.id);
        if (nearby && *nearby != entry) return false;
    }
    for (const auto& entry : view.nearbyItems) {
        if (entry.wielded || (!entry.parent && entry.storage.kind != ItemStorageKind::Ground
            && entry.storage.kind != ItemStorageKind::Object)) return false;
    }
    for (std::size_t i = 0; i < view.itemStorages.size(); ++i) {
        const auto& storage = view.itemStorages[i];
        if (!ValidItemStorage(storage.storage) || storage.reason > InventoryOutcome::Busy
            || storage.accessible != (storage.reason == InventoryOutcome::Succeeded)
            || (storage.massLimitGrams && storage.massGrams > *storage.massLimitGrams)
            || (storage.volumeLimitMl && storage.volumeMl > *storage.volumeLimitMl)) return false;
        if (storage.storage.kind == ItemStorageKind::Ground) {
            const auto position = storage.storage.position;
            const auto dx = std::int64_t{position.x} - view.player_position.x;
            const auto dz = std::int64_t{position.z} - view.player_position.z;
            if (position.y != view.player_position.y || dx < -1 || dx > 1 || dz < -1 || dz > 1
                || storage.massLimitGrams || storage.volumeLimitMl
                || (!storage.accessible && (storage.massGrams || storage.volumeMl))) return false;
        }
        if (storage.storage.kind == ItemStorageKind::Inventory && storage.volumeLimitMl) return false;
        if (storage.storage.kind == ItemStorageKind::Object) {
            const auto position = storage.objectPosition;
            const auto dx = std::int64_t{position.x} - view.player_position.x;
            const auto dz = std::int64_t{position.z} - view.player_position.z;
            if (!ValidPosition(position) || position.y != view.player_position.y
                || dx < -1 || dx > 1 || dz < -1 || dz > 1) return false;
        } else if (storage.objectPosition != TileCoord{}) return false;
        for (std::size_t j = 0; j < i; ++j) if (storage.storage == view.itemStorages[j].storage) return false;
    }
    for (const auto& entry : view.inventory)
        if (FindEntry(view.groundItems, entry.id)) return false;
    return true;
}
bool ValidCraftingJobs(const Vector<CraftingEntry>& jobs) {
    if (jobs.size() > maxItemEntries) return false;
    PersistentId last = 0;
    for (const auto& job : jobs) {
        if (job.id <= last || job.kind > CraftingKind::Recycle || job.status > CraftingStatus::StationBusy
            || !job.totalWork || job.completedWork >= job.totalWork
            || (job.kind == CraftingKind::Craft
                ? !job.craftingRecipe.value || job.recyclingRecipe.value
                : !job.recyclingRecipe.value || job.craftingRecipe.value)
            || (job.status == CraftingStatus::Working || job.status == CraftingStatus::PauseRequested) != (job.worker != 0)
            || (job.status == CraftingStatus::PauseRequested && !job.pauseRequested)
            || (job.status == CraftingStatus::Working && job.pauseRequested)) return false;
        last = job.id;
        PersistentId station = 0;
        for (auto id : job.stations) {
            if (!job.worker || id <= station) return false;
            station = id;
        }
    }
    return true;
}
bool ValidCraftingResult(const CommandResult& result) {
    if (result.craftingOutcome > CraftingOutcome::StationBusy) return false;
    switch (result.outcome) {
    case Outcome::grabbed: case Outcome::released: case Outcome::nothing_to_grab:
    case Outcome::grab_lost: case Outcome::grab_blocked:
        return !result.advanced && result.cost == 0;
    case Outcome::object_moved:
        return result.advanced && result.cost != 0;
    case Outcome::craft_started:
    case Outcome::recycling_started:
        return result.craftingOutcome == CraftingOutcome::Started && result.advanced;
    case Outcome::craft_resumed:
        return result.craftingOutcome == CraftingOutcome::Resumed && result.advanced;
    case Outcome::craft_paused:
        return (result.craftingOutcome == CraftingOutcome::Paused || result.craftingOutcome == CraftingOutcome::PauseRequested)
            && !result.advanced && !result.cost;
    case Outcome::craft_cancelled:
        return result.craftingOutcome == CraftingOutcome::Cancelled && !result.advanced && !result.cost;
    case Outcome::crafting_refused:
        return result.craftingOutcome >= CraftingOutcome::ActorUnavailable && !result.advanced && !result.cost;
    default: return true;
    }
}
bool WriteEntries(Writer& writer, const Vector<InventoryEntry>& entries) {
    writer.U16(static_cast<std::uint16_t>(entries.size()));
    for (const auto& entry : entries) {
        writer.U64(entry.id); writer.U32(entry.type.value); writer.U32(entry.contained_items);
        writer.U64(entry.parent); writer.U16(entry.installation.value); writer.U64(entry.massGrams);
        writer.U32(entry.volumeMl); writer.U32(entry.capacityVolumeMl); writer.U64(entry.usedVolumeMl);
        WriteStorage(writer, entry.storage); writer.U8(entry.wielded); writer.U8(entry.stackable);
        writer.U8(entry.worn); writer.U8(static_cast<std::uint8_t>(entry.heldHands));
        writer.U8(static_cast<std::uint8_t>(entry.energy.size()));
        for (const auto& energy : entry.energy) {
            writer.U16(energy.capability.value); writer.U32(energy.energy); writer.U32(energy.capacity);
        }
        writer.U8(static_cast<std::uint8_t>(entry.activities.size()));
        for (const auto& activity : entry.activities) {
            writer.U16(activity.capability.value); writer.U8(activity.active ? 1 : 0); writer.U64(activity.remainingTicks);
            if (!WriteText(writer, activity.status)) return false;
        }
        writer.U8(static_cast<std::uint8_t>(entry.actions.size()));
        for (const auto& action : entry.actions) {
            writer.U16(action.capability.value);
            if (!writer.Text(action.action, maxItemActionName)) return false;
            writer.U8(static_cast<std::uint8_t>(action.argument));
            writer.U8(static_cast<std::uint8_t>(action.availability)); writer.U32(action.baseCost);
            writer.U8(static_cast<std::uint8_t>(action.category));
            if (!WriteText(writer, action.label) || !WriteReason(writer, action.reason)) return false;
        }
        if (writer.bytes.size() > maxPacket) return false;
    }
    return true;
}
bool ReadEntries(Reader& reader, Vector<InventoryEntry>& entries) {
    std::uint16_t count;
    if (!reader.U16(count) || count > maxItemEntries) return false;
    entries.clear();
    for (unsigned i = 0; i < count; ++i) {
        InventoryEntry entry;
        if (!reader.U64(entry.id) || !reader.U32(entry.type.value) || !reader.U32(entry.contained_items) ||
            !reader.U64(entry.parent) || !reader.U16(entry.installation.value) || !reader.U64(entry.massGrams) ||
            !reader.U32(entry.volumeMl) || !reader.U32(entry.capacityVolumeMl) || !reader.U64(entry.usedVolumeMl)) return false;
        std::uint8_t n;
        std::uint8_t wielded, stackable;
        if (!ReadStorage(reader, entry.storage) || !reader.U8(wielded) || wielded > 1
            || !reader.U8(stackable) || stackable > 1) return false;
        entry.wielded = wielded != 0; entry.stackable = stackable != 0;
        if (!ReadBool(reader, entry.worn) || !ReadEnum(reader, entry.heldHands, HandMask::Both)) return false;
        if (!reader.U8(n) || n > maxItemCapabilities) return false;
        for (unsigned j = 0; j < n; ++j) {
            ItemEnergyView energy;
            if (!reader.U16(energy.capability.value) || !reader.U32(energy.energy) || !reader.U32(energy.capacity)) return false;
            entry.energy.push_back(energy);
        }
        if (!reader.U8(n) || n > maxItemCapabilities) return false;
        for (unsigned j = 0; j < n; ++j) {
            ItemBehaviorView activity; std::uint8_t active;
            if (!reader.U16(activity.capability.value) || !reader.U8(active) || active > 1 ||
                !reader.U64(activity.remainingTicks) || !ReadText(reader, activity.status)) return false;
            activity.active = active != 0; entry.activities.push_back(activity);
        }
        if (!reader.U8(n) || n > maxItemActions) return false;
        for (unsigned j = 0; j < n; ++j) {
            ItemActionInfo action; std::uint8_t argument, availability, category;
            if (!reader.U16(action.capability.value) || !reader.Text(action.action, maxItemActionName) || !reader.U8(argument) ||
                !reader.U8(availability) || !reader.U32(action.baseCost) || !reader.U8(category) ||
                !ReadText(reader, action.label) || !ReadReason(reader, action.reason)) return false;
            action.argument = static_cast<ItemActionArgument>(argument);
            action.availability = static_cast<ItemUseOutcome>(availability);
            action.category = static_cast<ItemActionCategory>(category);
            entry.actions.push_back(action);
        }
        entries.push_back(eastl::move(entry));
    }
    return true;
}
}

bool ValidTerrainDefinitions(const WorldView& view, const ContentCatalog& content) {
    for (const auto& cell : view.cells) {
        if (cell.terrainVisibility != Visibility::Unknown && !content.TryGetTerrain(cell.tile.terrain)) return false;
    }
    return true;
}

bool ValidMonsterDefinitions(const WorldView& view, const ContentCatalog& content) {
    for (const auto& cell : view.cells) {
        if (cell.monsterType.value && !content.TryGetMonster(cell.monsterType)) return false;
    }
    return true;
}

bool ValidObjectDefinitions(const WorldView& view, const ContentCatalog& content) {
    for (const auto& cell : view.cells) {
        if (!ValidObjectAppearance(cell.objectAppearance)
            || (cell.objectAppearance.type.value && !content.TryGetObject(cell.objectAppearance.type))) return false;
    }
    return true;
}

bool ValidLearningDefinitions(const WorldView& view, const ContentCatalog& content) {
    const auto& v = view.learning;
    if (!ValidLearning(v) || v.skills.size() > content.SkillCount()
        || v.recipes.size() > content.CraftingRecipeCount() || v.books.size() > content.BookCount()) return false;
    for (const auto& s : v.skills) {
        const auto& p = s.progress;
        const auto* definition = content.TryGetSkill(p.skill);
        if (!definition || s.requiredXp != SkillXpRequired(*definition, p.level)) return false;
        if (p.level == maximumSkillLevel) continue;
        const bool combat = definition->category == SkillCategory::Combat;
        if (!combat && p.level >= 5) {
            if (p.directBookXp || p.multiplier != ratioScale || p.boostRemainder) return false;
        } else if (p.theory || p.practice || p.directBookXp > (combat ? 0 : s.requiredXp * 3 / 10)
            || p.multiplier > (combat ? 12000u : 40000u)) return false;
    }
    for (const auto& r : v.recipes) if (!content.TryGetCraftingRecipe(r.recipe)) return false;
    for (const auto& b : v.books) {
        const auto* definition = content.TryGetBook(b.book);
        if (!definition) return false;
        const auto total = std::uint64_t{definition->studyWork} * ratioScale;
        if (b.completedWork > total || (!b.used && b.completedWork == total)) return false;
        std::uint64_t direct = 0, theory = 0;
        for (const auto& benefit : definition->benefits) {
            direct += std::uint64_t{benefit.directXp} * ratioScale;
            theory += std::uint64_t{benefit.theoryXp} * ratioScale;
        }
        if (b.cappedDirectXp > direct || b.cappedTheoryXp > theory
            || (!b.eligible && (b.cappedDirectXp || b.cappedTheoryXp))) return false;
        if (b.book == v.activeBook && v.studyRemaining > view.remainingTicks) return false;
    }
    return true;
}

bool ValidCombatDefinitions(const WorldView& view, const ContentCatalog& content) {
    const auto validCombat = [&](const CombatView& c) {
        return ValidCombatView(c) && (!c.styleId.value || content.TryGetCombatStyle(c.styleId))
            && (!c.feedback.result.moveId.value || content.TryGetCombatMove(c.feedback.result.moveId));
    };
    const auto& v = view.combatInfo;
    if (!validCombat(view.combat) || !validCombat(v.combat) || !ValidCombatInfo(v)
        || !ValidEventHistory({view.combatEvents.data(), view.combatEvents.size()})) return false;
    for (const auto& m : v.moves) if (!content.TryGetCombatMove(m.move)) return false;
    for (const auto& a : v.abilities) {
        const auto* definition = content.TryGetAbility(a.ability);
        if (!definition || a.targeting != definition->targeting) return false;
        if (a.source.kind == AbilityGrantSourceKind::Art) {
            const auto* art = content.TryGetCombatArt(a.source.art);
            if (!art) return false;
            bool found = false;
            for (const auto& grant : art->abilities) if (grant.ability == a.ability) found = true;
            if (!found) return false;
        }
    }
    for (const auto& a : v.arts) if (!content.TryGetCombatArt(a.art)) return false;
    for (const auto& h : v.equipment.hands) {
        if (!h.item) continue;
        const auto* item = FindEntry(view.inventory, h.item);
        const auto* definition = item ? content.TryGetItem(item->type) : nullptr;
        if (!definition || item->parent || item->worn) return false;
        bool found = false;
        for (const auto& grip : ItemMelee(*definition).grips) if (grip.kind == h.grip) found = true;
        if (!found) return false;
    }
    for (auto id : v.equipment.worn) {
        const auto* item = FindEntry(view.inventory, id);
        const auto* definition = item ? content.TryGetItem(item->type) : nullptr;
        if (!definition || !definition->armor || !item->worn || item->parent) return false;
    }
    for (const auto& m : v.manuals) {
        const auto* book = content.TryGetBook(m.book);
        if (!book || !content.TryGetCombatArt(m.art)) return false;
        bool found = false;
        for (const auto& manual : book->artManuals)
            if (manual.art == m.art && manual.successChance == m.successChance) found = true;
        if (!found) return false;
    }
    for (const auto& e : view.combatEvents) {
        if (!ValidEvent(e) || (e.move.value && !content.TryGetCombatMove(e.move))
            || (e.strike.move.value && !content.TryGetCombatMove(e.strike.move))
            || (e.strike.responseMove.value && !content.TryGetCombatMove(e.strike.responseMove))
            || (e.ability.value && !content.TryGetAbility(e.ability))
            || (e.art.value && !content.TryGetCombatArt(e.art))) return false;
    }
    return true;
}

bool ValidItemDefinitions(const WorldView& view, const ContentCatalog& content) {
    if (!ValidStations(view) || !ValidLists(view) || !ValidCraftingJobs(view.craftingJobs) || !ValidLearningDefinitions(view, content)
        || !ValidCombatDefinitions(view, content)) return false;
    for (const auto& station : view.stations) {
        const auto* definition = content.TryGetObject(station.type);
        if (!definition || (definition->toolTraits.Empty() && !definition->packingItem.value)
            || (!definition->packingItem.value && station.packing == StationActionOutcome::Packed)) return false;
    }
    for (const auto& job : view.craftingJobs) {
        if (job.kind == CraftingKind::Craft) {
            const auto* recipe = content.TryGetCraftingRecipe(job.craftingRecipe);
            if (!recipe || recipe->work != job.totalWork) return false;
        } else {
            const auto* recipe = content.TryGetRecyclingRecipe(job.recyclingRecipe);
            if (!recipe || recipe->work != job.totalWork) return false;
        }
    }
    for (const auto* entries : {&view.inventory, &view.groundItems, &view.nearbyItems}) {
        for (const auto& entry : *entries) {
            const auto* definition = content.TryGetItem(entry.type);
            if (!definition || (!definition->unfinishedCraft && entry.volumeMl != definition->volumeMl)) return false;
            if (entry.stackable && (!definition->capabilities.empty() || definition->unfinishedCraft)) return false;
            const auto* container = ItemContainer(*definition);
            if (definition->unfinishedCraft) {
                if (!container || entry.volumeMl > container->capacityVolumeMl || entry.contained_items
                    || entry.capacityVolumeMl || entry.usedVolumeMl) return false;
                for (const auto& child : *entries) if (child.parent == entry.id) return false;
            } else if (entry.capacityVolumeMl != (container ? container->capacityVolumeMl : 0)) return false;
            std::uint64_t mass = definition->massGrams, usedVolume = 0;
            std::size_t children = 0;
            for (const auto& child : *entries) {
                if (child.parent != entry.id) continue;
                ++children;
                if (child.massGrams > UINT64_MAX - mass) return false;
                mass += child.massGrams;
                if (!child.installation.value) {
                    if (!container || child.volumeMl > UINT64_MAX - usedVolume) return false;
                    usedVolume += child.volumeMl;
                } else {
                    for (const auto& sibling : *entries)
                        if (sibling.id != child.id && sibling.parent == entry.id &&
                            sibling.installation == child.installation) return false;
                }
            }
            if (children != entry.contained_items || (!definition->unfinishedCraft && mass != entry.massGrams)
                || usedVolume != entry.usedVolumeMl) return false;
            if (entry.installation.value) {
                const auto* parent = FindEntry(*entries, entry.parent);
                const auto* parentDefinition = parent ? content.TryGetItem(parent->type) : nullptr;
                const auto* behavior = parentDefinition ? ItemBehaviorConfig(*parentDefinition, entry.installation) : nullptr;
                if (!behavior || !behavior->AcceptsInstallation(*definition)) return false;
            }
            if (entry.energy.size() != definition->energyStores.size() ||
                entry.activities.size() != definition->behaviors.size()) return false;
            for (const auto& energy : entry.energy) {
                const auto* config = ItemEnergyStore(*definition, energy.capability);
                if (!config || energy.capacity != config->capacityEnergy) return false;
            }
            for (const auto& activity : entry.activities) {
                const auto* behavior = ItemBehaviorConfig(*definition, activity.capability);
                if (!behavior) return false;
                const auto& schema = behavior->Schema();
                if ((!schema.hasActivity && activity.active) || activity.remainingTicks > schema.maximumDelay ||
                    ((!schema.countdown || !activity.active) && activity.remainingTicks)) return false;
            }
            for (const auto& action : entry.actions) {
                if (!ValidAction(action)) return false;
                const auto* behavior = ItemBehaviorConfig(*definition, action.capability);
                const auto* declared = behavior ? behavior->FindAction(action.action) : nullptr;
                if (!declared || action.baseCost != declared->baseCost || action.argument != declared->argument ||
                    action.category != declared->category || action.label != declared->label) return false;
            }
        }
    }
    return true;
}

void Writer::U8(std::uint8_t v) { bytes.push_back(v); }
void Writer::U16(std::uint16_t v) { U8(static_cast<std::uint8_t>(v >> 8)); U8(static_cast<std::uint8_t>(v)); }
void Writer::U32(std::uint32_t v) { U16(static_cast<std::uint16_t>(v >> 16)); U16(static_cast<std::uint16_t>(v)); }
void Writer::U64(std::uint64_t v) { U32(static_cast<std::uint32_t>(v >> 32)); U32(static_cast<std::uint32_t>(v)); }
void Writer::I32(std::int32_t v) { U32(static_cast<std::uint32_t>(v)); }
bool Writer::Text(eastl::string_view v, std::size_t limit) {
    if (v.size() > limit || v.size() > 255) return false;
    for (unsigned char c : v) if (c < 32 || c == 127) return false;
    U8(static_cast<std::uint8_t>(v.size()));
    for (char c : v) U8(static_cast<std::uint8_t>(c));
    return true;
}
bool Writer::CommandValue(const Command& v) {
    if (!ValidCommandShape(v) || static_cast<unsigned>(v.style) > static_cast<unsigned>(CombatStyle::Defensive) || v.capability.value > maxItemCapabilities ||
        (!v.itemAction.empty() && !ValidItemActionId(v.itemAction))) return false;
    U8(static_cast<std::uint8_t>(v.kind)); I32(v.dx); I32(v.dz); U64(v.item); U32(v.waitCost);
    U16(v.capability.value);
    if (!Text(v.itemAction, maxItemActionName)) return false;
    U64(v.otherItem); I32(v.dy); U64(v.target); U8(static_cast<std::uint8_t>(v.style));
    I32(v.interaction.position.x); I32(v.interaction.position.z); I32(v.interaction.position.y);
    U64(v.interaction.object); U32(v.interaction.terrain.value);
    U8(static_cast<std::uint8_t>(v.interaction.kind)); U8(static_cast<std::uint8_t>(v.interaction.action));
    U32(v.recipe.value);
    WriteStorage(*this, v.source); WriteStorage(*this, v.destination);
    U32(v.book.value);
    U8(static_cast<std::uint8_t>(v.equipment.operation)); U64(v.equipment.item);
    U8(static_cast<std::uint8_t>(v.equipment.hand)); U8(static_cast<std::uint8_t>(v.equipment.grip));
    U32(v.combatStyle.value); U32(v.ability.ability.value); WriteGrant(*this, v.ability.source);
    U8(static_cast<std::uint8_t>(v.ability.target.kind)); U64(v.ability.target.actor);
    I32(v.ability.target.tile.x); I32(v.ability.target.tile.z); I32(v.ability.target.tile.y);
    U8(static_cast<std::uint8_t>(v.ability.target.direction)); U32(v.art.value);
    I32(v.powerBounds.min.x); I32(v.powerBounds.min.z); I32(v.powerBounds.min.y);
    I32(v.powerBounds.max.x); I32(v.powerBounds.max.z); I32(v.powerBounds.max.y);
    I32(v.position.x); I32(v.position.z); I32(v.position.y);
    return bytes.size() <= maxPacket;
}
bool Writer::Result(const PlayerCommandResult& v) {
    if (!v.player || !v.sequence || !v.opportunity || v.result.outcome < Outcome::waited ||
        v.result.outcome > Outcome::station_refused || !ValidStationResult(v.result) || !ValidPowerResult(v.result) || v.result.combatReason > CombatRefusal::InvalidDefinition || !ValidCraftingResult(v.result) || !ValidInventoryResult(v.result) || !ValidStudyResult(v.result) ||
        (v.result.storageError && (*v.result.storageError < SaveError::Io || *v.result.storageError > SaveError::NotOpen)) ||
        !ValidItemUseReason(v.result.itemOutcome, v.result.itemReason)) return false;
    U64(v.player); U64(v.sequence); U64(v.opportunity);
    U8(static_cast<std::uint8_t>(v.result.outcome)); U8(v.result.advanced ? 1 : 0);
    U64(v.result.tick); U64(v.result.cost);
    U8(v.result.storageError ? 1 : 0);
    if (v.result.storageError) U8(static_cast<std::uint8_t>(*v.result.storageError));
    U8(static_cast<std::uint8_t>(v.result.itemOutcome));
    if (!WriteReason(*this, v.result.itemReason)) return false;
    U8(static_cast<std::uint8_t>(v.result.craftingOutcome));
    if (!Text(v.result.objectName, 255)) return false;
    U8(static_cast<std::uint8_t>(v.result.inventoryOutcome));
    U8(static_cast<std::uint8_t>(v.result.studyOutcome));
    U8(static_cast<std::uint8_t>(v.result.combatReason));
    U8(static_cast<std::uint8_t>(v.result.powerOutcome));
    U8(static_cast<std::uint8_t>(v.result.stationOutcome));
    return bytes.size() <= maxPacket;
}
bool Writer::View(const WorldView& v, bool inspection) {
    if (!ValidStations(v) || !ValidPowerView(v) || !ValidViewBounds(v, inspection) ||
        v.cells.size() != static_cast<std::size_t>(v.width * v.height) ||
        !ValidLists(v) || !ValidCraftingJobs(v.craftingJobs) || !ValidLearning(v.learning) || v.players.size() > 32 ||
        v.playerId == 0 || v.opportunity == 0 || v.speed == 0 || v.pacing.ticksPerSecond == 0 ||
        v.playerLight > LightBand::Bright || !ValidCombatView(v.combat) || !ValidCombatInfo(v.combatInfo)
        || !ValidEventHistory({v.combatEvents.data(), v.combatEvents.size()})) return false;
    PersistentId lastPlayer = 0;
    for (const auto& p : v.players) {
        if (p.id <= lastPlayer || p.name.empty() || p.state > PlayerConnectionState::Active || p.life > ActorLifeState::Dead) return false;
        lastPlayer = p.id;
    }
    for (std::size_t i = 0; i < v.cells.size(); ++i)
        if (!ValidCell(v.cells[i], v, i)) return false;
    I32(v.origin.x); I32(v.origin.z); I32(v.origin.y);
    U8(static_cast<std::uint8_t>(v.width)); U8(static_cast<std::uint8_t>(v.height));
    I32(v.player_position.x); I32(v.player_position.z); I32(v.player_position.y); U64(v.tick);
    U64(v.playerId); U64(v.revision); U64(v.opportunity); U64(v.lastInputSequence);
    U64(v.remainingTicks); U32(v.speed);
    U32(v.pacing.ticksPerSecond); U8(v.pacing.rateLimited ? 1 : 0);
    U8(v.pacing.pauseWhenIdle ? 1 : 0); U8(v.commandQueued ? 1 : 0);
    U16(static_cast<std::uint16_t>(v.cells.size()));
    for (std::size_t i = 0; i < v.cells.size();) {
        const ViewCell& c = v.cells[i];
        std::size_t run = 1;
        while (i + run < v.cells.size() && v.cells[i + run] == c) { ++run; }
        U16(static_cast<std::uint16_t>(run));
        U8(static_cast<std::uint8_t>(c.terrainVisibility));
        U32(c.tile.terrain.value); U8(c.tile.damage); U64(c.actor); U8(c.player ? 1 : 0);
        U32(c.ground_items); U64(c.object);
        U8(static_cast<std::uint8_t>(c.objectAppearance.kind));
        U8(static_cast<std::uint8_t>(c.objectAppearance.facing));
        U8(c.objectAppearance.active ? 1 : 0); U8(c.objectAppearance.broken ? 1 : 0);
        U8(static_cast<std::uint8_t>(c.terrainDepth)); U8(static_cast<std::uint8_t>(c.overlayDepth));
        U8(static_cast<std::uint8_t>(c.overlayVisibility));
        U8(static_cast<std::uint8_t>(c.terrainLight)); U8(static_cast<std::uint8_t>(c.overlayLight));
        U32(c.monsterType.value);
        U32(c.objectAppearance.type.value);
        U64(c.powerGridId);
        i += run;
    }
    if (!WriteEntries(*this, v.inventory) || !WriteEntries(*this, v.groundItems)) return false;
    U64(v.carriedMassGrams); U64(v.carryCapacityGrams);
    U8(static_cast<std::uint8_t>(v.players.size()));
    for (const PlayerStatus& p : v.players) {
        U64(p.id); if (!Text({p.name.data(), p.name.size()}, 32)) return false;
        U8(static_cast<std::uint8_t>(p.state)); U8(p.ready ? 1 : 0);
        U8(p.commandQueued ? 1 : 0); U64(p.opportunity); U64(p.graceMicroseconds);
        U64(p.character); U8(static_cast<std::uint8_t>(p.life));
    }
    U8(static_cast<std::uint8_t>(v.playerLight));
    WriteCombat(*this, v.combat);
    U16(static_cast<std::uint16_t>(v.craftingJobs.size()));
    for (const auto& job : v.craftingJobs) {
        U64(job.id); U8(static_cast<std::uint8_t>(job.kind));
        U32(job.craftingRecipe.value); U32(job.recyclingRecipe.value);
        U8(static_cast<std::uint8_t>(job.status)); U64(job.worker);
        U32(job.totalWork); U32(job.completedWork); U64(job.fundedTicksRemaining);
        U8(job.pauseRequested ? 1 : 0);
        U8(static_cast<std::uint8_t>(job.stations.Size()));
        for (auto station : job.stations) U64(station);
    }
    U64(v.grabbedObject);
    U64(v.wieldedItem);
    if (!WriteEntries(*this, v.nearbyItems)) return false;
    U16(static_cast<std::uint16_t>(v.itemStorages.size()));
    for (const auto& storage : v.itemStorages) {
        WriteStorage(*this, storage.storage); U8(storage.accessible); U8(static_cast<std::uint8_t>(storage.reason));
        U64(storage.massGrams); U64(storage.volumeMl);
        U8(storage.massLimitGrams.has_value()); if (storage.massLimitGrams) U64(*storage.massLimitGrams);
        U8(storage.volumeLimitMl.has_value()); if (storage.volumeLimitMl) U64(*storage.volumeLimitMl);
        if (!Text(storage.name, 255)) return false;
        I32(storage.objectPosition.x); I32(storage.objectPosition.z); I32(storage.objectPosition.y);
    }
    WriteLearning(*this, v.learning);
    U64(v.characterId); WriteCombatInfo(*this, v.combatInfo);
    U32(static_cast<std::uint32_t>(v.combatEvents.size()));
    for (const auto& e : v.combatEvents) WriteEvent(*this, e);
    if (!inspection && !WriteEnemyHealth(*this, v)) return false;
    WritePower(*this, v);
    U16(static_cast<std::uint16_t>(v.stations.size()));
    for (const auto& station : v.stations) {
        U64(station.id); U32(station.type.value);
        I32(station.position.x); I32(station.position.z); I32(station.position.y);
        U8(station.enabled); U8(station.broken); U64(station.worker); U8(static_cast<std::uint8_t>(station.packing));
    }
    return bytes.size() <= maxPacket;
}
bool Reader::U8(std::uint8_t& v) { if (offset >= bytes.size()) return false; v = bytes[offset++]; return true; }
bool Reader::U16(std::uint16_t& v) { std::uint8_t a, b; if (!U8(a) || !U8(b)) return false; v = (std::uint16_t(a) << 8) | b; return true; }
bool Reader::U32(std::uint32_t& v) { std::uint16_t a, b; if (!U16(a) || !U16(b)) return false; v = (std::uint32_t(a) << 16) | b; return true; }
bool Reader::U64(std::uint64_t& v) { std::uint32_t a, b; if (!U32(a) || !U32(b)) return false; v = (std::uint64_t(a) << 32) | b; return true; }
bool Reader::I32(std::int32_t& v) { std::uint32_t raw; if (!U32(raw)) return false; v = static_cast<std::int32_t>(raw); return true; }
bool Reader::Text(String& v, std::size_t limit) {
    std::uint8_t n; if (!U8(n) || n > limit || n > bytes.size() - offset) return false;
    for (std::size_t i = 0; i < n; ++i)
        if (bytes[offset + i] < 32 || bytes[offset + i] == 127) return false;
    v.clear(); for (std::uint8_t i = 0; i < n; ++i) v.push_back(static_cast<char>(bytes[offset++]));
    return true;
}
bool Reader::CommandValue(Command& v) {
    std::uint8_t kind, style, interactionKind, objectAction;
    v = {};
    if (bytes.size() > maxPacket || !U8(kind) || kind > static_cast<std::uint8_t>(CommandKind::pack_station) ||
        !I32(v.dx) || !I32(v.dz) || !U64(v.item) || !U32(v.waitCost) ||
        !U16(v.capability.value) || v.capability.value > maxItemCapabilities ||
        !Text(v.itemAction, maxItemActionName) || !U64(v.otherItem) || !I32(v.dy) ||
        !U64(v.target) || !U8(style) || style > static_cast<std::uint8_t>(CombatStyle::Defensive)) return false;
    if (!I32(v.interaction.position.x) || !I32(v.interaction.position.z) || !I32(v.interaction.position.y)
        || !U64(v.interaction.object) || !U32(v.interaction.terrain.value)
        || !U8(interactionKind) || interactionKind > static_cast<std::uint8_t>(InteractionKind::PullObject)
        || !U8(objectAction) || objectAction > static_cast<std::uint8_t>(ObjectAction::Refuel)
        || !U32(v.recipe.value)) { return false; }
    v.interaction.kind = static_cast<InteractionKind>(interactionKind);
    v.interaction.action = static_cast<ObjectAction>(objectAction);
    v.kind = static_cast<CommandKind>(kind);
    v.style = static_cast<CombatStyle>(style);
    if (!ReadStorage(*this, v.source) || !ReadStorage(*this, v.destination) || !U32(v.book.value)) return false;
    if (!ReadEnum(*this, v.equipment.operation, EquipmentOperation::Remove) || !U64(v.equipment.item)
        || !ReadEnum(*this, v.equipment.hand, Hand::Right) || !ReadEnum(*this, v.equipment.grip, GripKind::TwoHanded)
        || !U32(v.combatStyle.value) || !U32(v.ability.ability.value) || !ReadGrant(*this, v.ability.source)
        || !ReadEnum(*this, v.ability.target.kind, AbilityTargetKind::CardinalDirection) || !U64(v.ability.target.actor)
        || !I32(v.ability.target.tile.x) || !I32(v.ability.target.tile.z) || !I32(v.ability.target.tile.y)
        || !ReadEnum(*this, v.ability.target.direction, CardinalDirection::West) || !U32(v.art.value)) return false;
    if (!I32(v.powerBounds.min.x) || !I32(v.powerBounds.min.z) || !I32(v.powerBounds.min.y)
        || !I32(v.powerBounds.max.x) || !I32(v.powerBounds.max.z) || !I32(v.powerBounds.max.y)
        || !I32(v.position.x) || !I32(v.position.z) || !I32(v.position.y)) return false;
    return (v.itemAction.empty() || ValidItemActionId(v.itemAction)) && ValidCommandShape(v);
}
bool Reader::Result(PlayerCommandResult& v) {
    std::uint8_t outcome, advanced;
    if (bytes.size() > maxPacket || !U64(v.player) || !v.player || !U64(v.sequence) || !v.sequence ||
        !U64(v.opportunity) || !v.opportunity || !U8(outcome) ||
        outcome > static_cast<std::uint8_t>(Outcome::station_refused) || !U8(advanced) || advanced > 1 ||
        !U64(v.result.tick) || !U64(v.result.cost)) return false;
    std::uint8_t hasError;
    if (!U8(hasError) || hasError > 1) return false;
    v.result.storageError.reset();
    if (hasError) {
        std::uint8_t error;
        if (!U8(error) || error > static_cast<std::uint8_t>(SaveError::NotOpen)) return false;
        v.result.storageError = static_cast<SaveError>(error);
    }
    std::uint8_t itemOutcome, craftingOutcome;
    if (!U8(itemOutcome) || itemOutcome > static_cast<std::uint8_t>(ItemUseOutcome::Refused) ||
        !ReadReason(*this, v.result.itemReason) ||
        !U8(craftingOutcome) || craftingOutcome > static_cast<std::uint8_t>(CraftingOutcome::StationBusy)
        || !Text(v.result.objectName, 255)) return false;
    v.result.itemOutcome = static_cast<ItemUseOutcome>(itemOutcome);
    v.result.craftingOutcome = static_cast<CraftingOutcome>(craftingOutcome);
    if (!ValidItemUseReason(v.result.itemOutcome, v.result.itemReason)) return false;
    v.result.outcome = static_cast<Outcome>(outcome); v.result.advanced = advanced != 0;
    std::uint8_t inventoryOutcome;
    if (!U8(inventoryOutcome)) return false;
    v.result.inventoryOutcome = static_cast<InventoryOutcome>(inventoryOutcome);
    std::uint8_t studyOutcome;
    if (!U8(studyOutcome)) return false;
    v.result.studyOutcome = static_cast<StudyOutcome>(studyOutcome);
    if (!ReadEnum(*this, v.result.combatReason, CombatRefusal::InvalidDefinition)) return false;
    if (!ReadEnum(*this, v.result.powerOutcome, PowerActionOutcome::NoNewCoverage)) return false;
    if (!ReadEnum(*this, v.result.stationOutcome, StationActionOutcome::FixedFixture)) return false;
    return ValidStationResult(v.result) && ValidPowerResult(v.result) && ValidCraftingResult(v.result) && ValidInventoryResult(v.result) && ValidStudyResult(v.result);
}
bool Reader::View(WorldView& v, bool inspection) {
    if (bytes.size() > maxPacket) return false;
    std::uint8_t w, h, rate, pause, queued; std::uint16_t count;
    if (!I32(v.origin.x) || !I32(v.origin.z) || !I32(v.origin.y) || !U8(w) || !U8(h) || w == 0 ||
        w > maxViewDimension || h != w || (w & 1) == 0 ||
        !I32(v.player_position.x) || !I32(v.player_position.z) || !I32(v.player_position.y) || !U64(v.tick) ||
        !U64(v.playerId) || !U64(v.revision) || !U64(v.opportunity) || !U64(v.lastInputSequence) ||
        !U64(v.remainingTicks) || !U32(v.speed) || !U32(v.pacing.ticksPerSecond) ||
        !v.playerId || !v.opportunity || !v.speed || !v.pacing.ticksPerSecond ||
        !U8(rate) || rate > 1 || !U8(pause) || pause > 1 ||
        !U8(queued) || queued > 1 || !U16(count) || count != w * h) return false;
    v.width = w; v.height = h; v.pacing.rateLimited = rate; v.pacing.pauseWhenIdle = pause;
    if (!ValidViewBounds(v, inspection)) return false;
    v.commandQueued = queued;
    v.cells.clear();
    v.cells.reserve(count);
    while (v.cells.size() < count) {
        std::uint16_t run;
        if (!U16(run) || run == 0 || run > count - v.cells.size()) return false;
        ViewCell c;
        std::uint8_t terrainVisibility, overlayVisibility, terrainLight, overlayLight, kind, facing, active, broken;
        if (!U8(terrainVisibility)) return false;
        c.terrainVisibility = static_cast<Visibility>(terrainVisibility);
        std::uint8_t player, terrainDepth, overlayDepth;
        if (!U32(c.tile.terrain.value) || !U8(c.tile.damage) || !U64(c.actor) ||
            !U8(player) || player > 1 || !U32(c.ground_items) || !U64(c.object) ||
            !U8(kind) || !U8(facing) || !U8(active) || active > 1 || !U8(broken) || broken > 1 ||
            !U8(terrainDepth) || !U8(overlayDepth) || !U8(overlayVisibility) ||
            !U8(terrainLight) || !U8(overlayLight) || !U32(c.monsterType.value)) return false;
        c.player = player;
        c.objectAppearance = {static_cast<ObjectKind>(kind), static_cast<ObjectFacing>(facing), active != 0, broken != 0};
        if (!U32(c.objectAppearance.type.value) || !U64(c.powerGridId)) return false;
        c.terrainDepth = terrainDepth; c.overlayDepth = overlayDepth;
        c.overlayVisibility = static_cast<Visibility>(overlayVisibility);
        c.terrainLight = static_cast<LightBand>(terrainLight); c.overlayLight = static_cast<LightBand>(overlayLight);
        for (unsigned i = 0; i < run; ++i) {
            // A run may cross row/world boundaries. Validate each expanded
            // position, not just the first coordinate carrying this payload.
            if (!ValidCell(c, v, v.cells.size())) return false;
            v.cells.push_back(c);
        }
    }
    if (!ReadEntries(*this, v.inventory) || !ReadEntries(*this, v.groundItems) ||
        !U64(v.carriedMassGrams) || !U64(v.carryCapacityGrams)) return false;
    std::uint8_t n; if (!U8(n) || n > 32) return false;
    v.players.clear();
    PersistentId lastPlayer = 0;
    for (unsigned i = 0; i < n; ++i) {
        PlayerStatus p; std::uint8_t state, ready, pending;
        if (!U64(p.id) || p.id <= lastPlayer || !Text(p.name, 32) || p.name.empty() || !U8(state) ||
            state > static_cast<std::uint8_t>(PlayerConnectionState::Active) ||
            !U8(ready) || ready > 1 || !U8(pending) || pending > 1 ||
            !U64(p.opportunity) || !U64(p.graceMicroseconds)) return false;
        p.state = static_cast<PlayerConnectionState>(state); p.ready = ready; p.commandQueued = pending;
        if (!U64(p.character) || !ReadEnum(*this, p.life, ActorLifeState::Dead)) return false;
        lastPlayer = p.id;
        v.players.push_back(eastl::move(p));
    }
    std::uint8_t playerLight;
    if (!U8(playerLight) || playerLight > static_cast<std::uint8_t>(LightBand::Bright)) return false;
    v.playerLight = static_cast<LightBand>(playerLight);
    if (!ReadCombat(*this, v.combat) || !U16(count) || count > maxItemEntries) return false;
    v.craftingJobs.clear();
    for (unsigned i = 0; i < count; ++i) {
        CraftingEntry job;
        std::uint8_t kind, status, pauseRequested;
        if (!U64(job.id) || !U8(kind) || !U32(job.craftingRecipe.value) || !U32(job.recyclingRecipe.value)
            || !U8(status) || !U64(job.worker) || !U32(job.totalWork) || !U32(job.completedWork)
            || !U64(job.fundedTicksRemaining) || !U8(pauseRequested) || pauseRequested > 1) return false;
        job.kind = static_cast<CraftingKind>(kind); job.status = static_cast<CraftingStatus>(status);
        job.pauseRequested = pauseRequested != 0;
        std::uint8_t stationCount;
        if (!U8(stationCount) || stationCount > maxRecipeTraits) return false;
        for (unsigned j = 0; j < stationCount; ++j) {
            PersistentId station;
            if (!U64(station) || !job.stations.PushBack(station)) return false;
        }
        v.craftingJobs.push_back(job);
    }
    if (!U64(v.grabbedObject) || !U64(v.wieldedItem) || !ReadEntries(*this, v.nearbyItems)
        || !U16(count) || count > maxItemStorages) return false;
    v.itemStorages.clear();
    for (unsigned i = 0; i < count; ++i) {
        ItemStorageView storage;
        std::uint8_t accessible, reason, present;
        std::uint64_t limit;
        if (!ReadStorage(*this, storage.storage) || !U8(accessible) || accessible > 1 || !U8(reason)
            || !U64(storage.massGrams) || !U64(storage.volumeMl) || !U8(present) || present > 1) return false;
        storage.accessible = accessible != 0; storage.reason = static_cast<InventoryOutcome>(reason);
        if (present) { if (!U64(limit)) return false; storage.massLimitGrams = limit; }
        if (!U8(present) || present > 1) return false;
        if (present) { if (!U64(limit)) return false; storage.volumeLimitMl = limit; }
        if (!Text(storage.name, 255)) return false;
        if (!I32(storage.objectPosition.x) || !I32(storage.objectPosition.z) || !I32(storage.objectPosition.y)) return false;
        v.itemStorages.push_back(eastl::move(storage));
    }
    if (!ReadLearning(*this, v.learning) || !ValidCraftingJobs(v.craftingJobs) || !ValidLists(v)
        || !U64(v.characterId) || !ReadCombatInfo(*this, v.combatInfo)) return false;
    std::uint32_t eventCount;
    if (!ReadCount(*this, eventCount, combatEventWireSize)) return false;
    v.combatEvents.clear();
    std::uint64_t previousSerial = 0;
    for (std::uint32_t i = 0; i < eventCount; ++i) {
        CombatEvent e;
        if (!ReadEvent(*this, e) || e.serial <= previousSerial) return false;
        previousSerial = e.serial; v.combatEvents.push_back(e);
    }
    v.enemyHealth.clear();
    if (!inspection && !ReadEnemyHealth(*this, v)) return false;
    bool present;
    if (!I32(v.powerSettings.maxWiringWidth) || !I32(v.powerSettings.maxWiringDepth)
        || !ValidPowerSettings(v.powerSettings) || !ReadBool(*this, present)) return false;
    v.currentPower.reset();
    if (present) {
        v.currentPower.emplace(); auto& p = *v.currentPower; std::uint64_t grid;
        if (!U64(grid) || !ReadBool(*this, p.mainsAvailable) || !U64(p.batteryEnergy) || !U64(p.batteryCapacity)
            || !U64(p.generatorOutputAvailable) || !U32(p.batteries) || !U32(p.runningGenerators)
            || !U32(p.runningMachines) || !U32(p.waitingMachines) || !U32(p.unloadedDevices)) return false;
        if (grid) p.grid = PowerGridId{grid};
    }
    if (!U16(count) || count > 289) return false;
    v.stations.clear();
    for (unsigned i = 0; i < count; ++i) {
        StationView station;
        if (!U64(station.id) || !U32(station.type.value) || !I32(station.position.x) || !I32(station.position.z)
            || !I32(station.position.y) || !ReadBool(*this, station.enabled) || !ReadBool(*this, station.broken)
            || !U64(station.worker) || !ReadEnum(*this, station.packing, StationActionOutcome::FixedFixture)) return false;
        v.stations.push_back(station);
    }
    return ValidStations(v) && ValidPowerView(v);
}

namespace {
bool ValidMapItems(const WorldView& view, const Vector<MapItemVisual>& items) {
    if (items.size() != view.cells.size() || items.size() > maxViewDimension * maxViewDimension) return false;
    for (std::size_t i = 0; i < items.size(); ++i) {
        const auto& c = view.cells[i]; const auto& item = items[i];
        const bool eligible = c.overlayVisibility == Visibility::Visible && !c.actor && c.ground_items;
        if ((item.item != 0) != (item.type.value != 0) || (item.item && !eligible)) return false;
    }
    return true;
}
bool WriteMapItems(Writer& w, const WorldView& view, const Vector<MapItemVisual>& items) {
    if (!ValidMapItems(view, items)) return false;
    std::uint16_t count = 0;
    for (const auto& item : items) if (item.item) ++count;
    w.U16(count);
    for (std::size_t i = 0; i < items.size(); ++i) if (items[i].item) {
        w.U16(static_cast<std::uint16_t>(i)); w.U64(items[i].item); w.U32(items[i].type.value);
    }
    return w.bytes.size() <= maxPacket;
}
bool ReadMapItems(Reader& r, const WorldView& view, Vector<MapItemVisual>& items) {
    std::uint16_t count;
    if (view.cells.size() > maxViewDimension * maxViewDimension || !r.U16(count) || count > view.cells.size()) return false;
    items.clear(); items.resize(view.cells.size());
    int previous = -1;
    for (unsigned i = 0; i < count; ++i) {
        std::uint16_t index; MapItemVisual item;
        if (!r.U16(index) || index >= items.size() || index <= previous ||
            !r.U64(item.item) || !item.item || !r.U32(item.type.value) || !item.type.value) return false;
        items[index] = item; previous = index;
    }
    return ValidMapItems(view, items);
}
bool ValidChoice(const WorldView& view, const InteractionChoice& choice) {
    const auto& p = choice.request.position;
    const auto dx = static_cast<std::int64_t>(p.x) - view.player_position.x;
    const auto dz = static_cast<std::int64_t>(p.z) - view.player_position.z;
    return ValidInteraction(choice.request) && p.y == view.player_position.y && dx >= -1 && dx <= 1 && dz >= -1 && dz <= 1 &&
        static_cast<unsigned>(choice.reason) <= static_cast<unsigned>(ObjectUseReason::MustClose);
}
bool ValidDetails(const PlayerInspection& value) {
    if (!ValidViewBounds(value.view, true) ||
        value.view.cells.size() != static_cast<std::size_t>(value.view.width * value.view.height)) return false;
    const auto& d = value.details;
    const auto x = static_cast<std::int64_t>(value.cursor.x) - value.view.origin.x;
    const auto z = static_cast<std::int64_t>(value.cursor.z) - value.view.origin.z;
    if (value.cursor.y != value.view.origin.y || x < 0 || z < 0 || x >= value.view.width || z >= value.view.height) return false;
    const auto& c = value.view.cells[static_cast<std::size_t>(z * value.view.width + x)];
    const bool visible = c.overlayVisibility == Visibility::Visible;
    const auto dx = static_cast<std::int64_t>(value.cursor.x) - value.view.player_position.x;
    const auto dz = static_cast<std::int64_t>(value.cursor.z) - value.view.player_position.z;
    const bool nearby = value.cursor.y - static_cast<int>(c.overlayDepth) == value.view.player_position.y &&
        dx >= -1 && dx <= 1 && dz >= -1 && dz <= 1;
    if ((d.actor && (!visible || d.actor != c.actor || !d.speed)) ||
        (!d.actor && (d.speed || d.combat != CombatView{})) || !ValidCombatView(d.combat) || d.combat.feedback != CombatView{}.feedback ||
        (d.object && (!visible || d.object != c.object || d.objectKind == ObjectKind::None || d.objectKind > ObjectKind::Lamp)) ||
        (!d.object && (d.objectType.value || d.objectKind != ObjectKind::None || d.enabled || d.broken || d.machine)) ||
        (!d.machine && (d.energy || d.energyCapacity || d.hasJob || d.running || d.remainingWork || d.gridPowered
            || d.runRequested || d.fundedTicks || d.generatorOutputRemaining || d.generatorIntervalStart || d.generatorIntervalInitialized)) ||
        d.energy > d.energyCapacity || (!d.hasJob && d.remainingWork)
        || (!d.generatorIntervalInitialized && (d.generatorIntervalStart || d.generatorOutputRemaining))
        || d.fundedTicks > powerQuantumTicks
        || (d.generatorIntervalInitialized && (d.generatorIntervalStart > value.view.tick
            || d.generatorIntervalStart % powerQuantumTicks != 0))
        || d.items.size() > maxInspectionItems) return false;
    bool ground = false; unsigned previousDepth = 0; bool first = true;
    for (const auto& item : d.items) {
        if (!visible || !item.item || !item.type.value || item.depth > maxInspectionDepth ||
            (item.stored && (!d.object || !nearby || ground || item.depth == 0)) || (!item.stored && !c.ground_items)) return false;
        const bool newSection = first || (ground == false && !item.stored);
        if (newSection ? item.depth != (item.stored ? 1u : 0u) : item.depth > previousDepth + 1) return false;
        for (const auto& other : d.items) if (&item != &other && item.item == other.item) return false;
        if (!item.stored) ground = true;
        first = false; previousDepth = item.depth;
    }
    return true;
}
}
bool Writer::Presentation(const WorldView& view, const PlayerPresentation& value) {
    if (!WriteMapItems(*this, view, value.mapItems) || value.interactions.size() > maxPresentationInteractions) return false;
    U16(static_cast<std::uint16_t>(value.interactions.size()));
    for (const auto& choice : value.interactions) {
        if (!ValidChoice(view, choice)) return false;
        Command command; command.kind = CommandKind::interact_action; command.interaction = choice.request;
        if (!CommandValue(command) || !Text(choice.target, 255) || !Text(choice.label, 255)) return false;
        U8(static_cast<std::uint8_t>(choice.reason));
    }
    return bytes.size() <= maxPacket;
}
bool Reader::Presentation(const WorldView& view, PlayerPresentation& value) {
    std::uint16_t count;
    if (bytes.size() > maxPacket || !ReadMapItems(*this, view, value.mapItems) || !U16(count) || count > maxPresentationInteractions) return false;
    value.interactions.clear();
    for (unsigned i = 0; i < count; ++i) {
        Command command; InteractionChoice choice; std::uint8_t reason;
        if (!CommandValue(command) || command.kind != CommandKind::interact_action ||
            command.dx || command.dz || command.dy || command.item || command.waitCost != ticks_per_turn ||
            command.capability.value || !command.itemAction.empty() || command.otherItem || command.target ||
            command.style != CombatStyle::Balanced || !Text(choice.target, 255) ||
            !Text(choice.label, 255) || !U8(reason)) return false;
        choice.request = command.interaction; choice.reason = static_cast<ObjectUseReason>(reason);
        if (!ValidChoice(view, choice)) return false;
        value.interactions.push_back(eastl::move(choice));
    }
    return true;
}
bool Writer::Inspection(const PlayerInspection& value) {
    if (!value.requestId || !ValidPosition(value.cursor) || value.status > InspectionStatus::Unavailable) return false;
    U64(value.requestId); I32(value.cursor.x); I32(value.cursor.z); I32(value.cursor.y); U8(static_cast<std::uint8_t>(value.status));
    if (value.status != InspectionStatus::Complete) return bytes.size() <= maxPacket;
    if (!ValidDetails(value) || !View(value.view, true) || !WriteMapItems(*this, value.view, value.mapItems)) return false;
    const auto& d = value.details;
    U64(d.actor); if (d.actor) { U32(d.speed); WriteCombat(*this, d.combat); }
    U64(d.object);
    if (d.object) {
        U32(d.objectType.value); U8(static_cast<std::uint8_t>(d.objectKind)); U8(d.enabled); U8(d.broken); U8(d.machine);
        if (d.machine) { U64(d.energy); U64(d.energyCapacity); U8(d.hasJob); U8(d.running); U64(d.remainingWork); }
        if (d.machine) {
            U8(d.gridPowered); U8(d.runRequested); U64(d.fundedTicks); U64(d.generatorOutputRemaining);
            U64(d.generatorIntervalStart); U8(d.generatorIntervalInitialized);
        }
    }
    U16(static_cast<std::uint16_t>(d.items.size()));
    for (const auto& item : d.items) { U64(item.item); U32(item.type.value); U8(static_cast<std::uint8_t>(item.depth)); U8(item.stored); }
    return bytes.size() <= maxPacket;
}
bool Reader::Inspection(PlayerInspection& value) {
    value = {}; std::uint8_t status;
    if (bytes.size() > maxPacket || !U64(value.requestId) || !value.requestId || !I32(value.cursor.x) || !I32(value.cursor.z) ||
        !I32(value.cursor.y) || !ValidPosition(value.cursor) || !U8(status) || status > static_cast<unsigned>(InspectionStatus::Unavailable)) return false;
    value.status = static_cast<InspectionStatus>(status);
    if (value.status != InspectionStatus::Complete) return true;
    if (!View(value.view, true) || !ReadMapItems(*this, value.view, value.mapItems)) return false;
    auto& d = value.details;
    if (!U64(d.actor) || (d.actor && (!U32(d.speed) || !ReadCombat(*this, d.combat))) || !U64(d.object)) return false;
    if (d.object) {
        std::uint8_t kind, enabled, broken, machine;
        if (!U32(d.objectType.value) || !U8(kind) || !U8(enabled) || enabled > 1 || !U8(broken) || broken > 1 || !U8(machine) || machine > 1) return false;
        d.objectKind = static_cast<ObjectKind>(kind);
        d.enabled = enabled; d.broken = broken; d.machine = machine;
        if (d.machine) {
            std::uint8_t job, running;
            if (!U64(d.energy) || !U64(d.energyCapacity) || !U8(job) || job > 1 || !U8(running) || running > 1 || !U64(d.remainingWork)) return false;
            d.hasJob = job; d.running = running;
            if (!ReadBool(*this, d.gridPowered) || !ReadBool(*this, d.runRequested) || !U64(d.fundedTicks)
                || !U64(d.generatorOutputRemaining) || !U64(d.generatorIntervalStart)
                || !ReadBool(*this, d.generatorIntervalInitialized)) return false;
        }
    }
    std::uint16_t count;
    if (!U16(count) || count > maxInspectionItems) return false;
    for (unsigned i = 0; i < count; ++i) {
        InspectionItem item; std::uint8_t depth, stored;
        if (!U64(item.item) || !U32(item.type.value) || !U8(depth) || !U8(stored) || stored > 1) return false;
        item.depth = depth; item.stored = stored; d.items.push_back(item);
    }
    return ValidDetails(value);
}
bool ValidPresentationDefinitions(const PlayerPresentation& value, const ContentCatalog& content) {
    for (const auto& item : value.mapItems) if (item.type.value > content.ItemCount()) return false;
    for (const auto& choice : value.interactions) if (choice.request.terrain.value > content.TerrainCount()) return false;
    return true;
}
bool ValidInspectionDefinitions(const PlayerInspection& value, const ContentCatalog& content) {
    if (value.status != InspectionStatus::Complete) return true;
    if (!ValidItemDefinitions(value.view, content) || !ValidTerrainDefinitions(value.view, content) ||
        !ValidMonsterDefinitions(value.view, content) || !ValidObjectDefinitions(value.view, content)
        || value.details.objectType.value > content.ObjectCount()) return false;
    const auto& combat = value.details.combat;
    if ((combat.styleId.value && !content.TryGetCombatStyle(combat.styleId))
        || (combat.feedback.result.moveId.value && !content.TryGetCombatMove(combat.feedback.result.moveId))) return false;
    for (const auto& item : value.mapItems) if (item.type.value > content.ItemCount()) return false;
    for (const auto& item : value.details.items) if (item.type.value > content.ItemCount()) return false;
    return true;
}
}
