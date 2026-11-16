# Content asset loading

Items reference text illustrations using a virtual absolute path:

```json5
art: '/art/items/claw_hammer/art.txt'
```

The leading slash names the **data root**, never the host filesystem root.
`LoadContentPack` uses the manifest's directory as that root. Definition files
may live in any subdirectory; moving one does not change asset references.
See [text art](item_presentation.md) for the versioned file grammar and RGB
palette. Legacy inline art arrays remain accepted for existing integrations.

## File and memory entry points

Pack loading needs no extra arguments. Standalone definition files require an
explicit root when they reference external assets:

```cpp
const eastl::string_view files[] = {"data/items/example.json5"};
auto result = LoadContentFiles(files, "data", &diagnostics);
```

The registry overload places `const ItemBehaviorRegistry&` before the data root.
The original rootless file-loading entry points remain valid for inline content;
an external reference without a reader returns `ContentError::InvalidData`.
They do not infer a root from source names or search parent directories.

Memory loading supplies a synchronous `ContentAssetReader`:

```cpp
ContentResult<String> ReadAsset(void* context, eastl::string_view virtualPath,
    Diagnostics* diagnostics);

ContentAssetReader assets{&myAssetStore, ReadAsset};
auto result = LoadContent(sources, assets, &diagnostics);
// Custom behavior registries: LoadContent(sources, registry, assets, diagnostics).
```

The callback receives a validated canonical virtual path and returns owned bytes
or a typed content error. It may append diagnostics when the pointer is non-null.
The caller keeps the context alive for the call; no callback/context pointer is
retained in the resulting catalog. This narrow boundary supports in-memory
fixtures or other existing stores without a virtual filesystem framework.

`ReadContentAssetFile(dataRoot, path, diagnostics)` is the file-backed adapter.
It validates the virtual path, resolves the physical root and target with
error-code filesystem operations, checks containment (including symlinks),
requires a regular file, and bounds reads to 147456 bytes. Expected I/O errors
remain `Io`; invalid references and escaping targets are `InvalidData`.

Host filenames and `dataRoot` use the same native narrow encoding as the
manifest/definition file APIs: the Windows active ANSI code page (`CP_ACP`),
and native filename bytes on other platforms. They are not implicitly UTF-8
on Windows. Virtual JSON asset paths are always well-formed UTF-8, validated
before invoking a reader. On Windows the adapter explicitly checks native-root
and UTF-8-reference conversions before constructing wide filesystem paths;
invalid root encoding returns `Io`, and invalid virtual paths return
`InvalidData`. Empty or embedded-NUL roots return `Io`. Filesystem error-code
overloads handle resolution failures; malformed encoding never relies on a
throwing filesystem path conversion. Allocation failure remains fatal.

## Path and ownership rules

- Paths start with one `/`, use forward slashes, and have nonempty components.
- Dot/parent components, backslashes, colons, controls, host-reserved punctuation,
  trailing separators, and components ending in a dot or space are invalid.
- Author case-consistent names for portable packs. Path cache identity is exact
  byte equality; host-specific case aliases are not a content feature.
- Assets are read and parsed once per referenced path during each catalog load.
  Items receive owning parsed values; there are no runtime filesystem reads or
  retained source buffers. The temporary cache is discarded after publication.
- A failed reference fails the whole load. Missing art is allowed only by omitting
  the field (or using an empty legacy array), not by pointing at a missing file.
- Parser diagnostics first identify the virtual asset filename and physical
  one-based line/byte-column, followed by the referencing JSON source and field.
  Reader/parser error kinds are preserved, including `UnsupportedVersion`.

The existing `ItemDefinition` value model is preserved. The per-load cache avoids
repeated I/O without introducing shared ownership into every artwork reference.
Palette names, filesystem paths, unused colors and line-ending conventions are
source details. Equality and multiplayer fingerprints use resolved text, cells,
RGB/default colors, and line boundaries. An actual visual change changes the
fingerprint; a source-only rename with identical artwork does not.

## Distribution and verification

Deploy the complete `data/` tree preserving relative layout. Manifest `files`
list JSON5 definition documents; artwork is reached through item references and
does not belong in that array. Referenced art is validated on ordinary pack load.

`tests/item_asset_content_tests.cpp` covers memory/file resolution, caching,
transactional failures, diagnostics, containment, and canonical fingerprints.
`tests/text_art_file_tests.cpp` covers the file grammar and limits.
