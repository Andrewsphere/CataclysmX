#pragma once

#include "client/localization.h"
#include "client/worldgen_debug.h"
#include "sdl/text.h"

namespace cataclysmx::sdl {
struct ScreenRow {
    String text;
    bool unavailable = false;
};
struct ScreenFrame {
    bool visible = false;
    bool full = false;
    bool followCursor = false;
    String title;
    String footer;
    Vector<ScreenRow> rows;
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
    int width, int height, const WorldView* view, int tileWidth, int tileHeight, int zoom, float uiScale);

eastl::optional<std::size_t> SoleAvailableAction(const Vector<InteractionChoice>& actions);

enum class OptionsEvent { None, Close, Save };
class OptionsUi {
public:
    void Open(const SettingsStore& store, const Settings& runtime);
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
    void Fields(Vector<SettingsField>& output) const;
    void Bindings(const SettingsStore& store, Keymap& output) const;
    void Begin(const SettingsStore& store, const char* directory, bool append);
    SettingsOverrides drafts_[2];
    Keymap keys_[2];
    Settings runtime_;
    SettingsScope scope_ = SettingsScope::Global;
    bool bindings_ = false;
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
