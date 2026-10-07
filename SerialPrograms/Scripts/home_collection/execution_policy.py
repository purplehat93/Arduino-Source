"""Bind reviewed metadata identities to the current live scanner policy."""
import hashlib
import json
import reference_catalog
import quality

MODE = "metadata_form_v1"


def build_execution_contract(workspace, target=None, report=None, verify_judge=False):
    raw = (reference_catalog.DATA / "HomeCollectionReference.json").read_bytes()
    reference = reference_catalog.build_reference()
    if json.loads(raw) != reference:
        raise ValueError("Regenerate the shipped reference before enabling execution.")
    verify_judge = verify_judge or (report or {}).get("judge_verification", False)
    keys, signatures, bases, judges = {}, {}, {}, {}
    for occurrence, observed in workspace["source_inventory"]["pokemon"].items():
        annotation = workspace["annotations"][occurrence]
        observation = observed.get("collection_observation", {})
        if not isinstance(observation, dict) or not isinstance(observation.get("form"), dict):
            raise ValueError(f"Execution needs recorded metadata form evidence: {occurrence}.")
        form = observation["form"]
        key = annotation.get("form_key")
        # Re-evaluate older capture metadata against today's pinned reference.
        # The original capture and its catalog digest remain in the source.
        fields = {k: v for k, v in observed.items() if k != "collection_observation"}
        match = reference_catalog.match_form(fields, reference)
        digest = form.get("resource_sha256")
        if (not annotation.get("identity_reviewed") or form.get("fields_confirmed") is not True
                or type(observation.get("version")) is not int or observation.get("version") != 1 or form.get("status") != "candidate_requires_review"
                or form.get("evidence") not in ("reference_types_and_gender", "reference_types_gender_and_ability")
                or form.get("resource") != "Pokemon/HomeCollectionReference.json"
                or not isinstance(digest, str) or len(digest) != 64
                or any(c not in "0123456789abcdef" for c in digest)
                or form.get("key") != key or form.get("candidates") != [key]
                or match.get("key") != key or match.get("candidates") != [key]
                or type(observed.get("shiny")) is not bool or observed.get("gmax") is not False):
            raise ValueError(f"Execution needs a reviewed, repeat-frame metadata form identity: {occurrence}.")
        fields["collection_form_key"] = key
        base = json.dumps(fields, sort_keys=True, separators=(",", ":"), ensure_ascii=False)
        bases.setdefault(base, []).append(occurrence)
        if verify_judge:
            judges[occurrence] = quality.source_judge(observed)
            fields["collection_judge"] = judges[occurrence]
        signature = json.dumps(fields, sort_keys=True, separators=(",", ":"), ensure_ascii=False)
        signatures.setdefault(signature, []).append(occurrence)
        keys[occurrence] = key
    duplicates = {item for ids in signatures.values() if len(ids) > 1 for item in ids}
    if verify_judge:
        for ids in bases.values():
            for i, first in enumerate(ids):
                for second in ids[i + 1:]:
                    if quality.overlap(judges[first], judges[second]):
                        duplicates.update((first, second))
        source = workspace["source_inventory"]["slots"]
        touched = {item for i, item in enumerate(source) if item is not None and target is not None and target[i] != item}
        selected = {a["occurrence"] for a in (report or {}).get("assignments", []) if a["occurrence"] is not None}
        if any(any(value is None for value in judges[item].values()) for item in touched | selected):
            raise ValueError("Moved or selected copies need all six confirmed source Judge ratings; keep unknown copies for review.")
    if duplicates:
        source = workspace["source_inventory"]["slots"]
        assigned = {a["occurrence"] for a in (report or {}).get("assignments", [])}
        if (target is None or report is None or assigned & duplicates
                or any(target[i] != item for i, item in enumerate(source) if item in duplicates)):
            raise ValueError("Execution cannot distinguish identical observed copies; retain them in unchanged review slots.")
    contract = {"version": 1, "mode": "metadata_form_judge_v1" if verify_judge else MODE,
            "reference_sha256": hashlib.sha256(raw).hexdigest(), "form_keys": keys,
            "quality_annotations_verified": False, "release_enabled": False,
            "stationary_review_occurrences": sorted(duplicates)}

    if verify_judge:
        contract["judge_requirements"] = judges
    return contract


SPECIES_MODE = "metadata_species_v2"


def species_record(observed, require_confirmation=True):
    """Stable summary identity for species ordering, without form/IV approval."""
    form = observed.get("collection_observation", {}).get("form", {})
    if require_confirmation and form.get("fields_confirmed") is not True:
        raise ValueError("Species execution requires repeated summary fields.")
    if (type(observed.get("dex")) is not int or not 1 <= observed["dex"] <= 1025
            or not isinstance(observed.get("name"), str) or not observed["name"]
            or type(observed.get("shiny")) is not bool or type(observed.get("gmax")) is not bool):
        raise ValueError("Species execution requires species/shiny/Gmax fields.")
    return {k: v for k, v in observed.items() if k != "collection_observation"}


def species_execution_contract(workspace):
    """Track copies by slot history; only unconfirmed/protected copies stay still."""
    reference = reference_catalog.build_reference()
    pairs = {(f["dex"], reference_catalog.canonical_species_name(f["species"])) for f in reference["forms"]}
    stationary, requirements = set(), {}
    for item, observed in workspace["source_inventory"]["pokemon"].items():
        species_record(observed, require_confirmation=False)
        if (observed["dex"], reference_catalog.canonical_species_name(observed["name"])) not in pairs:
            raise ValueError("Species name and Dex disagree: " + item)
        requirements[item] = {"dex": observed["dex"], "name": observed["name"]}
        if (observed.get("collection_observation", {}).get("form", {}).get("fields_confirmed") is not True
                or workspace["annotations"][item].get("protected") is True):
            stationary.add(item)
    return {"version": 1, "mode": SPECIES_MODE,
            "reference_sha256": hashlib.sha256((reference_catalog.DATA / "HomeCollectionReference.json").read_bytes()).hexdigest(),
            "ordering": "national_dex_species", "copies": "keep_all",
            "duplicate_policy": "tracked_slot_history",
            "species_requirements": requirements,
            "stationary_review_occurrences": sorted(stationary),
            "quality_annotations_verified": False, "release_enabled": False}
