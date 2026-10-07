import csv
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from collections import Counter

import collection


def workspace(size=60, count=6):
    records = []
    for index in range(size):
        record = {"index": index, "box": index // 30, "row": index // 6 % 5, "column": index % 6}
        examples = {0: (1, False), 1: (1, False), 2: (2, False), 3: (2, False), 30: (1, True), 31: (2, True)}
        if size == 60 and index in examples:
            dex, shiny = examples[index]
            record.update(name=f"species-{dex}", dex=dex, shiny=shiny, gender="Male")
        elif size != 60 and index < count:
            record.update(name="species-1", dex=1, shiny=False, gender="Male")
        records.append(record)
    context = {"version": 1, "scan_id": "synthetic", "physical_start_box": 31 if size == 60 else 1,
               "physical_end_box": 32 if size == 60 else size // 30, "total_boxes": 200,
               "box_count": size // 30, "coordinate_basis": "zero_based_relative_to_physical_start_box",
               "box_identity": "physical_counter_not_editable_label"}
    result = collection.make_workspace(collection.planner.import_inventory(records, context))
    for item, annotation in result["annotations"].items():
        annotation.update(form_key=f"form-{result['source_inventory']['pokemon'][item]['dex']}", identity_reviewed=True)
    return result


def catalog():
    return {"version": 1, "scope": "partial", "forms": [
        {"key": "form-1", "dex": 1, "gender": None, "final_evolution": False, "shiny_available": True},
        {"key": "form-2", "dex": 2, "gender": None, "final_evolution": True, "shiny_available": True}]}


def profiles():
    return {"version": 1, "collections": [
        {"id": "living", "kind": "living_form", "start_box": 31, "start_slot": 10},
        {"id": "shiny", "kind": "shiny_living_form", "start_box": 32, "start_slot": 10},
        {"id": "final", "kind": "final_form", "start_box": 31, "start_slot": 20}]}


class CollectionTests(unittest.TestCase):
    def test_merge_adjacent_chunks_preserves_occurrences_and_annotations(self):
        first, second = workspace(30, 2), workspace(30, 1)
        first_context = first["source_inventory"]["scan_context"]
        second_context = second["source_inventory"]["scan_context"]
        first_context.update(physical_start_box=10, physical_end_box=10, box_count=1)
        second_context.update(physical_start_box=11, physical_end_box=11, box_count=1)
        second["annotations"]["pokemon-00000"]["note"] = "Keep this individual."
        merged = collection.merge_scan_workspaces([second, first])
        inventory = merged["source_inventory"]
        self.assertEqual((inventory["scan_context"]["physical_start_box"],
                          inventory["scan_context"]["physical_end_box"]), (10, 11))
        self.assertEqual(len(inventory["slots"]), 60)
        self.assertEqual(inventory["slots"][30], "pokemon-00030")
        self.assertEqual(merged["annotations"]["pokemon-00030"]["note"], "Keep this individual.")
        self.assertEqual(sum(item is not None for item in inventory["slots"]), 3)

    def test_merge_rejects_overlapping_or_gapped_ranges(self):
        first, second = workspace(30, 1), workspace(30, 1)
        first["source_inventory"]["scan_context"].update(physical_start_box=10, physical_end_box=10, box_count=1)
        second["source_inventory"]["scan_context"].update(physical_start_box=12, physical_end_box=12, box_count=1)
        with self.assertRaisesRegex(ValueError, "gap"):
            collection.merge_scan_workspaces([first, second])
        second["source_inventory"]["scan_context"].update(physical_start_box=10, physical_end_box=10, box_count=1)
        with self.assertRaisesRegex(ValueError, "overlap"):
            collection.merge_scan_workspaces([first, second])

    def test_merge_scan_files_verifies_raw_sources_and_writes_reimportable_result(self):
        with tempfile.TemporaryDirectory() as directory:
            paths = []
            for box in (10, 11):
                chunk = workspace(30, 2)
                context = chunk["source_inventory"]["scan_context"]
                context.update(physical_start_box=box, physical_end_box=box, box_count=1,
                               scan_id=f"chunk-{box}", source_export=f"chunk-{box}.json")
                rows = []
                for index, item in enumerate(chunk["source_inventory"]["slots"]):
                    row = {"index": index, "box": 0, "row": index // 6 % 5, "column": index % 6}
                    if item is not None:
                        row.update(chunk["source_inventory"]["pokemon"][item])
                    rows.append(row)
                source = Path(directory) / f"chunk-{box}.json"
                collection.planner.write(source, rows)
                collection.planner.write(Path(directory) / f"chunk-{box}-scan.json", context)
                workspace_path = Path(directory) / f"chunk-{box}-workspace.json"
                collection.planner.write(workspace_path, chunk)
                paths.append(workspace_path)

            output = Path(directory) / "merged"
            merged_path = collection.merge_scan_files(paths, output)
            merged = collection.planner.read(merged_path)
            raw_path = Path(directory) / "merged.json"
            scan = collection.planner.load_scan_context(raw_path, required=True)
            imported = collection.planner.import_inventory(collection.planner.read(raw_path), scan)
            self.assertEqual(collection.planner.source_validation(imported),
                             collection.planner.source_validation(merged["source_inventory"]))
            self.assertEqual(len(merged["source_inventory"]["slots"]), 60)
            with self.assertRaisesRegex(ValueError, "already exists"):
                collection.merge_scan_files(paths, output)

    def test_unknown_scan_is_review_only(self):
        original = workspace()
        value = collection.make_workspace(original["source_inventory"])
        report = collection.review(value, catalog(), profiles())
        self.assertEqual(report["counts"]["unresolved"], 6)
        self.assertEqual(report["counts"]["review"], 6)
        self.assertFalse(report["release_enabled"])
        self.assertEqual(report["release_candidates"], [])
        self.assertTrue(all(item["action"] == "keep_for_review" for item in report["extras"]))
        self.assertTrue(all(annotation["ivs"] is None for annotation in value["annotations"].values()))
        with self.assertRaisesRegex(ValueError, "identities"):
            collection.move_plan(value, catalog(), profiles())

    def test_multiple_collections_and_all_extras_preserved(self):
        value = workspace()
        value["annotations"]["pokemon-00001"]["ivs"] = dict.fromkeys(
            ("hp", "attack", "defense", "special_attack", "special_defense", "speed"), 31)
        report = collection.review(value, catalog(), profiles())
        self.assertEqual(report["counts"], {"scanned": 6, "assigned": 5, "review": 1, "gaps": 0, "unresolved": 0})
        self.assertEqual(report["extras"][0]["occurrence"], "pokemon-00001")
        self.assertEqual(report["extras"][0]["annotation"]["ivs"]["speed"], 31)
        target = collection.target_layout(value, report)
        self.assertEqual(target[1], "pokemon-00001")
        self.assertEqual(Counter(target), Counter(value["source_inventory"]["slots"]))
        self.assertEqual(report["assignments"][2]["physical_box"], 32)
        self.assertEqual(report["assignments"][2]["slot"], 10)

    def test_explicit_preference_selects_individual_copy(self):
        definitions = profiles()
        definitions["collections"][0]["preferred"] = {"form-1": "pokemon-00001"}
        report = collection.review(workspace(), catalog(), definitions)
        self.assertEqual(report["assignments"][0]["occurrence"], "pokemon-00001")
        self.assertEqual(report["extras"][0]["occurrence"], "pokemon-00000")
        definitions["collections"][0]["preferred"] = {"form-1": "pokemon-00030"}
        with self.assertRaisesRegex(ValueError, "Preferred"):
            collection.review(workspace(), catalog(), definitions)

    def test_preserve_already_correct_copy(self):
        definitions = {"version": 1, "collections": [{"id": "one", "kind": "custom", "start_box": 31,
                                                       "start_slot": 2, "form_keys": ["form-1"]}]}
        self.assertEqual(collection.review(workspace(), catalog(), definitions)["assignments"][0]["occurrence"], "pokemon-00001")

    def test_missing_entry_reserves_gap(self):
        definitions = {"version": 1, "collections": [{"id": "living", "kind": "living_form", "start_box": 1}]}
        value = workspace(30, 3)
        report = collection.review(value, catalog(), definitions)
        self.assertEqual(len(report["gaps"]), 1)
        target = collection.target_layout(value, report)
        self.assertIsNone(target[1])
        self.assertEqual(Counter(target), Counter(value["source_inventory"]["slots"]))

    def test_storage_shortage_never_discards_review_pokemon(self):
        definitions = {"version": 1, "collections": [{"id": "living", "kind": "living_form", "start_box": 1}]}
        value = workspace(30, 30)
        report = collection.review(value, catalog(), definitions)
        self.assertEqual(report["counts"]["review"], 29)
        with self.assertRaisesRegex(ValueError, "storage"):
            collection.target_layout(value, report)
        self.assertEqual(len(value["source_inventory"]["pokemon"]), 30)

    def test_bad_ranges_and_overlaps_rejected(self):
        for box in (30, 33, True):
            definitions = profiles()
            definitions["collections"][0]["start_box"] = box
            with self.assertRaises(ValueError): collection.review(workspace(), catalog(), definitions)
        definitions = profiles()
        definitions["collections"][1]["start_box"] = 31
        with self.assertRaisesRegex(ValueError, "overlap"):
            collection.review(workspace(), catalog(), definitions)

    def test_species_and_gender_assignment_checked(self):
        value = workspace()
        value["annotations"]["pokemon-00000"]["form_key"] = "form-2"
        with self.assertRaisesRegex(ValueError, "species"):
            collection.review(value, catalog(), profiles())
        forms = catalog()
        forms["forms"][0]["gender"] = "Female"
        with self.assertRaisesRegex(ValueError, "gender"):
            collection.review(workspace(), forms, profiles())

    def test_unknown_evolution_or_shiny_availability_not_guessed(self):
        for field in ("final_evolution", "shiny_available"):
            forms = catalog()
            forms["forms"][0][field] = None
            with self.assertRaises(ValueError): collection.review(workspace(), forms, profiles())

    def test_release_policy_cannot_be_enabled(self):
        value = workspace()
        value["policy"]["release_enabled"] = True
        with self.assertRaisesRegex(ValueError, "cannot release"):
            collection.review(value, catalog(), profiles())

    def test_invalid_schema_and_catalog_contradictions(self):
        value = workspace()
        value["version"] = True
        with self.assertRaises(ValueError): collection.review(value, catalog(), profiles())
        forms = catalog()
        forms["forms"][0]["shiny_available"] = False
        with self.assertRaisesRegex(ValueError, "conflicts"):
            collection.review(workspace(), forms, profiles())

    def test_iv_annotations_validated_and_not_in_source_fingerprint(self):
        value = workspace()
        before = collection.planner.source_validation(value["source_inventory"])
        value["annotations"]["pokemon-00000"]["ivs"] = dict.fromkeys(
            ("hp", "attack", "defense", "special_attack", "special_defense", "speed"), None)
        collection.validate_workspace(value)
        self.assertEqual(before, collection.planner.source_validation(value["source_inventory"]))
        for bad in (32, -1, True, "31"):
            value["annotations"]["pokemon-00000"]["ivs"]["speed"] = bad
            with self.assertRaises(ValueError): collection.validate_workspace(value)

    def test_move_plan_replays_to_exact_lossless_target(self):
        value = workspace()
        plan = collection.move_plan(value, catalog(), profiles())
        self.assertTrue(plan["execution_blocked"])
        self.assertTrue(all(move.get("kind") != "group" for move in plan["moves"]))
        collection.planner.simulate(plan["inventory"]["slots"], plan["target"], plan["moves"], 0,
                                    collection.planner.CostModel(**plan["cost_model"]))
        self.assertEqual(Counter(plan["target"]), Counter(value["source_inventory"]["slots"]))
        self.assertEqual(plan["source_validation"], collection.planner.source_validation(value["source_inventory"]))

    def test_4900_occurrences_remain_distinct(self):
        value = workspace(6000, 4900)
        definitions = {"version": 1, "collections": [{"id": "one", "kind": "custom", "start_box": 1,
                                                       "form_keys": ["form-1"]}]}
        report = collection.review(value, catalog(), definitions)
        self.assertEqual(report["counts"]["review"], 4899)
        self.assertEqual(Counter(collection.target_layout(value, report)), Counter(value["source_inventory"]["slots"]))

    def test_cli_report_roundtrip_and_overwrite_refusal(self):
        with tempfile.TemporaryDirectory() as directory:
            paths = [Path(directory) / name for name in ("workspace.json", "catalog.json", "profiles.json", "review.json")]
            for path, data in zip(paths, (workspace(), catalog(), profiles())):
                path.write_text(json.dumps(data))
            command = [sys.executable, str(Path(collection.__file__)), "review", *map(str, paths)]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(json.loads(paths[3].read_text())["counts"]["scanned"], 6)
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)

    def test_csv_keeps_all_copies_and_unknown_ivs_blank(self):
        value = workspace()
        value["annotations"]["pokemon-00001"]["note"] = "=formula-like user note"
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "review.csv"
            collection.write_review_csv(value, collection.review(value, catalog(), profiles()), path)
            with path.open(encoding="utf-8-sig", newline="") as stream:
                rows = list(csv.DictReader(stream))
            self.assertEqual(len(rows), 6)
            self.assertEqual(len({row["occurrence"] for row in rows}), 6)
            self.assertTrue(all(row["iv_speed"] == "" for row in rows))
            extra = next(row for row in rows if row["action"] == "keep_for_review")
            self.assertEqual(extra["note"], "'=formula-like user note")


if __name__ == "__main__":
    unittest.main()
