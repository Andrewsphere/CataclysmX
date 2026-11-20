#include "skills/learning_system.h"
#include "core/world.h"
#include "digital/information_system.h"
#include "objects/storage_access.h"
#include <EASTL/algorithm.h>
#include <EASTL/array.h>
#include <EASTL/sort.h>
#include <cstdlib>

namespace cataclysmx {
namespace {
void Require(bool value) { if (!value) { std::abort(); } }
// Match crafting's bounded same-layer flood, including diagonal corner rules.
bool Nearby(const World& world, TileCoord from, TileCoord to, unsigned radius) {
    if (from.y != to.y || std::abs(from.x - to.x) > static_cast<int>(radius)
        || std::abs(from.z - to.z) > static_cast<int>(radius)) { return false; }
    eastl::array<TileCoord, 289> queue{};
    eastl::array<bool, 289> seen{};
    const int r = static_cast<int>(radius), width = r * 2 + 1;
    const auto passable = [&world](TileCoord p) {
        return world.GetTile(p) && world.IsActive(p) && !world.BlocksMovement(p);
    };
    std::size_t count = 1;
    queue[0] = from; seen[r * width + r] = true;
    for (std::size_t i = 0; i < count; ++i) {
        const auto current = queue[i];
        if (current == to) { return true; }
        for (int dz = -1; dz <= 1; ++dz) { for (int dx = -1; dx <= 1; ++dx) {
            if (!dx && !dz) { continue; }
            const TileCoord next{current.x + dx, current.z + dz, current.y};
            const int x = next.x - from.x + r, z = next.z - from.z + r;
            if (x < 0 || z < 0 || x >= width || z >= width || seen[z * width + x]) { continue; }
            if (!passable(next) || (dx && dz && (!passable({current.x + dx, current.z, current.y})
                || !passable({current.x, current.z + dz, current.y})))) { continue; }
            seen[z * width + x] = true;
            Require(count < queue.size()); queue[count++] = next;
        } }
    }
    return false;
}
bool CarriesBook(const World& world, ItemHandle handle, BookId book, eastl::span<const ItemHandle> accessible) {
    return world.Information().HasBook(handle, book, accessible);
}
bool ReferencesRecipe(const World& world, eastl::span<const ItemHandle> references, CraftingRecipeId recipe) {
    const auto& content = world.GetContent();
    for (auto item : references) {
        for (std::size_t i = 1; i <= content.BookCount(); ++i) {
            const BookId book{static_cast<std::uint32_t>(i)};
            const auto& recipes = content.GetBook(book).recipes;
            if (eastl::find(recipes.begin(), recipes.end(), recipe) != recipes.end()
                && CarriesBook(world, item, book, references)) { return true; }
        }
    }
    return false;
}
}

LearningSystem::LearningSystem(World& world) : world_(world) {}

void LearningSystem::GatherReferences(ActorHandle handle, unsigned radius) const {
    Require(radius <= 8);
    references_.clear();
    const auto* actor = world_.GetActor(handle);
    if (!actor || actor->parked || !world_.IsActive(actor->position)) { return; }
    references_.insert(references_.end(), actor->inventory.begin(), actor->inventory.end());
    const auto position = actor->position;
    const int r = static_cast<int>(radius);
    for (int z = -r; z <= r; ++z) { for (int x = -r; x <= r; ++x) {
        const TileCoord tile{position.x + x, position.z + z, position.y};
        const auto object = world_.ObjectAt(tile);
        if (CanReachStorage(world_, position, object, radius)) {
            const auto& contents = world_.GetObject(object)->contents;
            references_.insert(references_.end(), contents.begin(), contents.end());
        }
        if (Nearby(world_, position, tile, radius)) {
            for (auto item : world_.ItemsAt(tile)) { references_.push_back(item); }
        }
    } }
    for (std::size_t i = 0; i < references_.size(); ++i) {
        const auto reference = references_[i];
        const auto* item = world_.GetItem(reference);
        if (!item || item->location.installation.value || world_.IsReservedItem(reference)
            || world_.GetContent().GetItem(item->type).unfinishedCraft) {
            references_[i] = {}; continue;
        }
        references_.insert(references_.end(), item->contents.begin(), item->contents.end());
    }
    eastl::erase(references_, ItemHandle{});
}

bool LearningSystem::HasBookReference(ActorHandle actor, BookId book) const {
    if (!world_.GetContent().TryGetBook(book)) { return false; }
    GatherReferences(actor, world_.GetCraftingSettings().nearbyRadius);
    for (auto item : references_) { if (CarriesBook(world_, item, book, references_)) { return true; } }
    return false;
}
bool LearningSystem::HasRecipeReference(ActorHandle actor, CraftingRecipeId recipe) const {
    return HasRecipeReference(actor, recipe, world_.GetCraftingSettings().nearbyRadius);
}
bool LearningSystem::HasRecipeReference(ActorHandle actor, CraftingRecipeId recipe, unsigned radius) const {
    const auto& content = world_.GetContent();
    if (!content.TryGetCraftingRecipe(recipe)) { return false; }
    GatherReferences(actor, radius);
    return ReferencesRecipe(world_, references_, recipe);
}
bool LearningSystem::Busy(ActorHandle handle) const {
    const auto* actor = world_.GetActor(handle);
    if (!actor || actor->combat.life != ActorLifeState::Alive) { return false; }
    if (actor->learning.study.active) { return true; }
    if (actor->characterCombat) {
        for (const auto& attempt : actor->characterCombat->manuals) {
            // Active ownership includes a fully paid slice awaiting event processing.
            if (attempt.active) { return true; }
        }
    }
    return false;
}
eastl::optional<Tick> LearningSystem::ActorEventIn(ActorHandle handle) const {
    const auto* actor = world_.GetActor(handle);
    if (!actor || actor->combat.life != ActorLifeState::Alive || actor->parked
        || !world_.IsActive(actor->position) || !actor->learning.study.active) { return eastl::nullopt; }
    return actor->learning.study.remaining;
}
bool LearningSystem::FundStudy(ActorHandle handle) {
    auto* actor = world_.MutableActor(handle);
    auto& state = actor->learning.study;
    if (actor->combat.life != ActorLifeState::Alive
        || !CanStudyBook(actor->learning, world_.GetContent(), state.book)
        || !HasBookReference(handle, state.book)) { return false; }
    const auto& book = world_.GetContent().GetBook(state.book);
    const auto left = std::uint64_t{book.studyWork} * ratioScale - EnsureBookProgress(actor->learning, state.book).work;
    Require(left > 0);
    constexpr std::uint64_t numerator = std::uint64_t{ticks_per_turn} * ratioScale;
    const auto residue = std::uint64_t{state.timingRemainder} * actor->speed / state.timingSpeed;
    Tick duration = 100;
    auto work = (duration * numerator + residue) / actor->speed;
    if (work >= left) {
        duration = eastl::max<Tick>(1, (left * actor->speed - residue + numerator - 1) / numerator);
        work = left;
    }
    state.pendingWork = work; state.remaining = duration;
    state.timingRemainder = static_cast<std::uint32_t>((duration * numerator + residue) % actor->speed);
    state.timingSpeed = actor->speed;
    actor->actionDeadline.Set(world_.GetTick(), duration);
    Require(actor->turns != UINT64_MAX); ++actor->turns;
    world_.ActorChanged(handle);
    return true;
}
WorldResult<StudyResult> LearningSystem::Study(ActorHandle handle, BookId book) {
    const auto& content = world_.GetContent();
    if (!content.TryGetBook(book)) { return eastl::unexpected(WorldError::invalid_request); }
    auto* actor = world_.MutableActor(handle);
    if (!actor || actor->combat.life != ActorLifeState::Alive || actor->parked
        || !world_.IsActive(actor->position)) { return StudyResult{StudyOutcome::ActorUnavailable}; }
    if (Busy(handle) || CraftingBusy(handle) || world_.DigitalBusy(handle) || actor->actionDeadline.Remaining(world_.GetTick())
        || actor->combat.action.kind != ActorActionKind::None) { return StudyResult{StudyOutcome::ActorBusy}; }
    const auto* progress = FindBookProgress(actor->learning, book);
    if (progress && progress->used) { return StudyResult{StudyOutcome::AlreadyUsed}; }
    if (!CanStudyBook(actor->learning, content, book)) { return StudyResult{StudyOutcome::OutsideTeachingRange}; }
    if (!HasBookReference(handle, book)) { return StudyResult{StudyOutcome::MissingReference}; }
    // An administrative level change can make the final paid completion
    // temporarily ineligible. Its durable work still must never be paid twice.
    if (progress && progress->work == std::uint64_t{content.GetBook(book).studyWork} * ratioScale) {
        Require(CompleteBookStudy(actor->learning, content, book));
        world_.ActorChanged(handle);
        return StudyResult{StudyOutcome::Started};
    }
    auto& state = actor->learning.study;
    if (state.book != book) { state = {}; state.book = book; }
    state.active = true; state.pauseRequested = false;
    Require(FundStudy(handle));
    return StudyResult{StudyOutcome::Started};
}
WorldResult<StudyResult> LearningSystem::PauseStudy(ActorHandle handle) {
    auto* actor = world_.MutableActor(handle);
    if (!actor) { return StudyResult{StudyOutcome::ActorUnavailable}; }
    auto& state = actor->learning.study;
    if (!state.active) { return StudyResult{StudyOutcome::Paused}; }
    state.pauseRequested = true;
    world_.ActorChanged(handle);
    return StudyResult{StudyOutcome::PauseRequested};
}
void LearningSystem::Elapse(Tick elapsed) {
    world_.CollectActiveActors(actors_);
    for (auto handle : actors_) {
        auto* actor = world_.MutableActor(handle);
        auto& state = actor->learning.study;
        if (actor->combat.life != ActorLifeState::Alive) {
            if (state.active) { state = {}; actor->actionDeadline.Set(world_.GetTick(), 0); world_.ActorChanged(handle); }
            continue;
        }
        if (!state.active) { continue; }
        state.remaining -= eastl::min(elapsed, state.remaining);
        actor->actionDeadline.Set(world_.GetTick(), CheckedTickAdd(elapsed, state.remaining));
        world_.ActorChanged(handle);
    }
}
bool LearningSystem::ProcessEvents() {
    world_.CollectActiveActors(actors_);
    due_.clear();
    for (auto handle : actors_) {
        const auto* actor = world_.GetActor(handle);
        if (actor->combat.life != ActorLifeState::Alive) { continue; }
        const auto& state = actor->learning.study;
        if (state.active && !state.remaining) { due_.push_back(handle); }
    }
    eastl::sort(due_.begin(), due_.end(), [this](auto a, auto b) { return world_.GetActor(a)->id < world_.GetActor(b)->id; });
    for (auto handle : due_) {
        auto* actor = world_.MutableActor(handle);
        if (!actor || actor->combat.life != ActorLifeState::Alive || actor->parked
            || !world_.IsActive(actor->position)) { continue; }
        auto& state = actor->learning.study;
        auto& progress = EnsureBookProgress(actor->learning, state.book);
        progress.work += state.pendingWork; state.pendingWork = 0;
        if (progress.work == std::uint64_t{world_.GetContent().GetBook(state.book).studyWork} * ratioScale) {
            CompleteBookStudy(actor->learning, world_.GetContent(), state.book);
            state.active = false;
        } else if (state.pauseRequested || !FundStudy(handle)) { state.active = false; }
        if (!state.active) {
            state.pauseRequested = false;
            actor->actionDeadline.Set(world_.GetTick(), 0);
        }
        world_.ActorChanged(handle);
    }
    return !due_.empty();
}
WorldResult<SkillAward> LearningSystem::Train(ActorHandle handle, const SkillTraining& training,
    SkillActivity activity, std::uint64_t fixedWork) {
    auto* actor = world_.MutableActor(handle);
    if (!actor) { return eastl::unexpected(WorldError::invalid_handle); }
    const auto* definition = world_.GetContent().TryGetSkill(training.Skill());
    if (!definition || training.Level() > definition->maxLevel
        || training.XpPerWork() > ratioScale || (activity != SkillActivity::Craft
            && activity != SkillActivity::Disassembly && activity != SkillActivity::Combat)) {
        return eastl::unexpected(WorldError::invalid_request);
    }
    const auto award = TrainSkill(actor->learning, world_.GetContent(), training, activity, fixedWork);
    UpdateAutolearn(actor->learning, world_.GetContent());
    world_.ActorChanged(handle);
    return award;
}
WorldResult<void> LearningSystem::SetLevel(ActorHandle handle, SkillId skill, std::uint8_t level) {
    auto* actor = world_.MutableActor(handle);
    if (!actor) { return eastl::unexpected(WorldError::invalid_handle); }
    const auto* definition = world_.GetContent().TryGetSkill(skill);
    if (!definition || level > definition->maxLevel) { return eastl::unexpected(WorldError::invalid_request); }
    UpdateAutolearn(actor->learning, world_.GetContent());
    auto& progress = EnsureSkillProgress(actor->learning, skill);
    progress = SkillProgress{skill}; progress.level = level;
    UpdateAutolearn(actor->learning, world_.GetContent());
    world_.ActorChanged(handle);
    return {};
}
void LearningSystem::CollectView(ActorHandle handle, LearningView& output) const {
    output.skills.clear(); output.recipes.clear(); output.books.clear();
    output.activeBook = {}; output.studyRemaining = 0; output.studyPauseRequested = false;
    const auto* actor = world_.GetActor(handle);
    if (!actor) { return; }
    const auto& state = actor->learning;
    const auto& content = world_.GetContent();
    for (std::size_t i = 1; i <= content.SkillCount(); ++i) {
        const SkillId id{static_cast<std::uint32_t>(i)};
        const auto* progress = FindSkillProgress(state, id);
        const auto value = progress ? *progress : SkillProgress{id};
        output.skills.push_back({value, EffectiveSkillLevel(state, id), SkillXpRequired(content.GetSkill(id), value.level)});
    }
    GatherReferences(handle, world_.GetCraftingSettings().nearbyRadius);
    for (std::size_t i = 1; i <= content.CraftingRecipeCount(); ++i) {
        const CraftingRecipeId id{static_cast<std::uint32_t>(i)};
        const auto& recipe = content.GetCraftingRecipe(id);
        output.recipes.push_back({id, KnowsRecipe(state, content, id), ReferencesRecipe(world_, references_, id),
            MeetsSkillRequirements(state, {recipe.skills.begin(), recipe.skills.Size()}), RecipeLearningRatio(state, content, id)});
    }
    for (std::size_t i = 1; i <= content.BookCount(); ++i) {
        const BookId id{static_cast<std::uint32_t>(i)};
        const auto* progress = FindBookProgress(state, id);
        bool accessible = false;
        for (auto item : references_) { accessible |= CarriesBook(world_, item, id, references_); }
        if (!accessible && !progress) { continue; }
        BookStudyView view{id, accessible, progress && progress->used, CanStudyBook(state, content, id), progress ? progress->work : 0};
        if (view.eligible) { for (const auto& benefit : content.GetBook(id).benefits) {
            const auto* stored = FindSkillProgress(state, benefit.skill);
            const auto p = stored ? *stored : SkillProgress{benefit.skill};
            const unsigned next = p.level + 1;
            const auto& skill = content.GetSkill(benefit.skill);
            if (next > skill.maxLevel || next < benefit.firstLevel || next > benefit.lastLevel) { continue; }
            const auto required = SkillXpRequired(skill, p.level);
            if (skill.category == SkillCategory::Noncombat && p.level >= 5) {
                const auto amount = std::uint64_t{benefit.theoryXp} * ratioScale;
                view.cappedTheoryXp += amount - eastl::min(amount, required - p.xp - p.theory);
            } else if (skill.category == SkillCategory::Noncombat) {
                const auto amount = std::uint64_t{benefit.directXp} * ratioScale;
                view.cappedDirectXp += amount - eastl::min(amount, eastl::min(required * 3 / 10 - p.directBookXp, required - p.xp));
            }
        } }
        output.books.push_back(view);
    }
    if (state.study.active) {
        output.activeBook = state.study.book; output.studyRemaining = state.study.remaining;
        output.studyPauseRequested = state.study.pauseRequested;
    }
}
}
