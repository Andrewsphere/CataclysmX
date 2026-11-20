#!/usr/bin/env python3
"""Summarize workshop_probe CSVs without pooling stock across worlds."""

import argparse
import csv
import json
import math
import sys
from collections import Counter, defaultdict
from pathlib import Path


KEY = ("seed", "owner_region_x", "owner_region_z", "settlement", "parcel")
SETTLEMENT = KEY[:-1]
FILES = {
    "sites": (*KEY, "pattern", "scale", "era", "origin_patch", "panel",
              "example", "starting_settlement", "start_distance", "indoor",
              "outdoor_near16", "item_count", "inaccessible_items"),
    "stock": (*KEY, "kind", "id", "count", "reachable", "min_steps"),
    "starts": ("seed", "x", "z", "y", "generation_version", "sampled_settlements"),
    "items": ("id", "mass_g", "volume_ml", "placed_object", "books", "initial_energy", "capacity_energy",
              "crafting_energy_per_work"),
    "objects": ("id", "packing_item", "storage_ml", "ignition_heat", "fuel_item", "heat_per_fuel"),
    "providers": ("kind", "id", "trait", "tier"),
    "recipes": ("kind", "id", "output", "count", "work", "heat_per_work", "energy",
                "setup_work", "setup_work_per_unit"),
    "requirements": ("kind", "recipe", "requirement", "id", "count"),
}
RECIPE_IDS = (
    "assemble_crude_anvil", "assemble_charcoal_forge", "forge_tongs",
    "cut_tool_steel_blanks", "forge_pry_bar", "forge_smithing_hammer",
    "assemble_fabricated_anvil", "forge_wood_chisel", "make_current_arming_sword",
    "assemble_wiring_kit", "assemble_electrical_lead_set", "assemble_soldering_iron",
    "assemble_workshop_battery_pack", "assemble_makeshift_arc_welder",
    "assemble_improvised_welding_goggles", "assemble_welding_mask",
    "weld_workshop_rack", "weld_fabricated_anvil",
    "assemble_solar_generator_kit", "weld_woodgas_processing_assembly",
    "assemble_generator_drive", "assemble_woodgas_generator_kit",
    "print_plastic_cup", "print_small_parts_case", "print_electronics_case",
    "assemble_workshop_battery_pack_in_printed_case",
    "fit_machete_with_printed_grip", "stitch_canvas_duffel_with_printed_buckles",
)
KEY_ITEMS = (
    "flathead_screwdriver", "makeshift_screwdriver", "steel_scrap", "combination_pliers",
    "claw_hammer", "smithing_hammer", "forging_tongs", "hacksaw", "metal_file",
    "hand_drill", "adjustable_wrench", "multimeter", "soldering_iron", "arc_welder",
    "charcoal_forge_kit", "crude_anvil_kit", "fabricated_anvil_kit", "charcoal_charge",
    "spring_steel_bar", "solid_steel_block", "refractory_brick", "tool_steel_blank",
    "home_polymer_printer", "polymer_filament", "recovered_pv_panel",
    "solar_charge_controller", "solar_generator_kit", "recovered_gasoline_engine",
    "generator_alternator", "generator_drive_assembly", "woodgas_processing_assembly",
    "woodgas_generator_kit", "electrical_lead_set", "insulated_power_cable",
    "electrical_connector", "aa_rechargeable_battery", "workshop_primer_print",
    "workshop_ebook_reader", "salvage_notes_print",
    "steel_pipe", "steel_rod", "steel_sheet", "timber_plank", "wire_mesh",
    "solder_wire", "nuts_and_bolts",
)
BOOKS = ("workshop_primer_text", "salvage_notes_text")
# These are finite author-selected executions, not a dependency solver or XP planner.
# Each tuple is (recipe_id, execution count); a machine tuple is one selected batch.
ROUTES = {
    "skilled_found_forging_one_sword": (
        ("cut_timber_scrap", 1),
        ("make_current_arming_sword", 1),
    ),
    "skilled_manufactured_forging_one_sword": (
        ("cut_timber_scrap", 1),
        ("assemble_crude_anvil", 1),
        ("assemble_charcoal_forge", 1),
        ("forge_tongs", 1),
        ("cut_tool_steel_blanks", 2),
        ("forge_smithing_hammer", 1),
        ("assemble_fabricated_anvil", 1),
        ("make_current_arming_sword", 1),
    ),
    "skilled_assembled_woodgas": (
        ("assemble_electrical_lead_set", 1),
        ("assemble_generator_drive", 1),
        ("weld_woodgas_processing_assembly", 1),
        ("assemble_woodgas_generator_kit", 1),
    ),
    "static_printer_c0_to_c2_battery_consumer": (
        ("print_plastic_cup", 10),
        ("print_small_parts_case", 10),
        ("print_electronics_case", 1),
        ("assemble_electrical_lead_set", 1),
        ("assemble_workshop_battery_pack_in_printed_case", 1),
    ),
    # Audit-reference practice schedules: keep every job, including surplus at
    # a teaching ceiling. Timber preparation is extra F0 work, not an XP saving.
    "static_nostudy_manufactured_forging": (
        ("cut_timber_scrap", 4), ("make_wooden_club", 20),
        ("assemble_crude_anvil", 1), ("make_salvaged_machete", 8),
        ("assemble_charcoal_forge", 1), ("forge_tongs", 1),
        ("cut_tool_steel_blanks", 8), ("forge_pry_bar", 6),
        ("forge_smithing_hammer", 1), ("assemble_fabricated_anvil", 1),
        ("forge_wood_chisel", 24), ("make_current_arming_sword", 1),
    ),
    "static_nostudy_found_forging": (
        ("cut_timber_scrap", 4), ("make_wooden_club", 20),
        ("make_salvaged_machete", 9), ("cut_tool_steel_blanks", 6),
        ("forge_pry_bar", 10), ("forge_wood_chisel", 27),
        ("make_current_arming_sword", 1),
    ),
    "static_nostudy_electronics_assuming_m2": (
        ("assemble_wiring_kit", 25), ("assemble_electrical_lead_set", 12),
        ("assemble_soldering_iron", 1), ("assemble_workshop_battery_pack", 9),
        ("assemble_makeshift_arc_welder", 1),
    ),
}
POWER_PROVIDERS = {"soldering": "soldering_iron", "welding": "makeshift_arc_welder"}
SCREWDRIVER_RECIPE = "make_makeshift_screwdriver"
ROUTE_BUDGET_FIELDS = (
    "manual_recipe_work", "machine_setup_work", "total_manual_work",
    "machine_processing_ticks", "machine_energy", "powered_tool_energy", "total_energy",
    "heated_work", "ignition_heat", "total_heat", "forge_fuel_item", "forge_fuel_count",
    "gross_external_consumed_mass_g", "gross_external_consumed_volume_ml",
    "gross_external_input_mass_g", "gross_external_input_volume_ml",
)


def route_budget(schedule, items, objects, recipes, requirements, providers, recipe_keys):
    """Carry only explicitly ordered outputs forward; never expand an input recipe."""
    inventory, external, retained, external_traits = Counter(), Counter(), Counter(), {}
    skill_minima = {}
    manual_work = machine_ticks = machine_energy = tool_energy = heated_work = setup_work = 0
    steps = []

    def available_traits():
        result = {}
        for item in inventory.keys() | retained.keys():
            if inventory[item] + retained[item] <= 0:
                continue
            sources = [("item", item)]
            if items[item]["placed_object"]:
                sources.append(("object", items[item]["placed_object"]))
            for source in sources:
                for trait, tier in providers[source].items():
                    result[trait] = max(result.get(trait, 0), tier)
        return result

    for recipe_id, quantity in schedule:
        if quantity < 1:
            raise ValueError(f"{recipe_id}: route quantity must be positive")
        key = recipe_keys[recipe_id]
        recipe, reqs = recipes[key], requirements[key]
        # Retained tools are shared between executions, never multiplied by quantity.
        # Deficits are the maximum outside-tool reservation needed at any stage.
        for r in reqs:
            if r["requirement"] == "retained":
                name, n = r["id"], int(r["count"])
                retained[name] = max(retained[name], max(0, n - inventory[name]))
            elif r["requirement"] == "skill":
                name = r["id"]
                skill_minima[name] = max(skill_minima.get(name, 0), int(r["count"]))
        if key[0] == "machine":
            retained["home_polymer_printer"] = max(
                retained["home_polymer_printer"], 1 - inventory["home_polymer_printer"])
        # Check each operation before its output exists. An anvil-2 output cannot
        # retroactively satisfy the earlier anvil-1 bootstrap requirement.
        supplied = available_traits()
        step_traits = {}
        for r in reqs:
            if r["requirement"] == "trait":
                name, tier = r["id"], int(r["count"])
                step_traits[name] = max(step_traits.get(name, 0), tier)
                if supplied.get(name, 0) < tier:
                    external_traits[name] = max(external_traits.get(name, 0), tier)
        for r in reqs:
            if r["requirement"] != "consumed":
                continue
            name, needed = r["id"], int(r["count"]) * quantity
            used = min(inventory[name], needed)
            inventory[name] -= used
            external[name] += needed - used
        work = int(recipe["work"]) * quantity
        heat_numerator = work * int(recipe["heat_per_work"])
        heat = (heat_numerator + 9999) // 10000
        heated_work += heat
        step_energy = 0
        if key[0] == "machine":
            # One tuple is one batch: overhead once, per-unit preparation scaled.
            setup = int(recipe["setup_work"]) + int(recipe["setup_work_per_unit"]) * quantity
            setup_work += setup
            machine_ticks += work
            step_energy = int(recipe["energy"]) * quantity
            machine_energy += step_energy
        else:
            setup = 0
            manual_work += work
            for trait, provider in POWER_PROVIDERS.items():
                if trait in step_traits:
                    rate = int(items[provider]["crafting_energy_per_work"])
                    if rate <= 0:
                        raise ValueError(f"{provider}: expected positive crafting_energy_per_work")
                    # One selected provider per required powered capability, not
                    # every accessible stronger/weaker provider simultaneously.
                    step_energy += (work * rate + 9999) // 10000
            tool_energy += step_energy
        inventory[recipe["output"]] += int(recipe["count"]) * quantity
        steps.append({"recipe": recipe_id, "quantity": quantity, "kind": key[0],
                      "work": work, "setup_work": setup, "heat": heat, "energy": step_energy,
                      "skill_and_training_requirements": [
                          {"type": r["requirement"], "id": r["id"], "count": int(r["count"])}
                           for r in reqs if r["requirement"] in ("skill", "training_level", "training_rate", "completion_xp")]})

    ignition = fuel_count = 0
    fuel_item = ""
    if heated_work:
        forge = objects["charcoal_forge"]
        ignition, heat_per_fuel = int(forge["ignition_heat"]), int(forge["heat_per_fuel"])
        fuel_item = forge["fuel_item"]
        if not fuel_item or heat_per_fuel <= 0:
            raise ValueError("charcoal_forge: expected one positive whole-item heat fuel")
        fuel_count = (heated_work + ignition + heat_per_fuel - 1) // heat_per_fuel
        external[fuel_item] += fuel_count
    gross = external + retained
    return {
        "schedule": steps,
        "external_consumed": dict(+external),
        "external_retained_tools": dict(+retained),
        "external_provider_traits": external_traits,
        "required_learned_skill_minima": skill_minima,
        "remaining_constructed_outputs": dict(+inventory),
        "manual_recipe_work": manual_work,
        "machine_setup_work": setup_work,
        "total_manual_work": manual_work + setup_work,
        "machine_processing_ticks": machine_ticks,
        "machine_energy": machine_energy,
        "powered_tool_energy": tool_energy,
        "total_energy": machine_energy + tool_energy,
        "heated_work": heated_work, "ignition_heat": ignition,
        "total_heat": heated_work + ignition,
        "forge_fuel_item": fuel_item, "forge_fuel_count": fuel_count,
        "gross_external_consumed_mass_g": sum(int(items[n]["mass_g"]) * c for n, c in external.items()),
        "gross_external_consumed_volume_ml": sum(int(items[n]["volume_ml"]) * c for n, c in external.items()),
        "gross_external_input_mass_g": sum(int(items[n]["mass_g"]) * c for n, c in gross.items()),
        "gross_external_input_volume_ml": sum(int(items[n]["volume_ml"]) * c for n, c in gross.items()),
    }


def route_deficits(budget, counts, traits):
    consumed = budget["external_consumed"]
    tools = budget["external_retained_tools"]
    materials, missing_tools, missing_traits = {}, {}, {}
    # Reserve consumed stock first. An outside retained tool cannot also be spent.
    for name, n in consumed.items():
        if counts[name] < n:
            materials[name] = n - counts[name]
    for name, n in tools.items():
        remaining = max(0, counts[name] - consumed.get(name, 0))
        if remaining < n:
            missing_tools[name] = n - remaining
    for name, tier in budget["external_provider_traits"].items():
        if traits.get(name, 0) < tier:
            missing_traits[name] = {"required_tier": tier, "available_tier": traits.get(name, 0)}
    return materials, missing_tools, missing_traits


def load(directory):
    data = {}
    for name, fields in FILES.items():
        path = directory / (name + ".csv")
        try:
            with path.open(newline="", encoding="utf-8-sig") as stream:
                reader = csv.DictReader(stream)
                missing = set(fields) - set(reader.fieldnames or ())
                if missing:
                    raise ValueError(f"{path}: missing columns: {', '.join(sorted(missing))}")
                rows = list(reader)
        except OSError as error:
            raise ValueError(f"{path}: {error}") from error
        if not rows:
            raise ValueError(f"{path}: dataset is empty")
        data[name] = rows
    return data


def keyed(rows, fields):
    result = {}
    for row in rows:
        key = tuple(row[field] for field in fields)
        if key in result:
            raise ValueError(f"duplicate key {key}")
        result[key] = row
    return result


def distribution(values):
    values = sorted(values)
    if not values:
        return {"n": 0}
    def percentile(p):
        return values[math.ceil(p * len(values)) - 1]
    return {"n": len(values), "min": values[0], "p50": percentile(.5),
            "p90": percentile(.9), "max": values[-1], "mean": sum(values) / len(values)}


def write_csv(path, fields, rows):
    with path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def main(directory):
    data = load(directory)
    sites = keyed(data["sites"], KEY)
    items = {row["id"]: row for row in data["items"]}
    objects = {row["id"]: row for row in data["objects"]}
    recipes = keyed(data["recipes"], ("kind", "id"))
    starts = keyed(data["starts"], ("seed",))
    if {k[0] for k in sites} != {k[0] for k in starts}:
        raise ValueError("sites.csv and starts.csv have different seed sets")
    providers = defaultdict(dict)
    for row in data["providers"]:
        providers[(row["kind"], row["id"])][row["trait"]] = int(row["tier"])
    requirements = defaultdict(list)
    for row in data["requirements"]:
        requirements[(row["kind"], row["recipe"])].append(row)
    selected = {}
    for name in RECIPE_IDS:
        matches = [key for key in recipes if key[1] == name]
        if len(matches) != 1:
            raise ValueError(f"expected one recipe named {name}, found {len(matches)}")
        selected[name] = matches[0]
    route_recipe_keys = {}
    for name in {name for schedule in ROUTES.values() for name, _ in schedule} | {SCREWDRIVER_RECIPE}:
        matches = [key for key in recipes if key[1] == name]
        if len(matches) != 1:
            raise ValueError(f"expected one route recipe named {name}, found {len(matches)}")
        route_recipe_keys[name] = matches[0]
    route_meta = {}
    for name, schedule in ROUTES.items():
        route_meta[name] = {
            "base": route_budget(schedule, items, objects, recipes, requirements,
                                 providers, route_recipe_keys),
            "with_screwdriver_fallback": route_budget(
                ((SCREWDRIVER_RECIPE, 1),) + schedule, items, objects, recipes,
                requirements, providers, route_recipe_keys),
        }

    # Physical stock is counted once; a packed object contributes its original item
    # identity, while placed-object traits are a separate capability view.
    stocks = defaultdict(list)
    for row in data["stock"]:
        key = tuple(row[field] for field in KEY)
        if key not in sites:
            raise ValueError(f"stock references missing site {key}")
        count, reachable, steps = (int(row[x]) for x in ("count", "reachable", "min_steps"))
        if count < 0 or not 0 <= reachable <= count or (reachable > 0 and steps < 0):
            raise ValueError(f"invalid stock counts at {key}")
        if row["kind"] not in ("item", "object") or row["id"] not in (items if row["kind"] == "item" else objects):
            raise ValueError(f"unknown stock identity {row['kind']}:{row['id']}")
        stocks[key].append(row)

    settlements = defaultdict(list)
    for key, row in sites.items():
        settlements[key[:-1]].append(key)
    observations = []
    for skey, keys in sorted(settlements.items()):
        first = sites[keys[0]]
        for key in keys:
            if any(sites[key][x] != first[x] for x in ("scale", "era", "origin_patch", "panel", "example", "starting_settlement")):
                raise ValueError(f"inconsistent settlement metadata {skey}")
        groups = [f"settlement_scale_era:{first['scale']}:{first['era']}"]
        if first["origin_patch"] == "1":
            groups.append("origin_patch_settlement")
        if first["starting_settlement"] == "1":
            groups.append("starting_settlement")
        for group in groups:
            observations.append((group, skey[0], "/".join(skey[1:]), keys))
    for seed in sorted(k[0] for k in starts):
        seed_keys = [key for key in sites if key[0] == seed]
        observations.append(("seed_small_medium_sampled_union", seed, "all_sampled_small_medium",
                             [key for key in seed_keys if sites[key]["scale"] in ("small", "medium")]))
        observations.append(("seed_full_sampled_union", seed, "all_sampled_settlements", seed_keys))

    basket_rows, stock_rows, metrics_rows, distance_rows, route_rows = [], [], [], [], []
    group_metrics = defaultdict(lambda: defaultdict(list))
    group_baskets = defaultdict(lambda: [0, 0])
    group_routes = defaultdict(lambda: [0, 0])
    group_distances = defaultdict(list)
    group_patterns = defaultdict(Counter)
    group_site_distances = defaultdict(list)
    recipe_meta = {}
    for name, key in selected.items():
        reqs = requirements[key]
        inputs = [r for r in reqs if r["requirement"] in ("consumed", "retained")]
        recipe_meta[name] = {
            "kind": key[0], "output": recipes[key]["output"],
            "requirements": [{"type": r["requirement"], "id": r["id"], "count": int(r["count"])}
                             for r in reqs if r["requirement"] in ("consumed", "retained", "trait")],
            "gross_input_mass_g": sum(int(items[r["id"]]["mass_g"]) * int(r["count"]) for r in inputs),
            "gross_input_external_volume_ml": sum(int(items[r["id"]]["volume_ml"]) * int(r["count"]) for r in inputs),
        }

    for group, seed, identity, keys in observations:
        counts, inaccessible, nearest, traits, book_counts = Counter(), Counter(), {}, {}, Counter()
        for key in keys:
            site = sites[key]
            for row in stocks[key]:
                kind, name = row["kind"], row["id"]
                n, lost = int(row["reachable"]), int(row["count"]) - int(row["reachable"])
                item = name if kind == "item" else objects[name]["packing_item"]
                if item:
                    counts[item] += n
                    inaccessible[item] += lost
                    if n:
                        nearest[item] = min(nearest.get(item, float("inf")), float(site["start_distance"]))
                if n:
                    sources = [(kind, name)]
                    if kind == "item" and items[name]["placed_object"]:
                        sources.append(("object", items[name]["placed_object"]))
                    for source in sources:
                        for trait, tier in providers[source].items():
                            traits[trait] = max(traits.get(trait, 0), tier)
                    if item:
                        info = items[item]
                        if int(info["capacity_energy"]) == 0 or int(info["initial_energy"]) > 0:
                            for book in info["books"].split(";"):
                                if book:
                                    book_counts[book] += n

        def basket(reqs):
            consumed, retained, required_traits = Counter(), Counter(), {}
            for r in reqs:
                kind, name, amount = r["requirement"], r["id"], int(r["count"])
                if kind == "consumed":
                    consumed[name] += amount
                elif kind == "retained":
                    retained[name] += amount
                elif kind == "trait":
                    required_traits[name] = max(required_traits.get(name, 0), amount)
            fallback = required_traits.get("screwdriving") == 1 and traits.get("screwdriving", 0) < 1
            if fallback:
                consumed["steel_scrap"] += 1
            deficits = {}
            for name in consumed.keys() | retained.keys():
                needed = consumed[name] + retained[name]
                if counts[name] < needed:
                    deficits["item:" + name] = needed - counts[name]
            for name, tier in required_traits.items():
                if name == "screwdriving" and fallback:
                    continue
                if traits.get(name, 0) < tier:
                    deficits["trait:" + name] = tier - traits.get(name, 0)
            return deficits, fallback

        results = {}
        for name, key in selected.items():
            reqs = list(requirements[key])
            if key[0] == "machine":
                reqs.append({"requirement": "retained", "id": "home_polymer_printer", "count": "1"})
            deficits, fallback = basket(reqs)
            results[name] = not deficits
            basket_rows.append({"group": group, "seed": seed, "observation": identity, "recipe": name,
                                "covered": int(not deficits), "screwdriver_fallback": int(fallback),
                                "deficits_json": json.dumps(deficits, sort_keys=True)})
            accumulator = group_baskets[(group, name)]
            accumulator[0] += 1
            accumulator[1] += not deficits

        route_results = {}
        for name, variants in route_meta.items():
            base = variants["base"]
            fallback = (base["external_provider_traits"].get("screwdriving") == 1
                        and traits.get("screwdriving", 0) < 1)
            budget = variants["with_screwdriver_fallback"] if fallback else base
            materials, tools, missing_traits = route_deficits(budget, counts, traits)
            covered = not (materials or tools or missing_traits)
            route_results[name] = covered
            missing_mass = sum(int(items[n]["mass_g"]) * c for n, c in materials.items())
            missing_volume = sum(int(items[n]["volume_ml"]) * c for n, c in materials.items())
            route_rows.append({
                "group": group, "seed": seed, "observation": identity, "route": name,
                "materials_tools_heat_fuel_covered": int(covered),
                "screwdriver_fallback": int(fallback),
                "missing_materials_json": json.dumps(materials, sort_keys=True),
                "missing_tools_json": json.dumps(tools, sort_keys=True),
                "missing_provider_traits_json": json.dumps(missing_traits, sort_keys=True),
                "missing_material_mass_g": missing_mass, "missing_material_volume_ml": missing_volume,
                **{field: budget[field] for field in ROUTE_BUDGET_FIELDS},
            })
            accumulator = group_routes[(group, name)]
            accumulator[0] += 1
            accumulator[1] += covered

        def has_traits(required):
            return all(traits.get(name, 0) >= tier for name, tier in required.items())

        metrics = {
            "sites": len(keys),
            "indoor_hostiles_after_town_exclusion": sum(int(sites[k]["indoor"]) for k in keys),
            "outdoor_near16_site_exposures_after_town_exclusion": sum(int(sites[k]["outdoor_near16"]) for k in keys),
            "inaccessible_items": sum(int(sites[k]["inaccessible_items"]) for k in keys),
            "inaccessible_objects": sum(int(r["count"]) - int(r["reachable"]) for k in keys for r in stocks[k] if r["kind"] == "object"),
            "screwdriving_or_one_scrap": int(traits.get("screwdriving", 0) >= 1 or counts["steel_scrap"] >= 1),
            "basic_tool_set": int(has_traits({"hammering": 1, "metal_holding": 1, "metal_saw": 1,
                                               "metal_drilling": 1, "wrenching": 1, "electrical_testing": 1})
                                  and (traits.get("screwdriving", 0) >= 1 or counts["steel_scrap"] >= 1)),
            "forge_commissioning_physical": int(has_traits({"anvil": 2, "forge_heat": 1, "metal_holding": 2, "hammering": 2})
                                                and counts["charcoal_charge"] >= 2 and counts["spring_steel_bar"] >= 1),
            "printer_plus_50_filament": int(counts["home_polymer_printer"] >= 1 and counts["polymer_filament"] >= 50),
            "printer_plus_211_filament": int(counts["home_polymer_printer"] >= 1 and counts["polymer_filament"] >= 211),
            "solar_kit_or_direct_assembly": int(counts["solar_generator_kit"] >= 1 or results["assemble_solar_generator_kit"]),
            "woodgas_kit_or_direct_final_assembly": int(counts["woodgas_generator_kit"] >= 1 or results["assemble_woodgas_generator_kit"]),
            "forging_found_or_manufactured_bill": int(route_results["skilled_found_forging_one_sword"]
                                                      or route_results["skilled_manufactured_forging_one_sword"]),
            "woodgas_found_or_assembled_bill": int(counts["woodgas_generator_kit"] >= 1
                                                   or route_results["skilled_assembled_woodgas"]),
        }
        metrics.update({f"readable_book:{book}": book_counts[book] for book in BOOKS})
        for key in keys:
            group_patterns[group][sites[key]["pattern"]] += 1
            group_site_distances[group].append(float(sites[key]["start_distance"]))
        for metric, value in metrics.items():
            metrics_rows.append({"group": group, "seed": seed, "observation": identity, "metric": metric, "value": value})
            group_metrics[group][metric].append(value)
        for item in KEY_ITEMS:
            stock_rows.append({"group": group, "seed": seed, "observation": identity, "item": item,
                               "reachable": counts[item], "inaccessible": inaccessible[item]})
            if item in nearest:
                distance_rows.append({"group": group, "seed": seed, "observation": identity,
                                      "item": item, "nearest_site_start_distance": nearest[item]})
                group_distances[(group, item)].append(nearest[item])

    summary = {
        "policy": {
            "panel": "Probe default seeds 0..31 and 42; actual seeds listed below. Origin patch includes its unfiltered settlements; panel/start/example inclusion is purposive. These chosen seeds are descriptive evidence, not a population-wide probability estimate.",
            "observations": "Settlement groups count settlements, including overlapping group membership. Seed unions count seeds, including empty small/medium unions; never pool stock across seeds. Full union is sampled sites, not the entire world.",
            "coverage": "Raw baskets remain reachable consumed + retained + trait co-occurrence for independent single recipes; they ignore skills, knowledge, fuel, energy, transport and crafting recursion. Zero raw coverage of a craft-only intermediate is not a broken assembled route.",
            "routes": "Named finite tuple schedules carry constructed output inventory forward, spending it before accumulating gross outside consumed deficits. Outside reusable retained tools use maximum simultaneous counts, not execution totals. Required external traits are checked before each output and retain full minimum tiers, so later upgraded equipment cannot satisfy its own bootstrap. Installable/packable object counting follows the existing stock policy. Routes are independent static bills, not an optimizer, recursive solver, travel plan, or simulated skill progression.",
            "route_coverage": "Per-observation material/tool/provider co-occurrence includes gross whole-item charcoal plus one ignition; no recycling credits. Coverage ignores learned skills, knowledge, transport, power availability/charge/rate/daylight and clearing hostiles. Skill minima and recipe training remain catalog data, not proof of eligibility or XP earned. All stock remains within each reported settlement or same-seed sampled union.",
            "route_practice": "static_nostudy schedules reproduce the audit's gross practice examples, not an optimized learning plan. Forging adds four timber-scrap preparations before the unchanged F0 club schedule; extra F0 credit is discarded. Electronics assumes separately learned Mechanics 2 for the welder. Study and salvage alternatives are not credited. Required learned skill minima are maxima encountered along the schedule, not asserted starting levels or simulated awards.",
            "route_energy": "Machine processing uses exported energy and ticks; each machine tuple is one selected batch with live setup_work + setup_work_per_unit * quantity. Manual powered traits use live soldering_iron or makeshift_arc_welder energy_per_work / 10000, rounded up per scheduled block. These fixed accounting provider IDs are separate from externally missing provider traits: stronger found providers may qualify without changing this conservative selected-provider budget. No extra charge for unpowered assembly. Heat is work * heat_per_work / 10000, rounded up per block; one live charcoal_forge ignition and its exported heat_per_fuel fund the route. Refill before zero within capacity; no idle burn.",
            "fallback": "Raw baskets may substitute one additional loose steel_scrap for missing screwdriving tier 1. Finite routes instead prepend the actual exported make_makeshift_screwdriver recipe exactly once when needed, including its real inputs, work, output traits and mass/volume. Reserved tool stock is never also consumed.",
            "objects": "Reachable packable objects count as recoverable original packing_item; reachable items with placed_object supply installable object traits, without duplicating physical stock.",
            "access": "Conservative cardinal-approach structural audit, not a survivable path. Distances are Euclidean Town-start to nearest site entrance, not travel distance. Outdoor-near counts are site exposures and can overlap.",
            "books": "Book carriers count only if print (capacity zero) or initially charged; identities are deduplicated per carrier; copies do not imply additional XP.",
            "mass": "Gross recipe input mass and external volume include consumed and retained items, not trait-provider equipment or optional screwdriver fallback.",
            "route_mass": "Route gross outside inputs include consumed stock, forge fuel and maximum exact retained tools, including fallback when selected. Trait-only external provider equipment, printer staging space, wiring/site layout and intermediate/output transport volume are not included; these are not carried-load or peak-space measurements.",
        },
        "seeds": sorted((int(k[0]) for k in starts)),
        "site_count": len(sites), "settlement_count": len(settlements),
        "groups": {group: {metric: distribution(values) for metric, values in metrics.items()}
                   for group, metrics in group_metrics.items()},
        "site_patterns": {group: {"denominator_sites": sum(patterns.values()), "counts": dict(patterns)}
                          for group, patterns in group_patterns.items()},
        "site_start_distance": {group: distribution(values) for group, values in group_site_distances.items()},
        "basket_coverage": {f"{group}|{recipe}": {"denominator": n, "covered": yes}
                             for (group, recipe), (n, yes) in group_baskets.items()},
        "route_coverage": {f"{group}|{route}": {"denominator": n, "covered": yes}
                           for (group, route), (n, yes) in group_routes.items()},
        "route_observations": [
            {**row,
             "missing_materials": json.loads(row["missing_materials_json"]),
             "missing_tools": json.loads(row["missing_tools_json"]),
             "missing_provider_traits": json.loads(row["missing_provider_traits_json"])}
            for row in route_rows],
        "routes": route_meta,
        "route_accounting_power_providers": {
            trait: {"item": name, "crafting_energy_per_work": int(items[name]["crafting_energy_per_work"])}
            for trait, name in POWER_PROVIDERS.items()},
        "nearest_site_start_distance": {f"{group}|{item}": distribution(values)
                                        for (group, item), values in group_distances.items()},
        "recipes": recipe_meta,
        "heavy_kit_mass_volume": {name: {"mass_g": int(items[name]["mass_g"]), "volume_ml": int(items[name]["volume_ml"])}
                                  for name in ("crude_anvil_kit", "fabricated_anvil_kit", "charcoal_forge_kit",
                                               "solar_generator_kit", "woodgas_generator_kit") if name in items},
    }
    write_csv(directory / "derived_metrics.csv", ("group", "seed", "observation", "metric", "value"), metrics_rows)
    write_csv(directory / "derived_stock.csv", ("group", "seed", "observation", "item", "reachable", "inaccessible"), stock_rows)
    write_csv(directory / "derived_baskets.csv", ("group", "seed", "observation", "recipe", "covered", "screwdriver_fallback", "deficits_json"), basket_rows)
    write_csv(directory / "derived_distances.csv", ("group", "seed", "observation", "item", "nearest_site_start_distance"), distance_rows)
    write_csv(directory / "derived_routes.csv",
              ("group", "seed", "observation", "route", "materials_tools_heat_fuel_covered",
               "screwdriver_fallback", "missing_materials_json", "missing_tools_json",
               "missing_provider_traits_json", "missing_material_mass_g", "missing_material_volume_ml",
               *ROUTE_BUDGET_FIELDS), route_rows)
    with (directory / "summary.json").open("w", encoding="utf-8") as stream:
        json.dump(summary, stream, indent=2, sort_keys=True)
        stream.write("\n")
    print(f"Workshop report: {len(starts)} seeds, {len(settlements)} settlements, {len(sites)} sites; wrote summary.json and five derived CSVs.")
    print("Raw baskets and finite ordered schedules are static co-occurrence bills, not simulated playthroughs; see summary.json for policies and denominators.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path, help="directory containing workshop_probe CSVs")
    args = parser.parse_args()
    try:
        main(args.directory)
    except (ValueError, KeyError, OSError) as error:
        print(f"workshop report: {error}", file=sys.stderr)
        sys.exit(1)
