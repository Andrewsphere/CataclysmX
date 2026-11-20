#include "objects/machine_job.h"
#include "content/content.h"
#include "core/ratio.h"
#include "core/state_reader.h"

namespace cataclysmx {
namespace {
bool ValidName(const String& name) {
    return !name.empty() && name.find('\0') == String::npos;
}
}

bool ValidMachineJobState(const MachineJobState& s) {
    if (!ValidName(s.recipeName) || !ValidName(s.outputName)
        || (!s.softwareName.empty() && !ValidName(s.softwareName))
        || s.quantity == 0 || s.quantity > 255 || s.inputs.empty() || s.inputs.size() > maxRecipeInputs
        || s.outputCount == 0 || s.outputCount > 255 || s.outputCount % s.quantity != 0
        || s.duration == 0 || s.duration % s.quantity != 0 || s.energyPerTick == 0
        || s.duration > UINT64_MAX / s.energyPerTick || s.initiator == 0
        || s.timingSpeed == 0 || s.timingRemainder >= s.timingSpeed
        || s.remainingSetupTicks > machineSetupSliceTicks || s.remainingWork > s.duration
        || s.fundedTicks > s.remainingWork || s.completionRetryTicks > machineCompletionRetryTicks) { return false; }
    std::size_t count = 0;
    for (std::size_t i = 0; i < s.inputs.size(); ++i) {
        const auto& input = s.inputs[i];
        if (!ValidName(input.itemName) || input.count == 0 || input.count > 255
            || input.count % s.quantity != 0) { return false; }
        for (std::size_t j = 0; j < i; ++j) {
            if (s.inputs[j].itemName == input.itemName) { return false; }
        }
        count += input.count;
    }
    if (s.reservedInputs.size() != count) { return false; }
    PersistentId previous = 0;
    for (auto id : s.reservedInputs) {
        if (id <= previous) { return false; }
        previous = id;
    }
    for (std::size_t i = 0; i < s.skills.size(); ++i) {
        if (!ValidName(s.skills[i].skillName) || s.skills[i].level > maximumSkillLevel) { return false; }
        for (std::size_t j = 0; j < i; ++j) {
            if (s.skills[j].skillName == s.skills[i].skillName) { return false; }
        }
    }
    if (s.learningLevel > maximumSkillLevel
        || (s.learningSkillName.empty() ? s.learningXp != 0 || s.learningLevel != 0
            : !ValidName(s.learningSkillName))) { return false; }
    const auto total = std::uint64_t{s.totalSetupWork} * ratioScale;
    if (s.setupWork > total || s.pendingWork > total - s.setupWork) { return false; }
    // The pre-slice fractional residue is strictly below timingSpeed. The saved
    // residue is post-slice, so use the largest possible pre-slice residue here.
    const auto maximumPending = (machineSetupSliceTicks * ticks_per_turn * ratioScale
        + s.timingSpeed - 1) / s.timingSpeed;
    if (s.pendingWork > maximumPending) { return false; }
    if (s.waitingError && (static_cast<unsigned>(*s.waitingError) > static_cast<unsigned>(SaveError::NotOpen)
        || s.phase != MachineJobPhase::Processing || s.remainingWork != 0)) { return false; }
    if (s.completionRetryTicks && !s.waitingError) { return false; }
    switch (s.phase) {
    case MachineJobPhase::Setup:
        return total > s.setupWork && s.remainingWork == s.duration && !s.fundedTicks
            && !s.runRequested && !s.waitingError && !s.completionRetryTicks;
    case MachineJobPhase::Processing:
        return s.setupWork == total && !s.pendingWork && !s.remainingSetupTicks
            && !s.worker && !s.pauseRequested;
    }
    return false;
}

DataObject WriteMachineJobState(const MachineJobState& s) {
    DataObject out;
    out.SetUnsigned("version", 1);
    out.SetString("recipe", s.recipeName);
    out.SetString("output", s.outputName);
    out.SetString("software", s.softwareName);
    out.SetUnsigned("quantity", s.quantity);
    out.SetUnsigned("reserved_volume_ml", s.reservedVolumeMl);
    out.SetUnsigned("output_count", s.outputCount);
    out.SetUnsigned("duration", s.duration);
    out.SetUnsigned("energy_per_tick", s.energyPerTick);
    out.SetUnsigned("total_setup_work", s.totalSetupWork);
    out.SetUnsigned("initiator", s.initiator);
    out.SetUnsigned("worker", s.worker);
    out.SetString("learning_skill", s.learningSkillName);
    out.SetUnsigned("learning_level", s.learningLevel);
    out.SetUnsigned("learning_xp", s.learningXp);
    out.SetUnsigned("phase", static_cast<unsigned>(s.phase));
    out.SetUnsigned("setup_work", s.setupWork);
    out.SetUnsigned("pending_work", s.pendingWork);
    out.SetUnsigned("remaining_setup_ticks", s.remainingSetupTicks);
    out.SetUnsigned("timing_speed", s.timingSpeed);
    out.SetUnsigned("timing_remainder", s.timingRemainder);
    out.SetBoolean("pause_requested", s.pauseRequested);
    out.SetUnsigned("remaining_work", s.remainingWork);
    out.SetUnsigned("funded_ticks", s.fundedTicks);
    out.SetBoolean("run_requested", s.runRequested);
    out.SetUnsigned("completion_retry_ticks", s.completionRetryTicks);
    // Zero means no failure; SaveError itself has a zero-valued member.
    out.SetUnsigned("waiting_error", s.waitingError ? 1u + static_cast<unsigned>(*s.waitingError) : 0u);
    auto inputs = DataObject::List();
    for (const auto& input : s.inputs) {
        DataObject entry;
        entry.SetString("item", input.itemName);
        entry.SetUnsigned("count", input.count);
        inputs.Append(eastl::move(entry));
    }
    out.Set("inputs", eastl::move(inputs));
    auto reserved = DataObject::List();
    for (auto id : s.reservedInputs) { reserved.Append(DataObject::Unsigned(id)); }
    out.Set("reserved", eastl::move(reserved));
    auto skills = DataObject::List();
    for (const auto& skill : s.skills) {
        DataObject entry;
        entry.SetString("skill", skill.skillName);
        entry.SetUnsigned("level", skill.level);
        skills.Append(eastl::move(entry));
    }
    out.Set("skills", eastl::move(skills));
    return out;
}

StateResult<MachineJobState> ReadMachineJobState(const DataObject& in, Diagnostics* diagnostics) {
    StateReader reader(in, diagnostics);
    const auto invalid = [&] { return reader.Invalid({}, "invalid captured machine job"); };
    if (in.Type() != DataType::Object || in.Size() != 30) { return invalid(); }
    const auto version = reader.GetUnsignedOrError("version");
    if (!reader.Result()) { return invalid(); }
    if (version != 1) { return reader.Invalid("version", "unsupported machine job version", StateError::UnsupportedVersion); }
    MachineJobState s;
    s.recipeName = reader.GetStringOrError("recipe");
    s.outputName = reader.GetStringOrError("output");
    s.softwareName = reader.GetStringOrError("software");
    const auto quantity = reader.GetUnsignedOrError("quantity");
    s.reservedVolumeMl = reader.GetUnsignedOrError("reserved_volume_ml");
    const auto outputCount = reader.GetUnsignedOrError("output_count");
    s.duration = reader.GetUnsignedOrError("duration");
    const auto energy = reader.GetUnsignedOrError("energy_per_tick");
    const auto setup = reader.GetUnsignedOrError("total_setup_work");
    s.initiator = reader.GetUnsignedOrError("initiator");
    s.worker = reader.GetUnsignedOrError("worker");
    s.learningSkillName = reader.GetStringOrError("learning_skill");
    const auto learningLevel = reader.GetUnsignedOrError("learning_level");
    const auto learningXp = reader.GetUnsignedOrError("learning_xp");
    const auto phase = reader.GetUnsignedOrError("phase");
    s.setupWork = reader.GetUnsignedOrError("setup_work");
    s.pendingWork = reader.GetUnsignedOrError("pending_work");
    s.remainingSetupTicks = reader.GetUnsignedOrError("remaining_setup_ticks");
    const auto speed = reader.GetUnsignedOrError("timing_speed");
    const auto remainder = reader.GetUnsignedOrError("timing_remainder");
    s.pauseRequested = reader.GetBooleanOrError("pause_requested");
    s.remainingWork = reader.GetUnsignedOrError("remaining_work");
    s.fundedTicks = reader.GetUnsignedOrError("funded_ticks");
    s.runRequested = reader.GetBooleanOrError("run_requested");
    s.completionRetryTicks = reader.GetUnsignedOrError("completion_retry_ticks");
    const auto waiting = reader.GetUnsignedOrError("waiting_error");
    if (!reader.Result() || quantity > 255 || outputCount > 255 || energy > UINT32_MAX
        || setup > UINT32_MAX || learningLevel > maximumSkillLevel || learningXp > UINT32_MAX
        || phase > static_cast<unsigned>(MachineJobPhase::Processing)
        || speed > UINT32_MAX || remainder > UINT32_MAX
        || waiting > 1u + static_cast<unsigned>(SaveError::NotOpen)) { return invalid(); }
    s.quantity = static_cast<std::uint32_t>(quantity);
    s.outputCount = static_cast<std::uint32_t>(outputCount);
    s.energyPerTick = static_cast<std::uint32_t>(energy);
    s.totalSetupWork = static_cast<std::uint32_t>(setup);
    s.learningLevel = static_cast<std::uint8_t>(learningLevel);
    s.learningXp = static_cast<std::uint32_t>(learningXp);
    s.phase = static_cast<MachineJobPhase>(phase);
    s.timingSpeed = static_cast<std::uint32_t>(speed);
    s.timingRemainder = static_cast<std::uint32_t>(remainder);
    if (waiting) { s.waitingError = static_cast<SaveError>(waiting - 1); }
    const auto inputs = in.Get("inputs", DataType::List);
    const auto reserved = in.Get("reserved", DataType::List);
    const auto skills = in.Get("skills", DataType::List);
    if (!inputs || !reserved || !skills || (*inputs)->Size() > maxRecipeInputs
        || (*reserved)->Size() > maxRecipeInputs * 255) { return invalid(); }
    for (std::size_t i = 0; i < (*inputs)->Size(); ++i) {
        const auto& entry = (*inputs)->At(i);
        const auto item = entry.GetString("item");
        const auto count = entry.GetUnsigned("count");
        if (entry.Type() != DataType::Object || entry.Size() != 2 || !item || !count || *count > 255) { return invalid(); }
        s.inputs.push_back({String(*item), static_cast<std::uint32_t>(*count)});
    }
    for (std::size_t i = 0; i < (*reserved)->Size(); ++i) {
        const auto& entry = (*reserved)->At(i);
        if (entry.Type() != DataType::Unsigned) { return invalid(); }
        s.reservedInputs.push_back(entry.UnsignedValue());
    }
    for (std::size_t i = 0; i < (*skills)->Size(); ++i) {
        const auto& entry = (*skills)->At(i);
        const auto skill = entry.GetString("skill");
        const auto level = entry.GetUnsigned("level");
        if (entry.Type() != DataType::Object || entry.Size() != 2 || !skill || !level
            || *level > maximumSkillLevel) { return invalid(); }
        s.skills.push_back({String(*skill), static_cast<std::uint8_t>(*level)});
    }
    if (!ValidMachineJobState(s)) { return invalid(); }
    return s;
}

StateResult<void> ValidateCapturedMachineJob(const ContentCatalog& content, const MachineJobState& s) {
    const auto invalid = [] { return eastl::unexpected(StateError::InvalidData); };
    if (!ValidMachineJobState(s) || !content.FindMachineRecipe(s.recipeName).value
        || !content.FindItem(s.outputName).value) { return invalid(); }
    if (!s.softwareName.empty()) {
        const auto* info = content.TryGetInformation(content.FindInformation(s.softwareName));
        if (!info || !info->software) { return invalid(); }
    }
    for (const auto& input : s.inputs) {
        if (!content.FindItem(input.itemName).value) { return invalid(); }
    }
    for (const auto& skill : s.skills) {
        const auto* definition = content.TryGetSkill(content.FindSkill(skill.skillName));
        if (!definition || skill.level > definition->maxLevel) { return invalid(); }
    }
    if (!s.learningSkillName.empty()) {
        const auto* definition = content.TryGetSkill(content.FindSkill(s.learningSkillName));
        if (!definition || s.learningLevel > definition->maxLevel) { return invalid(); }
    }
    return {};
}
}
