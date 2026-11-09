# Sight, residency, and viewport defaults

Player sight reaches 64 tiles for adequately illuminated targets. Dim and dark
sight limits and glare still apply. Sight is independent of the displayed viewport,
which defaults to radius 64 in the terminal and is limited to radius 64
(129 by 129 cells). The headless `Server::View()` default remains radius 8.

New servers and inherited client settings use active radius 1 and resident radius
5 in chunks. The resident square supplies at least 80 tiles to its nearest
horizontal edge, enough for sight 64 and the 14-tile lighting dependency margin.
The smaller active radius keeps simulation activity separate from visibility.
Explicit smaller bubble settings remain valid; perception is clipped by residency.

`Server::LoadSave` restores the bubble radii stored in the checkpoint, including
older saves with smaller radii. The terminal continues to apply its effective
settings, including inherited defaults, after loading; explicit settings overrides
continue to take precedence. Save format remains version 7; LAN protocol version 9
supports run-encoded larger viewports with a 1 MiB packet limit.
