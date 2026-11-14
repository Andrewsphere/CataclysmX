#include "terminal/settings_menu.h"

#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/node.hpp>

#include <EASTL/utility.h>

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <utility>

namespace cataclysmx {
namespace {

eastl::string_view View(const String& text) { return {text.data(), text.size()}; }
std::string Frontend(const String& text) { return {text.data(), text.size()}; }
ftxui::Element Padded(ftxui::Element content) {
    return ftxui::hbox({ftxui::text(" "), std::move(content) | ftxui::flex, ftxui::text(" ")});
}
ftxui::Element Panel(ftxui::Element heading, ftxui::Element content) {
    return Padded(ftxui::vbox({std::move(heading) | ftxui::bold,
        ftxui::separatorLight(), std::move(content) | ftxui::flex}));
}
ftxui::Element PackedControls(ftxui::Elements controls) {
    ftxui::Elements row;
    for (auto& control : controls) {
        if (!row.empty()) { row.push_back(ftxui::text("  |  ")); }
        row.push_back(std::move(control));
    }
    row.push_back(ftxui::filler());
    return ftxui::hbox(std::move(row));
}
String MessageKey(eastl::string_view prefix, eastl::string_view name) {
    String key(prefix.data(), prefix.size()); key.append(name.data(), name.size()); return key;
}

enum class EditorKind { Number, Boolean, Language, ResidencyMode };
enum class FocusArea { Scope, Section, Category, List, Details, Count };
enum class EditorMode { Browse, Number, Language, Capture, Inspect };
enum class FeedbackKind { None, Success, Error };
struct FieldPresentation {
    SettingsField field;
    unsigned category;
    EditorKind editor;
    std::uint64_t minimum = 0;
    std::uint64_t maximum = 0;
};
constexpr FieldPresentation fields[] = {
    {SettingsField::Language, 0, EditorKind::Language},
    {SettingsField::ViewRadius, 0, EditorKind::Number, 0, playerSightRange},
    {SettingsField::KeyboardRepeatRate, 0, EditorKind::Number, 1, 60},
    {SettingsField::MessageHistorySize, 0, EditorKind::Number, 1, 100},
    {SettingsField::Seed, 1, EditorKind::Number, 0, UINT32_MAX},
    {SettingsField::ActiveRadius, 1, EditorKind::Number, 0, 8},
    {SettingsField::ResidentRadius, 1, EditorKind::Number, 0, 8},
    {SettingsField::ResidencyMode, 1, EditorKind::ResidencyMode},
    {SettingsField::ResidentRadiusTiles, 1, EditorKind::Number, 0, maxCircleResidencyRadius},
    {SettingsField::TicksPerSecond, 2, EditorKind::Number, 1, 1000000},
    {SettingsField::RateLimited, 2, EditorKind::Boolean},
    {SettingsField::PauseWhenIdle, 2, EditorKind::Boolean},
    {SettingsField::AutosaveInterval, 3, EditorKind::Number, 0, UINT64_MAX},
};
static_assert(std::size(fields) == static_cast<unsigned>(SettingsField::Count));
constexpr eastl::string_view categoryNames[] = {"interface", "world", "timing", "saving"};
constexpr InputContext bindingContexts[] = {
    InputContext::Gameplay, InputContext::Interaction, InputContext::Menu, InputContext::Debug, InputContext::Items
};

// Selection and both viewports belong to this exact scope/section/category.
struct PageState {
    std::size_t selected = 0;
    int listScroll = 0;
    int detailScroll = 0;
};

const InputBinding* FindBinding(const Keymap& keys, InputContext context, InputAction action) {
    for (const auto& binding : keys) {
        if (binding.context == context && binding.action == action) { return &binding; }
    }
    return nullptr;
}
bool SameBinding(const InputBinding* first, const InputBinding* second) {
    if (!first || !second) { return first == second; }
    return first->keys == second->keys;
}
bool SameOverride(const SettingsOverrides& first, const SettingsOverrides& second, SettingsField field) {
    if (HasOverride(first, field) != HasOverride(second, field)) { return false; }
    return !HasOverride(first, field)
        || SettingValue(ResolveSettings({}, first), field) == SettingValue(ResolveSettings({}, second), field);
}
bool ResidencyField(SettingsField field) {
    return field == SettingsField::ActiveRadius || field == SettingsField::ResidentRadius
        || field == SettingsField::ResidencyMode || field == SettingsField::ResidentRadiusTiles;
}
bool SuppliesField(const SettingsOverrides& overrides, SettingsField field) {
    if (field == SettingsField::ResidencyMode) {
        return overrides.residencyMode || overrides.residentRadius || overrides.residentRadiusTiles;
    }
    return HasOverride(overrides, field);
}

// FTXUI's frame centers focus. These two local DOM nodes instead preserve an
// explicit scroll offset and choose the layout from the actual allocated box,
// including the first frame and resizes; they do not query the physical terminal.
class SettingsScrollPane final : public ftxui::Node {
public:
    SettingsScrollPane(ftxui::Element content, int& offset, int selected = -1)
        : Node({std::move(content)}), offset_(offset), selected_(selected) {}
    void ComputeRequirement() override {
        Node::ComputeRequirement();
        requirement_.min_x = 0; requirement_.min_y = 0;
        requirement_.flex_grow_x = 1; requirement_.flex_grow_y = 1;
        requirement_.flex_shrink_x = 1; requirement_.flex_shrink_y = 1;
    }
    void SetBox(ftxui::Box box) override {
        Node::SetBox(box);
        const int height = std::max(0, box.y_max - box.y_min + 1);
        const int contentHeight = std::max(height, children_[0]->requirement().min_y);
        int wanted = offset_;
        if (selected_ >= 0 && height > 0) {
            if (selected_ < wanted) { wanted = selected_; }
            if (selected_ >= wanted + height) { wanted = selected_ - height + 1; }
        }
        actualOffset_ = std::clamp(wanted, 0, std::max(0, contentHeight - height));
        box.y_min -= actualOffset_; box.y_max = box.y_min + contentHeight - 1;
        children_[0]->SetBox(box);
    }
    void Render(ftxui::Screen& screen) override {
        // Paragraph height is finalized by FTXUI's layout iterations. Retain the
        // requested offset until then, rather than clamping it to an early size.
        offset_ = actualOffset_;
        const auto previous = screen.stencil;
        screen.stencil = ftxui::Box::Intersection(previous, box_);
        if (!screen.stencil.IsEmpty()) { Node::Render(screen); }
        screen.stencil = previous;
    }
private:
    int& offset_;
    int selected_;
    int actualOffset_ = 0;
};

class SettingsBody final : public ftxui::Node {
public:
    SettingsBody(ftxui::Element list, ftxui::Element details, ftxui::Element categoryStrip,
        ftxui::Element categorySidebar, int rowCount, bool& sidebarVisible)
        : Node({std::move(list), std::move(details), std::move(categoryStrip),
            std::move(categorySidebar), ftxui::separatorLight(), ftxui::separatorLight()}),
          rowCount_(rowCount), sidebarVisible_(sidebarVisible) {}
    void ComputeRequirement() override {
        Node::ComputeRequirement();
        requirement_.min_x = 0; requirement_.min_y = 0;
        requirement_.flex_grow_x = 1; requirement_.flex_grow_y = 1;
        requirement_.flex_shrink_x = 1; requirement_.flex_shrink_y = 1;
    }
    void SetBox(ftxui::Box box) override {
        Node::SetBox(box);
        auto list = box; auto details = box; auto divider = box;
        auto categories = box; auto categoryDivider = box;
        const int width = box.x_max - box.x_min + 1;
        const int height = box.y_max - box.y_min + 1;
        sidebarVisible_ = width >= 98; // 100 columns including the outer border.
        if (sidebarVisible_) {
            categories.x_max = box.x_min + 17;
            categoryDivider.x_min = categories.x_max + 1; categoryDivider.x_max = categoryDivider.x_min;
            list.x_min = categoryDivider.x_min + 1;
            list.x_max = list.x_min + (width - 20) * 45 / 100 - 1;
            divider.x_min = list.x_max + 1; divider.x_max = divider.x_min;
            details.x_min = divider.x_min + 1;
        } else {
            categories.y_max = box.y_min;
            categoryDivider.y_min = box.y_min + 1; categoryDivider.y_max = categoryDivider.y_min;
            list.y_min = box.y_min + 2;
            // Headers take two lines; the list viewport keeps its compact sizing.
            const int listHeight = std::min(std::max(2, rowCount_), std::max(2, (height - 5) / 3));
            list.y_max = std::min(box.y_max, list.y_min + listHeight + 1);
            divider.y_min = list.y_max + 1; divider.y_max = divider.y_min;
            details.y_min = divider.y_min + 1;
        }
        children_[0]->SetBox(list); children_[1]->SetBox(details);
        children_[sidebarVisible_ ? 3 : 2]->SetBox(categories);
        children_[4]->SetBox(divider); children_[5]->SetBox(categoryDivider);
    }
    void Render(ftxui::Screen& screen) override {
        const auto previous = screen.stencil;
        screen.stencil = ftxui::Box::Intersection(previous, box_);
        if (!screen.stencil.IsEmpty()) {
            children_[0]->Render(screen); children_[1]->Render(screen);
            children_[sidebarVisible_ ? 3 : 2]->Render(screen);
            children_[4]->Render(screen); children_[5]->Render(screen);
        }
        screen.stencil = previous;
    }
private:
    int rowCount_;
    bool& sidebarVisible_;
};

class SettingsMenu final : public ftxui::ComponentBase {
public:
    SettingsMenu(SettingsStore& store, Localization& localization, Server& server,
        TerminalKeyboard& keyboard, const char* directory, std::function<void()> onClose)
        : store_(store), localization_(localization), server_(server), keyboard_(keyboard),
          directory_(directory), onClose_(std::move(onClose)) { RefreshDrafts(); }

    bool Focusable() const override { return true; }
    ftxui::Element OnRender() override {
        const auto visibleFields = VisibleFields();
        const auto visibleBindings = VisibleBindings();
        auto& page = Page();
        const auto count = mode_ == EditorMode::Language ? languages_.size()
            : Bindings() ? visibleBindings.size() : visibleFields.size();
        if (count == 0) { std::abort(); }
        if (page.selected >= (Bindings() ? visibleBindings.size() : visibleFields.size())) { page.selected = 0; }
        ftxui::Elements rows;
        if (mode_ == EditorMode::Language) {
            for (std::size_t i = 0; i < languages_.size(); ++i) {
                auto row = ftxui::text((i == languageSelection_ ? "> " : "  ") + Frontend(languages_[i]));
                if (i == languageSelection_) { row = row | ftxui::inverted | ftxui::bold; }
                rows.push_back(row);
            }
        } else {
            const auto preview = Preview();
            for (std::size_t i = 0; i < count; ++i) {
                String label;
                String value;
                bool local = false;
                bool pending = false;
                bool unavailable = false;
                if (Bindings()) {
                    const auto& binding = visibleBindings[i];
                    label = localization_.Text(View(MessageKey("action.", ActionName(binding.action))));
                    value = BindingHint(PreviewKeys(), binding.context, binding.action);
                    local = FindBinding(keyDrafts_[Index()], binding.context, binding.action) != nullptr;
                    pending = !SameBinding(FindBinding(keyDrafts_[Index()], binding.context, binding.action),
                        FindBinding(store_.KeyOverrides(scope_), binding.context, binding.action));
                    unavailable = Unavailable(binding);
                } else {
                    const auto field = visibleFields[i]->field;
                    label = ReadOnly(field) ? localization_.Text("settings.ui.world_seed")
                        : localization_.Text(View(MessageKey("settings.ui.label.", SettingName(field))));
                    value = DisplayValue(preview, field);
                    local = HasOverride(drafts_[Index()], field);
                    pending = !SameOverride(drafts_[Index()], store_.Overrides(scope_), field);
                    if (mode_ == EditorMode::Number && i == page.selected) { value = editText_ + "_"; }
                }
                const char* marker = pending ? "* " : unavailable ? "! " : local ? "= " : "  ";
                auto row = ftxui::hbox({ftxui::text(i == page.selected ? "> " : "  "), ftxui::text(marker),
                    ftxui::text(Frontend(label)) | ftxui::flex, ftxui::text(" "),
                    ftxui::text(Frontend(value)) | ftxui::size(ftxui::WIDTH, ftxui::LESS_THAN, 12), ftxui::text(" ")});
                if (pending || unavailable) { row = row | ftxui::color(ftxui::Color::Yellow); }
                if (i == page.selected) {
                    row = row | ftxui::bold;
                    if (focus_ == FocusArea::List && mode_ != EditorMode::Inspect) { row = row | ftxui::inverted; }
                }
                rows.push_back(row);
            }
        }
        auto list = std::make_shared<SettingsScrollPane>(ftxui::vbox(std::move(rows)), page.listScroll,
            static_cast<int>(mode_ == EditorMode::Language ? languageSelection_ : page.selected));
        auto details = std::make_shared<SettingsScrollPane>(Details(visibleFields, visibleBindings), page.detailScroll);
        auto detailsHeading = ftxui::text(T("settings.ui.details"));
        if (focus_ == FocusArea::Details || mode_ == EditorMode::Inspect) { detailsHeading = detailsHeading | ftxui::inverted; }
        auto body = std::make_shared<SettingsBody>(
            Panel(ftxui::text(T(mode_ == EditorMode::Language ? "settings.ui.languages"
                : Bindings() ? "settings.ui.actions" : "settings.ui.options")), list),
            Panel(detailsHeading, details), Tabs(FocusArea::Category),
            Panel(ftxui::text(T("settings.ui.categories")), Tabs(FocusArea::Category, true)),
            static_cast<int>(count), sidebarVisible_);
        auto title = ftxui::hbox({ftxui::text(T("settings.title")) | ftxui::bold,
            ftxui::text("  "), StateBadge(), ftxui::filler()});
        const auto footer = Footer();
        return ftxui::vbox({Padded(title), Tabs(FocusArea::Scope), Tabs(FocusArea::Section),
            ftxui::separatorLight(), body | ftxui::flex, ftxui::separatorLight(), Padded(footer)})
            | ftxui::borderLight;
    }

    bool OnEvent(ftxui::Event event) override {
        const eastl::string_view raw(event.input().data(), event.input().size());
        const auto chord = DecodeTerminalInput(raw);
        if (mode_ == EditorMode::Inspect) {
            if (chord && chord->key == "Escape" && !chord->ctrl) { mode_ = EditorMode::Browse; }
            else { inspected_ = InspectTerminalInput(raw); }
            return true;
        }
        if (mode_ == EditorMode::Capture) { Capture(chord); return true; }
        if (mode_ == EditorMode::Number) { EditNumber(event, chord); return true; }
        if (!chord) { return true; }
        const auto action = ResolveAction(store_.Keys(), InputContext::Menu, *chord);
        if (!action) { return true; }
        if (mode_ == EditorMode::Language) {
            if (*action == InputAction::Cancel) { mode_ = EditorMode::Browse; }
            else if (*action == InputAction::North) { languageSelection_ = (languageSelection_ + languages_.size() - 1) % languages_.size(); }
            else if (*action == InputAction::South) { languageSelection_ = (languageSelection_ + 1) % languages_.size(); }
            else if (*action == InputAction::Accept) {
                drafts_[Index()].language = languages_[languageSelection_]; mode_ = EditorMode::Browse; ClearFeedback();
            }
            return true;
        }
        switch (*action) {
        case InputAction::Cancel: if (onClose_) { onClose_(); } break;
        case InputAction::Next: MoveFocus(1); break;
        case InputAction::Previous: MoveFocus(-1); break;
        case InputAction::North: case InputAction::South:
            if (focus_ == FocusArea::Details) {
                Page().detailScroll = std::max(0, Page().detailScroll + (*action == InputAction::North ? -1 : 1));
            } else if (focus_ == FocusArea::Category && sidebarVisible_) {
                ChangeTab(*action == InputAction::North ? -1 : 1);
            } else if (focus_ == FocusArea::List) {
                const auto count = Bindings() ? VisibleBindings().size() : VisibleFields().size();
                Page().selected = *action == InputAction::North ? (Page().selected + count - 1) % count : (Page().selected + 1) % count;
                Page().detailScroll = 0;
            }
            break;
        case InputAction::West: case InputAction::East: ChangeTab(*action == InputAction::West ? -1 : 1); break;
        case InputAction::Accept:
            if (focus_ != FocusArea::List) { focus_ = FocusArea::List; }
            else { BeginEdit(false); }
            break;
        case InputAction::AddBinding: if (Bindings() && focus_ == FocusArea::List) { BeginEdit(true); } break;
        case InputAction::Reset: ResetSelected(); break;
        case InputAction::Unbind:
            if (Bindings() && focus_ == FocusArea::List) {
                auto binding = VisibleBindings()[Page().selected]; binding.keys.clear();
                SetBinding(keyDrafts_[Index()], eastl::move(binding)); ClearFeedback();
            }
            break;
        case InputAction::Inspect:
            mode_ = EditorMode::Inspect; inspected_.clear(); ClearFeedback(); Page().detailScroll = 0; break;
        case InputAction::Apply: Apply(); break;
        case InputAction::SaveSettings:
            if (Apply()) {
                Diagnostics diagnostics;
                if (store_.Save(scope_, &diagnostics)) { Success("settings.ui.saved"); }
                else { Error("settings.ui.save_failed", FormatDiagnostics(diagnostics)); }
            }
            break;
        case InputAction::Reload: Reload(); break;
        default: break;
        }
        return true;
    }

private:
    unsigned Index() const { return scope_ == SettingsScope::Global ? 0 : 1; }
    bool Bindings() const { return sections_[Index()] == 1; }
    unsigned Category() const { return categories_[Index()][sections_[Index()]]; }
    PageState& Page() { return pages_[Index()][sections_[Index()]][Category()]; }
    std::string T(eastl::string_view key) const { return Frontend(localization_.Text(key)); }
    Settings Preview() const {
        return scope_ == SettingsScope::Global ? ResolveSettingsForServer(server_, drafts_[0], {})
            : ResolveSettingsForServer(server_, store_.Overrides(SettingsScope::Global), drafts_[1]);
    }
    Keymap PreviewKeys() const {
        return scope_ == SettingsScope::Global ? ResolveKeymap(keyDrafts_[0], {})
            : ResolveKeymap(store_.KeyOverrides(SettingsScope::Global), keyDrafts_[1]);
    }
    bool Pending(unsigned index) const {
        const auto scope = index == 0 ? SettingsScope::Global : SettingsScope::World;
        return drafts_[index] != store_.Overrides(scope) || keyDrafts_[index] != store_.KeyOverrides(scope);
    }
    bool ReadOnly(SettingsField field) const { return scope_ == SettingsScope::World && field == SettingsField::Seed; }
    bool Unavailable(const InputBinding& binding) const {
        if (keyboard_.Enhanced()) { return false; }
        for (const auto& key : binding.keys) { if (!LegacyKeyAvailable(key)) { return true; } }
        return false;
    }
    Vector<const FieldPresentation*> VisibleFields() const {
        Vector<const FieldPresentation*> result;
        for (const auto& field : fields) { if (field.category == Category()) { result.push_back(&field); } }
        return result;
    }
    Vector<InputBinding> VisibleBindings() const {
        Vector<InputBinding> result;
        for (const auto& binding : PreviewKeys()) {
            if (binding.context == bindingContexts[Category()]) { result.push_back(binding); }
        }
        return result;
    }
    String DisplayValue(const Settings& settings, SettingsField field) const {
        if (ReadOnly(field)) { return UnsignedText(server_.GetWorld().GetSeed()); }
        if (field == SettingsField::RateLimited) { return localization_.Text(settings.pacing.rateLimited ? "settings.ui.on" : "settings.ui.off"); }
        if (field == SettingsField::PauseWhenIdle) { return localization_.Text(settings.pacing.pauseWhenIdle ? "settings.ui.on" : "settings.ui.off"); }
        if (field == SettingsField::ResidencyMode) {
            return localization_.Text(settings.residencyMode == ResidencyMode::Circle ? "settings.ui.circle" : "settings.ui.square");
        }
        if ((field == SettingsField::ResidentRadius && settings.residencyMode == ResidencyMode::Circle)
            || (field == SettingsField::ResidentRadiusTiles && settings.residencyMode == ResidencyMode::Square)) {
            return localization_.Text("settings.ui.inactive_radius");
        }
        return SettingValue(settings, field);
    }
    String Source(bool local, bool global) const {
        if (local) { return localization_.Text(scope_ == SettingsScope::Global ? "settings.ui.source_global" : "settings.ui.source_world"); }
        return localization_.Text(global ? "settings.ui.inherit_global" : "settings.ui.inherit_defaults");
    }
    ftxui::Element StateBadge() const {
        if (feedbackKind_ == FeedbackKind::Error && feedbackScope_ == scope_) {
            return ftxui::text(T(View(feedbackKey_))) | ftxui::color(ftxui::Color::Red) | ftxui::bold;
        }
        const bool pending = Pending(Index());
        const bool dirty = store_.Dirty(scope_);
        if (pending || dirty) {
            return ftxui::text(T(pending ? (dirty ? "settings.ui.unapplied_unsaved" : "settings.ui.unapplied") : "settings.ui.unsaved"))
                | ftxui::color(ftxui::Color::Yellow);
        }
        if (feedbackKind_ == FeedbackKind::Success && feedbackScope_ == scope_) {
            return ftxui::text(T(View(feedbackKey_))) | ftxui::color(ftxui::Color::Green);
        }
        return ftxui::text("");
    }
    ftxui::Element Tabs(FocusArea area, bool vertical = false) const {
        const unsigned selected = area == FocusArea::Scope ? Index()
            : area == FocusArea::Section ? sections_[Index()] : Category();
        const unsigned count = area == FocusArea::Category
            ? static_cast<unsigned>(Bindings() ? std::size(bindingContexts) : std::size(categoryNames)) : 2;
        ftxui::Elements entries;
        for (unsigned i = 0; i < count; ++i) {
            String label;
            if (area == FocusArea::Scope) { label = localization_.Text(i == 0 ? "settings.global" : "settings.world"); }
            else if (area == FocusArea::Section) { label = localization_.Text(i == 0 ? "settings.preferences" : "settings.bindings"); }
            else { label = localization_.Text(View(MessageKey(Bindings() ? "context." : "settings.ui.category.",
                Bindings() ? ContextName(bindingContexts[i]) : categoryNames[i]))); }
            if (area == FocusArea::Scope && (Pending(i) || store_.Dirty(i == 0 ? SettingsScope::Global : SettingsScope::World))) { label += '*'; }
            auto tab = ftxui::hbox({ftxui::text(i == selected ? "[" : " "),
                ftxui::text(Frontend(label)) | ftxui::xflex_shrink, ftxui::text(i == selected ? "]" : " ")})
                | ftxui::xflex_shrink;
            if (i == selected) {
                tab = tab | ftxui::bold;
                if (focus_ == area && mode_ == EditorMode::Browse) { tab = tab | ftxui::inverted; }
            }
            if (!vertical && !entries.empty()) { entries.push_back(ftxui::text(" ")); }
            entries.push_back(tab);
        }
        entries.push_back(ftxui::filler());
        return vertical ? ftxui::vbox(std::move(entries)) : Padded(ftxui::hbox(std::move(entries)));
    }
    ftxui::Element FixedHint(const String& key, eastl::string_view label) const {
        return ftxui::hbox({ftxui::text(Frontend(key)) | ftxui::bold,
            ftxui::text(" " + T(label)) | ftxui::xflex_shrink}) | ftxui::xflex_shrink;
    }
    ftxui::Element Hint(InputAction action, eastl::string_view label) const {
        return FixedHint(action == InputAction::Cancel ? String("Escape")
            : BindingHint(store_.Keys(), InputContext::Menu, action), label);
    }
    ftxui::Element Footer() const {
        if (mode_ != EditorMode::Browse) {
            const auto key = mode_ == EditorMode::Capture ? "settings.capture"
                : mode_ == EditorMode::Inspect ? "settings.inspecting"
                : mode_ == EditorMode::Language ? "settings.ui.choose_language" : "settings.ui.number_prompt";
            ftxui::Elements controls;
            if (mode_ == EditorMode::Number) { controls.push_back(FixedHint("Enter", "settings.ui.accept")); }
            else if (mode_ == EditorMode::Language) { controls.push_back(Hint(InputAction::Accept, "settings.ui.accept")); }
            controls.push_back(FixedHint("Escape", "settings.ui.cancel"));
            return ftxui::vbox({ftxui::text(T(key)), PackedControls(std::move(controls))});
        }
        return ftxui::vbox({
            PackedControls({Hint(InputAction::Accept, "settings.ui.edit"), Hint(InputAction::Apply, "settings.ui.apply"),
                Hint(InputAction::SaveSettings, "settings.ui.save"), Hint(InputAction::Reset, "settings.ui.reset")}),
            PackedControls({Hint(InputAction::Next, "settings.ui.focus"),
                Hint(Bindings() ? InputAction::Inspect : InputAction::Reload, Bindings() ? "settings.ui.inspect" : "settings.ui.reload"),
                Hint(InputAction::Cancel, "settings.ui.close")})});
    }
    void DetailLine(ftxui::Elements& lines, eastl::string_view key, const String& value,
        ftxui::Color color = ftxui::Color::Default) const {
        lines.push_back(ftxui::paragraph(Frontend(localization_.Format(key, {{"value", value}}))) | ftxui::color(color));
    }
    ftxui::Element DetailTitle(const String& title) const {
        return ftxui::paragraph(Frontend(title)) | ftxui::bold;
    }
    ftxui::Element Details(const Vector<const FieldPresentation*>& visibleFields, const Vector<InputBinding>& visibleBindings) {
        ftxui::Elements lines;
        if (mode_ == EditorMode::Inspect) {
            lines.push_back(ftxui::text(T("settings.ui.inspection_title")) | ftxui::bold);
            lines.push_back(ftxui::paragraph(T("settings.ui.inspection_help")));
            lines.push_back(ftxui::text(T(keyboard_.Enhanced() ? "input.enhanced" : "input.legacy")));
            lines.push_back(ftxui::paragraph(Frontend(inspected_)));
            return ftxui::vbox(std::move(lines));
        }
        if (feedbackScope_ == scope_ && feedbackKind_ != FeedbackKind::None) {
            lines.push_back(ftxui::text(T(View(feedbackKey_))) | ftxui::color(
                feedbackKind_ == FeedbackKind::Error ? ftxui::Color::Red : ftxui::Color::Green));
            if (!diagnostics_.empty()) { lines.push_back(ftxui::paragraph(Frontend(diagnostics_)) | ftxui::color(ftxui::Color::Red)); }
        }
        if (Bindings()) {
            const auto& binding = visibleBindings[Page().selected];
            const auto name = ActionName(binding.action);
            lines.push_back(DetailTitle(localization_.Text(View(MessageKey("action.", name)))));
            lines.push_back(ftxui::paragraph(Frontend(localization_.Text(View(MessageKey("settings.ui.action_help.", name))))));
            lines.push_back(ftxui::paragraph(Frontend(localization_.Text(View(MessageKey("settings.ui.context_help.", ContextName(binding.context)))))));
            const auto* local = FindBinding(keyDrafts_[Index()], binding.context, binding.action);
            const bool global = scope_ == SettingsScope::World
                && FindBinding(store_.KeyOverrides(SettingsScope::Global), binding.context, binding.action);
            lines.push_back(ftxui::separatorLight());
            DetailLine(lines, "settings.ui.bindings_value", BindingHint(PreviewKeys(), binding.context, binding.action));
            DetailLine(lines, "settings.ui.source", Source(local != nullptr, global));
            const auto inherited = scope_ == SettingsScope::Global ? DefaultKeymap() : ResolveKeymap(store_.KeyOverrides(SettingsScope::Global), {});
            DetailLine(lines, "settings.ui.inherited_value", BindingHint(inherited, binding.context, binding.action));
            if (!SameBinding(local, FindBinding(store_.KeyOverrides(scope_), binding.context, binding.action))) {
                const auto applied = scope_ == SettingsScope::Global ? ResolveKeymap(store_.KeyOverrides(scope_), {}) : store_.Keys();
                DetailLine(lines, "settings.ui.applied_value", BindingHint(applied, binding.context, binding.action), ftxui::Color::Yellow);
            }
            if (Unavailable(binding)) { lines.push_back(ftxui::paragraph(T("input.legacy")) | ftxui::color(ftxui::Color::Yellow)); }
            lines.push_back(ftxui::separatorLight());
            lines.push_back(ftxui::paragraph(T("settings.ui.binding_effect")));
            lines.push_back(ftxui::paragraph(Frontend(localization_.Format("settings.ui.binding_controls", {
                {"add", BindingHint(store_.Keys(), InputContext::Menu, InputAction::AddBinding)},
                {"unbind", BindingHint(store_.Keys(), InputContext::Menu, InputAction::Unbind)},
                {"reload", BindingHint(store_.Keys(), InputContext::Menu, InputAction::Reload)}}))));
        } else {
            const auto field = visibleFields[Page().selected]->field;
            const auto name = SettingName(field);
            lines.push_back(DetailTitle(ReadOnly(field) ? localization_.Text("settings.ui.world_seed")
                : localization_.Text(View(MessageKey("settings.ui.label.", name)))));
            lines.push_back(ftxui::paragraph(Frontend(localization_.Text(View(MessageKey("settings.ui.description.", name))))));
            lines.push_back(ftxui::separatorLight());
            DetailLine(lines, "settings.ui.edit_value", mode_ == EditorMode::Number ? editText_ + "_" : DisplayValue(Preview(), field));
            if (ReadOnly(field)) {
                DetailLine(lines, "settings.ui.source", localization_.Text("settings.ui.saved_world"));
                lines.push_back(ftxui::paragraph(T("settings.ui.seed_fixed")));
                DetailLine(lines, "settings.ui.generation", UnsignedText(server_.GetWorld().GetGenerationVersion()));
            } else {
                const bool local = SuppliesField(drafts_[Index()], field);
                const bool global = scope_ == SettingsScope::World && SuppliesField(store_.Overrides(SettingsScope::Global), field);
                DetailLine(lines, "settings.ui.source", ResidencyField(field) && !local && !global
                    ? localization_.Text("settings.ui.source_runtime") : Source(local, global));
                const auto inherited = scope_ == SettingsScope::Global ? ResolveSettingsForServer(server_, {}, {})
                    : ResolveSettingsForServer(server_, store_.Overrides(SettingsScope::Global), {});
                DetailLine(lines, "settings.ui.inherited_value", DisplayValue(inherited, field));
                if (!SameOverride(drafts_[Index()], store_.Overrides(scope_), field)) {
                    const auto applied = scope_ == SettingsScope::Global ? ResolveSettingsForServer(server_, store_.Overrides(scope_), {})
                        : ResolveSettingsForServer(server_, store_.Overrides(SettingsScope::Global), store_.Overrides(SettingsScope::World));
                    DetailLine(lines, "settings.ui.applied_value", DisplayValue(applied, field), ftxui::Color::Yellow);
                }
                if (scope_ == SettingsScope::Global && HasOverride(store_.Overrides(SettingsScope::World), field)) {
                    lines.push_back(ftxui::paragraph(T("settings.ui.world_masks_global")));
                }
                if (ResidencyField(field)) {
                    lines.push_back(ftxui::paragraph(T("settings.ui.residency_inheritance")));
                    DetailLine(lines, "settings.ui.runtime_policy", DisplayValue(ResolveSettingsForServer(server_, {}, {}), SettingsField::ResidencyMode));
                }
            }
            lines.push_back(ftxui::separatorLight());
            lines.push_back(ftxui::paragraph(Frontend(localization_.Text(View(MessageKey("settings.ui.constraint.", name))))));
            lines.push_back(ftxui::paragraph(Frontend(localization_.Text(View(MessageKey("settings.ui.effect.", name))))));
            lines.push_back(ftxui::paragraph(T(field == SettingsField::Seed ? "settings.ui.when_created" : "settings.ui.when_applied")));
        }
        if (scope_ == SettingsScope::World && store_.WorldDirectory().empty()) { lines.push_back(ftxui::paragraph(T("settings.ui.memory_only"))); }
        lines.push_back(ftxui::text(""));
        lines.push_back(ftxui::paragraph(Frontend(localization_.Format("settings.ui.navigation", {
            {"next", BindingHint(store_.Keys(), InputContext::Menu, InputAction::Next)},
            {"previous", BindingHint(store_.Keys(), InputContext::Menu, InputAction::Previous)}}))));
        return ftxui::vbox(std::move(lines));
    }
    void MoveFocus(int delta) {
        const int count = static_cast<int>(FocusArea::Count);
        focus_ = static_cast<FocusArea>((static_cast<int>(focus_) + delta + count) % count);
    }
    void ChangeTab(int delta) {
        if (focus_ == FocusArea::Scope) { scope_ = scope_ == SettingsScope::Global ? SettingsScope::World : SettingsScope::Global; }
        else if (focus_ == FocusArea::Section) { sections_[Index()] = 1 - sections_[Index()]; }
        else if (focus_ == FocusArea::Category) {
            auto& category = categories_[Index()][sections_[Index()]];
            const int count = static_cast<int>(Bindings() ? std::size(bindingContexts) : std::size(categoryNames));
            category = static_cast<unsigned>((static_cast<int>(category) + delta + count) % count);
        }
    }
    void BeginEdit(bool append) {
        ClearFeedback();
        if (Bindings()) { mode_ = EditorMode::Capture; append_ = append; return; }
        const auto* presentation = VisibleFields()[Page().selected];
        const auto field = presentation->field;
        if (ReadOnly(field)) { return; }
        if (presentation->editor == EditorKind::ResidencyMode) {
            auto& draft = drafts_[Index()];
            draft.residencyMode = Preview().residencyMode == ResidencyMode::Circle ? ResidencyMode::Square : ResidencyMode::Circle;
            if (*draft.residencyMode == ResidencyMode::Square) { draft.residentRadiusTiles.reset(); }
            else { draft.residentRadius.reset(); }
        } else if (presentation->editor == EditorKind::Boolean) {
            const auto value = SettingValue(Preview(), field) == "true" ? "false" : "true";
            if (!SetOverride(drafts_[Index()], field, value)) { std::abort(); }
        } else if (presentation->editor == EditorKind::Language) {
            languages_ = AvailableLanguages(directory_.c_str()); languageSelection_ = 0;
            const auto current = Preview().language;
            for (std::size_t i = 0; i < languages_.size(); ++i) { if (languages_[i] == current) { languageSelection_ = i; } }
            mode_ = EditorMode::Language;
        } else { editText_ = SettingValue(Preview(), field); replaceOnType_ = true; mode_ = EditorMode::Number; }
        Page().detailScroll = 0;
    }
    void Capture(const eastl::optional<KeyChord>& chord) {
        if (chord && chord->key == "Escape" && !chord->ctrl) { mode_ = EditorMode::Browse; ClearFeedback(); return; }
        if (!chord) { Error("settings.ui.binding_failed", localization_.Text("input.unsupported")); return; }
        if (!keyboard_.Enhanced() && !LegacyKeyAvailable(*chord)) { Error("settings.ui.binding_failed", localization_.Text("input.unavailable")); return; }
        auto binding = VisibleBindings()[Page().selected];
        if (!append_) { binding.keys.clear(); }
        binding.keys.push_back(*chord);
        auto candidate = keyDrafts_[Index()]; SetBinding(candidate, binding);
        const auto resolved = scope_ == SettingsScope::Global ? ResolveKeymap(candidate, {})
            : ResolveKeymap(store_.KeyOverrides(SettingsScope::Global), candidate);
        Diagnostics diagnostics;
        if (!ValidateKeymap(resolved, &diagnostics)) { Error("settings.ui.binding_failed", FormatDiagnostics(diagnostics)); return; }
        keyDrafts_[Index()] = eastl::move(candidate); mode_ = EditorMode::Browse; ClearFeedback();
    }
    void EditNumber(const ftxui::Event& event, const eastl::optional<KeyChord>& chord) {
        if (chord && chord->key == "Escape" && !chord->ctrl) { mode_ = EditorMode::Browse; ClearFeedback(); return; }
        if (chord && chord->key == "Enter" && !chord->ctrl) {
            const auto* field = VisibleFields()[Page().selected];
            std::uint64_t number = 0;
            const auto parsed = std::from_chars(editText_.data(), editText_.data() + editText_.size(), number);
            if (parsed.ec != std::errc{} || parsed.ptr != editText_.data() + editText_.size()
                || number < field->minimum || number > field->maximum) {
                Error("settings.ui.invalid_value", localization_.Format("settings.ui.number_range", {
                    {"min", UnsignedText(field->minimum)}, {"max", UnsignedText(field->maximum)}})); return;
            }
            Diagnostics diagnostics;
            const auto result = SetOverride(drafts_[Index()], field->field, View(editText_), &diagnostics);
            if (!result) { Error("settings.ui.invalid_value", FormatDiagnostics(diagnostics)); return; }
            if (field->field == SettingsField::ResidentRadius) {
                drafts_[Index()].residentRadiusTiles.reset();
                drafts_[Index()].residencyMode.reset();
            } else if (field->field == SettingsField::ResidentRadiusTiles) {
                drafts_[Index()].residentRadius.reset();
                drafts_[Index()].residencyMode.reset();
            }
            mode_ = EditorMode::Browse; ClearFeedback();
        } else if (chord && chord->key == "Backspace" && !chord->ctrl) {
            if (!editText_.empty()) {
                std::size_t start = editText_.size() - 1;
                while (start > 0 && (static_cast<unsigned char>(editText_[start]) & 0xc0) == 0x80) { --start; }
                editText_.resize(start);
            }
            replaceOnType_ = false;
        } else if (event.is_character()) {
            const auto& text = event.input();
            if (replaceOnType_) { editText_.clear(); replaceOnType_ = false; }
            if (editText_.size() + text.size() <= 128) { editText_.append(text.data(), text.size()); }
        }
    }
    void ResetSelected() {
        if (focus_ != FocusArea::List) { return; }
        if (Bindings()) {
            const auto binding = VisibleBindings()[Page().selected];
            ResetBinding(keyDrafts_[Index()], binding.context, binding.action);
        } else {
            const auto field = VisibleFields()[Page().selected]->field;
            if (ReadOnly(field)) { return; }
            ResetOverride(drafts_[Index()], field);
        }
        ClearFeedback();
    }
    void RefreshDrafts() {
        for (unsigned i = 0; i < 2; ++i) {
            const auto scope = i == 0 ? SettingsScope::Global : SettingsScope::World;
            drafts_[i] = store_.Overrides(scope); keyDrafts_[i] = store_.KeyOverrides(scope);
        }
    }
    void ClearFeedback() { feedbackKind_ = FeedbackKind::None; feedbackKey_.clear(); diagnostics_.clear(); }
    void Success(eastl::string_view key) {
        feedbackKind_ = FeedbackKind::Success; feedbackScope_ = scope_;
        feedbackKey_.assign(key.data(), key.size()); diagnostics_.clear(); Page().detailScroll = 0;
    }
    void Error(eastl::string_view key, String diagnostics) {
        feedbackKind_ = FeedbackKind::Error; feedbackScope_ = scope_;
        feedbackKey_.assign(key.data(), key.size()); diagnostics_ = eastl::move(diagnostics); Page().detailScroll = 0;
    }
    bool Install(SettingsStore candidate) {
        Diagnostics diagnostics;
        Localization translated;
        const auto& settings = candidate.Effective();
        const auto loaded = translated.Load(directory_.c_str(), View(settings.language), &diagnostics);
        if (!loaded || !ApplySettings(server_, candidate, &diagnostics)) { Error("settings.ui.apply_failed", FormatDiagnostics(diagnostics)); return false; }
        store_ = eastl::move(candidate); localization_ = eastl::move(translated);
        Success("settings.ui.applied"); return true;
    }
    bool Apply() {
        auto candidate = store_;
        Diagnostics diagnostics;
        if (!candidate.SetOverrides(scope_, drafts_[Index()], keyDrafts_[Index()], &diagnostics)) {
            Error("settings.ui.apply_failed", FormatDiagnostics(diagnostics)); return false;
        }
        return Install(eastl::move(candidate));
    }
    void Reload() {
        auto candidate = store_;
        Diagnostics diagnostics;
        if (!candidate.Load(&diagnostics)) { Error("settings.ui.reload_failed", FormatDiagnostics(diagnostics)); return; }
        if (Install(eastl::move(candidate))) { RefreshDrafts(); Success("settings.ui.reloaded"); }
    }

    SettingsStore& store_;
    Localization& localization_;
    Server& server_;
    TerminalKeyboard& keyboard_;
    String directory_;
    std::function<void()> onClose_;
    SettingsScope scope_ = SettingsScope::Global;
    FocusArea focus_ = FocusArea::List;
    EditorMode mode_ = EditorMode::Browse;
    SettingsOverrides drafts_[2];
    Keymap keyDrafts_[2];
    unsigned sections_[2] = {0, 0};
    unsigned categories_[2][2] = {};
    PageState pages_[2][2][std::size(bindingContexts)];
    bool append_ = false;
    bool replaceOnType_ = false;
    bool sidebarVisible_ = false;
    Vector<String> languages_;
    std::size_t languageSelection_ = 0;
    String editText_;
    String inspected_;
    FeedbackKind feedbackKind_ = FeedbackKind::None;
    SettingsScope feedbackScope_ = SettingsScope::Global;
    String feedbackKey_;
    String diagnostics_;
};

}

ftxui::Component CreateSettingsMenu(SettingsStore& store, Localization& localization,
    Server& server, TerminalKeyboard& keyboard, const char* localizationDirectory,
    std::function<void()> onClose) {
    return ftxui::Make<SettingsMenu>(store, localization, server, keyboard, localizationDirectory, std::move(onClose));
}

}
