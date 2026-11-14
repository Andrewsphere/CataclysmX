# Sprite animation

The SDL animation API supports ambient loops, toggled loops, and single blinks.
Presentation time is supplied explicitly in milliseconds since tileset activation;
it never advances simulation time or consumes RNG. The engine has no animation
state, timers, or callbacks.

## Tileset mappings

Clips reference PNG sprites and positive per-frame durations. Local `.meta` files
hold animation definitions, while manifest mappings select a base visual and may
add an activation binding:

```json5
// sprite/item/bulb/loop.meta
{kind: 'animation', frames: [
  {sprite: 'sprite.item.bulb.on', duration_ms: 500},
  {sprite: 'sprite.item.bulb.off', duration_ms: 500},
]}

// sprite/item/bulb/once.meta
{kind: 'animation', frames: [
  {sprite: 'sprite.item.bulb.on', duration_ms: 120},
  {sprite: 'sprite.item.bulb.off', duration_ms: 120},
]}

// tileset.json5 mappings
items: {
  blinking_lightbulb: {visual: 'sprite.item.bulb.loop'},
  toggle_lightbulb: {visual: 'sprite.item.bulb.off', activation: {
    animation: 'sprite.item.bulb.loop', mode: 'toggle_loop',
  }},
  single_blink_lightbulb: {visual: 'sprite.item.bulb.off', activation: {
    animation: 'sprite.item.bulb.once', mode: 'once',
  }},
},
```

Source PNGs and animation-sheet frames are assembled into atlas pages at launch.
See [sprite authoring](source_tilesets.md) for source discovery and sheet metadata.

`SampleAnimation` selects the first cumulative frame end strictly greater than
the current phase. An exact loop boundary selects frame zero. `Once` completes
at the total duration without wrapping; its final sample reports completion, and
the playback controller restores the base mapping. Unequal durations, single-frame
clips, and late presentation frames use the same rules without replaying missed
frames. A single-frame loop has no deadline; a single-frame one-shot does.

## Playback ownership and API

`sdl/animation.h` provides SDL-free, render-owner `SpritePlaybackController`:

- Call `SetGeneration(session, assets)` before activating or sampling a new
  session/tileset. A changed generation clears all playback; matching generations
  preserve it across snapshots, viewport changes, movement, and pickup/drop.
- `Activate(itemId, mapping, tileset, elapsedMs)` returns `Unavailable`,
  `LoopStarted`, `Stopped`, or `PlayedOnce`. The item ID must be nonzero and the
  mapping must have an activation binding. Repeated single-shot activation
  restarts from frame zero. Activating an enabled toggle stops it immediately.
- `Stop(itemId)` explicitly removes an override. `Sample` returns the resolved
  static/fallback mapping and its next optional deadline without allocating.
- `RetireCompleted(tileset, elapsedMs)` removes completed one-shots even when
  their items are offscreen. Toggle entries remain until stopped or the generation
  changes; entries own no world resources and never dereference their item IDs.

All enabled loops share the tileset epoch, so activation joins the current phase
and may initially show an off frame. One-shots instead use their own activation
timestamp. Multiple items play independently. A temporarily hidden/carried item
retains its visual state; an elapsed one-shot is already complete when it returns.
Playback does not survive restarting the frontend and is not saved or replicated.

Separating this state from map packets prevents asynchronous map preparation from
restarting animations. Stable item identity keeps playback attached to the item
when it moves rather than to the old screen cell. The small value controller uses
an EASTL vector of activated items; ordinary ambient clips need no instance state.

## Snapshot and input path

The session owner calls `CaptureMapItems(world, view)` immediately after capturing
the view. Its owned row-major `MapItemVisual` vector contains the lowest
persistent-ID ground root only where the view selects a ground-item overlay.
It checks visibility and actor priority first and queries at `overlayDepth`,
independently of terrain depth. Hidden/occluded cells contain zero IDs/types.
No world pointers cross into the renderer or worker.

`PrepareMapPacket` and `MapPreparation::Submit` accept that optional vector. It
must be empty or match the view's cell count; other sizes violate an invariant.
The prepared cell retains the representative ID/type and uses the resolved
item mapping, falling back to generic ground items when absent. Actor → items →
object priority is preserved. Existing callers without item metadata retain
generic artwork.

`DrawFrame`, `VisibleAnimationDelay`, and `NextAnimationDelay` accept an optional
playback controller. Drawing and deadlines use the same sampling operation;
hidden, clipped, and unselected overlays create no animation deadlines. Expiration
redraws the current packet without snapshot capture, map jobs, or texture uploads.
Local help pauses do not pause playback. Minimized windows suppress drawing and
animation wakeups; restoration samples the elapsed phase once.

## Demo and artwork

New demo worlds place ordinary 50 g / 100 ml bulbs on floor tiles near spawn:
continuous at X/Z `(1,1)`, toggle at `(2,1)`, single blink at `(1,2)`.
The continuous bulb animates automatically. Toggle and single-blink bulbs
retain their artwork and playback metadata, but have no frontend activation
control. Pickup/drop uses the ordinary item commands. There is no emitted-light simulation,
energy use, gameplay action cost, or authoritative activation state.

Bulbs are created after existing demo entities, preserving their persistent IDs.
Existing saves receive no automatic insertion. Save/network layouts are unchanged;
the new content definitions naturally change the catalog fingerprint.

The default Gen1 32x32 pack includes separate transparent 32x32 off/on PNGs in
`assets/tiles/gen1_32x32/sprite/item/bulb`. `blinking_lightbulb` loops on/off at
500 ms per frame. `toggle_lightbulb` rests off and activates that same loop;
`single_blink_lightbulb` rests off and plays one on/off cycle at 120 ms per frame.
Existing presentation controls supply playback; content definitions and gameplay
timing are independent. The separate PNGs and local animation metadata are the
editable source artwork.
