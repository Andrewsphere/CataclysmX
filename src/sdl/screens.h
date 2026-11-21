#pragma once

#include "client/localization.h"
#include "client/combat_menu.h"
#include "client/settings_editor.h"
#include "client/worldgen_debug.h"
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
    String value = {};
    SettingsTone tone = SettingsTone::Normal;
    bool divider = false;
    bool button = false;
};
enum class ScreenPresentation { Large, Anchored };
struct ScreenFrame {
    bool visible = false;
    ScreenPresentation presentation = ScreenPresentation::Large;
    bool followCursor = false;
    bool scrollText = false; // Continuous wrapped rows; scroll is measured in text lines.
    bool structuredRows = false;
    String title;
    String footer;
    String compactTitle; // Optional chrome when large text leaves too little body space.
    String compactFooter;
    String status;
    SettingsTone statusTone = SettingsTone::Normal;
    SettingsTone scopeTone = SettingsTone::Normal;
    Vector<ScreenRow> rows;
    Vector<int> rowHeights; // Reused popup measurement storage.
    Vector<String> details;
    Vector<SettingsDetail> settingsDetails;
    int selected = -1;
    int scroll = 0;
    int detailScroll = 0;
    eastl::optional<TileCoord> cursor;
    Vector<TileCoord> highlights;
    const DebugMapView* debugImage = nullptr;
    // Optional generated viewport, prepared at the measured panel dimensions.
    // Borrows frontend-owned state for this draw, just like debugImage.
    WorldgenDebugPreview* worldgenPreview = nullptr;
};
// Screen coordinates are logical UI pixels. Never retains the supplied frame.
bool DrawScreen(SDL_Renderer* renderer, SdlText& text, ScreenFrame& frame,
    int width, int height, const WorldView* view, int tileWidth, int tileHeight, int zoom, float uiScale,
    const GameplayLayout* gameplay = nullptr, const char* footerOverride = nullptr);

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

using OptionsEvent = SettingsEditEvent;
class OptionsUi {
public:
    void Open(const SettingsStore& store, const Settings& runtime, bool remote = false);
    void Applied(const SettingsStore& store, const Settings& runtime);
    OptionsEvent Key(const KeyChord& chord, const SettingsStore& store,
        const Localization& localization, const char* directory);
    void Text(eastl::string_view text);
    void Present(const SettingsStore& store, const Localization& localization, ScreenFrame& frame) const;
    SettingsResult<SettingsStore> Prepare(const SettingsStore& store, Diagnostics* diagnostics) const;
    SettingsScope Scope() const { return editor_.Scope(); }
    bool Capturing() const { return editor_.Capturing(); }
    bool Inspecting() const { return editor_.Inspecting(); }
    bool EditingText() const { return editor_.EditingText(); }
    bool Browsing() const { return editor_.Browsing(); }
    void RememberScroll(const ScreenFrame& frame);
    void Feedback(String message, SettingsTone tone) { editor_.Feedback(eastl::move(message), tone); }
private:
    SettingsEditor editor_;
};
}
