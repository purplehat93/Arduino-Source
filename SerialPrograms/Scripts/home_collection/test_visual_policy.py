import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

import reference_catalog
from visual_detector import run_pixel_probe


@unittest.skipUnless(shutil.which("g++") and importlib.util.find_spec("PIL"), "g++ and Pillow required")
class VisualPixelTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        from PIL import Image, ImageDraw
        cls.temporary = tempfile.TemporaryDirectory()
        cls.directory = Path(cls.temporary.name)
        atlas = Image.new("RGBA", (128, 64))
        draw = ImageDraw.Draw(atlas)
        draw.rectangle((20, 8, 44, 56), fill=(220, 40, 30, 255))
        draw.rectangle((84, 8, 108, 56), fill=(30, 60, 220, 255))
        atlas.save(cls.directory / "AllHomeSprites.png")
        (cls.directory / "AllHomeSprites.json").write_text(json.dumps({"spriteLocations": {
            "red": {"left": 0, "top": 0}, "blue": {"left": 64, "top": 0}}}))
        cls.sprite = atlas.crop((0, 0, 64, 64))

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def frame(self, height=1080):
        from PIL import Image
        image = Image.new("RGB", (height * 16 // 9, height), (235, 255, 246))
        side = round(height * 0.1)
        sprite = self.sprite.resize((side, side), Image.Resampling.BILINEAR)
        image.paste(sprite, (round(image.width * .073 - side / 2), round(height * .226 - side / 2)), sprite)
        return image

    def evaluate(self, image, mapping=None):
        return run_pixel_probe(image, [{"row": 0, "column": 0,
                                       "templates": mapping or {"red-form": "red", "blue-form": "blue"}}],
                               self.directory, self.directory)[0]

    def test_correct_pixels_at_multiple_resolutions(self):
        for height in (720, 1080, 1440):
            self.assertEqual(self.evaluate(self.frame(height))["key"], "red-form")

    def test_moderate_dimming_preserves_evidence(self):
        from PIL import ImageEnhance
        self.assertEqual(self.evaluate(ImageEnhance.Brightness(self.frame()).enhance(.6))["key"], "red-form")

    def test_blank_black_and_unsupported_images_remain_unknown(self):
        from PIL import Image
        for color in ((255, 255, 255), (0, 0, 0), (235, 255, 246)):
            self.assertEqual(self.evaluate(Image.new("RGB", (1920, 1080), color))["key"], "")
        self.assertEqual(self.evaluate(self.frame(480))["key"], "")

    def test_duplicate_renderings_cannot_establish_identity(self):
        result = self.evaluate(self.frame(), {"first-form": "red", "second-form": "red"})
        self.assertEqual(result["key"], "")
        self.assertEqual(result["reason"], "lookalike_margin_too_small")
        self.assertEqual(self.evaluate(self.frame(), {"single-form": "red"})["key"], "")

    def test_native_frame_agreement_requires_independence(self):
        header = Path(__file__).resolve().parents[2] / "Source/VideoGames/PokemonHome/Programs/PokemonHome_CollectionVisualPolicy.h"
        source = '#include ' + json.dumps(str(header)) + '''
#include <cassert>
using namespace PokemonAutomation::NintendoSwitch::PokemonHome;
int main(){
    HomeVisualMatch a, b; a.key="first"; b.key="second";
    assert(agree_home_visual_frames(a,b,true).empty());
    b.key="first";
    assert(agree_home_visual_frames(a,b,false).empty());
    assert(agree_home_visual_frames(a,b,true)=="first");
    b.key.clear(); assert(agree_home_visual_frames(a,b,true).empty());
}
'''
        cpp = self.directory / "agreement.cpp"
        binary = self.directory / "agreement"
        cpp.write_text(source)
        subprocess.run(["g++", "-std=c++17", str(cpp), "-o", str(binary)], check=True, capture_output=True)
        subprocess.run([str(binary)], check=True, capture_output=True)


@unittest.skipUnless(shutil.which("g++") and importlib.util.find_spec("PIL"), "g++ and Pillow required")
class VisualColorExperimentTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        from PIL import Image, ImageDraw
        cls.temporary = tempfile.TemporaryDirectory()
        cls.directory = Path(cls.temporary.name)
        cls.atlas = Image.new("RGBA", (192, 64))
        draw = ImageDraw.Draw(cls.atlas)
        cls.locations = {}
        colors = ((220, 35, 35, 255), (220, 100, 20, 255), (30, 80, 220, 255))
        for index, color in enumerate(colors):
            x = index * 64
            cls.locations["sprite-" + str(index)] = {"left": x, "top": 0}
            draw.ellipse((x + 13, 5, x + 51, 59), fill=(85, 85, 85, 255))
            draw.ellipse((x + 25, 8, x + 39, 22), fill=color)
        cls.atlas.save(cls.directory / "AllHomeSprites.png")
        (cls.directory / "AllHomeSprites.json").write_text(json.dumps({"spriteLocations": cls.locations}))

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def frame(self, sprite=0, unrelated_shape=False):
        from PIL import Image, ImageDraw
        image = Image.new("RGB", (1920, 1080), (235, 255, 246))
        left, top = round(1920 * .073 - 54), round(1080 * .226 - 54)
        if unrelated_shape:
            draw = ImageDraw.Draw(image)
            draw.rectangle((left + 12, top + 4, left + 52, top + 58), fill=(85, 85, 85))
            draw.rectangle((left + 25, top + 8, left + 39, top + 22), fill=(220, 35, 35))
        else:
            icon = self.atlas.crop((sprite * 64, 0, (sprite + 1) * 64, 64))
            icon = icon.resize((108, 108), Image.Resampling.BILINEAR)
            image.paste(icon, (left, top), icon)
        return image

    def evaluate(self, frame, mapping, *, color=True):
        request = {"row": 0, "column": 0, "templates": mapping}
        return run_pixel_probe(frame, [request], self.directory, self.directory,
                               color_experiment=color)[0]

    def test_focused_color_resolves_close_hues_when_shape_stays_ambiguous(self):
        mapping = {"florges-red-flower": "sprite-0", "florges-orange-flower": "sprite-1",
                   "florges-blue-flower": "sprite-2"}
        baseline = self.evaluate(self.frame(), mapping, color=False)
        focused = self.evaluate(self.frame(), mapping)
        self.assertEqual(baseline["key"], "")
        self.assertEqual(baseline["reason"], "lookalike_margin_too_small")
        self.assertEqual(focused["key"], "florges-red-flower")
        self.assertGreaterEqual(focused["margin"], .035)

    def test_color_gate_rejects_unrelated_shape_and_unsupported_family(self):
        mapping = {"florges-red-flower": "sprite-0", "florges-orange-flower": "sprite-1",
                   "florges-blue-flower": "sprite-2"}
        unrelated = self.evaluate(self.frame(unrelated_shape=True), mapping)
        self.assertEqual(unrelated["key"], "")
        self.assertEqual(unrelated["reason"], "icon_score_too_weak")
        unsupported = self.evaluate(self.frame(), {"unown-a": "sprite-0", "unown-b": "sprite-1"})
        self.assertEqual(unsupported["key"], "")
        self.assertEqual(unsupported["reason"], "color_family_not_supported")

    def test_color_gate_rejects_incomplete_or_mixed_families(self):
        insufficient = self.evaluate(self.frame(), {"burmy-plant-cloak": "sprite-0"})
        self.assertEqual(insufficient["reason"], "insufficient_alternatives")
        mixed = self.evaluate(self.frame(), {"burmy-plant-cloak": "sprite-0",
                                             "florges-red-flower": "sprite-1"})
        self.assertEqual(mixed["reason"], "mixed_color_families")


class VisualEvidenceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.catalog = reference_catalog.build_reference()

    def observation(self):
        keys = [form["key"] for form in self.catalog["forms"] if form["species"] == "unown"]
        scores = [{"key": key, "distance": .05 if key == "unown-a" else .2, "overlap": .8} for key in keys]
        visual = {"status": "candidate_requires_review", "policy_version": 1,
                  "key": "unown-a", "render": "normal", "independent_frames": True,
                  "frames_agree": True, "templates_complete": True,
                  "reference_resource_sha256": hashlib.sha256((json.dumps(self.catalog, sort_keys=True, separators=(",", ":")) + "\n").encode()).hexdigest(),
                  "frames": [{"key": "unown-a", "scores": scores}, {"key": "unown-a", "scores": copy.deepcopy(scores)}]}
        return {"name": "unown", "dex": 201, "primary_type": "psychic", "secondary_type": "none",
                "gender": "Genderless", "gmax": False, "shiny": False,
                "collection_observation": {"form": {"fields_confirmed": True, "visual": visual}}}

    def test_valid_capture_report_remains_unreviewed_candidate(self):
        result = reference_catalog.match_form(self.observation(), self.catalog)
        self.assertEqual(result["key"], "unown-a")
        self.assertEqual(result["status"], "candidate_requires_review")
        self.assertEqual(result["evidence"], "reference_fields_and_two_box_icons")

    def test_native_resource_digest_requires_identical_catalog_content(self):
        observed = self.observation()
        raw = (reference_catalog.DATA / "HomeCollectionReference.json").read_bytes()
        observed["collection_observation"]["form"]["visual"]["reference_resource_sha256"] = hashlib.sha256(raw).hexdigest()
        self.assertEqual(reference_catalog.match_form(observed, self.catalog)["key"], "unown-a")
        changed = copy.deepcopy(self.catalog)
        changed["policy"]["identity_auto_approved"] = True
        self.assertIsNone(reference_catalog.match_form(observed, changed)["key"])

    def test_stale_incomplete_and_disagreeing_reports_are_rejected(self):
        for field, value in (("reference_resource_sha256", "stale"), ("templates_complete", False),
                             ("independent_frames", False), ("frames_agree", False),
                             ("render", "shiny"), ("key", "pikachu"), ("key", {})):
            observed = self.observation()
            observed["collection_observation"]["form"]["visual"][field] = value
            self.assertIsNone(reference_catalog.match_form(observed, self.catalog)["key"])
        observed = self.observation()
        observed["collection_observation"]["form"]["visual"]["frames"][1]["key"] = "unown-b"
        self.assertIsNone(reference_catalog.match_form(observed, self.catalog)["key"])

    def test_weak_tied_missing_and_invalid_scores_are_rejected(self):
        for scenario in ("weak", "tie", "missing", "nan", "overlap"):
            observed = self.observation()
            scores = observed["collection_observation"]["form"]["visual"]["frames"][0]["scores"]
            target = next(score for score in scores if score["key"] == "unown-a")
            if scenario == "weak": target["distance"] = .19
            if scenario == "tie": scores[1]["distance"] = .05
            if scenario == "missing": scores.pop()
            if scenario == "nan": target["distance"] = float("nan")
            if scenario == "overlap": target["overlap"] = .1
            self.assertIsNone(reference_catalog.match_form(observed, self.catalog)["key"])

    def test_shared_shiny_renderings_are_explicit(self):
        minior = [form for form in self.catalog["forms"] if form["species"] == "minior" and form["home_available"] is True]
        self.assertEqual({form["visual_templates"]["shiny"] for form in minior}, {"minior-core-shiny"})
        alcremie = [form for form in self.catalog["forms"] if form["species"] == "alcremie"]
        self.assertEqual(len(alcremie), 63)
        self.assertEqual(len({form["visual_templates"]["shiny"] for form in alcremie}), 7)


if __name__ == "__main__":
    unittest.main()
