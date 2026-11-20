# Crafting controls and frontend ownership

Press **&** (the configurable `crafting` action) in terminal or SDL. Terminal also exposes Crafting in Quick Actions and Controls; SDL lists the binding in Help. Use the configured menu Up/Down bindings to choose, Enter to submit, and Escape to close.

The menu lists enabled recipes individually, even when they produce the same item. Recipe rows show numeric recipe IDs, names and output counts. Reachable unfinished jobs show their persistent ID, recipe, completed/total work, status, worker, funded ticks and pending pause. Each job has explicit Resume, Pause and Cancel rows. Recycling rows identify visible carried/ground roots and leaf candidates by persistent ID.

The panel also shows learned level/maximum separately from effective skill levels,
XP, book boost or advanced
theory/practice reserves, recipe knowledge and accessible references, skill gates,
training ceilings and capped rewards. Book rows show study progress, eligibility,
benefits and recipe references; eligible accessible books offer study/resume, and
an active study offers pause. These are snapshot-based advisory views: the server
rechecks availability and paid-slice rules. Used books remain useful references.
At maximum, skills show the maximum-reached state instead of a `0/0` XP meter or
advanced-learning prompt. Recipe training ceilings are `min(source + 1, maxLevel)`;
capped training is separate from recipe usability. Book labels distinguish partial
study, used benefit, completed eligible study and completed work with no eligible
benefit. Eligible completed study applies its benefit without repeating paid work.
See [skills](skills.md) and [skill content](skills_content.md).

Availability is advisory: the server validates tools, inputs, energy, reachability and ownership. Reserved children are excluded by the server snapshot. Frontends never inspect or mutate live crafting state. A retained command selection does not silently change to its neighbor when a snapshot removes a row; stale selections are subject to authoritative refusal.

Terminal crafting stays open after submission and displays authoritative messages. Unlike other local terminal modals, it lets time advance, so accepted work progresses and Pause remains accessible. SDL uses its existing session pause on opening; submitting uses ResumeAndCommand, including for pause/cancel while the actor is working. Successful commands close the SDL menu. Refusals retain the choice and result text and request session pause again. Session pause does not cancel or refund a funded slice; a crafting Pause request is resolved by the server's paid-slice rules.

## Headless script commands

* `recipes`: list enabled recipe IDs/names/outputs.
* `crafting`: list recipes, reachable jobs and recycling candidates.
* `craft ID`: start a numeric crafting recipe.
* `recycle ITEM_ID`: recycle a visible item's persistent ID.
* `resume-craft JOB_ID`, `pause-craft JOB_ID`, `cancel-craft JOB_ID`: operate on an unfinished job's persistent ID.

Commands print authoritative tick, advanced flag and result text through the existing script result path. Use `elapse MICROSECONDS` to advance time under the current pacing settings. These additions apply to the local script parser in `headless/main.cpp`.

## Shared client API and build registration

`CraftingMenuRow` owns a `Command` payload and display label. `CollectCraftingMenu(view, content, localization, rows)` clears and refills caller-owned reusable storage; no disposable vector is returned. `SameCraftingChoice` compares command kind, recipe ID and item/job persistent ID, excluding progress text. Snapshot-owned objects/handles never escape into menu state. English labels follow existing fallback conventions; item names use Localization.

Register `src/client/crafting_menu.cpp` (and its header for IDE visibility) with `cataclysmx_client` and `tests/crafting_ui_tests.cpp` with `cataclysmx_tests`. The tests cover recipe identity, disabled recipes, snapshot-only job controls, recycling payloads, refreshed selection identity, default bindings and authoritative refusal text. Run the `[crafting-ui]` Catch2 tag after integration.

## Bundled demo

With a reachable rock, open **&** and choose **Shape stone marker**. It needs no tools and takes 300 work. The resulting stone marker can be selected for recycling in the same menu; recycling takes 100 work and returns one rock. Register `tests/crafting_demo_tests.cpp` with `cataclysmx_tests` to check the bundled definitions.
