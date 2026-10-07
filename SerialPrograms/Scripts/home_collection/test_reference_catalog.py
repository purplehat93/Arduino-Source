import copy
import hashlib
import json
import tempfile
import shutil
import unittest
from pathlib import Path

import collection
import reference_catalog
from test_collection import workspace


class ReferenceCatalogTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.catalog = reference_catalog.build_reference()
        cls.forms = collection.validate_catalog(cls.catalog)

    def observed(self, dex, name, primary, secondary="none", gender="Male"):
        return dict(dex=dex, name=name, primary_type=primary, secondary_type=secondary,
                    gender=gender, gmax=False)

    def test_catalog_is_reproducible_and_unknown_availability_explicit(self):
        self.assertEqual(self.catalog, reference_catalog.build_reference())
        self.assertEqual(self.catalog["coverage"]["species"], 1025)
        self.assertTrue(all(form["shiny_available"] is None and form["home_available"] is None
                            for form in self.forms.values() if "home_review" not in form))
        self.assertNotIn("venusaur-mega", self.forms)
        self.assertNotIn("darmanitan-zen-mode", self.forms)
        self.assertNotIn("zacian-crowned-sword", self.forms)

    def test_shipped_catalog_matches_pinned_data_and_scanner_checksum(self):
        raw = (reference_catalog.DATA / "HomeCollectionReference.json").read_bytes()
        self.assertEqual(json.loads(raw), self.catalog)
        scanner = Path(__file__).resolve().parents[2] / "Source/VideoGames/PokemonHome/Programs/PokemonHome_CollectionObservation.inc"
        policy = scanner.with_name("PokemonHome_CollectionExecutionPolicy.h")
        self.assertIn(hashlib.sha256(raw).hexdigest(), policy.read_text())
        self.assertIn("HOME_COLLECTION_REFERENCE_SHA256", scanner.read_text())

    def test_curated_unavailable_shiny_forms_and_unknowns(self):
        unavailable = {
            "pikachu-original-cap", "pikachu-hoenn-cap", "pikachu-sinnoh-cap",
            "pikachu-unova-cap", "pikachu-kalos-cap", "pikachu-alola-cap",
            "pikachu-world-cap", "victini", "vivillon-poke-ball",
            "floette-eternal-flower", "cosmog", "cosmoem", "magearna",
            "magearna-original-color", "marshadow", "kubfu",
            "urshifu-rapid-strike-style", "urshifu-single-strike-style",
            "zarude", "zarude-dada", "glastrier", "spectrier", "calyrex",
            "ursaluna-bloodmoon", "walking-wake", "iron-leaves", "okidogi",
            "munkidori", "fezandipiti", "ogerpon-teal-mask", "gouging-fire",
            "raging-bolt", "iron-boulder", "iron-crown", "terapagos-normal",
            "pecharunt",
        }
        for key in unavailable:
            with self.subTest(key=key):
                form = self.forms[key]
                self.assertIs(form["shiny_available"], False)
                self.assertIn("https://www.serebii.net/games/shiny.shtml", form["home_review"]["sources"])
        self.assertIsNone(self.forms["pikachu-male"]["shiny_available"])
        self.assertIsNone(self.forms["bulbasaur"]["shiny_available"])
        self.assertEqual(len(unavailable), 36)

    def test_form_specific_evolution_and_gender_restrictions(self):
        expected = {"corsola": True, "corsola-galar": False,
                    "basculin-blue-striped": True, "basculin-red-striped": True,
                    "basculin-white-striped": False, "combee-male": True,
                    "combee-female": False, "bulbasaur": False, "typhlosion-hisui": True}
        for key, terminal in expected.items():
            self.assertIs(self.forms[key]["final_evolution"], terminal, key)

    def test_regional_and_gender_matching_is_conservative(self):
        matcher = reference_catalog.match_form
        self.assertEqual(matcher(self.observed(157, "typhlosion", "fire", "ghost"), self.catalog)["key"], "typhlosion-hisui")
        self.assertEqual(matcher(self.observed(222, "corsola", "ghost"), self.catalog)["key"], "corsola-galar")
        self.assertEqual(matcher(self.observed(3, "venusaur", "grass", "poison", "Female"), self.catalog)["key"], "venusaur-female")
        # Equal-type cosmetic variants cannot be resolved from these fields.
        self.assertIsNone(matcher(self.observed(201, "unown", "psychic"), self.catalog)["key"])
        self.assertIsNone(matcher(self.observed(550, "basculin", "water"), self.catalog)["key"])
        self.assertEqual(matcher(self.observed(172, "pichu", "electric"), self.catalog)["key"], "pichu")

    def test_non_home_pichu_is_excluded_for_both_genders(self):
        self.assertIs(self.forms["pichu-spiky-eared"]["home_available"], False)
        self.assertEqual(self.forms["pichu-spiky-eared"]["gender"], "Female")
        for gender in ("Male", "Female"):
            match = reference_catalog.match_form(self.observed(172, "pichu", "electric", gender=gender), self.catalog)
            self.assertEqual(match["key"], "pichu")
            self.assertEqual(match["candidates"], ["pichu"])
        # Unknown eligibility stays conservative; never treat null as excluded.
        catalog = copy.deepcopy(self.catalog)
        next(form for form in catalog["forms"] if form["key"] == "pichu-spiky-eared")["home_available"] = None
        self.assertIsNone(reference_catalog.match_form(self.observed(172, "pichu", "electric", gender="Female"), catalog)["key"])

    def test_bad_species_types_gender_and_gmax_never_produce_identity(self):
        value = self.observed(3, "venusaur", "grass", "poison")
        for field, wrong in (("name", "bulbasaur"), ("primary_type", "fire"),
                             ("gender", "unknown"), ("gmax", True), ("gmax", None)):
            with self.subTest(field=field, wrong=wrong):
                bad = {**value, field: wrong}
                self.assertIsNone(reference_catalog.match_form(bad, self.catalog)["key"])

    def test_missing_classification_blocks_unique_match(self):
        catalog = copy.deepcopy(self.catalog)
        catalog["forms"].append(dict(key="unknown-corsola-form", dex=222, species="corsola", types=None))
        self.assertIsNone(reference_catalog.match_form(self.observed(222, "corsola", "ghost"), catalog)["key"])

    def test_source_corruption_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            data = Path(directory) / "data"
            shutil.copytree(reference_catalog.DATA, data)
            with (data / "pokeapi/pokemon_forms.csv").open("a") as stream:
                stream.write("corruption")
            with self.assertRaisesRegex(ValueError, "checksum mismatch"):
                reference_catalog.build_reference(data)

    def test_minior_mapping_and_ogerpon_storage_rules(self):
        cores = [form for form in self.catalog["forms"] if form["species"] == "minior"
                 and form["home_available"] is True]
        self.assertEqual(len(cores), 7)
        self.assertTrue(all(form["key"].endswith("-core") and form["types"] == ["rock", "flying"]
                            for form in cores))
        self.assertEqual(self.catalog["coverage"]["unmapped"], [])
        match = reference_catalog.match_form(self.observed(774, "minior", "rock", "flying", "Genderless"), self.catalog)
        self.assertEqual(set(match["candidates"]), {form["key"] for form in cores})
        self.assertIsNone(match["key"])
        self.assertEqual(reference_catalog.match_form(self.observed(1017, "ogerpon", "grass", gender="Female"), self.catalog)["key"], "ogerpon-teal-mask")
        for other_type in ("fire", "water", "rock"):
            self.assertIsNone(reference_catalog.match_form(self.observed(1017, "ogerpon", "grass", other_type, "Female"), self.catalog)["key"])

    def test_home_normalization_does_not_create_impossible_candidates(self):
        cases = [(646, "kyurem", "dragon", "ice", "Genderless", "kyurem"),
                 (649, "genesect", "bug", "steel", "Genderless", "genesect"),
                 (890, "eternatus", "poison", "dragon", "Genderless", "eternatus"),
                 (20, "raticate", "dark", "normal", "Male", "raticate-alola"),
                 (105, "marowak", "fire", "ghost", "Male", "marowak-alola"),
                 (133, "eevee", "normal", "none", "Female", "eevee-female")]
        for dex, name, primary, secondary, gender, key in cases:
            self.assertEqual(reference_catalog.match_form(self.observed(dex, name, primary, secondary, gender), self.catalog)["key"], key)
        for dex, name in ((493, "arceus"), (773, "silvally")):
            self.assertIsNone(reference_catalog.match_form(self.observed(dex, name, "fire", gender="Genderless"), self.catalog)["key"])

    def test_species_aliases_do_not_relax_types_or_change_source(self):
        observed = self.observed(865, "sirfetch-d", "fighting")
        self.assertEqual(reference_catalog.match_form(observed, self.catalog)["key"], "sirfetchd")
        self.assertEqual(observed["name"], "sirfetch-d")
        self.assertIsNone(reference_catalog.match_form({**observed, "name": "sirfetch"}, self.catalog)["key"])
        self.assertIsNone(reference_catalog.match_form({**observed, "primary_type": "fire"}, self.catalog)["key"])

    def test_reviewed_ability_rules_are_exact_and_conservative(self):
        cases = [(744, "rockruff", "rock", "none", "own-tempo", "rockruff-own-tempo"),
                 (744, "rockruff", "rock", "none", "keen-eye", "rockruff"),
                 (744, "rockruff", "rock", "none", "vital-spirit", "rockruff"),
                 (744, "rockruff", "rock", "none", "steadfast", "rockruff"),
                 (658, "greninja", "water", "dark", "battle-bond", "greninja-battle-bond"),
                 (658, "greninja", "water", "dark", "protean", "greninja")]
        for dex, name, primary, secondary, ability, key in cases:
            observed = self.observed(dex, name, primary, secondary)
            result = reference_catalog.match_form({**observed, "ability": ability}, self.catalog)
            self.assertEqual(result["key"], key)
            self.assertEqual(result["evidence"], "reference_types_gender_and_ability")
            display = ability.replace("-", " ").title()
            self.assertEqual(reference_catalog.match_form({**observed, "ability": display}, self.catalog)["key"], key)
            for missing in (None, "", "not-an-ability", "OwnTempo", "own  tempo"):
                self.assertIsNone(reference_catalog.match_form({**observed, "ability": missing}, self.catalog)["key"])
        zygarde = {**self.observed(718, "zygarde", "dragon", "ground", "Genderless"), "ability": "power-construct"}
        self.assertIsNone(reference_catalog.match_form({**self.observed(658, "greninja", "water", "dark", "Female"), "ability": "battle-bond"}, self.catalog)["key"])
        match = reference_catalog.match_form(zygarde, self.catalog)
        self.assertIsNone(match["key"])
        self.assertEqual(set(match["candidates"]), {"zygarde-10-power-construct", "zygarde-50-power-construct"})
        bad = copy.deepcopy(self.catalog)
        next(form for form in bad["forms"] if form["key"] == "rockruff").pop("abilities")
        self.assertEqual(reference_catalog.match_form({**self.observed(744, "rockruff", "rock"), "ability": "own-tempo"}, bad)["reason"], "ability_rules_incomplete")

    def test_identification_report_does_not_approve_or_mutate(self):
        value = collection.make_workspace(workspace()["source_inventory"])
        original = copy.deepcopy(value)
        report = reference_catalog.identification_report(value, self.catalog)
        self.assertEqual(report["counts"]["scanned"], 6)
        self.assertFalse(report["identity_auto_approved"])
        self.assertEqual(value, original)

    def test_unreviewed_home_eligibility_blocks_collection_placement(self):
        value = collection.make_workspace(workspace()["source_inventory"])
        profile = {"version": 1, "collections": [{"id": "living", "kind": "living_form",
                    "start_box": 31, "form_keys": ["bulbasaur"]}]}
        with self.assertRaisesRegex(ValueError, "HOME eligibility"):
            collection.review(value, self.catalog, profile)
        reviewed = copy.deepcopy(self.catalog)
        next(form for form in reviewed["forms"] if form["key"] == "bulbasaur")["home_available"] = True
        report = collection.review(value, reviewed, profile)
        self.assertEqual(len(report["gaps"]), 1)


if __name__ == "__main__":
    unittest.main()
