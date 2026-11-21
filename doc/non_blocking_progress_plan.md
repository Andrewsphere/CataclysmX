# [CataclysmX] Non-blocking loading and activity progress

**Status — Future work.** This document records the proposed design for later review and implementation.

## Summary

Build one small status display for SDL and terminal. The first release covers startup, world saves and shutdown, LAN joining and reconnecting, and **Wait for craft**. Solo craft waiting fast-forwards the normal simulation. LAN craft waiting shows progress at the host’s pace. Manual saves and autosaves keep the UI, simulation, and LAN transport running, including on headless LAN hosts. Each published world save remains a single SQLite file.

## Shared framework

- Move SDL’s session owner and channels into a frontend-independent target. Give terminal the same ownership boundary. The session owns the live server and transport. Both frontends consume owned views and submit typed requests. Convert terminal settings, inspection, and debug paths that currently borrow the server or world to use snapshots and owner requests.
- Add a shared progress value keyed by session generation and operation ID. It carries a localized label, optional completed/total units, and an available stop action. Intermediate updates can replace one another. Every accepted operation delivers exactly one terminal outcome through a reliable reply. Stale updates are ignored after a session change. A full queue returns `Busy` promptly, with reserved capacity for stop and shutdown requests.
- Render a small SDL ring or terminal `| / - \` beside the existing status text. Delay indeterminate spinners by 150 ms. Show known progress immediately. The frontend’s monotonic clock drives animation, and authoritative work or simulation ticks drive progress. Startup and shutdown take priority, followed by connection, manual save, activity, and autosave. Publish errors through the existing message area so they remain visible after the indicator clears.
- Bring up a minimal UI before reading settings, content, or saves. Keep SDL GPU uploads on its render thread and yield between uploads. For LAN, track connecting through admission and the first usable snapshot. Escape cancels the connection attempt. On quit, keep the UI responsive while the host parks guests and the final checkpoint completes. An interactive final-save failure offers **Retry** or **Quit without saving**.

## Wait for craft

- Add **Wait for craft** to the shared crafting menu for the player’s active job. Track it by persistent job ID and show authoritative completed/total work. Expose a compact, job-keyed craft outcome in the player’s view so both frontends can distinguish completion, cancellation, and interruption even when the job disappears between snapshots. Carry that outcome over LAN and bump the wire protocol. Keep it runtime-only.
- In solo play, advance the existing simulation through bounded batches, yielding to requests and publishing progress between batches. Check the job at simulation event boundaries and stop at its completion or interruption tick. **Stop fast-forward** returns to normal pacing and leaves the craft running. **Pause craft** retains the existing paid-slice rule.
- On LAN, both host and guest use **Wait for craft** to follow their own job at the current session pace. Other players retain their normal input opportunities and grace periods. Closing the progress display leaves crafting active. Shared time skips, sleep, and wait-until actions can use the same progress interface in later work.

## Background checkpoints

- At a simulation boundary, capture an owned checkpoint containing session state, dirty chunks and pending archives, player memories, and power state. Resume simulation and network pumping while a single writer handles serialization, validation, and publication. Keep one checkpoint in flight and coalesce additional requests into one follow-up capture. Take the final checkpoint after guests are parked and gameplay admission stops.
- Load a private local read copy at startup. During a checkpoint the session continues streaming chunks from its current copy. The writer clones that committed copy, applies the capture with the existing SQLite transaction and validation, and prepares it as the next read copy. SQLite writes and rollback journals stay in private working storage. The save schema and legacy upgrade rules stay compatible with existing saves. [SQLite Backup API](https://sqlite.org/backup.html)
- Copy the validated database into a dedicated staging directory on the destination filesystem. Flush it, then publish it with a same-filesystem atomic replacement. First publication uses a no-overwrite operation. Hold one writer lease per save path in the staging directory. Report a typed storage error if the destination cannot provide the required locking or replacement guarantees. The published save path gets no adjacent temporary files. [Windows file replacement](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-movefileexw) · [Linux atomic rename](https://man7.org/linux/man-pages/man2/rename.2.html)
- After publication succeeds, switch the session to the new read copy at an owner boundary. Clear only dirty entries whose change stamps match the capture and advance autosave cadence from the captured tick. Preserve newer changes and all pending changes on failure. If publication reports an ambiguous outcome, verify the published file before acknowledging success. Use this path for SDL, terminal, and live headless hosts. Keep scripted headless saves and terminal `--render` synchronous.

## Verification

- Use a fake clock and deliberately held worker operations to verify both UIs keep drawing and processing input. Cover spinner delay, determinate progress, cancellation with a full request queue, stale updates, and reliable terminal outcomes.
- Compare solo fast-forward with ordinary simulation through the same craft. Check completion tick, outputs, and deterministic state. Cover stopping acceleration, paid-slice pause, missing tools or power, and actor death. In LAN loopback, verify another player can keep acting at normal pace while the crafter watches progress.
- Hold a checkpoint writer open and verify that ticks, LAN commands, and chunk reads continue on all three host paths. Mutate and unload chunks during the write, then reopen successive saves to verify both captured and newer state. Exercise first-save collisions, write and replacement failures, legacy saves, and final-save ordering. Verify the published save has no adjacent temporary files throughout.
- Run the focused `win-msvc-sdl` CTest cases for session, frontend, crafting, persistence, and LAN roundtrips. Document the progress API, producer lifecycle, and single-file checkpoint behavior under `doc/`.
