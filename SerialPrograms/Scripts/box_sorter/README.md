# Offline HOME box sorter planner

Prototype for comparing controller travel before integrating a new planner into
the C++ HOME programs. Python 3.10 or newer, standard library only. Run from this
directory; on Windows use `py -3` instead of `python3` if appropriate.

```sh
python3 planner.py import boxes.json inventory.json
python3 planner.py plan inventory.json plan.json --target-export boxes-sorted.json
python3 planner.py simulate plan.json
python3 planner.py benchmark --boxes 200 --seed 1
python3 -m unittest discover -s . -v
```

Input exports use `save_boxes_data_to_json`: one object per slot, zero-based
`index`, `box`, `row`, `column`, with metadata omitted for empty slots. Complete
boxes are required. Export order may vary, but coordinates must agree. Paths
may be absolute. Output files must not already exist.

Without a target option, `plan` uses stable ascending dex order, with empty
slots at the end. To reproduce existing sorting preferences, supply the existing
sorter's `-sorted.json` rather than relying on the default order. The planner
also accepts the Living Dex export format, but does not generate Living Dex
templates or combine its separate living/reject ranges yet. Each input describes
one contiguous box range. Coordinates are relative to that range.

HOME Box Sorter now writes a companion `<basename>-scan.json` with a scan ID,
physical start/end box numbers, total box count and an explicit coordinate
basis. For example, physical boxes 31–32 export relative boxes 0–1. Physical
identity comes from the ordinary box counter, never an editable box label.
The importer automatically reads this companion file beside its source export;
`--scan-context` can select it explicitly. Keep it with the source export. Plans
from indexed inventories carry the physical range and an SHA-256 fingerprint
of the ordered observed metadata at each slot. Box labels, job-local occurrence
IDs, filenames and scan timestamps are excluded from this fingerprint.

Check an existing plan against a fresh Dry Run of the same physical range:

```sh
python3 planner.py verify-source plan.json fresh-boxes.json
```

The fresh export needs its own `fresh-boxes-scan.json` companion. Verification
rejects a different physical range, observed metadata/content changes, changed
slot order or edits to the plan's recorded source. It also simulates the plan's
preconditions. It performs no console actions and writes no execution permit.
Legacy unindexed plans remain usable for offline simulation but cannot pass
this physical-source check. Identical observed Pokemon records cannot be
distinguished by this fingerprint; it is not a permanent Pokemon identity.

This command checks the initial source, not a partly executed checkpoint.
The Box Sorter GUI also supports indexed saved plans:

1. Set **Saved Plan File** to the plan's JSON path. Start in ordinary box view
   with no Pokemon held. The program locates the recorded physical range and
   scans it before attempting any pickup.
2. Leave **Dry Run** enabled to check the source without executing or writing
   a checkpoint. Disable it to execute. **Plan Moves This Run** defaults to one
   verified move; zero executes all remaining moves.
3. Enable **Resume Saved Plan** to continue using the adjacent
   `<plan>.checkpoint.json`. A fresh scan must match the recorded move boundary.

Checkpoints are written atomically before pickup and after verifying both
affected slots through occupancy and summary reads. A bounded run pauses
unheld after a verified move. Interrupted moves are reconciled against their
before and after layouts; ambiguous or unexpected layouts stop execution.
Resolve a held Pokemon manually before restarting. Do not delete a checkpoint
and blindly repeat an interrupted move. Existing checkpoints cannot be
overwritten by starting a new execution with resume disabled.

The GUI supports individual swaps and moves to empty slots. Group moves are
rejected before scanning or pickup. Held navigation verifies the physical box
and destination hand marker. It selects Box Spaces for longer 200-box HOME
routes when the estimated route cost beats L/R, retaining L/R for short trips.
A live 70-box held round trip with source identity verification passed, taking
33.544 seconds through Box Spaces versus 93.174 seconds with L/R on the tested
setup. These are travel timings, not a whole-collection speedup; broader sorting
and arbitrary held endpoints still need validation. Available metadata cannot distinguish identical Pokemon records, and a
matching scan assumes the layout stays under the automation's control during
the run; it is not a permanent identity or execution permit.


## Edit a target

Copy `inventory.json` to `target.json`. Rearrange IDs and `null` entries in its
`slots` array, keeping every ID exactly once and retaining the same array length.
Then run:

```sh
python3 planner.py plan inventory.json custom-plan.json --target target.json
```

Keep the original inventory unchanged: it describes the starting state. IDs
identify scanned occurrences within this job, not globally identifiable Pokemon.
Identical Pokemon receive separate IDs. Legacy target exports match exact metadata
and preserve already-correct duplicates first; edited ID layouts specify exact
occurrences. Metadata edits, omitted Pokemon, extra Pokemon and repeated IDs are
rejected. Removing a Pokemon never requests release.

## Strategies and validation

* `destination_first`: place each occupied target in slot order. This models the
  existing sorter's movement policy with indexed lookups. It also supports holes
  anywhere in a custom target. Unlike C++ metadata equality, explicit ID targets
  distinguish duplicates, so comparisons are for the chosen identity assignment.
* `cycle_local`: choose the next misplaced Pokemon by travel cost, place it, then
  follow the displaced Pokemon to its destination until the chain closes or an
  empty slot ends it. Empty slots participate naturally; no reserved staging box
  is required. This is a heuristic, not an optimal route solver.
* `nearest_swap`: preserve the minimum swap count of destination-first, but
  choose each next destination by the estimated cursor travel for that swap.
  This greedy ordering can reduce travel on shuffled layouts; it does not
  guarantee the globally shortest route.
* `lookahead2` is an experimental route-ordering method. It scores a bounded
  shortlist by the travel for the next two independent swaps. It remains
  available to tests and direct experiments but is excluded from automatic plan
  selection because it lost at both 30- and 200-box scale under the current model.
* `batch_first`: when a box is completely empty in both source and target, use
  it as a reusable staging area for full six-Pokemon rows. Move the current row
  to empty scratch slots, gather its desired Pokemon into the workspace, carry
  the row to its now-empty target, then let the baseline finish remaining moves.
  This first staging heuristic is deliberately conservative and may be slower;
  it stays in the comparison so the planner can reject it.

All candidates are independently replayed. Each move records expected source and
destination IDs. Simulation rejects inconsistent preconditions and checks the
exact final layout and inventory conservation. The lower estimated-time plan is
selected, retaining the baseline on ties. Results are deterministic.

The saved plan includes its source inventory, target, cursor, cost model,
comparison metrics and selected moves. `simulate` replays those moves rather
than trusting the saved metrics. The GUI validates these plan preconditions
independently and stores execution progress in a separate checkpoint.

## Cost model and limits

See [TIMING.md](TIMING.md) for the repeatable console protocol to measure these
costs and replace the current assumptions.

The model follows current `PokemonHome_BoxNavigation.cpp`: linear L/R box travel,
the existing row/column paths, 80 ms presses, `GAME_DELAY`, and the extra 240 ms
for box changes and Y presses. On either leg of a swap, it compares linear travel
with Box Spaces: return to the box start, open the overview, page in 30-box steps,
navigate the six-by-five grid, enter the box, then move to the exact slot. The
user confirms that Box Spaces works while holding one Pokemon; exact timings and
overview-grid costs remain unmeasured. The planner's 240 ms default matches the
current sorter setting; it is not a measured HOME response time or a Switch 1/2
profile. Pass `--game-delay-ms` to match another configured setting and
`--start-cursor` to specify the zero-based initial slot (default 0).

The group model includes rectangular selection, empty destinations, Box Spaces
page changes in 30-box steps, and the six-by-five box overview. A 1x2 group, an
empty destination, and one 30-box jump while carrying were verified on console.
The user confirms the same overview navigation works while carrying one Pokemon.
Other shapes, very long jumps, exact controller timing, and overview-grid costs
remain unmeasured.
Estimates exclude scanning, capture latency, verification, saving, retries and
hardware overhead; they are not measured console speedups. The synthetic
benchmark shuffles Pokemon in the occupied range and leaves the remaining tail
empty; it does not represent every collection. A full-occupancy benchmark has no
workspace.

### Expected gain and adaptive timing

A same-layout 200-box, 90%-occupied comparison estimated 290,651 seconds
(80.7 hours) for destination-first linear travel and 39,852 seconds (11.1 hours)
for nearest-swap with Box Spaces on both legs: an estimated 86.3% reduction, or
about 7.3x faster. This is a synthetic route estimate at the current 240 ms
`GAME_DELAY`; the overview actions and HOME settling time have not been timed on
console. Treat it as a hypothesis to validate on the user's setup, not an
expected guarantee. Changing the delay changes route costs and can also change
which navigation method the planner selects.

The app now measures queue and detector time for summary-screen transitions.
The regular Box Sorter also checks the red cursor marker after an unheld,
within-box cursor move. Confirmed moves lower that run's local cursor delay by
5 ms to an 80 ms floor. A mismatch stops before the next slot interaction.
Held moves and box changes still use the configured delay because their target
state is not confirmed yet. The learned value lasts only for the current run;
the option and planner cost model are unchanged. These timings are a first
adaptive step, not a measured speedup. See [TIMING.md](TIMING.md) for the next
verification work and limits.

Next, verify the cursor-marker coordinates on the real app and add independent
box/overview destination detection. Then extend learning to held movement and
box navigation, persist setup-specific measurements, and connect measured costs
to route planning. Integrate a strategy only when its complete simulated route
beats the baseline. Crash reconciliation and checkpoints remain future work.
Release would require a separately designed explicit workflow.

## Full-size navigation benchmark

200 boxes, seed 1, `GAME_DELAY=240 ms`, cursor 0. Compare nearest-swap with the
experimental lookahead2; both routes are replayed to verify the exact target.
Planning time is route generation plus simulation on the current development
machine.

| Occupancy | Nearest estimate | Lookahead estimate | Lookahead regression | Swaps (both) | Planning time (nearest / lookahead) |
| --- | ---: | ---: | ---: | ---: | ---: |
| 50% | 20,357.52 s | 21,042.00 s | 3.36% | 2,992 | 21.8 / 23.1 s |
| 90% | 39,691.20 s | 41,035.52 s | 3.39% | 5,386 | 72.0 / 74.0 s |
| 100% | 44,960.32 s | 46,520.32 s | 3.47% | 5,985 | 89.5 / 89.4 s |

Nearest-swap beats lookahead2 in all three pairwise cases; the gap is 3.4–3.5%.
It also won a 30-box seed-1 run under the corrected model. Automatic planning
therefore skips the slower lookahead search. Nearest-swap estimates 5.7, 11.0,
and 12.5 hours of sorting actions at 50%, 90%, and 100% occupancy. These are not
console timings: overview action delays and grid costs still need measurement.
The estimates fall substantially when Box Spaces is allowed on the held-Pokemon
leg; the user confirms this capability, but its timing has not yet been measured.


## Native executor safety tests

With Qt 6 and Visual Studio x64 Build Tools installed, run from PowerShell:

```powershell
.\test_saved_plan_execution.ps1 -QtRoot 'C:\path\to\Qt\6.8.3\msvc2022_64'
```

The harness includes the production executor and substitutes a modeled console.
It checks plan rejection, output-path collisions, Dry Run, bounded execution,
resume, completed checkpoints, interrupted move reconciliation, mismatched
layouts, failed postconditions, held placement failure and ambiguous duplicates.
It cannot validate camera detectors or actual controller/console behavior; the
bounded live restoring test is recorded separately in TIMING.md.
