#include "client/settings_editor.h"

#include <EASTL/algorithm.h>

#include <algorithm>
#include <charconv>
#include <iterator>

namespace cataclysmx {
namespace {

enum class EditorKind { Number, Boolean, Language, ResidencyMode };
struct Field {
    SettingsField id;
    int section;
    EditorKind kind = EditorKind::Number;
    std::uint64_t minimum = 0;
    std::uint64_t maximum = 0;
};
constexpr Field fields[] = {
    {SettingsField::Language, 0, EditorKind::Language},
    {SettingsField::ViewRadius, 0, EditorKind::Number, 0, playerSightRange},
    {SettingsField::KeyboardRepeatRate, 0, EditorKind::Number, 1, 60},
    {SettingsField::MessageHistorySize, 0, EditorKind::Number, 1, 100},
    {SettingsField::TextSize, 0, EditorKind::Number, 8, 48},
    {SettingsField::Seed, 1, EditorKind::Number, 0, UINT32_MAX},
    {SettingsField::ActiveRadius, 1, EditorKind::Number, 0, 8},
    {SettingsField::ResidentRadius, 1, EditorKind::Number, 0, 8},
    {SettingsField::ResidencyMode, 1, EditorKind::ResidencyMode},
    {SettingsField::ResidentRadiusTiles, 1, EditorKind::Number, 0, maxCircleResidencyRadius},
    {SettingsField::TicksPerSecond, 2, EditorKind::Number, 1, 1000000},
    {SettingsField::RateLimited, 2, EditorKind::Boolean},
    {SettingsField::PauseWhenIdle, 2, EditorKind::Boolean},
    {SettingsField::AutosaveInterval, 3, EditorKind::Number, 0, UINT64_MAX}
};
static_assert(std::size(fields) == static_cast<std::size_t>(SettingsField::Count));
constexpr const char* sectionNames[] = {"interface", "world", "timing", "saving"};
constexpr InputContext contexts[] = {InputContext::Shared, InputContext::SharedLayer, InputContext::Gameplay, InputContext::Interaction,
    InputContext::Menu, InputContext::Debug, InputContext::Items};
constexpr int scopeButton = -2;
constexpr int divider = -1;

eastl::string_view View(const String& text) { return {text.data(), text.size()}; }
String Local(const Localization& loc, eastl::string_view prefix, eastl::string_view suffix) {
    String key(prefix.data(), prefix.size());
    key.append(suffix.data(), suffix.size());
    return loc.Text(View(key), suffix);
}
bool ReadNumber(const String& text, std::uint64_t& number) {
    const auto result = std::from_chars(text.data(), text.data() + text.size(), number);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}
bool IsResidency(SettingsField field) {
    return field == SettingsField::ActiveRadius || field == SettingsField::ResidencyMode
        || field == SettingsField::ResidentRadius || field == SettingsField::ResidentRadiusTiles;
}
bool SuppliesField(const SettingsOverrides& overrides, SettingsField field) {
    if (field == SettingsField::ResidencyMode) {
        return overrides.residencyMode || overrides.residentRadius || overrides.residentRadiusTiles;
    }
    return HasOverride(overrides, field);
}
bool InactiveRadius(const Settings& settings, SettingsField field) {
    return (field == SettingsField::ResidentRadius && settings.residencyMode == ResidencyMode::Circle)
        || (field == SettingsField::ResidentRadiusTiles && settings.residencyMode == ResidencyMode::Square);
}
String DisplayValue(const Settings& settings, SettingsField field, const Localization& loc) {
    if (field == SettingsField::RateLimited || field == SettingsField::PauseWhenIdle) {
        const bool enabled = field == SettingsField::RateLimited ? settings.pacing.rateLimited : settings.pacing.pauseWhenIdle;
        return loc.Text(enabled ? "settings.ui.on" : "settings.ui.off");
    }
    if (field == SettingsField::ResidencyMode) {
        return loc.Text(settings.residencyMode == ResidencyMode::Circle ? "settings.ui.circle" : "settings.ui.square");
    }
    return SettingValue(settings, field);
}
bool SameSetting(const SettingsOverrides& a, const SettingsOverrides& b, SettingsField field) {
    if (SuppliesField(a, field) != SuppliesField(b, field)) { return false; }
    return !SuppliesField(a, field)
        || SettingValue(ResolveSettings({}, a), field) == SettingValue(ResolveSettings({}, b), field);
}
const InputBinding* FindBinding(const Keymap& keys, const InputBinding& id) {
    for (const auto& binding : keys) {
        if (binding.context == id.context && binding.action == id.action) { return &binding; }
    }
    return nullptr;
}
const InputBinding* FindOverrideBinding(const Keymap& keys, const InputBinding& id) {
    if (const auto* exact = FindBinding(keys, id)) { return exact; }
    if (id.context == InputContext::Shared && IsCardinalDirection(id.action)) {
        for (const auto context : contexts) {
            if (const auto* legacy = FindBinding(keys, {context, id.action, {}})) { return legacy; }
        }
    }
    return nullptr;
}
Keymap SharedKeys(const Keymap& global, const Keymap& world) {
    auto shared = DefaultSharedKeymap();
    for (const auto* layer : {&global, &world}) {
        for (const auto& binding : *layer) {
            if (IsSharedInputContext(binding.context)) { SetBinding(shared, binding); }
        }
    }
    return shared;
}
void RemoveSharedKeys(InputBinding& local, const Keymap& shared) {
    for (auto it = local.keys.begin(); it != local.keys.end();) {
        it = IsSharedInputKey(shared, local, *it) ? local.keys.erase(it) : it + 1;
    }
}
bool SameBinding(const InputBinding* a, const InputBinding* b) {
    return a && b ? *a == *b : a == b;
}
bool HasUnavailableChord(const InputBinding& binding) {
    for (const auto& key : binding.keys) {
        if (!LegacyKeyAvailable(key)) { return true; }
    }
    return false;
}
String DetailedChords(const InputBinding* binding) {
    String chords;
    if (binding) {
        for (const auto& key : binding->keys) {
            if (!chords.empty()) { chords += ", "; }
            chords += ChordName(key);
        }
    }
    return chords.empty() ? String("—") : chords;
}
SettingsTone ValueTone(bool edited, bool readOnly, bool here, bool fromGlobal, SettingsTone scope) {
    if (edited) { return SettingsTone::Pending; }
    if (readOnly) { return SettingsTone::Muted; }
    if (here) { return scope; }
    return fromGlobal ? SettingsTone::Global : SettingsTone::Muted;
}
String Source(const Localization& loc, SettingsScope scope, bool here, bool global) {
    if (here) { return loc.Text(scope == SettingsScope::Global ? "settings.ui.source_global" : "settings.ui.source_world"); }
    return loc.Text(global ? "settings.ui.inherit_global" : "settings.ui.inherit_defaults");
}

}

void SettingsEditor::Open(const SettingsStore& store, const Settings& runtime, bool sdl, bool guest) {
    runtime_ = runtime;
    sdl_ = sdl;
    guest_ = guest;
    scope_ = SettingsScope::Global;
    bindings_ = false;
    LoadScope(store);
}
void SettingsEditor::Applied(const SettingsStore& store, const Settings& runtime) {
    runtime_ = runtime;
    draft_ = store.Overrides(scope_);
    keys_ = store.KeyOverrides(scope_);
    BuildRows(store);
}
void SettingsEditor::LoadScope(const SettingsStore& store) {
    pages_[0] = {};
    pages_[1] = {};
    mode_ = Mode::Browse;
    Feedback({}, SettingsTone::Normal);
    Applied(store, runtime_);
}
void SettingsEditor::BuildRows(const SettingsStore& store) {
    rows_.clear();
    rows_.push_back({-1, scopeButton});
    if (bindings_) {
        const Keymap empty;
        const auto& global = scope_ == SettingsScope::Global ? keys_ : store.KeyOverrides(SettingsScope::Global);
        const auto& world = scope_ == SettingsScope::Global ? empty : keys_;
        resolved_ = ResolveKeymap(global, world);
        const auto shared = SharedKeys(global, world);
        const auto defaults = DefaultKeymap();
        const auto defaultShared = DefaultSharedKeymap();
        bindingRows_ = shared;
        for (auto& binding : bindingRows_) {
            if (IsCardinalDirection(binding.action)) {
                binding.keys = FindBinding(resolved_, {InputContext::Gameplay, binding.action, {}})->keys;
            }
        }
        for (const auto& binding : resolved_) {
            if (IsCardinalDirection(binding.action)) { continue; }
            if (binding.context == InputContext::Menu
                && (binding.action == InputAction::Apply || binding.action == InputAction::Reload)) { continue; }
            auto remaining = binding;
            RemoveSharedKeys(remaining, shared);
            if (remaining.keys.empty()) {
                const auto* original = FindBinding(defaults, binding);
                if (!original) { continue; }
                auto defaultRemaining = *original;
                RemoveSharedKeys(defaultRemaining, defaultShared);
                if (defaultRemaining.keys.empty()) { continue; }
            }
            bindingRows_.push_back(eastl::move(remaining));
        }
        for (std::size_t section = 0; section < std::size(contexts); ++section) {
            const auto context = contexts[section];
            bool started = false;
            for (std::size_t i = 0; i < bindingRows_.size(); ++i) {
                const auto& binding = bindingRows_[i];
                const auto sectionContext = IsSharedInputContext(binding.context)
                    ? (IsCardinalDirection(binding.action) ? InputContext::Shared : InputContext::SharedLayer) : binding.context;
                if (sectionContext != context) { continue; }
                if (!started) { rows_.push_back({static_cast<int>(section), divider}); started = true; }
                rows_.push_back({static_cast<int>(section), static_cast<int>(i)});
            }
        }
    } else {
        int section = -1;
        for (std::size_t i = 0; i < std::size(fields); ++i) {
            const auto& field = fields[i];
            if (field.id == SettingsField::TextSize && (!sdl_ || scope_ == SettingsScope::World)) { continue; }
            if (field.section != section) {
                section = field.section;
                rows_.push_back({section, divider});
            }
            rows_.push_back({section, static_cast<int>(i)});
        }
    }
    auto& page = Current();
    page.selected = std::min(page.selected, static_cast<int>(rows_.size()) - 1);
    if (rows_[page.selected].item == divider) { ++page.selected; }
}
Settings SettingsEditor::Preview(const SettingsStore& store) const {
    return scope_ == SettingsScope::Global ? ResolveSettingsWithResidency(runtime_, draft_, {})
        : ResolveSettingsWithResidency(runtime_, store.Overrides(SettingsScope::Global), draft_);
}
bool SettingsEditor::ReadOnly(SettingsField field) const {
    if (field == SettingsField::Seed && scope_ == SettingsScope::World) { return true; }
    return guest_ && (IsResidency(field) || field == SettingsField::Seed || field == SettingsField::TicksPerSecond
        || field == SettingsField::RateLimited || field == SettingsField::PauseWhenIdle || field == SettingsField::AutosaveInterval);
}
void SettingsEditor::Move(int direction, bool section) {
    const int count = static_cast<int>(rows_.size());
    auto& page = Current();
    int selected = page.selected;
    if (section) {
        const int amount = bindings_ ? static_cast<int>(std::size(contexts)) : static_cast<int>(std::size(sectionNames));
        const int current = rows_[selected].section;
        int target = current < 0 ? (direction > 0 ? 0 : amount - 1) : (current + direction + amount) % amount;
        for (int attempt = 0; attempt < amount; ++attempt) {
            const auto match = eastl::find_if(rows_.begin(), rows_.end(), [target](const Row& row) {
                return row.section == target && row.item >= 0;
            });
            if (match != rows_.end()) {
                selected = static_cast<int>(match - rows_.begin());
                page.listScroll = selected - 1;
                break;
            }
            target = (target + direction + amount) % amount;
        }
    } else {
        do { selected = (selected + direction + count) % count; } while (rows_[selected].item == divider);
    }
    page.selected = selected;
    page.chord = 0;
    page.detailScroll = 0;
}
void SettingsEditor::EditField(SettingsField field, int direction, const SettingsStore& store,
    const Localization& loc, const char* directory) {
    if (ReadOnly(field)) { return; }
    const auto& entry = fields[rows_[Current().selected].item];
    const auto value = SettingValue(Preview(store), field);
    Feedback({}, SettingsTone::Normal);
    if (entry.kind == EditorKind::Boolean) {
        SetField(field, value == "true" ? String("false") : String("true"), loc);
    } else if (entry.kind == EditorKind::ResidencyMode) {
        SetField(field, value == "circle" ? String("square") : String("circle"), loc);
    } else if (entry.kind == EditorKind::Language) {
        languages_ = AvailableLanguages(directory);
        if (languages_.empty()) { return; }
        const auto found = eastl::find(languages_.begin(), languages_.end(), value);
        language_ = found != languages_.end() ? static_cast<std::size_t>(found - languages_.begin()) : 0;
        if (!direction) { languageScroll_ = 0; mode_ = Mode::Language; }
        else {
            if (found != languages_.end()) { language_ = (language_ + languages_.size() + direction) % languages_.size(); }
            SetField(field, languages_[language_], loc);
        }
    } else if (!direction) {
        text_ = value;
        replace_ = true;
        mode_ = Mode::Number;
    } else {
        std::uint64_t number = 0;
        if (!ReadNumber(value, number)) { return; }
        if (direction > 0 && number < entry.maximum) { SetField(field, UnsignedText(number + 1), loc); }
        if (direction < 0 && number > entry.minimum) { SetField(field, UnsignedText(number - 1), loc); }
    }
}
bool SettingsEditor::SetField(SettingsField field, const String& value, const Localization& loc) {
    const auto& entry = fields[rows_[Current().selected].item];
    if (entry.kind == EditorKind::Number) {
        std::uint64_t number = 0;
        if (!ReadNumber(value, number) || number < entry.minimum || number > entry.maximum) {
            Feedback(loc.Text("settings.ui.invalid_value") + ": " + loc.Format("settings.ui.number_range", {
                {"min", UnsignedText(entry.minimum)}, {"max", UnsignedText(entry.maximum)}}), SettingsTone::Error);
            return false;
        }
    }
    auto candidate = draft_;
    if (field == SettingsField::ResidentRadius || field == SettingsField::ResidentRadiusTiles) {
        candidate.residencyMode.reset();
        ResetOverride(candidate, field == SettingsField::ResidentRadius ? SettingsField::ResidentRadiusTiles : SettingsField::ResidentRadius);
    } else if (field == SettingsField::ResidencyMode) {
        ResetOverride(candidate, value == "circle" ? SettingsField::ResidentRadius : SettingsField::ResidentRadiusTiles);
    }
    Diagnostics diagnostics;
    if (!SetOverride(candidate, field, View(value), &diagnostics)) {
        Feedback(FormatDiagnostics(diagnostics), SettingsTone::Error);
        return false;
    }
    draft_ = eastl::move(candidate);
    mode_ = Mode::Browse;
    Feedback({}, SettingsTone::Normal);
    return true;
}
void SettingsEditor::ChangeBinding(const SettingsStore& store, InputBinding binding, const Localization& loc) {
    auto candidate = keys_;
    binding.inheritShared = HasSharedInput(binding.context, binding.action);
    SetBinding(candidate, eastl::move(binding));
    const auto& global = scope_ == SettingsScope::Global ? candidate : store.KeyOverrides(SettingsScope::Global);
    const auto& world = scope_ == SettingsScope::World ? candidate : store.KeyOverrides(SettingsScope::World);
    Diagnostics diagnostics;
    if (!ValidateKeymap(candidate, &diagnostics) || !ValidateKeymap(ResolveKeymap(global, {}), &diagnostics)
        || !ValidateKeymap(ResolveKeymap(global, world), &diagnostics)) {
        Feedback(loc.Text("settings.ui.binding_failed") + ": " + FormatDiagnostics(diagnostics), SettingsTone::Error);
        return;
    }
    keys_ = eastl::move(candidate);
    BuildRows(store);
    mode_ = Mode::Browse;
    Feedback({}, SettingsTone::Normal);
}
void SettingsEditor::Text(eastl::string_view text) {
    if (!EditingText()) { return; }
    if (replace_) { text_.clear(); replace_ = false; }
    if (text_.size() + text.size() <= 128) { text_.append(text.data(), text.size()); }
}
SettingsEditEvent SettingsEditor::Key(const eastl::optional<KeyChord>& chord, const SettingsStore& store,
    const Localization& loc, const char* directory, bool enhanced, String inspected) {
    if (chord == KeyChord{"Escape"}) {
        if (Browsing()) { return SettingsEditEvent::Close; }
        mode_ = Mode::Browse;
        Feedback({}, SettingsTone::Normal);
        return SettingsEditEvent::None;
    }
    auto& page = Current();
    const int item = rows_[page.selected].item;
    if (Inspecting()) {
        if (inspected.empty() && chord) { inspected = ChordName(*chord); }
        Feedback(eastl::move(inspected), SettingsTone::Normal);
        return SettingsEditEvent::None;
    }
    if (Capturing()) {
        if (!chord || (!enhanced && !LegacyKeyAvailable(*chord))) {
            Feedback(loc.Text(chord ? "input.unavailable" : "input.unsupported"), SettingsTone::Error);
            return SettingsEditEvent::None;
        }
        auto binding = bindingRows_[item];
        if (*chord == KeyChord{"F9"}
            && (binding.context == InputContext::Gameplay || binding.context == InputContext::Interaction
                || IsSharedInputContext(binding.context))) {
            Feedback(loc.Text("settings.ui.multiplayer_key"), SettingsTone::Error);
            return SettingsEditEvent::None;
        }
        if (captureSlot_ < binding.keys.size()) { binding.keys[captureSlot_] = *chord; }
        else { binding.keys.push_back(*chord); }
        ChangeBinding(store, eastl::move(binding), loc);
        if (!Capturing()) { page.chord = captureSlot_; }
        return SettingsEditEvent::None;
    }
    if (EditingText()) {
        if (chord == KeyChord{"Enter"}) { SetField(fields[item].id, text_, loc); }
        else if (chord == KeyChord{"Backspace"}) {
            if (!text_.empty()) {
                auto start = text_.size() - 1;
                while (start && (static_cast<unsigned char>(text_[start]) & 0xc0) == 0x80) { --start; }
                text_.resize(start);
            }
            replace_ = false;
        }
        return SettingsEditEvent::None;
    }
    const auto action = chord ? ResolveAction(store.Keys(), InputContext::Menu, *chord) : eastl::nullopt;
    if (mode_ == Mode::Language) {
        if (action == InputAction::North) { language_ = (language_ + languages_.size() - 1) % languages_.size(); }
        if (action == InputAction::South) { language_ = (language_ + 1) % languages_.size(); }
        if (action == InputAction::Accept) { SetField(fields[item].id, languages_[language_], loc); }
        if (action == InputAction::Cancel) { mode_ = Mode::Browse; }
        return SettingsEditEvent::None;
    }
    if (!action) { return SettingsEditEvent::None; }
    if (*action == InputAction::SwitchSettingsScope || (*action == InputAction::Accept && item == scopeButton)) {
        scope_ = scope_ == SettingsScope::Global ? SettingsScope::World : SettingsScope::Global;
        LoadScope(store);
        return SettingsEditEvent::None;
    }
    switch (*action) {
    case InputAction::Cancel: return SettingsEditEvent::Close;
    case InputAction::SaveSettings: return SettingsEditEvent::Save;
    case InputAction::Next: case InputAction::Previous: bindings_ = !bindings_; BuildRows(store); break;
    case InputAction::North: case InputAction::South:
        Move(*action == InputAction::North ? -1 : 1, false); break;
    case InputAction::PreviousSettingsSection: case InputAction::NextSettingsSection:
        Move(*action == InputAction::PreviousSettingsSection ? -1 : 1, true); break;
    case InputAction::DetailsUp: case InputAction::DetailsDown:
        page.detailScroll = std::clamp(page.detailScroll + (*action == InputAction::DetailsUp ? -1 : 1), 0, 10000);
        break;
    case InputAction::West: case InputAction::East:
        if (item < 0) { break; }
        if (bindings_) {
            const auto count = bindingRows_[item].keys.size() + 1;
            page.chord = (page.chord + count + (*action == InputAction::West ? -1 : 1)) % count;
        } else { EditField(fields[item].id, *action == InputAction::West ? -1 : 1, store, loc, directory); }
        break;
    case InputAction::Accept: case InputAction::AddBinding:
        if (item < 0) { break; }
        if (bindings_) {
            const auto count = bindingRows_[item].keys.size();
            captureSlot_ = *action == InputAction::AddBinding ? count : std::min(page.chord, count);
            mode_ = Mode::Capture;
            Feedback({}, SettingsTone::Normal);
        } else if (*action == InputAction::Accept) { EditField(fields[item].id, 0, store, loc, directory); }
        break;
    case InputAction::Reset:
        if (item < 0) { break; }
        if (bindings_) {
            ResetBinding(keys_, bindingRows_[item].context, bindingRows_[item].action);
            page.chord = 0;
            BuildRows(store);
        } else if (!ReadOnly(fields[item].id)) { ResetOverride(draft_, fields[item].id); }
        Feedback({}, SettingsTone::Normal);
        break;
    case InputAction::Unbind:
        if (bindings_ && item >= 0 && page.chord < bindingRows_[item].keys.size()) {
            auto binding = bindingRows_[item];
            binding.keys.erase(binding.keys.begin() + page.chord);
            ChangeBinding(store, eastl::move(binding), loc);
        }
        break;
    case InputAction::Inspect:
        if (bindings_) { mode_ = Mode::Inspect; Feedback({}, SettingsTone::Normal); }
        break;
    default: break;
    }
    return SettingsEditEvent::None;
}
int& SettingsEditor::ListScroll() { return mode_ == Mode::Language ? languageScroll_ : Current().listScroll; }
int SettingsEditor::ListScroll() const { return mode_ == Mode::Language ? languageScroll_ : Current().listScroll; }
int& SettingsEditor::DetailScroll() { return Current().detailScroll; }
void SettingsEditor::Feedback(String message, SettingsTone tone) {
    feedback_ = eastl::move(message);
    feedbackTone_ = tone;
    Current().detailScroll = 0;
}
SettingsResult<SettingsStore> SettingsEditor::Prepare(const SettingsStore& store, Diagnostics* diagnostics) const {
    auto candidate = store;
    const auto valid = candidate.SetOverrides(scope_, draft_, keys_, diagnostics);
    if (!valid) { return eastl::unexpected(valid.error()); }
    return candidate;
}

SettingsMenuFrame SettingsEditor::Present(const SettingsStore& store, const Localization& loc, bool enhanced) const {
    SettingsMenuFrame frame;
    const bool global = scope_ == SettingsScope::Global;
    frame.scopeTone = global ? SettingsTone::Global : SettingsTone::World;
    const auto scope = loc.Text(global ? "settings.global" : "settings.world");
    const auto preferences = loc.Text("settings.preferences");
    const auto keybindings = loc.Text("settings.bindings");
    frame.tabs = bindings_ ? preferences + "   [" + keybindings + "]" : "[" + preferences + "]   " + keybindings;
    frame.activeTab = bindings_ ? keybindings : preferences;
    frame.selected = Current().selected;
    const auto hint = [&](InputAction action) { return BindingHint(store.Keys(), InputContext::Menu, action); };
    const auto control = [&](InputAction action, const char* label) { return hint(action) + " " + loc.Text(label); };
    const bool pending = draft_ != store.Overrides(scope_) || keys_ != store.KeyOverrides(scope_);
    const bool dirty = store.Dirty(scope_);
    if (feedbackTone_ != SettingsTone::Normal && !feedback_.empty()) {
        frame.status = feedback_;
        frame.statusTone = feedbackTone_;
    } else if (dirty && !pending && scope_ == SettingsScope::World && store.WorldDirectory().empty()) {
        frame.status = loc.Text("settings.ui.session_only");
        frame.statusTone = SettingsTone::Success;
    } else if (pending || dirty) {
        frame.status = loc.Text(pending ? (dirty ? "settings.ui.unapplied_unsaved" : "settings.ui.unapplied") : "settings.ui.unsaved");
        frame.statusTone = SettingsTone::Pending;
    }
    if (Browsing()) {
        frame.compactHint = control(InputAction::SaveSettings, "settings.ui.save") + " Esc " + hint(InputAction::Next);
        frame.hints.push_back(control(InputAction::SaveSettings, "settings.ui.save") + "  |  Escape " + loc.Text("settings.ui.close")
            + "  |  " + hint(InputAction::Next) + "/" + hint(InputAction::Previous) + " " + loc.Text("settings.ui.change_tab"));
        frame.hints.push_back(hint(InputAction::North) + "/" + hint(InputAction::South) + " " + loc.Text("settings.ui.choose_row")
            + "  |  " + hint(InputAction::West) + "/" + hint(InputAction::East) + " " + loc.Text(bindings_ ? "settings.ui.choose_chord" : "settings.ui.adjust")
            + "  |  " + control(InputAction::Accept, "settings.ui.edit") + "  |  " + control(InputAction::SwitchSettingsScope, "settings.ui.switch_scope"));
        String secondary = control(InputAction::Reset, "settings.ui.reset") + "  |  "
            + hint(InputAction::PreviousSettingsSection) + "/" + hint(InputAction::NextSettingsSection)
            + " " + loc.Text("settings.ui.jump_section") + "  |  " + hint(InputAction::DetailsUp) + "/" + hint(InputAction::DetailsDown)
            + " " + loc.Text("settings.ui.details");
        frame.hints.push_back(eastl::move(secondary));
        if (bindings_) {
            frame.hints.push_back(control(InputAction::AddBinding, "settings.ui.add_chord") + "  |  "
                + control(InputAction::Unbind, "settings.ui.remove_chord") + "  |  " + control(InputAction::Inspect, "settings.ui.inspect"));
        }
    } else {
        frame.hints.push_back(loc.Text(Capturing() ? "settings.capture" : Inspecting() ? "settings.inspecting"
            : EditingText() ? "settings.ui.number_prompt" : "settings.ui.choose_language"));
        String cancel = "Escape " + loc.Text("settings.ui.cancel");
        if (EditingText()) { cancel = "Enter " + loc.Text("settings.ui.accept") + "  |  " + cancel; }
        if (mode_ == Mode::Language) { cancel = control(InputAction::Accept, "settings.ui.accept") + "  |  " + cancel; }
        frame.hints.push_back(cancel);
        frame.compactHint = eastl::move(cancel);
    }
    if (mode_ == Mode::Language) {
        for (const auto& language : languages_) { frame.rows.push_back({language}); }
        frame.selected = static_cast<int>(language_);
        return frame;
    }

    const auto detail = [&](const char* key, const String& value, SettingsTone tone = SettingsTone::Normal) {
        frame.details.push_back({loc.Format(key, {{"value", value}}), tone});
    };
    const auto dividerLine = [&](const char* key) {
        frame.details.push_back({loc.Text(key), SettingsTone::Muted, true});
    };
    const auto preview = Preview(store);
    const auto& saved = store.Overrides(scope_);
    const auto& inheritedGlobal = store.Overrides(SettingsScope::Global);
    const auto& savedWorld = store.Overrides(SettingsScope::World);
    for (const auto& row : rows_) {
        if (row.item == scopeButton) {
            frame.rows.push_back({scope, hint(InputAction::SwitchSettingsScope), frame.scopeTone, false, true});
        } else if (row.item == divider) {
            frame.rows.push_back({bindings_ ? Local(loc, "context.", ContextName(contexts[row.section]))
                : Local(loc, "settings.ui.category.", sectionNames[row.section]), {}, SettingsTone::Normal, true});
        } else if (bindings_) {
            const auto& binding = bindingRows_[row.item];
            const bool selected = &row == &rows_[Current().selected];
            const bool shared = IsSharedInputContext(binding.context);
            const bool direction = IsCardinalDirection(binding.action);
            const auto& globalKeys = store.KeyOverrides(SettingsScope::Global);
            const auto* local = FindOverrideBinding(keys_, binding);
            const auto* savedBinding = FindOverrideBinding(store.KeyOverrides(scope_), binding);
            const bool changed = !SameBinding(local, savedBinding);
            const auto* globalBinding = global ? nullptr : FindOverrideBinding(globalKeys, binding);
            const bool fromGlobal = globalBinding && !local;
            const bool fromHere = local != nullptr;
            const bool unavailable = !enhanced && HasUnavailableChord(binding);
            String chords;
            for (std::size_t i = 0; i < binding.keys.size(); ++i) {
                const auto chordName = ChordName(binding.keys[i]);
                if (!chords.empty()) { chords += "  "; }
                chords += selected && Current().chord == i ? "[" + chordName + "]" : chordName;
            }
            if (selected) {
                if (!chords.empty()) { chords += "  "; }
                const auto add = loc.Text("settings.ui.add_slot");
                chords += Current().chord == binding.keys.size() ? "[" + add + "]" : add;
            } else if (chords.empty()) { chords = "—"; }
            auto tone = ValueTone(changed, false, fromHere, fromGlobal, frame.scopeTone);
            if (unavailable && !changed) { tone = SettingsTone::Warning; }
            auto label = Local(loc, "action.", ActionName(binding.action));
            if (binding.context == InputContext::SharedLayer) {
                label = loc.Format("settings.ui.alternate_binding", {{"value", label}});
            }
            frame.rows.push_back({label, eastl::move(chords), tone});
            if (!selected) { continue; }
            frame.details.push_back({label, frame.scopeTone});
            frame.details.push_back({direction ? loc.Text("settings.ui.shared_direction_help")
                : Local(loc, "settings.ui.action_help.", ActionName(binding.action))});
            dividerLine("settings.ui.binding_details");
            detail("settings.ui.bindings_value", DetailedChords(&binding));
            if (shared && !direction) {
                String targets;
                String exceptions;
                for (const auto context : SharedInputContexts(binding.context, binding.action)) {
                    const auto name = Local(loc, "context.", ContextName(context));
                    if (!targets.empty()) { targets += ", "; }
                    targets += name;
                    if (eastl::any_of(binding.keys.begin(), binding.keys.end(), [&](const KeyChord& key) {
                        return ResolveAction(resolved_, context, key) != binding.action;
                    })) {
                        if (!exceptions.empty()) { exceptions += ", "; }
                        exceptions += name;
                    }
                }
                detail("settings.ui.binding_contexts", targets);
                if (!exceptions.empty()) { detail("settings.ui.binding_context_exceptions", exceptions, SettingsTone::Warning); }
            }
            dividerLine("settings.ui.scope_details");
            detail("settings.ui.source", Source(loc, scope_, fromHere, fromGlobal), ValueTone(false, false, fromHere, fromGlobal, frame.scopeTone));
            const auto inheritedShared = global ? DefaultSharedKeymap() : SharedKeys(globalKeys, {});
            const auto inherited = global ? DefaultKeymap() : ResolveKeymap(globalKeys, {});
            const auto inheritedContext = direction ? InputContext::Gameplay : binding.context;
            const auto* before = FindBinding(shared && !direction ? inheritedShared : inherited,
                {inheritedContext, binding.action, {}});
            InputBinding reset{binding.context, binding.action, {}};
            if (before) { reset.keys = before->keys; }
            if (!shared) { RemoveSharedKeys(reset, inheritedShared); }
            detail("settings.ui.inherited_value", DetailedChords(&reset));
            if (global && FindOverrideBinding(store.KeyOverrides(SettingsScope::World), binding)) {
                frame.details.push_back({loc.Text(direction ? "settings.ui.has_world_direction"
                    : "settings.ui.world_masks_global"), SettingsTone::World});
            }
            if (direction) {
                bool listedLegacy = false;
                for (const auto context : contexts) {
                    if (IsSharedInputContext(context) || context == InputContext::Gameplay) { continue; }
                    const auto* contextual = FindBinding(resolved_, {context, binding.action, {}});
                    if (!contextual && context == InputContext::Items) {
                        contextual = FindBinding(resolved_, {InputContext::Menu, binding.action, {}});
                    }
                    if (!contextual || contextual->keys == binding.keys) { continue; }
                    if (!listedLegacy) { dividerLine("settings.ui.legacy_directions"); listedLegacy = true; }
                    const auto name = Local(loc, "context.", ContextName(context));
                    frame.details.push_back({loc.Format("settings.ui.context_binding", {{"context", name},
                        {"value", DetailedChords(contextual)}}), SettingsTone::Warning});
                }
            }
            if (unavailable) { frame.details.push_back({loc.Text("input.legacy"), SettingsTone::Warning}); }
        } else {
            const auto field = fields[row.item].id;
            const bool selected = &row == &rows_[Current().selected];
            const bool readOnly = ReadOnly(field);
            const bool inactive = InactiveRadius(guest_ ? runtime_ : preview, field);
            auto label = field == SettingsField::Seed && !global ? loc.Text("settings.ui.world_seed")
                : Local(loc, "settings.ui.label.", SettingName(field));
            String value;
            if (selected && EditingText()) { value = text_ + " _"; }
            else if (guest_ && readOnly && field != SettingsField::TicksPerSecond && field != SettingsField::RateLimited
                && field != SettingsField::PauseWhenIdle) { value = loc.Text("settings.ui.host_owned"); }
            else if (inactive) { value = loc.Text("settings.ui.inactive_radius"); }
            else { value = DisplayValue(readOnly ? runtime_ : preview, field, loc); }
            const bool local = SuppliesField(draft_, field);
            const bool changed = !SameSetting(draft_, saved, field);
            const bool inheritsGlobal = !global && SuppliesField(inheritedGlobal, field);
            frame.rows.push_back({label, eastl::move(value), ValueTone(changed, readOnly || inactive, local,
                inheritsGlobal, frame.scopeTone), false, false, readOnly});
            if (!selected) { continue; }
            frame.details.push_back({eastl::move(label), frame.scopeTone});
            frame.details.push_back({Local(loc, "settings.ui.description.", SettingName(field))});
            if (guest_ && readOnly) { frame.details.push_back({loc.Text("settings.ui.controlled_by_host"), SettingsTone::Muted}); }
            else if (readOnly) { frame.details.push_back({loc.Text("settings.ui.seed_fixed"), SettingsTone::Muted}); }
            else {
                dividerLine("settings.ui.scope_details");
                detail("settings.ui.source", IsResidency(field) && !local && !inheritsGlobal
                    ? loc.Text("settings.ui.source_runtime") : Source(loc, scope_, local, inheritsGlobal),
                    ValueTone(false, false, local, inheritsGlobal, frame.scopeTone));
                const auto inherited = ResolveSettingsWithResidency(runtime_, global ? SettingsOverrides{} : inheritedGlobal, {});
                detail("settings.ui.inherited_value", DisplayValue(inherited, field, loc));
                if (global && SuppliesField(savedWorld, field)) { frame.details.push_back({loc.Text("settings.ui.world_masks_global"), SettingsTone::World}); }
                if (changed) {
                    const auto applied = global ? ResolveSettingsWithResidency(runtime_, saved, {})
                        : ResolveSettingsWithResidency(runtime_, inheritedGlobal, saved);
                    detail("settings.ui.applied_value", DisplayValue(applied, field, loc));
                }
                dividerLine("settings.ui.effect_details");
                frame.details.push_back({Local(loc, "settings.ui.constraint.", SettingName(field))});
                frame.details.push_back({Local(loc, "settings.ui.effect.", SettingName(field))});
            }
        }
    }
    if (rows_[Current().selected].item == scopeButton) { frame.details.push_back({loc.Text("settings.ui.scope_help")}); }
    if (Inspecting()) {
        frame.details.clear();
        frame.details.push_back({loc.Text("settings.ui.inspection_title"), frame.scopeTone});
        if (!sdl_) { frame.details.push_back({loc.Text(enhanced ? "input.enhanced" : "input.legacy")}); }
        frame.details.push_back({feedback_});
    } else {
        if (scope_ == SettingsScope::World && store.WorldDirectory().empty()) { frame.details.push_back({loc.Text("settings.ui.memory_only"), SettingsTone::Muted}); }
        if (feedbackTone_ == SettingsTone::Error && !feedback_.empty()) { frame.details.insert(frame.details.begin(), {feedback_, SettingsTone::Error}); }
    }
    return frame;
}

}
