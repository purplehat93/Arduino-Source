import copy
import json
from pathlib import Path
import random
import subprocess
import sys
import tempfile
import unittest

from planner import (CostModel, compare, import_inventory, legacy_slots,
                     make_plan, simulate, target_from_export, validate_inventory)


def export(items):
    return [dict(index=i, box=i // 30, row=i // 6 % 5, column=i % 6,
                 **({"name": f"species-{item}", "dex": item} if item is not None else {}))
            for i, item in enumerate(items)]


class PlannerTests(unittest.TestCase):
    def test_cli_roundtrip_and_overwrite_protection(self):
        script = str(Path(__file__).with_name("planner.py"))
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "boxes.json"
            inventory = root / "inventory.json"
            plan = root / "plan.json"
            source.write_text(json.dumps(export([2, 1] + [None] * 28)), encoding="utf-8-sig")
            for args in (("import", source, inventory), ("plan", inventory, plan), ("simulate", plan)):
                result = subprocess.run([sys.executable, script, *map(str, args)],
                                        capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
            original = plan.read_bytes()
            result = subprocess.run([sys.executable, script, "plan", str(inventory), str(plan)],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 2)
            self.assertEqual(plan.read_bytes(), original)

    def test_random_permutations_and_holes(self):
        rng = random.Random(528)
        for count in (0, 1, 15, 29, 30, 45, 60):
            for _ in range(20):
                inventory = import_inventory(export(list(range(count)) + [None] * (60 - count)))
                rng.shuffle(inventory["slots"])
                target = inventory["slots"].copy()
                rng.shuffle(target)
                before = copy.deepcopy(inventory)
                plan = compare(inventory, target, start_cursor=37)
                metrics = simulate(inventory["slots"], target, plan["moves"], 37, CostModel())
                self.assertEqual(metrics, plan["comparison"][plan["strategy"]])
                self.assertLessEqual(metrics["estimated_seconds"],
                                     plan["comparison"]["destination_first"]["estimated_seconds"])
                self.assertEqual(plan["comparison"]["nearest_swap"]["swaps"],
                                 plan["comparison"]["destination_first"]["swaps"])
                self.assertEqual(inventory, before)

    def test_already_sorted(self):
        inventory = import_inventory(export([1] * 30))
        self.assertEqual(compare(inventory, inventory["slots"])["moves"], [])

    def test_duplicates_preserve_correct_slots(self):
        inventory = import_inventory(export([1, 2, 1] + [None] * 27))
        target = target_from_export(inventory, export([1, 1, 2] + [None] * 27))
        self.assertEqual(target[:3], ["pokemon-00000", "pokemon-00002", "pokemon-00001"])
        compare(inventory, target)

    def test_reject_missing_duplicate_unknown_ids(self):
        inventory = import_inventory(export([1, 2] + [None] * 28))
        for replacement in (None, "pokemon-00000", "unknown"):
            target = inventory["slots"].copy()
            target[1] = replacement
            with self.assertRaises(ValueError):
                compare(inventory, target)

    def test_reject_modified_export(self):
        inventory = import_inventory(export([1, 2] + [None] * 28))
        for items in ([1, 3], [1, None], [1, 1]):
            with self.assertRaises(ValueError):
                target_from_export(inventory, export(items + [None] * 28))

    def test_export_coordinates_and_order(self):
        rows = export([1] + [None] * 29)
        self.assertEqual(legacy_slots(rows), legacy_slots(list(reversed(rows))))
        for field, value in (("index", 1), ("box", 1), ("row", True)):
            broken = copy.deepcopy(rows)
            broken[0][field] = value
            with self.assertRaises(ValueError):
                legacy_slots(broken)
        with self.assertRaises(ValueError):
            legacy_slots(rows[:-1])

    def test_tampered_and_truncated_plan(self):
        inventory = import_inventory(export([2, 1] + [None] * 28))
        target = inventory["slots"].copy()
        target[0], target[1] = target[1], target[0]
        plan = compare(inventory, target)
        with self.assertRaises(ValueError):
            simulate(inventory["slots"], target, [], 0, CostModel())
        plan["moves"][0]["expected_source"] = "wrong"
        with self.assertRaises(ValueError):
            simulate(inventory["slots"], target, plan["moves"], 0, CostModel())

    def test_cost_matches_navigation(self):
        model = CostModel(100)
        self.assertEqual(model.navigation(0, 59), (1, 4))
        self.assertEqual(model.travel_ms(0, 59), 420 + 4 * 180)
        self.assertEqual(model.group_navigation(3510, 4410), (1, 0))
        self.assertEqual(CostModel().game_delay_ms, 240)
        self.assertEqual(model.navigation_route(0, 3000)[0], "overview")
        self.assertEqual(model.navigation_route(0, 30)[0], "linear")
        for delay in (-1, float("nan"), float("inf")):
            with self.assertRaises(ValueError):
                CostModel(delay)

    def test_simulator_uses_overview_for_both_swap_legs(self):
        source = [None] * 3030
        source[0], source[3000] = "a", "b"
        target = source.copy()
        target[0], target[3000] = "b", "a"
        move = {"source": 3000, "destination": 0,
                "expected_source": "b", "expected_destination": "a"}
        metrics = simulate(source, target, [move], 0, CostModel())
        self.assertEqual(metrics["overview_page_changes"], 6)
        self.assertEqual(metrics["overview_confirms"], 4)
        self.assertEqual(metrics["box_transitions"], 0)

    def test_rectangular_group_transfer_and_preconditions(self):
        source = [None] * 4440
        source[3510:3512] = ["a", "b"]
        target = [None] * 4440
        target[4410:4412] = ["a", "b"]
        move = {"kind": "group", "width": 2, "height": 1,
                "source_slots": [3510, 3511], "destination_slots": [4410, 4411],
                "expected_pokemon": ["a", "b"]}
        metrics = simulate(source, target, [move], 3510, CostModel())
        self.assertEqual((metrics["groups"], metrics["overview_page_changes"]), (1, 1))
        occupied = source.copy()
        occupied[30] = "blocker"
        with self.assertRaises(ValueError):
            simulate(occupied, target, [move], 3510, CostModel())
        malformed = dict(move, destination_slots=[35, 36])
        with self.assertRaises(ValueError):
            simulate(source, target, [malformed], 3510, CostModel())

    def test_batch_planner_stages_then_carries_full_row(self):
        source = ["p5", "p4", "p3", "p2", "p1", "p0"] + [None] * 84
        target = ["p0", "p1", "p2", "p3", "p4", "p5"] + [None] * 84
        moves = make_plan(source, target, "batch_first", 0, CostModel())
        self.assertEqual(sum(move.get("kind") == "group" for move in moves), 1)
        self.assertEqual(simulate(source, target, moves, 0, CostModel())["groups"], 1)

    def test_lookahead_experiment_replays_but_is_not_auto_selected(self):
        inventory = import_inventory(export(list(range(60)) + [None] * 30))
        target = inventory["slots"].copy()
        source = target.copy()
        random.Random(91).shuffle(source)
        baseline = make_plan(source, target, "destination_first", 0, CostModel())
        experimental = make_plan(source, target, "lookahead2", 0, CostModel())
        baseline_metrics = simulate(source, target, baseline, 0, CostModel())
        experimental_metrics = simulate(source, target, experimental, 0, CostModel())
        self.assertEqual(experimental_metrics["swaps"], baseline_metrics["swaps"])
        self.assertNotIn("lookahead2", compare(inventory | {"slots": source}, target)["comparison"])

    def test_inventory_and_cursor_validation(self):
        inventory = import_inventory(export([1] + [None] * 29))
        for cursor in (-1, 30, True):
            with self.assertRaises(ValueError):
                compare(inventory, inventory["slots"], cursor)
        inventory["slots"][1] = inventory["slots"][0]
        with self.assertRaises(ValueError):
            validate_inventory(inventory)

class SavedSourceTests(unittest.TestCase):
    def context(self, count=1, first=31, scan_id='job-a'):
        return dict(version=1, scan_id=scan_id, physical_start_box=first,
                    physical_end_box=first+count-1, total_boxes=200, box_count=count,
                    coordinate_basis='zero_based_relative_to_physical_start_box',
                    box_identity='physical_counter_not_editable_label',
                    source_export='boxes.json')

    def fixture(self):
        from planner import verify_saved_source
        rows = export([2, 1] + [None] * 28)
        context = self.context()
        inventory = import_inventory(rows, context)
        return rows, context, compare(inventory, sorted(inventory['slots'][:2], reverse=True) + [None] * 28)

    def test_fresh_scan_id_and_file_names_do_not_change_identity(self):
        from planner import verify_saved_source
        rows, context, plan = self.fixture()
        context['scan_id'] = 'fresh-job'
        context['source_export'] = 'fresh.json'
        context['box_label'] = 'renamed label'
        self.assertEqual(verify_saved_source(plan, rows, context)['result'], 'source_matches')

    def test_changed_position_content_and_empty_slot_are_rejected(self):
        from planner import verify_saved_source
        rows, context, plan = self.fixture()
        for changed in (export([1, 2] + [None] * 28), export([2, 3] + [None] * 28),
                        export([2, None, 1] + [None] * 27)):
            with self.subTest(changed=changed[:3]), self.assertRaisesRegex(ValueError, 'layout or observed metadata changed'):
                verify_saved_source(plan, changed, context)

    def test_physical_range_change_is_rejected_even_with_same_contents(self):
        from planner import verify_saved_source
        rows, context, plan = self.fixture()
        with self.assertRaisesRegex(ValueError, 'different physical box range'):
            verify_saved_source(plan, rows, self.context(first=32))

    def test_source_edits_and_unindexed_plans_fail_closed(self):
        from planner import verify_saved_source
        rows, context, plan = self.fixture()
        changed = copy.deepcopy(plan)
        changed['inventory']['slots'][:2] = reversed(changed['inventory']['slots'][:2])
        with self.assertRaisesRegex(ValueError, 'recorded source was changed'):
            verify_saved_source(changed, rows, context)
        legacy = compare(import_inventory(rows), plan['target'])
        with self.assertRaisesRegex(ValueError, 'physical scan context'):
            verify_saved_source(legacy, rows, context)

    def test_invalid_range_metadata_is_rejected(self):
        rows = export([1] + [None] * 29)
        for patch in ({'physical_start_box': 0}, {'physical_end_box': 32}, {'box_count': True},
                      {'total_boxes': 301}, {'coordinate_basis': 'label'}, {'scan_id': ''}):
            with self.subTest(patch=patch), self.assertRaises(ValueError):
                import_inventory(rows, dict(self.context(), **patch))

    def test_sidecar_cannot_be_attached_to_wrong_export(self):
        from planner import load_scan_context
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / 'different.json'
            sidecar = root / 'different-scan.json'
            sidecar.write_text(json.dumps(self.context()))
            with self.assertRaisesRegex(ValueError, 'different source export'):
                load_scan_context(path)
            with self.assertRaisesRegex(ValueError, 'physical scan context'):
                load_scan_context(root / 'missing.json', required=True)

    def test_cli_import_plan_and_fresh_verification(self):
        script = str(Path(__file__).with_name('planner.py'))
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            rows, context, _ = self.fixture()
            (root / 'boxes.json').write_text(json.dumps(rows))
            (root / 'boxes-scan.json').write_text(json.dumps(context))
            (root / 'fresh.json').write_text(json.dumps(rows))
            fresh = dict(context, scan_id='job-b', source_export='fresh.json')
            (root / 'fresh-scan.json').write_text(json.dumps(fresh))
            for args in [('import', 'boxes.json', 'inventory.json'),
                         ('plan', 'inventory.json', 'plan.json'),
                         ('verify-source', 'plan.json', 'fresh.json')]:
                result = subprocess.run([sys.executable, script, *args], cwd=root,
                                        capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
            self.assertFalse(json.loads(result.stdout)['console_moves_performed'])
            (root / 'fresh.json').write_text(json.dumps(export([1, 2] + [None] * 28)))
            result = subprocess.run([sys.executable, script, 'verify-source', 'plan.json', 'fresh.json'],
                                    cwd=root, capture_output=True, text=True)
            self.assertEqual(result.returncode, 2)


if __name__ == "__main__":
    unittest.main()
