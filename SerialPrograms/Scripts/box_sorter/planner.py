#!/usr/bin/env python3
"""Offline HOME box move planner. Standard library only; Python 3.10+."""

import argparse
from collections import Counter, defaultdict, deque
from dataclasses import asdict, dataclass
import hashlib
import heapq
import json
import math
from pathlib import Path
import random


COORDINATES = {"index", "box", "row", "column"}


def read(path):
    return json.loads(Path(path).read_text(encoding="utf-8-sig"))


def write(path, value):
    # Exclusive creation protects both source inventories and existing plans.
    with Path(path).open("x", encoding="utf-8") as stream:
        json.dump(value, stream, indent=2, ensure_ascii=False, allow_nan=False)
        stream.write("\n")


def legacy_slots(rows):
    """Read save_boxes_data_to_json output, including coordinate-only empty slots."""
    if not isinstance(rows, list) or not rows or len(rows) % 30:
        raise ValueError("Export must contain complete boxes of 30 slots.")
    slots = [None] * len(rows)
    seen = set()
    for row in rows:
        if not isinstance(row, dict) or not COORDINATES <= row.keys():
            raise ValueError("Each exported slot needs index, box, row and column.")
        index = row["index"]
        if type(index) is not int or not 0 <= index < len(rows) or index in seen:
            raise ValueError("Slot indices must be unique and cover the entire export.")
        expected = (index // 30, index // 6 % 5, index % 6)
        actual = tuple(row[key] for key in ("box", "row", "column"))
        if any(type(v) is not int for v in actual) or actual != expected:
            raise ValueError(f"Inconsistent coordinates at slot {index}.")
        seen.add(index)
        info = {key: value for key, value in row.items() if key not in COORDINATES}
        if info:
            if not isinstance(info.get("name"), str) or not info["name"]:
                raise ValueError(f"Occupied slot {index} needs a name.")
            if type(info.get("dex")) is not int or info["dex"] < 0:
                raise ValueError(f"Occupied slot {index} needs a nonnegative dex number.")
            slots[index] = info
    return slots


def import_inventory(rows, scan_context=None):
    entries = legacy_slots(rows)
    slots = [f"pokemon-{i:05d}" if item is not None else None
             for i, item in enumerate(entries)]
    inventory = {"version": 1, "slots": slots,
                 "pokemon": {slots[i]: item for i, item in enumerate(entries) if item is not None}}
    if scan_context is not None:
        inventory["scan_context"] = validate_scan_context(scan_context, len(slots))
    return inventory


def validate_inventory(inventory):
    if not isinstance(inventory, dict) or inventory.get("version") != 1:
        raise ValueError("Expected a version 1 inventory.")
    slots, catalog = inventory.get("slots"), inventory.get("pokemon")
    if not isinstance(slots, list) or not slots or len(slots) % 30:
        raise ValueError("Inventory must contain complete boxes.")
    if not isinstance(catalog, dict) or any(not isinstance(v, dict) for v in catalog.values()):
        raise ValueError("Invalid Pokemon catalog.")
    validate_layout(slots, list(catalog))
    if "scan_context" in inventory:
        validate_scan_context(inventory["scan_context"], len(slots))
    return slots


def validate_scan_context(context, slot_count):
    if not isinstance(context, dict) or type(context.get("version")) is not int or context["version"] != 1:
        raise ValueError("Expected a version 1 scan context.")
    fields = ("physical_start_box", "physical_end_box", "total_boxes", "box_count")
    if any(type(context.get(key)) is not int for key in fields):
        raise ValueError("Physical box range must contain integer positions.")
    first, last, total, count = (context[key] for key in fields)
    if not 1 <= first <= last <= total <= 300 or count != slot_count // 30 or last != first + count - 1:
        raise ValueError("Scan context does not match the exported physical box range.")
    if context.get("coordinate_basis") != "zero_based_relative_to_physical_start_box" or context.get("box_identity") != "physical_counter_not_editable_label":
        raise ValueError("Unsupported box identity or coordinate basis.")
    if not isinstance(context.get("scan_id"), str) or not context["scan_id"]:
        raise ValueError("Scan context requires a scan ID.")
    return dict(context)


def load_scan_context(export_path, explicit_path=None, required=False):
    export_path = Path(export_path)
    path = Path(explicit_path) if explicit_path else export_path.with_name(export_path.stem + "-scan.json")
    if not path.exists():
        if explicit_path or required:
            raise ValueError("A physical scan context is required; rescan with the updated HOME sorter.")
        return None
    context = read(path)
    if not isinstance(context, dict) or not isinstance(context.get("source_export"), str):
        raise ValueError("Scan context requires its source export filename.")
    # Accept renamed directories, but never silently bind the target export or
    # a different source file to this scan's origin.
    source_name = context["source_export"].replace("\\", "/").rsplit("/", 1)[-1]
    if source_name != export_path.name:
        raise ValueError("Scan context belongs to a different source export.")
    return context


def source_validation(inventory):
    slots = validate_inventory(inventory)
    if "scan_context" not in inventory:
        raise ValueError("Saved-plan source checks need a physical scan context.")
    context = validate_scan_context(inventory["scan_context"], len(slots))
    physical_range = {key: context[key] for key in
                      ("physical_start_box", "physical_end_box", "total_boxes", "box_count")}
    # Names of boxes, filenames, scan timestamps and job-local occurrence IDs
    # are not identity. Hash ordered observed Pokemon records at physical slots.
    observed = [inventory["pokemon"][item] if item is not None else None for item in slots]
    canonical = json.dumps({"physical_range": physical_range, "slots": observed},
                           sort_keys=True, separators=(",", ":"), ensure_ascii=False, allow_nan=False)
    return {"version": 1, "physical_range": physical_range,
            "layout_sha256": hashlib.sha256(canonical.encode("utf-8")).hexdigest()}


def verify_saved_source(plan, fresh_rows, fresh_context):
    if not isinstance(plan, dict) or plan.get("version") != 1:
        raise ValueError("Unsupported plan version.")
    expected = source_validation(plan["inventory"])
    if plan.get("source_validation") != expected:
        raise ValueError("Plan source validation is missing or its recorded source was changed; regenerate the plan.")
    # A matching scan does not excuse a broken plan: replay its preconditions.
    simulate(validate_inventory(plan["inventory"]), plan["target"], plan["moves"],
             plan["start_cursor"], CostModel(**plan["cost_model"]))
    fresh = source_validation(import_inventory(fresh_rows, fresh_context))
    if fresh["physical_range"] != expected["physical_range"]:
        raise ValueError("Fresh scan covers a different physical box range; rescan the original range.")
    if fresh["layout_sha256"] != expected["layout_sha256"]:
        raise ValueError("Pokemon layout or observed metadata changed; rescan and regenerate the plan.")
    return {"result": "source_matches", "physical_range": expected["physical_range"],
            "console_moves_performed": False}


def validate_layout(slots, ids, size=None):
    if not isinstance(slots, list) or (size is not None and len(slots) != size):
        raise ValueError("Target must have the same slot count as the inventory.")
    if any(item is not None and not isinstance(item, str) for item in slots):
        raise ValueError("Slots must contain Pokemon IDs or null.")
    occupied = [item for item in slots if item is not None]
    if Counter(occupied) != Counter(list(ids)) or len(set(occupied)) != len(occupied):
        raise ValueError("Layout must contain every inventory ID exactly once; no release is implied.")


def target_from_export(inventory, rows):
    """Match identical exported records, preserving already-correct duplicates first."""
    entries = legacy_slots(rows)
    source = validate_inventory(inventory)
    if len(entries) != len(source):
        raise ValueError("Source and target box counts differ.")
    signature = lambda item: json.dumps(item, sort_keys=True, allow_nan=False)
    catalog = inventory["pokemon"]
    remaining = defaultdict(deque)
    target = [None] * len(source)
    used = set()
    for i, item in enumerate(entries):
        if item is not None and source[i] is not None and catalog[source[i]] == item:
            target[i] = source[i]
            used.add(source[i])
    for item in source:
        if item is not None and item not in used:
            remaining[signature(catalog[item])].append(item)
    for i, item in enumerate(entries):
        if item is not None and target[i] is None:
            matches = remaining[signature(item)]
            if not matches:
                raise ValueError("Target export changes or adds Pokemon metadata.")
            target[i] = matches.popleft()
    validate_layout(target, catalog, len(source))
    return target


@dataclass(frozen=True)
class CostModel:
    game_delay_ms: float = 240
    group_page_size: int = 30

    def __post_init__(self):
        if not math.isfinite(self.game_delay_ms) or self.game_delay_ms < 0:
            raise ValueError("Game delay must be finite and nonnegative.")
        if type(self.group_page_size) is not int or self.group_page_size < 1:
            raise ValueError("Group navigation page size must be a positive integer.")

    def navigation(self, start, end):
        box = abs(start // 30 - end // 30)
        row = abs(start // 6 % 5 - end // 6 % 5)
        # Match PokemonHome_BoxNavigation.cpp, including its special row path.
        row = 3 if row == 4 else row
        col = abs(start % 6 - end % 6)
        cursor = row + min(col, 6 - col)
        return box, cursor

    def travel_ms(self, start, end):
        box, cursor = self.navigation(start, end)
        return box * (320 + self.game_delay_ms) + cursor * (80 + self.game_delay_ms)

    def overview_navigation(self, start, end):
        """Estimate Box Spaces navigation in either held or unheld mode."""
        source_box, target_box = start // 30, end // 30
        pages = abs(target_box // self.group_page_size - source_box // self.group_page_size)
        source_cell, target_cell = source_box % self.group_page_size, target_box % self.group_page_size
        grid = abs(source_cell // 6 - target_cell // 6) + abs(source_cell % 6 - target_cell % 6)
        # Return to the box's first slot, go up to Box Spaces, open the overview,
        # enter the destination box, then move to its exact slot.
        before = self.navigation(start, source_box * 30)[1]
        after = self.navigation(target_box * 30, end)[1]
        cursor = before + 2 + grid + after
        confirms = 2
        return pages, cursor, confirms

    def navigation_route(self, start, end):
        """Choose the cheaper box-scroll or Box Spaces route."""
        linear = self.travel_ms(start, end)
        pages, cursor, confirms = self.overview_navigation(start, end)
        overview = (pages + confirms) * (320 + self.game_delay_ms) + cursor * (80 + self.game_delay_ms)
        if overview < linear:
            return "overview", pages, cursor, confirms
        box, cursor = self.navigation(start, end)
        return "linear", box, cursor, 0

    def route_travel_ms(self, start, end):
        mode, first, cursor, confirms = self.navigation_route(start, end)
        if mode == "linear":
            return first * (320 + self.game_delay_ms) + cursor * (80 + self.game_delay_ms)
        return (first + confirms) * (320 + self.game_delay_ms) + cursor * (80 + self.game_delay_ms)

    def group_navigation(self, start, end):
        """Observed Box Spaces paging while holding a group, plus box-grid steps.

        HOME showed 30 boxes per page. Their overview is a 6x5 grid. This is
        still a cost-model assumption: the console check verified one page jump,
        but did not measure action latency or very long jumps.
        """
        source_box, target_box = start // 30, end // 30
        source_page, target_page = source_box // self.group_page_size, target_box // self.group_page_size
        pages = abs(target_page - source_page)
        source_cell, target_cell = source_box % self.group_page_size, target_box % self.group_page_size
        row = abs(source_cell // 6 - target_cell // 6)
        col = abs(source_cell % 6 - target_cell % 6)
        grid = row + col
        return pages, grid


def make_plan(source, target, strategy, start_cursor, model):
    """Swaps are abstract completed operations, never live controller commands."""
    state = source.copy()
    positions = {item: i for i, item in enumerate(state) if item is not None}
    destinations = {item: i for i, item in enumerate(target) if item is not None}
    moves = []
    cursor = start_cursor

    def swap(start, end):
        nonlocal cursor
        moves.append({"source": start, "destination": end,
                      "expected_source": state[start], "expected_destination": state[end]})
        state[start], state[end] = state[end], state[start]
        for slot in (start, end):
            if state[slot] is not None:
                positions[state[slot]] = slot
        cursor = end

    if strategy == "batch_first":
        # Reuse a box that is empty in both the starting and final layouts.
        # Park a target row's occupants, assemble the desired row in that box,
        # carry the group to the cleared row, then sort any remaining entries.
        work_boxes = [box for box in range(len(state) // 30)
                      if all(state[i] is None and target[i] is None
                             for i in range(box * 30, (box + 1) * 30))]
        if work_boxes:
            work_box = work_boxes[-1]
            for box in range(len(state) // 30):
                for row in range(5):
                    ends = [box * 30 + row * 6 + col for col in range(6)]
                    ids = [target[i] for i in ends]
                    if any(item is None for item in ids) or [state[i] for i in ends] == ids:
                        continue
                    stage = [work_box * 30 + row * 6 + col for col in range(6)]
                    if any(state[i] is not None for i in stage):
                        continue
                    occupants = [(i, state[i]) for i in ends if state[i] is not None]
                    scratch = [i for i, item in enumerate(state)
                               if item is None and i not in stage and i not in ends]
                    if len(scratch) < len(occupants):
                        continue
                    for (source_slot, item), empty in zip(occupants, scratch):
                        moves.append({"kind": "move", "source": source_slot,
                                      "destination": empty, "expected_source": item})
                        state[empty], state[source_slot] = item, None
                        positions[item] = empty
                        cursor = empty
                    for stage_slot, item in zip(stage, ids):
                        source_slot = positions[item]
                        moves.append({"kind": "move", "source": source_slot,
                                      "destination": stage_slot, "expected_source": item})
                        state[stage_slot], state[source_slot] = item, None
                        positions[item] = stage_slot
                        cursor = stage_slot
                    moves.append({"kind": "group", "width": 6, "height": 1,
                                  "source_slots": stage, "destination_slots": ends,
                                  "expected_pokemon": ids})
                    for stage_slot, end in zip(stage, ends):
                        item = state[stage_slot]
                        state[end], state[stage_slot] = item, None
                        positions[item] = end
                    cursor = ends[-1]
        for end, item in enumerate(target):
            if item is not None and state[end] != item:
                swap(positions[item], end)
    elif strategy == "destination_first":
        for end, item in enumerate(target):
            if item is not None and state[end] != item:
                swap(positions[item], end)
    elif strategy == "nearest_swap":
        # Choose the next legal destination swap by the cursor travel it costs
        # now. This preserves minimum swap counts within each permutation cycle
        # while avoiding the arbitrary low-to-high destination order.
        pending = {end for end, item in enumerate(target)
                   if item is not None and state[end] != item}
        while pending:
            end = min(pending, key=lambda destination: (
                model.route_travel_ms(cursor, positions[target[destination]])
                + model.route_travel_ms(positions[target[destination]], destination), destination))
            start = positions[target[end]]
            swap(start, end)
            pending.discard(end)
            if target[start] is not None and state[start] != target[start]:
                pending.add(start)
            else:
                pending.discard(start)
    elif strategy == "lookahead2":
        # Search a bounded set of near-term swaps. Only independent pairs are
        # considered as follow-ups, so their preconditions remain unchanged.
        pending = {end for end, item in enumerate(target)
                   if item is not None and state[end] != item}
        while pending:
            def score(end, from_cursor):
                start = positions[target[end]]
                return (model.route_travel_ms(from_cursor, start)
                        + model.route_travel_ms(start, end), end, start)

            shortlist = heapq.nsmallest(8, pending, key=lambda end: score(end, cursor))
            best = None
            for end in shortlist:
                first_cost, _, start = score(end, cursor)
                followup = 0
                for next_end in shortlist:
                    if next_end == end:
                        continue
                    next_start = positions[target[next_end]]
                    if {start, end} & {next_start, next_end}:
                        continue
                    candidate = (model.route_travel_ms(end, next_start)
                                 + model.route_travel_ms(next_start, next_end))
                    if not followup or candidate < followup:
                        followup = candidate
                choice = (first_cost + followup, first_cost, end, start)
                if best is None or choice < best:
                    best = choice
            _, _, end, start = best
            swap(start, end)
            pending.discard(end)
            if target[start] is not None and state[start] != target[start]:
                pending.add(start)
            else:
                pending.discard(start)
    elif strategy == "cycle_local":
        pending = {item for item in positions if positions[item] != destinations[item]}
        while pending:
            item = min(pending, key=lambda p: (
                model.route_travel_ms(cursor, positions[p])
                + model.route_travel_ms(positions[p], destinations[p]), destinations[p]))
            # Follow the displaced item without returning to the next target slot.
            while item is not None and item in pending:
                start, end = positions[item], destinations[item]
                displaced = state[end]
                swap(start, end)
                pending.remove(item)
                if displaced is not None and positions[displaced] == destinations[displaced]:
                    pending.discard(displaced)
                item = displaced
    else:
        raise ValueError(f"Unknown strategy: {strategy}")
    if state != target:
        raise ValueError("Planner failed to produce the requested layout.")
    return moves


def rectangle(indices, width, height, size):
    if type(width) is not int or type(height) is not int or width < 1 or height < 1 or len(indices) != width * height:
        raise ValueError("Invalid group rectangle dimensions.")
    if len(indices) != len(set(indices)) or any(type(i) is not int or not 0 <= i < size for i in indices):
        raise ValueError("Invalid group rectangle slots.")
    top_left = indices[0]
    row, col, box = top_left // 6 % 5, top_left % 6, top_left // 30
    expected = [box * 30 + (row + y) * 6 + col + x
                for y in range(height) for x in range(width)]
    if col + width > 6 or row + height > 5 or indices != expected:
        raise ValueError("Group selection must be a non-wrapping rectangle within one box.")
    return expected


def simulate(source, target, moves, start_cursor, model):
    """Independently replay preconditions and verify the complete final layout."""
    validate_layout(target, [p for p in source if p is not None], len(source))
    if type(start_cursor) is not int or not 0 <= start_cursor < len(source):
        raise ValueError("Start cursor is outside the inventory.")
    state = source.copy()
    cursor = start_cursor
    boxes = directions = groups = selection_steps = page_jumps = overview_steps = overview_confirms = 0
    for move in moves:
        if move.get("kind") == "group":
            width, height = move["width"], move["height"]
            starts = rectangle(move["source_slots"], width, height, len(state))
            ends = rectangle(move["destination_slots"], width, height, len(state))
            if len({i // 30 for i in starts}) != 1 or len({i // 30 for i in ends}) != 1:
                raise ValueError("Group source and destination must each be inside one box.")
            if [state[i] for i in starts] != move["expected_pokemon"] or any(state[i] is not None for i in ends):
                raise ValueError("Group move precondition failed: source changed or destination is occupied.")
            anchor = starts[0]
            mode, first, direction, confirms = model.navigation_route(cursor, anchor)
            if mode == "overview":
                page_jumps += first
                overview_steps += direction
                overview_confirms += confirms
            else:
                boxes += first
                directions += direction
            pages, grid = model.group_navigation(anchor, ends[0])
            page_jumps += pages
            overview_steps += grid
            # Open Box Spaces, page/grid navigation, select target box, then drop.
            selection_steps += (move["width"] - 1) + (move["height"] - 1)
            for start, end in zip(starts, ends):
                state[end] = state[start]
                state[start] = None
            cursor = ends[-1]
            groups += 1
            continue
        start, end = move["source"], move["destination"]
        if any(type(i) is not int or not 0 <= i < len(state) for i in (start, end)) or start == end:
            raise ValueError("Invalid move coordinates.")
        if state[start] is None or state[start] != move["expected_source"]:
            raise ValueError("Move precondition does not match simulated state.")
        if move.get("kind") == "move":
            if state[end] is not None:
                raise ValueError("Staging move destination is occupied.")
        elif state[end] != move["expected_destination"]:
            raise ValueError("Swap destination changed.")
        for route_start, route_end in ((cursor, start), (start, end)):
            mode, first, direction, confirms = model.navigation_route(route_start, route_end)
            if mode == "overview":
                page_jumps += first
                overview_steps += direction
                overview_confirms += confirms
            else:
                boxes += first
                directions += direction
        state[start], state[end] = state[end], state[start]
        cursor = end
    if state != target or Counter(state) != Counter(source):
        raise ValueError("Simulation did not preserve inventory and reach the target.")
    swaps = sum(move.get("kind") not in ("group", "move") for move in moves)
    singles = sum(move.get("kind") == "move" for move in moves)
    # Group costs include mode entry, three selection/ pickup confirmations,
    # placement, and two up inputs to reach Box Spaces from the top row.
    buttons = 2 * (swaps + singles) + 7 * groups + page_jumps + overview_confirms + selection_steps
    return {"swaps": swaps, "single_moves": singles, "groups": groups, "box_transitions": boxes,
            "overview_page_changes": page_jumps, "overview_cursor_presses": overview_steps,
            "overview_confirms": overview_confirms,
            "cursor_presses": directions,
            "selection_steps": selection_steps,
            "controller_actions": boxes + directions + overview_steps + buttons,
            "estimated_seconds": round(((boxes + buttons) * (320 + model.game_delay_ms)
                                        + (directions + overview_steps) * (80 + model.game_delay_ms)) / 1000, 3)}


def compare(inventory, target, start_cursor=0, model=None):
    source = validate_inventory(inventory)
    validate_layout(target, inventory["pokemon"], len(source))
    model = model or CostModel()
    if type(start_cursor) is not int or not 0 <= start_cursor < len(source):
        raise ValueError("Start cursor is outside the inventory.")
    candidates = {}
    # lookahead2 remains available as an experiment, but it loses on measured
    # simulator cases and is too costly to evaluate during normal planning.
    for strategy in ("destination_first", "cycle_local", "nearest_swap", "batch_first"):
        moves = make_plan(source, target, strategy, start_cursor, model)
        candidates[strategy] = {"moves": moves, "metrics": simulate(source, target, moves, start_cursor, model)}
    winner = min(candidates, key=lambda key: candidates[key]["metrics"]["estimated_seconds"])
    result = {"version": 1, "inventory": inventory, "target": target,
              "start_cursor": start_cursor, "cost_model": asdict(model), "strategy": winner,
              "comparison": {key: value["metrics"] for key, value in candidates.items()},
              "moves": candidates[winner]["moves"]}
    if "scan_context" in inventory:
        result["source_validation"] = source_validation(inventory)
    return result


def benchmark(boxes=150, seed=1):
    if boxes < 1:
        raise ValueError("Box count must be positive.")
    rng = random.Random(seed)
    reports = []
    for occupancy in (0.5, 0.9, 1.0):
        count = int(boxes * 30 * occupancy)
        target = [f"pokemon-{i:05d}" for i in range(count)] + [None] * (boxes * 30 - count)
        source = target[:count].copy()
        rng.shuffle(source)
        source += [None] * (boxes * 30 - count)
        inventory = {"version": 1, "slots": source,
                     "pokemon": {p: {"name": "synthetic", "dex": i % 1025 + 1}
                                 for i, p in enumerate(target) if p is not None}}
        plan = compare(inventory, target)
        reports.append({"boxes": boxes, "occupancy": occupancy, "seed": seed,
                        "strategy": plan["strategy"], "comparison": plan["comparison"]})
    return reports


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    imp = commands.add_parser("import", help="Convert a sorter JSON export to an editable inventory")
    imp.add_argument("export")
    imp.add_argument("output")
    imp.add_argument("--scan-context", help="Physical scan sidecar; autodetected beside the source export")
    plan = commands.add_parser("plan", help="Plan and verify moves without controlling a console")
    plan.add_argument("inventory")
    plan.add_argument("output")
    targets = plan.add_mutually_exclusive_group()
    targets.add_argument("--target", help="Edited inventory: only the slots array defines the target")
    targets.add_argument("--target-export", help="Existing sorter -sorted.json or edited legacy export")
    plan.add_argument("--start-cursor", type=int, default=0, help="Zero-based slot; default 0")
    plan.add_argument("--game-delay-ms", type=float, default=240)
    sim = commands.add_parser("simulate", help="Independently replay a saved plan")
    sim.add_argument("plan")
    verify = commands.add_parser("verify-source", help="Check a saved plan against a fresh scan; performs no console actions")
    verify.add_argument("plan")
    verify.add_argument("fresh_export")
    verify.add_argument("--scan-context", help="Fresh scan sidecar; autodetected beside its export")
    bench = commands.add_parser("benchmark", help="Compare deterministic synthetic layouts")
    bench.add_argument("--boxes", type=int, default=200)
    bench.add_argument("--seed", type=int, default=1)
    args = parser.parse_args()
    try:
        if args.command == "import":
            write(args.output, import_inventory(read(args.export), load_scan_context(args.export, args.scan_context)))
        elif args.command == "plan":
            inventory = read(args.inventory)
            source = validate_inventory(inventory)
            if args.target:
                edited = read(args.target)
                validate_inventory(edited)
                if edited["pokemon"] != inventory["pokemon"]:
                    raise ValueError("Edit target slots only; Pokemon metadata must remain unchanged.")
                target = edited["slots"]
            elif args.target_export:
                target = target_from_export(inventory, read(args.target_export))
            else:
                catalog = inventory["pokemon"]
                if any(type(p.get("dex")) is not int for p in catalog.values()):
                    raise ValueError("Default dex sorting requires integer dex values.")
                target = sorted((p for p in source if p is not None), key=lambda p: catalog[p]["dex"])
                target += [None] * (len(source) - len(target))
            result = compare(inventory, target, args.start_cursor, CostModel(args.game_delay_ms))
            write(args.output, result)
            print(json.dumps({"strategy": result["strategy"], "comparison": result["comparison"]}, indent=2))
        elif args.command == "verify-source":
            context = load_scan_context(args.fresh_export, args.scan_context, required=True)
            print(json.dumps(verify_saved_source(read(args.plan), read(args.fresh_export), context), indent=2))
        elif args.command == "simulate":
            result = read(args.plan)
            if result["version"] != 1:
                raise ValueError("Unsupported plan version.")
            source = validate_inventory(result["inventory"])
            print(json.dumps(simulate(source, result["target"], result["moves"],
                                      result["start_cursor"], CostModel(**result["cost_model"])), indent=2))
        else:
            print(json.dumps(benchmark(args.boxes, args.seed), indent=2))
    except (ValueError, OSError, KeyError, TypeError) as error:
        parser.exit(2, f"Error: {error}\n")


if __name__ == "__main__":
    main()
