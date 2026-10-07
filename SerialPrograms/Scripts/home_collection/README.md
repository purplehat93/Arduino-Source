# HOME Collection Manager

A separate collection workflow for living forms, shiny living forms, final
evolutions and custom ordered collections. It reuses the verified HOME scanner
and lossless move planner. Individual Pokemon records, collection definitions,
quality annotations and execution progress are separate.

## What works now

The Windows app has a **Collection Manager** panel under Pokemon Home. Its
default action indexes inventory: start in ordinary box view, choose a bounded
contiguous physical range and a new output basename, then Start. First Physical
Box to Index can navigate to a specified physical counter; 0 retains the current
box. The requested range is checked before navigation and its origin confirmed
before scanning. It scans and
returns to the starting box. An optional Collection Plan File runs a separately
prepared plan through the shared verified executor. Verify Plan Only defaults
to true; Maximum Moves This Run defaults to one. Release is unavailable.
Existing output files are refused to protect manual edits. It writes:

- `<basename>.json`: raw observed source slots.
- `<basename>-scan.json`: physical range and scan identity.
- `<basename>-workspace.json`: source inventory plus editable annotations.

Box labels are never identity. Occurrence IDs distinguish every scanned copy
within this job; they are not permanent identities across independent scans.
The shared scanner reads species and shiny status, among other summary fields.
It now reads English Judge ratings and suggests form candidates where local
resource data and observed species/type/gender are unambiguous. Exact IVs are
filled only for exact Judge ratings; unsupported fields stay unknown. Form
identity review defaults to false, and ambiguous forms are never assigned
a standard form implicitly.

The Python tool generates review reports and independently simulated individual
move proposals.
Planning can run from WSL. The GUI does not yet provide an inventory/profile
editor. Default move proposals are previews marked `execution_blocked`.
`plan --enable-execution` requires explicitly reviewed form identities that the
current pinned metadata matcher can re-identify without icon evidence. Manual
form assignments that conflict with the captured or current metadata remain
blocked. Alpha/Gigantamax, icon-only forms and uncertain observations remain
outside this first execution path. For exact-form/quality collection assignments, indistinguishable observed copies
remain in their original review slots. Species-only sorting uses the separate
slot-history policy described below and moves matching-summary copies normally.
Quality annotations are retained for review; they are not independently
verified or ranked by this execution path.

Collection Manager execution uses the saved plan and checkpoint without a
full inventory rescan. **Do not move, add, remove or release Pokemon in the
planned boxes after indexing or while paused.** Execution checks the affected
slots before pickup and after placement, stopping on a mismatch. Verify Plan
Only remains an optional complete scan of the bounded range. It compares stable raw
summary fields and newly confirmed metadata form keys, rather than OCR text
from Judge evidence. Source evidence remains unchanged in plan fingerprints.
Older captured metadata is re-evaluated against the current reference during
planning; live execution requires the current pinned reference. A checkpoint
records confirmed move boundaries and leaves uncertain placements in flight.
Resume skips completed moves. An interrupted move is reconciled by reading
only its source and destination slots; ambiguous or mismatched states stop.
Unrelated slots are not audited during execution or resume.

## Readable files

Copy the empty `catalog.template.json` and `profiles.template.json`. An empty
profile produces an inventory review without assigning collection entries.
It is a useful starting point before any identities have been reviewed.

Catalog entries define the exact form distinctions you want and their order.
Gender variants can have separate keys and a gender constraint; Gigantamax,
Alpha, cosmetic forms and other dimensions need an explicit definition rather
than being conflated. A partial catalog never claims a complete national dex.
For example, this is a definition template, not a verified species catalog:

```json
{
  "version": 1,
  "scope": "partial",
  "forms": [
    {"key": "your-form-key", "dex": 1, "gender": null,
     "final_evolution": null, "shiny_available": null}
  ]
}
```

Set `final_evolution` and `shiny_available` from reviewed catalog data before
using final-form or shiny profiles. Unknown classifications are rejected for
those profiles. The pinned reference catalog below supplies form/evolution
data; HOME eligibility and shiny availability still need review. Do not use
synthetic test fixtures as Pokemon data.

A collection profile places an ordered subset at a physical destination:

```json
{
  "version": 1,
  "collections": [
    {"id": "living", "kind": "living_form", "start_box": 31,
     "start_slot": 1, "form_keys": ["your-form-key"],
     "preferred": {"your-form-key": "pokemon-00001"}}
  ]
}
```

Kinds: `living_form`, `shiny_living_form`, `final_form`, `custom`. Regular
collections require non-shiny copies by default; `shiny: true` requests shiny
copies. Shiny living forms always require shiny copies. `form_keys` defaults to
the catalog's order; final-form and shiny-availability filters apply afterward.
Physical boxes and one-based slots map to the scanned range. Multiple
collections cannot overlap. A copy is assigned to at most one collection.
Missing entries reserve empty gaps, but absence is only reported in the
reviewed scanned range, not across unscanned boxes or unidentified forms.

In the workspace, annotate each occurrence without editing its raw observation:

```json
{"form_key": "your-form-key", "identity_reviewed": true,
 "ivs": {"hp": null, "attack": null, "defense": null,
         "special_attack": null, "special_defense": null, "speed": null},
 "protected": true, "note": "Keep this individual for later review"}
```

`ivs: null` means unread. Individual unknown IVs can also be null. Known IVs
are integers 0..31. These are manual annotations, not verified scanner results.
They do not authorize releasing a Pokemon or prove which indistinguishable copy
is present after another scan. `protected` is a retention note, not a fixed-slot
constraint. Extras are all retained regardless of this flag. Verified quality
selection uses immutable source Judge evidence, with original IVs and Hyper
Training kept separate; manual annotations never become live requirements.

Explicit `preferred` IDs choose which individual fills an entry. Otherwise an
already-correct placement is preferred, then source order. There is no implicit
"best IV" ranking: desirable IVs and other traits depend on the user's purpose.
Review reports include every extra's observed fields and quality annotations.

## Commands

With Python 3.10+ in this directory:

```sh
# Alternatively import an existing indexed Box Sorter scan.
python3 collection.py import boxes.json workspace.json

# After scanning adjacent ranges as separate jobs, verify and merge the chunks
python3 collection.py merge-scans merged-home chunk-01-workspace.json chunk-02-workspace.json

# Inspect assignments, gaps, unknown identities and all retained extras.
python3 collection.py review workspace.json catalog.json profiles.json review.json --csv review.csv

# Export an editable, source-bound identity review sheet.
python3 collection.py identity-review workspace.json catalog.json identities.json

# After explicit decisions, create a new workspace without overwriting the old one.
python3 collection.py apply-identities workspace.json catalog.json identities.json reviewed-workspace.json

# After identity review, simulate a move proposal (live execution remains blocked).
python3 collection.py plan workspace.json catalog.json profiles.json moves.json

# Opt in only when the reviewed source meets the supported live identity contract.
python3 collection.py plan workspace.json catalog.json profiles.json executable-moves.json --enable-execution

python3 -m unittest discover -s . -v
```

The GUI-created workspace can be used directly. Outputs are created exclusively,
so rerunning a command cannot overwrite a reviewed file. Planning is blocked
while form identities remain unresolved. Every original occurrence is conserved
in the final layout, including extras. Extra copies remain in their current
slots where possible; displaced copies are assigned available review storage.
Insufficient storage causes an error rather than deleting or truncating extras.
The plan is independently replayed and carries the physical source fingerprint.
Quality/profile annotations never alter the raw source used for live verification.

For a long inventory, scan smaller contiguous ranges with a distinct output
basename for each run. A completed chunk is saved independently if a later run
is interrupted. `merge-scans` verifies each chunk against its raw export, requires
matching HOME box counts and adjacent non-overlapping physical ranges, and writes
a new combined source, scan context and workspace without overwriting inputs. It
preserves every occurrence and its annotation. This supports manual pause/restart
between chunks; it does not checkpoint progress inside a running scan.

The optional CSV includes every individual, its physical position, collection
assignment, retained-extra status and quality fields. Unknown IVs remain blank.
It is a readable report for spreadsheet review; the workspace/profile JSON files
remain the authoritative editable inputs.

## Next stages

1. Verified form identification and a versioned catalog of forms/evolutions,
   including gender, variant and shiny-availability definitions.
2. Resumable inventory scanning suitable for about 4,900 Pokemon; preserve
   partial scans and explicitly validate merged physical ranges and stale data.
3. In-program inventory/profile editor and individual-copy selection, including
   reviewable quality fields. Extend the current English Judge reader to other
   languages/game views; keep uncertain observations unknown.
4. Validate the new bounded metadata plan executor live, including one-move
   pause and resume. Extend verification to icon-dependent forms only after
   representative validation; validate broader held overview routes and workloads.
5. Optional release workflow with explicit individual decisions, protections,
   a concrete reviewed release manifest and fresh per-target checks. Release
   before movement can avoid wasted moves, but must produce a verified journal,
   then rescan and build a new move plan from the surviving collection. Never
   reuse a pre-release source fingerprint or blindly repeat an uncertain release.

Release execution is not implemented in this version. "Not required by this
profile" is not a release decision. Every extra remains `keep_for_review`.

## Manual identity review

The identity sheet includes each occurrence's physical coordinates, raw
observations and current annotations, plus editable `decision`, `form_key` and
`review_note` fields. Every decision initially says `leave_unchanged`; candidates
are never automatically confirmed. Set `confirm` with a catalog form key and a
note describing the identification basis, or `unknown` with a null key to clear
an identity. Leave the other fields intact. Extend the catalog before exporting
a fresh sheet if the required form is missing.

Applying decisions requires the same source fingerprint, catalog, observations
and annotations used to export the sheet. Every occurrence must appear exactly
once. Species, gender and shiny constraints are checked, quality/protection
annotations are retained, and a new workspace is written exclusively. Manual
confirmation is a review annotation, not verified visual detection or persistent
identity across scans. Collection execution remains blocked.

## Pinned reference catalog and identification

```sh
python3 collection.py reference-catalog catalog.reference.json
python3 collection.py identify workspace.json catalog.reference.json identification.json
```

`catalog_data/` contains a pinned PokéAPI snapshot, file checksums, its BSD
license, and the installed local form map used only for compatibility keys.
Generation is offline and reproducible. The snapshot covers 1,025 species;
the catalog includes 1,523 candidate entries, including cosmetic/gender
distinctions and 12 explicitly unmapped resource entries. It excludes 157
battle-form source records. This is a community reference, **not** a verified
complete list of forms storable in HOME. HOME eligibility stays unknown except
explicit reviewed overrides. Shiny availability now marks a small sourced set
of unobtainable shinies false; all other entries remain unknown, not assumed shiny. Alpha and Gigantamax factor are individual attributes, rather than
extra species or battle transformations silently becoming living-dex entries.

Evolution classification respects source-form and gender restrictions where
explicitly present. Unqualified evolution rules on nondefault forms stay
unknown. The classification spans the snapshot's evolution rules across games;
it does not mean an individual can evolve in the currently selected game.
Normal/Galarian Corsola, Basculin stripe variants and male/female Combee are
regression cases. Collection inclusion still belongs to reviewed profile files. Reference entries
with unknown/false HOME eligibility are rejected for collection placement; mark
eligibility explicitly after review instead of treating them as missing Pokemon.

Identification uses exact observed dex/species, normal types and gender. It
returns a candidate only when exactly one reference form fits; all look-alike
forms remain unknown. Missing reference classifications block matching for that
species. The report does not edit annotations or auto-approve candidates.
The Windows scanner uses the same rule, validates the shipped resource checksum
and confirms dex, types, gender and Gigantamax status on a second summary frame
before recording evidence. Agreement confirms observed fields, not a complete
visual form identity. Missing/corrupt reference resources retain unknown forms.
Deploy `HomeCollectionReference.json` to `Resources/Pokemon/` alongside the
PokéAPI license; changed snapshots require updating the scanner's pinned digest.

The saved two-box export and a fresh Windows live regression both yield the
same 52 candidates and eight unknown entries. The live run confirmed all 60
records across frames, matched all prior accepted Judge ratings and completed
with zero moves/releases in about four minutes. This bounded evidence does not
validate the full catalog or unknown visual forms.

## Judge observations

Collection Manager reads the English HOME summary Judge screen using Y display
cycling. It recognizes numeric stats, base points, Judge and optional Condition
screens from their visible controls. Each display transition waits for two
matching frames and logs measured queue/confirmation time; it does not wait
for the wrong screen detector to time out. It confirms the screen and species, reads each of six labels from two
frames, and accepts only matching recognized ratings. It returns to numeric
summary view to restore the language badge before advancing. Unknown labels, unavailable
Judge and unsupported languages retain unknown values. Some game-specific views
(such as the observed Legends: Arceus view) do not offer the Judge cycle; these
remain unavailable rather than interpreting effort levels as IVs. Raw readings, per-stat
status, ranges and Hyper Training are saved in `collection_observation` on the
source record; import preserves this evidence for the same occurrence.

`ivs` contains exact values only for No Good (0), Fantastic (30) and Best (31).
Decent is 1–15, Pretty Good 16–25 and Very Good 26–29. Hyper Training records
the displayed training indicator with unknown original IV, never an invented
31. CSV adds Judge rating and training columns independently of exact IVs.

Form candidates use the pinned reference catalog described above. They remain
unreviewed; candidate agreement does not establish a complete visual form
classifier or identity across scans. Ambiguous forms and Gigantamax-factor
individuals remain unknown.

Create a partial editable catalog from a new workspace:

```sh
python3 collection.py catalog workspace.json catalog.observed.json
```

The observed-only catalog leaves final-evolution and shiny-availability fields
null until supplied from a reviewed catalog. This command creates candidates, not an authoritative living
form/evolution database. Existing review and execution gates remain in force.


### HOME eligibility correction for Pichu

`catalog_data/home-overrides.json` stores reviewed eligibility with source
links and a digest in catalog provenance. Ordinary Pichu is HOME-compatible;
Spiky-eared Pichu cannot leave HeartGold/SoulSilver and is excluded from HOME
matching. Both male and female ordinary Pichu resolve to the ordinary form.
Only explicit `home_available: false` excludes a candidate; null eligibility
stays conservative. This changes matching evidence, not identity approval.
The corrected audit of the saved 60-slot scan has 60 candidates and zero
ambiguous entries. Prior raw exports/reports are retained, and corrected
artifacts use a `pichu-fix` suffix. Windows Release built; 40 collection tests
and native/Python parity across all 60 captured records passed. No new console
scan was needed because the visual field readers were unchanged.

### HOME catalog cleanup and ability rules

The subsequent catalog cleanup and reviewed ability rules are documented in
[FORM-COVERAGE.md](FORM-COVERAGE.md). Run `python3 form_coverage.py` for current
offline counts. All 44 tests pass, including whole-reference native/Python
parity when `g++` is available. Updating a catalog does not approve identities
or overwrite existing scans; source-bound review sheets must use the intended
catalog hash. Previously saved scans need no rescan solely to apply new catalog
rules, but ability-dependent forms require verified readings in future scans.

### Visual form candidates

The first native two-frame box-icon detector is integrated into Collection
Manager. It only refines unresolved metadata alternatives and never approves
identities. Pinned templates, complete alternative coverage, an absolute score,
a winning margin and independent-frame agreement are required. Shared renderings
and unreadable icons remain unknown. See [VISUAL-RECOGNITION.md](VISUAL-RECOGNITION.md)
for the 53 tests, offline reproduction commands and the distinction between
synthetic regression and actual console validation.


## Native inventory checkpoints

Collection Manager saves an atomic inventory checkpoint after each complete box.
Resume Inventory Index loads that checkpoint, retains the original physical range,
and skips completed boxes. An interrupted box may be scanned again; its partial
observations are not committed as a complete box. Use the same output basename
for inventory resume. The native program scans the whole configured range without
an external runner. Original inventory exports remain immutable evidence.

## Complete species sorting

`species-plan WORKSPACE OUTPUT` creates a National Dex keep-all plan using
`metadata_species_v2` and `duplicate_policy=tracked_slot_history`:

```sh
python3 SerialPrograms/Scripts/home_collection/collection.py species-plan \
  /path/to/workspace.json /path/to/species-plan.json
```

Every confirmed copy is included, including copies with matching summary fields.
The executor tracks occurrences through their starting physical slots and each
journaled swap. Occupied-slot swaps work with completely full boxes and require
no spare staging slot. Explicitly protected copies and unconfirmed observations
remain stationary; inspect that list before executing a plan.

The planner omits swaps between equal live summary records and adjusts equivalent
target labels to match actual movement history. Native loading rejects such swaps
independently: identical before/after observations could not reconcile an
interrupted swap. It validates conservation, move preconditions, source hash,
physical bounds and species ordering. Old species-v1 plans retain their original
stationary-copy policy.

This mode does not certify forms or IVs, rank copies or release Pokemon. Summary
checks compare the stable fields that were read, while annotations follow the
tracked occurrences. Manual replacement/rearrangement of matching-summary copies
cannot be detected. Leave the planned boxes unchanged between runs.

## Executing and resuming a plan

In Collection Manager, set Collection Plan File, a fresh Collection Output
basename, Verify Plan Only off and a bounded Maximum Moves This Run. The first
execution of a new plan has Resume Collection Plan off. Subsequent runs load
that same plan and its `.checkpoint.json` with Resume on. Set the move limit to
0 for all remaining operations once the setup has been validated. Use a fresh
output basename for each execution run; preserve the plan/checkpoint pair.

Per-move execution reads the two affected slots before pickup and after placement.
It stops on mismatched or ambiguous observations. Resume reconstructs committed
layout by replaying completed operations and reconciles only an interrupted
operation, without a full rescan. Verify Plan Only provides an optional full
bounded audit. Resuming never means continuing from a regenerated initial plan.

The feature is an alpha: synthetic tests and bounded live operations validate
individual behaviors; a complete long-running sort and end-to-end speedup still
need live verification. Planning currently requires this offline CLI; the native
app executes the resulting plan. In-app profile editing, automatic deletion,
controller passthrough and a headless vision workflow remain future work.
