#include "client/crafting_menu.h"
#include "client/text.h"
#include "client/combat_menu.h"

#include <initializer_list>

namespace cataclysmx {
namespace {
const char* Status(CraftingStatus status) {
    switch (status) {
    case CraftingStatus::Paused: return "paused";
    case CraftingStatus::Working: return "working";
    case CraftingStatus::PauseRequested: return "pause requested";
    case CraftingStatus::MissingTools: return "missing tools";
    case CraftingStatus::InsufficientEnergy: return "insufficient energy";
    case CraftingStatus::WorkerUnavailable: return "worker unavailable";
    case CraftingStatus::MissingSkills: return "missing skills";
    case CraftingStatus::MissingRecipeReference: return "missing recipe reference";
    case CraftingStatus::StationBusy: return "station in use";
    case CraftingStatus::ColdForge: return "forge cold";
    case CraftingStatus::InsufficientFuel: return "insufficient forge fuel";
    }
    return "unknown";
}

String FixedText(std::uint64_t value) {
    String result = UnsignedText(value / ratioScale);
    if (value % ratioScale) {
        String fraction = UnsignedText(value % ratioScale);
        while (fraction.size() < 4) { fraction.insert(fraction.begin(), '0'); }
        while (fraction.back() == '0') { fraction.pop_back(); }
        result += "." + fraction;
    }
    return result;
}

const SkillView* Skill(const WorldView& view, SkillId id) {
    for (const auto& skill : view.learning.skills) { if (skill.progress.skill == id) { return &skill; } }
    return nullptr;
}

String SkillName(const ContentCatalog& content, SkillId id) {
    const auto* skill = content.TryGetSkill(id);
    return skill ? skill->name : "Skill #" + UnsignedText(id.value);
}

void Requirements(String& label, const ContentCatalog& content,
    eastl::span<const SkillRequirement> requirements) {
    for (const auto& requirement : requirements) {
        label += " " + SkillName(content, requirement.skill) + " " + UnsignedText(requirement.level);
    }
}

void Training(String& label, const WorldView& view, const ContentCatalog& content,
    eastl::span<const SkillTraining> training, bool disassembly) {
    if (training.empty()) { label += " | no skill XP (no training)"; }
    for (const auto& reward : training) {
        const auto* skill = Skill(view, reward.skill);
        const unsigned level = skill ? skill->progress.level : 0;
        const auto* definition = content.TryGetSkill(reward.skill);
        const unsigned maximum = definition ? definition->maxLevel : maximumSkillLevel;
        const unsigned cap = reward.level < maximum ? reward.level + 1 : maximum;
        label += " | " + SkillName(content, reward.skill) + " training cap " + UnsignedText(cap);
        if (!reward.xpPerWork) { label += ": no XP (rate zero)"; }
        else if (skill && level >= cap) { label += ": no XP (learned level at cap)"; }
        else {
            label += ": " + FixedText(reward.xpPerWork) + " XP/work";
            if (skill && definition && definition->category == SkillCategory::Noncombat && level >= 5) {
                label += disassembly ? " theory" : " practice";
                label += " (advancement needs paired theory + practice)";
                const auto reserve = disassembly ? skill->progress.theory : skill->progress.practice;
                if (skill->progress.xp + reserve >= skill->requiredXp) { label += "; no XP (reserve capped)"; }
            } else if (definition && definition->category == SkillCategory::Noncombat && cap > 5) {
                label += " (surplus clipped on reaching advanced level 5; then "
                    + String(disassembly ? "theory" : "practice") + " needs paired theory + practice)";
            }
        }
    }
}

String BookLabel(const BookStudyView& book, const ContentCatalog& content) {
    const auto& definition = content.GetBook(book.book);
    String label = "Book #" + UnsignedText(book.book.value) + " " + definition.name + " (" + definition.id + ")";
    label += book.accessible ? " [accessible]" : " [reference unavailable]";
    if (book.used) { label += " [used; study benefit already consumed]"; }
    else if (book.completedWork == std::uint64_t{definition.studyWork} * ratioScale) {
        label += book.eligible ? " [completed study; eligible benefit]" : " [completed study; no eligible benefit]";
    } else {
        if (book.completedWork) { label += " [partial study]"; }
        label += book.eligible ? " [eligible]" : " [outside teaching range]";
    }
    label += " | study " + FixedText(book.completedWork) + "/" + UnsignedText(definition.studyWork) + " base work";
    label += " (study time " + UnsignedText(definition.studyWork) + " ticks at speed 100)";
    label += " | clipped XP: direct " + FixedText(book.cappedDirectXp) + ", theory " + FixedText(book.cappedTheoryXp);
    for (const auto& benefit : definition.benefits) {
        label += " | " + SkillName(content, benefit.skill) + " teaches next levels "
            + UnsignedText(benefit.firstLevel) + "-" + UnsignedText(benefit.lastLevel);
        label += ", direct " + UnsignedText(benefit.directXp) + ", theory " + UnsignedText(benefit.theoryXp)
            + " XP, boost " + FixedText(benefit.multiplier) + "x until next level";
    }
    if (!definition.recipes.empty()) {
        label += " | recipe reference (also useful after study):";
        for (auto recipe : definition.recipes) { label += " " + content.GetCraftingRecipe(recipe).name; }
    }
    return label;
}
}

bool SameCraftingChoice(const Command& left, const Command& right) {
    return left.kind == right.kind && left.item == right.item && left.recipe == right.recipe && left.book == right.book
        && left.combatStyle == right.combatStyle && left.art == right.art && left.ability == right.ability
        && left.equipment == right.equipment;
}

void CollectCraftingMenu(const WorldView& view, const ContentCatalog& content,
    const Localization& localization, Vector<CraftingMenuRow>& rows) {
    rows.clear();
    if (view.learning.activeBook.value) {
        Command command; command.kind = CommandKind::pause_study;
        const auto* book = content.TryGetBook(view.learning.activeBook);
        rows.push_back({command, "Pause study #" + UnsignedText(view.learning.activeBook.value)
            + " " + (book ? book->name : String("unknown book"))
            + (view.learning.studyPauseRequested ? " [pause requested]" : " [studying]")
            + " | paid slice remaining " + UnsignedText(view.learning.studyRemaining) + " ticks"});
    }
    for (const auto& book : view.learning.books) {
        if (!book.accessible || book.used || !book.eligible || book.book == view.learning.activeBook
            || !content.TryGetBook(book.book)) { continue; }
        Command command; command.kind = CommandKind::study_book; command.book = book.book;
        rows.push_back({command, String(book.completedWork ? "Resume study " : "Study ") + BookLabel(book, content)});
    }
    for (std::size_t i = 1; i <= content.CraftingRecipeCount(); ++i) {
        const CraftingRecipeId id{static_cast<std::uint32_t>(i)};
        const auto& recipe = content.GetCraftingRecipe(id);
        if (!recipe.enabled) { continue; }
        Command command; command.kind = CommandKind::craft; command.recipe = id;
        String label = "Craft #" + UnsignedText(i) + " " + recipe.name
            + " -> " + localization.ItemName(content, recipe.result.item) + " x" + UnsignedText(recipe.result.count);
        for (const auto& knowledge : view.learning.recipes) {
            if (knowledge.recipe != id) { continue; }
            if (!knowledge.requirementsMet) { label += " [missing skills]"; }
            if (!knowledge.known && !knowledge.referenceAvailable) { label += " [missing recipe reference]"; }
            label += knowledge.known ? " [known]" : knowledge.referenceAvailable ? " [reference available]" : " [unknown]";
            label += " | memorization " + FixedText(knowledge.learningRatio * std::uint64_t{100}) + "%";
            break;
        }
        if (!recipe.skills.empty()) { label += " | requires learned:"; Requirements(label, content, recipe.skills); }
        if (!recipe.knownByDefault) {
            label += " | memorize: " + UnsignedText(recipe.craftsToLearn) + " crafts / "
                + UnsignedText(recipe.disassembliesToLearn) + " disassemblies (combined progress)";
        }
        if (recipe.autolearn) {
            label += " | autolearn requires recipe skills +";
            if (recipe.autolearnSkills.empty()) { label += " no extra thresholds"; }
            Requirements(label, content, recipe.autolearnSkills);
        }
        Training(label, view, content, recipe.training, false);
        rows.push_back({command, eastl::move(label)});
    }
    for (const auto& job : view.craftingJobs) {
        String label = " job #" + UnsignedText(job.id) + " ";
        if (job.kind == CraftingKind::Craft) {
            if (const auto* recipe = content.TryGetCraftingRecipe(job.craftingRecipe)) { label += recipe->name; }
        } else if (const auto* recipe = content.TryGetRecyclingRecipe(job.recyclingRecipe)) { label += recipe->name; }
        label += " " + UnsignedText(job.completedWork) + "/" + UnsignedText(job.totalWork);
        label += " ["; label += Status(job.status); label += "] worker #" + UnsignedText(job.worker);
        label += " paid ticks " + UnsignedText(job.fundedTicksRemaining);
        if (job.pauseRequested) { label += " (pause requested)"; }
        for (auto station : job.stations) { label += " | station #" + UnsignedText(station); }
        for (const auto kind : {CommandKind::resume_craft, CommandKind::pause_craft, CommandKind::cancel_craft}) {
            Command command; command.kind = kind; command.item = job.id;
            rows.push_back({command, String(kind == CommandKind::resume_craft ? "Resume" : kind == CommandKind::pause_craft ? "Pause" : "Cancel") + label});
        }
    }
    const auto items = [&](const Vector<InventoryEntry>& entries, const char* location) {
        for (const auto& item : entries) {
            if (!content.FindRecyclingFor(item.type).value || item.contained_items != 0 || item.installation.value) { continue; }
            bool job = false;
            for (const auto& entry : view.craftingJobs) { job |= entry.id == item.id; }
            if (job) { continue; }
            Command command; command.kind = CommandKind::recycle; command.item = item.id;
            String label = "Recycle #" + UnsignedText(item.id) + " " + localization.ItemName(content, item.type) + location;
            const auto& recipe = content.GetRecyclingRecipe(content.FindRecyclingFor(item.type));
            if (!recipe.skills.empty()) { label += " | requires learned:"; Requirements(label, content, recipe.skills); }
            Training(label, view, content, recipe.training, true);
            rows.push_back({command, eastl::move(label)});
        }
    };
    items(view.inventory, " (carried)");
    items(view.groundItems, " (ground)");
}

void CollectLearningSummary(const WorldView& view, const ContentCatalog& content, Vector<String>& lines) {
    lines.clear();
    for (const auto& skill : view.learning.skills) {
        const auto& p = skill.progress;
        const auto* definition = content.TryGetSkill(p.skill);
        if (!definition) { continue; }
        String line = definition->name + " | learned " + UnsignedText(p.level) + "/" + UnsignedText(definition->maxLevel)
            + " | effective " + NumberText(skill.effectiveLevel);
        if (p.level == definition->maxLevel) { line += " | maximum reached"; }
        else {
            line += " | XP " + FixedText(p.xp) + "/" + FixedText(skill.requiredXp);
            if (definition->category == SkillCategory::Noncombat && p.level >= 5) {
                line += " | theory " + FixedText(p.theory) + ", practice " + FixedText(p.practice)
                    + " (each reserve capped at " + FixedText(skill.requiredXp - p.xp)
                    + "; paired theory + practice advances XP)";
            } else {
                if (definition->category == SkillCategory::Noncombat) {
                    line += " | direct book XP " + FixedText(p.directBookXp) + "/" + FixedText(skill.requiredXp * 3 / 10)
                        + " (30% per-level cap; study boost cap 4x)";
                } else { line += " | combat: books grant no direct/theory XP; boost cap 1.2x"; }
                line += " | study boost " + FixedText(p.multiplier) + "x until next level";
            }
        }
        lines.push_back(eastl::move(line));
    }
    if (view.learning.activeBook.value) {
        const auto* book = content.TryGetBook(view.learning.activeBook);
        lines.push_back("Studying #" + UnsignedText(view.learning.activeBook.value) + " "
            + (book ? book->name : String("unknown book")) + " | paid slice " + UnsignedText(view.learning.studyRemaining)
            + " ticks remaining" + (view.learning.studyPauseRequested ? " [pause requested]" : ""));
    }
    for (const auto& book : view.learning.books) {
        if (content.TryGetBook(book.book)) { lines.push_back(BookLabel(book, content)); }
    }
    const auto combat = CombatDetailsText(content, view);
    std::size_t start = 0;
    while (start < combat.size()) {
        const auto end = combat.find('\n', start);
        lines.push_back(combat.substr(start, end == String::npos ? String::npos : end - start));
        if (end == String::npos) { break; }
        start = end + 1;
    }
    Vector<CombatMenuEntry> combatRows;
    CollectCombatMenu(content, view, combatRows);
    for (const auto& row : combatRows) {
        if (row.command.kind == CommandKind::study_art || row.command.kind == CommandKind::pause_art_study) {
            lines.push_back(row.label + " | " + row.details);
        }
    }
}
}
