#pragma once

#include "client/input.h"
#include "sdl/map_packet.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_render.h>

namespace cataclysmx::sdl {

struct SpriteClickResult {
    MapItemVisual item;
    SpriteActivationResult activation = SpriteActivationResult::Unavailable;
};
// The app blocks clicks while a modal/interaction owns input or the map is unavailable.
// This path only mutates frontend playback; it cannot submit gameplay commands.
eastl::optional<SpriteClickResult> ActivateMapItem(SDL_Renderer* renderer, const SDL_MouseButtonEvent& event,
    bool inputBlocked, const MapPacket& packet, const TilesetDefinition& tileset,
    SpritePlaybackController& playback, std::uint64_t elapsedMilliseconds, int zoom = 0, float uiScale = 1.0f);

eastl::optional<KeyChord> TranslateKey(const SDL_KeyboardEvent& event);
int MapZoomChange(const KeyChord& chord);
bool AllowsKeyRepeat(InputContext context, InputAction action);

}
