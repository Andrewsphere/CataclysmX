# Structured diagnostics

`Diagnostics` is an optional, caller-owned collection of explanatory frames for
operations such as parsing and loading. Pass a null `Diagnostics*` when details
are not needed so callers can avoid producing diagnostic strings and locations.

Nested functions append frames as errors propagate outward: the innermost
operation adds its frame first, then each enclosing operation may add context.
`Frames()` therefore exposes innermost-first append order. `FormatDiagnostics`
renders the frames in reverse order (outermost first), indenting each deeper
frame by two spaces and including any file, data path, line, and column
associated with that frame.

The same `Diagnostics` instance can be reused, but callers must call `Clear()`
between independent operations. Frames from separate operations otherwise
remain together and their order no longer describes a single nested failure.

`DiagnosticFrame` keeps its message and optional structured location together;
the formatter derives its human-readable output from those fields. This avoids
flattening source locations into an opaque message while allowing callers to
inspect metadata programmatically.
