# SDL frontend architecture

This describes the implemented local SDL frontend. [Controls and APIs](sdl_controls.md)
cover gameplay screens; [options redesign](sdl_options_redesign.md) specifies the
later visual rebuild and is not an implementation-status document.

## Ownership and boundaries

The SDL main thread owns the window, Vulkan renderer, text, assets, input,
frontend settings, and screen state. It never borrows mutable World data.
The session thread owns Server, simulation, elapsed-time pacing, and World.
Map preparation and asset preparation operate on owned input snapshots and
immutable content. Neither worker controls gameplay activation time.

SessionChannels provides a bounded request/reply FIFO and a latest-snapshot
mailbox. Each accepted request reserves enough reply capacity for admission and
completion. Replies retain request/session identity. Snapshot replacement may
skip intermediate views; reliable command results are never replaced with views. A command completion is
published only after its new opportunity snapshot, so a frontend can safely submit
the next action using that snapshot.

Only the session owner mutates simulation. Commands carry the player's identity
and current opportunity, use authoritative admission, and retain one pending
intent. The frontend permits one outstanding gameplay command, preventing held
input from filling a queue with future actions.

## Paused screens

Help, Examine, interaction, options, and worldgen debug request Pause before
initialization. Closing requests Resume; selected interactions use
ResumeAndCommand to discard paused wall time and submit in one owner operation.
Paused UI clocks, rendering, and keyboard repeats never advance simulation.

An interaction snapshot contains visible doors and objects on the player's tile
and eight neighboring tiles, on the same Y level. The UI groups actions by tile.
Direction input immediately opens that tile's choices; one available action
executes immediately. Discovery returns typed refusal reasons through caller-owned
output, and the server re-queries the exact persistent object/terrain and action
before mutation. A state change never substitutes a different action.

Examine requests include an absolute cursor with Y. The owner builds
`ViewForPlayerAtLayer(player, radius, layerY)`, a filtered projection using the
player's actual perception and memories. Inspection replies carry the view,
matching map item visuals, and descriptions. Terrain and overlays retain their
independent downward projection depths. Inspection teaches no memories and
acquires no chunks. Unknown tiles remain unknown on every selected layer.
The frontend coalesces cursor requests and rejects replies for obsolete cursors.
Layer changes require a matching prepared map before displaying its contents.
Normal gameplay views always start at the player's actual layer.

## Keyboard input

TranslateKey preserves printable case and represents Ctrl/Alt/Shift explicitly
for named keys. Binding resolution remains shared with terminal; settings and
keybinding parsing remain in the content integration boundary.

HeldKey owns the latest non-modifier key. A physical press dispatches immediately;
after 250 ms it dispatches at keyboard_repeat_rate (10 Hz by default, 1–60 Hz).
Native repeated keydown events are suppressed. Synthetic repeats use the normal
handler and current modifiers/context, so navigation, combat, zoom, confirmations,
screen commands, and saving share one mechanism. New actions need no repeat
allowlist. Release and window focus loss clear the held key. Stalls produce one
repeat rather than a catch-up burst. Numeric text repeats the committed input
without also accepting its native duplicate. Binding capture consumes one chord.

This input clock belongs to the frontend and does not participate in simulation
or replay. Command admission and opportunity checks still apply to every repeat.

## Rendering, scale, and assets

MapPreparation transforms filtered WorldView and map item values into a MapPacket.
Identity includes session, view revision, asset generation, and viewport generation.
The main thread rejects incompatible results. Live sprites, remembered appearances,
lighting/depth styling, and hidden cells use the same filtered snapshot boundary.

Map cells use native tileset dimensions multiplied by 2^(zoomStep/4), bounded to
1/4–8 times native size. Step zero means native pixel size. Window resizing changes
the visible area instead of fitting the whole snapshot. Rectangular cells preserve
their native aspect ratio. UI scale affects text/panels independently of map zoom.
Map layout, sprite visibility, highlights, and mouse hit testing share this transform.
Gameplay centers on the player; Examine centers on its cursor.

Assets preserve native pack metadata and sprite anchors. Asset preparation and
image decoding are separated from main-thread SDL texture creation. Active assets
remain alive while render packets use their mapping generation. Sprite playback
is frontend presentation state; clicking an activation sprite does not submit a
simulation command. Modal screens block map activation clicks.

The Vulkan sprite shader supplies perception tinting for PNG artwork. Ground and
upright passes preserve row ordering and overhangs. Text and glyph fallback use
SdlText; rendering remains on the SDL main thread. Animation deadlines schedule
redraws only for visible active animation. Minimized windows avoid rendering work.

## UI and settings

ScreenFrame contains owned presentation values; DrawScreen lays out rows and
details side by side on wide windows and stacked on compact windows. OptionsUi
owns drafts, focus, numeric/language editors, and binding capture. No simulation
entity pointers or third-party UI framework cross this boundary.

The gameplay view shows status and a small F1 Help reminder. Binding lists live
in F1. Modal screens may show their own contextual instructions.

Options has one Save and apply operation. It validates the selected scope's draft
and stages language loading, requests authoritative application, then installs
frontend state and persists that scope. Application failure leaves current state
intact. Persistence failure retains applied settings and reports Applied, unsaved;
retry remains available. Other-scope drafts survive saving. Temporary worlds have
session-only world overrides and say so explicitly.

Reopening clean options reloads persisted settings and applies them through the
same owner handshake. Unsaved state is retained after a save failure. Seed remains
a future-world default and cannot mutate the current world. Residency without an
explicit override preserves the runtime policy. See [settings API](settings_api.md).

## Shutdown and validation

Session shutdown stops admission, drains/resolves outstanding replies, and joins
its owner thread. Worker objects and textures are destroyed before the renderer;
the window and SDL runtime outlive their dependent resources. Storage errors are
reported separately from successful gameplay outcomes.

The linux-gcc-sdl preset builds terminal, SDL, and shared engine tests. Existing
SDL tests cover key translation/repeats, channels and pause semantics, filtered
inspection, options staging, software/Vulkan rendering, assets, and map transforms.
The frontend smoke exercises actual screen input in an offscreen Vulkan window.
Network integration tests require localhost sockets. Tooling harness tests are not
part of gameplay verification.
