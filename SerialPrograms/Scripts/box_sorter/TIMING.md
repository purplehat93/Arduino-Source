# HOME navigation timing and verification

Navigation timing is measured on the actual controller, capture card and HOME
screen. Console model is setup metadata, not a delay profile. Model route costs
are not live wall-clock forecasts and do not include all identity-verification
work. Compare complete jobs with the same inventory and target before claiming
a speedup.

## Calibration

Navigation Timing Test provides a short representative calibration and optional
extended validation. Start unheld in HOME; the short run measures Box Spaces page
changes, overview grid movement, ordinary box changes and within-box navigation.
Routes include both directions, horizontal/vertical movements, page boundaries
and the partial last page. Initial setup actions use individually confirmed,
conservative delays. Measurement routes queue inputs before checking the endpoint
so confirmation cannot mask a missed command.

The lowest tested passing delay gets a safety margin. A readable validation miss
backs off and retries from a confirmed state. Unreadable state or failure at the
conservative baseline ends the test. Completion attempts to restore the original
selection; manual interruption cannot guarantee restoration. Extended testing
covers more routes and repeated rounds when diagnosing a persistent problem.

## During sorting

The shared navigation code confirms selected physical boxes and cursor slots
with detectors/OCR, independent of editable box names. Box Spaces offers long
jumps when the route estimate favors it; ordinary L/R remains available. Holding
a single Pokemon uses the independently checked held route. The original range
still bounds every planned source/destination.

Slot confirmation requires a centered, tapered pointer and repeated agreement.
Changed/unreadable frames reset agreement. Bounded observation retries add no
extra navigation commands. Clean confirmations gradually lower the local delay;
late confirmations back off. Controller scheduling overhead is excluded from
game-settling backoff to avoid progressive slowdown from CPU/capture load.
A detector failure stops before the next slot interaction.

The inventory scanner and saved-plan executor use stable observations at scan
entry, after returning from a summary, and after navigation. A selected Pokemon's
artwork is not cursor evidence. Dim frames use brightness-relative checks;
black/off-console frames are not accepted as valid HOME state.

## Logs

`HOME_TIMING`, `HOME_NAV`, `HOME_SCAN` and `HOME_PLAN` entries record route/state
verification, input gaps, confirmation overhead, retries and progress. Use
`std::chrono::steady_clock` for durations. Group samples by route, distance,
direction and held state; mixed-length action summaries are not per-step costs.
Save screenshots/video for failed or ambiguous trials instead of collecting
routine screenshots for every input.

```csv
route,mode,distance,direction,trial,start_box,end_box,input_count,queue_ms,confirmed_ms,misses,notes
```

Warm up the setup, compare representative short and long moves, and report
median/p90 timings plus failures. Startup travel and full inventory indexing
should be reported separately from execution of an already indexed plan.

## Validation boundaries

Tests cover route bounds, stable/transient cursor observations, pointer geometry,
resume journals and move pre/postconditions. Bounded live trials validate
individual routes and saved-plan restarts. A modeled Box Spaces improvement is
not proof of a whole-sort speedup. Long-run execution, total throughput and native
Linux capture behavior still require physical testing on the intended setup.
