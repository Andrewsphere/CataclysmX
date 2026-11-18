let nextList = 0;

export function element(tag, attributes = {}, ...children) {
    const node = document.createElement(tag);
    for (const [key, value] of Object.entries(attributes)) {
        if (value === undefined || value === null || value === false) continue;
        if (key === "class") node.className = value;
        else if (key.startsWith("on")) node.addEventListener(key.slice(2), value);
        else if (key === "value") node.value = value;
        else node.setAttribute(key, value === true ? "" : value);
    }
    node.append(...children.filter(child => child !== undefined && child !== null));
    return node;
}

export function button(label, action, attributes = {}) {
    return element("button", {type: "button", onclick: action, ...attributes}, label);
}

export function select(value, choices, onChange, attributes = {}) {
    const node = element("select", {...attributes, onchange: event => onChange(event.target.value)},
        ...choices.map(choice => Array.isArray(choice) ?
            element("option", {value: choice[0]}, choice[1]) : element("option", {value: choice}, choice)));
    node.value = value ?? "";
    return node;
}

export const text = (key, title, options = {}) => ({key, title, kind: "text", ...options});
export const integer = (key, title, options = {}) => ({key, title, kind: "integer", ...options});
export const toggle = (key, title) => ({key, title, kind: "boolean"});
export const choice = (key, title, values) => ({key, title, kind: "choices", values});
export const ref = (key, title, type, options = {}) => ({key, title, kind: "reference", type, ...options});
export const group = (key, title, fields, initial = {}) => ({key, title, kind: "group", fields, initial});
export const list = (key, title, fields, initial) => ({key, title, kind: "list", fields, initial});
const longText = (key, title) => ({key, title, kind: "textarea"});
const art = {key: "art", title: "Text art", kind: "art"};

export const facing = ["north", "east", "south", "west"];
export const site = ["building", "landmark", "park", "square"];
export const use = ["mixed", "residential", "commercial", "industrial"];

const damage = ["blunt", "slashing", "piercing", "ballistic", "heat", "chemical", "choking"];
const regions = ["head", "eyes", "mouth", "ears", "torso", "left_arm", "right_arm", "left_hand", "right_hand",
    "left_leg", "right_leg", "left_foot", "right_foot", "attack_apparatus", "locomotion", "core"];

const light = group("light", "Light", [integer("intensity", "Intensity"), integer("range", "Range")], {intensity: 0, range: 0});
const location = [integer("x", "X"), integer("z", "Z"), integer("y", "Y")];
const furniture = [ref("object", "Object", "object"), choice("facing", "Facing", facing)];
const loot = [ref("pool", "Loot pool", "loot_pool"), integer("chance", "Chance / 10,000", {placeholder: "10,000"}),
    integer("rolls", "Rolls", {placeholder: "1"}), choice("destination", "Destination", ["ground", "furniture"])];
const monsters = [ref("group", "Monster group", "monster_group"),
    integer("chance", "Chance / 10,000", {placeholder: "10,000"})];

export const recipeFields = [
    ref("terrain", "Terrain", "terrain"), ...furniture,
    list("loot", "Loot", loot, {pool: ""}), list("monsters", "Monster spawn", monsters, {group: ""}),
];

export const placementFields = [
    list("furniture", "Coordinate furniture", [...location, ...furniture], {x: 0, z: 0, y: 0, object: ""}),
    list("loot", "Coordinate loot", [...location, ...loot], {x: 0, z: 0, y: 0, pool: ""}),
    list("monsters", "Coordinate monsters", [...location, ...monsters], {x: 0, z: 0, y: 0, group: ""}),
];

export const patternFields = [
    choice("site", "Site", site), choice("use", "Use", use), text("family", "Family"),
    integer("family_weight", "Family weight", {placeholder: "1"}), integer("weight", "Pattern weight", {placeholder: "1"}),
    group("entrance", "Entrance", [integer("x", "X"), integer("z", "Z")], {x: 0, z: 0}),
];

const itemCapabilities = [
    text("id", "Local ID"), choice("type", "Kind", ["container", "energy_store", "charger", "door_controller"]),
    integer("capacity_volume_ml", "Capacity (mL)", {when: "container"}),
    integer("capacity_energy", "Energy capacity", {when: "energy_store"}),
    integer("initial_energy", "Initial energy", {when: "energy_store"}),
    {...toggle("rechargeable", "Rechargeable"), when: "energy_store"},
    text("battery_class", "Battery class", {when: ["energy_store", "charger"]}),
    text("power_source", "Local energy store", {when: "charger"}),
    integer("energy_per_turn", "Energy per turn", {when: "charger"}),
    integer("delay_ticks", "Delay (ticks)", {when: "door_controller"}),
    integer("action_cost", "Action cost", {when: ["charger", "door_controller"]}),
];

const grip = [
    choice("kind", "Kind", ["one_handed", "two_handed"]), integer("damage_ratio", "Damage ratio"),
    integer("cost_ratio", "Cost ratio"), integer("stamina_ratio", "Stamina ratio"),
    integer("block_ratio", "Block ratio"), integer("rating_bonus100", "Rating bonus × 100"),
];
const reach = group("reach", "Reach", [integer("distance", "Distance"),
    choice("height", "Height", ["any", "lower", "upper"]),
    list("regions", "Contact regions", choice("", "", regions), "torso")], {distance: 1});

export const fieldsFor = {
    terrain: [
        text("name", "Name"), group("movement", "Movement", [toggle("passable", "Passable")], {passable: true}),
        toggle("blocks_sight", "Blocks sight"), toggle("open_space", "Open space"), toggle("stairs", "Stairs"),
        ref("looks_like", "Inherit appearance from", "terrain"),
        group("appearance", "Glyph appearance", [text("symbol", "Symbol"), text("foreground", "Foreground"),
            text("background", "Background")]),
        ...["opens_to", "closes_to", "locks_to", "unlocks_to"].map(key => ref(key, key.replace("_", " "), "terrain")), light,
    ],
    object: [
        text("name", "Name"), choice("behavior", "Behavior", ["none", "crafting_machine"]),
        choice("kind", "Kind", ["generic", "chair", "table", "lamp"]), toggle("movable", "Movable"),
        toggle("blocks_movement", "Blocks movement"), toggle("blocks_sight", "Blocks sight"),
        integer("capacity_volume_ml", "Storage (mL)"), integer("move_cost", "Move cost (ticks)"), light,
        group("machine", "Machine settings", [
            ref("input", "Input", "item"), integer("input_count", "Input count"),
            ref("output", "Output", "item"), integer("output_count", "Output count"),
            integer("duration_ticks", "Duration (ticks)"), integer("energy_capacity", "Energy capacity"),
            integer("energy_per_tick", "Energy per tick"),
        ]),
    ],
    loot_pool: [
        list("entries", "Weighted entries", [
            ref("item", "Item", "item", {exclusive: "pool"}), ref("pool", "Nested pool", "loot_pool", {exclusive: "item"}),
            integer("weight", "Weight", {placeholder: "1"}),
            group("count", "Count range", [integer("min", "Minimum"), integer("max", "Maximum")], {min: 1, max: 1}),
        ], {item: "", weight: 1}),
    ],
    monster_group: [
        list("entries", "Weighted entries", [ref("monster", "Monster", "monster"), integer("weight", "Weight", {placeholder: "1"})],
            {monster: "", weight: 1}),
    ],
    item: [
        text("name", "Name"), longText("description", "Description"), integer("mass_g", "Mass (g)"),
        integer("volume_ml", "External volume (mL)"), art,
        list("flags", "Flags", ref("", "", "flag"), ""),
        list("capabilities", "Capabilities", itemCapabilities, {type: "container", id: "", capacity_volume_ml: 1}),
        group("melee", "Melee", [
            list("damage", "Damage packets", [choice("type", "Type", damage), integer("amount", "Hundredths of HP"),
                integer("penetration", "Penetration")], {type: "blunt", amount: 400}),
            integer("attack_cost", "Attack cost"), ref("handling_skill", "Handling skill", "skill"),
            integer("hands", "Legacy hand count"), list("grips", "Grips", grip, {kind: "one_handed"}),
            integer("block_capacity", "Block capacity"),
            {...list("allowed_responses", "Allowed responses", choice("", "", ["dodge", "parry", "block"]), "parry"),
                keepEmpty: true},
        ], {damage: [{type: "blunt", amount: 400}]}),
        group("armor", "Armor", [
            choice("layer", "Layer", ["under", "normal", "outer", "strapped"]), integer("wear_cost", "Wear cost"),
            integer("remove_cost", "Remove cost"), list("regions", "Covered regions", [
                choice("region", "Region", regions), integer("coverage", "Coverage / 10,000"),
                integer("encumbrance", "Encumbrance"), toggle("overlap_exempt", "Overlap exempt"),
            ], {region: "torso"}),
            group("protection", "Protection", damage.map(kind => integer(kind, kind)), {}),
            list("rigid_regions", "Rigid regions", choice("", "", regions), "torso"),
        ], {layer: "normal", regions: [{region: "torso"}], protection: {}}),
        list("tool_traits", "Tool traits", [ref("id", "Trait", "tool_trait"), integer("tier", "Tier")], {id: "", tier: 1}),
        toggle("recovery_allowed", "Recovery allowed"),
        group("crafting_power", "Crafting power", [text("store", "Local energy store"),
            integer("energy_per_work", "Energy per work / 10,000")], {store: "", energy_per_work: 10000}),
        list("books", "Books", ref("", "", "book"), ""), text("reading_power", "Reading power store"),
    ],
    monster: [
        text("name", "Name"), ref("ai_profile", "AI profile", "ai_profile"), choice("body", "Body", ["simple", "detailed"]),
        choice("faction", "Faction", ["neutral", "survivors", "undead"]), integer("speed", "Speed"),
        integer("attack_damage", "Attack damage (HP)"), integer("accuracy", "Accuracy / 10,000"),
        ref("combat_style", "Combat style", "combat_style"), toggle("authored_training_ranks", "Use authored training ranks"),
        integer("offensive_training_rank", "Offensive training rank"), integer("defensive_training_rank", "Defensive training rank"),
        integer("attack_rating100", "Attack rating × 100"), integer("defense_rating100", "Defense rating × 100"),
        toggle("inexhaustible", "Inexhaustible"), reach,
    ],
    variants: [
        text("description", "Description"),
        list("alternatives", "Weighted visuals", [
            ref("visual", "Sprite or animation", "variant_source"), integer("weight", "Weight", {placeholder: "1"}),
            choice("rotation", "Rotation", [0, 90, 180, 270]), toggle("flip_x", "Flip horizontally"), toggle("flip_y", "Flip vertically"),
        ], {visual: "", weight: 1}),
    ],
};

function numberValue(value) {
    if (!/^[+-]?\d+$/.test(value)) throw new Error("Enter a whole number.");
    const parsed = BigInt(value);
    return Number.isSafeInteger(Number(parsed)) ? Number(parsed) : parsed;
}

function inputFor(field, value, refs, update) {
    const {kind, title, values, type, placeholder} = field;
    const applied = event => {
        const node = event.target;
        node.setCustomValidity("");
        if (kind !== "integer" || !node.value.trim()) {
            update(node.value === "" ? undefined : node.value);
            return;
        }
        try {
            update(numberValue(node.value.trim()));
        } catch (error) {
            node.setCustomValidity(error.message);
            node.reportValidity();
            update(Number.NaN);
        }
    };
    if (kind === "boolean") {
        return select(value === undefined ? "" : String(value), [["", "Default"], ["true", "Yes"], ["false", "No"]],
            selected => update(selected ? selected === "true" : undefined), {"aria-label": title});
    }
    if (kind === "choices") {
        return select(value, [["", "Default"], ...values],
            selected => update(selected === "" ? undefined : values.find(entry => String(entry) === selected)),
            {"aria-label": title});
    }
    if (kind === "textarea") {
        return element("textarea", {rows: 3, value: value ?? "", onchange: applied, "aria-label": title});
    }
    if (kind === "art" && Array.isArray(value)) {
        return element("textarea", {rows: 4, value: value.join("\n"), "aria-label": title,
            onchange: event => update(event.target.value ? event.target.value.split("\n") : undefined)});
    }
    const input = element("input", {value: value ?? "", inputmode: kind === "integer" ? "numeric" : undefined,
        placeholder: placeholder ?? (kind === "art" ? "/art/items/…/art.txt" : undefined),
        onchange: applied, "aria-label": title, autocomplete: "off"});
    if (kind !== "reference") return input;
    const id = "reference-" + ++nextList;
    input.setAttribute("list", id);
    const options = refs(type).map(option => Array.isArray(option) ?
        element("option", {value: option[0], label: option[1]}) : element("option", {value: option}));
    return element("div", {class: "reference"}, input, element("datalist", {id}, ...options));
}

function displayField(field, value) {
    return !field.when || [field.when].flat().includes(value.type) || Object.hasOwn(value, field.key);
}

export function mountFields(target, initial, fields, refs, onChange) {
    const model = structuredClone(initial);
    function update(rebuild) {
        onChange(structuredClone(model));
        if (rebuild && target.isConnected) render();
    }

    function fieldNodes(value, descriptors) {
        const nodes = [];
        for (const field of descriptors) {
            if (!displayField(field, value)) continue;
            const {key, title, kind} = field;
            if (kind === "group") {
                const header = element("div", {class: "field-heading"}, element("strong", {}, title));
                if (value[key] === undefined) {
                    header.append(button("Add", () => { value[key] = structuredClone(field.initial); update(true); }));
                    nodes.push(header);
                } else {
                    header.append(button("Remove", () => { delete value[key]; update(true); }));
                    nodes.push(element("details", {class: "form-group", open: true},
                        element("summary", {}, title), header, ...fieldNodes(value[key], field.fields)));
                }
            } else if (kind === "list") {
                const header = element("div", {class: "field-heading"}, element("strong", {}, title),
                    button("Add", () => {
                        if (!value[key]) value[key] = [];
                        value[key].push(structuredClone(field.initial));
                        update(true);
                    }));
                if (field.keepEmpty && !value[key]?.length) {
                    const explicit = Object.hasOwn(value, key);
                    header.insertBefore(button(explicit ? "Use default" : "Set none", () => {
                        if (explicit) delete value[key];
                        else value[key] = [];
                        update(true);
                    }), header.lastChild);
                }
                const entries = (value[key] ?? []).map((entry, index) => {
                    const remove = button("Remove", () => {
                        value[key].splice(index, 1);
                        if (value[key].length === 0 && !field.keepEmpty) delete value[key];
                        update(true);
                    });
                    if (!Array.isArray(field.fields)) {
                        const input = inputFor({...field.fields, title}, entry, refs, replacement => {
                            value[key][index] = replacement ?? "";
                            update(false);
                        });
                        return element("div", {class: "list-row"}, input, remove);
                    }
                    return element("div", {class: "list-card"}, element("div", {class: "field-heading"},
                        element("span", {}, entry.item || entry.pool || entry.monster || entry.id || entry.visual || title), remove),
                    ...fieldNodes(entry, field.fields));
                });
                nodes.push(element("div", {class: "form-group"}, header, ...entries));
            } else {
                const input = inputFor(field, value[key], refs, replacement => {
                    if (replacement === undefined) delete value[key];
                    else {
                        value[key] = replacement;
                        if (field.exclusive) delete value[field.exclusive];
                    }
                    update(Boolean(field.exclusive || key === "type"));
                });
                nodes.push(element("label", {class: "field"}, element("span", {}, title), input));
            }
        }
        return nodes;
    }

    function render() { target.replaceChildren(...fieldNodes(model, fields)); }
    render();
}
