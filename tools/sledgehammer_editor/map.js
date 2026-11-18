import {element} from "./forms.js";

const colors = {
    black: "#171b1c", dark_gray: "#737b79", red: "#a95351", light_red: "#ec776c", green: "#528160",
    light_green: "#a5c882", cyan: "#518b90", light_cyan: "#8ac7c7", blue: "#576caa", light_blue: "#92a9e5",
    magenta: "#9d659f", light_magenta: "#d59bcb", brown: "#9a7953", yellow: "#a78d52",
    light_yellow: "#e6cc80", light_gray: "#b6bcb4", white: "#edf0e5",
};

const imageCache = new Map();

export function color(value, fallback = "#737b79") {
    return /^#[a-f\d]{6}$/i.test(value ?? "") ? value : colors[value] ?? fallback;
}

function image(path, redraw) {
    if (!path) return null;
    if (!imageCache.has(path)) {
        const bitmap = new Image();
        bitmap.src = "/image/" + path.split("/").map(encodeURIComponent).join("/");
        imageCache.set(path, bitmap);
    }
    const bitmap = imageCache.get(path);
    if (!bitmap.complete) bitmap.addEventListener("load", redraw, {once: true});
    return bitmap.complete && bitmap.naturalWidth ? bitmap : null;
}

function paintImage(context, path, x, z, size, redraw, rotation = 0, flipX = false, flipY = false) {
    const bitmap = image(path, redraw);
    if (!bitmap) return false;
    const scale = Math.min(size / bitmap.width, size / bitmap.height);
    const width = bitmap.width * scale;
    const height = bitmap.height * scale;
    context.save();
    context.translate(x + size / 2, z + size / 2);
    context.rotate(rotation * Math.PI / 180);
    context.scale(flipX ? -1 : 1, flipY ? -1 : 1);
    context.drawImage(bitmap, -width / 2, -height / 2, width, height);
    context.restore();
    return true;
}

export function recipe(value) {
    return typeof value === "string" ? {terrain: value} : value ?? {};
}

function canvasBox(canvas) {
    const rect = canvas.getBoundingClientRect();
    return {
        left: rect.left + canvas.clientLeft,
        top: rect.top + canvas.clientTop,
        width: rect.width - 2 * canvas.clientLeft,
        height: rect.height - 2 * canvas.clientTop,
    };
}

export function mapZoom(viewport, canvas, levels, initial, resize) {
    let level = initial;

    function zoomTo(next, point) {
        if (next === level) return;
        const before = canvasBox(canvas);
        const view = viewport.getBoundingClientRect();
        const x = point?.clientX ?? view.left + viewport.clientWidth / 2;
        const y = point?.clientY ?? view.top + viewport.clientHeight / 2;
        const column = (x - before.left) / before.width;
        const row = (y - before.top) / before.height;
        level = next;
        resize(level);
        const after = canvasBox(canvas);
        viewport.scrollLeft = Math.round(viewport.scrollLeft + after.left + column * after.width - x);
        viewport.scrollTop = Math.round(viewport.scrollTop + after.top + row * after.height - y);
    }

    canvas.addEventListener("wheel", event => {
        if (!event.deltaY || event.ctrlKey || event.metaKey || event.shiftKey) return;
        event.preventDefault();
        const step = event.deltaY < 0 ? 1 : -1;
        const index = Math.max(0, Math.min(levels.length - 1, levels.indexOf(level) + step));
        zoomTo(levels[index], event);
    }, {passive: false});
    return zoomTo;
}

function line(first, last) {
    const steps = Math.max(Math.abs(last.x - first.x), Math.abs(last.z - first.z));
    if (!steps) return [first];
    return Array.from({length: steps + 1}, (_, step) => ({
        x: Math.round(first.x + (last.x - first.x) * step / steps),
        z: Math.round(first.z + (last.z - first.z) * step / steps),
    }));
}

function rectangle(first, last) {
    const cells = [];
    for (let z = Math.min(first.z, last.z); z <= Math.max(first.z, last.z); z++) {
        for (let x = Math.min(first.x, last.x); x <= Math.max(first.x, last.x); x++) cells.push({x, z});
    }
    return cells;
}

export function flood(rows, selected) {
    const width = rows[0].length;
    const height = rows.length;
    const glyph = rows[selected.z][selected.x];
    const cells = [];
    const seen = new Set();
    const pending = [selected];
    for (let i = 0; i < pending.length; i++) {
        const {x, z} = pending[i];
        if (x < 0 || x >= width || z < 0 || z >= height || rows[z][x] !== glyph || seen.has(z * width + x)) continue;
        seen.add(z * width + x);
        cells.push({x, z});
        pending.push({x: x - 1, z}, {x: x + 1, z}, {x, z: z - 1}, {x, z: z + 1});
    }
    return cells;
}

export function layoutCanvas(options) {
    const {rows, palette, entrance, selected, visual, appearance, markers, tool, glyph} = options;
    let size = options.size;
    const width = rows[0]?.length ?? 0;
    const height = rows.length;
    const canvas = element("canvas", {
        class: "layout-canvas", width: width * size, height: height * size, tabindex: 0,
        "aria-label": "Pattern map. Use arrow keys to select a cell and Enter to use the current tool.",
    });
    const context = canvas.getContext("2d");
    let stroke;
    let hover;
    const styles = new Map(Object.entries(palette).map(([key, value]) => {
        const cell = recipe(value);
        return [key, {cell, look: appearance(cell.terrain), terrainArt: visual("terrain", cell.terrain),
            objectArt: cell.object ? visual("objects", cell.object) : null}];
    }));

    function draw() {
        if (!canvas.isConnected && canvas.dataset.ready) return;
        context.imageSmoothingEnabled = false;
        context.textAlign = "center";
        context.textBaseline = "middle";
        context.font = Math.round(size * 0.52) + "px monospace";
        const pending = stroke && tool === "rectangle" ? rectangle(stroke.first, stroke.last) : stroke?.points.values();
        const painted = new Set(Array.from(pending ?? [], point => point.z * width + point.x));
        for (let z = 0; z < height; z++) {
            for (let x = 0; x < width; x++) {
                const dx = x * size;
                const dz = z * size;
                const {cell, look, terrainArt, objectArt} = styles.get(painted.has(z * width + x) ? glyph : rows[z][x]) ??
                    {cell: {}, look: {}};
                context.fillStyle = color(look.background, "#222a2a");
                context.fillRect(dx, dz, size, size);
                if (!paintImage(context, terrainArt?.path, dx, dz, size, draw,
                    terrainArt?.rotation, terrainArt?.flipX, terrainArt?.flipY)) {
                    context.fillStyle = color(look.foreground);
                    context.fillText(look.symbol || rows[z][x], dx + size / 2, dz + size / 2);
                }
                if (cell.object && !paintImage(context, objectArt?.path, dx + 2, dz + 2, size - 4, draw,
                    facingRotation(cell.facing))) {
                    context.fillStyle = "#dcaf75";
                    context.fillRect(dx + size * .28, dz + size * .28, size * .44, size * .44);
                }
                const explicit = markers.get(z * width + x);
                if (explicit?.furniture) {
                    context.fillStyle = "#f2c486";
                    context.fillRect(dx + 2, dz + 2, 5, 5);
                }
                if (cell.loot?.length || explicit?.loot) {
                    context.fillStyle = "#f0cc68";
                    context.fillRect(dx + size - 6, dz + size - 6, 4, 4);
                }
                if (cell.monsters?.length || explicit?.monsters) {
                    context.fillStyle = "#f38b81";
                    context.fillRect(dx + size - 6, dz + 2, 4, 4);
                }
                context.strokeStyle = "#10181865";
                context.strokeRect(dx + .5, dz + .5, size, size);
            }
        }
        outline(entrance, "#aee395", 2);
        outline(hover, "#dce4d18a", 1);
        outline(selected, "#ebcc87", 2);
        canvas.dataset.ready = "true";
    }

    function outline(point, strokeColor, lineWidth) {
        if (!point) return;
        context.lineWidth = lineWidth;
        context.strokeStyle = strokeColor;
        const inset = lineWidth / 2;
        context.strokeRect(point.x * size + inset, point.z * size + inset, size - lineWidth, size - lineWidth);
        context.lineWidth = 1;
    }

    function position(event) {
        const box = canvasBox(canvas);
        return {
            x: Math.max(0, Math.min(width - 1, Math.floor((event.clientX - box.left) * width / box.width))),
            z: Math.max(0, Math.min(height - 1, Math.floor((event.clientY - box.top) * height / box.height))),
        };
    }

    function start(event) {
        if (event.button !== 0) return;
        const cell = position(event);
        if (!["brush", "rectangle"].includes(tool)) {
            options.apply(tool === "fill" ? flood(rows, cell) : [cell], cell);
            return;
        }
        canvas.setPointerCapture(event.pointerId);
        stroke = {first: cell, last: cell, points: new Map([[cell.z * width + cell.x, cell]])};
        draw();
    }

    function move(event) {
        const point = position(event);
        if (hover && point.x === hover.x && point.z === hover.z) return;
        hover = point;
        if (stroke) {
            for (const cell of line(stroke.last, point)) stroke.points.set(cell.z * width + cell.x, cell);
            stroke.last = point;
        }
        draw();
    }

    function end(event) {
        if (!stroke) return;
        move(event);
        const {first, last, points} = stroke;
        stroke = null;
        const cells = tool === "rectangle" ? rectangle(first, last) : [...points.values()];
        options.apply(cells, last);
    }

    canvas.addEventListener("pointerdown", start);
    canvas.addEventListener("pointermove", move);
    canvas.addEventListener("pointerup", end);
    canvas.addEventListener("pointercancel", () => { stroke = null; draw(); });
    canvas.addEventListener("pointerleave", () => { hover = null; draw(); });
    canvas.addEventListener("keydown", event => {
        const cell = selected ?? {x: 0, z: 0};
        const offsets = {ArrowLeft: [-1, 0], ArrowRight: [1, 0], ArrowUp: [0, -1], ArrowDown: [0, 1]};
        if (event.key === "Enter") {
            event.preventDefault();
            options.apply(tool === "fill" ? flood(rows, cell) : [cell], cell, true);
        } else if (offsets[event.key]) {
            event.preventDefault();
            const [dx, dz] = offsets[event.key];
            options.select({
                x: Math.max(0, Math.min(width - 1, cell.x + dx)), z: Math.max(0, Math.min(height - 1, cell.z + dz)),
            }, true);
        }
    });
    requestAnimationFrame(draw);
    return {
        canvas,
        resize(nextSize) {
            size = nextSize;
            canvas.width = width * size;
            canvas.height = height * size;
            draw();
        },
    };
}

function facingRotation(name) {
    return {north: 0, east: 90, south: 180, west: 270}[name] ?? 0;
}

function rgbHex(hex) {
    return [Number.parseInt(hex.slice(1, 3), 16), Number.parseInt(hex.slice(3, 5), 16), Number.parseInt(hex.slice(5, 7), 16)];
}

function terrainColor(terrain) {
    if (!terrain?.foreground) return [27, 35, 35];
    const foreground = rgbHex(terrain.foreground);
    const background = rgbHex(terrain.background);
    return foreground.map((channel, index) => Math.round(channel * .43 + background[index] * .57));
}

export function inside(bounds, x, z) {
    return x >= bounds[0] && x < bounds[2] && z >= bounds[1] && z < bounds[3];
}

export function renderRegion(canvas, result, layer, selected) {
    const [minX, minZ, maxX, maxZ] = result.bounds;
    const width = maxX - minX;
    const height = maxZ - minZ;
    canvas.width = width * 2;
    canvas.height = height * 2;
    const context = canvas.getContext("2d");
    const pixels = document.createElement("canvas");
    pixels.width = width;
    pixels.height = height;
    const buffer = pixels.getContext("2d");
    const data = buffer.createImageData(width, height);
    const palette = result.terrain.map(terrainColor);
    const road = [181, 158, 106];
    const meadow = [100, 130, 88];
    const woodland = [48, 89, 67];
    for (let index = 0; index < result.tiles.length; index++) {
        let rgb = palette[result.tiles[index]];
        if (layer === "biomes") rgb = result.biomes[index] ? woodland : meadow;
        if (layer === "roads") rgb = result.roads[index] ? road : rgb.map(value => Math.round(value * .57));
        const offset = index * 4;
        data.data[offset] = rgb[0];
        data.data[offset + 1] = rgb[1];
        data.data[offset + 2] = rgb[2];
        data.data[offset + 3] = 255;
    }
    buffer.putImageData(data, 0, 0);
    context.imageSmoothingEnabled = false;
    context.drawImage(pixels, 0, 0, width * 2, height * 2);
    if (layer === "parcels") {
        for (const parcel of result.parcels) {
            const [x, z, right, bottom] = parcel.bounds;
            context.strokeStyle = ["#a7be94", "#d4ac73", "#8fd5a3", "#bcb5e8"][parcel.site] ?? "#a7be94";
            context.strokeRect((x - minX) * 2 + .5, (z - minZ) * 2 + .5, (right - x) * 2 - 1, (bottom - z) * 2 - 1);
        }
    }
    if (layer === "spawns") {
        for (const marker of result.placements) {
            context.fillStyle = {furniture: "#a1d2db", loot: "#f0cc68", monster: "#f38b81"}[marker.kind];
            context.fillRect((marker.x - minX) * 2, (marker.z - minZ) * 2, 2, 2);
        }
    }
    if (selected && inside(result.bounds, selected.x, selected.z)) {
        const building = result.buildings.find(entry => inside(entry.bounds, selected.x, selected.z));
        context.strokeStyle = "#efcf83";
        if (building) {
            const [x, z, right, bottom] = building.bounds;
            context.strokeRect((x - minX) * 2 + .5, (z - minZ) * 2 + .5, (right - x) * 2 - 1, (bottom - z) * 2 - 1);
        }
        context.fillStyle = "#fff5db";
        context.fillRect((selected.x - minX) * 2, (selected.z - minZ) * 2, 2, 2);
    }
}

export function regionPosition(canvas, event, bounds) {
    const [x, z, right, bottom] = bounds;
    const box = canvasBox(canvas);
    return {x: x + Math.floor((event.clientX - box.left) * (right - x) / box.width),
        z: z + Math.floor((event.clientY - box.top) * (bottom - z) / box.height)};
}
