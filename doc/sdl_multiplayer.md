# SDL LAN multiplayer

SDL, terminal and live headless clients can share an authoritative LAN game.
Use the same updated build (protocol 18, `0x43584e12`) and matching content on
all peers. Select the same scenario or content pack on each end; incompatible
content is rejected at admission. The host owns the world and simulation.

## Start and connect

From the repository root, with the Windows SDL and debug presets built, start a
persistent SDL host and join from a terminal or headless client in another shell:

```powershell
./out/build/win-msvc-sdl/cataclysmx_sdl_game.exe --host --port 24680 --name Host --save world.sqlite
./out/build/win-msvc-debug/cataclysmx_game.exe --join 127.0.0.1:24680 --name Alice --profile alice-profile.txt
./out/build/win-msvc-debug/cataclysmx_headless.exe --join 127.0.0.1:24680 --name Bob --profile bob-profile.txt --run-for 3000 --command "move e"
```

Conversely, host in terminal or headless and join with SDL:

```powershell
./out/build/win-msvc-debug/cataclysmx_game.exe --host --port 24680 --name Host
./out/build/win-msvc-debug/cataclysmx_headless.exe --host --port 24680 --name Host --run-for 30000
./out/build/win-msvc-sdl/cataclysmx_sdl_game.exe --join 127.0.0.1:24680 --name Alice --profile alice-profile.txt
```

The two host commands are alternatives: run only one on that port. Replace
loopback with the host's LAN address for another machine. `--discover` starts
the SDL browser; `--discovery-address 127.0.0.1` directs discovery to loopback
instead of broadcasting. The default game and discovery ports are 24680 and
24681; use `--port` and `--discovery-port` to change them.

## Multiplayer settings and live play

Press **F9**, or open **F2 → Interface → Multiplayer settings...**. The HUD also
shows the F9 shortcut when space permits. Use **Up/Down**, **Tab/Shift+Tab** to
select a row and **Enter** to edit or activate it. Text entry replaces the initial
value; Enter accepts and Escape cancels an edit. Escape closes the screen when
not editing. The screen includes explicit Join/reconnect, Leave, Open host, Close
host, and Refresh actions, with unavailable actions disabled for the current role.

Player name, join address, session label, game/discovery ports, and discovery
address are editable here. Empty discovery address means broadcast. Host label
and ports are configured before opening hosting; close hosting to change them.
Discovered sessions are selectable rows, with compatibility and player counts.
Selection follows the discovered session's identity even if the list reorders.

The existing shortcuts also work: **j** edits the direct address, **n**
the player name, and **s** the session label; Enter accepts an edit. **O** opens
the local world to LAN, **C** closes hosting, **R** refreshes discovery, and
**1–9** joins a compatible discovered session. Address editing selects Join on
completion, so a second Enter joins the entered address;
**D** leaves a guest session. **X** toggles new-character mode; then press Enter
to join—it does not join immediately. **P** opens Players through the shared
binding. The LAN menu and other screens capture local input but do not pause a
hosted world. Guest and host commands are validated by the host. Guests may edit
local presentation preferences and bindings, but world settings and pacing belong
to the host. Worldgen and AI debug are host/local-only; guests use filtered remote
Examine. See [SDL controls](sdl_controls.md).

### Host pacing

Local players can configure pacing before hosting; an active host can change it
without stopping the shared session. Guests see current host values read-only.

- **Game speed (ticks/s):** 1–1,000,000. There are 100 ticks per in-world second.
  Enter edits the number; Left/Right halves/doubles it. This rate also controls
  player input grace periods.
- **Limit simulation speed:** caps paid action recovery to the configured rate.
  Turning it off allows recovery to fast-forward under the existing input rules.
- **Pause when everyone is idle:** retains the shared all-idle pause policy.
  A multiplayer menu itself does not pause the world.

Edits remain a draft until **Apply pacing changes**. Application is acknowledged
by the session owner before installing the settings in the frontend. The three
pacing fields become world overrides, preserving other preferences and bindings.
They are saved to the world's configuration directory when one exists; temporary
worlds report session-only application. A save failure reports **Applied, unsaved**
and the same action can retry persistence. Player capacity remains four active
players including the host.

### Screen API

`sdl/multiplayer_screen.h` provides `MultiplayerUi`. `Open` starts a fresh draft;
`Refresh` incorporates current session status without overwriting dirty host edits.
`Key` returns a small `MultiplayerEvent` describing an action; the caller reads the
edited host, connection or pacing values and submits an existing session request.
`Text` accepts SDL text input, and `Present` fills the existing owned `ScreenFrame`.
`Applied` installs acknowledged pacing; `Feedback` reports application/persistence
results independently. `ResetConnection` clears transient editing and new-character
intent when the world/connection epoch changes.

Keeping editing and presentation separate from session effects makes authority
checks and validation testable without a window. No transport or world pointers
are retained by the screen. F2's Interface entry returns `OptionsEvent::Multiplayer`,
using the same modal/session transition as F9.

## Saves and identities

`--save world.sqlite` creates a new persistent world and refuses an existing save;
`--load world.sqlite` resumes it. `--seed` cannot be combined with `--load`.
Use the matching content for the saved world. Restart a persistent host with:

```powershell
./out/build/win-msvc-sdl/cataclysmx_sdl_game.exe --host --port 24680 --name Host --load world.sqlite
```

The default gameplay save key is **S** (Shift+s). Closing LAN parks guests before
checkpointing an open save; shutdown also closes hosting and checkpoints. Guests
cannot save the host world. Storage failures are reported; a temporary world has
no persistent checkpoint until created with `--save`.

The guest profile defaults to `cataclysmx-lan-profile.txt` in the working
directory; `--profile` chooses another file. It stores a server-issued reconnect
token per literal address and port, not the world or inventory. Keep both the
host save and guest profile to resume a parked character. Hostname and numeric
address are separate keys. Rejected reconnects retain the old token; use X then
Enter, or startup `--new-character --join ...`, to explicitly request a new
identity. The replacement token is saved after admission; if it cannot be saved,
SDL returns to local play rather than relying on an undurable identity. See
[LAN profile](lan_profile.md).

Joining from an existing local session preserves that local session for return.
On a startup join, local creation is lazy: leaving or a failed connection creates
the selected local scenario if it has a valid start, otherwise reports an explicit
Demo fallback. Other creation failures are reported.

## Smoke modes

Local `--smoke` exercises the local SDL flow. With `--host`, smoke waits for an
active guest; it also edits/applies host pacing through the multiplayer screen.
With `--join`, it exercises remote UI, read-only host settings and a command; with
`--discover`, it waits for compatible discovery. Start the corresponding peer
separately when using network smoke modes.

For implementation details see [SDL architecture](sdl_client.md),
[networking](networking.md), and [network presentation](network_presentation.md).
