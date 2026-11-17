#include "core/server.h"
#include "skills/learning_system.h"

#include <catch2/catch_test_macros.hpp>

namespace {
using namespace cataclysmx;
ContentCatalog StudyContent() {
    const ContentSource source{"study-server.json5", R"({format_version:1,definitions:[
      {type:'terrain',id:'floor',name:'floor',movement:{passable:true},blocks_sight:false,appearance:{symbol:'.',foreground:'white'}},
      {type:'terrain',id:'wall',name:'wall',movement:{passable:false},blocks_sight:true,appearance:{symbol:'#',foreground:'white'}},
      {type:'terrain',id:'air',name:'air',movement:{passable:true},open_space:true,blocks_sight:false,appearance:{symbol:' ',foreground:'white'}},
      {type:'terrain',id:'closed_door',name:'door',movement:{passable:false},blocks_sight:true,appearance:{symbol:'+',foreground:'white'}},
      {type:'item',id:'rock',name:'rock',mass_g:10,volume_ml:20},
      {type:'item',id:'bandage',name:'bandage',mass_g:2,volume_ml:5},
      {type:'item',id:'backpack',name:'backpack',mass_g:10,volume_ml:10,capabilities:[{type:'container',id:'storage',capacity_volume_ml:100}]},
      {type:'skill',id:'making',name:'Making'},
      {type:'book',id:'manual',name:'Manual',study_work:250,benefits:[{skill:'making',first_level:1,last_level:1,direct_xp:10}]},
      {type:'item',id:'printed_manual',name:'Manual',mass_g:1,volume_ml:1,books:['manual']}
    ]})"};
    Diagnostics diagnostics;
    auto result = LoadContent({&source, 1}, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str()); REQUIRE(result); return eastl::move(*result);
}
struct StudyRig {
    ContentCatalog content = StudyContent();
    Server server{content};
    StudyRig() {
        auto pacing = server.GetPacing(); pacing.rateLimited = true; pacing.ticksPerSecond = 100;
        REQUIRE(server.SetPacing(pacing));
    }
    void AddBook() { REQUIRE(server.GetWorld().CreateItem(content.FindItem("printed_manual"), ItemLocation::Carried(server.GetPlayer()))); }
    Command Study() const { Command c; c.kind = CommandKind::study_book; c.book = content.FindBook("manual"); return c; }
};

TEST_CASE("Study commands require an exact argument envelope", "[skills][server]") {
    Command c; c.kind = CommandKind::study_book; CHECK_FALSE(ValidCommandShape(c)); c.book = {1};
    REQUIRE(ValidCommandShape(c));
    SECTION("direction") { c.dx = 1; }
    SECTION("vertical") { c.dy = 1; }
    SECTION("depth") { c.dz = 1; }
    SECTION("item") { c.item = 1; }
    SECTION("other item") { c.otherItem = 1; }
    SECTION("cost") { c.waitCost = 1; }
    SECTION("capability") { c.capability = {1}; }
    SECTION("action") { c.itemAction = "start"; }
    SECTION("target") { c.target = 1; }
    SECTION("style") { c.style = CombatStyle::Defensive; }
    SECTION("interaction") { c.interaction.object = 1; }
    SECTION("recipe") { c.recipe = {1}; }
    SECTION("source") { c.source.id = 1; }
    SECTION("destination") { c.destination.id = 1; }
    SECTION("pause with book") { c.kind = CommandKind::pause_study; }
    SECTION("book on wait") { c.kind = CommandKind::wait; }
    CHECK_FALSE(ValidCommandShape(c));
    Command pause; pause.kind = CommandKind::pause_study; CHECK(ValidCommandShape(pause));
}

TEST_CASE("Study pause interrupts funded work and clears buffered intent", "[skills][server]") {
    StudyRig r; r.AddBook();
    const auto start = r.server.Send(r.Study());
    REQUIRE(start.outcome == Outcome::study_started); CHECK(start.studyOutcome == StudyOutcome::Started);
    CHECK(start.advanced); CHECK(start.cost == 100); CHECK(start.tick == 0);
    r.server.Update(300000);
    const auto player = r.server.GetHostPlayerId(); auto context = r.server.GetInputContext(player);
    REQUIRE(r.server.Submit(player, {context.lastInputSequence + 1, context.opportunity, {}}).outcome == Outcome::queued);
    context = r.server.GetInputContext(player);
    Command pause; pause.kind = CommandKind::pause_study;
    const PlayerInput input{context.lastInputSequence + 1, context.opportunity, pause};
    const auto result = r.server.Submit(player, input);
    CHECK(result.outcome == Outcome::study_paused); CHECK(result.studyOutcome == StudyOutcome::PauseRequested);
    CHECK_FALSE(result.advanced); CHECK(result.cost == 0); CHECK(result.tick == 30);
    auto view = r.server.View(0); CHECK_FALSE(view.commandQueued);
    CHECK(view.opportunity == context.opportunity + 1); CHECK(view.learning.studyPauseRequested);
    CHECK(r.server.Submit(player, input).outcome == Outcome::stale_input);
    r.server.Update(700000); view = r.server.View(0);
    CHECK(view.tick == 100); CHECK(view.remainingTicks == 0);
    REQUIRE(view.learning.books.size() == 1); CHECK(view.learning.books[0].completedWork == 100 * ratioScale);
    REQUIRE(r.server.Send(r.Study()).outcome == Outcome::study_started);
    r.server.Update(1500000);
    const auto used = r.server.Send(r.Study());
    CHECK(used.outcome == Outcome::study_refused); CHECK(used.studyOutcome == StudyOutcome::AlreadyUsed);
    CHECK_FALSE(used.advanced); CHECK(used.cost == 0);
    view = r.server.View(0);
    REQUIRE(view.learning.skills.size() == 1); CHECK(view.learning.skills[0].progress.xp == 10 * ratioScale);
    CHECK(view.learning.books[0].used); CHECK(view.learning.activeBook == BookId{});
}

TEST_CASE("Study pause cannot interrupt unrelated funded actions", "[skills][server]") {
    StudyRig r; r.AddBook();
    REQUIRE(r.server.Send({}).outcome == Outcome::waited);
    const auto player = r.server.GetHostPlayerId(); const auto context = r.server.GetInputContext(player);
    Command pause; pause.kind = CommandKind::pause_study;
    const auto result = r.server.Submit(player, {context.lastInputSequence + 1, context.opportunity, pause});
    CHECK(result.outcome == Outcome::queued); CHECK_FALSE(result.advanced);
    CHECK(r.server.View(0).remainingTicks == 100); CHECK(r.server.View(0).commandQueued);
    CHECK_FALSE(r.server.GetWorld().Learning().Busy(r.server.GetPlayer()));
}

TEST_CASE("Every existing command rejects a book argument", "[skills][server]") {
    for (unsigned kind = 0; kind <= static_cast<unsigned>(CommandKind::transfer_item); ++kind) {
        Command command; command.kind = static_cast<CommandKind>(kind); command.book = {1};
        CHECK_FALSE(ValidCommandShape(command));
    }
}

TEST_CASE("Study refusals and repeated projections preserve authoritative learning state", "[skills][server]") {
    StudyRig r;
    auto command = r.Study();
    SECTION("missing reference") {
        const auto result = r.server.Send(command);
        CHECK(result.outcome == Outcome::study_refused); CHECK(result.studyOutcome == StudyOutcome::MissingReference);
    }
    SECTION("invalid catalog identity is checked by learning") {
        command.book = {UINT32_MAX};
        const auto result = r.server.Send(command); CHECK(result.outcome == Outcome::study_refused); CHECK_FALSE(result.advanced);
    }
    SECTION("outside teaching range") {
        r.AddBook(); REQUIRE(r.server.GetWorld().Learning().SetLevel(r.server.GetPlayer(), r.content.FindSkill("making"), 1));
        const auto result = r.server.Send(command);
        CHECK(result.outcome == Outcome::study_refused); CHECK(result.studyOutcome == StudyOutcome::OutsideTeachingRange);
    }
    const auto before = r.server.GetWorld().GetActor(r.server.GetPlayer())->learning;
    const auto revision = r.server.GetRevision();
    const auto first = r.server.View(0); const auto second = r.server.View(0);
    CHECK(first.learning == second.learning); CHECK(r.server.GetRevision() == revision);
    CHECK(r.server.GetWorld().GetActor(r.server.GetPlayer())->learning == before);
}
}
