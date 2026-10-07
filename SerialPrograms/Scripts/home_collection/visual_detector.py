"""Offline HOME icon evaluation using the native C++ pixel policy.

Requires Pillow and g++; retains no images or personal inventory in the repo.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile

from reference_catalog import build_reference


def validate_visual_assets(catalog, resources):
    for name, manifest in catalog["provenance"]["visual_assets"]["files"].items():
        if hashlib.sha256((resources / name).read_bytes()).hexdigest() != manifest["sha256"]:
            raise ValueError("Visual resource checksum mismatch: " + name)


def write_image_blob(image, path):
    image = image.convert("RGBA")
    path.write_bytes(struct.pack("<II", image.width, image.height) + image.tobytes("raw", "BGRA"))


def run_pixel_probe(frame, requests, sprites, directory, *, color_experiment=False):
    """Requests contain row, column, and explicit form-key -> atlas-key mappings."""
    from PIL import Image
    directory = Path(directory)
    binary = directory / "visual_probe"
    if not binary.exists():
        subprocess.run(["g++", "-std=c++17", "-O2", str(Path(__file__).with_name("visual_probe.cpp")),
                        "-o", str(binary)], check=True, capture_output=True)
    frame_path = directory / "frame.bin"
    write_image_blob(frame, frame_path)
    atlas = Image.open(sprites / "AllHomeSprites.png").convert("RGBA")
    locations = json.loads((sprites / "AllHomeSprites.json").read_text())["spriteLocations"]
    lines = [str(len(requests))]
    for request in requests:
        mapping = request["templates"]
        lines.append(f'{request["row"]} {request["column"]} {len(mapping)}')
        for key, sprite in mapping.items():
            path = directory / (hashlib.sha256(sprite.encode()).hexdigest() + ".bin")
            if not path.exists():
                xy = locations[sprite]
                write_image_blob(atlas.crop((xy["left"], xy["top"], xy["left"] + 64, xy["top"] + 64)), path)
            lines.append(json.dumps(key) + " " + json.dumps(str(path)))
    command = [str(binary), str(frame_path)] + (["--color-experiment"] if color_experiment else [])
    result = subprocess.run(command, input="\n".join(lines) + "\n",
                            text=True, capture_output=True, check=True)
    return [json.loads(line) for line in result.stdout.splitlines()]


def main():
    from PIL import Image
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--resources", type=Path, required=True, help="Resources/Pokemon directory")
    parser.add_argument("--frame", type=Path, required=True)
    parser.add_argument("--workspace", type=Path, required=True)
    parser.add_argument("--physical-box", type=int, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    catalog = build_reference()
    validate_visual_assets(catalog, args.resources)
    workspace = json.loads(args.workspace.read_text())
    inventory = workspace["source_inventory"]
    relative_box = args.physical_box - inventory["scan_context"]["physical_start_box"]
    if relative_box < 0 or relative_box >= inventory["scan_context"]["box_count"]:
        parser.error("Physical box is outside the source scan")
    requests = []
    for index, occurrence in enumerate(inventory["slots"][relative_box * 30:relative_box * 30 + 30]):
        if occurrence is None:
            continue
        observed = inventory["pokemon"][occurrence]
        render = "shiny" if observed.get("shiny") is True else "normal"
        forms = [form for form in catalog["forms"] if form["species"] == observed["name"]
                 and form.get("home_available") is not False]
        mapping = {form["key"]: form.get("visual_templates", {}).get(render) for form in forms}
        requests.append({"row": index // 6, "column": index % 6, "templates": {k: v for k, v in mapping.items() if v},
                         "templates_complete": all(mapping.values()), "slot": index + 1,
                         "expected_unreviewed_key": workspace["annotations"][occurrence]["form_key"]})
    with tempfile.TemporaryDirectory() as directory:
        results = run_pixel_probe(Image.open(args.frame), requests, args.resources, directory)
    entries = [{**request, "visual": result} for request, result in zip(requests, results)]
    report = {"kind": "offline_home_icon_probe", "identity_auto_approved": False,
              "independent_frames_verified": False, "physical_box": args.physical_box,
              "frame_sha256": hashlib.sha256(args.frame.read_bytes()).hexdigest(), "entries": entries}
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"icons": len(entries), "strong_pixel_matches": sum(bool(result["key"]) for result in results)}))


if __name__ == "__main__":
    main()
