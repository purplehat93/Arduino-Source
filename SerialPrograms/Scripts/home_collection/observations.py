"""Conservative interpretation of HOME observations; never rank or release copies."""
from copy import deepcopy

STATS = ("hp", "attack", "defense", "special_attack", "special_defense", "speed")
RATINGS = {"No Good": (0, 0, False), "Decent": (1, 15, False),
           "Pretty Good": (16, 25, False), "Very Good": (26, 29, False),
           "Fantastic": (30, 30, False), "Best": (31, 31, False),
           "Hyper trained!": (None, None, True)}


def validate_judge(judge):
    if not isinstance(judge, dict) or judge.get("status") not in (
            "unknown", "unsupported_language", "unavailable_or_unreadable", "partial", "read"):
        raise ValueError("Invalid Judge observation status.")
    stats = judge.get("stats")
    if judge["status"] not in ("partial", "read"):
        if stats is not None:
            raise ValueError("Unavailable Judge must not contain accepted ratings.")
        return None
    if not isinstance(stats, dict) or set(stats) != set(STATS):
        raise ValueError("Judge observations require all six stat records.")
    exact, read = {}, 0
    for stat, record in stats.items():
        if not isinstance(record, dict) or record.get("status") not in ("read", "unreadable"):
            raise ValueError("Invalid Judge stat status.")
        if not all(isinstance(record.get(key), str) for key in ("raw", "raw_confirmation")):
            raise ValueError("Judge observations need both raw readings.")
        exact[stat] = None
        fields = (record.get("iv_min"), record.get("iv_max"), record.get("hyper_trained"))
        if record["status"] == "unreadable":
            if record.get("rating") is not None or fields != (None, None, None):
                raise ValueError("Unreadable Judge must retain unknown values.")
            continue
        expected = RATINGS.get(record.get("rating"))
        if expected is None or fields != expected or type(fields[2]) is not bool:
            raise ValueError("Judge rating disagrees with its IV range or Hyper Training.")
        if any(value is not None and type(value) is not int for value in fields[:2]):
            raise ValueError("Judge IV bounds must be integers or null.")
        read += 1
        lo, hi, trained = fields
        if lo is not None and lo == hi and not trained:
            exact[stat] = lo
    if (judge["status"] == "read") != (read == 6):
        raise ValueError("Judge completeness disagrees with stat observations.")
    return exact


def annotations_from_observation(observed):
    annotation = {"form_key": None, "identity_reviewed": False, "ivs": None,
                  "protected": False, "note": ""}
    evidence = observed.get("collection_observation")
    if evidence is None:
        return annotation
    if not isinstance(evidence, dict) or type(evidence.get("version")) is not int or evidence["version"] != 1:
        raise ValueError("Unsupported collection observation version.")
    form = evidence.get("form")
    if not isinstance(form, dict) or form.get("status") not in ("unknown", "candidate_requires_review"):
        raise ValueError("Invalid observed form status.")
    key = form.get("key")
    if (form["status"] == "unknown" and key is not None
            or form["status"] == "candidate_requires_review" and (not isinstance(key, str) or not key)):
        raise ValueError("Observed form candidate disagrees with its status.")
    judge = evidence.get("judge")
    exact = validate_judge(judge)
    if evidence.get("ivs") != exact:
        raise ValueError("Exact observed IVs must agree with Judge evidence.")
    annotation.update(form_key=key, ivs=deepcopy(exact), iv_judge=deepcopy(judge))
    return annotation
