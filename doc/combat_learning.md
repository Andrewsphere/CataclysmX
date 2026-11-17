# Art acquisition and manual study

`src/combat/combat_learning.cpp` implements the acquisition/study members of
`CombatSystem`. `LearningSystem::Busy` covers both ordinary books and art manuals,
including a slice whose deadline has expired but whose event is still pending.
Ordinary study and art study cannot own an actor simultaneously.

## Acquisition

`GrantArt(actor, art, reason)` is an authoritative creation, teaching, manual, or
administrative operation. It is not a client study shortcut. Acquiring an already
known art never resets or increases its XP. Arts are stored by stable definition
name order. Acquisition grants only the requested art and leaves the selected
style unchanged, including the first acquisition. Baseline styles and typed
fallback catalogs deliberately do not grant learned arts.

Acquisition derives art ability grants using **learned** skill requirements and
the art's XP. Grants are deduplicated by `(ability, source)`; independent innate
grants and source cooldowns are preserved. Reapplying acquisition can refresh the
derived grants without awarding XP. The runtime progression owner must also
refresh these gates when learned skills or art XP change. Combat training and
art-XP awards are implemented by the runtime owner.

## Manual attempts

An authored `ArtManualDefinition` selects an art, an explicit skill/minimum learned
level, a fixed success chance in ratio units, and a positive base duration.
`QueryArtStudy` checks life, activity, competing work, knowledge, prerequisites,
and `Learning().HasBookReference`. It never rolls or creates progress.

`StudyArt` starts or resumes a paused `(book, art)` attempt. At the first start it
captures chance and `ArtStudyDuration(baseDuration, learnedSurplus)`. Later level
changes do not alter that snapshot. Each funded slice is at most 100 actual ticks,
sets the ordinary action deadline, and increments the actor's action count once.
Art study does not create a combat action or consume stamina. Large valid durations
are handled without narrowing to a 32-bit action cost.

`remaining` is unfinished time for the whole attempt. `fundedTicks` is unfinished
time for its currently paid slice. Both decrease together during active elapsed
time. There is no unearned time credit across a funding boundary.

Pause requests take effect after the paid slice. Losing the reference or learned
prerequisite also stops funding at that boundary. Paused, parked, and inactive
attempts freeze, including a fully paid slice awaiting event processing. World
death handling clears attempts and suspends the deadline, so no remaining funded
slice can complete after death. A completed
attempt paused by cancellation or invalid eligibility retains zero remaining time
and its snapshot; after explicit eligible resume it completes without another
funded action. This prevents both a free roll on cancellation and double payment.

At valid completion, `ProcessArtStudyEvents(rng)` performs one success test using
`DrawBelow(rng, ratioScale)`, including at zero/full authored chance. Actor cohorts
are processed by persistent ID. Success acquires the art; failure clears the
attempt snapshot so a retry pays the full newly computed duration. Neither result
marks the ordinary `BookProgress::used` flag. Known arts cannot be studied again.
`ArtStudyCompleted` reports the actor, art, full captured duration in `cost`, and
success in `learnedArt`. Inspection, pause, reference loss, and ineligible
completion consume no RNG and emit no completion event.

## Integration requirements

`ManualAttemptProgress` includes these funded-slice fields:

```cpp
Tick fundedTicks = 0;
bool pauseRequested = false;
```

Persist these alongside book/art stable names, remaining time, captured duration,
chance, and active state. A due active slice may legitimately have zero funded
ticks. A paused completed attempt may have zero remaining time and a nonzero
captured duration. A failed attempt has zero duration/chance/remaining and retains
only its `(book, art)` identity. Validate `fundedTicks <= remaining`,
`fundedTicks <= 100`, and global ordinary/art study exclusivity. World death
finalization must cancel study ownership and suspend deadlines.

The runtime scheduler calls `ElapseArtStudy(elapsed)` **before** advancing World
time, processes `ProcessArtStudyEvents(rng)` at the event boundary, and includes
`NextArtStudyEventIn()` when choosing the next boundary. A zero result is due work,
not permission to accept input. Do not additionally invoke these hooks from
`LearningSystem`, since that would charge elapsed time twice.

`src/combat/combat_learning.cpp` and `tests/combat_learning_tests.cpp` exercise
paid boundaries, exclusive busy
ownership, failure/retry, cancellation/reference loss including a fully paid
attempt, snapshot duration/learned prerequisites, idempotent acquisition, parked
and dead actors, independent ability sources and unlock gates, and a save during
a funded pause request. Tests load an owned main-pack catalog, then configure a
250-tick boxing manual on the workshop primer before constructing World. Catalogs
remain immutable throughout each World's lifetime.
