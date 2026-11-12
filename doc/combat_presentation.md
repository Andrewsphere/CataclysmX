# Combat presentation

`CombatStatusText` and `CombatFeedbackText` format the authoritative `CombatView`
for frontends. They return empty strings for unavailable data, so an empty
snapshot does not invent player health or combat events. HP is displayed in
hundredths; feedback is presentation-only and does not alter simulation state.
The initial strings are English fallbacks shared by terminal and SDL.
