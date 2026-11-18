import {button, element, fieldsFor, mountFields, ref, select} from "./forms.js";
import {kindLabels} from "./model.js";

const kinds = ["terrain", "object", "loot_pool", "item", "monster_group", "monster", "variants"];
const graphicsSection = {terrain: "terrain", object: "objects", item: "items", monster: "monsters"};

function initial(app, type, id, name) {
    const first = (kind, fallback = "") => app.model.references(kind)[0]?.[0] ?? fallback;
    if (type === "terrain") {
        return {id, name, movement: {passable: true}, blocks_sight: false, looks_like: "floor"};
    }
    if (type === "object") return {id, name, behavior: "none", kind: "generic"};
    if (type === "loot_pool") return {id, entries: [{item: first("item"), weight: 1}]};
    if (type === "monster_group") return {id, entries: [{monster: first("monster"), weight: 1}]};
    if (type === "item") return {id, name, mass_g: 100, volume_ml: 100};
    if (type === "monster") return {id, name, ai_profile: first("ai_profile"), body: "simple", faction: "undead"};
    return {id, kind: "variants", alternatives: [{visual: first("variant_source"), weight: 1}]};
}

function newDefinition(app, type, original) {
    const named = ["terrain", "object", "item", "monster"].includes(type);
    app.dialog(original ? "Duplicate " + kindLabels[type].toLowerCase() : "New " + kindLabels[type].toLowerCase(), [
        {key: "id", name: type === "variants" ? "Graphics ID" : "Stable ID", value: original ? original.id + "_copy" : "",
            placeholder: type === "variants" ? "tile.world.my_variants" : "stable_content_id"},
        ...(named ? [{key: "name", name: "Display name", value: original?.value.name ?? ""}] : []),
    ], values => {
        const definition = original ? {...structuredClone(original.value), id: values.id} : initial(app, type, values.id, values.name);
        if (named) {
            if (!values.name.trim()) throw new Error("Enter a display name.");
            definition.name = values.name;
        }
        app.model.create(type, definition);
        app.catalog.id = values.id;
        app.catalog.search = "";
        app.catalog.listScroll = 0;
        app.changed(true);
    });
}

function artwork(app, record, panel) {
    const section = graphicsSection[record.type];
    if (record.type === "variants") {
        panel.append(element("h2", {}, "Alternatives"),
            element("p", {class: "sidebar-note muted"}, "Assign this set as a tileset visual from a terrain, object, item, or monster."));
        const renderAlternatives = () => {
            const latest = app.model.get(record.type, record.id);
            const entries = latest.value.alternatives ?? [];
            previews.replaceChildren(...entries.map(entry => {
                const path = app.model.images.get(entry.visual);
                return element("div", {class: "art-preview"},
                    path ? element("img", {src: app.imageUrl(path), alt: entry.visual}) : null,
                    element("p", {class: "code sidebar-note"}, entry.visual || "Choose a visual"));
            }));
        };
        const previews = element("div", {class: "stack"});
        panel.append(previews);
        renderAlternatives();
        return renderAlternatives;
    }
    if (!section) return () => {};
    panel.append(element("h2", {}, "Tileset artwork"));
    const form = element("div", {class: "sidebar-note"});
    const preview = element("div", {class: "art-preview"});
    panel.append(form, preview);
    const renderPreview = () => {
        const current = app.model.visuals()(section, record.id);
        const mapping = app.model.mapping(record);
        preview.replaceChildren(current ? element("img", {src: app.imageUrl(current.path), alt: mapping.visual}) :
            element("p", {class: "sidebar-note muted"}, "Uses glyph fallback."));
        if (app.model.get("variants", mapping?.visual)) {
            preview.append(element("p", {class: "sidebar-note"}, button("Edit visual set", () => {
                app.catalog.type = "variants";
                app.catalog.id = mapping.visual;
                app.render();
            })));
        }
    };
    if (record.type === "terrain" && app.model.tileset === "assets/tiles/gen1_32x32/tileset.json5") {
        panel.append(element("p", {class: "sidebar-note muted"}, "Assigning Gen1 art makes this terrain available to the bundled packs."));
    }
    const visual = ref("visual", "Visual", record.type === "terrain" ? "visual" : "object_visual");
    mountFields(form, {visual: app.model.mapping(record)?.visual}, [visual], type => app.model.references(type), next => {
        app.model.setMapping(record, next.visual);
        app.changed();
        renderPreview();
    });
    renderPreview();
    return renderPreview;
}

export function renderCatalog(app) {
    const state = app.catalog;
    const sidebar = element("aside", {class: "side pane-pad"});
    const inspector = element("aside", {class: "inspector pane-pad"});
    const records = app.model.records().filter(record => record.type === state.type && record.editable)
        .sort((left, right) => left.id.localeCompare(right.id));
    const record = records.find(record => record.id === state.id) ?? records[0];
    state.id = record?.id;
    const picker = select(state.type, kinds.map(kind => [kind, kindLabels[kind]]), type => {
        state.type = type;
        state.id = null;
        state.search = "";
        state.listScroll = 0;
        app.render();
    }, {"aria-label": "Definition kind"});
    sidebar.append(element("label", {class: "field"}, element("span", {}, "Definition kind"), picker));
    const library = element("div");
    sidebar.append(library);
    app.library(library, "Definitions", records, record?.id, entry => { state.id = entry.id; app.render(); },
        () => newDefinition(app, state.type), entry => [entry.value.name ?? entry.id, entry.value.name ? entry.id : null],
        "Search definitions", state);
    if (!record) {
        return [sidebar, element("section", {class: "stage"}, element("div", {class: "empty"},
            element("h2", {}, "Create a " + kindLabels[state.type].toLowerCase()),
            button("New", () => newDefinition(app, state.type)))), inspector];
    }
    const heading = element("h1", {}, record.value.name ?? record.id);
    const stage = element("section", {class: "stage"},
        element("div", {class: "stage-header"},
            element("div", {class: "stage-heading"}, heading, button("Duplicate", () => newDefinition(app, state.type, record))),
            element("small", {class: "code"}, record.id + " · " + record.path)));
    const form = element("div", {class: "catalog-form"});
    stage.append(form);
    const refreshArt = artwork(app, record, inspector);
    mountFields(form, record.value, fieldsFor[state.type], type => app.model.references(type), value => {
        app.update(record, value);
        heading.textContent = value.name || record.id;
        refreshArt();
    });
    return [sidebar, stage, inspector];
}
