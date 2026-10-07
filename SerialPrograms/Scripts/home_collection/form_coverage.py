"""Offline reference-policy coverage, not a live detector accuracy benchmark."""
import argparse
from collections import Counter
import json

from reference_catalog import build_reference, match_form


def audit(catalog, use_ability=True):
    entries = []
    for form in catalog["forms"]:
        candidates = set()
        if form.get("home_available") is False:
            status = "excluded_home"
        else:
            results = []
            for gender in ([form["gender"]] if form["gender"] else ["Male", "Female"]):
                for ability in (form.get("abilities") or [None]) if use_ability else [None]:
                    types = form.get("types") or []
                    result = match_form({"dex": form["dex"], "name": form["species"],
                                         "gender": gender, "gmax": False, "ability": ability,
                                         "primary_type": types[0] if types else "none",
                                         "secondary_type": types[1] if len(types) > 1 else "none"}, catalog)
                    results.append(result)
                    candidates.update(result.get("candidates", []))
            if all(result.get("key") == form["key"] for result in results):
                status = "unique"
            elif any(result.get("reason") == "reference_coverage_needs_review" for result in results):
                status = "catalog_blocked"
            else:
                status = "ambiguous"
        entries.append({"key": form["key"], "species": form["species"],
                        "status": status, "candidates": sorted(candidates)})
    return {"kind": "offline_reference_policy_coverage", "live_accuracy_measured": False,
            "assumptions": ["correct species/types/gender readings", "gmax=false",
                            "reviewed ability tokens supplied" if use_ability else "ability unavailable",
                            "unknown HOME eligibility retained conservatively"],
            "counts": dict(Counter(entry["status"] for entry in entries)), "forms": entries}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", help="Write per-form JSON; otherwise print counts")
    args = parser.parse_args()
    result = audit(build_reference())
    if args.output:
        with open(args.output, "w") as output:
            json.dump(result, output, indent=2)
            output.write("\n")
    print(json.dumps(result["counts"], sort_keys=True))
