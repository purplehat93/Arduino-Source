import copy
import unittest

import collection
from test_collection import workspace, catalog


class IdentityReviewTests(unittest.TestCase):
    def setUp(self):
        self.workspace = collection.make_workspace(workspace()["source_inventory"])
        self.catalog = catalog()
        self.sheet = collection.identity_review_sheet(self.workspace, self.catalog)

    def apply(self):
        return collection.apply_identity_review(self.workspace, self.catalog, self.sheet)

    def confirm(self):
        self.sheet["entries"][0].update(decision="confirm", form_key="form-1",
                                       review_note="Inspected this individual's summary.")

    def test_no_automatic_approval_or_source_changes(self):
        original = copy.deepcopy(self.workspace)
        self.assertEqual(self.apply(), original)
        self.confirm()
        result = self.apply()
        self.assertEqual(self.workspace, original)
        self.assertEqual(result["source_inventory"], original["source_inventory"])
        self.assertTrue(result["annotations"]["pokemon-00000"]["identity_reviewed"])
        self.assertEqual(sum(a["identity_reviewed"] for a in result["annotations"].values()), 1)
        self.assertFalse(result["policy"]["release_enabled"])

    def test_species_gender_and_shiny_constraints(self):
        self.confirm()
        self.sheet["entries"][0]["form_key"] = "form-2"
        with self.assertRaisesRegex(ValueError, "species"):
            self.apply()
        self.sheet["entries"][0]["form_key"] = "form-1"
        self.catalog["forms"][0]["gender"] = "Female"
        self.sheet["catalog"] = copy.deepcopy(self.catalog)
        with self.assertRaisesRegex(ValueError, "gender"):
            self.apply()

    def test_stale_source_catalog_and_annotations_rejected(self):
        self.sheet["source_validation"] = {}
        with self.assertRaisesRegex(ValueError, "source mismatch"):
            self.apply()
        self.sheet = collection.identity_review_sheet(self.workspace, self.catalog)
        self.catalog["forms"][0]["final_evolution"] = True
        with self.assertRaisesRegex(ValueError, "catalog changed"):
            self.apply()
        self.sheet = collection.identity_review_sheet(self.workspace, self.catalog)
        self.workspace["annotations"]["pokemon-00000"]["protected"] = True
        with self.assertRaisesRegex(ValueError, "annotations changed"):
            self.apply()

    def test_missing_duplicate_or_modified_evidence_rejected(self):
        self.sheet["entries"].pop()
        with self.assertRaisesRegex(ValueError, "every occurrence"):
            self.apply()
        self.sheet = collection.identity_review_sheet(self.workspace, self.catalog)
        self.sheet["entries"][1] = copy.deepcopy(self.sheet["entries"][0])
        with self.assertRaisesRegex(ValueError, "repeated"):
            self.apply()
        self.sheet = collection.identity_review_sheet(self.workspace, self.catalog)
        self.sheet["entries"][0]["observed"]["dex"] = 2
        with self.assertRaisesRegex(ValueError, "evidence"):
            self.apply()

    def test_confirmation_needs_explicit_decision_and_note(self):
        self.sheet["entries"][0]["form_key"] = "form-1"
        with self.assertRaisesRegex(ValueError, "explicit decision"):
            self.apply()
        self.sheet["entries"][0]["decision"] = "confirm"
        with self.assertRaisesRegex(ValueError, "review note"):
            self.apply()

    def test_unknown_clears_identity_but_retains_quality(self):
        self.confirm()
        reviewed = self.apply()
        sheet = collection.identity_review_sheet(reviewed, self.catalog)
        sheet["entries"][0].update(decision="unknown", form_key=None)
        result = collection.apply_identity_review(reviewed, self.catalog, sheet)
        annotation = result["annotations"]["pokemon-00000"]
        self.assertFalse(annotation["identity_reviewed"])
        self.assertIsNone(annotation["form_key"])
        self.assertEqual(annotation["ivs"], reviewed["annotations"]["pokemon-00000"]["ivs"])


if __name__ == "__main__":
    unittest.main()
