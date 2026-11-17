#pragma once

#include "content/book_definition.h"
#include "core/data_object.h"
#include "core/state_result.h"
#include "core/timing.h"
#include <EASTL/span.h>

namespace cataclysmx {
class ContentCatalog;
// XP and reserves use 1/ratioScale XP, independent of work subdivision.
struct SkillProgress {
    SkillId skill{};
    std::uint8_t level = 0;
    std::uint64_t xp = 0;
    std::uint64_t theory = 0;
    std::uint64_t practice = 0;
    std::uint64_t directBookXp = 0;
    std::uint32_t multiplier = ratioScale;
    std::uint32_t workRemainder = 0;
    std::uint32_t boostRemainder = 0;
    bool operator==(const SkillProgress&) const = default;
};
struct RecipeProgress {
    CraftingRecipeId recipe{};
    // Goal = craftsToLearn * disassembliesToLearn * ratioScale.
    std::uint64_t points = 0;
    bool operator==(const RecipeProgress&) const = default;
};
struct BookProgress {
    BookId book{};
    std::uint64_t work = 0; // 1/ratioScale base work; retained when paused.
    bool used = false;
    bool operator==(const BookProgress&) const = default;
};
struct StudyState {
    BookId book{};
    Tick remaining = 0;
    std::uint64_t pendingWork = 0;
    std::uint32_t timingSpeed = 100;
    std::uint32_t timingRemainder = 0;
    bool active = false;
    bool pauseRequested = false;
    bool operator==(const StudyState&) const = default;
};
struct LearningState {
    // Sparse, catalog-ID ordered. Empty actors incur no element allocations.
    Vector<SkillProgress> skills;
    Vector<RecipeProgress> recipes;
    Vector<BookProgress> books;
    StudyState study;
    bool operator==(const LearningState&) const = default;
};
struct SkillAward {
    std::uint64_t advancement = 0;
    std::uint64_t capped = 0;
    std::uint8_t levels = 0;
};
const SkillProgress* FindSkillProgress(const LearningState& state, SkillId skill);
SkillProgress& EnsureSkillProgress(LearningState& state, SkillId skill);
const BookProgress* FindBookProgress(const LearningState& state, BookId book);
BookProgress& EnsureBookProgress(LearningState& state, BookId book);
std::uint8_t LearnedSkillLevel(const LearningState& state, SkillId skill);
int EffectiveSkillLevel(const LearningState& state, SkillId skill, int modifier = 0);
bool MeetsSkillRequirements(const LearningState& state, eastl::span<const SkillRequirement> requirements);
std::uint64_t SkillXpRequired(const SkillDefinition& definition, std::uint8_t currentLevel);
SkillAward TrainSkill(LearningState& state, const ContentCatalog& content,
    const SkillTraining& training, SkillActivity activity, std::uint64_t fixedWork);
bool CanStudyBook(const LearningState& state, const ContentCatalog& content, BookId book);
// Applies all eligible benefits once, including clipping; never memorizes recipes.
bool CompleteBookStudy(LearningState& state, const ContentCatalog& content, BookId book);
std::uint64_t RecipeLearningGoal(const CraftingRecipeDefinition& recipe);
bool KnowsRecipe(const LearningState& state, const ContentCatalog& content, CraftingRecipeId recipe);
std::uint32_t RecipeLearningRatio(const LearningState& state, const ContentCatalog& content, CraftingRecipeId recipe);
void LearnRecipe(LearningState& state, const ContentCatalog& content, CraftingRecipeId recipe, std::uint64_t points);
// Materialize newly reached autolearn thresholds as permanent knowledge.
void UpdateAutolearn(LearningState& state, const ContentCatalog& content);
DataObject CaptureLearningState(const LearningState& state, const ContentCatalog& content);
StateResult<LearningState> ReadLearningState(const DataObject& input, const ContentCatalog& content,
    Diagnostics* diagnostics = nullptr);
}
