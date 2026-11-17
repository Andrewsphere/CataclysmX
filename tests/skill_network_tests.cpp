#include "network/network.h"
#include "network/wire.h"

#include <catch2/catch_test_macros.hpp>
#include <cstring>

namespace {
using namespace cataclysmx;
using namespace cataclysmx::network;
constexpr char LearningData[] = R"({format_version:1,definitions:[
 {type:'skill',id:'making',name:'Making',category:'noncombat',xp_to_next:[100,200,400,800,1200,1800,2600,3600,4800,6400]},
 {type:'book',id:'manual',name:'Manual',study_work:250,benefits:[{skill:'making',first_level:1,last_level:5,direct_xp:10,theory_xp:20,multiplier:11000}],recipes:['make']},
 {type:'book',id:'other',name:'Other',study_work:300,recipes:['make']},
 {type:'item',id:'product',name:'Product',mass_g:1,volume_ml:1},
 {type:'item',id:'reader',name:'Reader',mass_g:1,volume_ml:1,books:['manual'],reading_power:'battery',
  capabilities:[{type:'energy_store',id:'battery',capacity_energy:10,rechargeable:false}]},
 {type:'recipe',id:'make',inputs:[],result:{item:'product',count:1},work:1,skills:[{skill:'making',level:1}],
  training:[{skill:'making',level:1,xp_per_work:100}],knowledge:{known_by_default:false,autolearn:[{skill:'making',level:1}],crafts_to_learn:2,disassemblies_to_learn:3}},
 {type:'recycling_recipe',id:'unmake',target:'product',returns:[],work:1,skills:[{skill:'making',level:1}],
  training:[{skill:'making',level:1,xp_per_work:100}],teaches_recipes:['make']}
]})";
ContentCatalog LearningCatalog(eastl::string_view text = LearningData) {
    const ContentSource source{"learning-network.json5", text}; Diagnostics diagnostics;
    auto result = LoadContent({&source, 1}, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str()); REQUIRE(result); return eastl::move(*result);
}
WorldView LearningSnapshot(const ContentCatalog& content) {
    WorldView v; v.width = 1; v.height = 1; v.playerId = 1; v.cells.push_back({}); v.remainingTicks = 70;
    SkillView s; s.progress.skill = content.FindSkill("making"); s.progress.xp = ratioScale;
    s.progress.directBookXp = ratioScale; s.effectiveLevel = 0; s.requiredXp = 100 * ratioScale;
    v.learning.skills.push_back(s);
    v.learning.recipes.push_back({content.FindCraftingRecipe("make"), false, true, false, 5000});
    v.learning.books.push_back({content.FindBook("manual"), true, false, true, 100 * ratioScale, 0, 0});
    v.learning.activeBook = content.FindBook("manual"); v.learning.studyRemaining = 70; v.learning.studyPauseRequested = true;
    return v;
}

TEST_CASE("Study command and result codecs preserve authoritative outcomes", "[skills][network][codec]") {
    for (auto kind : {CommandKind::study_book, CommandKind::pause_study}) {
        Command c; c.kind = kind; if (kind == CommandKind::study_book) c.book = {42};
        wire::Writer w; REQUIRE(w.CommandValue(c)); Command decoded;
        wire::Reader r{{w.bytes.data(), w.bytes.size()}};
        REQUIRE(r.CommandValue(decoded)); CHECK(r.Done()); CHECK(decoded.kind == kind); CHECK(decoded.book == c.book);
        for (std::size_t size = 0; size < w.bytes.size(); ++size) {
            wire::Reader truncated{{w.bytes.data(), size}}; CHECK_FALSE(truncated.CommandValue(decoded));
        }
        if (kind == CommandKind::pause_study) {
            w.bytes.back() = 1; wire::Reader bad{{w.bytes.data(), w.bytes.size()}}; CHECK_FALSE(bad.CommandValue(decoded));
        }
    }
    for (auto outcome : {StudyOutcome::Started, StudyOutcome::PauseRequested, StudyOutcome::Paused,
        StudyOutcome::ActorUnavailable, StudyOutcome::ActorBusy, StudyOutcome::MissingReference,
        StudyOutcome::AlreadyUsed, StudyOutcome::OutsideTeachingRange}) {
        PlayerCommandResult receipt{1, 2, 3, {Outcome::study_refused, false, 40}};
        receipt.result.studyOutcome = outcome;
        if (outcome == StudyOutcome::Started) {
            receipt.result.outcome = Outcome::study_started; receipt.result.advanced = true; receipt.result.cost = 100;
        } else if (outcome == StudyOutcome::Paused || outcome == StudyOutcome::PauseRequested) receipt.result.outcome = Outcome::study_paused;
        wire::Writer w; REQUIRE(w.Result(receipt)); wire::Reader r{{w.bytes.data(), w.bytes.size()}}; PlayerCommandResult decoded;
        REQUIRE(r.Result(decoded)); CHECK(r.Done()); CHECK(decoded.result.studyOutcome == outcome);
        CHECK(decoded.result.outcome == receipt.result.outcome); CHECK(decoded.result.cost == receipt.result.cost);
        w.bytes.back() = 255; wire::Reader bad{{w.bytes.data(), w.bytes.size()}}; CHECK_FALSE(bad.Result(decoded));
        receipt.result.advanced = !receipt.result.advanced; wire::Writer invalid; CHECK_FALSE(invalid.Result(receipt));
    }
}

TEST_CASE("Learning snapshots roundtrip with strict shape and catalog validation", "[skills][network][codec]") {
    const auto content = LearningCatalog(); auto v = LearningSnapshot(content);
    REQUIRE(wire::ValidLearningDefinitions(v, content));
    wire::Writer w; REQUIRE(w.View(v)); wire::Reader r{{w.bytes.data(), w.bytes.size()}}; WorldView decoded;
    REQUIRE(r.View(decoded)); CHECK(r.Done()); CHECK(decoded.learning == v.learning);
    CHECK(wire::ValidLearningDefinitions(decoded, content));
    for (std::size_t size = 0; size < w.bytes.size(); ++size) {
        wire::Reader truncated{{w.bytes.data(), size}}; CHECK_FALSE(truncated.View(decoded));
    }
    w.bytes.back() = 2; wire::Reader bad{{w.bytes.data(), w.bytes.size()}}; CHECK_FALSE(bad.View(decoded));
    SECTION("duplicate skills") { v.learning.skills.push_back(v.learning.skills[0]); }
    SECTION("duplicate books") { v.learning.books.push_back(v.learning.books[0]); }
    SECTION("duplicate recipes") { v.learning.recipes.push_back(v.learning.recipes[0]); }
    SECTION("level") { v.learning.skills[0].progress.level = 11; }
    SECTION("effective level") { v.learning.skills[0].effectiveLevel = -1; }
    SECTION("xp") { v.learning.skills[0].progress.xp = v.learning.skills[0].requiredXp; }
    SECTION("ratio") { v.learning.recipes[0].learningRatio = ratioScale + 1; }
    SECTION("active book missing") { v.learning.activeBook = {999}; }
    SECTION("used eligible book") { v.learning.books[0].used = true; }
    SECTION("unknown skill") { v.learning.skills[0].progress.skill = {999}; }
    SECTION("unknown recipe") { v.learning.recipes[0].recipe = {999}; }
    SECTION("work exceeds definition") { v.learning.books[0].completedWork = 251 * ratioScale; }
    SECTION("incorrect required XP") { ++v.learning.skills[0].requiredXp; }
    SECTION("combat multiplier") {
        v.learning.skills[0].progress.multiplier = 40001;
    }
    CHECK_FALSE(wire::ValidLearningDefinitions(v, content));
}

TEST_CASE("Empty learning views remain valid for legacy snapshots", "[skills][network][codec]") {
    const auto content = LearningCatalog(); auto v = LearningSnapshot(content); v.learning = {};
    wire::Writer w; REQUIRE(w.View(v)); wire::Reader r{{w.bytes.data(), w.bytes.size()}}; WorldView decoded;
    REQUIRE(r.View(decoded)); CHECK(r.Done()); CHECK(decoded.learning == LearningView{});
    CHECK(wire::ValidItemDefinitions(decoded, content));
}

TEST_CASE("Learning snapshot writers reject malformed progress before serialization", "[skills][network][codec]") {
    const auto content = LearningCatalog(); auto v = LearningSnapshot(content);
    SECTION("duplicate IDs") { v.learning.skills.push_back(v.learning.skills[0]); }
    SECTION("zero ID") { v.learning.recipes[0].recipe = {}; }
    SECTION("maximum level retains XP") { v.learning.skills[0].progress.level = maximumSkillLevel; }
    SECTION("work remainder") { v.learning.skills[0].progress.workRemainder = ratioScale; }
    SECTION("boost remainder") { v.learning.skills[0].progress.boostRemainder = ratioScale; }
    SECTION("direct XP exceeds earned XP") { ++v.learning.skills[0].progress.directBookXp; }
    SECTION("both reserve pools") { v.learning.skills[0].progress.practice = 1; v.learning.skills[0].progress.theory = 1; }
    SECTION("known flag disagrees with ratio") { v.learning.recipes[0].known = true; }
    SECTION("inactive study retains deadline") { v.learning.activeBook = {}; }
    wire::Writer w; CHECK_FALSE(w.View(v));
}

TEST_CASE("Learning fingerprints cover definitions carriers and recipe learning semantics", "[skills][network]") {
    const auto original = LearningCatalog(); const auto fingerprint = ContentFingerprint(original);
    String changed = LearningData;
    const auto replace = [&](const char* before, const char* after) {
        const auto position = changed.find(before); REQUIRE(position != String::npos);
        changed.replace(position, std::strlen(before), after);
    };
    SECTION("skill name") { replace("name:'Making'", "name:'Crafting'"); }
    SECTION("XP thresholds") { replace("[100,200,", "[101,200,"); }
    SECTION("book work") { replace("study_work:250", "study_work:251"); }
    SECTION("book direct XP") { replace("direct_xp:10", "direct_xp:11"); }
    SECTION("book theory") { replace("theory_xp:20", "theory_xp:21"); }
    SECTION("book multiplier") { replace("multiplier:11000", "multiplier:12000"); }
    SECTION("book range") { replace("last_level:5", "last_level:4"); }
    SECTION("book recipes") { replace("recipes:['make']", "recipes:[]"); }
    SECTION("carrier identities") { replace("books:['manual']", "books:['other']"); }
    SECTION("carrier power") { replace("reading_power:'battery',", ""); }
    SECTION("requirements") {
        replace("skills:[{skill:'making',level:1}]", "skills:[{skill:'making',level:2}]");
        replace("training:[{skill:'making',level:1", "training:[{skill:'making',level:2");
        replace("autolearn:[{skill:'making',level:1}]", "autolearn:[{skill:'making',level:2}]");
    }
    SECTION("training") { replace("xp_per_work:100", "xp_per_work:101"); }
    SECTION("known by default") { replace("known_by_default:false", "known_by_default:true"); }
    SECTION("autolearn") { replace("autolearn:[{skill:'making',level:1}]", "autolearn:[]"); }
    SECTION("craft learning count") { replace("crafts_to_learn:2", "crafts_to_learn:4"); }
    SECTION("disassembly learning count") { replace("disassemblies_to_learn:3", "disassemblies_to_learn:4"); }
    SECTION("recycler teaches") { replace("teaches_recipes:['make']", "teaches_recipes:[]"); }
    SECTION("recycler training") {
        replace("training:[{skill:'making',level:1,xp_per_work:100}],teaches_recipes", "training:[{skill:'making',level:1,xp_per_work:101}],teaches_recipes");
    }
    CHECK(ContentFingerprint(LearningCatalog({changed.data(), changed.size()})) != fingerprint);
    CHECK(ContentFingerprint(LearningCatalog()) == fingerprint);
}

TEST_CASE("Learning fingerprints distinguish skill categories with compatible book benefits", "[skills][network]") {
    // Multiplier-only benefits are valid for both categories, isolating the category change.
    String baseline = LearningData;
    const char* benefits = "direct_xp:10,theory_xp:20";
    const auto benefitPosition = baseline.find(benefits); REQUIRE(benefitPosition != String::npos);
    baseline.replace(benefitPosition, std::strlen(benefits), "direct_xp:0,theory_xp:0");
    const auto original = LearningCatalog({baseline.data(), baseline.size()});
    const auto fingerprint = ContentFingerprint(original);
    String changed = baseline;
    const char* category = "category:'noncombat'";
    const auto categoryPosition = changed.find(category); REQUIRE(categoryPosition != String::npos);
    changed.replace(categoryPosition, std::strlen(category), "category:'combat'");
    CHECK(ContentFingerprint(LearningCatalog({changed.data(), changed.size()})) != fingerprint);
    CHECK(ContentFingerprint(LearningCatalog({baseline.data(), baseline.size()})) == fingerprint);
}
}
