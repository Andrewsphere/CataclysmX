#!/usr/bin/env python3
"""Offline PNG operations for tiles and sprites. See doc/graphics_tool.md."""

import argparse
import glob
from pathlib import Path
import struct
import sys
import tempfile
import zlib


PngSignature = b"\x89PNG\r\n\x1a\n"
MaxPixels = 16 * 1024 * 1024
MaxFileBytes = 64 * 1024 * 1024
MaxBatchBytes = 256 * 1024 * 1024
MaxFiles = 4096


class Image:
    __slots__ = ("width", "height", "pixels", "encoding")

    def __init__(self, width, height, pixels=None, encoding=(8, 6)):
        if width <= 0 or height <= 0 or width * height > MaxPixels:
            raise ValueError("image dimensions must be positive and at most 16,777,216 pixels")
        self.width = width
        self.height = height
        self.pixels = bytearray(width * height * 4) if pixels is None else pixels
        self.encoding = encoding
        assert len(self.pixels) == width * height * 4


def Paeth(left, above, corner):
    estimate = left + above - corner
    a, b, c = abs(estimate - left), abs(estimate - above), abs(estimate - corner)
    if a <= b and a <= c:
        return left
    return above if b <= c else corner


def ReadPng(path):
    with path.open("rb") as stream:
        data = stream.read(MaxFileBytes + 1)
    if len(data) > MaxFileBytes:
        raise ValueError(f"{path}: PNG exceeds the 64 MiB encoded limit")
    if not data.startswith(PngSignature):
        raise ValueError(f"{path}: invalid PNG signature")
    offset = 8
    header = palette = transparency = None
    compressed = bytearray()
    seenData = endedData = endedFile = False
    while offset < len(data):
        if offset + 12 > len(data):
            raise ValueError(f"{path}: truncated PNG chunk")
        length, kind = struct.unpack_from(">I4s", data, offset)
        end = offset + 12 + length
        if end > len(data):
            raise ValueError(f"{path}: truncated {kind!r} chunk")
        if not all(65 <= value <= 90 or 97 <= value <= 122 for value in kind) or kind[2] & 32:
            raise ValueError(f"{path}: invalid PNG chunk type")
        payload = memoryview(data)[offset + 8:end - 4]
        crc = zlib.crc32(payload, zlib.crc32(kind))
        if crc != struct.unpack_from(">I", data, end - 4)[0]:
            raise ValueError(f"{path}: invalid {kind.decode('ascii')} CRC")
        if header is None and kind != b"IHDR":
            raise ValueError(f"{path}: IHDR must be first")
        if seenData and kind != b"IDAT":
            endedData = True
        if kind == b"IHDR":
            if header is not None or length != 13:
                raise ValueError(f"{path}: invalid or duplicate IHDR")
            header = struct.unpack(">IIBBBBB", payload)
            width, height, depth, color, compression, filtering, interlace = header
            if width <= 0 or height <= 0 or width * height > MaxPixels:
                raise ValueError(f"{path}: invalid dimensions or image exceeds 16,777,216 pixels")
            if color not in (0, 2, 3, 4, 6) or (depth != 8 and not (color == 3 and depth in (1, 2, 4))):
                raise ValueError(f"{path}: unsupported PNG color type/bit depth {color}/{depth}")
            if compression != 0 or filtering != 0 or interlace != 0:
                raise ValueError(f"{path}: unsupported PNG compression, filtering, or interlace")
        elif kind == b"PLTE":
            if palette is not None or seenData or transparency is not None or color in (0, 4):
                raise ValueError(f"{path}: invalid PLTE ordering or color type")
            if length == 0 or length % 3 or length > 768 or (color == 3 and length // 3 > 1 << depth):
                raise ValueError(f"{path}: invalid PNG palette size")
            palette = bytes(payload)
        elif kind == b"tRNS":
            if transparency is not None or seenData:
                raise ValueError(f"{path}: invalid tRNS ordering")
            if color == 0:
                valid = length == 2 and struct.unpack(">H", payload)[0] < 256
            elif color == 2:
                valid = length == 6 and all(value < 256 for value in struct.unpack(">HHH", payload))
            elif color == 3:
                valid = palette is not None and 0 < length <= len(palette) // 3
            else:
                valid = False
            if not valid:
                raise ValueError(f"{path}: invalid PNG transparency")
            transparency = bytes(payload)
        elif kind == b"IDAT":
            if endedData or (color == 3 and palette is None):
                raise ValueError(f"{path}: invalid IDAT ordering or missing palette")
            seenData = True
            compressed.extend(payload)
        elif kind == b"IEND":
            if length or not seenData or end != len(data):
                raise ValueError(f"{path}: invalid IEND or trailing data")
            endedFile = True
            break
        elif kind in (b"acTL", b"fcTL", b"fdAT"):
            raise ValueError(f"{path}: animated PNGs are unsupported")
        elif not kind[0] & 32:
            raise ValueError(f"{path}: unsupported critical PNG chunk {kind!r}")
        offset = end
    if not endedFile:
        raise ValueError(f"{path}: missing IEND")

    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[color]
    rowBytes = (width * channels * depth + 7) // 8
    pixelBytes = max(1, (channels * depth + 7) // 8)
    expected = height * (rowBytes + 1)
    decoder = zlib.decompressobj()
    raw = decoder.decompress(compressed, expected + 1)
    if len(raw) != expected or not decoder.eof or decoder.unused_data or decoder.unconsumed_tail:
        raise ValueError(f"{path}: invalid PNG decompression size or stream")
    image = Image(width, height, encoding=(depth, color))
    previous = bytearray(rowBytes)
    transparentSample = None
    if transparency is not None and color in (0, 2):
        transparentSample = struct.unpack(">H" if color == 0 else ">HHH", transparency)
    for y in range(height):
        start = y * (rowBytes + 1)
        filterType = raw[start]
        row = bytearray(raw[start + 1:start + 1 + rowBytes])
        if filterType > 4:
            raise ValueError(f"{path}: invalid PNG scanline filter {filterType}")
        if filterType:
            for x in range(rowBytes):
                left = row[x - pixelBytes] if x >= pixelBytes else 0
                above = previous[x]
                corner = previous[x - pixelBytes] if x >= pixelBytes else 0
                if filterType == 1:
                    predictor = left
                elif filterType == 2:
                    predictor = above
                elif filterType == 3:
                    predictor = (left + above) // 2
                else:
                    predictor = Paeth(left, above, corner)
                row[x] = (row[x] + predictor) & 255
        destination = y * width * 4
        if color == 6:
            image.pixels[destination:destination + width * 4] = row
        else:
            for x in range(width):
                if color == 2:
                    red, green, blue = row[x * 3:x * 3 + 3]
                    alpha = 0 if (red, green, blue) == transparentSample else 255
                elif color in (0, 4):
                    red = green = blue = row[x * channels]
                    alpha = row[x * 2 + 1] if color == 4 else (0 if (red,) == transparentSample else 255)
                else:
                    index = (row[x * depth // 8] >> (8 - depth - x * depth % 8)) & ((1 << depth) - 1)
                    if index * 3 + 3 > len(palette):
                        raise ValueError(f"{path}: PNG palette index out of range")
                    red, green, blue = palette[index * 3:index * 3 + 3]
                    alpha = transparency[index] if transparency is not None and index < len(transparency) else 255
                image.pixels[destination + x * 4:destination + x * 4 + 4] = bytes((red, green, blue, alpha))
        previous = row
    return image


def Chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(data, zlib.crc32(kind)))


def EncodePng(image):
    stride = image.width * 4
    raw = bytearray((stride + 1) * image.height)
    for y in range(image.height):
        raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)] = image.pixels[y * stride:(y + 1) * stride]
    header = struct.pack(">IIBBBBB", image.width, image.height, 8, 6, 0, 0, 0)
    encoded = PngSignature + Chunk(b"IHDR", header) + Chunk(b"IDAT", zlib.compress(raw, 9)) + Chunk(b"IEND", b"")
    if len(encoded) > MaxFileBytes:
        raise ValueError("output PNG exceeds the 64 MiB encoded limit")
    return encoded


def Crop(image, x, y, width, height):
    if x < 0 or y < 0 or x + width > image.width or y + height > image.height:
        raise ValueError("crop rectangle is outside the image")
    result = Image(width, height)
    for row in range(height):
        start = ((y + row) * image.width + x) * 4
        result.pixels[row * width * 4:(row + 1) * width * 4] = image.pixels[start:start + width * 4]
    return result


def ClosestColor(red, green, blue, palette):
    return min(palette, key=lambda color: (red - color[0]) ** 2 + (green - color[1]) ** 2 + (blue - color[2]) ** 2)


def Resize(image, width, height, method="nearest", palette=None, alphaMin=None, coverage=0.5):
    result = Image(width, height)
    if method == "box" and (width > image.width or height > image.height):
        raise ValueError("box resizing only supports downsampling; use nearest for enlargement")
    for y in range(height):
        for x in range(width):
            destination = (y * width + x) * 4
            if method == "nearest":
                sourceX = (2 * x + 1) * image.width // (2 * width)
                sourceY = (2 * y + 1) * image.height // (2 * height)
                start = (sourceY * image.width + sourceX) * 4
                red, green, blue, alpha = image.pixels[start:start + 4]
            else:
                left, right = x * image.width // width, (x + 1) * image.width // width
                top, bottom = y * image.height // height, (y + 1) * image.height // height
                red = green = blue = alpha = count = 0
                for sourceY in range(top, bottom):
                    for sourceX in range(left, right):
                        start = (sourceY * image.width + sourceX) * 4
                        if alphaMin is not None and image.pixels[start + 3] < alphaMin:
                            continue
                        red += image.pixels[start]
                        green += image.pixels[start + 1]
                        blue += image.pixels[start + 2]
                        alpha += image.pixels[start + 3]
                        count += 1
                if not count or (alphaMin is not None and count < (right - left) * (bottom - top) * coverage):
                    continue
                red, green, blue = red / count, green / count, blue / count
                alpha = 255 if alphaMin is not None else alpha / count
            # Palette selection deliberately precedes integer channel rounding.
            if palette is not None:
                red, green, blue = ClosestColor(red, green, blue, palette)
            result.pixels[destination:destination + 4] = bytes(int(value + 0.5) for value in (red, green, blue, alpha))
    return result


def CleanAlpha(image, opaque=False, threshold=None, border=0, isolated=False, zeroRgb=False):
    result = Image(image.width, image.height, bytearray(image.pixels))
    for y in range(image.height):
        for x in range(image.width):
            start = (y * image.width + x) * 4
            if opaque:
                result.pixels[start + 3] = 255
            elif threshold is not None:
                result.pixels[start + 3] = 255 if result.pixels[start + 3] >= threshold else 0
            if x < border or y < border or x >= image.width - border or y >= image.height - border:
                result.pixels[start:start + 4] = b"\0\0\0\0"
    if isolated:
        original = result.pixels[:]
        for y in range(image.height):
            for x in range(image.width):
                start = (y * image.width + x) * 4
                if not original[start + 3]:
                    continue
                neighbors = any(
                    original[(ny * image.width + nx) * 4 + 3]
                    for ny in range(max(0, y - 1), min(image.height, y + 2))
                    for nx in range(max(0, x - 1), min(image.width, x + 2))
                    if (nx, ny) != (x, y)
                )
                if not neighbors:
                    result.pixels[start:start + 4] = b"\0\0\0\0"
    if zeroRgb:
        for start in range(0, len(result.pixels), 4):
            if not result.pixels[start + 3]:
                result.pixels[start:start + 3] = b"\0\0\0"
    return result


def Transform(image, flipX, flipY, rotation):
    width, height = (image.height, image.width) if rotation in (90, 270) else (image.width, image.height)
    result = Image(width, height)
    for y in range(image.height):
        for x in range(image.width):
            dx = image.width - 1 - x if flipX else x
            dy = image.height - 1 - y if flipY else y
            if rotation == 90:
                dx, dy = image.height - 1 - dy, dx
            elif rotation == 180:
                dx, dy = image.width - 1 - dx, image.height - 1 - dy
            elif rotation == 270:
                dx, dy = dy, image.width - 1 - dx
            start, destination = (y * image.width + x) * 4, (dy * width + dx) * 4
            result.pixels[destination:destination + 4] = image.pixels[start:start + 4]
    return result


def ApplyPalette(image, palette):
    result = Image(image.width, image.height, bytearray(image.pixels))
    for start in range(0, len(result.pixels), 4):
        result.pixels[start:start + 3] = bytes(ClosestColor(*result.pixels[start:start + 3], palette))
    return result


def CopyInto(destination, image, left, top):
    for y in range(image.height):
        start = ((top + y) * destination.width + left) * 4
        destination.pixels[start:start + image.width * 4] = image.pixels[y * image.width * 4:(y + 1) * image.width * 4]


def Assemble(images, columns, cell, spacing, margin):
    width, height = cell if cell else (images[0].width, images[0].height)
    if any(image.width > width or image.height > height for image in images):
        raise ValueError("an input is larger than the assembly cell")
    if cell is None and any((image.width, image.height) != (width, height) for image in images):
        raise ValueError("unequal images require an explicit --cell")
    rows = (len(images) + columns - 1) // columns
    result = Image(columns * width + (columns - 1) * spacing + 2 * margin,
                   rows * height + (rows - 1) * spacing + 2 * margin)
    for index, image in enumerate(images):
        CopyInto(result, image, margin + index % columns * (width + spacing), margin + index // columns * (height + spacing))
    return result


def Preview(images, columns, scale):
    cell = max(image.width for image in images), max(image.height for image in images)
    sheet = Assemble(images, columns, cell, 1, 1)
    for y in range(sheet.height):
        for x in range(sheet.width):
            start = (y * sheet.width + x) * 4
            background = 192 if (x // 8 + y // 8) % 2 else 128
            alpha = sheet.pixels[start + 3]
            for channel in range(3):
                sheet.pixels[start + channel] = (sheet.pixels[start + channel] * alpha + background * (255 - alpha) + 127) // 255
            sheet.pixels[start + 3] = 255
    return Resize(sheet, sheet.width * scale, sheet.height * scale)


def Inspect(path, image):
    transparent = partial = opaque = 0
    left, top, right, bottom = image.width, image.height, 0, 0
    colors = set()
    for y in range(image.height):
        for x in range(image.width):
            start = (y * image.width + x) * 4
            color = bytes(image.pixels[start:start + 4])
            colors.add(color)
            if color[3] == 0:
                transparent += 1
            else:
                if color[3] == 255:
                    opaque += 1
                else:
                    partial += 1
                left, top, right, bottom = min(left, x), min(top, y), max(right, x + 1), max(bottom, y + 1)
    names = {0: "grayscale", 2: "RGB", 3: "indexed", 4: "grayscale-alpha", 6: "RGBA"}
    depth, colorType = image.encoding
    bounds = "empty" if right == 0 else f"{left},{top},{right - left},{bottom - top}"
    return (f"{path}: {image.width}x{image.height}, {names[colorType]} {depth}-bit; "
            f"RGBA colors={len(colors)}, alpha(clear/partial/opaque)={transparent}/{partial}/{opaque}, "
            f"visible bounds(x,y,w,h)={bounds}")


def ReadLines(path):
    if path.stat().st_size > 1024 * 1024:
        raise ValueError(f"{path}: text data exceeds 1 MiB")
    return [line.strip() for line in path.read_text(encoding="utf-8-sig").splitlines()
            if line.strip() and not line.lstrip().startswith("//")]


def ReadPalette(path):
    lines = ReadLines(path)
    if not 1 <= len(lines) <= 256:
        raise ValueError(f"{path}: a palette needs 1 to 256 RGB colors")
    result = []
    for line in lines:
        value = line.removeprefix("#")
        if len(value) != 6 or any(character not in "0123456789abcdefABCDEF" for character in value):
            raise ValueError(f"{path}: expected one RRGGBB or #RRGGBB color per line")
        result.append(tuple(bytes.fromhex(value)))
    return result


def ExpandInputs(patterns):
    result = []
    identities = set()
    for pattern in patterns:
        paths = [Path(pattern)] if Path(pattern).is_file() else [Path(match) for match in sorted(glob.glob(pattern))]
        if not paths:
            raise ValueError(f"no input files match: {pattern}")
        for path in paths:
            if not path.is_file():
                raise ValueError(f"input is not a file: {path}")
            stat = path.stat()
            identity = stat.st_dev, stat.st_ino
            if identity in identities:
                raise ValueError(f"duplicate input: {path}")
            identities.add(identity)
            result.append(path)
            if len(result) > MaxFiles:
                raise ValueError("at most 4096 input files are supported")
    return result


def UnderDirectory(directory, name):
    relative = Path(name)
    if relative.is_absolute() or any(part == ".." for part in relative.parts) or "\\" in name or ":" in name:
        raise ValueError(f"output name must be a contained relative PNG path: {name}")
    path = directory / relative
    if not path.resolve().is_relative_to(directory.resolve()):
        raise ValueError(f"output escapes its directory: {name}")
    return path


def SplitJobs(image, args):
    margin, spacing = args.margin, args.spacing
    if args.cell:
        width, height = args.cell
        usableWidth = image.width - 2 * margin + spacing
        usableHeight = image.height - 2 * margin + spacing
        if usableWidth <= 0 or usableHeight <= 0 or usableWidth % (width + spacing) or usableHeight % (height + spacing):
            raise ValueError("image does not divide exactly into cells, spacing, and margins")
        columns, rows = usableWidth // (width + spacing), usableHeight // (height + spacing)
    else:
        columns, rows = args.grid
    if columns * rows > MaxFiles:
        raise ValueError("at most 4096 split outputs are supported")
    width = image.width - 2 * margin - (columns - 1) * spacing
    height = image.height - 2 * margin - (rows - 1) * spacing
    if width < columns or height < rows:
        raise ValueError("grid, spacing, and margins leave empty cells")
    names = ReadLines(args.names_file) if args.names_file else [f"{index:04d}.png" for index in range(columns * rows)]
    if len(names) != columns * rows:
        raise ValueError("filename list must contain exactly one PNG path per cell")
    result = []
    for index, name in enumerate(names):
        x, y = index % columns, index // columns
        left = margin + x * width // columns + x * spacing
        top = margin + y * height // rows + y * spacing
        cellWidth = (x + 1) * width // columns - x * width // columns
        cellHeight = (y + 1) * height // rows - y * height // rows
        result.append((UnderDirectory(args.output_dir, name), Crop(image, left, top, cellWidth, cellHeight)))
    return result


def CheckDestinations(jobs, inputs, force):
    resolved = set()
    inputIdentities = {(path.stat().st_dev, path.stat().st_ino) for path in inputs}
    inputPaths = {path.resolve() for path in inputs}
    destinationIdentities = set()
    for path, image in jobs:
        if path.suffix.lower() != ".png":
            raise ValueError(f"output must have a .png extension: {path}")
        absolute = path.resolve()
        if absolute in resolved:
            raise ValueError(f"duplicate output destination: {path}")
        resolved.add(absolute)
        if absolute in inputPaths:
            raise ValueError(f"cannot overwrite an input: {path}")
        if path.exists():
            stat = path.stat()
            identity = stat.st_dev, stat.st_ino
            if identity in inputIdentities:
                raise ValueError(f"cannot overwrite an input alias: {path}")
            if identity in destinationIdentities:
                raise ValueError(f"duplicate output alias: {path}")
            destinationIdentities.add(identity)
            if not path.is_file():
                raise ValueError(f"output is not a file: {path}")
            if not force:
                raise ValueError(f"output already exists (use --force): {path}")
        for parent in path.parents:
            if parent.exists() and not parent.is_dir():
                raise ValueError(f"output parent is not a directory: {parent}")
    if sum(len(image.pixels) for path, image in jobs) > MaxBatchBytes:
        raise ValueError("output images exceed the 256 MiB batch limit")


def WriteOutput(path, encoded, force):
    path.parent.mkdir(parents=True, exist_ok=True)
    if force:
        temporary = None
        try:
            with tempfile.NamedTemporaryFile(dir=path.parent, prefix=".graphics-", suffix=".tmp", delete=False) as stream:
                temporary = Path(stream.name)
                stream.write(encoded)
            temporary.replace(path)
        finally:
            if temporary is not None:
                temporary.unlink(missing_ok=True)
    else:
        # Exclusive creation also protects against a destination created after preflight.
        with path.open("xb") as stream:
            try:
                stream.write(encoded)
            except OSError:
                stream.close()
                path.unlink(missing_ok=True)
                raise


def Positive(value):
    number = int(value)
    if number <= 0:
        raise argparse.ArgumentTypeError("must be positive")
    return number


def Nonnegative(value):
    number = int(value)
    if number < 0:
        raise argparse.ArgumentTypeError("must be nonnegative")
    return number


def ByteValue(value):
    number = int(value)
    if not 0 <= number <= 255:
        raise argparse.ArgumentTypeError("must be in 0..255")
    return number


def Coverage(value):
    number = float(value)
    if not 0 <= number <= 1:
        raise argparse.ArgumentTypeError("must be in 0..1")
    return number


def BuildParser():
    parser = argparse.ArgumentParser(description=__doc__)
    subcommands = parser.add_subparsers(dest="command", required=True)
    descriptions = (
        ("inspect", "Report PNG encoding, transparency, visible bounds, and RGBA color count."),
        ("crop", "Extract a rectangle from each image."),
        ("resize", "Resize each image using center nearest sampling or box downsampling."),
        ("split", "Split one sheet into row-major PNGs, default names 0000.png, 0001.png, ..."),
        ("assemble", "Place ordered images at cell top-lefts in a transparent grid."),
        ("transform", "Apply source-space flips, then a clockwise quarter-turn rotation."),
        ("alpha", "Apply opacity/threshold, border clearing, isolated-pixel removal, then RGB clearing."),
        ("palette", "Map RGB to an ordered palette without changing alpha."),
        ("preview", "Make a checkerboard contact sheet, preserving input order."),
    )
    for name, description in descriptions:
        command = subcommands.add_parser(name, help=description, description=description)
        command.add_argument("inputs", nargs="+", metavar="INPUT", help="input files or quoted glob patterns")
        if name != "inspect":
            if name == "split":
                command.add_argument("--output-dir", type=Path, required=True)
            elif name in ("assemble", "preview"):
                command.add_argument("-o", "--output", type=Path, required=True)
            else:
                outputs = command.add_mutually_exclusive_group(required=True)
                outputs.add_argument("-o", "--output", type=Path, help="one input only")
                outputs.add_argument("--output-dir", type=Path, help="batch output using input basenames")
            command.add_argument("--force", action="store_true", help="replace existing outputs; never overwrite inputs")
        if name == "crop":
            command.add_argument("--rect", type=int, nargs=4, required=True, metavar=("X", "Y", "W", "H"))
        elif name == "resize":
            command.add_argument("--size", type=Positive, nargs=2, required=True, metavar=("W", "H"))
            command.add_argument("--filter", choices=("nearest", "box"), default="nearest")
            command.add_argument("--palette", type=Path, help="snap unrounded RGB averages to this palette")
            command.add_argument("--sample-alpha-min", type=ByteValue, help="box only: discard samples below this alpha; emit binary alpha")
            command.add_argument("--min-coverage", type=Coverage, help="box with alpha gate: minimum covered fraction (default 0.5)")
        elif name == "split":
            layout = command.add_mutually_exclusive_group(required=True)
            layout.add_argument("--grid", type=Positive, nargs=2, metavar=("COLUMNS", "ROWS"))
            layout.add_argument("--cell", type=Positive, nargs=2, metavar=("W", "H"))
            command.add_argument("--names-file", type=Path, help="one relative .png path per cell")
        elif name in ("assemble", "preview"):
            command.add_argument("--columns", type=Positive, required=True)
            if name == "assemble":
                command.add_argument("--cell", type=Positive, nargs=2, metavar=("W", "H"))
            else:
                command.add_argument("--scale", type=Positive, default=4)
        elif name == "transform":
            command.add_argument("--flip-x", action="store_true")
            command.add_argument("--flip-y", action="store_true")
            command.add_argument("--rotate", type=int, choices=(0, 90, 180, 270), default=0)
        elif name == "alpha":
            opacity = command.add_mutually_exclusive_group()
            opacity.add_argument("--opaque", action="store_true")
            opacity.add_argument("--threshold", type=ByteValue)
            command.add_argument("--clear-border", type=Nonnegative, default=0)
            command.add_argument("--remove-isolated", action="store_true")
            command.add_argument("--zero-hidden-rgb", action="store_true")
        elif name == "palette":
            command.add_argument("--palette", type=Path, required=True)
        if name in ("split", "assemble"):
            command.add_argument("--spacing", type=Nonnegative, default=0)
            command.add_argument("--margin", type=Nonnegative, default=0)
    return parser


def Run(args):
    paths = ExpandInputs(args.inputs)
    if args.command == "resize":
        if args.filter != "box" and (args.sample_alpha_min is not None or args.min_coverage is not None):
            raise ValueError("alpha sampling options require --filter box")
        if args.min_coverage is not None and args.sample_alpha_min is None:
            raise ValueError("--min-coverage requires --sample-alpha-min")
    if args.command == "split" and len(paths) != 1:
        raise ValueError("split requires exactly one source sheet")
    if getattr(args, "output", None) is not None and args.command not in ("assemble", "preview") and len(paths) != 1:
        raise ValueError("multiple inputs require --output-dir")
    palettePath = getattr(args, "palette", None)
    palette = ReadPalette(palettePath) if palettePath is not None else None
    images = []
    totalBytes = 0
    for path in paths:
        image = ReadPng(path)
        totalBytes += len(image.pixels)
        if totalBytes > MaxBatchBytes:
            raise ValueError("input images exceed the 256 MiB batch limit")
        images.append(image)
    if args.command == "inspect":
        for path, image in zip(paths, images):
            print(Inspect(path, image))
        return
    if args.command == "split":
        jobs = SplitJobs(images[0], args)
    elif args.command == "assemble":
        jobs = [(args.output, Assemble(images, args.columns, args.cell, args.spacing, args.margin))]
    elif args.command == "preview":
        jobs = [(args.output, Preview(images, args.columns, args.scale))]
    else:
        jobs = []
        outputBytes = 0
        for path, image in zip(paths, images):
            if args.command == "crop":
                result = Crop(image, *args.rect)
            elif args.command == "resize":
                result = Resize(image, *args.size, args.filter, palette, args.sample_alpha_min,
                                args.min_coverage if args.min_coverage is not None else 0.5)
            elif args.command == "transform":
                result = Transform(image, args.flip_x, args.flip_y, args.rotate)
            elif args.command == "alpha":
                result = CleanAlpha(image, args.opaque, args.threshold, args.clear_border, args.remove_isolated, args.zero_hidden_rgb)
            else:
                result = ApplyPalette(image, palette)
            outputBytes += len(result.pixels)
            if outputBytes > MaxBatchBytes:
                raise ValueError("output images exceed the 256 MiB batch limit")
            destination = args.output if args.output is not None else UnderDirectory(args.output_dir, path.name)
            jobs.append((destination, result))
    protected = paths[:]
    if palettePath is not None:
        protected.append(palettePath)
    if getattr(args, "names_file", None) is not None:
        protected.append(args.names_file)
    CheckDestinations(jobs, protected, args.force)
    # Encoding also completes before the first filesystem mutation.
    encoded = [(path, EncodePng(image)) for path, image in jobs]
    for path, data in encoded:
        WriteOutput(path, data, args.force)
        print(path)


def Main():
    args = BuildParser().parse_args()
    try:
        Run(args)
    except (OSError, ValueError, RuntimeError, zlib.error) as error:
        print(f"graphics: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(Main())
