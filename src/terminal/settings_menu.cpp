#include "terminal/settings_menu.h"
#include "client/settings_editor.h"

#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/node.hpp>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

namespace cataclysmx {
namespace {

eastl::string_view View(const String& text) { return {text.data(), text.size()}; }
std::string Frontend(const String& text) { return {text.data(), text.size()}; }
ftxui::Element Text(const String& text) { return ftxui::text(Frontend(text)); }
ftxui::Element Padded(ftxui::Element content) {
    return ftxui::hbox({ftxui::text(" "), std::move(content) | ftxui::flex, ftxui::text(" ")});
}

ftxui::Color ToneColor(SettingsTone tone) {
    switch (tone) {
    case SettingsTone::Muted: return ftxui::Color::GrayLight;
    case SettingsTone::Global: return ftxui::Color::CyanLight;
    case SettingsTone::World: return ftxui::Color::MagentaLight;
    case SettingsTone::Pending: case SettingsTone::Warning: return ftxui::Color::Yellow;
    case SettingsTone::Success: return ftxui::Color::GreenLight;
    case SettingsTone::Error: return ftxui::Color::RedLight;
    case SettingsTone::Normal: return ftxui::Color::Default;
    }
    return ftxui::Color::Default;
}

ftxui::Element SettingsDivider(const String& label, SettingsTone tone) {
    return ftxui::hbox({Text(label) | ftxui::bold, ftxui::text(" "), ftxui::separatorLight() | ftxui::flex})
        | ftxui::color(ToneColor(tone));
}

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

// Choose the layout from the allocated box, including the first frame and resizes.
class SettingsBody final : public ftxui::Node {
public:
    SettingsBody(ftxui::Element list, ftxui::Element details)
        : Node({std::move(list), std::move(details), ftxui::separatorLight()}) {}
    void ComputeRequirement() override {
        Node::ComputeRequirement();
        requirement_.min_x = 0; requirement_.min_y = 0;
        requirement_.flex_grow_x = 1; requirement_.flex_grow_y = 1;
        requirement_.flex_shrink_x = 1; requirement_.flex_shrink_y = 1;
    }
    void SetBox(ftxui::Box box) override {
        Node::SetBox(box);
        auto list = box; auto details = box; auto divider = box;
        const int width = box.x_max - box.x_min + 1;
        const int height = box.y_max - box.y_min + 1;
        if (width >= 98) {
            list.x_max = box.x_min + width * 2 / 3 - 1;
            divider.x_min = list.x_max + 1; divider.x_max = divider.x_min;
            details.x_min = divider.x_min + 1;
        } else {
            list.y_max = box.y_min + std::max(1, height * 3 / 5) - 1;
            divider.y_min = list.y_max + 1; divider.y_max = divider.y_min;
            details.y_min = divider.y_min + 1;
        }
        children_[0]->SetBox(list);
        children_[1]->SetBox(details);
        children_[2]->SetBox(divider);
    }
};

class SettingsMenu final : public ftxui::ComponentBase {
public:
    SettingsMenu(SettingsStore& store, Localization& localization, Server& server,
        TerminalKeyboard& keyboard, const char* directory, std::function<void()> onClose)
        : store_(store), localization_(localization), server_(server), keyboard_(keyboard),
          directory_(directory), onClose_(std::move(onClose)) {
        editor_.Open(store_, Runtime(), false);
    }

    bool Focusable() const override { return true; }
    ftxui::Element OnRender() override {
        const auto menu = editor_.Present(store_, localization_, keyboard_.Enhanced());
        ftxui::Elements rows;
        for (std::size_t i = 0; i < menu.rows.size(); ++i) {
            const auto& row = menu.rows[i];
            if (row.divider) {
                rows.push_back(SettingsDivider(row.label, menu.scopeTone));
                continue;
            }
            const bool selected = static_cast<int>(i) == menu.selected;
            const char* marker = row.tone == SettingsTone::Pending ? "* " : row.tone == SettingsTone::Global ? "G "
                : row.tone == SettingsTone::World ? "W " : row.tone == SettingsTone::Warning ? "! " : "  ";
            const auto label = row.button ? "[" + row.label + "]" : row.label;
            auto line = ftxui::hbox({ftxui::text(selected ? "> " : "  "), ftxui::text(row.button ? "  " : marker) | ftxui::color(ToneColor(row.tone)),
                Text(label) | ftxui::color(ToneColor(row.button ? menu.scopeTone : SettingsTone::Normal)) | ftxui::flex, ftxui::text("  "),
                Text(row.value) | ftxui::xflex_shrink | ftxui::color(ToneColor(row.tone))});
            if (selected) { line = line | ftxui::bold | ftxui::inverted; }
            rows.push_back(std::move(line));
        }
        ftxui::Elements details;
        for (const auto& line : menu.details) {
            if (line.divider) { details.push_back(SettingsDivider(line.text, line.tone)); }
            else { details.push_back(ftxui::paragraph(Frontend(line.text)) | ftxui::color(ToneColor(line.tone))); }
        }
        ftxui::Elements footer;
        for (const auto& hint : menu.hints) { footer.push_back(ftxui::paragraph(Frontend(hint))); }
        auto list = std::make_shared<SettingsScrollPane>(ftxui::vbox(std::move(rows)), editor_.ListScroll(), menu.selected);
        auto pane = std::make_shared<SettingsScrollPane>(ftxui::vbox(std::move(details)), editor_.DetailScroll());
        auto body = std::make_shared<SettingsBody>(Padded(list), Padded(pane));
        const auto scope = localization_.Text(editor_.Scope() == SettingsScope::Global ? "settings.global" : "settings.world");
        auto heading = ftxui::hbox({Text(localization_.Text("settings.title") + "  ·  ") | ftxui::bold,
            Text(scope) | ftxui::bold | ftxui::color(ToneColor(menu.scopeTone)),
            ftxui::text("   "), Text(menu.status) | ftxui::color(ToneColor(menu.statusTone)) | ftxui::xflex_shrink, ftxui::filler()});
        return ftxui::vbox({Padded(heading), Padded(Text(menu.tabs) | ftxui::bold),
            ftxui::separatorLight(), body | ftxui::flex, ftxui::separatorLight(), Padded(ftxui::vbox(std::move(footer)))})
            | ftxui::borderLight;
    }

    bool OnEvent(ftxui::Event event) override {
        const auto& input = event.input();
        const eastl::string_view raw(input.data(), input.size());
        const auto chord = DecodeTerminalInput(raw);
        if (editor_.EditingText() && event.is_character()
            && chord != KeyChord{"Escape"} && chord != KeyChord{"Backspace"} && chord != KeyChord{"Enter"}) {
            editor_.Text(raw);
            return true;
        }
        const auto inspected = editor_.Inspecting() ? InspectTerminalInput(raw) : String{};
        switch (editor_.Key(chord, store_, localization_, directory_.c_str(), keyboard_.Enhanced(), inspected)) {
        case SettingsEditEvent::Save: Save(); break;
        case SettingsEditEvent::Close: if (onClose_) { onClose_(); } break;
        case SettingsEditEvent::None: break;
        }
        return true;
    }
private:
    Settings Runtime() const {
        auto runtime = ResolveSettingsForServer(server_, {}, {});
        runtime.seed = server_.GetWorld().GetSeed();
        return runtime;
    }
    void Save() {
        const auto scope = editor_.Scope();
        Diagnostics diagnostics;
        auto candidate = editor_.Prepare(store_, &diagnostics);
        if (!candidate) {
            editor_.Feedback(localization_.Text("settings.ui.apply_failed") + ": " + FormatDiagnostics(diagnostics), SettingsTone::Error);
            return;
        }
        if (candidate->Overrides(scope) != store_.Overrides(scope)
            || candidate->KeyOverrides(scope) != store_.KeyOverrides(scope)) {
            const bool languageChanged = candidate->Effective().language != store_.Effective().language;
            Localization translated;
            if ((languageChanged && !translated.Load(directory_.c_str(), View(candidate->Effective().language), &diagnostics))
                || !ApplySettings(server_, *candidate, &diagnostics)) {
                editor_.Feedback(localization_.Text("settings.ui.apply_failed") + ": " + FormatDiagnostics(diagnostics), SettingsTone::Error);
                return;
            }
            store_ = eastl::move(*candidate);
            if (languageChanged) { localization_ = eastl::move(translated); }
            editor_.Applied(store_, Runtime());
        }
        if (scope == SettingsScope::World && store_.WorldDirectory().empty()) {
            editor_.Feedback(localization_.Text("settings.ui.session_only"), SettingsTone::Success);
        } else if (store_.Dirty(scope) && !store_.Save(scope, &diagnostics)) {
            editor_.Feedback(localization_.Text("settings.ui.unsaved") + ": " + FormatDiagnostics(diagnostics), SettingsTone::Error);
        } else { editor_.Feedback(localization_.Text("settings.ui.saved"), SettingsTone::Success); }
    }

    SettingsStore& store_;
    Localization& localization_;
    Server& server_;
    TerminalKeyboard& keyboard_;
    String directory_;
    std::function<void()> onClose_;
    SettingsEditor editor_;
};

}

ftxui::Component CreateSettingsMenu(SettingsStore& store, Localization& localization,
    Server& server, TerminalKeyboard& keyboard, const char* localizationDirectory,
    std::function<void()> onClose) {
    return ftxui::Make<SettingsMenu>(store, localization, server, keyboard, localizationDirectory, std::move(onClose));
}

}
