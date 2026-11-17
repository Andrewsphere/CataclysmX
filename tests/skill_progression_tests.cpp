#include "skills/skill_state.h"
#include "content/content.h"
#include <catch2/catch_test_macros.hpp>
#include <initializer_list>

namespace {
using namespace cataclysmx;
constexpr std::uint64_t xpUnit = ratioScale;

ContentCatalog LearningContent(bool extra = false) {
    const ContentSource sources[]{
        {"learning", R"({format_version:1,definitions:[
          {type:'skill',id:'making',name:'Making',xp_to_next:[100,100,100,100,100,100,100,100,100,100]},
          {type:'skill',id:'melee',name:'Melee',category:'combat',xp_to_next:[100,100,100,100,100,100,100,100,100,100]},
          {type:'book',id:'strong',name:'Strong',study_work:100,benefits:[
            {skill:'making',first_level:1,last_level:5,direct_xp:20,multiplier:30000}]},
          {type:'book',id:'weak',name:'Weak',study_work:100,benefits:[
            {skill:'making',first_level:1,last_level:5,direct_xp:20,multiplier:20000}]},
          {type:'book',id:'mixed',name:'Mixed',study_work:100,benefits:[
            {skill:'making',first_level:1,last_level:10,direct_xp:20,theory_xp:200,multiplier:40000}]},
          {type:'book',id:'combat',name:'Combat',study_work:100,benefits:[
            {skill:'melee',first_level:1,last_level:10,multiplier:12000}]},
          {type:'book',id:'both',name:'Both',study_work:100,recipes:['manual'],benefits:[
            {skill:'making',first_level:1,last_level:5,multiplier:20000},
            {skill:'melee',first_level:1,last_level:10,multiplier:11000}]},
          {type:'item',id:'material',name:'Material',mass_g:1,volume_ml:1},
          {type:'item',id:'result',name:'Result',mass_g:1,volume_ml:1},
          {type:'recipe',id:'common',inputs:[{item:'material',count:1}],result:{item:'result',count:1},work:10},
          {type:'recipe',id:'manual',inputs:[{item:'material',count:1}],result:{item:'result',count:1},work:10,
            knowledge:{crafts_to_learn:2,disassemblies_to_learn:3}},
          {type:'recipe',id:'automatic',inputs:[{item:'material',count:1}],result:{item:'result',count:1},work:10,
            skills:[{skill:'making',level:2}],knowledge:{autolearn:[{skill:'melee',level:1}]} }
        ]})"},
        {"extra", R"({format_version:1,definitions:[{type:'skill',id:'aaa',name:'Earlier'}]})"}
    };
    Diagnostics diagnostics;
    auto result = LoadContent({sources, extra ? 2u : 1u}, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str());
    REQUIRE(result);
    return eastl::move(*result);
}

SkillAward Train(LearningState& state, const ContentCatalog& content, SkillId skill,
    std::uint64_t xp, SkillActivity activity = SkillActivity::Craft, std::uint8_t ceiling = 9) {
    return TrainSkill(state, content, {skill, ceiling, ratioScale}, activity, xp * xpUnit);
}

TEST_CASE("Skill teaching ceilings and ordinary to paired boundary clip surplus", "[skills][progression]") {
    const auto content = LearningContent();
    const auto skill = content.FindSkill("making");
    LearningState state;
    auto award = Train(state, content, skill, 250, SkillActivity::Craft, 0);
    CHECK(award.advancement == 100 * xpUnit);
    CHECK(award.capped == 150 * xpUnit);
    CHECK(award.levels == 1);
    CHECK(EffectiveSkillLevel(state, skill, 20) == 21);
    CHECK(EffectiveSkillLevel(state, skill, -20) == 0);
    const SkillRequirement requirement{skill, 2};
    CHECK_FALSE(MeetsSkillRequirements(state, {&requirement, 1}));
    award = Train(state, content, skill, 1000);
    CHECK(award.levels == 4);
    CHECK(award.capped == 600 * xpUnit);
    const auto* p = FindSkillProgress(state, skill);
    REQUIRE(p);
    CHECK(p->level == 5);
    CHECK(p->practice == 0);
    CHECK(p->theory == 0);
    CHECK(p->xp == 0);
    CHECK(Train(state, content, skill, 1, SkillActivity::Craft, 3).capped == xpUnit);
}

TEST_CASE("Advanced reserves cap remaining XP and pair without cross level banking", "[skills][progression]") {
    const auto content = LearningContent();
    const auto skill = content.FindSkill("making");
    LearningState state;
    EnsureSkillProgress(state, skill).level = 5;
    auto award = Train(state, content, skill, 80, SkillActivity::Disassembly);
    CHECK(award.advancement == 0);
    award = Train(state, content, skill, 30);
    CHECK(award.advancement == 30 * xpUnit);
    CHECK(FindSkillProgress(state, skill)->theory == 50 * xpUnit);
    award = Train(state, content, skill, 1000, SkillActivity::Disassembly);
    CHECK(award.capped == 980 * xpUnit);
    CHECK(FindSkillProgress(state, skill)->theory == 70 * xpUnit);
    award = Train(state, content, skill, 200);
    CHECK(award.levels == 1);
    CHECK(award.advancement == 70 * xpUnit);
    CHECK(award.capped == 130 * xpUnit);
    CHECK(FindSkillProgress(state, skill)->level == 6);
    CHECK(FindSkillProgress(state, skill)->practice == 0);
    CHECK(FindSkillProgress(state, skill)->theory == 0);
    CHECK(Train(state, content, skill, 20, SkillActivity::Combat).capped == 20 * xpUnit);
}

TEST_CASE("Books share the full-level direct cap and retain only the strongest boost", "[skills][books]") {
    const auto content = LearningContent();
    const auto skill = content.FindSkill("making");
    const auto strong = content.FindBook("strong");
    LearningState state;
    REQUIRE(CompleteBookStudy(state, content, strong));
    REQUIRE(CompleteBookStudy(state, content, content.FindBook("weak")));
    CHECK(FindSkillProgress(state, skill)->xp == 30 * xpUnit);
    CHECK(FindSkillProgress(state, skill)->directBookXp == 30 * xpUnit);
    CHECK(FindSkillProgress(state, skill)->multiplier == 30000);
    CHECK_FALSE(CompleteBookStudy(state, content, strong));
    CHECK(FindBookProgress(state, strong)->work == 100 * xpUnit);
    const auto award = Train(state, content, skill, 30);
    CHECK(award.levels == 1);
    // 23 1/3 base XP completes the boosted level; the rest is unboosted.
    CHECK(FindSkillProgress(state, skill)->xp == 66666);
    CHECK(FindSkillProgress(state, skill)->multiplier == ratioScale);
    CHECK(FindSkillProgress(state, skill)->directBookXp == 0);
    CHECK_FALSE(CanStudyBook(state, content, strong));
}

TEST_CASE("A book that levels a skill does not boost its new level", "[skills][books]") {
    const auto content = LearningContent();
    LearningState state;
    const auto skill = content.FindSkill("making");
    Train(state, content, skill, 90);
    REQUIRE(CompleteBookStudy(state, content, content.FindBook("strong")));
    CHECK(LearnedSkillLevel(state, skill) == 1);
    CHECK(FindSkillProgress(state, skill)->xp == 0);
    CHECK(FindSkillProgress(state, skill)->multiplier == ratioScale);
    CHECK(FindSkillProgress(state, skill)->directBookXp == 0);
}

TEST_CASE("Mixed band books grant only theory in advanced levels", "[skills][books]") {
    const auto content = LearningContent();
    const auto skill = content.FindSkill("making");
    const auto mixed = content.FindBook("mixed");
    LearningState state;
    EnsureSkillProgress(state, skill).level = 5;
    CHECK_FALSE(CompleteBookStudy(state, content, content.FindBook("strong")));
    CHECK(FindBookProgress(state, content.FindBook("strong")) == nullptr);
    Train(state, content, skill, 100);
    REQUIRE(CompleteBookStudy(state, content, mixed));
    const auto* p = FindSkillProgress(state, skill);
    CHECK(p->level == 6);
    CHECK(p->xp == 0);
    CHECK(p->theory == 0);
    CHECK(p->multiplier == ratioScale);
    CHECK_FALSE(CompleteBookStudy(state, content, mixed));
}

TEST_CASE("Combat keeps ordinary XP through mastery with a small expiring book boost", "[skills][combat]") {
    const auto content = LearningContent();
    const auto skill = content.FindSkill("melee");
    LearningState state;
    EnsureSkillProgress(state, skill).level = 5;
    REQUIRE(CompleteBookStudy(state, content, content.FindBook("combat")));
    CHECK(FindSkillProgress(state, skill)->xp == 0);
    CHECK(FindSkillProgress(state, skill)->multiplier == 12000);
    Train(state, content, skill, 10, SkillActivity::Combat);
    CHECK(FindSkillProgress(state, skill)->xp == 12 * xpUnit);
    const auto award = TrainSkill(state, content, {skill, 10, ratioScale}, SkillActivity::Combat, UINT64_MAX);
    CHECK(award.levels == 5);
    CHECK(award.advancement == 488 * xpUnit);
    CHECK(award.capped > 0);
    CHECK(LearnedSkillLevel(state, skill) == 10);
    CHECK(EffectiveSkillLevel(state, skill, 5) == 15);
    CHECK(FindSkillProgress(state, skill)->multiplier == ratioScale);
    CHECK(ReadLearningState(CaptureLearningState(state, content), content));
}

TEST_CASE("Fractional work and boost credit survives subdivision and persistence", "[skills][progression]") {
    const auto content = LearningContent();
    const auto skill = content.FindSkill("melee");
    LearningState bulk;
    REQUIRE(CompleteBookStudy(bulk, content, content.FindBook("combat")));
    auto split = bulk;
    const SkillTraining training{skill, 9, 137};
    TrainSkill(bulk, content, training, SkillActivity::Combat, 10301);
    for (unsigned i = 0; i < 10301; ++i) {
        TrainSkill(split, content, training, SkillActivity::Combat, 1);
        if (i == 100) {
            auto restored = ReadLearningState(CaptureLearningState(split, content), content);
            REQUIRE(restored);
            split = eastl::move(*restored);
        }
    }
    CHECK(split == bulk);
    CHECK(FindSkillProgress(bulk, skill)->workRemainder != 0);
}

TEST_CASE("Fractional surplus stops at teaching and ordinary band boundaries", "[skills][progression]") {
    const auto content = LearningContent();
    const auto skill = content.FindSkill("making");
    LearningState bulk;
    auto& progress = EnsureSkillProgress(bulk, skill);
    SkillTraining training{skill, 0, 5000};
    auto activity = SkillActivity::Craft;
    SECTION("teaching ceiling") {}
    SECTION("ordinary to advanced at the teaching ceiling") {
        progress.level = 4;
        training.level = 4;
    }
    SECTION("ordinary to advanced with an otherwise eligible source") {
        progress.level = 4;
        training.level = 9;
        // Combat cannot add advanced reserves after the band transition.
        activity = SkillActivity::Combat;
    }
    progress.xp = SkillXpRequired(content.GetSkill(skill), progress.level) - 1;
    const auto nextLevel = progress.level + 1;
    auto split = bulk;
    const auto award = TrainSkill(bulk, content, training, activity, 3);
    CHECK(award.levels == 1);
    CHECK(award.capped == 0);
    TrainSkill(split, content, training, activity, 2);
    TrainSkill(split, content, training, activity, 1);
    CHECK(bulk == split);
    CHECK(FindSkillProgress(bulk, skill)->level == nextLevel);
    CHECK(FindSkillProgress(bulk, skill)->workRemainder == 0);
    training.level = 9;
    TrainSkill(bulk, content, training, SkillActivity::Craft, 1);
    TrainSkill(split, content, training, SkillActivity::Craft, 1);
    CHECK(bulk == split);
    CHECK(FindSkillProgress(bulk, skill)->xp == 0);
    CHECK(FindSkillProgress(bulk, skill)->practice == 0);
    CHECK(FindSkillProgress(bulk, skill)->workRemainder == 5000);
}

TEST_CASE("Eligible ordinary level-ups retain fractional work", "[skills][progression]") {
    const auto content = LearningContent();
    const auto skill = content.FindSkill("making");
    LearningState bulk;
    EnsureSkillProgress(bulk, skill).xp = SkillXpRequired(content.GetSkill(skill), 0) - 1;
    std::uint64_t work = 3;
    unsigned expectedLevel = 1;
    SECTION("no whole unit remains after leveling") {}
    SECTION("source continues across multiple levels") {
        work += 2 * SkillXpRequired(content.GetSkill(skill), 1);
        expectedLevel = 2;
    }
    auto split = bulk;
    const SkillTraining training{skill, 9, 5000};
    TrainSkill(bulk, content, training, SkillActivity::Craft, work);
    TrainSkill(split, content, training, SkillActivity::Craft, 2);
    TrainSkill(split, content, training, SkillActivity::Craft, work - 2);
    CHECK(bulk == split);
    CHECK(FindSkillProgress(bulk, skill)->level == expectedLevel);
    CHECK(FindSkillProgress(bulk, skill)->workRemainder == 5000);
    CHECK(TrainSkill(bulk, content, training, SkillActivity::Craft, 1).advancement == 1);
}

TEST_CASE("Advanced reserves discard only capacity-bound fractional work", "[skills][progression]") {
    const auto content = LearningContent();
    const auto skill = content.FindSkill("making");
    const SkillTraining training{skill, 9, 5000};
    for (const auto activity : {SkillActivity::Craft, SkillActivity::Disassembly}) {
        LearningState bulk;
        auto& progress = EnsureSkillProgress(bulk, skill);
        progress.level = 5;
        const auto required = SkillXpRequired(content.GetSkill(skill), 5);
        auto& reserve = activity == SkillActivity::Disassembly ? progress.theory : progress.practice;
        reserve = required - 1;
        std::uint64_t work = 3;
        bool capped = true;
        bool levels = false;
        SECTION("exact whole-unit fill discards the fraction") {}
        SECTION("whole-unit surplus also discards the fraction") { work = 5; }
        SECTION("already full reserve rejects a fraction-only award") { reserve = required; work = 1; }
        SECTION("unfilled reserve retains the fraction") { reserve = required - 2; capped = false; }
        SECTION("pairing completes the level with no fractional banking") {
            progress.xp = required - 1;
            reserve = 0;
            auto& opposite = activity == SkillActivity::Disassembly ? progress.practice : progress.theory;
            opposite = 1;
            // At the teaching ceiling the second split award is ineligible.
            levels = true;
        }
        auto split = bulk;
        auto source = training;
        if (levels) { source.level = 5; }
        TrainSkill(bulk, content, source, activity, work);
        const auto firstWork = work > 1 ? 2u : 1u;
        TrainSkill(split, content, source, activity, firstWork);
        TrainSkill(split, content, source, activity, work - firstWork);
        CHECK(bulk == split);
        CHECK(FindSkillProgress(bulk, skill)->workRemainder == (capped ? 0u : 5000u));
        CHECK(FindSkillProgress(bulk, skill)->level == (levels ? 6 : 5));
        // A different eligible reserve must not receive the discarded fraction.
        const auto oppositeActivity = activity == SkillActivity::Disassembly
            ? SkillActivity::Craft : SkillActivity::Disassembly;
        TrainSkill(bulk, content, training, oppositeActivity, 1);
        TrainSkill(split, content, training, oppositeActivity, 1);
        CHECK(bulk == split);
        CHECK(FindSkillProgress(bulk, skill)->xp == (levels || capped ? 0u : 1u));
    }
}

TEST_CASE("All book benefits are once per identity and reading never memorizes recipes", "[skills][books]") {
    const auto content = LearningContent();
    LearningState state;
    const auto book = content.FindBook("both");
    REQUIRE(CompleteBookStudy(state, content, book));
    CHECK(FindSkillProgress(state, content.FindSkill("making"))->multiplier == 20000);
    CHECK(FindSkillProgress(state, content.FindSkill("melee"))->multiplier == 11000);
    CHECK_FALSE(KnowsRecipe(state, content, content.FindCraftingRecipe("manual")));
    CHECK_FALSE(CanStudyBook(state, content, book));
}

TEST_CASE("Recipe points combine sources and autolearn permanently requires both sets of skills", "[skills][recipes]") {
    const auto content = LearningContent();
    const auto making = content.FindSkill("making"), melee = content.FindSkill("melee");
    const auto manual = content.FindCraftingRecipe("manual"), automatic = content.FindCraftingRecipe("automatic");
    LearningState state;
    CHECK(KnowsRecipe(state, content, content.FindCraftingRecipe("common")));
    CHECK(RecipeLearningGoal(content.GetCraftingRecipe(manual)) == 6 * xpUnit);
    LearnRecipe(state, content, manual, 3 * xpUnit);
    CHECK(RecipeLearningRatio(state, content, manual) == 5000);
    LearnRecipe(state, content, manual, 2 * xpUnit);
    CHECK_FALSE(KnowsRecipe(state, content, manual));
    LearnRecipe(state, content, manual, UINT64_MAX);
    CHECK(KnowsRecipe(state, content, manual));
    CHECK(RecipeLearningRatio(state, content, manual) == ratioScale);
    Train(state, content, melee, 100, SkillActivity::Combat);
    CHECK_FALSE(KnowsRecipe(state, content, automatic));
    Train(state, content, making, 200);
    CHECK(KnowsRecipe(state, content, automatic));
    // Materialized knowledge survives subsequent changes to learned skills.
    EnsureSkillProgress(state, making).level = 0;
    CHECK(KnowsRecipe(state, content, automatic));
    LearningState derived;
    EnsureSkillProgress(derived, making).level = 2;
    EnsureSkillProgress(derived, melee).level = 1;
    CHECK(KnowsRecipe(derived, content, automatic));
    CHECK(derived.recipes.empty());
}

TEST_CASE("Learning state saves stable identities and restores canonical arrays", "[skills][state]") {
    const auto content = LearningContent();
    const auto changed = LearningContent(true);
    LearningState state;
    const auto making = content.FindSkill("making");
    EnsureSkillProgress(state, content.FindSkill("melee")).level = 2;
    EnsureSkillProgress(state, making).level = 5;
    Train(state, content, making, 30, SkillActivity::Disassembly);
    LearnRecipe(state, content, content.FindCraftingRecipe("manual"), 1234);
    const auto book = content.FindBook("mixed");
    EnsureBookProgress(state, book).work = 123;
    state.study = {book, 10, 500000, 137, 5, true, true};
    const auto saved = CaptureLearningState(state, content);
    auto restored = ReadLearningState(saved, content);
    REQUIRE(restored);
    CHECK(*restored == state);
    restored = ReadLearningState(saved, changed);
    REQUIRE(restored);
    CHECK(FindSkillProgress(*restored, changed.FindSkill("making"))->theory == 30 * xpUnit);
    CHECK(CaptureLearningState(*restored, changed) == saved);
    state.study.active = false;
    state.study.pauseRequested = false;
    state.study.remaining = state.study.pendingWork = 0;
    restored = ReadLearningState(CaptureLearningState(state, content), content);
    REQUIRE(restored);
    CHECK(*restored == state);
    const auto legacy = ReadLearningState(DataObject{}, content);
    REQUIRE(legacy);
    CHECK(*legacy == LearningState{});
}

TEST_CASE("Funded study preserves carry-only slices at maximum speed", "[skills][state]") {
    const auto content = LearningContent();
    LearningState state;
    const auto book = content.FindBook("strong");
    EnsureBookProgress(state, book);
    state.study = {book, 0, 0, UINT32_MAX, 0, true, false};
    constexpr std::uint64_t sliceNumerator = std::uint64_t{100} * ticks_per_turn * ratioScale;
    for (unsigned slice = 0; slice < 43; ++slice) {
        const auto numerator = sliceNumerator + state.study.timingRemainder;
        state.study.pendingWork = numerator / state.study.timingSpeed;
        state.study.timingRemainder = static_cast<std::uint32_t>(numerator % state.study.timingSpeed);
        state.study.remaining = 100;
        CHECK(state.study.pendingWork == (slice == 42 ? 1u : 0u));
        auto restored = ReadLearningState(CaptureLearningState(state, content), content);
        REQUIRE(restored);
        CHECK(*restored == state);
        state = eastl::move(*restored);
        // A due slice with zero work is also a valid persisted state.
        state.study.remaining = 0;
        restored = ReadLearningState(CaptureLearningState(state, content), content);
        REQUIRE(restored);
        CHECK(*restored == state);
        EnsureBookProgress(state, book).work += state.study.pendingWork;
        state.study.pendingWork = 0;
    }
    CHECK(FindBookProgress(state, book)->work == 1);
    CHECK(state.study.timingRemainder == 5032705);
}

TEST_CASE("Funded study remains serializable after training leaves the teaching range", "[skills][state]") {
    const auto content = LearningContent();
    LearningState state;
    const auto book = content.FindBook("strong");
    EnsureBookProgress(state, book).work = 123;
    REQUIRE(CanStudyBook(state, content, book));
    state.study = {book, 37, 500000, 100, 0, true, false};
    Train(state, content, content.FindSkill("making"), 500);
    CHECK_FALSE(CanStudyBook(state, content, book));
    CHECK(state.study.active);
    const auto restored = ReadLearningState(CaptureLearningState(state, content), content);
    REQUIRE(restored);
    CHECK(*restored == state);
    CHECK_FALSE(FindBookProgress(*restored, book)->used);
}

TEST_CASE("Learning state rejects corrupt references credit categories and study", "[skills][state]") {
    const auto content = LearningContent();
    LearningState state;
    const auto skill = content.FindSkill("making");
    EnsureSkillProgress(state, skill).level = 5;
    const auto book = content.FindBook("mixed");
    EnsureBookProgress(state, book).work = 10;
    auto saved = CaptureLearningState(state, content);
    auto skillValue = saved.Find("skills")->At(0);
    SECTION("unknown skill") { skillValue.SetString("skill", "missing"); }
    SECTION("numeric identity") { skillValue.SetUnsigned("skill", skill.value); }
    SECTION("overfull XP") { skillValue.SetUnsigned("xp", 100 * xpUnit); }
    SECTION("overfull reserve") { skillValue.SetUnsigned("xp", 90 * xpUnit); skillValue.SetUnsigned("theory", 11 * xpUnit); }
    SECTION("unconsumed pairs") { skillValue.SetUnsigned("theory", 1); skillValue.SetUnsigned("practice", 1); }
    SECTION("advanced boost") { skillValue.SetUnsigned("multiplier", 20000); }
    SECTION("advanced direct credit") { skillValue.SetUnsigned("directBookXp", 1); }
    SECTION("ordinary reserve") { skillValue.SetUnsigned("level", 4); skillValue.SetUnsigned("practice", 1); }
    SECTION("direct book cap") {
        skillValue.SetUnsigned("level", 4); skillValue.SetUnsigned("xp", 40 * xpUnit);
        skillValue.SetUnsigned("directBookXp", 31 * xpUnit);
    }
    SECTION("mastery residual") { skillValue.SetUnsigned("level", 10); skillValue.SetUnsigned("xp", 1); }
    SECTION("bad fractional carry") { skillValue.SetUnsigned("workRemainder", ratioScale); }
    SECTION("inactive pending work") { saved.Find("study")->SetUnsigned("pendingWork", 1); }
    SECTION("unknown retained book") { saved.Find("study")->SetString("book", "missing"); }
    SECTION("active without book") { saved.Find("study")->SetBoolean("active", true); }
    SECTION("active overfunded") {
        saved.Find("study")->SetBoolean("active", true);
        saved.Find("study")->SetString("book", "mixed");
        saved.Find("study")->SetUnsigned("pendingWork", 100 * xpUnit);
    }
    SECTION("active impossible duration") {
        saved.Find("study")->SetBoolean("active", true);
        saved.Find("study")->SetString("book", "mixed");
        saved.Find("study")->SetUnsigned("pendingWork", xpUnit);
        saved.Find("study")->SetUnsigned("remaining", 101);
    }
    SECTION("active credit exceeds slice plus carry bound") {
        saved.Find("study")->SetBoolean("active", true);
        saved.Find("study")->SetString("book", "mixed");
        saved.Find("study")->SetUnsigned("timingSpeed", UINT32_MAX);
        saved.Find("study")->SetUnsigned("pendingWork", 2);
    }
    SECTION("active completed book") {
        saved.Find("study")->SetBoolean("active", true);
        saved.Find("study")->SetString("book", "mixed");
        auto value = saved.Find("books")->At(0);
        value.SetUnsigned("work", 100 * xpUnit);
        auto list = DataObject::List(); list.Append(value); saved.Set("books", eastl::move(list));
    }
    SECTION("active consumed book") {
        saved.Find("study")->SetBoolean("active", true);
        saved.Find("study")->SetString("book", "mixed");
        auto value = saved.Find("books")->At(0);
        value.SetBoolean("used", true); value.SetUnsigned("work", 100 * xpUnit);
        auto list = DataObject::List(); list.Append(value); saved.Set("books", eastl::move(list));
    }
    SECTION("invalid timing carry") { saved.Find("study")->SetUnsigned("timingRemainder", 100); }
    SECTION("book overcomplete") {
        auto value = saved.Find("books")->At(0);
        value.SetUnsigned("work", 100 * xpUnit + 1);
        auto list = DataObject::List(); list.Append(value); saved.Set("books", eastl::move(list));
    }
    SECTION("used book incomplete") {
        auto value = saved.Find("books")->At(0);
        value.SetBoolean("used", true);
        auto list = DataObject::List(); list.Append(value); saved.Set("books", eastl::move(list));
    }
    SECTION("recipe overcap") {
        DataObject value; value.SetString("recipe", "manual"); value.SetUnsigned("points", 6 * xpUnit + 1);
        auto list = DataObject::List(); list.Append(value); saved.Set("recipes", eastl::move(list));
    }
    SECTION("unexpected field") { saved.SetBoolean("extra", true); }
    SECTION("duplicate identity") {
        saved.Find("skills")->Append(skillValue);
        CHECK_FALSE(ReadLearningState(saved, content));
        return;
    }
    auto list = DataObject::List(); list.Append(skillValue); saved.Set("skills", eastl::move(list));
    CHECK_FALSE(ReadLearningState(saved, content));
}

}
