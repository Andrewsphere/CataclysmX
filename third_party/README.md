# Vendored dependencies

Pinned upstream files, downloaded by `scripts/vendor.ps1`. No submodules.

| Library | Upstream | Exact commit | License |
| --- | --- | --- | --- |
| unordered_dense | https://github.com/martinus/unordered_dense | `7c58a8b7a48e790fe72f6508eb3b46317cbba9d5` | `unordered_dense/LICENSE` (MIT) |
| SlotMap | https://github.com/SergeyMakeev/SlotMap | `122fb6960bc417f82f65a0469d38bfa05b506a25` | `slotmap/LICENSE` (MIT) |

Both unordered_dense headers are included. Upstream include paths are preserved.
Local patches: `slotmap/const-iterator.patch` removes constness from the const
key/value iterator's internal reference-wrapper cache. The old iterator wrote
through `const_cast` to a const subobject (undefined behavior). The returned
wrapper still refers to `const T`, and iterator access remains read-only.
The vendor script reapplies this patch. Updating pins is an explicit dependency change.
