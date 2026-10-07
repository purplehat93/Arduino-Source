"""Synthetic form-family regression; this does not measure live HOME accuracy."""
import argparse
import json
from pathlib import Path
import tempfile

from reference_catalog import build_reference, match_form
from visual_detector import run_pixel_probe, validate_visual_assets


def validate(resources):
    from PIL import Image
    catalog = build_reference()
    validate_visual_assets(catalog, resources)
    forms = {form["key"]: form for form in catalog["forms"]}
    keys = ["unown-a", "unown-b", "unown-m", "unown-question", "vivillon-archipelago",
            "vivillon-fancy", "vivillon-poke-ball", "minior-red-core", "minior-blue-core",
            "furfrou-heart-trim", "furfrou-natural", "deerling-autumn", "deerling-spring",
            "tatsugiri-curly", "tatsugiri-stretchy", "sinistea-phony", "polteageist-antique",
            "poltchageist-artisan", "sinistcha-masterpiece", "alcremie-vanilla-cream-strawberry",
            "alcremie-ruby-cream-strawberry", "zygarde-ten-percent", "zygarde-fifty-percent"]
    configurations = [(key, False) for key in keys]
    configurations += [(key, True) for key in ("minior-red-core", "minior-blue-core",
                                               "alcremie-vanilla-cream-strawberry", "alcremie-ruby-cream-strawberry")]
    atlas = Image.open(resources / "AllHomeSprites.png").convert("RGBA")
    locations = json.loads((resources / "AllHomeSprites.json").read_text())["spriteLocations"]
    frame = Image.new("RGB", (1920, 1080), (235, 255, 246))
    requests, labels = [], []
    for index, (key, shiny) in enumerate(configurations):
        form = forms[key]
        render = "shiny" if shiny else "normal"
        slug = form["visual_templates"][render]
        if not slug:
            raise ValueError("Regression sprite missing: " + key)
        xy = locations[slug]
        sprite = atlas.crop((xy["left"], xy["top"], xy["left"] + 64, xy["top"] + 64))
        sprite = sprite.resize((108, 108), Image.Resampling.BILINEAR)
        row, column = divmod(index, 6)
        frame.paste(sprite, (round(1920 * (.073 + .072 * column) - 54),
                             round(1080 * (.226 + .105 * row) - 54)), sprite)
        observed = {"dex": form["dex"], "name": form["species"], "gmax": False,
                    "primary_type": form["types"][0], "secondary_type": form["types"][1] if len(form["types"]) > 1 else "none",
                    "gender": form["gender"] or "Male", "ability": (form.get("abilities") or [None])[0]}
        candidates = match_form(observed, catalog)["candidates"]
        mapping = {candidate: forms[candidate]["visual_templates"][render] for candidate in candidates}
        if not all(mapping.values()):
            raise ValueError("Regression candidate templates incomplete: " + key)
        requests.append({"row": row, "column": column, "templates": mapping})
        labels.append({"key": key, "shiny": shiny})
    with tempfile.TemporaryDirectory() as directory:
        results = run_pixel_probe(frame, requests, resources, Path(directory))
    entries = [{**label, "result": result} for label, result in zip(labels, results)]
    summary = {"synthetic_icons": len(entries), "strong_matches": sum(bool(entry["result"]["key"]) for entry in entries),
               "wrong_strong_matches": sum(bool(entry["result"]["key"]) and entry["result"]["key"] != entry["key"] for entry in entries)}
    return {"kind": "synthetic_form_family_regression", "live_accuracy_measured": False,
            "identity_auto_approved": False, "summary": summary, "entries": entries}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--resources", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    report = validate(args.resources)
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report["summary"]))
    if report["summary"]["wrong_strong_matches"]:
        raise SystemExit("Synthetic regression found an incorrect accepted form")
