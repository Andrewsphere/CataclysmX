# LAN client identity profile

`client/lan_profile.h` provides `LanProfile`, `LanIdentity` and
`ProfileResult<T>` (`eastl::expected<T, ProfileError>`). Values use the project
`String`/`Vector` types and `eastl::string_view`; C-string paths are the explicit
stdio boundary. `Load(path)` returns an empty profile for a missing file, and
`Malformed` or `Io` for corrupt and unreadable files without modifying an
already loaded profile. `Set(identity)` reports `InvalidField` or `Full` and
replaces only the matching address **and port**. `Find(address, port)` returns
the matching identity, if any; `Forget(address, port)` reports whether it was
removed. `Save(path)` reports I/O or temporary-name collisions and does not
replace the old file unless writing, flushing and closing succeed.

The bounded version-1 text format stores at most 64 records, each an address
(up to 255 bytes), nonzero decimal port, nonzero decimal unsigned 64-bit token,
and name (up to 64 bytes), separated by tabs. Text preserves UTF-8 bytes and
rejects control characters and embedded field separators.
Input is length-checked and duplicates/truncated records are rejected. Save
uses exclusive temporary creation, file flush/sync, and an atomic replace where
available. Frontends save a received token on the first poll that exposes it,
including while **joining** and waiting for the initial authoritative view.
An invalid saved token remains intact on failed
rejoin; choosing **X** in the terminal LAN menu or `--new-character` in headless
explicitly replaces it only after new admission.

A profile is a durable local identity mapping. Host-side SQLite saves own the
characters, inventory and parked states. Identity lookup
keys on the literal address string and port: a hostname and its numeric address
are separate aliases, and two different host worlds sharing an endpoint may
reject the older token. In either case the client preserves that token until
the user explicitly chooses a fresh identity. This small endpoint key avoids
speculative server identity discovery before connecting.
