# City probe

`cataclysmx_city_probe [panel|runtime [seed]]` loads and validates the
configured content pack. It is a measurement tool, not a test. Redirect CSV
stdout to a local file under `out`; diagnostics and aggregates go to stderr.

The default `panel` uses seeds 0–31 and 42 and the four regions specified in
`city_identity_design.md`. It selects overlapping cheap descriptors before
generating full plans, deduplicating by seed, owner patch and ordinal. It also
marks the first origin-patch example of every scale/era combination; examples
already in the panel appear once with both flags. Missing combinations are an
error. Bounds are half-open, floors sum authored occupied floors of building
sites (excluding outdoor civic sites), and
expected indoor population uses ratioScale units. Actual counts distinguish the
population placement kinds. Planning time excludes descriptor selection and is
hardware-dependent. The aggregate includes both panel and added examples.
The CSV also reports residential sites/floors and the nearest envelope-edge gap
(Euclidean rectangle separation, checking adjacent planning patches). Stderr
lists size/era descriptor counts from all 33 origin patches, avoiding the
land-area bias of the four-region panel, plus actual selected-pattern counts.

`runtime [seed]` defaults to seed 42. It uses a Town server and the origin
patch's large city, materializes the city with active radius 1 and resident
radius 5, and moves the player to a legal city-center tile without deleting
actors. Setup includes server construction, loading, placement and one warm
wait and joining a second player. It then times up to 20 two-player wait
rounds through the normal input barrier and separate radius-12 views for both
players, stopping on death. Actor counts are sampled after setup and include both players; resident
counts include active actors. Tick advancement, completed waits and death are
reported explicitly.

Setup also creates an explicit workshop fixture in the origin clearing: a
catalog grid scrap press on a small mains-connected building grid, supplied with
20 actual steel-scrap items. Its ordinary Start action consumes the inputs.
The player leaves it behind before the measured rounds; the temporary setup
request is released and a nonzero machine-only column count is required.
The final observation reloads the column outside the timed rounds and reports
remaining work and contained outputs. No monsters are removed, and machine
progress and power payment use the normal scheduler. This is a controlled
workload fixture, not evidence of a naturally generated workshop at the origin.
Timings are observations, not assertions.

## Pass-4 reference measurements

Generation 8, bundled Town content, Windows/MSVC Release, Ryzen 9 9950X3D.
The prescribed panel plus nine size/era examples contains 83 distinct settlements
and 4,610 sites. Local CSVs/logs are under `out/pass4-city-*`; they are not committed.

| Scale | Examples | Sites min / mean / max | Mean residential floors | Indoor actors min–max | Outdoor actors min–max |
| --- | ---: | --- | ---: | --- | --- |
| Small | 28 | 14 / 15.61 / 18 | 1.52 | 36–126 | 29–52 |
| Medium | 42 | 33 / 48.10 / 59 | 2.14 | 175–518 | 329–660 |
| Large | 13 | 150 / 165.62 / 185 | 3.23 | 1,056–3,150 | 3,247–4,717 |

One medium example has 33 sites, below the typical 36-site band; 41/42 fall within
the band. Actual newer-site shares are 16.71% / 44.86% / 83.77% in older / mixed /
newer settlements, against tuning targets of 20% / 55% / 85%. These are measured
outcomes, not guarantees. Outdoor totals divided by eligible area reproduce
0.5 / 1.5 / 3.0 actors per 100 tiles (single-city stochastic rounding).
The closest envelope-edge gaps are 128–261 tiles, mean 190.34.

The origin-patch descriptor census is independent of the region panel's area
bias: 198 small, 99 medium, 33 large (60/30/10 exactly). Era counts are 76 older,
161 mixed and 93 newer (23.03/48.79/28.18%); the accepted selection probabilities
remain 25/45/30. All thirteen added families and three variants occur in the
measured panel, including six plants and sixteen laboratories.

All nine explicit examples use owner patch `(0,0)`. Their `(seed, ordinal)` IDs:
small older `(2,8)`, mixed `(0,5)`, newer `(0,4)`; medium older `(0,1)`, mixed
`(0,2)`, newer `(5,1)`; large older `(0,0)`, mixed `(1,0)`, newer `(2,0)`.
Every large envelope spans region boundaries. Full-plan preparation measured
0.14–0.43 ms small, 0.82–3.55 ms medium and 12.87–31.23 ms large.

The seed-42 live large-city run recorded 713 active and 1,937 resident actors,
two players, and one machine-only column. Setup took 173.36 ms; five completed
rounds plus the host's fatal sixth round advanced 600 ticks in 95.57 ms.
Ten radius-12 views took 1.43 ms total. The press completed its 300 work ticks
offscreen and produced one item from twenty consumed scrap. The host died and
the guest survived; this is an unprotected city-center exposure measurement,
not a successful expedition playthrough. Other integration checks were running
on the same machine, so timings are indicative rather than isolated benchmarks.

Functional acceptance and follow-up tuning remain distinct: outdoor density was
not reduced for performance, mixed-era construction and the medium outlier remain
visible tuning observations, and end-to-end expedition balance still needs play.
