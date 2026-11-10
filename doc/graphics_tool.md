# Tile and sprite graphics tool

`tools/graphics.py` is an optional offline Python 3.10+ CLI. It uses only the standard
library and needs no packages, PowerShell, System.Drawing, or game build.
Run commands from the repository root. Examples use `python3`; on Windows use
`py -3` or your installed `python` executable if `python3` is an app-store alias.

```sh
python3 tools/graphics.py --help
python3 tools/graphics.py resize --help
python3 tools/graphics.py inspect "assets/tiles/gen1_32x32/tile/world/*.png"
```

## Inputs, outputs, and PNG support

Commands accept files or quoted glob patterns. Explicit argument order is retained;
each pattern's matches are sorted. Duplicate inputs, including aliases, are rejected.
Paths resolve from the caller's working directory. There are no pack presets,
automatic asset discovery, workflow recipes, or environment-specific defaults.

For crop/resize/transform/alpha/palette, select `--output file.png` for one input or
`--output-dir directory` to retain input basenames in a batch. Split always needs
`--output-dir`; assemble/preview always need `--output`. Outputs must end in `.png`.
Existing destinations require `--force`. Inputs and their aliases cannot be
overwritten even with `--force`; use a separate output path when editing artwork.
Duplicate output names, unsafe split names, and symlinks escaping an output
directory are rejected. All inputs, operations, destinations, and encodings are
validated before writing. Replacement uses a temporary file beside the output;
new files use exclusive creation. A filesystem failure can leave a batch partially
written. Expected failures print a diagnostic to stderr and return status 1;
argument syntax errors return 2.

Supported inputs are non-interlaced 8-bit RGB, RGBA, grayscale, grayscale-alpha,
and 1/2/4/8-bit indexed PNGs, including PLTE/tRNS transparency and all five scanline
filters. Grayscale/RGB transparent-key samples retain their RGB. The decoder
checks signature, chunk order/lengths/CRCs, palette indices, and bounded zlib data.
16-bit samples, low-bit grayscale, interlacing, animation, and unknown critical
chunks are rejected. Ancillary chunks are ignored; color profiles are not applied.

Outputs are RGBA8 PNGs containing IHDR, IDAT, and IEND, without embedded metadata
or timestamps. The same pixels produce repeatable bytes with the same Python/zlib
environment; encoding bytes can differ from older System.Drawing files. No `.meta`
or manifest files are read, generated, or modified.

Limits are 16,777,216 pixels per image (64 MiB RGBA), 64 MiB per encoded PNG,
4096 inputs/outputs, and 256 MiB each for a batch's input and output pixels.
Palette/name text files are limited to 1 MiB. Processing and PNG compression require
additional temporary memory beyond those pixel budgets.

## Commands

| Command | Required options | Optional operations |
| --- | --- | --- |
| `inspect INPUT...` | None | Prints original encoding, exact RGBA color count, clear/partial/opaque pixel counts, and visible `x,y,w,h` bounds. |
| `crop INPUT...` | `--rect X Y W H` and output | The positive-size rectangle must fit completely inside every image. |
| `resize INPUT...` | `--size W H` and output | `--filter nearest` (default) or `box`; optional `--palette FILE`, `--sample-alpha-min N`, `--min-coverage F`. |
| `split INPUT` | `--grid COLUMNS ROWS` or `--cell W H`; `--output-dir DIR` | `--margin N`, `--spacing N`, `--names-file FILE`. |
| `assemble INPUT...` | `--columns N`, `--output FILE` | `--cell W H`, `--margin N`, `--spacing N`. |
| `transform INPUT...` | Output | `--flip-x`, `--flip-y`, `--rotate 0\|90\|180\|270`. |
| `alpha INPUT...` | Output | `--opaque` or `--threshold N`; `--clear-border N`, `--remove-isolated`, `--zero-hidden-rgb`. |
| `palette INPUT...` | `--palette FILE` and output | Preserve alpha, including invisible RGB unless explicitly cleared separately. |
| `preview INPUT...` | `--columns N`, `--output FILE` | `--scale N` (default 4), enlarged with nearest sampling. |

Nearest resizing center-samples each destination pixel using integer arithmetic.
Box resizing partitions source pixels into integer rectangles and only downsamples.
Normally it averages the four straight RGBA channels independently, rounding halves
up. `--sample-alpha-min N` instead ignores samples with alpha below N, averages
surviving RGB, and emits opaque pixels only when their fraction reaches
`--min-coverage` (default 0.5); rejected regions become transparent black. These
two options require box resizing. Palette selection uses unrounded RGB averages,
squared RGB distance, no dithering, and the first palette entry on ties.

Split uses row-major order. Grid mode partitions the space remaining after margins
and spacing using floor boundaries, so uneven image dimensions are supported.
Cell mode requires exact divisibility. Default names are `0000.png`, `0001.png`,
etc. A names file supplies exactly one relative `.png` path per cell, with forward
slashes for subdirectories. Palette files contain one `RRGGBB` or `#RRGGBB` color per line, with
1–256 ordered entries. Both text formats allow blank lines and `//` comment lines.

Assemble places images at cell top-lefts, without resampling, and clears unused
space. Inputs must have identical dimensions unless `--cell` is supplied; all must
fit the explicit cell. Preview uses the largest input dimensions as its cell,
one-pixel margins/spacing, and an eight-pixel gray checkerboard beneath alpha.
It composites to opaque pixels before enlargement. Use explicit inputs when
contact-sheet order matters.

Alpha processing applies opacity/threshold first, then border clearing, then
isolated-pixel removal, then optional invisible RGB clearing. Threshold is inclusive
(`alpha >= N` becomes 255). Border and isolated-pixel removal clear all four channels.
An isolated pixel has no nonzero-alpha neighbor among the surrounding eight pixels;
diagonal connections survive. Threshold alone preserves RGB beneath zero alpha.
Transforms apply horizontal/vertical flips in source space before rotation.

```sh
python3 tools/graphics.py crop reference.png --rect 64 0 32 64 --output cutout.png
python3 tools/graphics.py resize cutout.png --size 32 64 --output tree.png
python3 tools/graphics.py transform tree.png --flip-x --rotate 90 --output turned.png
python3 tools/graphics.py alpha turned.png --threshold 224 --clear-border 1 --zero-hidden-rgb --output clean.png
python3 tools/graphics.py palette clean.png --palette palette.txt --output recolored.png
python3 tools/graphics.py preview "out/art/*.png" --columns 4 --scale 6 --output out/contact.png
```

## Reproducing bundled artwork

The following commands write only under `out/graphics`. Use fresh stage directories
for each workflow; add `--force` to commands when deliberately repeating them.
Quoted glob patterns work in PowerShell and POSIX shells because Python expands
them. Commands print their output filenames. For an intentional installation,
change only the final destination and supply `--force` for existing files.

### Individual Gen1 sprites, asphalt, and tree

```sh
python3 tools/graphics.py resize assets/tiles/references/modular_gen1/asphalt.png --size 32 32 --output out/graphics/individual/tile/world/road/asphalt.png
python3 tools/graphics.py resize assets/tiles/references/modular_gen1/tree.png --size 32 64 --output out/graphics/individual/sprite/world/tree/tall.png
```

The individual production PNGs are the editable sources; runtime atlas pages are
assembled at launch. Asphalt is opaque 32x32; the 32x64 tree preserves source alpha.
Metadata remains artist-authored in the existing pack; these commands generate only PNGs.

## Design and maintenance

The tool owns only image operations. Pack-specific paths, names, and palettes stay
in documentation or plain data so adding graphics requires no new scripts or CLI
presets. A small PNG codec keeps the tool usable offline without package setup;
explicit encoding limits avoid silently approximating unsupported images.

Do not add, maintain, or run tooling tests. Game/runtime tests are separate.
