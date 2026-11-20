#include "skills/skill_state.h"

#include "content/content.h"
#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <cstdlib>
#include <initializer_list>
#include <limits>

namespace cataclysmx {
namespace {

bool Advanced(const SkillDefinition& definition, const SkillProgress& progress) {
    return definition.category == SkillCategory::Noncombat && progress.level >= 5;
}

void LevelUp(SkillProgress& progress, const SkillDefinition& definition) {
    ++progress.level;
    progress.xp = 0;
    progress.theory = 0;
    progress.practice = 0;
    progress.directBookXp = 0;
    progress.multiplier = ratioScale;
    progress.boostRemainder = 0;
    if (progress.level == definition.maxLevel) { progress.workRemainder = 0; }
}

SkillAward AddReserve(SkillProgress& progress, const SkillDefinition& definition,
    bool theory, std::uint64_t amount) {
    SkillAward award;
    const auto remaining = SkillXpRequired(definition, progress.level) - progress.xp;
    auto& reserve = theory ? progress.theory : progress.practice;
    const auto accepted = eastl::min(amount, remaining - reserve);
    reserve += accepted;
    award.capped = amount - accepted;
    const auto pairs = eastl::min(progress.theory, progress.practice);
    progress.theory -= pairs;
    progress.practice -= pairs;
    progress.xp += pairs;
    award.advancement = pairs;
    if (progress.xp == SkillXpRequired(definition, progress.level)) {
        LevelUp(progress, definition);
        award.levels = 1;
    }
    return award;
}

bool Eligible(const LearningState& state, const ContentCatalog& content, const SkillBookBenefit& benefit) {
    const unsigned next = LearnedSkillLevel(state, benefit.skill) + 1;
    return next <= content.GetSkill(benefit.skill).maxLevel && next >= benefit.firstLevel && next <= benefit.lastLevel;
}

bool Autolearns(const LearningState& state, const CraftingRecipeDefinition& recipe) {
    return recipe.autolearn && MeetsSkillRequirements(state, recipe.skills)
        && MeetsSkillRequirements(state, recipe.autolearnSkills);
}

bool Fields(const DataObject& data, std::initializer_list<const char*> names) {
    if (data.Type() != DataType::Object || data.Size() != names.size()) { return false; }
    for (const auto* name : names) { if (!data.Find(name)) { return false; } }
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

bool ValidSkill(const SkillProgress& p, const SkillDefinition& definition) {
    if (p.level > definition.maxLevel || p.workRemainder >= ratioScale || p.boostRemainder >= ratioScale) {
        return false;
    }
    if (p.level == definition.maxLevel) {
        return !p.xp && !p.theory && !p.practice && !p.directBookXp && !p.workRemainder
            && !p.boostRemainder && p.multiplier == ratioScale;
    }
    const auto required = SkillXpRequired(definition, p.level);
    if (p.xp >= required) { return false; }
    if (Advanced(definition, p)) {
        return p.theory <= required - p.xp && p.practice <= required - p.xp
            && !(p.theory && p.practice) && !p.directBookXp
            && p.multiplier == ratioScale && !p.boostRemainder;
    }
    const bool combat = definition.category == SkillCategory::Combat;
    return !p.theory && !p.practice && p.directBookXp <= p.xp
        && p.directBookXp <= (combat ? 0 : required * 3 / 10)
        && p.multiplier >= ratioScale && p.multiplier <= (combat ? 12000u : 40000u)
        && (p.multiplier != ratioScale || !p.boostRemainder);
}

// This is a save-format contract, not the current content default curve.
SkillDefinition LegacyCookingChemistry() {
    SkillDefinition definition;
    definition.category = SkillCategory::Noncombat;
    definition.maxLevel = 10;
    definition.xpToNext = {100, 200, 400, 800, 1200, 1800, 2600, 3600, 4800, 6400};
    return definition;
}

bool MigrationDestination(const SkillDefinition* definition, std::uint8_t maximum,
    const SkillDefinition& legacy) {
    if (!definition || definition->category != SkillCategory::Noncombat || definition->maxLevel != maximum) {
        return false;
    }
    for (unsigned i = 0; i < maximum; ++i) {
        if (definition->xpToNext[i] != legacy.xpToNext[i]) { return false; }
    }
    return true;
}

bool ValidState(const LearningState& state, const ContentCatalog& content) {
    std::uint32_t previous = 0;
    for (const auto& p : state.skills) {
        const auto* definition = content.TryGetSkill(p.skill);
        if (p.skill.value <= previous || !definition || !ValidSkill(p, *definition)) { return false; }
        previous = p.skill.value;
    }
    previous = 0;
    for (const auto& p : state.recipes) {
        const auto* definition = content.TryGetCraftingRecipe(p.recipe);
        if (p.recipe.value <= previous || !definition || p.points > RecipeLearningGoal(*definition)) { return false; }
        previous = p.recipe.value;
    }
    previous = 0;
    for (const auto& p : state.books) {
        const auto* definition = content.TryGetBook(p.book);
        if (p.book.value <= previous || !definition) { return false; }
        const auto goal = std::uint64_t{definition->studyWork} * ratioScale;
        if (p.work > goal || (p.used && p.work != goal)) { return false; }
        previous = p.book.value;
    }
    const auto& study = state.study;
    if (!study.timingSpeed || study.timingRemainder >= study.timingSpeed) { return false; }
    const auto* book = content.TryGetBook(study.book);
    if (study.book.value && !book) { return false; }
    if (!study.active) { return !study.remaining && !study.pendingWork && !study.pauseRequested; }
    if (!book) { return false; }
    const auto* progress = FindBookProgress(state, study.book);
    if (progress && progress->used) { return false; }
    const auto work = progress ? progress->work : 0;
    const auto goal = std::uint64_t{book->studyWork} * ratioScale;
    // Eligibility is checked when funding, not while an already-paid slice runs.
    // At very large speeds a slice can advance only the fractional timing carry.
    if (work >= goal || study.pendingWork > goal - work || study.remaining > 100) { return false; }
    constexpr std::uint64_t workPerTickNumerator = std::uint64_t{ratioScale} * ticks_per_turn;
    // The incoming (possibly rescaled) carry is strictly less than timingSpeed.
    // Remaining time may already be partially elapsed, so bound the original slice.
    const auto maximumWork = (100 * workPerTickNumerator + study.timingSpeed - 1) / study.timingSpeed;
    return study.pendingWork <= maximumWork;
}

}

const SkillProgress* FindSkillProgress(const LearningState& state, SkillId skill) {
    for (const auto& progress : state.skills) { if (progress.skill == skill) { return &progress; } }
    return nullptr;
}

SkillProgress& EnsureSkillProgress(LearningState& state, SkillId skill) {
    if (!skill.value) { std::abort(); }
    auto position = state.skills.begin();
    while (position != state.skills.end() && position->skill.value < skill.value) { ++position; }
    if (position != state.skills.end() && position->skill == skill) { return *position; }
    return *state.skills.insert(position, SkillProgress{skill});
}

const BookProgress* FindBookProgress(const LearningState& state, BookId book) {
    for (const auto& progress : state.books) { if (progress.book == book) { return &progress; } }
    return nullptr;
}

BookProgress& EnsureBookProgress(LearningState& state, BookId book) {
    if (!book.value) { std::abort(); }
    auto position = state.books.begin();
    while (position != state.books.end() && position->book.value < book.value) { ++position; }
    if (position != state.books.end() && position->book == book) { return *position; }
    return *state.books.insert(position, BookProgress{book});
}

std::uint8_t LearnedSkillLevel(const LearningState& state, SkillId skill) {
    const auto* progress = FindSkillProgress(state, skill);
    return progress ? progress->level : 0;
}

int EffectiveSkillLevel(const LearningState& state, SkillId skill, int modifier) {
    const auto value = std::int64_t{LearnedSkillLevel(state, skill)} + modifier;
    return static_cast<int>(eastl::clamp(value, std::int64_t{0}, std::int64_t{std::numeric_limits<int>::max()}));
}

bool MeetsSkillRequirements(const LearningState& state, eastl::span<const SkillRequirement> requirements) {
    for (const auto& requirement : requirements) {
        if (LearnedSkillLevel(state, requirement.skill) < requirement.level) { return false; }
    }
    return true;
}

std::uint64_t SkillXpRequired(const SkillDefinition& definition, std::uint8_t currentLevel) {
    if (currentLevel > definition.maxLevel) { std::abort(); }
    return currentLevel < definition.maxLevel ? std::uint64_t{definition.xpToNext[currentLevel]} * ratioScale : 0;
}

SkillAward TrainSkill(LearningState& state, const ContentCatalog& content,
    const SkillTraining& training, SkillActivity activity, std::uint64_t fixedWork) {
    const auto& definition = content.GetSkill(training.skill);
    if (training.level > definition.maxLevel || training.xpPerWork > ratioScale) { std::abort(); }
    SkillAward award;
    if (!fixedWork || !training.xpPerWork) { return award; }
    auto& progress = EnsureSkillProgress(state, training.skill);
    // Quotient/remainder decomposition avoids overflowing even UINT64_MAX work.
    const auto fraction = (fixedWork % ratioScale) * training.xpPerWork + progress.workRemainder;
    const auto amount = (fixedWork / ratioScale) * training.xpPerWork + fraction / ratioScale;
    const auto ceiling = eastl::min<unsigned>(training.level + 1, definition.maxLevel);
    if (progress.level >= ceiling || (Advanced(definition, progress) && activity == SkillActivity::Combat)) {
        award.capped = amount;
        return award;
    }
    progress.workRemainder = static_cast<std::uint32_t>(fraction % ratioScale);
    return AwardSkillXp(state, content, training.skill, training.level, activity, amount);
}

SkillAward AwardSkillXp(LearningState& state, const ContentCatalog& content,
    SkillId skill, std::uint8_t trainingLevel, SkillActivity activity, std::uint64_t amount) {
    const auto& definition = content.GetSkill(skill);
    if (trainingLevel > definition.maxLevel) { std::abort(); }
    SkillAward award;
    if (!amount && !FindSkillProgress(state, skill)) { return award; }
    auto& progress = EnsureSkillProgress(state, skill);
    const auto ceiling = eastl::min<unsigned>(trainingLevel + 1, definition.maxLevel);
    if (progress.level >= ceiling || (Advanced(definition, progress) && activity == SkillActivity::Combat)) {
        award.capped = amount;
        return award;
    }
    if (Advanced(definition, progress)) {
        const auto reserve = activity == SkillActivity::Disassembly ? progress.theory : progress.practice;
        const auto capacity = SkillXpRequired(definition, progress.level) - progress.xp - reserve;
        // A full reserve cannot bank fractional surplus, even without a whole capped unit.
        if (amount >= capacity) { progress.workRemainder = 0; }
        award = AddReserve(progress, definition, activity == SkillActivity::Disassembly, amount);
    } else {
        while (amount && progress.level < ceiling && !Advanced(definition, progress)) {
            const auto remaining = SkillXpRequired(definition, progress.level) - progress.xp;
            const auto numerator = remaining * ratioScale - progress.boostRemainder;
            const auto needed = numerator / progress.multiplier + (numerator % progress.multiplier != 0);
            const auto spent = eastl::min(amount, needed);
            const auto boosted = spent * progress.multiplier + progress.boostRemainder;
            const auto gained = eastl::min(remaining, boosted / ratioScale);
            progress.boostRemainder = static_cast<std::uint32_t>(boosted % ratioScale);
            amount -= spent;
            progress.xp += gained;
            award.advancement += gained;
            award.capped += boosted / ratioScale - gained;
            if (progress.xp == SkillXpRequired(definition, progress.level)) {
                LevelUp(progress, definition);
                ++award.levels;
            }
        }
        // Ordinary surplus never becomes advanced theory/practice.
        award.capped += amount;
        // Check the resulting boundary even when the final whole unit was consumed.
        // Continuing ordinary awards retain their carry across eligible level-ups.
        if (progress.level >= ceiling || Advanced(definition, progress)) { progress.workRemainder = 0; }
    }
    if (award.levels) { UpdateAutolearn(state, content); }
    return award;
}

bool CanStudyBook(const LearningState& state, const ContentCatalog& content, BookId book) {
    const auto* definition = content.TryGetBook(book);
    const auto* progress = FindBookProgress(state, book);
    if (!definition || (progress && progress->used)) { return false; }
    for (const auto& benefit : definition->benefits) { if (Eligible(state, content, benefit)) { return true; } }
    return false;
}

bool CompleteBookStudy(LearningState& state, const ContentCatalog& content, BookId book) {
    if (!CanStudyBook(state, content, book)) { return false; }
    const auto& definition = content.GetBook(book);
    for (const auto& benefit : definition.benefits) {
        if (!Eligible(state, content, benefit)) { continue; }
        auto& progress = EnsureSkillProgress(state, benefit.skill);
        const auto& skill = content.GetSkill(benefit.skill);
        if (Advanced(skill, progress)) {
            AddReserve(progress, skill, true, std::uint64_t{benefit.theoryXp} * ratioScale);
            continue;
        }
        const auto earnedLevel = progress.level;
        const bool combat = skill.category == SkillCategory::Combat;
        if (!combat) {
            const auto required = SkillXpRequired(skill, progress.level);
            const auto allowance = required * 3 / 10 - progress.directBookXp;
            const auto grant = eastl::min(eastl::min(allowance, required - progress.xp),
                std::uint64_t{benefit.directXp} * ratioScale);
            progress.xp += grant;
            progress.directBookXp += grant;
            if (progress.xp == required) { LevelUp(progress, skill); }
        }
        if (progress.level == earnedLevel) {
            progress.multiplier = eastl::max(progress.multiplier,
                eastl::clamp(benefit.multiplier, std::uint32_t{ratioScale}, combat ? 12000u : 40000u));
        }
    }
    auto& progress = EnsureBookProgress(state, book);
    progress.work = std::uint64_t{definition.studyWork} * ratioScale;
    progress.used = true;
    if (state.study.book == book) {
        state.study.active = false;
        state.study.pauseRequested = false;
        state.study.remaining = 0;
        state.study.pendingWork = 0;
    }
    UpdateAutolearn(state, content);
    return true;
}

std::uint64_t RecipeLearningGoal(const CraftingRecipeDefinition& recipe) {
    if (!recipe.craftsToLearn || !recipe.disassembliesToLearn) { std::abort(); }
    return std::uint64_t{recipe.craftsToLearn} * recipe.disassembliesToLearn * ratioScale;
}

bool KnowsRecipe(const LearningState& state, const ContentCatalog& content, CraftingRecipeId recipe) {
    const auto& definition = content.GetCraftingRecipe(recipe);
    if (definition.knownByDefault || Autolearns(state, definition)) { return true; }
    for (const auto& progress : state.recipes) {
        if (progress.recipe == recipe) { return progress.points >= RecipeLearningGoal(definition); }
    }
    return false;
}

std::uint32_t RecipeLearningRatio(const LearningState& state, const ContentCatalog& content, CraftingRecipeId recipe) {
    if (KnowsRecipe(state, content, recipe)) { return ratioScale; }
    for (const auto& progress : state.recipes) {
        if (progress.recipe == recipe) {
            return static_cast<std::uint32_t>(progress.points * ratioScale / RecipeLearningGoal(content.GetCraftingRecipe(recipe)));
        }
    }
    return 0;
}

void LearnRecipe(LearningState& state, const ContentCatalog& content, CraftingRecipeId recipe, std::uint64_t points) {
    const auto goal = RecipeLearningGoal(content.GetCraftingRecipe(recipe));
    if (!points) { return; }
    auto position = state.recipes.begin();
    while (position != state.recipes.end() && position->recipe.value < recipe.value) { ++position; }
    if (position == state.recipes.end() || position->recipe != recipe) {
        position = state.recipes.insert(position, RecipeProgress{recipe});
    }
    position->points += eastl::min(points, goal - position->points);
}

void UpdateAutolearn(LearningState& state, const ContentCatalog& content) {
    for (std::size_t i = 1; i <= content.CraftingRecipeCount(); ++i) {
        const CraftingRecipeId id{static_cast<std::uint32_t>(i)};
        const auto& recipe = content.GetCraftingRecipe(id);
        if (!recipe.knownByDefault && Autolearns(state, recipe)) {
            LearnRecipe(state, content, id, RecipeLearningGoal(recipe));
        }
    }
}

DataObject CaptureLearningState(const LearningState& state, const ContentCatalog& content) {
    if (!ValidState(state, content)) { std::abort(); }
    DataObject data;
    data.SetUnsigned("version", 2);
    auto skills = DataObject::List();
    for (const auto& p : state.skills) {
        DataObject value;
        value.SetString("skill", content.GetSkill(p.skill).id);
        value.SetUnsigned("level", p.level);
        value.SetUnsigned("xp", p.xp);
        value.SetUnsigned("theory", p.theory);
        value.SetUnsigned("practice", p.practice);
        value.SetUnsigned("directBookXp", p.directBookXp);
        value.SetUnsigned("multiplier", p.multiplier);
        value.SetUnsigned("workRemainder", p.workRemainder);
        value.SetUnsigned("boostRemainder", p.boostRemainder);
        skills.Append(eastl::move(value));
    }
    data.Set("skills", eastl::move(skills));
    auto recipes = DataObject::List();
    for (const auto& p : state.recipes) {
        DataObject value;
        value.SetString("recipe", content.GetCraftingRecipe(p.recipe).id);
        value.SetUnsigned("points", p.points);
        recipes.Append(eastl::move(value));
    }
    data.Set("recipes", eastl::move(recipes));
    auto books = DataObject::List();
    for (const auto& p : state.books) {
        DataObject value;
        value.SetString("book", content.GetBook(p.book).id);
        value.SetUnsigned("work", p.work);
        value.SetBoolean("used", p.used);
        books.Append(eastl::move(value));
    }
    data.Set("books", eastl::move(books));
    DataObject study;
    study.SetString("book", state.study.book.value ? eastl::string_view(content.GetBook(state.study.book).id) : eastl::string_view{});
    study.SetUnsigned("remaining", state.study.remaining);
    study.SetUnsigned("pendingWork", state.study.pendingWork);
    study.SetUnsigned("timingSpeed", state.study.timingSpeed);
    study.SetUnsigned("timingRemainder", state.study.timingRemainder);
    study.SetBoolean("active", state.study.active);
    study.SetBoolean("pauseRequested", state.study.pauseRequested);
    data.Set("study", eastl::move(study));
    return data;
}

StateResult<LearningState> ReadLearningState(const DataObject& input, const ContentCatalog& content,
    Diagnostics* diagnostics) {
    const auto invalid = [diagnostics](StateError error = StateError::InvalidData,
        const char* message = "Invalid or unsupported learning state") {
        if (diagnostics) { diagnostics->Add(message); }
        return eastl::unexpected(error);
    };
    if (input.Type() == DataType::Object && !input.Size()) { return LearningState{}; }
    const auto version = input.GetUnsigned("version");
    if (!version) { return invalid(); }
    if (*version != 1 && *version != 2) { return invalid(StateError::UnsupportedVersion); }
    if (!Fields(input, {"version", "skills", "recipes", "books", "study"})) { return invalid(); }
    const auto skills = input.Get("skills", DataType::List);
    const auto recipes = input.Get("recipes", DataType::List);
    const auto books = input.Get("books", DataType::List);
    if (!skills || !recipes || !books || (*skills)->Size() > content.SkillCount()
        || (*recipes)->Size() > content.CraftingRecipeCount() || (*books)->Size() > content.BookCount()) { return invalid(); }
    LearningState state;
    bool migrated = false;
    const auto cooking = content.FindSkill("cooking");
    const auto chemistry = content.FindSkill("chemistry");
    for (std::size_t i = 0; i < (*skills)->Size(); ++i) {
        const auto& value = (*skills)->At(i);
        if (!Fields(value, {"skill", "level", "xp", "theory", "practice", "directBookXp", "multiplier",
            "workRemainder", "boostRemainder"})) { return invalid(); }
        const auto name = value.GetString("skill");
        if (!name) { return invalid(); }
        const auto id = content.FindSkill(*name);
        const bool legacy = *version == 1 && !id.value && *name == "cooking_chemistry";
        if (legacy && migrated) {
            return invalid(StateError::InvalidData, "Duplicate legacy cooking_chemistry skill");
        }
        if (migrated && (id == cooking || id == chemistry)) {
            return invalid(StateError::InvalidData, "Conflicting cooking_chemistry migration destination");
        }
        if (!legacy && (!id.value || FindSkillProgress(state, id))) {
            return invalid(StateError::InvalidData, "Unknown, duplicate or conflicting saved skill");
        }
        SkillProgress p{id};
        std::uint64_t level = 0;
        if (!Number(value, "level", maximumSkillLevel, level) || !Number(value, "xp", UINT64_MAX, p.xp)
            || !Number(value, "theory", UINT64_MAX, p.theory) || !Number(value, "practice", UINT64_MAX, p.practice)
            || !Number(value, "directBookXp", UINT64_MAX, p.directBookXp) || !Number32(value, "multiplier", p.multiplier)
            || !Number32(value, "workRemainder", p.workRemainder) || !Number32(value, "boostRemainder", p.boostRemainder)) {
            return invalid(StateError::InvalidData, legacy
                ? "Invalid legacy cooking_chemistry progress" : "Invalid saved skill progress");
        }
        p.level = static_cast<std::uint8_t>(level);
        if (!legacy) {
            EnsureSkillProgress(state, id) = p;
            continue;
        }
        const auto historical = LegacyCookingChemistry();
        if (!ValidSkill(p, historical)) {
            return invalid(StateError::InvalidData, "Invalid legacy cooking_chemistry progress");
        }
        if (!MigrationDestination(content.TryGetSkill(cooking), 5, historical)
            || !MigrationDestination(content.TryGetSkill(chemistry), 10, historical)) {
            return invalid(StateError::InvalidData, "Incompatible cooking_chemistry migration destinations");
        }
        if (FindSkillProgress(state, cooking) || FindSkillProgress(state, chemistry)) {
            return invalid(StateError::InvalidData, "Conflicting cooking_chemistry migration destination");
        }
        p.skill = chemistry;
        EnsureSkillProgress(state, chemistry) = p;
        p.skill = cooking;
        if (p.level >= 5) { p = SkillProgress{cooking}; p.level = 5; }
        EnsureSkillProgress(state, cooking) = p;
        migrated = true;
    }
    for (std::size_t i = 0; i < (*recipes)->Size(); ++i) {
        const auto& value = (*recipes)->At(i);
        if (!Fields(value, {"recipe", "points"})) { return invalid(); }
        const auto name = value.GetString("recipe");
        if (!name) { return invalid(); }
        const auto id = content.FindCraftingRecipe(*name);
        if (!id.value) { return invalid(); }
        for (const auto& p : state.recipes) { if (p.recipe == id) { return invalid(); } }
        RecipeProgress p{id};
        if (!Number(value, "points", RecipeLearningGoal(content.GetCraftingRecipe(id)), p.points)) { return invalid(); }
        state.recipes.push_back(p);
    }
    eastl::sort(state.recipes.begin(), state.recipes.end(), [](const auto& a, const auto& b) { return a.recipe.value < b.recipe.value; });
    for (std::size_t i = 0; i < (*books)->Size(); ++i) {
        const auto& value = (*books)->At(i);
        if (!Fields(value, {"book", "work", "used"})) { return invalid(); }
        const auto name = value.GetString("book");
        const auto used = value.GetBoolean("used");
        if (!name || !used) { return invalid(); }
        const auto id = content.FindBook(*name);
        if (!id.value || FindBookProgress(state, id)) { return invalid(); }
        auto& p = EnsureBookProgress(state, id);
        p.used = *used;
        if (!Number(value, "work", UINT64_MAX, p.work)) { return invalid(); }
    }
    const auto& study = *input.Find("study");
    if (!Fields(study, {"book", "remaining", "pendingWork", "timingSpeed", "timingRemainder", "active", "pauseRequested"})) { return invalid(); }
    const auto name = study.GetString("book");
    const auto active = study.GetBoolean("active");
    const auto pause = study.GetBoolean("pauseRequested");
    if (!name || !active || !pause) { return invalid(); }
    if (!name->empty()) {
        state.study.book = content.FindBook(*name);
        if (!state.study.book.value) { return invalid(); }
    }
    state.study.active = *active;
    state.study.pauseRequested = *pause;
    if (!Number(study, "remaining", UINT64_MAX, state.study.remaining)
        || !Number(study, "pendingWork", UINT64_MAX, state.study.pendingWork)
        || !Number32(study, "timingSpeed", state.study.timingSpeed)
        || !Number32(study, "timingRemainder", state.study.timingRemainder)
        || !ValidState(state, content)) { return invalid(); }
    if (migrated) { UpdateAutolearn(state, content); }
    return state;
}

}
