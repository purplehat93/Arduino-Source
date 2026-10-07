# HOME visual form recognition — 2026-10-02

The first native icon detector is implemented. It supplies **unreviewed form
candidates**, not verified identities. Most cosmetic families still need real
console examples before claiming reliable coverage.

## How the scanner uses it

Collection Manager captures two distinct video frames at the first box slot,
where the tooltip sits above the icon grid. Existing navigation already parks
the cursor there. Capture adds a 150 ms wait per box and no navigation inputs.
Frames must show ordinary box view and the same first-slot cursor, with increasing
capture timestamps. Otherwise metadata scanning continues without visual evidence.

Summary Dex/types/gender and reviewed ability rules narrow the candidates first.
Only unresolved alternatives are passed to the visual detector. It requires:

- The exact pinned HOME atlas JSON and PNG, with templates for every remaining
  candidate and the observed normal/shiny rendering.
- At least two competing forms. A single template cannot verify identity.
- A readable 16:9 image at 720p or greater, with enough icon foreground.
- Normalized RGB distance at most 0.14, foreground overlap at least 0.45, and
  a distance margin of at least 0.035 over the nearest alternative.
- The same accepted candidate in both independently captured frames.

The implementation searches small position/scale variations and normalizes
moderate dimming using the pale background. Distances are not probabilities.
These are initial conservative thresholds, not a statistically calibrated
guarantee across every form, capture card or resolution.

The native export retains both score lists, rendering, agreement flags, reference
resource hash and candidate key. Offline identification validates this source
evidence against the current catalog and score policy. Edited annotations are
never substituted for capture evidence. Existing explicit identity-review,
HOME eligibility and execution gates remain in force.

## Validation and limits

- 53 suite tests passed, including native/Python metadata parity, malformed/stale
  visual report rejection and native pixel-policy tests at 720p/1080p/1440p.
- Synthetic blank/black feeds, single templates, duplicate renderings, insufficient
  confidence and disagreeing/nonindependent frames cannot establish a candidate.
- A 27-icon synthetic family regression accepted 16 correct forms and no wrong
  forms. Accepted examples included Unown, Vivillon, normal Minior colors,
  Furfrou, Tatsugiri and Zygarde shapes. This tests the algorithm on atlas-derived
  renderings; it is **not live accuracy** for those families.
- Tea-form pairs, shared shiny Minior/Alcremie renderings, close ordinary
  Alcremie combinations and one close Deerling example remained unresolved.
- On two existing real screenshots, the final competing-form policy accepted
  the three unobstructed normal/Hisuian Typhlosion icons in each, agreeing with
  independently read summary types. It rejected a badge-obstructed icon and tiny
  gender distinctions. Ordinary single-form species remain metadata candidates;
  they are not counted as visual successes.

The initial single-template probe reported 24 matching icons, but a wrong-species
negative exposed that such a score alone is insufficient. The final policy
requires competing alternatives; the initial count is not a supported coverage
claim. Species identity still comes from verified summary fields, not this icon
detector. A visual result does not independently certify the summary OCR.

273 of 283 ambiguous reference entries have complete normal template sets;
257 have complete shiny sets. These counts include shared/indistinguishable
images and do not imply successful recognition, shiny availability or HOME
eligibility. Metadata-only coverage stays 1,156 unique / 283 ambiguous / 84
excluded. No synthetic result is silently added to that measured baseline.

## Windows live regression

The rebuilt Release completed a one-box Collection Manager scan of physical
box 78 at 14:42:30–14:44:33 on 2026-10-02. It exported 30 occupied slots,
30 unreviewed form candidates and 29 complete Judge readings; the remaining
Judge result was unchanged from the earlier scan. All 30 records retain the
independent-box-frame flag. Ordinary observation fields and Judge evidence
match the earlier box-78 records exactly. GUI workspace and offline import
are identical; offline re-identification reproduces all 30 keys.

The log reports normal completion, moves=0 and releases=0. This validates
capture/export integration and metadata regression, **not live accuracy on
ambiguous cosmetic forms**, because these 30 records already resolve by
metadata. The app remains idle in box 78 at its first slot. Private artifacts
use `home-collection-visual-regression-20261002` in the Windows Release folder;
prior scans/workspaces remain untouched. Runtime reference hash was verified.

## Reproduce offline checks

Run from this directory, replacing the resource directory with the installed
`Resources/Pokemon` path. Pillow and g++ are required for pixel checks.

```sh
python3 -m unittest discover -s . -q
python3 visual_validation.py --resources /path/to/Resources/Pokemon --output /tmp/visual-families.json
python3 visual_detector.py --resources /path/to/Resources/Pokemon --frame /path/to/box.png --workspace /path/to/workspace.json --physical-box 77 --output /tmp/visual-box.json
```

The last command needs an actual matching box screenshot and workspace. It is
an offline diagnostic, not two-frame identity evidence and not a command that
changes annotations. Keep private screenshots and source inventories outside
the source repository and vault.

Implementation: `PokemonHome_CollectionVisualPolicy.h` contains the shared pixel
algorithm. `visual_probe.cpp` runs that same code offline; `visual_detector.py`
and `visual_validation.py` prepare inputs. `CollectionObservation.inc` performs
native frame agreement and export; `reference_catalog.py` validates evidence
when re-identifying a saved scan. `catalog_data/visual-resource.json` pins assets.

## First prospective cosmetic captures

On 2026-10-02, six distinct real occurrences were evaluated with the existing
thresholds frozen: ordinary Unown F, shiny Unown T and X, two shiny Trash Cloak
Burmy and ordinary red-flower Florges. Expected labels came from assistant manual
review of visible letter/cloak/color against named reference icons, rather than
the scorer's output. Species/render were confirmed by fresh scanner observations
for boxes 96/98 and the selected-Pokemon detail bar for Unown F. This is diagnostic
label review, not user identity approval. These examples were not calibration
inputs; repeated frames of one occurrence do not increase the sample count.

The offline native pixel probe accepted all three Unown correctly, accepted no
wrong forms and left the other three occurrences unresolved. Margins were
0.223 / 0.087 / 0.149 for Unown F/T/X, 0.017 / 0.012 for the Burmy and 0.0048 for
Florges. Florges ranked red ahead of orange, but without sufficient separation.
Do not lower the global margin to force these cases through.

Fresh native scans of physical boxes 98 and 96 completed normally at 15:06:06
and 15:09:01, exporting 25 and 27 occupied slots respectively, with moves=0 and
releases=0. Both shiny Unown passed independent-frame agreement in the app;
both Burmy and Florges stayed unknown in both frames. GUI/offline workspace
parity and offline re-identification of these five ambiguous occurrences passed.
Unown F was checked offline only, not with a full native inventory scan.

Private Release artifacts use `home-collection-cosmetic-box96-20261002`,
`home-collection-cosmetic-box98-20261002` and
`home-collection-real-cosmetic-probe-20261002` (JSON plus WSL replay script).
No annotations were approved or rewritten. The app is idle in ordinary physical
box 94 with Unown F selected and no Pokemon held. Earlier box-78 idle state above
is historical. No detector code, resource pins or thresholds changed this pass.

This small sample establishes real Unown successes and concrete cloak/color
limits; it does not validate all letters, cosmetic families or capture setups.
Next: collect contrasting cloak and flower colors as separate occurrences, then
evaluate a family-specific color/foreground comparison against held-out examples.
Additional Vivillon, Furfrou, Deerling, Minior and Alcremie captures remain needed.
Keep unsupported/look-alike distinctions unresolved and preserve review gates.


### Focused color experiment — 2026-10-02

Offline only: `PokemonHome_CollectionVisualColorExperiment.h` and the pixel
probe's `--color-experiment` option compare the differing colored regions for
Flabebe, Florges and Burmy. Existing shape limits remain mandatory. Production
Collection Manager and identity import still use the original visual policy;
experimental chromaticity scores must not be serialized as version-1 RGB scores.
The reference sprite atlas already supplies templates; owning every Pokemon is
not required. Real-console captures validate rendering rather than build an atlas.

All 13 examples inspected during development are labeled development, including
examples previously labeled holdout before metric revisions. Preserving existing
baseline matches and using color only as a fallback recognizes 12/13 development
examples correctly, with zero wrong accepted matches and one unresolved ordinary
red Florges. A subsequently collected, untouched box-22 sample contains three
shiny Burmy (slots 7/8/9: Plant/Sandy/Trash). The frozen experiment recognizes all
three correctly; baseline recognizes only Plant. This is a small sample, not
proof of complete form coverage. Floette remains outside this experiment because
its eligible template set is incomplete.

Private evidence: Release/home-collection-color-experiment-20261002.json, with
separate development/heldout labels and baseline/experiment/combined results.
Fresh heldout grid: screenshot-20261002-154628194836.png; selected detail captures
confirm Burmy/shiny separately. The three occurrences are distinct from box-96
Burmy used during development. No Pokemon moved or released. App remains idle
in physical box 22, first slot. The original suite plus three synthetic focused-color gate tests passed (56 total).
These exercise close hues, wrong-shape rejection, unsupported families, and incomplete/mixed candidates.

Next: gather broader held-out examples only for families where offline limits or
real rendering still fail, then design separate typed color evidence and native/
Python verification parity before any Windows integration. This experiment is
still offline and must not be treated as saved identity evidence. Do not increase
thresholds merely to accept the remaining ordinary red Florges. Existing atlas
references support broad offline tests without owning each Pokemon.
