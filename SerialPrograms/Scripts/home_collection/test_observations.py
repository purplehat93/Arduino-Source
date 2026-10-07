import unittest
import csv
import tempfile
from pathlib import Path
import collection
import observations
from test_collection import workspace


def evidence():
    ratings = ["Best", "No Good", "Fantastic", "Decent", "Hyper trained!", "Pretty Good"]
    stats = {}
    for stat, rating in zip(observations.STATS, ratings):
        lo, hi, trained = observations.RATINGS[rating]
        stats[stat] = dict(status="read", rating=rating, iv_min=lo, iv_max=hi,
                           hyper_trained=trained, raw=rating, raw_confirmation=rating)
    judge = dict(status="read", stats=stats)
    return dict(version=1, form=dict(key="species-1", status="candidate_requires_review"),
                judge=judge, ivs=observations.validate_judge(judge))


class ObservationTests(unittest.TestCase):
    def test_ranges_do_not_become_exact_and_hyper_training_not_31(self):
        values = evidence()["ivs"]
        self.assertEqual(values, dict(hp=31, attack=0, defense=30,
                                     special_attack=None, special_defense=None, speed=None))

    def test_import_retains_evidence_and_occurrence_without_approving_form(self):
        inventory = workspace()["source_inventory"]
        inventory["pokemon"]["pokemon-00000"]["collection_observation"] = evidence()
        value = collection.make_workspace(inventory)
        annotation = value["annotations"]["pokemon-00000"]
        self.assertEqual(annotation["form_key"], "species-1")
        self.assertFalse(annotation["identity_reviewed"])
        self.assertEqual(annotation["iv_judge"], evidence()["judge"])
        collection.validate_workspace(value)
        partial = collection.observed_catalog(value)
        self.assertIsNone(partial["forms"][0]["final_evolution"])
        self.assertIsNone(partial["forms"][0]["shiny_available"])
        report = collection.review(value, partial, {"version": 1, "collections": []})
        self.assertEqual(report["counts"]["unresolved"], 6)
        self.assertEqual(report["counts"]["review"], 6)

    def test_source_fingerprint_includes_evidence_and_annotations_do_not_change_it(self):
        inventory = workspace()["source_inventory"]
        original = collection.planner.source_validation(inventory)
        inventory["pokemon"]["pokemon-00000"]["collection_observation"] = evidence()
        enriched = collection.planner.source_validation(inventory)
        self.assertNotEqual(original, enriched)
        value = collection.make_workspace(inventory)
        value["annotations"]["pokemon-00000"].update(form_key="manually-reviewed-form", identity_reviewed=True)
        self.assertEqual(enriched, collection.planner.source_validation(value["source_inventory"]))

    def test_csv_keeps_rating_and_training_separate_from_exact_iv(self):
        inventory = workspace()["source_inventory"]
        inventory["pokemon"]["pokemon-00000"]["collection_observation"] = evidence()
        value = collection.make_workspace(inventory)
        report = collection.review(value, collection.observed_catalog(value), {"version": 1, "collections": []})
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "review.csv"
            collection.write_review_csv(value, report, path)
            with path.open(encoding="utf-8-sig", newline="") as stream:
                row = next(csv.DictReader(stream))
        self.assertEqual(row["iv_hp"], "31")
        self.assertEqual(row["iv_special_defense"], "")
        self.assertEqual(row["judge_special_defense"], "Hyper trained!")
        self.assertEqual(row["hyper_trained_special_defense"], "True")
        self.assertEqual(row["judge_special_attack"], "Decent")
        self.assertEqual(row["iv_special_attack"], "")

    def test_hyper_training_cannot_invent_original_iv(self):
        value = evidence()
        value["ivs"]["special_defense"] = 31
        with self.assertRaisesRegex(ValueError, "Exact"):
            observations.annotations_from_observation({"collection_observation": value})

    def test_unreadable_retains_nulls_and_partial_state(self):
        value = evidence()
        value["judge"]["stats"]["hp"] = dict(status="unreadable", raw="8est", raw_confirmation="Best",
                                               rating=None, iv_min=None, iv_max=None, hyper_trained=None)
        value["judge"]["status"] = "partial"
        value["ivs"]["hp"] = None
        self.assertIsNone(observations.annotations_from_observation({"collection_observation": value})["ivs"]["hp"])
        value["judge"]["stats"]["hp"]["rating"] = "Best"
        with self.assertRaises(ValueError):
            observations.annotations_from_observation({"collection_observation": value})

    def test_rating_and_range_must_agree(self):
        for minimum, maximum, trained in [(1, 31, False), (31, 31, True), (True, 31, False)]:
            judge = evidence()["judge"]
            judge["stats"]["hp"].update(iv_min=minimum, iv_max=maximum, hyper_trained=trained)
            with self.assertRaises(ValueError):
                observations.validate_judge(judge)

    def test_unavailable_judge_stays_unknown(self):
        self.assertIsNone(observations.validate_judge({"status": "unavailable_or_unreadable"}))
        with self.assertRaises(ValueError):
            observations.validate_judge({"status": "unsupported_language", "stats": {}})


if __name__ == "__main__":
    unittest.main()
