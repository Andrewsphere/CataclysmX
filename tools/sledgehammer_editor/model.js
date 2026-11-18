import {SourceFile, equal} from "./source.js";

export const kindLabels = {
    building_pattern: "Pattern", terrain: "Terrain", object: "Object", loot_pool: "Loot pool",
    monster_group: "Monster group", item: "Item", monster: "Monster", variants: "Visual variants",
};

const folders = {
    building_pattern: "worldgen/buildings", loot_pool: "worldgen/loot", terrain: "terrain",
    object: "objects", item: "items", monster: "monsters", monster_group: "monsters",
};

const sectionFor = {objects: "object", ai_profiles: "ai_profile"};
const sections = ["definitions", "objects", "ai_profiles"];
const mappingSections = {terrain: "terrain", object: "objects", item: "items", monster: "monsters"};

export function manifestFile(path) {
    return "data/" + path.replace(/\\/g, "/").split("/").filter(part => part && part !== ".").join("/");
}

export function idIsValid(id) {
    return /^[a-z][a-z0-9_]*$/.test(id);
}

export class Workspace {
    constructor(pack, opened) {
        this.pack = pack;
        this.tileset = opened.tileset;
        this.manifests = opened.manifests.map(file => file.path);
        this.images = new Map(opened.images.map(({id, path}) => [id, path]));
        this.files = new Map();
        for (const {path, text, editable} of [...opened.manifests, ...opened.sources, ...opened.metadata]) {
            try {
                this.files.set(path, new SourceFile(path, text, editable));
            } catch (error) {
                throw new Error(path + " · " + error.message);
            }
        }
        this.undo = [];
        this.redo = [];
    }

    records() {
        const records = [];
        const sourcePaths = this.files.get(this.pack).value.files.map(manifestFile);
        for (const path of sourcePaths) {
            const file = this.files.get(path);
            if (!file?.value) continue;
            for (const section of sections) {
                (file.value[section] ?? []).forEach((value, index) => {
                    if (value && typeof value === "object") {
                        records.push({path, section, index, value, id: value.id, type: sectionFor[section] ?? value.type,
                            editable: file.editable});
                    }
                });
            }
        }
        const base = this.tileset.slice(0, -"tileset.json5".length);
        for (const [path, file] of this.files) {
            if (path.startsWith(base) && file.value?.kind) {
                records.push({path, value: file.value, id: path.slice(base.length, -5).replaceAll("/", "."),
                    type: file.value.kind, editable: file.value.kind === "variants"});
            }
        }
        return records;
    }

    get(type, id) { return this.records().find(record => record.type === type && record.id === id); }

    edits() {
        return [...this.files.values()].map(file => file.edit()).filter(Boolean);
    }

    get dirty() {
        return [...this.files.values()].some(file => !equal(file.original?.value, file.value));
    }

    addFile(path) {
        if (!this.files.has(path)) this.files.set(path, new SourceFile(path, null));
        return this.files.get(path);
    }

    change(values) {
        const changes = [];
        for (const [path, value] of values) {
            const file = this.addFile(path);
            if (!equal(file.value, value)) {
                changes.push({file, before: file.value, after: value});
                file.value = value;
            }
        }
        if (!changes.length) return false;
        this.undo.push(changes);
        if (this.undo.length > 100) this.undo.shift();
        this.redo = [];
        return true;
    }

    step(backward) {
        const from = backward ? this.undo : this.redo;
        const to = backward ? this.redo : this.undo;
        const changes = from.pop();
        if (!changes) return false;
        for (const change of changes) change.file.value = backward ? change.before : change.after;
        to.push(changes);
        return true;
    }

    saved(edits) {
        for (const {path, text} of edits) {
            this.files.set(path, new SourceFile(path, text, this.files.get(path).editable));
        }
        this.undo = [];
        this.redo = [];
    }

    update(record, next) {
        if (!record?.editable) return false;
        if (!record.section) return this.change([[record.path, next]]);
        const file = this.files.get(record.path);
        if (record.type === "building_pattern") {
            const original = file.original?.value.definitions.find(entry => entry.id === record.id);
            const used = new Set([next, ...(next.layers ?? []), ...(next.basements ?? [])]
                .flatMap(layer => layer.rows.flatMap(row => [...row])));
            const palette = Object.fromEntries(Object.entries(next.palette).filter(([glyph]) =>
                used.has(glyph) || Object.hasOwn(original?.palette ?? {}, glyph)));
            next = {...next, palette};
        }
        const content = {...file.value, [record.section]: [...file.value[record.section]]};
        content[record.section][record.index] = next;
        return this.change([[record.path, content]]);
    }

    include(changes, manifest, path) {
        const current = changes.get(manifest) ?? this.files.get(manifest).value;
        if (!current.files.some(entry => manifestFile(entry) === path)) {
            changes.set(manifest, {...current, files: [...current.files, path.slice("data/".length)]});
        }
    }

    create(type, record) {
        if (type === "variants") {
            const id = record.id;
            const roots = this.files.get(this.tileset).value.source_directories ?? ["tile", "sprite"];
            const relative = id.replaceAll(".", "/");
            if (!/^[a-z0-9_-]+(?:\.[a-z0-9_-]+)+$/.test(id) || !roots.some(root => relative.startsWith(root + "/"))) {
                throw new Error("Use a graphics ID under " + roots.join(" or ") + ", for example tile.world.my_variants.");
            }
            const path = this.tileset.replace(/tileset\.json5$/, relative + ".meta");
            if (this.files.get(path)?.value || this.images.has(id)) throw new Error("That graphics ID is already in use.");
            const {id: _, ...definition} = record;
            this.change([[path, definition]]);
            return;
        }
        if (!idIsValid(record.id)) throw new Error("Use a lowercase ID with letters, numbers, and underscores.");
        if (this.records().some(entry => entry.id === record.id && !["flag", "variants", "animation", "connections"].includes(entry.type))) {
            throw new Error("That definition ID is already in use.");
        }
        let path = "data/" + folders[type] + "/" + record.id + ".json5";
        const section = type === "object" ? "objects" : "definitions";
        const existing = this.files.get(path)?.value;
        if (existing && !Array.isArray(existing[section])) path = path.replace(/\.json5$/, "_editor.json5");
        const current = this.files.get(path)?.value ?? {format_version: 1, [section]: []};
        const definition = type === "object" ? record : {type, ...record};
        const changes = new Map([[path, {...current, [section]: [...current[section], definition]}]]);
        this.include(changes, this.pack, path);
        this.change(changes);
    }

    references(type) {
        const records = this.records();
        const images = [...this.images.keys()].map(id => [id, id]);
        if (type === "visual" || type === "object_visual" || type === "variant_source") {
            const kinds = type === "visual" ? ["variants", "animation", "connections"] : type === "object_visual" ? ["variants", "animation"] : ["animation"];
            return [...images, ...records.filter(entry => kinds.includes(entry.type)).map(({id}) => [id, id])]
                .sort(([a], [b]) => a.localeCompare(b));
        }
        return records.filter(entry => entry.type === type && (type !== "flag" || entry.value.domains?.includes("item")))
            .map(({id, value}) => [id, value.name ?? id]).sort(([a], [b]) => a.localeCompare(b));
    }

    mapping(record) {
        const section = mappingSections[record.type];
        return section ? this.files.get(this.tileset).value[section]?.[record.id] : undefined;
    }

    setMapping(record, visual) {
        const section = mappingSections[record.type];
        const current = this.files.get(this.tileset).value;
        const mappings = {...current[section]};
        if (visual) {
            const initial = record.type === "terrain" ? {} : {optional: true};
            mappings[record.id] = {...(mappings[record.id] ?? initial), visual};
        } else {
            delete mappings[record.id];
        }
        const changes = new Map([[this.tileset, {...current, [section]: mappings}]]);
        if (visual && record.type === "terrain" && this.tileset === "assets/tiles/gen1_32x32/tileset.json5") {
            for (const manifest of this.manifests) this.include(changes, manifest, record.path);
        }
        this.change(changes);
    }

    terrainLooks() {
        const records = new Map(this.records().filter(record => record.type === "terrain").map(({id, value}) => [id, value]));
        return id => {
            const looks = {};
            const seen = new Set();
            while (id && !seen.has(id)) {
                seen.add(id);
                const value = records.get(id);
                if (!value) break;
                for (const [key, property] of Object.entries(value.appearance ?? {})) {
                    if (!Object.hasOwn(looks, key)) looks[key] = property;
                }
                id = value.looks_like;
            }
            return looks;
        };
    }

    visuals() {
        const groups = new Map(this.records().filter(record => ["variants", "animation", "connections"].includes(record.type))
            .map(({id, value}) => [id, value]));
        const entries = this.files.get(this.tileset).value;
        const getVisual = (id, depth = 0) => {
            if (depth > 4) return null;
            if (this.images.has(id)) return {path: this.images.get(id)};
            const group = groups.get(id);
            if (!group) return null;
            if (group.kind === "animation") return getVisual(group.frames?.[0]?.sprite, depth + 1);
            const entry = group.kind === "variants" ? group.alternatives?.[0] : group.shapes?.isolated;
            const visual = getVisual(entry?.visual, depth + 1);
            return visual && {...visual, rotation: entry.rotation, flipX: entry.flip_x, flipY: entry.flip_y};
        };
        return (section, id) => getVisual(entries[section]?.[id]?.visual);
    }
}
