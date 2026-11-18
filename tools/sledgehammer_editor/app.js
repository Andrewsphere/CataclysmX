import {button, element, select} from "./forms.js";
import {inside, mapZoom, regionPosition, renderRegion} from "./map.js";
import {Workspace} from "./model.js";
import {renderLayout} from "./layout.js";
import {renderCatalog} from "./catalog.js";

const workspace = document.querySelector("#workspace");
const packPicker = document.querySelector("#pack");
const saveButton = document.querySelector("#save");
const undoButton = document.querySelector("#undo");
const redoButton = document.querySelector("#redo");
const saveState = document.querySelector("#save-state");
const problems = document.querySelector("#problems");
const dialog = document.querySelector("#dialog");
const dialogForm = document.querySelector("#dialog-form");
const tabs = [...document.querySelectorAll("[data-tab]")];
const token = location.hash.slice(1);
const packs = {
    "data/content.json5": "Town", "data/first_house.json5": "First House",
    "data/demo.json5": "Demo", "data/zombie_showcase.json5": "Zombie Showcase",
};
const profiles = ["Legacy grid", "Main street", "Crossroads", "Offset neighborhood"];
const regionLayers = [["terrain", "Terrain"], ["roads", "Roads"], ["parcels", "Parcels"],
    ["biomes", "Biomes"], ["spawns", "Spawns"]];
const regionZoomLevels = [0.5, 0.75, 1, 1.25, 1.5, 2, 3, 4];

const app = {
    model: null,
    tab: "layout",
    busy: false,
    opened: false,
    layout: {layer: "surface", tool: "select", cell: {x: 0, z: 0}, zoom: 22, inspector: "cell",
        scroll: {left: 0, top: 0}, search: "", listScroll: 0},
    catalog: {type: "terrain", search: "", listScroll: 0},
    region: {seed: "42", regionX: 0, regionZ: 0, y: 0, layer: "terrain", outdated: true, zoom: 1,
        scroll: {left: 0, top: 0}},

    controls() {
        packPicker.disabled = this.busy || !this.model;
        saveButton.disabled = this.busy || !this.model;
        undoButton.disabled = this.busy || !this.model?.undo.length;
        redoButton.disabled = this.busy || !this.model?.redo.length;
        for (const button of tabs) {
            button.classList.toggle("active", this.tab === button.dataset.tab);
            button.setAttribute("aria-pressed", String(this.tab === button.dataset.tab));
            button.disabled = this.busy;
        }
        workspace.inert = this.busy;
        saveState.classList.toggle("dirty", Boolean(this.model?.dirty));
        saveState.textContent = this.status ?? (this.model?.dirty ? "Unsaved changes" : this.opened ? "Saved" : "Opening content…");
    },

    render() {
        this.controls();
        if (!this.model) return;
        workspace.className = "workspace " + this.tab;
        const render = {layout: renderLayout, catalog: renderCatalog, region: renderPreview};
        workspace.replaceChildren(...render[this.tab](this));
    },

    changed(rebuild = false) {
        this.region.outdated = true;
        this.status = null;
        if (rebuild) this.render();
        else this.controls();
    },

    update(record, next, rebuild = false) {
        if (this.model.update(record, next)) this.changed(rebuild);
    },

    message(error) { this.showProblems({error}); },

    showProblems(result) {
        problems.hidden = !result;
        if (!result) { problems.replaceChildren(); return; }
        const header = element("header", {}, element("strong", {}, result.error),
            button("Dismiss", () => this.showProblems(null)));
        const frames = (result.diagnostics ?? []).map(frame => {
            const place = [frame.file, frame.path, frame.line === undefined ? null : "line " + frame.line]
                .filter(Boolean).join(" · ");
            const entry = element("li");
            if (place) {
                const index = /^(definitions|objects)\[(\d+)\]/.exec(frame.path ?? "");
                const exact = this.model?.records().find(record => record.path === frame.file && index &&
                    record.section === index[1] && record.index === Number(index[2]));
                const fallback = this.model?.records().find(record => record.path === frame.file && record.editable);
                const record = exact ?? fallback;
                entry.append(record ? button(place, () => this.openDiagnostic(record, frame), {class: "problem-link"}) :
                    element("span", {class: "muted"}, place), " — ");
            }
            entry.append(frame.message);
            return entry;
        });
        problems.replaceChildren(header, frames.length ? element("ul", {}, ...frames) : "");
    },

    openDiagnostic(record, frame) {
        if (record.type === "building_pattern") {
            this.tab = "layout";
            this.layout.id = record.id;
            const floor = /\.layers\[(\d+)\]/.exec(frame.path)?.[1];
            const basement = /\.basements\[(\d+)\]/.exec(frame.path)?.[1];
            const row = /rows\[(\d+)\]/.exec(frame.path)?.[1];
            this.layout.layer = floor !== undefined ? "y" + record.value.layers?.[Number(floor)]?.y :
                basement !== undefined ? "basement:" + record.value.basements?.[Number(basement)]?.id : "surface";
            this.layout.cell = {x: 0, z: Number(row ?? 0)};
            this.layout.glyph = undefined;
            this.layout.brush = undefined;
            this.layout.inspector = row === undefined ? "pattern" : "cell";
        } else if (record.editable) {
            this.tab = "catalog";
            this.catalog.type = record.type;
            this.catalog.id = record.id;
        }
        this.render();
    },

    dialog(title, fields, submit) {
        const inputs = fields.map(field => {
            const attributes = {name: field.key, required: true, placeholder: field.placeholder};
            const input = field.options ? select(field.value, field.options, () => {}, attributes) :
                element("input", {...attributes, type: field.number ? "number" : "text", value: field.value ?? "",
                    autocomplete: "off"});
            return element("label", {class: "field"}, element("span", {}, field.name), input);
        });
        const feedback = element("p", {class: "dialog-error", role: "alert", hidden: true});
        const actions = element("div", {class: "dialog-actions"}, button("Cancel", () => dialog.close()),
            element("button", {type: "submit", class: "primary"}, "Apply"));
        dialogForm.replaceChildren(element("h2", {}, title), ...inputs, feedback, actions);
        dialogForm.onsubmit = event => {
            event.preventDefault();
            try {
                submit(Object.fromEntries(new FormData(dialogForm)));
                dialog.close();
            } catch (error) {
                feedback.hidden = false;
                feedback.textContent = error.message;
            }
        };
        dialog.showModal();
    },

    library(parent, title, records, active, activate, create, describe, placeholder, state) {
        const heading = element("div", {class: "panel-header"}, element("h2", {}, title), button("+ New", create));
        const list = element("div", {class: "library-list", onscroll: event => { state.listScroll = event.target.scrollTop; }});
        const search = element("input", {type: "search", class: "library-search", value: state.search, placeholder,
            "aria-label": placeholder, oninput: event => {
                state.search = event.target.value;
                state.listScroll = 0;
                redraw();
            }});
        const redraw = () => {
            const query = state.search.toLowerCase().trim();
            const entries = records.filter(record => (record.id + " " + (record.value.name ?? "") + " " +
                (record.value.site ?? "")).toLowerCase().includes(query));
            list.replaceChildren(...entries.map(record => {
                const [name, detail] = describe(record);
                const node = button("", () => activate(record), {class: "library-entry" + (record.id === active ? " active" : ""),
                    "aria-pressed": String(record.id === active)});
                node.append(element("strong", {}, name), detail ? element("small", {}, detail) : "");
                return node;
            }), entries.length ? "" : element("p", {class: "sidebar-note muted"}, "No matches."));
            list.scrollTop = state.listScroll;
        };
        parent.append(heading, search, list);
        redraw();
        requestAnimationFrame(() => { if (list.isConnected) list.scrollTop = state.listScroll; });
    },

    imageUrl(path) { return "/image/" + path.split("/").map(encodeURIComponent).join("/"); },
};

async function request(operation, fields) {
    if (!token) throw new Error("Open the editor using the URL printed by the launcher.");
    const response = await fetch("/api", {
        method: "POST", headers: {"Content-Type": "application/json", "X-Editor-Token": token},
        body: JSON.stringify({operation, ...fields}),
    });
    return response.json();
}

async function openPack(pack) {
    app.busy = true;
    app.status = "Opening content…";
    app.showProblems(null);
    app.controls();
    try {
        const result = await request("open", {pack});
        if (!result.ok) { app.showProblems(result); return; }
        app.model = new Workspace(pack, result);
        const options = app.model.manifests.map(path => element("option", {value: path}, packs[path] ?? path));
        packPicker.replaceChildren(...options);
        packPicker.value = pack;
        Object.assign(app.layout, {id: null, glyph: undefined, brush: undefined, layer: "surface",
            cell: {x: 0, z: 0}, search: "", listScroll: 0, scroll: {left: 0, top: 0}});
        Object.assign(app.catalog, {id: null, search: "", listScroll: 0});
        Object.assign(app.region, {result: null, outdated: true, selected: null, zoom: 1, scroll: {left: 0, top: 0}});
        app.opened = true;
    } catch (error) {
        app.message(error.message);
    } finally {
        if (app.model) packPicker.value = app.model.pack;
        app.busy = false;
        app.status = null;
        app.render();
    }
}

async function save() {
    if (document.activeElement.matches("input, textarea, select")) document.activeElement.blur();
    if (app.busy || !app.model?.dirty) return;
    app.busy = true;
    app.status = "Saving…";
    app.controls();
    try {
        const edits = app.model.edits();
        const result = await request("save", {pack: app.model.pack, edits});
        if (result.ok) {
            app.model.saved(edits);
            app.showProblems(null);
        } else {
            app.showProblems(result);
        }
    } catch (error) {
        app.message(error.message);
    } finally {
        app.busy = false;
        app.status = null;
        app.controls();
    }
}

async function generate() {
    if (app.busy) return;
    const {seed, regionX, regionZ, y} = app.region;
    app.busy = true;
    app.status = "Generating…";
    app.controls();
    try {
        const result = await request("preview", {pack: app.model.pack, seed, regionX, regionZ, y, edits: app.model.edits()});
        if (result.ok) {
            if (app.region.result?.bounds.some((value, index) => value !== result.bounds[index])) {
                app.region.scroll = {left: 0, top: 0};
            }
            Object.assign(app.region, {result, outdated: false, selected: null, shownY: y});
            app.showProblems(null);
        } else {
            app.showProblems(result);
        }
    } catch (error) {
        app.message(error.message);
    } finally {
        app.busy = false;
        app.status = null;
        app.render();
    }
}

function details(values) {
    return element("dl", {class: "details"}, ...values.flatMap(([key, value]) =>
        [element("dt", {}, key), element("dd", {class: "code"}, String(value))]));
}

function regionInspector(app, panel) {
    const {result, selected, shownY} = app.region;
    if (!result || !selected) {
        panel.replaceChildren(element("h2", {}, "Location"), element("p", {class: "sidebar-note muted"},
            result ? "Select a tile to inspect its terrain and generated population." : "Generate a region to inspect a location."));
        return;
    }
    const {x, z} = selected;
    const [minX, minZ, maxX] = result.bounds;
    const index = (z - minZ) * (maxX - minX) + x - minX;
    const tile = result.terrain[result.tiles[index]];
    const building = result.buildings.find(entry => inside(entry.bounds, x, z));
    const contents = result.placements.filter(entry => entry.x === x && entry.z === z);
    panel.replaceChildren(element("h2", {}, "Location"),
        details([["X / Z / Y", x + " / " + z + " / " + shownY], ["Terrain", tile.id],
            ["Biome", result.biomes[index] ? "Woodland" : "Meadow"], ["Road", result.roads[index] ? "Yes" : "No"]]),
        ...(building ? [element("h2", {}, "Building"), details([["Pattern", building.id], ["Rotation", building.rotation + "°"],
            ["Entrance", building.entranceX + " / " + building.entranceZ], ["Basement", building.basement ?? "None"],
            ["Abandoned", building.abandoned ? "Yes" : "No"]]),
        button("Open pattern", () => {
            app.tab = "layout";
            Object.assign(app.layout, {id: building.id, layer: "surface", brush: undefined, glyph: undefined, cell: {x: 0, z: 0}});
            app.render();
        })] : []),
        ...(contents.length ? [element("h2", {class: "sidebar-note"}, "Generated placements"),
            ...contents.map(entry => element("p", {class: "sidebar-note code"}, entry.kind + " · " + entry.id +
                (entry.destination ? " · " + entry.destination : "")))] : []));
}

function renderPreview(app) {
    const state = app.region;
    const sidebar = element("aside", {class: "side pane-pad"});
    let stale;
    const field = (key, title, limits) => {
        const input = element("input", {value: state[key], type: limits ? "number" : "text", inputmode: "numeric",
            min: limits?.[0], max: limits?.[1], required: true, oninput: event => {
                state[key] = limits ? (event.target.value === "" ? null : Number(event.target.value)) : event.target.value.trim();
                state.outdated = true;
                stale.textContent = "Generate to update the preview.";
            }});
        return element("label", {class: "field"}, element("span", {}, title), input);
    };
    const form = element("form", {onsubmit: event => { event.preventDefault(); generate(); }},
        element("h2", {class: "sidebar-note"}, "Region settings"),
        field("seed", "Seed"), field("regionX", "Region X", [-3907, 3906]), field("regionZ", "Region Z", [-3907, 3906]),
        field("y", "Elevation Y", [-10, 10]),
        element("button", {type: "submit", class: "primary"}, "Generate"));
    sidebar.append(form);
    const panel = element("aside", {class: "inspector pane-pad"});
    const stage = element("section", {class: "stage"});
    const header = element("div", {class: "stage-header"}, element("h1", {}, "Region preview"));
    const summary = state.result ? profiles[state.result.profile] + " · Generator v" + state.result.version + " · Y " + state.shownY : "";
    stale = element("small", {}, state.outdated ? "Generate to update the preview." : summary);
    header.append(stale);
    const canvas = element("canvas", {class: "region-canvas", tabindex: 0, "aria-label": "Generated map"});
    const layerControl = select(state.layer, regionLayers, layer => {
        state.layer = layer;
        if (state.result) renderRegion(canvas, state.result, state.layer, state.selected);
    }, {"aria-label": "Preview overlay"});
    const map = element("div", {class: "workbench", onscroll: event => {
        state.scroll = {left: event.target.scrollLeft, top: event.target.scrollTop};
    }}, state.result ? canvas : element("div", {class: "empty"},
        element("h2", {}, "Generate a region"), element("p", {}, "Preview the active content using the game’s world generator.")));
    stage.append(header, element("div", {class: "toolbar"}, layerControl), map);
    if (state.result) {
        const [minX, minZ, maxX, maxZ] = state.result.bounds;
        canvas.style.setProperty("--preview-columns", maxX - minX);
        canvas.style.setProperty("--preview-rows", maxZ - minZ);
        canvas.style.setProperty("--preview-zoom", state.zoom);
        mapZoom(map, canvas, regionZoomLevels, state.zoom, zoom => {
            state.zoom = zoom;
            canvas.style.setProperty("--preview-zoom", zoom);
        });
        const scrolled = state.scroll;
        requestAnimationFrame(() => {
            if (!canvas.isConnected) return;
            map.scrollLeft = scrolled.left;
            map.scrollTop = scrolled.top;
        });
        const redraw = () => {
            renderRegion(canvas, state.result, state.layer, state.selected);
            regionInspector(app, panel);
        };
        redraw();
        canvas.addEventListener("click", event => {
            const position = regionPosition(canvas, event, state.result.bounds);
            if (inside(state.result.bounds, position.x, position.z)) {
                state.selected = position;
                redraw();
            }
        });
        canvas.addEventListener("keydown", event => {
            const directions = {ArrowLeft: [-1, 0], ArrowRight: [1, 0], ArrowUp: [0, -1], ArrowDown: [0, 1]};
            if (!directions[event.key]) return;
            event.preventDefault();
            const {x, z} = state.selected ?? {x: (minX + maxX) >> 1, z: (minZ + maxZ) >> 1};
            const [dx, dz] = directions[event.key];
            state.selected = {x: Math.min(maxX - 1, Math.max(minX, x + dx)), z: Math.min(maxZ - 1, Math.max(minZ, z + dz))};
            redraw();
        });
    } else {
        regionInspector(app, panel);
    }
    return [sidebar, stage, panel];
}

function history(backward) {
    if (app.busy || !app.model?.step(backward)) return;
    app.changed(true);
}

packPicker.addEventListener("change", () => {
    if (!app.model?.dirty || window.confirm("Discard unsaved changes and switch packs?")) openPack(packPicker.value);
    else packPicker.value = app.model.pack;
});
saveButton.addEventListener("click", save);
undoButton.addEventListener("click", () => history(true));
redoButton.addEventListener("click", () => history(false));
for (const tab of tabs) {
    tab.addEventListener("click", () => { app.tab = tab.dataset.tab; app.render(); });
}
window.addEventListener("beforeunload", event => {
    if (app.model?.dirty) {
        event.preventDefault();
        event.returnValue = true;
    }
});
document.addEventListener("keydown", event => {
    if (!(event.ctrlKey || event.metaKey)) return;
    const key = event.key.toLowerCase();
    if (key === "s") {
        event.preventDefault();
        save();
    } else if (!event.target.matches("input, textarea, select") && (key === "z" || key === "y")) {
        event.preventDefault();
        history(key === "z" && !event.shiftKey);
    }
});

openPack("data/content.json5");
