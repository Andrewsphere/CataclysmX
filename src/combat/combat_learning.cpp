#include "combat/combat_system.h"
#include "combat/combat_rules.h"
#include "core/world.h"
#include "skills/learning_system.h"
#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <cstdlib>

namespace cataclysmx {
namespace {
void Require(bool value) { if (!value) { std::abort(); } }

bool KnowsArt(const Actor& actor, CombatArtId art) {
    if (!actor.characterCombat) { return false; }
    for (const auto& progress : actor.characterCombat->arts) {
        if (progress.art == art) { return true; }
    }
    return false;
}
const ArtManualDefinition* FindManual(const ContentCatalog& content, const ArtManualRequest& request) {
    const auto* book = content.TryGetBook(request.book);
    if (!book || !content.TryGetCombatArt(request.art)) { return nullptr; }
    for (const auto& manual : book->artManuals) {
        if (manual.art == request.art) { return &manual; }
    }
    return nullptr;
}
bool MeetsPrerequisite(const Actor& actor, const ArtManualDefinition& manual) {
    return LearnedSkillLevel(actor.learning, manual.prerequisite.Skill()) >= manual.prerequisite.Level();
}
ManualAttemptProgress* FindAttempt(Actor& actor, const ArtManualRequest& request) {
    if (!actor.characterCombat) { return nullptr; }
    for (auto& attempt : actor.characterCombat->manuals) {
        if (attempt.book == request.book && attempt.art == request.art) { return &attempt; }
    }
    return nullptr;
}
bool Studying(const Actor& actor) {
    if (!actor.characterCombat) { return false; }
    for (const auto& attempt : actor.characterCombat->manuals) {
        if (attempt.active) { return true; }
    }
    return false;
}
// Each funding boundary counts as one action, just like ordinary book study.
void FundSlice(Actor& actor, ManualAttemptProgress& attempt, Tick now) {
    Require(attempt.remaining > 0 && attempt.fundedTicks == 0);
    attempt.fundedTicks = eastl::min<Tick>(100, attempt.remaining);
    actor.actionDeadline.Set(now, attempt.fundedTicks);
    Require(actor.turns != UINT64_MAX);
    ++actor.turns;
}
}

WorldResult<void> CombatSystem::GrantArt(ActorHandle handle, CombatArtId art, ArtGrantReason reason) {
    auto* actor = world_.MutableActor(handle);
    if (!actor) { return eastl::unexpected(WorldError::invalid_handle); }
    const auto& content = world_.GetContent();
    if (!content.TryGetCombatArt(art)) { return eastl::unexpected(WorldError::invalid_request); }
    switch (reason) {
    case ArtGrantReason::CharacterCreation:
    case ArtGrantReason::Teaching:
    case ArtGrantReason::Manual:
    case ArtGrantReason::Administration: break;
    default: return eastl::unexpected(WorldError::invalid_request);
    }
    if (!actor->characterCombat) { actor->characterCombat = eastl::make_unique<CharacterCombatState>(); }
    auto& state = *actor->characterCombat;
    // Acquisition is explicit. The current style neither grants another art nor
    // changes when the actor learns their first art; style changes remain paid.
    if (!KnowsArt(*actor, art)) { state.arts.push_back({art}); }
    eastl::sort(state.arts.begin(), state.arts.end(), [&content](const auto& a, const auto& b) {
        return content.GetCombatArt(a.art).id < content.GetCombatArt(b.art).id;
    });
    // Rebuild only art-derived grants. Innate grants and cooldown identities survive.
    eastl::erase_if(state.abilityGrants, [](const auto& grant) {
        return grant.source.kind == AbilityGrantSourceKind::Art;
    });
    for (const auto& progress : state.arts) {
        for (const auto& grant : content.GetCombatArt(progress.art).abilities) {
            if (progress.xp < grant.unlock.artXp) { continue; }
            bool unlocked = true;
            for (const auto& requirement : grant.unlock.skills) {
                if (LearnedSkillLevel(actor->learning, requirement.Skill()) < requirement.Level()) { unlocked = false; break; }
            }
            if (!unlocked) { continue; }
            const AbilityGrant value{grant.ability, {AbilityGrantSourceKind::Art, progress.art}};
            if (eastl::find(state.abilityGrants.begin(), state.abilityGrants.end(), value) == state.abilityGrants.end()) {
                state.abilityGrants.push_back(value);
            }
        }
    }
    world_.ActorChanged(handle);
    return {};
}

CombatAvailability CombatSystem::QueryArtStudy(ActorHandle handle, const ArtManualRequest& request) const {
    const auto* actor = world_.GetActor(handle);
    if (!actor) { return {CombatRefusal::InvalidActor}; }
    if (actor->combat.life != ActorLifeState::Alive) { return {CombatRefusal::Dead}; }
    if (actor->parked || !world_.IsActive(actor->position)) { return {CombatRefusal::Inactive}; }
    const auto* manual = FindManual(world_.GetContent(), request);
    if (!manual) { return {CombatRefusal::InvalidDefinition}; }
    if (KnowsArt(*actor, request.art)) { return {CombatRefusal::AlreadyKnown}; }
    if (actor->learning.study.active || Studying(*actor)
        || actor->actionDeadline.Remaining(world_.GetTick()) || actor->combat.action.kind != ActorActionKind::None) {
        return {CombatRefusal::Busy};
    }
    Vector<CraftingJobView> jobs;
    world_.CollectCraftingJobs(handle, jobs);
    for (const auto& job : jobs) { if (job.worker == actor->id) { return {CombatRefusal::Busy}; } }
    if (!MeetsPrerequisite(*actor, *manual)) { return {CombatRefusal::Locked}; }
    if (!world_.Learning().HasBookReference(handle, request.book)) { return {CombatRefusal::MissingReference}; }
    return {};
}

ArtStudyResult CombatSystem::StudyArt(ActorHandle handle, const ArtManualRequest& request) {
    const auto query = QueryArtStudy(handle, request);
    if (!query.Available()) { return {query.reason}; }
    auto* actor = world_.MutableActor(handle);
    if (!actor->characterCombat) { actor->characterCombat = eastl::make_unique<CharacterCombatState>(); }
    auto* attempt = FindAttempt(*actor, request);
    if (!attempt) {
        actor->characterCombat->manuals.push_back({request.book, request.art});
        attempt = &actor->characterCombat->manuals.back();
    }
    if (!attempt->totalDuration) {
        const auto& manual = *FindManual(world_.GetContent(), request);
        const auto surplus = LearnedSkillLevel(actor->learning, manual.prerequisite.Skill()) - manual.prerequisite.Level();
        attempt->totalDuration = ArtStudyDuration(manual.baseDuration, surplus);
        Require(attempt->totalDuration > 0 && manual.successChance <= ratioScale);
        attempt->remaining = attempt->totalDuration;
        attempt->successChance = manual.successChance;
    }
    attempt->active = true;
    attempt->pauseRequested = false;
    // A completed but ineligible attempt resumes directly to event processing,
    // without paying for any of its work twice.
    if (attempt->remaining) { FundSlice(*actor, *attempt, world_.GetTick()); }
    world_.ActorChanged(handle);
    return {CombatRefusal::None, attempt->totalDuration, attempt->successChance, true};
}

WorldResult<void> CombatSystem::PauseArtStudy(ActorHandle handle) {
    auto* actor = world_.MutableActor(handle);
    if (!actor) { return eastl::unexpected(WorldError::invalid_handle); }
    if (!actor->characterCombat) { return {}; }
    for (auto& attempt : actor->characterCombat->manuals) {
        if (attempt.active) { attempt.pauseRequested = true; }
    }
    world_.ActorChanged(handle);
    return {};
}

void CombatSystem::ElapseArtStudy(Tick elapsed) {
    world_.CollectActiveActors(elapsedActors_);
    for (auto handle : elapsedActors_) {
        auto* actor = world_.MutableActor(handle);
        if (!actor || actor->parked || actor->combat.life != ActorLifeState::Alive
            || !world_.IsActive(actor->position) || !actor->characterCombat) { continue; }
        for (auto& attempt : actor->characterCombat->manuals) {
            if (!attempt.active) { continue; }
            const auto paid = eastl::min(elapsed, attempt.fundedTicks);
            Require(paid <= attempt.remaining);
            attempt.remaining -= paid;
            attempt.fundedTicks -= paid;
            // Elapse is called before World advances its tick, as with regular study.
            actor->actionDeadline.Set(world_.GetTick(), CheckedTickAdd(elapsed, attempt.fundedTicks));
            world_.ActorChanged(handle);
        }
    }
}

void CombatSystem::ProcessArtStudyEvents(std::uint32_t& rng) {
    world_.CollectActiveActors(dueActors_);
    eastl::sort(dueActors_.begin(), dueActors_.end(), [this](auto a, auto b) {
        return world_.GetActor(a)->id < world_.GetActor(b)->id;
    });
    for (auto handle : dueActors_) {
        auto* actor = world_.MutableActor(handle);
        if (!actor || actor->parked || actor->combat.life != ActorLifeState::Alive
            || !world_.IsActive(actor->position) || !actor->characterCombat) { continue; }
        for (auto& attempt : actor->characterCombat->manuals) {
            if (!attempt.active || attempt.fundedTicks) { continue; }
            const auto* manual = FindManual(world_.GetContent(), {attempt.book, attempt.art});
            const bool valid = manual && MeetsPrerequisite(*actor, *manual)
                && world_.Learning().HasBookReference(handle, attempt.book) && !KnowsArt(*actor, attempt.art);
            if (!attempt.pauseRequested && valid && !attempt.remaining) {
                const bool success = DrawBelow(rng, ratioScale) < attempt.successChance;
                CombatEvent event;
                event.tick = world_.GetTick(); event.kind = CombatEventKind::ArtStudyCompleted;
                event.actor = actor->id; event.art = attempt.art; event.cost = attempt.totalDuration;
                event.learnedArt = success;
                if (success) { Require(bool(GrantArt(handle, attempt.art, ArtGrantReason::Manual))); }
                EmitEvent(event);
                // Failure clears the snapshot too: the next attempt pays full work.
                const auto book = attempt.book;
                const auto art = attempt.art;
                attempt = {}; attempt.book = book; attempt.art = art;
            } else if (!attempt.pauseRequested && valid && attempt.remaining) {
                FundSlice(*actor, attempt, world_.GetTick());
            } else {
                attempt.active = false; attempt.pauseRequested = false;
            }
            if (!attempt.active) { actor->actionDeadline.Set(world_.GetTick(), 0); }
            world_.ActorChanged(handle);
        }
    }
}

eastl::optional<Tick> CombatSystem::NextArtStudyEventIn() const {
    world_.CollectActiveActors(queriedActors_);
    eastl::optional<Tick> next;
    for (auto handle : queriedActors_) {
        const auto* actor = world_.GetActor(handle);
        if (!actor || actor->parked || actor->combat.life != ActorLifeState::Alive
            || !world_.IsActive(actor->position) || !actor->characterCombat) { continue; }
        for (const auto& attempt : actor->characterCombat->manuals) {
            if (attempt.active && (!next || attempt.fundedTicks < *next)) { next = attempt.fundedTicks; }
        }
    }
    return next;
}
}
