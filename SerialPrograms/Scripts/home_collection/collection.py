#!/usr/bin/env python3
"""Editable HOME collection definitions and lossless, review-first planning."""
import argparse
import csv
import copy
from dataclasses import asdict
import hashlib
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "box_sorter"))
import planner
import observations
import reference_catalog
import execution_policy
import quality


def version_one(value):
    return isinstance(value, dict) and type(value.get("version")) is int and value["version"] == 1


def make_workspace(inventory):
    planner.validate_inventory(inventory)
    planner.source_validation(inventory)
    return {"version": 1, "kind": "home_collection_workspace",
            "source_inventory": inventory,
            "annotations": {item: observations.annotations_from_observation(inventory["pokemon"][item])
                            for item in inventory["slots"] if item is not None},
            "policy": {"extras": "review", "release_enabled": False}}


def merge_scan_workspaces(workspaces):
    """Join adjacent scan chunks while retaining each occurrence and annotation."""
    if not isinstance(workspaces, list) or not workspaces:
        raise ValueError("Provide at least one scan workspace.")
    checked = []
    for workspace in workspaces:
        inventory = validate_workspace(workspace)
        context = inventory["scan_context"]
        checked.append((context["physical_start_box"], workspace))
    checked.sort(key=lambda pair: pair[0])
    total = checked[0][1]["source_inventory"]["scan_context"]["total_boxes"]
    expected_start = checked[0][0]
    if any(item[1]["source_inventory"]["scan_context"]["total_boxes"] != total for item in checked):
        raise ValueError("Scan chunks disagree about the physical HOME box count.")

    slots, pokemon, annotations, source_hashes = [], {}, {}, []
    first = expected_start
    for start, workspace in checked:
        inventory = workspace["source_inventory"]
        context = inventory["scan_context"]
        if start != expected_start:
            raise ValueError("Scan chunks overlap or have a physical box gap.")
        source_hashes.append(planner.source_validation(inventory)["layout_sha256"])
        for index, old_id in enumerate(inventory["slots"]):
            new_index = len(slots)
            if old_id is None:
                slots.append(None)
                continue
            new_id = f"pokemon-{new_index:05d}"
            slots.append(new_id)
            pokemon[new_id] = copy.deepcopy(inventory["pokemon"][old_id])
            annotations[new_id] = copy.deepcopy(workspace["annotations"][old_id])
        expected_start = context["physical_end_box"] + 1

    last = expected_start - 1
    context = {"version": 1, "physical_start_box": first, "physical_end_box": last,
               "total_boxes": total, "box_count": len(slots) // 30,
               "coordinate_basis": "zero_based_relative_to_physical_start_box",
               "box_identity": "physical_counter_not_editable_label",
               "scan_id": "home-collection-merge-" + hashlib.sha256(
                   json.dumps({"range": [first, last], "layouts": source_hashes},
                              separators=(",", ":")).encode()).hexdigest()[:16],
               "source_export": "merged.json", "merged_layout_sha256": source_hashes}
    merged = {"version": 1, "slots": slots, "pokemon": pokemon, "scan_context": context}
    result = {"version": 1, "kind": "home_collection_workspace", "source_inventory": merged,
              "annotations": annotations, "policy": {"extras": "review", "release_enabled": False}}
    validate_workspace(result)
    return result


def merge_scan_files(workspace_paths, output_basename):
    """Verify each raw scan, then write one contiguous merged source/workspace."""
    workspaces = []
    for workspace_path in map(Path, workspace_paths):
        workspace = planner.read(workspace_path)
        inventory = validate_workspace(workspace)
        context = inventory["scan_context"]
        source_name = Path(context.get("source_export", "")).name
        if not source_name:
            raise ValueError("Each workspace must identify its raw source export.")
        source_path = workspace_path.parent / source_name
        scan_context = planner.load_scan_context(source_path, required=True)
        raw_inventory = planner.import_inventory(planner.read(source_path), scan_context)
        if planner.source_validation(raw_inventory) != planner.source_validation(inventory):
            raise ValueError(f"Workspace does not match its raw source export: {workspace_path}")
        workspaces.append(workspace)

    workspace = merge_scan_workspaces(workspaces)
    inventory = workspace["source_inventory"]
    basename = Path(output_basename)
    source_path = Path(str(basename) + ".json")
    scan_path = Path(str(basename) + "-scan.json")
    workspace_path = Path(str(basename) + "-workspace.json")
    if len({path.resolve() for path in (source_path, scan_path, workspace_path)}) != 3:
        raise ValueError("Merged output filenames collide.")
    if any(path.exists() for path in (source_path, scan_path, workspace_path)):
        raise ValueError("A merged output already exists; choose a new basename to preserve it.")

    inventory["scan_context"]["source_export"] = source_path.name
    rows = []
    for index, occurrence in enumerate(inventory["slots"]):
        row = {"index": index, "box": index // 30, "row": index // 6 % 5, "column": index % 6}
        if occurrence is not None:
            row.update(copy.deepcopy(inventory["pokemon"][occurrence]))
        rows.append(row)
    planner.write(source_path, rows)
    planner.write(scan_path, inventory["scan_context"])
    planner.write(workspace_path, workspace)
    return workspace_path


def validate_workspace(workspace):
    if not version_one(workspace) or workspace.get("kind") != "home_collection_workspace":
        raise ValueError("Expected a version 1 collection workspace.")
    inventory = workspace["source_inventory"]
    if not version_one(inventory):
        raise ValueError("Unsupported source inventory version.")
    planner.validate_inventory(inventory)
    planner.source_validation(inventory)
    for observed in inventory["pokemon"].values():
        if not isinstance(observed.get("name"), str) or not observed["name"] or type(observed.get("dex")) is not int or observed["dex"] < 0:
            raise ValueError("Source observations need species name and integer dex number.")
    if workspace.get("policy") != {"extras": "review", "release_enabled": False}:
        raise ValueError("This version keeps every extra for review and cannot release Pokemon.")
    annotations = workspace.get("annotations")
    if not isinstance(annotations, dict) or set(annotations) != set(inventory["pokemon"]):
        raise ValueError("Every source occurrence needs its own annotation record.")
    for annotation in annotations.values():
        if "iv_judge" in annotation:
            observations.validate_judge(annotation["iv_judge"])
        if not isinstance(annotation, dict):
            raise ValueError("Invalid annotation.")
        for key in ("identity_reviewed", "protected"):
            if type(annotation.get(key)) is not bool:
                raise ValueError(f"{key} must be an explicit boolean.")
        if annotation.get("form_key") is not None and (not isinstance(annotation["form_key"], str) or not annotation["form_key"]):
            raise ValueError("Form key must be a nonempty string or null for unknown.")
        if annotation["identity_reviewed"] and annotation.get("form_key") is None:
            raise ValueError("Reviewed identity needs an explicit form key.")
        if not isinstance(annotation.get("note", ""), str):
            raise ValueError("Review note must be text.")
        ivs = annotation.get("ivs")
        if ivs is not None:
            if not isinstance(ivs, dict) or set(ivs) != {"hp", "attack", "defense", "special_attack", "special_defense", "speed"}:
                raise ValueError("IVs must be null or a complete six-stat record.")
            if any(value is not None and (type(value) is not int or not 0 <= value <= 31) for value in ivs.values()):
                raise ValueError("IV values must be integers 0..31 or null for unknown.")
    return inventory


def observed_catalog(workspace):
    """An editable partial catalog of candidates, not an evolution database."""
    inventory = validate_workspace(workspace)
    forms = {}
    for item, observed in inventory["pokemon"].items():
        key = workspace["annotations"][item].get("form_key")
        if key is None:
            continue
        gender = "Male" if key.endswith("-male") else "Female" if key.endswith("-female") else None
        record = {"key": key, "dex": observed["dex"], "gender": gender,
                  "final_evolution": None, "shiny_available": None}
        if key in forms and forms[key] != record:
            raise ValueError("Observed form candidates conflict across species.")
        forms[key] = record
    return {"version": 1, "scope": "partial", "forms": list(forms.values())}


def validate_catalog(catalog):
    if not version_one(catalog) or catalog.get("scope") not in ("partial", "complete"):
        raise ValueError("Catalog needs version 1 and an explicit partial/complete scope.")
    if not isinstance(catalog.get("forms"), list):
        raise ValueError("Catalog needs an ordered forms list.")
    forms = {}
    for form in catalog["forms"]:
        if not isinstance(form, dict) or not isinstance(form.get("key"), str) or not form["key"] or form["key"] in forms:
            raise ValueError("Catalog form keys must be unique nonempty strings.")
        if type(form.get("dex")) is not int or form["dex"] < 1:
            raise ValueError("Form needs a positive species dex number.")
        for key in ("final_evolution", "shiny_available", "home_available"):
            if form.get(key) is not None and type(form[key]) is not bool:
                raise ValueError(f"{key} must be true, false, or null for unknown.")
        if form.get("gender") not in (None, "Male", "Female", "Genderless"):
            raise ValueError("Unsupported catalog gender.")
        if "abilities" in form and (not isinstance(form["abilities"], list)
                or not form["abilities"] or any(not isinstance(ability, str) or not ability
                                              for ability in form["abilities"])
                or len(set(form["abilities"])) != len(form["abilities"])):
            raise ValueError("Ability constraints need unique nonempty ability tokens.")
        if "visual_templates" in form:
            templates = form["visual_templates"]
            if (not isinstance(templates, dict) or set(templates) != {"normal", "shiny"}
                    or any(value is not None and (not isinstance(value, str) or not value)
                           for value in templates.values())):
                raise ValueError("Visual templates need normal/shiny tokens or null.")
        forms[form["key"]] = form
    return forms


def identity_review_sheet(workspace, catalog):
    """Source-bound decisions, with no candidate automatically approved."""
    inventory = validate_workspace(workspace)
    validate_catalog(catalog)
    entries = []
    for index, item in enumerate(inventory["slots"]):
        if item is None:
            continue
        annotation = workspace["annotations"][item]
        entries.append({"occurrence": item,
                        "physical_box": inventory["scan_context"]["physical_start_box"] + index // 30,
                        "slot": index % 30 + 1,
                        "observed": copy.deepcopy(inventory["pokemon"][item]),
                        "current_annotation": copy.deepcopy(annotation),
                        "decision": "leave_unchanged", "form_key": annotation.get("form_key"),
                        "review_note": ""})
    return {"version": 1, "kind": "home_identity_review",
            "source_validation": planner.source_validation(inventory),
            "catalog": copy.deepcopy(catalog), "entries": entries}


def apply_identity_review(workspace, catalog, sheet):
    """Apply explicit manual identity decisions to a copy, never raw evidence."""
    expected = identity_review_sheet(workspace, catalog)
    if not version_one(sheet) or sheet.get("kind") != expected["kind"]:
        raise ValueError("Expected a version 1 identity review sheet.")
    if sheet.get("source_validation") != expected["source_validation"]:
        raise ValueError("Identity review source mismatch; export a fresh sheet.")
    if sheet.get("catalog") != expected["catalog"]:
        raise ValueError("Identity review catalog changed; export a fresh sheet.")
    entries = sheet.get("entries")
    if not isinstance(entries, list) or len(entries) != len(expected["entries"]):
        raise ValueError("Identity review must retain every occurrence.")
    originals = {entry["occurrence"]: entry for entry in expected["entries"]}
    seen = set()
    result = copy.deepcopy(workspace)
    for entry in entries:
        if not isinstance(entry, dict) or not isinstance(entry.get("occurrence"), str):
            raise ValueError("Invalid identity review entry.")
        item = entry["occurrence"]
        if item not in originals or item in seen:
            raise ValueError("Unknown or repeated identity review occurrence.")
        seen.add(item)
        original = originals[item]
        if set(entry) != set(original) or any(entry[key] != original[key] for key in
                ("physical_box", "slot", "observed", "current_annotation")):
            raise ValueError("Identity review evidence or annotations changed; export a fresh sheet.")
        decision = entry["decision"]
        if decision not in ("leave_unchanged", "confirm", "unknown"):
            raise ValueError("Decision must be leave_unchanged, confirm, or unknown.")
        if not isinstance(entry["review_note"], str):
            raise ValueError("Identity review note must be text.")
        annotation = result["annotations"][item]
        if decision == "leave_unchanged":
            if entry["form_key"] != original["form_key"] or entry["review_note"]:
                raise ValueError("Edited identity requires an explicit decision.")
            continue
        if decision == "unknown":
            if entry["form_key"] is not None:
                raise ValueError("Unknown identity needs a null form key.")
            annotation.update(form_key=None, identity_reviewed=False)
        else:
            if not entry["review_note"].strip():
                raise ValueError("Confirmed identity needs a review note describing its basis.")
            annotation.update(form_key=entry["form_key"], identity_reviewed=True)
        annotation["identity_review"] = {"method": "manual", "note": entry["review_note"],
                                        "source_validation": expected["source_validation"]}
    # Existing species, gender, shiny and catalog checks also apply to manual decisions.
    review(result, catalog, {"version": 1, "collections": []})
    return result


def review(workspace, catalog, profiles):
    inventory = validate_workspace(workspace)
    forms = validate_catalog(catalog)
    if not version_one(profiles) or not isinstance(profiles.get("collections"), list):
        raise ValueError("Expected a version 1 collection profile list.")
    annotations = workspace["annotations"]
    eligible, unresolved = {}, []
    for item in inventory["slots"]:
        if item is None:
            continue
        annotation, observed = annotations[item], inventory["pokemon"][item]
        key = annotation.get("form_key")
        if key is not None:
            if key not in forms or forms[key]["dex"] != observed.get("dex"):
                raise ValueError(f"Form assignment disagrees with the observed species: {item}.")
            if forms[key].get("gender") is not None and forms[key]["gender"] != observed.get("gender"):
                raise ValueError(f"Form assignment disagrees with observed gender: {item}.")
        if not annotation["identity_reviewed"] or type(observed.get("shiny")) is not bool:
            unresolved.append(item)
        else:
            if observed["shiny"] and forms[key].get("shiny_available") is False:
                raise ValueError("Reviewed shiny identity conflicts with catalog availability.")
            eligible.setdefault((key, observed["shiny"]), []).append(item)
    context = inventory["scan_context"]
    used, reserved, names, assignments, gaps = set(), set(), set(), [], []
    quality_unresolved, judge_verification = [], False
    for profile in profiles["collections"]:
        if not isinstance(profile, dict) or not isinstance(profile.get("id"), str) or not profile["id"] or profile["id"] in names:
            raise ValueError("Each collection needs a unique ID.")
        names.add(profile["id"])
        kind = profile.get("kind")
        if kind not in ("living_form", "shiny_living_form", "final_form", "custom"):
            raise ValueError("Unsupported collection kind.")
        if type(profile.get("start_box")) is not int or type(profile.get("start_slot", 1)) is not int or not 1 <= profile.get("start_slot", 1) <= 30:
            raise ValueError("Collection placement needs a physical box and slot 1..30.")
        if not context["physical_start_box"] <= profile["start_box"] <= context["physical_end_box"]:
            raise ValueError("Collection starts outside the scanned physical range.")
        start = (profile["start_box"] - context["physical_start_box"]) * 30 + profile.get("start_slot", 1) - 1
        keys = profile.get("form_keys", list(forms))
        if not isinstance(keys, list) or any(not isinstance(key, str) or key not in forms for key in keys) or len(set(keys)) != len(keys):
            raise ValueError("Invalid or repeated collection form keys.")
        if any("home_available" in forms[key] and forms[key]["home_available"] is not True for key in keys):
            raise ValueError("Reference forms need reviewed HOME eligibility before collection placement.")
        if kind == "final_form":
            if any(forms[key].get("final_evolution") is None for key in keys):
                raise ValueError("Final-form collection needs known evolution classification for every selected form.")
            keys = [key for key in keys if forms[key]["final_evolution"]]
        shiny = profile.get("shiny", kind == "shiny_living_form")
        if type(shiny) is not bool or (kind == "shiny_living_form" and not shiny):
            raise ValueError("Collection shiny requirement must be an explicit boolean.")
        if shiny:
            if any(forms[key].get("shiny_available") is None for key in keys):
                raise ValueError("Shiny collection needs explicit availability in the catalog.")
            keys = [key for key in keys if forms[key]["shiny_available"]]
        quality_policy = profile.get("quality")
        required, preference = quality.validate_policy(quality_policy) if quality_policy is not None else ({}, "existing")
        judge_verification |= quality_policy is not None
        preferred = profile.get("preferred", {})
        if not isinstance(preferred, dict) or any(key not in keys for key in preferred):
            raise ValueError("Preferred assignments must refer to required form keys.")
        for offset, key in enumerate(keys):
            index = start + offset
            if not 0 <= index < len(inventory["slots"]) or index in reserved:
                raise ValueError("Collection destinations overlap or fall outside the scanned range.")
            reserved.add(index)
            choices = [item for item in eligible.get((key, shiny), ()) if item not in used]
            if quality_policy is not None:
                accepted = []
                ratings = {}
                for item in choices:
                    evidence = quality.source_judge(inventory["pokemon"][item])
                    needed = set(observations.STATS) if preference == "most_best" else set(required)
                    if any(evidence[stat] is None for stat in needed):
                        quality_unresolved.append({"collection": profile["id"], "occurrence": item,
                                                   "status": "unknown_judge_keep_for_review"})
                        continue
                    if any(evidence[stat]["rating"] != label for stat, label in required.items()):
                        continue
                    accepted.append(item)
                    ratings[item] = sum(value is not None and value["rating"] == "Best" for value in evidence.values())
                choices = accepted
                if preference == "most_best" and choices and preferred.get(key) is None:
                    best = max(ratings[item] for item in choices)
                    choices = [item for item in choices if ratings[item] == best]
            selected = preferred.get(key)
            if selected is not None:
                if selected not in choices:
                    raise ValueError("Preferred occurrence is unavailable, unreviewed, or belongs to another entry.")
            else:
                # Prefer an existing correct placement; do not invent an IV ranking.
                current = inventory["slots"][index]
                selected = current if current in choices else next(iter(choices), None)
            if selected is not None:
                used.add(selected)
            assignment = {"collection": profile["id"], "form_key": key, "shiny": shiny,
                          "index": index, "physical_box": context["physical_start_box"] + index // 30,
                          "slot": index % 30 + 1, "occurrence": selected}
            assignments.append(assignment)
            if selected is None:
                gaps.append({**assignment, "status": "not_found_in_reviewed_scanned_inventory"})
    extras = []
    for index, item in enumerate(inventory["slots"]):
        if item is not None and item not in used:
            extras.append({"occurrence": item, "physical_box": context["physical_start_box"] + index // 30,
                           "slot": index % 30 + 1, "observed": inventory["pokemon"][item],
                           "annotation": annotations[item], "action": "keep_for_review"})
    return {"version": 1, "kind": "home_collection_review", "catalog_scope": catalog["scope"],
            "source_validation": planner.source_validation(inventory), "assignments": assignments,
            "gaps": gaps, "unresolved_identity": unresolved, "extras": extras,
            "quality_unresolved": quality_unresolved, "judge_verification": judge_verification,
            "release_candidates": [], "release_enabled": False,
            "counts": {"scanned": len(inventory["pokemon"]), "assigned": len(used),
                       "review": len(extras), "gaps": len(gaps), "unresolved": len(unresolved)}}


def target_layout(workspace, report):
    inventory = validate_workspace(workspace)
    if report.get("source_validation") != planner.source_validation(inventory):
        raise ValueError("Review belongs to another source inventory.")
    if report["unresolved_identity"]:
        raise ValueError("Review form identities before planning collection moves.")
    target = [None] * len(inventory["slots"])
    reserved, assigned = set(), set()
    for assignment in report["assignments"]:
        index, item = assignment["index"], assignment["occurrence"]
        if type(index) is not int or not 0 <= index < len(target) or index in reserved:
            raise ValueError("Invalid collection destination.")
        if item is not None and (item not in inventory["pokemon"] or item in assigned):
            raise ValueError("Invalid or repeated assigned occurrence.")
        reserved.add(index)
        target[index] = item
        if item is not None:
            assigned.add(item)
    extras = [item for item in inventory["slots"] if item is not None and item not in assigned]
    free = [index for index in range(len(target)) if index not in reserved]
    if len(extras) > len(free):
        raise ValueError("Collection gaps plus review Pokemon need more scanned storage; no extras will be discarded.")
    locations = {item: index for index, item in enumerate(inventory["slots"]) if item is not None}
    remaining = []
    for item in extras:
        index = locations[item]
        if index not in reserved:
            target[index] = item
        else:
            remaining.append(item)
    empty = (index for index in free if target[index] is None)
    for item in remaining:
        target[next(empty)] = item
    planner.validate_layout(target, inventory["pokemon"], len(target))
    return target


def move_plan(workspace, catalog, profiles, enable_execution=False, verify_judge=False):
    report = review(workspace, catalog, profiles)
    report["judge_verification"] |= verify_judge
    inventory = validate_workspace(workspace)
    target = target_layout(workspace, report)
    model = planner.CostModel()
    moves = planner.make_plan(inventory["slots"], target, "nearest_swap", 0, model)
    metrics = planner.simulate(inventory["slots"], target, moves, 0, model)
    result = {"version": 1, "inventory": inventory, "target": target, "start_cursor": 0,
            "cost_model": asdict(model), "strategy": "nearest_swap", "moves": moves,
            "comparison": {"nearest_swap": metrics}, "source_validation": planner.source_validation(inventory),
            "collection_review": report, "execution_blocked": True,
            "execution_blocked_reason": "Preview plan. Enable execution only after reviewed metadata identities pass live-verification requirements."}
    if enable_execution:
        result["collection_execution"] = execution_policy.build_execution_contract(workspace, target, report, verify_judge=verify_judge)
        result["execution_blocked"] = False
        result.pop("execution_blocked_reason")
    return result


def write_review_csv(workspace, report, path):
    inventory = validate_workspace(workspace)
    locations = {item: index for index, item in enumerate(inventory["slots"]) if item is not None}
    assignments = {item["occurrence"]: item["collection"] for item in report["assignments"] if item["occurrence"] is not None}
    stats = ("hp", "attack", "defense", "special_attack", "special_defense", "speed")
    fields = ["occurrence", "physical_box", "slot", "dex", "name", "form_key", "identity_reviewed",
              "shiny", "collection", "action", "protected", *["iv_" + stat for stat in stats],
              *["judge_" + stat for stat in stats], *["hyper_trained_" + stat for stat in stats], "note"]
    def cell(value):
        if value is None:
            return ""
        if isinstance(value, str) and value.startswith(("=", "+", "-", "@")):
            return "'" + value
        return value
    with Path(path).open("x", encoding="utf-8-sig", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        for item, index in locations.items():
            annotation, observed = workspace["annotations"][item], inventory["pokemon"][item]
            row = {"occurrence": item, "physical_box": inventory["scan_context"]["physical_start_box"] + index // 30,
                   "slot": index % 30 + 1, "dex": observed["dex"], "name": observed["name"],
                   "form_key": annotation["form_key"], "identity_reviewed": annotation["identity_reviewed"],
                   "shiny": observed.get("shiny"), "collection": assignments.get(item),
                   "action": "collection_member" if item in assignments else "keep_for_review",
                   "protected": annotation["protected"], "note": annotation.get("note", "")}
            row.update({"iv_" + stat: (annotation.get("ivs") or {}).get(stat) for stat in stats})
            judge = (annotation.get("iv_judge") or {}).get("stats", {})
            row.update({"judge_" + stat: judge.get(stat, {}).get("rating") for stat in stats})
            row.update({"hyper_trained_" + stat: judge.get(stat, {}).get("hyper_trained") for stat in stats})
            writer.writerow({key: cell(value) for key, value in row.items()})


def species_move_plan(workspace):
    inventory = validate_workspace(workspace)
    contract = execution_policy.species_execution_contract(workspace)
    stationary = set(contract["stationary_review_occurrences"])
    source = inventory["slots"]
    target = [None] * len(source)
    fixed_slots = {i for i, item in enumerate(source) if item in stationary}
    for i in fixed_slots:
        target[i] = source[i]
    movable = sorted((item for item in source if item is not None and item not in stationary),
                     key=lambda item: inventory["pokemon"][item]["dex"])
    free = [i for i in range(len(target)) if i not in fixed_slots]
    for i, item in zip(free, movable):
        target[i] = item
    model = planner.CostModel()
    moves = planner.make_plan(source, target, "nearest_swap", 0, model)
    # Equal live identities can trade target labels without changing the requested
    # species order. Never physically swap such copies: before/after observations
    # would be identical, making an interrupted move impossible to reconcile.
    signatures = {item: execution_policy.species_record(record, False)
                  for item, record in inventory["pokemon"].items()}
    signatures[None] = None
    state = source.copy()
    physical_moves = []
    for move in moves:
        a, b = move["source"], move["destination"]
        if state[a] in stationary or state[b] in stationary:
            raise ValueError("Species plan touches a stationary review copy.")
        if signatures[state[a]] == signatures[state[b]]:
            continue
        move = dict(move, expected_source=state[a], expected_destination=state[b])
        physical_moves.append(move)
        state[a], state[b] = state[b], state[a]
    if any(signatures[a] != signatures[b] for a, b in zip(state, target)):
        raise ValueError("Species plan changed the requested observable layout.")
    target = state
    moves = physical_moves
    metrics = planner.simulate(source, target, moves, 0, model)
    return {"version": 1, "inventory": inventory, "target": target, "start_cursor": 0,
            "cost_model": asdict(model), "strategy": "nearest_swap", "moves": moves,
            "metrics": metrics, "source_validation": planner.source_validation(inventory),
            "execution_blocked": False, "collection_execution": contract,
            "real_job": {"layout": "national_dex_all_copies", "physical_range": [inventory["scan_context"]["physical_start_box"], inventory["scan_context"]["physical_end_box"]],
                         "release_enabled": False, "exact_form_assignment": False,
                         "stationary_review_count": len(stationary)}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    species = commands.add_parser("species-plan", help="National Dex keep-all with slot-tracked copies; no exact-form assignments")
    species.add_argument("workspace"); species.add_argument("output")
    scan = commands.add_parser("import", help="Create an editable workspace from an indexed source export")
    scan.add_argument("source"); scan.add_argument("output")
    merge = commands.add_parser("merge-scans", help="Join adjacent, verified scan chunks into one source-bound workspace")
    merge.add_argument("output_basename", help="New output basename; creates .json, -scan.json and -workspace.json")
    merge.add_argument("workspaces", nargs="+", help="Chunk workspace JSON files with adjacent physical ranges")
    candidates = commands.add_parser("catalog", help="Create an editable partial catalog from observed form candidates")
    candidates.add_argument("workspace"); candidates.add_argument("output")
    reference = commands.add_parser("reference-catalog", help="Build the pinned form/evolution reference catalog")
    reference.add_argument("output")
    identify = commands.add_parser("identify", help="Audit form candidates against reference types and gender")
    for argument in ("workspace", "catalog", "output"):
        identify.add_argument(argument)
    identities = commands.add_parser("identity-review", help="Export source-bound manual form review decisions")
    for argument in ("workspace", "catalog", "output"):
        identities.add_argument(argument)
    apply = commands.add_parser("apply-identities", help="Apply explicit review decisions to a new workspace")
    for argument in ("workspace", "catalog", "decisions", "output"):
        apply.add_argument(argument)
    for name in ("review", "plan"):
        command = commands.add_parser(name)
        for argument in ("workspace", "catalog", "profiles", "output"):
            command.add_argument(argument)
        if name == "plan":
            command.add_argument("--verify-judge", action="store_true", help="Verify captured Judge evidence during live execution")
            command.add_argument("--enable-execution", action="store_true",
                                 help="Require reviewed metadata identities and bind the plan to live form verification")
        if name == "review":
            command.add_argument("--csv", help="Also create a spreadsheet-friendly review report (not authoritative input)")
    args = parser.parse_args()
    try:
        if args.command == "species-plan":
            result = species_move_plan(planner.read(args.workspace))
        elif args.command == "import":
            inventory = planner.import_inventory(planner.read(args.source), planner.load_scan_context(args.source, required=True))
            result = make_workspace(inventory)
        elif args.command == "merge-scans":
            output_path = merge_scan_files(args.workspaces, args.output_basename)
            print(json.dumps({"result": "merged_scans_written", "output": str(output_path),
                              "release_enabled": False}))
            return
        elif args.command == "catalog":
            result = observed_catalog(planner.read(args.workspace))
        elif args.command == "reference-catalog":
            result = reference_catalog.build_reference()
            validate_catalog(result)
        elif args.command == "identify":
            result = reference_catalog.identification_report(planner.read(args.workspace), planner.read(args.catalog))
        elif args.command == "identity-review":
            result = identity_review_sheet(planner.read(args.workspace), planner.read(args.catalog))
        elif args.command == "apply-identities":
            result = apply_identity_review(planner.read(args.workspace), planner.read(args.catalog),
                                           planner.read(args.decisions))
        else:
            function = review if args.command == "review" else move_plan
            arguments = (planner.read(args.workspace), planner.read(args.catalog), planner.read(args.profiles))
            result = function(*arguments, enable_execution=args.enable_execution, verify_judge=args.verify_judge) if args.command == "plan" else function(*arguments)
        if getattr(args, "csv", None):
            if Path(args.csv).exists() or Path(args.csv).resolve() == Path(args.output).resolve():
                raise ValueError("CSV output exists or collides with the JSON output.")
        planner.write(args.output, result)
        if getattr(args, "csv", None):
            write_review_csv(planner.read(args.workspace), result, args.csv)
        print(json.dumps(result.get("counts", {"result": args.command + "_written", "release_enabled": False})))
    except (ValueError, KeyError, OSError, TypeError) as error:
        parser.exit(2, f"Error: {error}\n")


if __name__ == "__main__":
    main()
