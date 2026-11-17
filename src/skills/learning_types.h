#pragma once

#include "skills/skill_state.h"

namespace cataclysmx {
enum class StudyOutcome : std::uint8_t {
    Started, PauseRequested, Paused, ActorUnavailable, ActorBusy,
    MissingReference, AlreadyUsed, OutsideTeachingRange
};
struct StudyResult {
    StudyOutcome outcome = StudyOutcome::ActorUnavailable;
};
struct SkillView {
    SkillProgress progress;
    int effectiveLevel = 0;
    std::uint64_t requiredXp = 0;
    bool operator==(const SkillView&) const = default;
};
struct RecipeKnowledgeView {
    CraftingRecipeId recipe{};
    bool known = false;
    bool referenceAvailable = false;
    bool requirementsMet = false;
    std::uint32_t learningRatio = 0;
    bool operator==(const RecipeKnowledgeView&) const = default;
};
struct BookStudyView {
    BookId book{};
    bool accessible = false;
    bool used = false;
    bool eligible = false;
    std::uint64_t completedWork = 0;
    // Preview clipping, fixed-point XP across eligible benefits.
    std::uint64_t cappedDirectXp = 0;
    std::uint64_t cappedTheoryXp = 0;
    bool operator==(const BookStudyView&) const = default;
};
struct LearningView {
    Vector<SkillView> skills;
    Vector<RecipeKnowledgeView> recipes;
    Vector<BookStudyView> books;
    BookId activeBook{};
    Tick studyRemaining = 0;
    bool studyPauseRequested = false;
    bool operator==(const LearningView&) const = default;
};
}
