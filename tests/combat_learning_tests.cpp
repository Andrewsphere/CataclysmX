#include "combat/combat_system.h"
#include "combat/combat_rules.h"
#include "core/world.h"
#include "skills/learning_system.h"
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <string>

namespace {
using namespace cataclysmx;
template<class T, class E> T Take(eastl::expected<T, E> value) { REQUIRE(value); return eastl::move(*value); }

ContentCatalog ManualContent(std::uint32_t chance) {
    Diagnostics diagnostics;
    auto loaded = LoadContentPack(CATACLYSMX_CONTENT_PACK, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str()); REQUIRE(loaded);
    auto content = eastl::move(*loaded);
    const auto art = content.FindCombatArt("boxing");
    const auto skill = content.FindSkill("fabrication");
    const auto bookId = content.FindBook("workshop_primer_text");
    REQUIRE(art.value); REQUIRE(skill.value); REQUIRE(bookId.value);
    // Configure our owned pack before World borrows it. Baseline-only fixture
    // catalogs intentionally contain no learned arts.
    auto& book = const_cast<BookDefinition&>(content.GetBook(bookId));
    book.studyWork = 250;
    book.recipes.clear();
    book.benefits.clear();
    SkillBookBenefit benefit;
    benefit.skill = skill; benefit.firstLevel = 1; benefit.lastLevel = 10; benefit.directXp = 1;
    book.benefits.push_back(benefit);
    book.artManuals = {{art, {skill, 1}, chance, 250}};
    return content;
}
struct Rig {
    ContentCatalog content;
    World world;
    ActorHandle actor{};
    ItemHandle copy{};
    LoadRequest residency{};
    ArtManualRequest request;
    std::uint32_t rng = 71;
    explicit Rig(std::uint32_t chance = ratioScale) : Rig(ManualContent(chance)) {}
    explicit Rig(ContentCatalog catalog) : content(eastl::move(catalog)), world(content),
        request{content.FindBook("workshop_primer_text"), content.FindCombatArt("boxing")} {
        residency = Take(world.RequestChunks({{0, 0}, 0}, LoadMode::active)); REQUIRE(world.ReconcileChunks());
        REQUIRE(world.SetTile({2, 2}, {content.FindTerrain("floor"), 0}));
        actor = Take(world.CreateActor({2, 2}, true));
        copy = Take(world.CreateItem(content.FindItem("workshop_primer_print"), ItemLocation::Carried(actor)));
        REQUIRE(world.Learning().SetLevel(actor, content.FindSkill("fabrication"), 1));
    }
    const ManualAttemptProgress& Attempt() const { return world.GetActor(actor)->characterCombat->manuals.front(); }
    void Start() { REQUIRE(world.Combat().StudyArt(actor, request).committed); }
    void Step(Tick elapsed) { world.ElapseTicks(elapsed); world.Combat().ProcessArtStudyEvents(rng); }
};

TEST_CASE("Art attempts pay every slice and roll only at a valid full completion", "[combat][learning]") {
    Rig r;
    const auto before = r.rng;
    CHECK(r.world.Combat().QueryArtStudy(r.actor, r.request).Available());
    CHECK(r.rng == before);
    r.Start();
    CHECK(r.Attempt().totalDuration == 250);
    CHECK(r.world.GetActor(r.actor)->turns == 1);
    r.Step(99); CHECK(r.rng == before); CHECK(r.Attempt().remaining == 151);
    r.Step(1); CHECK(r.rng == before); CHECK(r.world.GetActor(r.actor)->turns == 2);
    CHECK(r.world.Learning().Busy(r.actor));
    CHECK(Take(r.world.Learning().Study(r.actor, r.request.book)).outcome == StudyOutcome::ActorBusy);
    r.Step(100); r.world.ElapseTicks(50);
    CHECK(r.Attempt().remaining == 0); CHECK(r.Attempt().fundedTicks == 0);
    CHECK(r.world.Learning().Busy(r.actor));
    CHECK_FALSE(r.world.ScheduleActorAction(r.actor));
    CHECK(r.rng == before);
    r.world.Combat().ProcessArtStudyEvents(r.rng);
    CHECK(r.rng != before); CHECK_FALSE(r.world.Learning().Busy(r.actor));
    CHECK(r.world.GetActor(r.actor)->turns == 3);
    Vector<CombatEvent> events; r.world.Combat().DrainEvents(events);
    REQUIRE(events.size() == 1); CHECK(events[0].learnedArt); CHECK(events[0].cost == 250);
    CHECK(r.world.Combat().StudyArt(r.actor, r.request).reason == CombatRefusal::AlreadyKnown);
    auto& known = *const_cast<Actor*>(r.world.GetActor(r.actor))->characterCombat;
    for (auto& progress : known.arts) { if (progress.art == r.request.art) { progress.xp = 12345; } }
    const auto snapshot = CloneCharacterCombat(r.world.GetActor(r.actor)->characterCombat.get());
    REQUIRE(r.world.Combat().GrantArt(r.actor, r.request.art, ArtGrantReason::Teaching));
    CHECK(EqualCharacterCombat(snapshot.get(), r.world.GetActor(r.actor)->characterCombat.get()));
    const auto& grants = r.world.GetActor(r.actor)->characterCombat->abilityGrants;
    for (std::size_t i = 0; i < grants.size(); ++i) {
        for (std::size_t j = i + 1; j < grants.size(); ++j) { CHECK_FALSE(grants[i] == grants[j]); }
    }
    r.world.Combat().ProcessArtStudyEvents(r.rng);
    r.world.Combat().DrainEvents(events); CHECK(events.empty());
    CHECK_FALSE(FindBookProgress(r.world.GetActor(r.actor)->learning, r.request.book));
}

TEST_CASE("Art cancellation and reference loss retain paid progress without inspection rolls", "[combat][learning]") {
    Rig r; r.Start(); r.Step(30);
    REQUIRE(r.world.Combat().PauseArtStudy(r.actor));
    CHECK(r.world.Learning().Busy(r.actor)); r.Step(70);
    CHECK_FALSE(r.world.Learning().Busy(r.actor)); CHECK(r.Attempt().remaining == 150); CHECK(r.rng == 71);
    r.Step(1000); CHECK(r.Attempt().remaining == 150);
    REQUIRE(r.world.DestroyItem(r.copy));
    CHECK(r.world.Combat().StudyArt(r.actor, r.request).reason == CombatRefusal::MissingReference);
    r.copy = Take(r.world.CreateItem(r.content.FindItem("workshop_primer_print"), ItemLocation::Carried(r.actor)));
    r.Start(); REQUIRE(r.world.DestroyItem(r.copy)); r.Step(100);
    CHECK_FALSE(r.Attempt().active); CHECK(r.Attempt().remaining == 50); CHECK(r.rng == 71);
    r.copy = Take(r.world.CreateItem(r.content.FindItem("workshop_primer_print"), ItemLocation::Carried(r.actor)));
    r.Start(); REQUIRE(r.world.DestroyItem(r.copy)); r.Step(50);
    CHECK_FALSE(r.Attempt().active); CHECK(r.Attempt().remaining == 0); CHECK(r.rng == 71);
    r.copy = Take(r.world.CreateItem(r.content.FindItem("workshop_primer_print"), ItemLocation::Carried(r.actor)));
    const auto turns = r.world.GetActor(r.actor)->turns;
    r.Start(); r.world.Combat().ProcessArtStudyEvents(r.rng);
    CHECK(r.world.GetActor(r.actor)->turns == turns); CHECK(r.rng != 71);
}

TEST_CASE("Failed art attempts allow a fresh fully funded retry", "[combat][learning]") {
    Rig r{0}; r.Start(); r.Step(100); r.Step(100); r.Step(50);
    CHECK_FALSE(r.Attempt().active); CHECK(r.Attempt().totalDuration == 0);
    CHECK(r.world.Combat().QueryArtStudy(r.actor, r.request).Available());
    Vector<CombatEvent> events; r.world.Combat().DrainEvents(events);
    REQUIRE(events.size() == 1); CHECK_FALSE(events[0].learnedArt); CHECK(events[0].cost == 250);
    r.Start(); CHECK(r.Attempt().remaining == 250); CHECK(r.Attempt().successChance == 0);
    CHECK_FALSE(FindBookProgress(r.world.GetActor(r.actor)->learning, r.request.book));
}

TEST_CASE("Ordinary book benefits and art attempts are independent", "[combat][learning]") {
    Rig r;
    REQUIRE(Take(r.world.Learning().Study(r.actor, r.request.book)).outcome == StudyOutcome::Started);
    CHECK(r.world.Combat().StudyArt(r.actor, r.request).reason == CombatRefusal::Busy);
    for (const Tick elapsed : {Tick{100}, Tick{100}, Tick{50}}) {
        r.world.ElapseTicks(elapsed); r.world.Learning().ProcessEvents();
    }
    REQUIRE(FindBookProgress(r.world.GetActor(r.actor)->learning, r.request.book)->used);
    r.Start(); r.Step(100); r.Step(100); r.Step(50);
    CHECK(r.world.Combat().QueryArtStudy(r.actor, r.request).reason == CombatRefusal::AlreadyKnown);
    CHECK(FindBookProgress(r.world.GetActor(r.actor)->learning, r.request.book)->used);
}

TEST_CASE("Loss of learned prerequisite prevents a paid completion roll", "[combat][learning]") {
    Rig r; r.Start(); r.Step(100); r.Step(100);
    REQUIRE(r.world.Learning().SetLevel(r.actor, r.content.FindSkill("fabrication"), 0));
    r.Step(50); CHECK(r.Attempt().remaining == 0); CHECK_FALSE(r.Attempt().active); CHECK(r.rng == 71);
    REQUIRE(r.world.Learning().SetLevel(r.actor, r.content.FindSkill("fabrication"), 1));
    const auto turns = r.world.GetActor(r.actor)->turns;
    r.Start(); r.world.Combat().ProcessArtStudyEvents(r.rng);
    CHECK(r.world.GetActor(r.actor)->turns == turns); CHECK(r.rng != 71);
}

TEST_CASE("Art study uses learned prerequisites and snapshots duration", "[combat][learning]") {
    Rig r;
    REQUIRE(r.world.Learning().SetLevel(r.actor, r.content.FindSkill("fabrication"), 0));
    const auto& learning = r.world.GetActor(r.actor)->learning;
    CHECK(EffectiveSkillLevel(learning, r.content.FindSkill("fabrication"), 10) == 10);
    CHECK(r.world.Combat().StudyArt(r.actor, r.request).reason == CombatRefusal::Locked);
    REQUIRE(r.world.Learning().SetLevel(r.actor, r.content.FindSkill("fabrication"), 3));
    r.Start(); CHECK(r.Attempt().totalDuration == ArtStudyDuration(250, 2));
    REQUIRE(r.world.Learning().SetLevel(r.actor, r.content.FindSkill("fabrication"), 10));
    CHECK(r.Attempt().totalDuration == ArtStudyDuration(250, 2));
}

TEST_CASE("Parked art study freezes and dead actors cannot complete it", "[combat][learning]") {
    Rig r; r.Start(); r.Step(30);
    const auto frozen = CloneCharacterCombat(r.world.GetActor(r.actor)->characterCombat.get());
    REQUIRE(r.world.SetActorParked(r.actor, true)); r.Step(1000);
    CHECK(EqualCharacterCombat(frozen.get(), r.world.GetActor(r.actor)->characterCombat.get()));
    CHECK(r.Attempt().remaining == 220); CHECK(r.Attempt().fundedTicks == 70);
    CHECK_FALSE(r.world.Combat().NextArtStudyEventIn());
    REQUIRE(r.world.SetActorParked(r.actor, false));
    REQUIRE(r.world.Combat().NextArtStudyEventIn());
    CHECK(*r.world.Combat().NextArtStudyEventIn() == 70);
    r.world.MarkActorDead(r.actor); r.Step(1000);
    CHECK(r.rng == 71); CHECK_FALSE(r.world.Learning().Busy(r.actor));
    CHECK(r.world.GetActor(r.actor)->characterCombat->manuals.empty());
    CHECK(r.world.GetActor(r.actor)->actionDeadline.suspended);
    CHECK(r.world.GetActorRemainingTicks(r.actor) == 0);
    CHECK_FALSE(r.world.Combat().NextArtStudyEventIn());
    CHECK(r.world.Combat().StudyArt(r.actor, r.request).reason == CombatRefusal::Dead);
    CHECK(Take(r.world.Learning().Study(r.actor, r.request.book)).outcome == StudyOutcome::ActorUnavailable);
}

TEST_CASE("Resident inactive art study preserves its funded slice", "[combat][learning]") {
    Rig r; r.Start(); r.Step(37);
    const auto frozen = CloneCharacterCombat(r.world.GetActor(r.actor)->characterCombat.get());
    Take(r.world.RequestChunks({{0, 0}, 0}, LoadMode::resident));
    REQUIRE(r.world.ReleaseRequest(r.residency)); REQUIRE(r.world.ReconcileChunks());
    r.Step(1000);
    CHECK(EqualCharacterCombat(frozen.get(), r.world.GetActor(r.actor)->characterCombat.get()));
    CHECK_FALSE(r.world.Combat().NextArtStudyEventIn()); CHECK(r.rng == 71);
    r.residency = Take(r.world.RequestChunks({{0, 0}, 0}, LoadMode::active));
    REQUIRE(r.world.ReconcileChunks());
    REQUIRE(r.world.Combat().NextArtStudyEventIn());
    CHECK(*r.world.Combat().NextArtStudyEventIn() == 63);
    r.Step(63); CHECK(r.Attempt().remaining == 150); CHECK(r.Attempt().fundedTicks == 100);
}

TEST_CASE("Parking a fully paid art attempt defers its roll until reactivation", "[combat][learning]") {
    Rig r; r.Start(); r.Step(100); r.Step(100); r.world.ElapseTicks(50);
    REQUIRE(r.Attempt().remaining == 0); REQUIRE(r.Attempt().fundedTicks == 0);
    REQUIRE(r.world.SetActorParked(r.actor, true)); r.Step(1000);
    CHECK(r.Attempt().active); CHECK(r.rng == 71); CHECK_FALSE(r.world.Combat().NextArtStudyEventIn());
    REQUIRE(r.world.SetActorParked(r.actor, false));
    REQUIRE(r.world.Combat().NextArtStudyEventIn()); CHECK(*r.world.Combat().NextArtStudyEventIn() == 0);
    r.world.Combat().ProcessArtStudyEvents(r.rng);
    CHECK(r.rng != 71); CHECK(r.world.GetActor(r.actor)->turns == 3);
    CHECK(r.world.Combat().QueryArtStudy(r.actor, r.request).reason == CombatRefusal::AlreadyKnown);
}

TEST_CASE("Explicit art grants preserve source identity and enforce learned and XP gates", "[combat][learning]") {
    auto content = ManualContent(ratioScale);
    const auto boxing = content.FindCombatArt("boxing");
    const auto forms = content.FindCombatArt("weapon_forms");
    const auto focus = content.FindAbility("focus");
    const auto unarmed = content.GetCombatSkills().unarmed;
    auto& boxingDefinition = const_cast<CombatArtDefinition&>(content.GetCombatArt(boxing));
    ArtAbilityGrant gated{focus}; gated.unlock.artXp = 100; gated.unlock.skills = {{unarmed, 1}};
    boxingDefinition.abilities = {gated};
    auto& formsDefinition = const_cast<CombatArtDefinition&>(content.GetCombatArt(forms));
    formsDefinition.abilities = {{focus}};
    Rig r(eastl::move(content));
    const auto initialStyle = r.world.GetActor(r.actor)->combat.styleId;
    REQUIRE(r.world.Combat().GrantArt(r.actor, boxing, ArtGrantReason::Teaching));
    auto& state = *const_cast<Actor*>(r.world.GetActor(r.actor))->characterCombat;
    REQUIRE(state.arts.size() == 1); CHECK(state.arts.front().art == boxing);
    CHECK(state.abilityGrants.empty()); CHECK(r.world.GetActor(r.actor)->combat.styleId == initialStyle);
    // Arrange existing XP and an independent innate grant/cooldown as actor state.
    state.arts.front().xp = 100;
    const AbilityGrant innate{focus, {AbilityGrantSourceKind::Innate, {}}};
    state.abilityGrants.push_back(innate);
    state.abilityCooldowns.push_back({focus, innate.source, 42});
    REQUIRE(r.world.Combat().GrantArt(r.actor, boxing, ArtGrantReason::Teaching));
    REQUIRE(state.abilityGrants.size() == 1); CHECK(state.abilityGrants.front() == innate);
    REQUIRE(r.world.Learning().SetLevel(r.actor, unarmed, 1));
    REQUIRE(r.world.Combat().GrantArt(r.actor, forms, ArtGrantReason::Administration));
    REQUIRE(r.world.Combat().GrantArt(r.actor, boxing, ArtGrantReason::Teaching));
    CHECK(state.arts.size() == 2); CHECK(state.arts.front().art == boxing); CHECK(state.arts.front().xp == 100);
    REQUIRE(state.abilityGrants.size() == 3);
    CHECK(state.abilityGrants[0] == innate);
    CHECK(state.abilityGrants[1] == AbilityGrant{focus, {AbilityGrantSourceKind::Art, boxing}});
    CHECK(state.abilityGrants[2] == AbilityGrant{focus, {AbilityGrantSourceKind::Art, forms}});
    REQUIRE(state.abilityCooldowns.size() == 1); CHECK(state.abilityCooldowns.front().remaining == 42);
    CHECK(r.world.GetActor(r.actor)->combat.styleId == initialStyle);
}

TEST_CASE("Paid art slices and pause requests survive a save", "[combat][learning][save]") {
    const auto content = ManualContent(ratioScale);
    const auto directory = std::filesystem::temp_directory_path() / ("cataclysmx-art-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    REQUIRE(std::filesystem::create_directory(directory));
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code e; std::filesystem::remove_all(path, e); } } cleanup{directory};
    const auto path = (directory / "world.sqlite").string();
    PersistentId id = 0;
    const ArtManualRequest request{content.FindBook("workshop_primer_text"), content.FindCombatArt("boxing")};
    {
        World world(content);
        Take(world.RequestChunks({{0, 0}, 0}, LoadMode::active)); REQUIRE(world.ReconcileChunks());
        REQUIRE(world.SetTile({2, 2}, {content.FindTerrain("floor"), 0}));
        const auto actor = Take(world.CreateActor({2, 2}, true)); id = world.GetActor(actor)->id;
        Take(world.CreateItem(content.FindItem("workshop_primer_print"), ItemLocation::Carried(actor)));
        REQUIRE(world.Learning().SetLevel(actor, content.FindSkill("fabrication"), 1));
        REQUIRE(world.Combat().StudyArt(actor, request).committed);
        world.ElapseTicks(37); REQUIRE(world.Combat().PauseArtStudy(actor));
        SessionState session; session.player = id; session.playerPosition = {2, 2};
        REQUIRE(world.CreateSave(path.c_str(), session));
    }
    World restored(content); Take(restored.LoadSave(path.c_str()));
    Take(restored.RequestChunks({{0, 0}, 0}, LoadMode::active)); REQUIRE(restored.ReconcileChunks());
    const auto actor = restored.FindActor(id); REQUIRE(actor.value);
    const auto& attempt = restored.GetActor(actor)->characterCombat->manuals.front();
    CHECK(attempt.remaining == 213); CHECK(attempt.fundedTicks == 63); CHECK(attempt.pauseRequested);
    std::uint32_t rng = 71; restored.ElapseTicks(63); restored.Combat().ProcessArtStudyEvents(rng);
    CHECK_FALSE(attempt.active); CHECK(attempt.remaining == 150); CHECK(rng == 71);
    REQUIRE(restored.Combat().StudyArt(actor, request).committed);
    restored.ElapseTicks(100); restored.Combat().ProcessArtStudyEvents(rng);
    restored.ElapseTicks(50); restored.Combat().ProcessArtStudyEvents(rng);
    CHECK(restored.Combat().QueryArtStudy(actor, request).reason == CombatRefusal::AlreadyKnown);
}
}
