#pragma once

#include "client/localization.h"

namespace cataclysmx {

enum class SettingsEditEvent { None, Close, Save };
enum class SettingsTone { Normal, Muted, Global, World, Pending, Warning, Success, Error };

struct SettingsDetail {
    String text;
    SettingsTone tone = SettingsTone::Normal;
    bool divider = false;
};

struct SettingsMenuRow {
    String label;
    String value;
    SettingsTone tone = SettingsTone::Normal;
    bool divider = false;
    bool button = false;
    bool readOnly = false;
};

struct SettingsMenuFrame {
    Vector<SettingsMenuRow> rows;
    Vector<SettingsDetail> details;
    Vector<String> hints;
    String activeTab;
    String compactHint;
    String tabs;
    String status;
    SettingsTone statusTone = SettingsTone::Normal;
    SettingsTone scopeTone = SettingsTone::Global;
    int selected = 0;
};

// A keyboard-only editor over one sparse configuration layer. The frontend
// supplies its runtime snapshot, owns its viewports, and commits a Save event.
class SettingsEditor {
public:
    void Open(const SettingsStore& store, const Settings& runtime, bool sdl, bool guest = false);
    void Applied(const SettingsStore& store, const Settings& runtime);
    SettingsEditEvent Key(const eastl::optional<KeyChord>& chord, const SettingsStore& store,
        const Localization& loc, const char* directory, bool enhanced = true, String inspected = {});
    void Text(eastl::string_view text);
    SettingsMenuFrame Present(const SettingsStore& store, const Localization& loc, bool enhanced = true) const;
    SettingsResult<SettingsStore> Prepare(const SettingsStore& store, Diagnostics* diagnostics) const;
    void Feedback(String message, SettingsTone tone);
    SettingsScope Scope() const { return scope_; }
    bool Capturing() const { return mode_ == Mode::Capture; }
    bool Inspecting() const { return mode_ == Mode::Inspect; }
    bool EditingText() const { return mode_ == Mode::Number; }
    bool Browsing() const { return mode_ == Mode::Browse; }
    int& ListScroll();
    int ListScroll() const;
    int& DetailScroll();
    int DetailScroll() const { return Current().detailScroll; }
private:
    enum class Mode { Browse, Number, Language, Capture, Inspect };
    struct Page {
        int selected = 2;
        int listScroll = 0;
        int detailScroll = 0;
        std::size_t chord = 0;
    };
    struct Row {
        int section;
        int item; // -2 scope button, -1 section divider, otherwise a field or binding index.
    };
    Page& Current() { return pages_[bindings_]; }
    const Page& Current() const { return pages_[bindings_]; }
    Settings Preview(const SettingsStore& store) const;
    void LoadScope(const SettingsStore& store);
    void BuildRows(const SettingsStore& store);
    void Move(int direction, bool section);
    void EditField(SettingsField field, int direction, const SettingsStore& store,
        const Localization& loc, const char* directory);
    bool SetField(SettingsField field, const String& value, const Localization& loc);
    void ChangeBinding(const SettingsStore& store, InputBinding binding, const Localization& loc);
    bool ReadOnly(SettingsField field) const;
    SettingsOverrides draft_;
    Keymap keys_;
    Keymap resolved_;
    Keymap bindingRows_;
    Vector<Row> rows_;
    Settings runtime_;
    SettingsScope scope_ = SettingsScope::Global;
    Page pages_[2];
    int languageScroll_ = 0;
    Mode mode_ = Mode::Browse;
    bool bindings_ = false;
    bool sdl_ = false;
    bool guest_ = false;
    bool replace_ = false;
    String text_;
    String feedback_;
    SettingsTone feedbackTone_ = SettingsTone::Normal;
    Vector<String> languages_;
    std::size_t language_ = 0;
    std::size_t captureSlot_ = 0;
};

}
