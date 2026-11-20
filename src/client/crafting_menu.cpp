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
        && left.equipment == right.equipment && left.target == right.target
        && left.machineRecipe == right.machineRecipe && left.quantity == right.quantity && left.machineAction == right.machineAction
        && left.informationSource == right.informationSource && left.informationDestination == right.informationDestination
        && left.information == right.information && left.informationHost == right.informationHost;
}

void CollectCraftingMenu(const WorldView& view, const ContentCatalog& content,
    const Localization& localization, Vector<CraftingMenuRow>& rows) {
    rows.clear();
    for (const auto& machine : view.machines) {
        const String identity = " machine #" + UnsignedText(machine.object);
        String progress = identity + (!machine.hasJob ? " [idle]" : machine.setup ? " [setup]" : machine.running ? " [processing]" : " [stopped]");
        progress += " | setup " + UnsignedText(machine.setupCompleted) + "/" + UnsignedText(machine.setupWork);
        progress += " | remaining " + UnsignedText(machine.remainingTicks) + " paid " + UnsignedText(machine.fundedTicks);
        progress += " | reserved " + UnsignedText(machine.reservedVolumeMl) + " ml | expected nominal XP "
            + FixedText(machine.learningXp) + " (recipient boosts/caps apply)";
        progress += " | initiator #" + UnsignedText(machine.initiator) + " worker #" + UnsignedText(machine.worker);
        if (machine.pauseRequested) { progress += " [pause requested]"; }
        if (machine.completionPending) { progress += " [completion storage pending]"; }
        for (auto action : {MachineAction::Stop, MachineAction::Resume, MachineAction::Cancel, MachineAction::Collect}) {
            Command command; command.kind = CommandKind::machine_action; command.target = machine.object; command.machineAction = action;
            const char* label = action == MachineAction::Stop ? "Stop" : action == MachineAction::Resume ? "Resume"
                : action == MachineAction::Cancel ? "Cancel" : "Collect";
            rows.push_back({command, String(label) + progress});
        }
        for (const auto& preview : machine.recipes) {
            const auto* recipe = content.TryGetMachineRecipe(preview.recipe);
            if (!recipe) { continue; }
            // Each explicit quantity is a stable choice; stale requests are never reduced.
            const unsigned maximum = preview.maximumQuantity ? preview.maximumQuantity : 1;
            for (unsigned quantity = 1; quantity <= maximum; ++quantity) {
                Command command; command.kind = CommandKind::begin_machine; command.target = machine.object;
                command.machineRecipe = preview.recipe; command.quantity = static_cast<std::uint16_t>(quantity);
                String label = "Begin" + identity + " " + recipe->name + " x" + UnsignedText(quantity)
                    + " [range 1-" + UnsignedText(preview.maximumQuantity) + "; " + MachineOutcomeText(preview.availability) + "]";
                label += " | setup " + UnsignedText(recipe->setupWork + std::uint64_t{recipe->setupWorkPerUnit} * quantity);
                label += " | processing " + UnsignedText(recipe->duration * quantity) + " ticks | energy "
                    + UnsignedText(recipe->duration * quantity * recipe->energyPerTick);
                label += " | nominal XP " + FixedText(std::uint64_t{recipe->learning.xpPerExecution} * quantity
                    * (recipe->learning.level >= 5 ? 8000 : ratioScale))
                    + " (recipient boosts/caps apply)";
                if (!recipe->skills.empty()) { label += " | requires learned:"; Requirements(label, content, recipe->skills); }
                if (const auto* skill = content.TryGetSkill(recipe->learning.skill)) {
                    label += " | " + skill->name + " training ceiling "
                        + UnsignedText(eastl::min<unsigned>(recipe->learning.level + 1, skill->maxLevel));
                }
                std::uint64_t inputVolume = 0;
                for (const auto& input : recipe->inputs) {
                    label += " | " + localization.ItemName(content, input.Item()) + " x" + UnsignedText(input.Count() * quantity);
                    inputVolume += std::uint64_t{content.GetItem(input.Item()).volumeMl} * input.Count() * quantity;
                }
                const auto outputVolume = std::uint64_t{content.GetItem(recipe->result.item).volumeMl} * recipe->result.count * quantity;
                label += " | capacity reservation " + UnsignedText(eastl::max(inputVolume, outputVolume)) + " ml";
                rows.push_back({command, eastl::move(label)});
            }
        }
    }
    const auto deviceName = [](const InformationDeviceView& device) {
        return String(device.endpoint.object ? "controller #" : "device #")
            + UnsignedText(device.endpoint.object ? device.endpoint.object : device.endpoint.item);
    };
    for (const auto& source : view.informationDevices) {
        for (auto package : source.packages) {
            const auto* definition = content.TryGetInformation(package);
            if (!definition) { continue; }
            if (source.writable) {
                Command command; command.kind = CommandKind::remove_information;
                command.informationDestination = source.endpoint; command.information = package;
                rows.push_back({command, "Delete " + definition->name + " from " + deviceName(source)});
            }
            for (const auto& destination : view.informationDevices) {
                if (!destination.writable || destination.endpoint == source.endpoint) { continue; }
                const auto addCopy = [&](PersistentId host) {
                    Command command; command.kind = CommandKind::copy_information;
                    command.informationSource = source.endpoint; command.informationDestination = destination.endpoint;
                    command.information = package; command.informationHost = host;
                    String label = source.endpoint.object ? "Export " : destination.endpoint.object ? "Import " : "Copy ";
                    label += definition->name + " from " + deviceName(source) + " to " + deviceName(destination);
                    label += host ? " via host #" + UnsignedText(host) : " (direct)";
                    label += " | 100 base work, no XP";
                    if (!source.powered || !destination.powered) label += " [check device/host power]";
                    if (source.transferring || destination.transferring) label += " [transfer active]";
                    rows.push_back({command, eastl::move(label)});
                };
                addCopy(0);
                for (const auto& host : view.informationDevices) {
                    if (host.endpoint.item && host.reader && host.powered) { addCopy(host.endpoint.item); }
                }
            }
        }
        if (source.transferring) {
            Command command; command.kind = CommandKind::cancel_information;
            rows.push_back({command, "Cancel information transfer | remaining " + UnsignedText(source.remainingTicks)
                + " ticks (no time refund)"});
        }
    }
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
    for (const auto& device : view.informationDevices) {
        String line = String(device.endpoint.object ? "Controller #" : "Information device #")
            + UnsignedText(device.endpoint.object ? device.endpoint.object : device.endpoint.item);
        line += device.powered ? " [powered]" : " [unpowered]";
        line += device.writable ? " [writable]" : " [read only]";
        if (device.reader) { line += " [reader/transfer host]"; }
        if (device.transferring) { line += " | copy remaining " + UnsignedText(device.remainingTicks) + " ticks"; }
        line += " | packages:";
        if (device.packages.empty()) { line += " empty"; }
        for (auto package : device.packages) {
            if (const auto* definition = content.TryGetInformation(package)) {
                line += " #" + UnsignedText(package.value) + " " + definition->name;
            }
        }
        lines.push_back(eastl::move(line));
    }
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
