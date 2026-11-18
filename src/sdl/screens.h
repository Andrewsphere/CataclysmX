#pragma once

#include "client/localization.h"
#include "client/combat_menu.h"
#include "client/worldgen_debug.h"
#include "core/static_list.h"
#include "sdl/text.h"
#include "sdl/layout.h"

namespace cataclysmx::sdl {
struct ScreenHealthRow {
    CombatPartView state;
    String value;
};
struct ScreenRow {
    String text;
    bool unavailable = false;
    eastl::optional<ScreenHealthRow> health = {};
};
enum class ScreenPresentation { Large, Anchored };
struct ScreenFrame {
    bool visible = false;
    ScreenPresentation presentation = ScreenPresentation::Large;
    bool followCursor = false;
    bool scrollText = false; // Continuous wrapped rows; scroll is measured in text lines.
    String title;
    String footer;
    String compactTitle; // Optional chrome when large text leaves too little body space.
    String compactFooter;
    Vector<ScreenRow> rows;
    Vector<int> rowHeights; // Reused popup measurement storage.
    Vector<String> details;
    int selected = -1;
    int scroll = 0;
    int detailScroll = 0;
    eastl::optional<TileCoord> cursor;
    Vector<TileCoord> highlights;
    const DebugMapView* debugImage = nullptr;
};
// Screen coordinates are logical UI pixels. Never retains the supplied frame.
bool DrawScreen(SDL_Renderer* renderer, SdlText& text, ScreenFrame& frame,
    int width, int height, const WorldView* view, int tileWidth, int tileHeight, int zoom, float uiScale,
    const GameplayLayout* gameplay = nullptr);

eastl::optional<std::size_t> SoleAvailableAction(const Vector<InteractionChoice>& actions);

// Snapshot-only combat interaction. Only Accept returns an authoritative command.
class CombatUi {
public:
    void Refresh(const ContentCatalog& content, const WorldView& view);
    void MoveSelection(int delta);
    bool SelectHotkey(InputAction action, const ContentCatalog& content);
    bool BeginChoice(TileCoord origin);
    bool Targeting() const { return pendingAbility_ && pendingAbility_->ability.target.kind != AbilityTargetKind::Self; }
    void MoveTarget(InputAction action);
    void CancelTarget() { pendingAbility_.reset(); }
    eastl::optional<Command> Accept();
    void Present(ScreenFrame& frame) const;
private:
    Vector<CombatMenuEntry> rows_;
    std::size_t selected_ = 0;
    eastl::optional<Command> pendingAbility_;
    TileCoord origin_{};
    String feedback_;
};
void PresentCombatInfo(const ContentCatalog& content, const WorldView& view, ScreenFrame& frame);

// Serial scope is the session generation, not the character's lifetime.
class CombatEventCursor {
public:
    void Drain(const ContentCatalog& content, const WorldView& view, std::uint64_t generation,
        Vector<String>& messages);
private:
    std::uint64_t generation_ = 0;
    std::uint64_t serial_ = 0;
};

enum class OptionsEvent { None, Close, Save, Multiplayer };
class OptionsUi {
public:
    void Open(const SettingsStore& store, const Settings& runtime, bool remote = false);
    void Applied(const SettingsStore& store, const Settings& runtime, bool reloaded);
    OptionsEvent Key(const KeyChord& chord, const SettingsStore& store,
        const Localization& localization, const char* directory);
    void Text(eastl::string_view text);
    void Present(const SettingsStore& store, const Localization& localization, ScreenFrame& frame) const;
    SettingsResult<void> Prepare(const SettingsStore& store, SettingsStore& candidate,
        Diagnostics* diagnostics) const;
    SettingsScope Scope() const { return scope_; }
    bool Capturing() const { return mode_ == Mode::Capture; }
    bool EditingText() const { return mode_ == Mode::Number; }
    void Feedback(String message) { feedback_ = eastl::move(message); }
private:
    enum class Mode { Browse, Number, Language, Capture, Inspect };
    unsigned Index() const { return scope_ == SettingsScope::Global ? 0 : 1; }
    Settings Preview(const SettingsStore& store) const;
    Keymap PreviewKeys(const SettingsStore& store) const;
    using VisibleFieldList = StaticList<SettingsField, static_cast<std::size_t>(SettingsField::Count)>;
    void Fields(VisibleFieldList& output) const;
    void Bindings(const SettingsStore& store, Keymap& output) const;
    void Begin(const SettingsStore& store, const char* directory, bool append);
    SettingsOverrides drafts_[2];
    Keymap keys_[2];
    Settings runtime_;
    SettingsScope scope_ = SettingsScope::Global;
    bool bindings_ = false;
    bool remote_ = false;
    unsigned category_ = 0;
    unsigned focus_ = 3; // Scope, section, category, list, details.
    std::size_t selected_ = 0;
    int detailScroll_ = 0;
    Mode mode_ = Mode::Browse;
    bool append_ = false;
    bool replace_ = false;
    String edit_;
    String feedback_;
    Vector<String> languages_;
    std::size_t language_ = 0;
};
}
