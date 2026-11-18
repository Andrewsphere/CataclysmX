import {button, element, mountFields, patternFields, placementFields, recipeFields, select, site} from "./forms.js";
import {layoutCanvas, mapZoom, recipe} from "./map.js";
import {equal} from "./source.js";
import {idIsValid} from "./model.js";

const glyphOrder = ".#:+>gTabcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789" +
    Array.from({length: 95}, (_, index) => String.fromCharCode(index + 32)).join("");
const tools = [["select", "Select"], ["brush", "Brush"], ["rectangle", "Rectangle"], ["fill", "Fill"], ["eyedropper", "Eyedropper"]];
const zoomOptions = [[14, "Small tiles"], [22, "Medium tiles"], [32, "Large tiles"], [44, "XL tiles"]];
const zoomLevels = zoomOptions.map(([size]) => size);

function layers(pattern) {
    return [
        {key: "surface", name: "Surface · Y 0", y: 0, rows: pattern.rows, markers: pattern},
        ...(pattern.layers ?? []).map(layer => ({
            key: "y" + layer.y, name: "Y " + layer.y, y: layer.y, rows: layer.rows, markers: pattern,
        })),
        ...(pattern.basements ?? []).map(basement => ({
            key: "basement:" + basement.id, name: basement.id + " · Y −1", y: -1, rows: basement.rows, markers: basement,
        })),
    ];
}

function setRows(pattern, key, rows) {
    if (key === "surface") pattern.rows = rows;
    else if (key.startsWith("basement:")) pattern.basements.find(entry => "basement:" + entry.id === key).rows = rows;
    else pattern.layers.find(entry => "y" + entry.y === key).rows = rows;
}

function ensureGlyph(pattern, cell, preferred, editCell = false) {
    if (preferred !== undefined && Object.hasOwn(pattern.palette, preferred) && equal(recipe(pattern.palette[preferred]), cell)) {
        return preferred;
    }
    for (const [glyph, value] of Object.entries(pattern.palette)) {
        if (equal(recipe(value), cell)) return glyph;
    }
    const stored = Object.keys(cell).length === 1 ? cell.terrain : cell;
    if (editCell && preferred !== undefined && layers(pattern).reduce((count, layer) =>
        count + layer.rows.reduce((total, row) => total + [...row].filter(glyph => glyph === preferred).length, 0), 0) === 1) {
        pattern.palette[preferred] = stored;
        return preferred;
    }
    const free = [...glyphOrder].find(glyph => !Object.hasOwn(pattern.palette, glyph));
    if (free === undefined) throw new Error("This pattern uses every palette glyph. Choose an existing recipe or remove an unused glyph.");
    pattern.palette[free] = stored;
    return free;
}

function paint(pattern, layer, cells, glyph) {
    const rows = layer.rows.map(row => [...row]);
    for (const {x, z} of cells) rows[z][x] = glyph;
    setRows(pattern, layer.key, rows.map(row => row.join("")));
}

function markersFor(pattern, layer) {
    const result = new Map();
    const containers = layer.markers === pattern ? [pattern] : [pattern, layer.markers];
    for (const container of containers) {
        for (const kind of ["furniture", "loot", "monsters"]) {
            for (const marker of container[kind] ?? []) {
                if (marker.y !== layer.y) continue;
                const key = marker.z * pattern.rows[0].length + marker.x;
                const entry = result.get(key) ?? {};
                entry[kind] = true;
                result.set(key, entry);
            }
        }
    }
    return result;
}

function dimensions(width, height) {
    if (![width, height].every(size => Number.isInteger(size) && size >= 1 && size <= 64)) {
        throw new Error("Width and height must be between 1 and 64.");
    }
}

function title(id) {
    return id.replaceAll("_", " ").replace(/^./, character => character.toUpperCase());
}

function nextPattern(app, id) {
    app.layout.id = id;
    app.layout.layer = "surface";
    app.layout.glyph = undefined;
    app.layout.brush = undefined;
    app.layout.cell = {x: 0, z: 0};
    app.layout.scroll = {left: 0, top: 0};
    app.layout.inspector = "cell";
    app.render();
}

function newPattern(app, original) {
    const options = original ? [] : [
        {key: "site", name: "Site", value: "building", options: site},
        {key: "width", name: "Width", value: "14", number: true},
        {key: "height", name: "Height", value: "12", number: true},
    ];
    app.dialog(original ? "Duplicate pattern" : "New pattern", [
        {key: "id", name: "Pattern ID", value: original ? original.id + "_copy" : "", placeholder: "garden_house"},
        ...options,
    ], values => {
        let pattern;
        if (original) {
            pattern = {...structuredClone(original.value), id: values.id};
        } else {
            const width = Number(values.width);
            const height = Number(values.height);
            dimensions(width, height);
            const outdoor = values.site === "park" || values.site === "square";
            const x = Math.floor(width / 2);
            const z = height - 1;
            const surface = outdoor ? (values.site === "square" ? "pavement" : "grass") : "floor";
            const rows = Array.from({length: height}, (_, row) => Array.from({length: width}, (_, column) => {
                if (outdoor || (row === z && column === x)) return ".";
                return row === 0 || row === z || column === 0 || column === width - 1 ? "#" : ".";
            }).join(""));
            pattern = {id: values.id, site: values.site, rows, palette: {".": surface}, entrance: {x, z}};
            if (!outdoor && rows.some(row => row.includes("#"))) pattern.palette["#"] = "wall";
        }
        app.model.create("building_pattern", pattern);
        app.layout.search = "";
        app.layout.listScroll = 0;
        app.changed();
        nextPattern(app, values.id);
    });
}

function addLayer(app, record, basement) {
    app.dialog(basement ? "New basement variant" : "New elevation", basement ? [
        {key: "id", name: "Variant ID", placeholder: "storage_cellar"},
        {key: "weight", name: "Weight", value: "1", number: true},
    ] : [{key: "y", name: "Elevation (Y)", value: "1", number: true}], values => {
        const pattern = structuredClone(app.model.get(record.type, record.id).value);
        const y = basement ? -1 : Number(values.y);
        if (!Number.isInteger(y) || y < -10 || y > 10 || y === 0) throw new Error("Choose a nonzero Y between −10 and 10.");
        if (pattern.layers?.some(layer => layer.y === y) || (!basement && y === -1 && pattern.basements?.length)) {
            throw new Error("That elevation is already in use.");
        }
        if (basement && (!idIsValid(values.id) || pattern.basements?.some(entry => entry.id === values.id))) {
            throw new Error("Choose a unique lowercase basement ID.");
        }
        const fill = ensureGlyph(pattern, {terrain: y < 0 ? "floor" : "air"});
        const stairs = new Set(app.model.records().filter(entry => entry.type === "terrain" && entry.value.stairs)
            .map(entry => entry.id));
        const rows = pattern.rows.map(row => [...row].map(glyph =>
            y === -1 && stairs.has(recipe(pattern.palette[glyph]).terrain) ? glyph : fill).join(""));
        if (basement) {
            const weight = Number(values.weight);
            if (!Number.isInteger(weight) || weight < 1 || weight > 4294967295) throw new Error("Enter a positive basement weight.");
            (pattern.basements ??= []).push({id: values.id, weight, rows});
        } else {
            (pattern.layers ??= []).push({y, rows});
        }
        app.layout.layer = basement ? "basement:" + values.id : "y" + y;
        app.update(record, pattern, true);
    });
}

function removeLayer(app, record, layer) {
    if (!window.confirm("Remove this layer and its coordinate placements?")) return;
    const pattern = structuredClone(record.value);
    if (layer.key.startsWith("basement:")) {
        pattern.basements = pattern.basements.filter(entry => "basement:" + entry.id !== layer.key);
        if (!pattern.basements.length) delete pattern.basements;
    } else {
        pattern.layers = pattern.layers.filter(entry => entry.y !== layer.y);
        if (!pattern.layers.length) delete pattern.layers;
    }
    if (!layers(pattern).some(entry => entry.y === layer.y)) {
        for (const key of ["furniture", "loot", "monsters"]) {
            if (pattern[key]) pattern[key] = pattern[key].filter(entry => entry.y !== layer.y);
        }
    }
    app.layout.layer = "surface";
    app.update(record, pattern, true);
}

function resize(app, record) {
    app.dialog("Resize pattern", [
        {key: "width", name: "Width", value: String(record.value.rows[0].length), number: true},
        {key: "height", name: "Height", value: String(record.value.rows.length), number: true},
    ], values => {
        const width = Number(values.width);
        const height = Number(values.height);
        dimensions(width, height);
        const pattern = structuredClone(record.value);
        if (pattern.entrance.x >= width || pattern.entrance.z >= height) throw new Error("Move the entrance before cropping its tile.");
        const outside = entry => entry.x < 0 || entry.z < 0 || entry.x >= width || entry.z >= height;
        const population = cell => cell.object || cell.loot?.length || cell.monsters?.length;
        const crops = layers(pattern).some(layer => layer.rows.some((row, z) =>
            [...row].some((glyph, x) => outside({x, z}) && population(recipe(pattern.palette[glyph])))));
        const coordinateCrops = [pattern, ...(pattern.basements ?? [])].some(container =>
            ["furniture", "loot", "monsters"].some(key => container[key]?.some(outside)));
        if ((crops || coordinateCrops) && !window.confirm("This resize removes furniture, loot, or monster placements. Continue?")) {
            return;
        }
        for (const layer of layers(pattern)) {
            const terrain = layer.y === 0 ? (pattern.site === "park" ? "grass" : pattern.site === "square" ? "pavement" : "floor")
                : layer.y < 0 ? "floor" : "air";
            const fill = ensureGlyph(pattern, {terrain});
            const rows = Array.from({length: height}, (_, z) => (layer.rows[z] ?? "").slice(0, width).padEnd(width, fill));
            setRows(pattern, layer.key, rows);
        }
        for (const container of [pattern, ...(pattern.basements ?? [])]) {
            for (const key of ["furniture", "loot", "monsters"]) {
                if (container[key]) container[key] = container[key].filter(entry => !outside(entry));
            }
        }
        app.layout.cell = {x: Math.min(width - 1, app.layout.cell.x), z: Math.min(height - 1, app.layout.cell.z)};
        app.update(record, pattern, true);
        const {x, z} = pattern.entrance;
        if (x > 0 && z > 0 && x < width - 1 && z < height - 1) {
            app.message("Move the entrance to the new boundary before saving.");
        }
    });
}

function fieldsOnly(value, fields) {
    return Object.fromEntries(fields.filter(field => Object.hasOwn(value, field.key)).map(field => [field.key, value[field.key]]));
}

function changedFields(value, fields, next) {
    const result = {...value};
    for (const {key} of fields) delete result[key];
    return {...result, ...next};
}

function cellInspector(app, record, layer, canvasRefresh) {
    const cell = app.layout.cell;
    const glyph = layer.rows[cell.z]?.[cell.x];
    const form = element("div");
    mountFields(form, recipe(record.value.palette[glyph]), recipeFields, type => app.model.references(type), value => {
        if (!value.terrain) return app.message("Choose terrain for this cell.");
        const pattern = structuredClone(app.model.get(record.type, record.id).value);
        const current = layers(pattern).find(entry => entry.key === layer.key);
        if (!value.object) delete value.facing;
        try {
            const changed = ensureGlyph(pattern, value, current.rows[cell.z][cell.x], true);
            paint(pattern, current, [cell], changed);
            app.update(record, pattern);
            canvasRefresh();
        } catch (error) {
            app.message(error.message);
        }
    });
    const hasMarkers = markersFor(record.value, layer).has(cell.z * record.value.rows[0].length + cell.x);
    return [
        element("div", {class: "panel-header"}, element("h2", {}, "Cell"),
            element("span", {class: "coord muted"}, "X " + cell.x + "  Z " + cell.z + "  Y " + layer.y)),
        ...(layer.y === 0 ? [element("p", {class: "sidebar-note"},
            button("Set as entrance", () => app.update(record, {...app.model.get(record.type, record.id).value,
                entrance: {...cell}}, true)))] : []),
        ...(hasMarkers ? [element("p", {class: "sidebar-note muted"}, "This cell also has coordinate placements. Edit them in Pattern.")] : []),
        form,
    ];
}

function patternInspector(app, record, layer, canvasRefresh) {
    const form = element("div");
    const coords = element("div");
    mountFields(form, fieldsOnly(record.value, patternFields), patternFields, type => app.model.references(type), value => {
        app.update(record, changedFields(app.model.get(record.type, record.id).value, patternFields, value));
        canvasRefresh();
    });
    const inBasement = layer.key.startsWith("basement:");
    const scope = inBasement ? layer.markers : record.value;
    const located = placementFields.map(field => ({...field, initial: {...field.initial, y: layer.y}}));
    mountFields(coords, scope, located, type => app.model.references(type), value => {
        const pattern = structuredClone(app.model.get(record.type, record.id).value);
        const target = inBasement ? pattern.basements.find(item => "basement:" + item.id === layer.key) : pattern;
        for (const {key} of placementFields) {
            if (Object.hasOwn(value, key)) target[key] = value[key];
            else delete target[key];
        }
        app.update(record, pattern);
        canvasRefresh();
    });
    const tools = element("div", {class: "inline sidebar-note"}, button("Resize", () => resize(app, record)),
        button("Duplicate", () => newPattern(app, record)),
        ...(layer.y !== 0 ? [button("Remove layer", () => removeLayer(app, record, layer))] : []));
    let weight;
    if (inBasement) {
        weight = element("label", {class: "field"}, element("span", {}, "Basement weight"),
            element("input", {type: "number", min: 1, max: 4294967295, value: layer.markers.weight ?? 1, onchange: event => {
                const pattern = structuredClone(app.model.get(record.type, record.id).value);
                const entry = pattern.basements.find(item => "basement:" + item.id === layer.key);
                entry.weight = Number(event.target.value);
                app.update(record, pattern);
            }}));
    }
    return [element("h2", {}, "Pattern settings"), tools, form, weight,
        element("h3", {}, inBasement ? "Basement placements" : "Common placements"), coords];
}

function paletteGroups(glyphs) {
    const groups = new Map();
    for (const [glyph, value] of glyphs) {
        const cell = recipe(value);
        const key = JSON.stringify([cell.terrain, cell.object]);
        const variants = groups.get(key) ?? [];
        const comparable = {
            facing: cell.object ? cell.facing ?? "north" : undefined,
            loot: (cell.loot ?? []).map(marker => ({chance: 10000, rolls: 1, destination: "ground", ...marker})),
            monsters: (cell.monsters ?? []).map(marker => ({chance: 10000, ...marker})),
        };
        const existing = variants.find(variant => equal(variant.comparable, comparable));
        if (existing) existing.glyphs.push(glyph);
        else variants.push({glyphs: [glyph], cell, comparable});
        groups.set(key, variants);
    }
    return groups.values();
}

function variantName({glyphs, comparable}) {
    const chance = value => value === 10000 ? "" : " (" + Number(value) / 100 + "%)";
    const parts = [
        comparable.facing && title(comparable.facing),
        ...comparable.loot.map(loot => "Loot " + loot.pool + " ×" + loot.rolls + ", " +
            loot.destination + chance(loot.chance)),
        ...comparable.monsters.map(spawn => "Spawn " + spawn.group + chance(spawn.chance)),
    ].filter(Boolean);
    return (glyphs[0] === " " ? "␣" : glyphs[0]) + " · " + (parts.join(" · ") || "Terrain only");
}

function palette(app, panel, record, chooseBrush) {
    const layout = app.layout;
    const glyphs = Object.entries(record.value.palette);
    const visual = app.model.visuals();
    const options = app.model.references("terrain");
    const picker = element("input", {list: "terrain-picker", placeholder: "Choose terrain brush…",
        "aria-label": "Terrain brush", onchange: event => {
            if (!options.some(([id]) => id === event.target.value)) return;
            chooseBrush(undefined, {terrain: event.target.value});
        }});
    if (layout.brush && layout.glyph === undefined) picker.value = layout.brush.terrain;
    const choices = element("datalist", {id: "terrain-picker"}, ...options.map(([id, name]) =>
        element("option", {value: id, label: name})));
    const grid = element("div", {class: "palette-grid"});
    let variantPicker;
    for (const variants of paletteGroups(glyphs)) {
        const active = variants.find(variant => variant.glyphs.includes(layout.glyph));
        const plain = variants.filter(({comparable}) => !comparable.loot.length && !comparable.monsters.length);
        const {cell, glyphs: aliases} = active ?? plain.find(({comparable}) => comparable.facing === "north") ??
            plain[0] ?? variants[0];
        const glyph = active ? layout.glyph : aliases[0];
        const label = [cell.object, cell.terrain].filter(Boolean).map(title).join(" · ");
        const art = visual(cell.object ? "objects" : "terrain", cell.object ?? cell.terrain);
        const description = variants.length > 1 ? label + " (" + variants.length + " variants)" : label;
        const node = button("", () => chooseBrush(glyph, recipe(record.value.palette[glyph])),
            {class: "palette-tile", title: description, "aria-label": description, "aria-pressed": String(Boolean(active))});
        if (art) node.append(element("img", {src: app.imageUrl(art.path), alt: ""}));
        node.append(element("span", {}, glyph === " " ? "␣" : glyph));
        if (variants.length > 1) {
            node.append(element("span", {class: "palette-variant-mark", "aria-hidden": "true"}, "▾"));
        }
        grid.append(node);
        if (active && variants.length > 1) {
            const picker = select(active.glyphs[0], variants.map(variant => [variant.glyphs[0], variantName(variant)]),
                glyph => chooseBrush(glyph, recipe(record.value.palette[glyph])), {"aria-label": "Palette variant"});
            variantPicker = element("label", {class: "field palette-variants"},
                element("span", {}, label + " variants"), picker);
        }
    }
    const used = new Set(layers(record.value).flatMap(layer => layer.rows.flatMap(row => [...row])));
    const unused = glyphs.filter(([glyph]) => !used.has(glyph));
    const remove = unused.length ? element("details", {class: "form-group"}, element("summary", {}, "Unused glyphs"),
        ...unused.map(([glyph]) => element("div", {class: "field-heading"}, element("code", {}, glyph === " " ? "␣" : glyph),
            button("Remove", () => {
                const value = structuredClone(app.model.get(record.type, record.id).value);
                delete value.palette[glyph];
                if (layout.glyph === glyph) { layout.glyph = undefined; layout.brush = undefined; }
                app.update(record, value, true);
            })))) : null;
    panel.replaceChildren(element("div", {class: "panel-header"}, element("h2", {}, "Palette")), picker, choices, grid);
    if (variantPicker) panel.append(variantPicker);
    if (remove) panel.append(remove);
}

export function renderLayout(app) {
    const state = app.layout;
    const records = app.model.records().filter(entry => entry.type === "building_pattern");
    const record = records.find(entry => entry.id === state.id) ?? records.find(entry => entry.id === "furnished_house") ?? records[0];
    state.id = record?.id;
    const sidebar = element("aside", {class: "side"});
    const library = element("div", {class: "pane-pad"});
    sidebar.append(library);
    app.library(library, "Patterns", records, record?.id, entry => nextPattern(app, entry.id),
        () => newPattern(app), entry => [title(entry.id), (entry.value.site ?? "building") + " · " + entry.value.rows[0].length + "×" + entry.value.rows.length],
        "Search patterns", state);
    if (!record) return [sidebar, element("div", {class: "empty"}, element("h2", {}, "Create your first pattern"))];

    const pattern = record.value;
    const available = layers(pattern);
    const layer = available.find(entry => entry.key === state.layer) ?? available[0];
    state.layer = layer.key;
    state.cell.x = Math.min(state.cell.x, layer.rows[0].length - 1);
    state.cell.z = Math.min(state.cell.z, layer.rows.length - 1);
    if (state.glyph !== undefined && !Object.hasOwn(pattern.palette, state.glyph)) state.glyph = undefined;
    if (!state.brush) {
        state.glyph = layer.rows[state.cell.z][state.cell.x];
        state.brush = recipe(pattern.palette[state.glyph]);
    }
    const palettePanel = element("div", {class: "pane-pad panel-rule"});
    sidebar.append(palettePanel);
    const map = element("div", {class: "workbench", onscroll: event => {
        state.scroll = {left: event.target.scrollLeft, top: event.target.scrollTop};
    }});
    const inspector = element("aside", {class: "inspector pane-pad"});
    let zoomMap;
    const zoomControl = select(state.zoom, zoomOptions, value => zoomMap(Number(value)), {"aria-label": "Map zoom"});
    const toolButtons = tools.map(([key, label]) =>
        button(label, () => { state.tool = key; app.render(); }, {"aria-pressed": String(key === state.tool)}));

    function chooseBrush(glyph, brush) {
        const focused = document.activeElement;
        let restoreFocus;
        if (palettePanel.contains(focused)) {
            if (focused.matches(".palette-tile")) restoreFocus = ".palette-tile[aria-pressed=true]";
            else if (focused.matches("select")) restoreFocus = "select";
            else restoreFocus = "input";
        }
        state.glyph = glyph;
        state.brush = brush;
        if (!["brush", "fill", "rectangle"].includes(state.tool)) {
            state.tool = "brush";
            toolButtons.forEach((node, index) => node.setAttribute("aria-pressed", String(tools[index][0] === state.tool)));
        }
        refreshCanvas();
        if (restoreFocus) palettePanel.querySelector(restoreFocus)?.focus({preventScroll: true});
    }

    function refreshCanvas(focus = false) {
        const live = app.model.get(record.type, record.id);
        const livePattern = live.value;
        const active = layers(livePattern).find(entry => entry.key === state.layer);
        const scrolled = state.scroll;
        const {canvas, resize} = layoutCanvas({
            rows: active.rows, palette: livePattern.palette, size: state.zoom, entrance: active.y === 0 ? livePattern.entrance : null,
            selected: state.cell, visual: app.model.visuals(), appearance: app.model.terrainLooks(),
            markers: markersFor(livePattern, active), tool: state.tool, glyph: state.glyph,
            apply: (cells, last, keyboard) => apply(live, active, cells, last, keyboard), select: (cell, keyboard) => {
                state.cell = cell;
                state.inspector = "cell";
                refreshCanvas(keyboard);
                refreshInspector();
            },
        });
        map.replaceChildren(canvas);
        zoomMap = mapZoom(map, canvas, zoomLevels, state.zoom, size => {
            state.zoom = size;
            zoomControl.value = String(size);
            resize(size);
        });
        requestAnimationFrame(() => {
            if (!canvas.isConnected) return;
            map.scrollLeft = scrolled.left;
            map.scrollTop = scrolled.top;
            if (focus) canvas.focus({preventScroll: true});
        });
        palette(app, palettePanel, live, chooseBrush);
    }

    function refreshInspector() {
        const live = app.model.get(record.type, record.id);
        const active = layers(live.value).find(entry => entry.key === state.layer);
        const tabs = element("div", {class: "inspector-tabs"}, ...[["cell", "Cell"], ["pattern", "Pattern"]].map(([key, label]) =>
            button(label, () => { state.inspector = key; refreshInspector(); }, {"aria-pressed": String(state.inspector === key)})));
        const refresh = () => refreshCanvas();
        inspector.replaceChildren(tabs, ...(state.inspector === "pattern" ?
            patternInspector(app, live, active, refresh) : cellInspector(app, live, active, refresh)));
    }

    function apply(live, active, cells, last, keyboard) {
        state.cell = last;
        state.inspector = "cell";
        if (state.tool === "eyedropper") {
            state.glyph = active.rows[last.z][last.x];
            state.brush = recipe(live.value.palette[state.glyph]);
        } else if (state.tool !== "select") {
            const next = structuredClone(live.value);
            try {
                state.glyph = ensureGlyph(next, state.brush, state.glyph);
                paint(next, active, cells, state.glyph);
                app.update(live, next);
            } catch (error) {
                app.message(error.message);
            }
        }
        refreshCanvas(keyboard);
        refreshInspector();
    }

    const heading = element("div", {class: "stage-header"}, element("div", {class: "stage-heading"},
        element("h1", {}, title(record.id)), element("span", {class: "coord muted"}, pattern.rows[0].length + " × " + pattern.rows.length)),
        element("small", {class: "code"}, record.id + " · " + (pattern.use ?? "mixed")));
    const toolBar = element("div", {class: "toolbar", role: "toolbar", "aria-label": "Map tools"}, ...toolButtons,
        element("span", {class: "toolbar-spacer"}), select(state.layer, available.map(entry => [entry.key, entry.name]),
            key => { state.layer = key; app.render(); }, {"aria-label": "Floor or basement"}),
        button("+ Layer", () => addLayer(app, record, false)), button("+ Basement", () => addLayer(app, record, true)),
        zoomControl);
    const legend = element("div", {class: "map-footer"},
        ...[["entrance", "Entrance"], ["loot", "Loot"], ["monster", "Monster"]].map(([key, label]) =>
            element("span", {class: "legend"}, element("span", {class: "legend-mark " + key}), label)));
    const stage = element("section", {class: "stage"}, heading, toolBar, map, legend);
    refreshCanvas();
    refreshInspector();
    return [sidebar, stage, inspector];
}
