# Terminal redraw and timer reuse

`GameplayUi` retains the FTXUI terrain element tree from its last map frame.
It compares the incoming row-major `ViewCell` values and dimensions before
reusing it; changed terrain, visibility, light, depth, or overlays rebuild the
tree. This deliberately does not depend on server revision or endpoint identity:
equal projected cells produce equal terrain presentation. The content catalog is
expected to remain immutable and alive while used by the UI; changing to a
different catalog object rebuilds the tree. The viewport wrapper, crop anchor,
localized crop indicator, status, and layout are rebuilt each frame, so player
position, translation, modal state, and terminal size can change independently.
`GameplayUi::MapPreparations()` counts actual tree preparations for diagnostics.
Rendered elements must still be drawn before the next UI render or destruction.

The timer thread posts an FTXUI Custom wakeup at most once until a Custom event
is consumed by the event callback. The callback continues to measure real
elapsed time and pumps the same host, guest, and discovery paths before input
dispatch. Keyboard events are neither coalesced nor reordered by this gate;
simulation pacing and the server's latest queued-intent behavior remain in their
existing paths. A wakeup can be posted while a prior callback is processing, but
repeated timer intervals cannot by themselves accumulate a backlog. Other
FTXUI Custom events also rearm the gate; this is not an instrumentation of the
FTXUI event queue.

The optimization still compares and copies projected cells on a changed map,
constructs a view on redraw, and performs FTXUI layout and rasterization every
frame. It does not measure terminal output, interactive queue latency, or promise
a frame-time target. See `perception_benchmark_baseline.md` for the pre-change
measurement and its reproduction details; no post-change timing is implied here.
