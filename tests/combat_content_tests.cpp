#include "combat/combat_types.h"
#include "content/content.h"
#include "network/network.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <initializer_list>

namespace {
using namespace cataclysmx;

eastl::string_view View(const String& value) { return {value.data(), value.size()}; }
String ReadCombatFixture(const char* relative) {
    String path = CATACLYSMX_CONTENT_PACK;
    path.resize(path.find_last_of("/\\") + 1);
    path += relative;
    std::FILE* file = nullptr;
#ifdef _MSC_VER
    fopen_s(&file, path.c_str(), "rb");
#else
    file = std::fopen(path.c_str(), "rb");
#endif
    REQUIRE(file != nullptr);
    String out; char buffer[4096];
    for (;;) {
        const auto count = std::fread(buffer, 1, sizeof(buffer), file);
        out.append(buffer, count);
        if (!count) { break; }
    }
    CHECK(std::ferror(file) == 0); CHECK(std::fclose(file) == 0);
    return out;
}
ContentResult<ContentCatalog> LoadFixture(eastl::string_view definitions, Diagnostics* diagnostics = nullptr) {
    String document = "{format_version:1,definitions:[";
    document.append(definitions.data(), definitions.size()); document += "]}";
    const ContentSource source{"fixture.json5", View(document)};
    return LoadContent({&source, 1}, diagnostics);
}
ContentResult<ContentCatalog> LoadCombat(eastl::string_view extra, eastl::string_view from = {}, eastl::string_view to = {}, Diagnostics* diagnostics = nullptr) {
    auto combat = ReadCombatFixture("combat/core.json5");
    const auto skills = ReadCombatFixture("skills/core.json5");
    if (!from.empty()) {
        const auto position = combat.find(from.data(), 0, from.size());
        REQUIRE(position != String::npos);
        combat.replace(position, from.size(), to.data(), to.size());
    }
    String document = "{format_version:1,definitions:["; document.append(extra.data(), extra.size()); document += "]}";
    const ContentSource sources[] = {{"combat.json5", View(combat)}, {"skills.json5", View(skills)}, {"extra.json5", View(document)}};
    return LoadContent({sources, 3}, diagnostics);
}

TEST_CASE("Skill ceilings and active curves participate in content fingerprints", "[content][combat-content][skill-content]") {
    const auto implicit = LoadFixture("{type:'skill',id:'limited',name:'Limited',max_level:5}");
    const auto explicitCurve = LoadFixture("{type:'skill',id:'limited',name:'Limited',xp_to_next:[100,200,400,800,1200],max_level:5}");
    const auto higher = LoadFixture("{type:'skill',id:'limited',name:'Limited',max_level:6}");
    const auto changed = LoadFixture("{type:'skill',id:'limited',name:'Limited',max_level:5,xp_to_next:[100,200,400,800,1201]}");
    REQUIRE(implicit); REQUIRE(explicitCurve); REQUIRE(higher); REQUIRE(changed);
    CHECK(network::ContentFingerprint(*implicit) == network::ContentFingerprint(*explicitCurve));
    CHECK(network::ContentFingerprint(*implicit) != network::ContentFingerprint(*higher));
    CHECK(network::ContentFingerprint(*implicit) != network::ContentFingerprint(*changed));
    const auto defaultMaximum = LoadFixture("{type:'skill',id:'limited',name:'Limited'}");
    const auto explicitMaximum = LoadFixture("{type:'skill',id:'limited',name:'Limited',max_level:10}");
    REQUIRE(defaultMaximum); REQUIRE(explicitMaximum);
    CHECK(network::ContentFingerprint(*defaultMaximum) == network::ContentFingerprint(*explicitMaximum));
}

TEST_CASE("Combat unlocks and art manuals enforce forward referenced skill ceilings", "[content][combat-content][skill-content]") {
    constexpr auto skill = "{type:'skill',id:'limited',name:'Limited',category:'combat',max_level:5}";
    struct Example { const char* definition; const char* path; };
    for (const auto& example : {
        Example{"{type:'combat_move',id:'limited_move',name:'Move',requirements:{primary:{kind:'empty_hand'}},strikes:[{}],unlock:{skills:[{skill:'limited',level:6}]}}",
                "definitions[0].unlock.skills[0].level"},
        Example{"{type:'book',id:'limited_manual',name:'Manual',study_work:1,art_manuals:[{art:'boxing',skill:'limited',level:6,success_chance:10000,base_duration:1}]}",
                "definitions[0].art_manuals[0].level"}
    }) {
        String extra = example.definition;
        extra += ',';
        extra += skill;
        Diagnostics diagnostics;
        const auto loaded = LoadCombat(View(extra), {}, {}, &diagnostics);
        REQUIRE_FALSE(loaded);
        REQUIRE_FALSE(diagnostics.Empty());
        REQUIRE(diagnostics.Frames()[0].location);
        CHECK(diagnostics.Frames()[0].location->path == example.path);
        CHECK(diagnostics.Frames()[0].message.find("maximum 5") != String::npos);
        CHECK_FALSE(LoadCombat(View(extra)));
    }
    Diagnostics diagnostics;
    const auto artGrant = LoadCombat(skill,
        "ability:'leap',unlock:{skills:[{skill:'unarmed',level:2}]",
        "ability:'leap',unlock:{skills:[{skill:'limited',level:6}]", &diagnostics);
    REQUIRE_FALSE(artGrant);
    REQUIRE_FALSE(diagnostics.Empty());
    CHECK(diagnostics.Frames()[0].message.find("maximum 5") != String::npos);
    CHECK(diagnostics.Frames()[0].location->path.find(".abilities[1].unlock.skills[0].level") != String::npos);
    for (const char* level : {"0", "5"}) {
        String extra = "{type:'combat_move',id:'limited_move',name:'Move',requirements:{primary:{kind:'empty_hand'}},strikes:[{}],unlock:{skills:[{skill:'limited',level:";
        extra += level;
        extra += "}]}},{type:'book',id:'limited_manual',name:'Manual',study_work:1,art_manuals:[{art:'boxing',skill:'limited',level:";
        extra += level;
        extra += ",success_chance:10000,base_duration:1}]},";
        extra += skill;
        CHECK(LoadCombat(View(extra)));
    }
}

TEST_CASE("Combat fixture baseline is explicit and does not add skills", "[content][combat-content]") {
    const auto result = LoadFixture("");
    REQUIRE(result);
    CHECK(result->SkillCount() == 0);
    CHECK(result->GetCombatSkills() == CombatSkillIds{});
    CHECK(result->CombatMoveCount() == 15);
    CHECK(result->CombatStyleCount() == 3);
    CHECK(result->CombatArtCount() == 0);
    CHECK(result->AbilityCount() == 0);
    for (const char* style : {"basic_balanced", "basic_aggressive", "basic_defensive"}) {
        const auto id = result->FindCombatStyle(style);
        REQUIRE(id.value != 0);
        CHECK(result->GetCombatStyle(id).moves.size() == 15);
    }
    CHECK(result->FindCombatMove("missing") == CombatMoveId{});
    CHECK(result->TryGetCombatMove({}) == nullptr);
    CHECK(result->TryGetCombatStyle({UINT32_MAX}) == nullptr);
    CHECK(result->TryGetCombatArt({1}) == nullptr);
    CHECK(result->TryGetAbility({1}) == nullptr);
    const auto& corpse = result->GetItem(result->CorpseItem());
    CHECK(corpse.id == "combat_corpse");
    REQUIRE(corpse.container);
    CHECK(corpse.container->config.capacityVolumeMl == UINT32_MAX);
    CHECK_FALSE(corpse.unfinishedCraft);
    CHECK_FALSE(LoadFixture("{type:'item',id:'combat_corpse',name:'fake',mass_g:0,volume_ml:1}"));
    // A single combat category must never activate fixture fallback.
    CHECK_FALSE(LoadFixture("{type:'combat_style',id:'partial',name:'partial',moves:[]}"));
}

TEST_CASE("Natural sources distinguish apparatus attacks from ordinary and exhausted bites", "[content][combat-content]") {
    const auto fixture = LoadFixture(""); REQUIRE(fixture);
    const auto shipped = LoadCombat(""); REQUIRE(shipped);
    for (const auto* catalog : {&*fixture, &*shipped}) {
        const auto& native = catalog->GetCombatMove(catalog->FindCombatMove("basic_natural"));
        const auto& bite = catalog->GetCombatMove(catalog->FindCombatMove("basic_bite"));
        const auto& exhausted = catalog->GetCombatMove(catalog->FindCombatMove("basic_exhausted_natural"));
        CHECK(native.requirements.primary.requiresNaturalApparatus);
        CHECK_FALSE(bite.requirements.primary.requiresNaturalApparatus);
        CHECK(bite.trigger == CombatTrigger::Attack);
        CHECK_FALSE(bite.requirements.exhaustedOnly);
        CHECK(bite.cost.staminaCost > 0);
        REQUIRE(bite.strikes[0].damage.size() == 1);
        CHECK(bite.strikes[0].damage[0] == DamagePacket{DamageType::Piercing, 700, 0});
        CHECK_FALSE(exhausted.requirements.primary.requiresNaturalApparatus);
        CHECK(exhausted.requirements.exhaustedOnly);
        CHECK(exhausted.cost.staminaCost == 0);
        CHECK(exhausted.cooldown == 0);
        REQUIRE(exhausted.strikes[0].damage.size() == 1);
        CHECK(exhausted.strikes[0].damage[0] == DamagePacket{DamageType::Piercing, 700, 0});
        CHECK(exhausted.strikes[0].damageRatio == 5000);
        for (const char* name : {"basic_balanced", "basic_aggressive", "basic_defensive"}) {
            bool ordinaryIncluded = false, fallbackIncluded = false;
            for (const auto& weighted : catalog->GetCombatStyle(catalog->FindCombatStyle(name)).moves) {
                ordinaryIncluded |= weighted.move == catalog->FindCombatMove("basic_bite") && weighted.weight > 0;
                fallbackIncluded |= weighted.move == catalog->FindCombatMove("basic_exhausted_natural") && weighted.weight > 0;
            }
            CHECK(ordinaryIncluded);
            CHECK(fallbackIncluded);
        }
    }
    const auto implicit = LoadCombat("{type:'combat_move',id:'custom_natural',name:'Natural',requirements:{primary:{kind:'natural'}},strikes:[{}]}");
    const auto explicitTrue = LoadCombat("{type:'combat_move',id:'custom_natural',name:'Natural',requirements:{primary:{kind:'natural',requires_natural_apparatus:true}},strikes:[{}]}");
    const auto explicitFalse = LoadCombat("{type:'combat_move',id:'custom_natural',name:'Natural',requirements:{primary:{kind:'natural',requires_natural_apparatus:false}},strikes:[{}]}");
    REQUIRE(implicit); REQUIRE(explicitTrue); REQUIRE(explicitFalse);
    CHECK(implicit->GetCombatMove(implicit->FindCombatMove("custom_natural")).requirements.primary.requiresNaturalApparatus);
    CHECK(WriteCombatContent(*implicit) == WriteCombatContent(*explicitTrue));
    CHECK_FALSE(WriteCombatContent(*implicit) == WriteCombatContent(*explicitFalse));
}

TEST_CASE("Attack handling exclusions are validated and fingerprinted as a set", "[content][combat-content][shield-unarmed]") {
    constexpr auto field = "exclude_attack_handling_skills:['shields']";
    const auto baseline = LoadCombat(""); REQUIRE(baseline);
    const auto cleared = LoadCombat("", field, "exclude_attack_handling_skills:[]"); REQUIRE(cleared);
    CHECK(network::ContentFingerprint(*baseline) != network::ContentFingerprint(*cleared));
    const auto a = LoadCombat("", field, "exclude_attack_handling_skills:['shields','axes']"); REQUIRE(a);
    const auto b = LoadCombat("", field, "exclude_attack_handling_skills:['axes','shields']"); REQUIRE(b);
    CHECK(network::ContentFingerprint(*a) == network::ContentFingerprint(*b));
    for (const auto* invalid : {"null", "'shields'", "[0]", "['missing']", "['fabrication']", "['shields','shields']"}) {
        INFO(invalid);
        String replacement = "exclude_attack_handling_skills:"; replacement += invalid;
        CHECK_FALSE(LoadCombat("", field, View(replacement)));
    }
    const auto skills = ReadCombatFixture("skills/core.json5");
    const ContentSource source{"skills.json5", View(skills)};
    const auto fallback = LoadContent({&source, 1}); REQUIRE(fallback);
    for (const auto* catalog : {&*baseline, &*fallback}) {
        for (const auto* name : {"basic_balanced", "basic_aggressive", "basic_defensive"}) {
            const auto& style = catalog->GetCombatStyle(catalog->FindCombatStyle(name));
            REQUIRE(style.excludedAttackHandlingSkills.size() == 1);
            CHECK(style.excludedAttackHandlingSkills[0] == catalog->GetCombatSkills().shields);
        }
    }
}

TEST_CASE("Shipped unarmed basics trail household weapons while boxing retains its strikes", "[content][combat-content][shield-unarmed]") {
    const auto shipped = LoadContentPack(CATACLYSMX_CONTENT_PACK); REQUIRE(shipped);
    const auto fallback = LoadFixture(""); REQUIRE(fallback);
    const auto move = [&](const char* name) -> const CombatMoveDefinition& {
        return shipped->GetCombatMove(shipped->FindCombatMove(name));
    };
    const auto& knife = *shipped->GetItem(shipped->FindItem("chef_knife")).melee;
    const auto& hammer = *shipped->GetItem(shipped->FindItem("claw_hammer")).melee;
    const auto unarmedDamage = [](const CombatMoveDefinition& attack) {
        return MakeCombatState(true).attackDamage * body_hp_scale * attack.strikes[0].damageRatio / ratioScale;
    };
    for (const auto* name : {"basic_fist", "basic_kick", "basic_exhausted_fist", "basic_exhausted_kick"}) {
        INFO(name);
        const auto& attack = move(name);
        CHECK(attack.strikes[0].damageRatio == fallback->GetCombatMove(fallback->FindCombatMove(name)).strikes[0].damageRatio);
        const auto& weapon = move(attack.requirements.exhaustedOnly ? "basic_exhausted_weapon" : "basic_weapon");
        for (const auto* tool : {&knife, &hammer}) {
            const auto weaponDamage = tool->damage[0].amount * weapon.strikes[0].damageRatio / ratioScale;
            // Compare neutral-grip damage per recovery tick at equal training.
            CHECK(std::uint64_t{unarmedDamage(attack)} * weapon.cost.recoveryCost * tool->attackCost * 4
                <= std::uint64_t{weaponDamage} * attack.cost.recoveryCost * 100 * 3);
        }
    }
    CHECK(unarmedDamage(move("boxing_jab")) > unarmedDamage(move("basic_kick")));
    CHECK(unarmedDamage(move("boxing_cross")) > unarmedDamage(move("boxing_jab")));
    CHECK(move("boxing_jab").unlock == MoveUnlock{});
    CHECK(move("boxing_cross").unlock.artXp > 0);
    CHECK(move("basic_natural").strikes[0].damageRatio == ratioScale);
}

TEST_CASE("Combat catalogs resolve moves styles arts abilities manuals and handling families", "[content][combat-content]") {
    const auto result = LoadCombat(R"(
        {type:'book',id:'manual',name:'Manual',study_work:1,
         art_manuals:[{art:'boxing',skill:'unarmed',level:0,success_chance:7000,base_duration:600}],
         benefits:[{skill:'melee',first_level:1,last_level:3,multiplier:11000}]},
        {type:'item',id:'knuckles',name:'Knuckles',mass_g:1,volume_ml:1,
         melee:{handling_skill:'unarmed',grips:[{kind:'one_handed',damage_ratio:11000}],damage:[{type:'blunt',amount:600}]}},
        {type:'item',id:'legacy',name:'Legacy',mass_g:1,volume_ml:1,melee:{hands:2,damage:[{type:'blunt',amount:100}]}},
        {type:'item',id:'coat',name:'Coat',mass_g:1,volume_ml:1,
         armor:{layer:'outer',regions:[{region:'torso',coverage:8000,encumbrance:5}],rigid_regions:['torso'],protection:{blunt:200}}}
    )");
    REQUIRE(result);
    CHECK(result->CombatMoveCount() == 20);
    CHECK(result->CombatStyleCount() == 5);
    CHECK(result->CombatArtCount() == 2);
    CHECK(result->AbilityCount() == 3);
    const auto skills = result->GetCombatSkills();
    CHECK(skills.shields == result->FindSkill("shields"));
    const auto& knuckles = *result->GetItem(result->FindItem("knuckles")).melee;
    CHECK(knuckles.handlingSkill == skills.unarmed);
    REQUIRE(knuckles.grips.size() == 1);
    CHECK(knuckles.grips[0].damageRatio == 11000);
    const auto& legacy = *result->GetItem(result->FindItem("legacy")).melee;
    CHECK(legacy.handlingSkill == skills.melee);
    REQUIRE(legacy.grips.size() == 1);
    CHECK(legacy.grips[0].kind == GripKind::TwoHanded);
    const auto& manual = result->GetBook(result->FindBook("manual"));
    REQUIRE(manual.artManuals.size() == 1);
    CHECK(manual.artManuals[0].art == result->FindCombatArt("boxing"));
    CHECK(manual.benefits.size() == 1);
    const auto& armor = *result->GetItem(result->FindItem("coat")).armor;
    CHECK(armor.layer == ArmorLayer::Outer);
    CHECK(armor.regions[0].coverage == 8000);
    const auto& sweep = result->GetAbility(result->FindAbility("sweep"));
    CHECK(sweep.sweepMove == result->FindCombatMove("weapon_sweep_strike"));
    const auto& advanced = result->GetCombatMove(result->FindCombatMove("boxing_cross"));
    CHECK(advanced.unlock.artXp == 50000);
    CHECK(advanced.unlock.skills[0].level == 2);
}

TEST_CASE("Combat strict schemas reject malformed references weights effects and sources", "[content][combat-content]") {
    struct Mutation { const char* from; const char* to; };
    for (const auto& mutation : {
            Mutation{"name:'Weapon strike'", "name:'Weapon strike',typo:1"},
            Mutation{"name:'Weapon strike'", "name:'Weapon strike',name:'duplicate'"},
            Mutation{"id:'boxing_cross'", "id:'boxing_jab'"},
            Mutation{"move:'basic_weapon',weight:40", "move:'missing',weight:40"},
            Mutation{"move:'basic_weapon',weight:40", "move:'basic_weapon',weight:4294967295"},
            Mutation{"condition:'damaging_contact'", "condition:'teleport'"},
            Mutation{"kind:'held_item',grip:'one_handed'", "kind:'kick',grip:'one_handed'"},
            Mutation{"kind:'natural',requires_natural_apparatus:false", "kind:'natural',requires_natural_apparatus:0"},
            Mutation{"kind:'natural',requires_natural_apparatus:false", "kind:'natural',requires_natural_apparatus:null"},
            Mutation{"kind:'natural',requires_natural_apparatus:false", "kind:'natural',requires_natural_apparatus:'false'"},
            Mutation{"kind:'natural',requires_natural_apparatus:false", "kind:'natural',requires_natural_apparatus:false,requires_natural_apparatus:true"},
            Mutation{"primary:{kind:'held_item'}", "primary:{kind:'held_item',requires_natural_apparatus:false}"},
            Mutation{"primary:{kind:'held_item'}", "primary:{kind:'held_item',requires_natural_apparatus:true}"},
            Mutation{"primary:{kind:'empty_hand'}", "primary:{kind:'empty_hand',requires_natural_apparatus:false}"},
            Mutation{"kind:'kick',functional_legs:'both'", "kind:'kick',functional_legs:'both',requires_natural_apparatus:false"},
            Mutation{"source:'secondary'", "source:'third'"},
            Mutation{"damage_ratio:4000", "damage_ratio:0"},
            Mutation{"damage_ratio:4000", "damage_ratio:4000,reach:{height:'lower',regions:['eyes']}"},
            Mutation{"rating_bonus100:-50", "rating_bonus100:18446744073709551615"},
            Mutation{"amount:700", "amount:700,penetration:-1"},
            Mutation{"damage:[{type:'piercing',amount:700}]", "damage:[{type:'piercing',amount:700},{type:'piercing',amount:100}]"},
            Mutation{"targeting:'self'", "targeting:'actor'"},
            Mutation{"sweep_move:'weapon_sweep_strike'", "sweep_move:'basic_counter'"},
            Mutation{"styles:['boxing_guard']", "styles:['weapon_guard']"},
            Mutation{"moves:[{move:'boxing_jab',weight:50},{move:'boxing_cross',weight:25},{move:'basic_kick',weight:5},{move:'basic_dodge',weight:40},{move:'basic_exhausted_fist',weight:10},{move:'basic_exhausted_kick',weight:5}]",
                "moves:[{move:'boxing_cross',weight:25}]"},
            Mutation{"id:'basic_balanced'", "id:'renamed_balanced'"},
        }) {
        INFO(mutation.to);
        Diagnostics diagnostics;
        const auto result = LoadCombat("", mutation.from, mutation.to, &diagnostics);
        CHECK_FALSE(result);
        CHECK_FALSE(diagnostics.Empty());
        CHECK_FALSE(LoadCombat("", mutation.from, mutation.to));
    }
}

TEST_CASE("Armor grips and art manuals reject invalid physical schemas", "[content][combat-content]") {
    for (const char* fields : {
            "armor:{regions:[{region:'torso',coverage:10001}]}",
            "armor:{regions:[{region:'torso'},{region:'torso'}]}",
            "armor:{regions:[{region:'torso'}],rigid_regions:['eyes']}",
            "armor:{regions:[{region:'mouth',encumbrance:65536}]}",
            "armor:{layer:'skin',regions:[{region:'torso'}]}",
            "armor:{regions:[{region:'eyes',coverage:1.5}]}",
            "melee:{damage:[{type:'blunt',amount:100}],hands:2,grips:[{kind:'two_handed'}]}",
            "melee:{damage:[{type:'blunt',amount:100}],grips:[{kind:'one_handed'},{kind:'one_handed'}]}",
            "melee:{damage:[{type:'blunt',amount:100}],handling_skill:'missing'}",
        }) {
        INFO(fields);
        String item = "{type:'item',id:'item',name:'Item',mass_g:1,volume_ml:1,"; item += fields; item += '}';
        CHECK_FALSE(LoadCombat(View(item)));
    }
    for (const char* fields : {"art:'missing',skill:'melee',level:0,success_chance:1,base_duration:1",
            "art:'boxing',skill:'fabrication',level:0,success_chance:1,base_duration:1",
            "art:'boxing',skill:'melee',level:0,success_chance:10001,base_duration:1",
            "art:'boxing',skill:'melee',level:0,success_chance:1,base_duration:0"}) {
        String book = "{type:'book',id:'manual',name:'Manual',study_work:1,art_manuals:[{"; book += fields; book += "}]}";
        CHECK_FALSE(LoadCombat(View(book)));
    }
}

TEST_CASE("Combat packed semantics reflect all expansion tuning", "[content][combat-content]") {
    const auto original = LoadCombat(""); REQUIRE(original);
    const auto packed = WriteCombatContent(*original);
    struct Change { const char* from; const char* to; };
    for (const auto& change : {Change{"focus_rating_bonus100:100", "focus_rating_bonus100:101"},
            Change{"move:'basic_weapon',weight:40", "move:'basic_weapon',weight:41"},
            Change{"art_xp:50000", "art_xp:50001"},
            Change{"rating_bonus100:-50", "rating_bonus100:-51"},
            Change{"requires_natural_apparatus:false", "requires_natural_apparatus:true"}}) {
        const auto modified = LoadCombat("", change.from, change.to); REQUIRE(modified);
        CHECK_FALSE(WriteCombatContent(*modified) == packed);
    }
    const auto again = LoadCombat(""); REQUIRE(again);
    CHECK(WriteCombatContent(*again) == packed);
}

TEST_CASE("Monster content keeps stamina policy training ranks ratings and reach independent", "[content][combat-content]") {
    const ContentSource source{"monsters.json5", R"({format_version:1,ai_profiles:[{id:'ai'}],definitions:[
        {type:'monster',id:'ordinary',name:'Ordinary',ai_profile:'ai',faction:'undead',
         offensive_training_rank:3,defensive_training_rank:2,attack_rating100:25,defense_rating100:-50,reach:{distance:2}},
        {type:'monster',id:'endless',name:'Endless',ai_profile:'ai',faction:'neutral',inexhaustible:true},
        {type:'monster',id:'defaults',name:'Defaults',ai_profile:'ai'}
    ]})"};
    const auto result = LoadContent({&source, 1}); REQUIRE(result);
    const auto& ordinary = result->GetMonster(result->FindMonster("ordinary"));
    CHECK_FALSE(ordinary.inexhaustible);
    CHECK(ordinary.authoredTrainingRanks);
    CHECK(ordinary.offensiveTrainingRank == 3);
    CHECK(ordinary.defensiveTrainingRank == 2);
    CHECK(ordinary.attackRating100 == 25);
    CHECK(ordinary.defenseRating100 == -50);
    CHECK(ordinary.reach.distance == 2);
    CHECK(ordinary.combatStyle == result->FindCombatStyle("basic_balanced"));
    CHECK(result->GetMonster(result->FindMonster("endless")).inexhaustible);
    const auto& defaults = result->GetMonster(result->FindMonster("defaults"));
    CHECK(defaults.body == BodyKind::Simple);
    CHECK(defaults.faction == CombatFaction::Undead);
    CHECK(defaults.speed == 100);
    CHECK(defaults.attackDamage == 12);
    CHECK(defaults.accuracy == 9000);
    CHECK(defaults.combatStyle == result->FindCombatStyle("basic_balanced"));
    CHECK(defaults.authoredTrainingRanks);
    CHECK(defaults.offensiveTrainingRank == 0);
    CHECK(defaults.defensiveTrainingRank == 0);
    CHECK(defaults.attackRating100 == 0);
    CHECK(defaults.defenseRating100 == 0);
    CHECK_FALSE(defaults.inexhaustible);
    CHECK(defaults.reach == ReachProfile{});
}

TEST_CASE("Shipped combat pack provides wearable equipment learned arts and explicit undead ranks", "[content][combat-content]") {
    Diagnostics diagnostics;
    const auto loaded = LoadContentPack(CATACLYSMX_CONTENT_PACK, &diagnostics);
    for (const auto& frame : diagnostics.Frames()) { INFO(frame.message.c_str()); }
    REQUIRE(loaded);
    CHECK(loaded->GetItem(loaded->FindItem("brass_knuckles")).melee->handlingSkill == loaded->GetCombatSkills().unarmed);
    CHECK(loaded->GetItem(loaded->FindItem("wooden_shield")).melee->blockCapacity == 1400);
    CHECK(loaded->GetItem(loaded->FindItem("safety_goggles")).armor->regions[0].region == ContactRegion::Eyes);
    CHECK(loaded->GetMonster(loaded->FindMonster("zombie_longarms")).reach.distance == 2);
    CHECK(loaded->GetMonster(loaded->FindMonster("cellar_zombie")).inexhaustible);
    CHECK(loaded->GetBook(loaded->FindBook("boxing_manual_text")).artManuals.size() == 1);
}
}
