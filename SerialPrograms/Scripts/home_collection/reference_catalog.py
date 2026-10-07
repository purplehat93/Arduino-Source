"""Build a pinned reference catalog; match candidates without approving identities."""
import csv
import hashlib
import json
import math
from pathlib import Path

DATA = Path(__file__).with_name("catalog_data")


def compatibility_keys(key):
    """Explicit vocabulary adapters; never use display text to identify forms."""
    aliases = {key}
    suffixes = {"burmy": "-cloak", "wormadam": "-cloak", "shellos": "-sea",
                "gastrodon": "-sea", "genesect": "-drive", "flabebe": "-flower",
                "floette": "-flower", "florges": "-flower", "furfrou": "-trim",
                "pumpkaboo": "-size", "gourgeist": "-size", "oricorio": "-style",
                "urshifu": "-style"}
    for prefix, suffix in suffixes.items():
        if key.startswith(prefix + "-"):
            aliases.add(key + suffix)
    if key.startswith("alcremie-") and key.endswith("-sweet"):
        aliases.add(key[:-6])
    if key.startswith("farfetchd"):
        aliases.add(key.replace("farfetchd", "farfetch-d", 1))
    if key.startswith("darmanitan-"):
        aliases.add(key + "-mode")
    if key.startswith("minior-") and key.rsplit("-", 1)[-1] in {
            "red", "orange", "yellow", "green", "blue", "indigo", "violet"}:
        aliases.add(key + "-core")
    aliases.update({"castform": {"castform-normal"}, "hoopa": {"hoopa-confined"},
                    "zygarde-50": {"zygarde-fifty-percent"}, "zygarde-10": {"zygarde-ten-percent"},
                    "xerneas-neutral": {"xerneas-neutral-mode"}, "xerneas-active": {"xerneas-active-mode"},
                    "necrozma-dawn": {"necrozma-dawn-wings"}, "necrozma-dusk": {"necrozma-dusk-mane"},
                    "magearna-original": {"magearna-original-color"},
                    "eiscue-ice": {"eiscue-ice-face"}, "eiscue-noice": {"eiscue-noice-face"},
                    "morpeko-full-belly": {"morpeko-full-belly-mode"}, "morpeko-hangry": {"morpeko-hangry-mode"},
                    "zacian": {"zacian-hero-of-many-battles"}, "zacian-crowned": {"zacian-crowned-sword"},
                    "zamazenta": {"zamazenta-hero-of-many-battles"}, "zamazenta-crowned": {"zamazenta-crowned-shield"},
                    "calyrex-ice": {"calyrex-ice-rider"}, "calyrex-shadow": {"calyrex-shadow-rider"},
                    "ogerpon": {"ogerpon-teal-mask"}, "terapagos": {"terapagos-normal"}}.get(key, set()))
    return aliases


def build_reference(data=DATA):
    data = Path(data)
    provenance = json.loads((data / "pokeapi/manifest.json").read_text())
    tables = {}
    for name, source in provenance["files"].items():
        raw = (data / "pokeapi" / name).read_bytes()
        if hashlib.sha256(raw).hexdigest() != source["sha256"]:
            raise ValueError("Reference data checksum mismatch: " + name)
        tables[name[:-4]] = list(csv.DictReader(raw.decode().splitlines()))
    local_provenance = json.loads((data / "local-resource.json").read_text())
    raw = (data / "AllFormDisplayMap.json").read_bytes()
    if hashlib.sha256(raw).hexdigest() != local_provenance["sha256"]:
        raise ValueError("Local form resource checksum mismatch.")
    local = json.loads(raw)
    visual_provenance = json.loads((data / "visual-resource.json").read_text())
    visual_raw = (data / "AllHomeSprites.json").read_bytes()
    if hashlib.sha256(visual_raw).hexdigest() != visual_provenance["files"]["AllHomeSprites.json"]["sha256"]:
        raise ValueError("Visual sprite vocabulary checksum mismatch.")
    sprite_keys = json.loads(visual_raw)["spriteLocations"].keys()
    species = {row["id"]: row for row in tables["pokemon_species"]}
    pokemon = {row["id"]: row for row in tables["pokemon"]}
    abilities = {row["id"]: row["identifier"] for row in tables["abilities"]}
    pokemon_abilities = {}
    for row in tables["pokemon_abilities"]:
        pokemon_abilities.setdefault(row["pokemon_id"], set()).add(abilities[row["ability_id"]])
    form_pokemon = {int(row["id"]): row["pokemon_id"] for row in tables["pokemon_forms"]}
    types = {row["id"]: row["identifier"] for row in tables["types"]}
    pokemon_types, form_types = {}, {}
    for table, key, target in (("pokemon_types", "pokemon_id", pokemon_types),
                               ("pokemon_form_types", "pokemon_form_id", form_types)):
        for row in tables[table]:
            target.setdefault(row[key], {})[int(row["slot"])] = types[row["type_id"]]
    children = {}
    for child in species.values():
        if child["evolves_from_species_id"]:
            children.setdefault(child["evolves_from_species_id"], []).append(child["id"])
    evolutions = {}
    for row in tables["pokemon_evolution"]:
        evolutions.setdefault(row["evolved_species_id"], []).append(row)
    defaults = {row["pokemon_id"]: row for row in tables["pokemon_forms"] if row["is_default"] == "1"}
    forms, excluded, matched_local = [], [], set()
    excluded_aliases = set()
    for row in tables["pokemon_forms"]:
        mon = pokemon[row["pokemon_id"]]
        sp = species[mon["species_id"]]
        slug, key = sp["identifier"], row["identifier"]
        if slug == "farfetchd":
            slug = "farfetch-d"
        if row["is_battle_only"] == "1" or row["is_mega"] == "1" or key.endswith("-gmax"):
            excluded.append(key)
            excluded_aliases.update(compatibility_keys(key))
            continue
        source_types = form_types.get(row["id"], pokemon_types.get(row["pokemon_id"], {}))
        ordered_types = [source_types[slot] for slot in sorted(source_types)]
        if not 1 <= len(ordered_types) <= 2:
            raise ValueError("Missing reference types: " + key)
        descendants = children.get(sp["id"], [])
        outgoing = [edge for child in descendants for edge in evolutions.get(child, [])]
        # Explicit form restrictions are respected. An unqualified species-level
        # edge does not prove which nondefault regional/cosmetic form evolves.
        if not descendants:
            terminal, basis = True, "species_has_no_evolution_children"
        elif any(edge["required_pokemon_form_id"] == row["id"] for edge in outgoing):
            terminal, basis = False, "explicit_source_form_evolution"
        elif any(not edge["required_pokemon_form_id"] for edge in outgoing):
            if mon["is_default"] == "1" and row["is_default"] == "1":
                terminal, basis = False, "default_form_species_evolution"
            else:
                terminal, basis = None, "unqualified_evolution_needs_form_review"
        elif outgoing and len({edge["evolved_species_id"] for edge in outgoing}) == len(descendants):
            terminal, basis = True, "all_outgoing_evolutions_restrict_other_forms"
        else:
            terminal, basis = None, "incomplete_evolution_rules"
        gender = "Male" if sp["gender_rate"] == "0" else "Female" if sp["gender_rate"] == "8" else "Genderless" if sp["gender_rate"] == "-1" else None
        if key.endswith("-male"):
            gender = "Male"
        elif key.endswith("-female"):
            gender = "Female"
        # Explicit compatibility translations, not display-name fuzzy matching.
        local_keys = {pair[0] for pair in local.get(slug, [])}
        aliases = compatibility_keys(key)
        variants = [(key, gender)]
        if {key + "-male", key + "-female"} <= local_keys:
            variants = [(key + "-male", "Male"), (key + "-female", "Female")]
        if mon["is_default"] == "1" and defaults.get(mon["id"]) == row:
            if {slug + "-male", slug + "-female"} <= local_keys and key == slug:
                variants = [(slug + "-male", "Male"), (slug + "-female", "Female")]
            elif slug in local_keys and key not in local_keys:
                aliases.add(slug)
        for variant, constraint in variants:
            possible = aliases & local_keys if variant == key else {variant}
            output_key = sorted(possible)[0] if possible else variant
            matched_local.update((slug, candidate) for candidate in possible)
            variant_terminal, variant_basis = terminal, basis
            gender_ids = {"Female": "1", "Male": "2", "Genderless": "3"}
            if (outgoing and constraint is not None
                    and all(edge["gender_id"] and edge["gender_id"] != gender_ids[constraint] for edge in outgoing)
                    and len({edge["evolved_species_id"] for edge in outgoing}) == len(descendants)):
                variant_terminal, variant_basis = True, "all_outgoing_evolutions_require_other_gender"
            forms.append({"key": output_key, "dex": int(sp["id"]), "species": slug,
                          "gender": constraint, "types": ordered_types,
                          "final_evolution": variant_terminal, "shiny_available": None,
                          "home_available": None, "classification": "reference_candidate",
                          "source_form": key, "source_form_id": int(row["id"]),
                          "evolution_basis": variant_basis})
    # Keep resource-only variants visible. They block automatic unique matching
    # until their classification is reviewed, rather than silently disappearing.
    by_name = {row["identifier"]: row for row in species.values()}
    by_name["farfetch-d"] = by_name["farfetchd"]
    unresolved = []
    for slug, entries in local.items():
        if slug not in by_name:
            unresolved.append({"species": slug, "reason": "species_not_in_reference"})
            continue
        for key, _ in entries:
            if (slug, key) in matched_local or key in excluded_aliases or key.endswith("-shiny") or "-mega" in key or key.endswith(("-primal", "-gmax")):
                continue
            unresolved.append({"species": slug, "key": key, "reason": "resource_form_unmapped"})
            forms.append({"key": key, "dex": int(by_name[slug]["id"]), "species": slug,
                          "gender": None, "types": None, "final_evolution": None,
                          "shiny_available": None, "home_available": None,
                          "classification": "resource_requires_review"})
    forms.sort(key=lambda form: (form["dex"], form["key"]))
    if len({form["key"] for form in forms}) != len(forms):
        raise ValueError("Conflicting reference form keys.")
    override_bytes = (data / "home-overrides.json").read_bytes()
    overrides = json.loads(override_bytes)
    if overrides.get("version") != 1:
        raise ValueError("Unsupported HOME override version.")
    by_key = {form["key"]: form for form in forms}
    for key, override in overrides["forms"].items():
        if key not in by_key or not override.get("sources") or not override.get("evidence"):
            raise ValueError("HOME override needs an existing form and review evidence.")
        for field in ("home_available", "gender", "final_evolution", "shiny_available"):
            if field in override:
                by_key[key][field] = override[field]
        by_key[key]["home_review"] = override
        if override.get("match_ability") is True:
            pokemon_id = form_pokemon.get(by_key[key].get("source_form_id"))
            if not pokemon_abilities.get(pokemon_id):
                raise ValueError("Reviewed ability rule needs pinned source abilities: " + key)
            by_key[key]["abilities"] = sorted(pokemon_abilities[pokemon_id])
        if "final_evolution" in override:
            by_key[key]["evolution_basis"] = "reviewed_form_specific_override"
    for form in forms:
        keys = compatibility_keys(form["key"])
        visual_aliases = {
            "rockruff-own-tempo": "rockruff", "greninja-battle-bond": "greninja",
            "zygarde-10-power-construct": "zygarde-ten-percent",
            "zygarde-50-power-construct": "zygarde-fifty-percent",
        }
        if form["key"] in visual_aliases:
            keys.add(visual_aliases[form["key"]])
        if form["species"] in ("mothim", "scatterbug", "spewpa"):
            keys.add(form["species"])
        normal = sorted(keys & sprite_keys)
        shiny_keys = {key + "-shiny" for key in keys}
        if form["species"] == "minior" and form["key"].endswith("-core"):
            shiny_keys.add("minior-core-shiny")
        if form["species"] == "alcremie":
            sweet = form["key"].split("-")[-1]
            if sweet == "sweet":
                sweet = form["key"].split("-")[-2]
            shiny_keys.add("alcremie-" + sweet + "-shiny")
        shiny = sorted(shiny_keys & sprite_keys)
        form["visual_templates"] = {"normal": normal[0] if normal else None,
                                    "shiny": shiny[0] if shiny else None}
    return {"version": 1, "scope": "partial", "kind": "home_reference_catalog",
            "provenance": {"pokeapi": provenance, "local_resource": local_provenance,
                           "visual_assets": visual_provenance,
                           "home_overrides_sha256": hashlib.sha256(override_bytes).hexdigest()},
            "coverage": {"species": len(species), "forms": len(forms),
                         "battle_forms_excluded": len(excluded),
                         "unmapped": [entry for entry in unresolved
                                      if by_key.get(entry.get("key"), {}).get("home_available") is not False],
                         "reviewed_ineligible_unmapped": [entry for entry in unresolved
                                      if by_key.get(entry.get("key"), {}).get("home_available") is False]},
            "policy": {"home_eligibility": "requires_review", "shiny_availability": "unknown",
                       "alpha": "individual_attribute", "gigantamax": "individual_attribute",
                       "visual_detector": {"version": 1, "max_distance": 0.14, "minimum_margin": 0.035,
                                           "minimum_overlap": 0.45, "minimum_competing_forms": 2,
                                           "status": "experimental_candidate_evidence"},
                       "identity_auto_approved": False}, "forms": forms}


def canonical_species_name(name):
    # Explicit local-resource aliases; no fuzzy matching.
    return {"farfetch-d": "farfetchd", "sirfetch-d": "sirfetchd"}.get(name, name)


def canonical_ability_name(name):
    # Compare HOME display labels with pinned slugs without editing observations.
    if not isinstance(name, str):
        return ""
    return name.translate(str.maketrans("ABCDEFGHIJKLMNOPQRSTUVWXYZ ", "abcdefghijklmnopqrstuvwxyz-"))


def match_form(observed, catalog):
    """Unique type/gender matches are evidence candidates, never verified identity."""
    result = {"key": None, "status": "unknown", "evidence": "reference_types_and_gender"}
    if observed.get("gmax") is not False:
        return {**result, "reason": "gigantamax_factor_unknown_or_present"}
    forms = [form for form in catalog["forms"] if form["dex"] == observed.get("dex")
             and form.get("home_available") is not False]
    if not forms or any(form.get("types") is None for form in forms):
        return {**result, "reason": "reference_coverage_needs_review"}
    if any(canonical_species_name(form["species"]) != canonical_species_name(observed.get("name")) for form in forms):
        return {**result, "reason": "species_name_and_dex_disagree"}
    primary, secondary = observed.get("primary_type"), observed.get("secondary_type")
    if not isinstance(primary, str) or not isinstance(secondary, str) or primary == "none":
        return {**result, "reason": "observed_types_unreadable"}
    types = [primary] + ([secondary] if secondary != "none" else [])
    candidates = [form for form in forms if sorted(form["types"]) == sorted(types)
                  and (form["gender"] is None or form["gender"] == observed.get("gender"))]
    if any(form.get("abilities") for form in forms):
        if not all(form.get("abilities") for form in forms):
            return {**result, "reason": "ability_rules_incomplete"}
        ability = canonical_ability_name(observed.get("ability"))
        if not ability:
            return {**result, "reason": "ability_unreadable",
                    "candidates": [form["key"] for form in candidates]}
        result["evidence"] = "reference_types_gender_and_ability"
        candidates = [form for form in candidates if ability in form["abilities"]]
    result["candidates"] = [form["key"] for form in candidates]
    if len(candidates) == 1:
        result.update(key=candidates[0]["key"], status="candidate_requires_review")
    else:
        result["reason"] = "ambiguous_visible_fields" if candidates else "no_matching_reference_form"
        if len(candidates) > 1:
            key = verified_visual_candidate(observed, catalog, candidates)
            if key is not None:
                result.update(key=key, status="candidate_requires_review",
                              evidence="reference_fields_and_two_box_icons")
                result.pop("reason", None)
    return result


def verified_visual_candidate(observed, catalog, candidates):
    """Replay source evidence only; never trust an editable annotation as input.

    This validates the capture report/provenance, not the correctness of its
    pixels. The result still requires explicit identity review.
    """
    observation = observed.get("collection_observation")
    if not isinstance(observation, dict):
        return None
    form = observation.get("form")
    if not isinstance(form, dict) or form.get("fields_confirmed") is not True:
        return None
    visual = form.get("visual")
    if not isinstance(visual, dict) or type(visual.get("policy_version")) is not int or visual["policy_version"] != 1:
        return None
    digest = hashlib.sha256((json.dumps(catalog, sort_keys=True, separators=(",", ":")) + "\n").encode()).hexdigest()
    capture_digest = visual.get("reference_resource_sha256")
    if capture_digest != digest:
        # Native captures fingerprint shipped bytes rather than a serialization.
        # Both representations must describe exactly the same catalog content.
        try:
            raw = (DATA / "HomeCollectionReference.json").read_bytes()
            if hashlib.sha256(raw).hexdigest() != capture_digest or json.loads(raw) != catalog:
                return None
        except (OSError, ValueError):
            return None
    if (visual.get("status") != "candidate_requires_review"
            or any(visual.get(flag) is not True for flag in ("frames_agree", "independent_frames", "templates_complete"))):
        return None
    if type(observed.get("shiny")) is not bool:
        return None
    render = "shiny" if observed["shiny"] else "normal"
    if visual.get("render") != render or any(not candidate.get("visual_templates", {}).get(render) for candidate in candidates):
        return None
    key = visual.get("key")
    keys = {candidate["key"] for candidate in candidates}
    if not isinstance(key, str) or key not in keys:
        return None
    frames = visual.get("frames")
    if not isinstance(frames, list) or len(frames) != 2:
        return None
    for frame in frames:
        if not isinstance(frame, dict) or frame.get("key") != key:
            return None
        scores = frame.get("scores")
        if not isinstance(scores, list) or len(scores) != len(keys):
            return None
        if any(not isinstance(score, dict) for score in scores):
            return None
        if any(not isinstance(score.get("key"), str) for score in scores):
            return None
        if {score["key"] for score in scores} != keys:
            return None
        for score in scores:
            for field in ("distance", "overlap"):
                value = score.get(field)
                if type(value) not in (int, float) or not math.isfinite(value) or not 0 <= value <= 1:
                    return None
        ranked = sorted(scores, key=lambda score: (score["distance"], score["key"]))
        if (ranked[0]["key"] != key or ranked[0]["distance"] > 0.14 or ranked[0]["overlap"] < 0.45
                or ranked[1]["distance"] - ranked[0]["distance"] < 0.035):
            return None
    return key


def identification_report(workspace, catalog):
    import collection
    inventory = collection.validate_workspace(workspace)
    collection.validate_catalog(catalog)
    entries = []
    for index, item in enumerate(inventory["slots"]):
        if item is None:
            continue
        entries.append({"occurrence": item,
                        "physical_box": inventory["scan_context"]["physical_start_box"] + index // 30,
                        "slot": index % 30 + 1,
                        "current_form_key": workspace["annotations"][item]["form_key"],
                        "match": match_form(inventory["pokemon"][item], catalog)})
    return {"version": 1, "kind": "home_form_identification_report",
            "source_validation": collection.planner.source_validation(inventory),
            "catalog_sha256": hashlib.sha256(json.dumps(catalog, sort_keys=True, separators=(",", ":")).encode()).hexdigest(),
            "counts": {"scanned": len(entries),
                       "candidates": sum(entry["match"]["status"] == "candidate_requires_review" for entry in entries)},
            "identity_auto_approved": False, "entries": entries}
