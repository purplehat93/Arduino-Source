# HOME form recognition audit — 2026-10-02

Most gaps have practical implementation paths; full automatic identity coverage
is not established. The initial audit below was read-only. The subsequent
catalog and ability implementation is recorded here separately.

## Completed catalog and ability milestone

The subsequent visual prototype is implemented and documented separately in
[VISUAL-RECOGNITION.md](VISUAL-RECOGNITION.md). The counts below remain the
metadata-only baseline; they do not incorporate synthetic visual matches.

| Outcome | Initial audit | After cleanup |
| --- | ---: | ---: |
| Unique evidence candidate | 1,171 | 1,156 |
| Ambiguous | 328 | 283 |
| Incomplete mapping blocks | 30 | 0 |
| Explicitly excluded stored forms | 1 | 84 |
| Reference entries | 1,530 | 1,523 |

Seven duplicate Minior core entries were merged into established local keys.
The unique count decreases because previously counted nonstored alternatives
are now excluded. With correct field readings and reviewed ability tokens,
1,156 / 1,439 = 80.3% of retained reference entries yield one candidate.
This is still not live detector accuracy or a complete HOME target list.
Without ability readings, 1,152 entries yield one candidate and 287 remain
unresolved. Unknown HOME eligibility remains conservative.

Completed:

- Minior core aliases/types reconciled; seven cores remain genuinely ambiguous
  without visual color evidence. Meteor forms are explicit exclusions.
- Ogerpon resolves to Teal Mask; held-mask and Terastallized alternatives do
  not block matching. Excluded resource labels remain in the audit provenance.
- Reviewed exclusions for Totem, Let's Go partner, Cosplay Pikachu, fused,
  alternate Arceus/Silvally types, Genesect Drives, Origin legendary variants
  and Eternamax entries. Eligibility review remains partial; ride and hidden
  pattern variants still need their own review.
- Rockruff and Greninja use pinned ability tokens. Zygarde ability narrows
  candidates to its appropriate pair, but shape remains unresolved. Missing,
  unknown or incompletely classified ability evidence cannot produce identity.
  Battle Bond Greninja is restricted to male observations.
- Native scanner reads ability again on a second frame for these families and
  stops before advancing if it disagrees. Candidates are never auto-approved.

Validation: 44 Python-suite tests passed, including 2,433 whole-reference
native/Python comparisons with valid, absent and invalid ability observations.
The standalone native policy test passed. Replaying the saved 60-individual
scan gives the same 60 candidate keys, zero new ambiguities and no changed
annotations. A separate catalog-cleanup identification report preserves the
original scan/workspaces. Windows Release compiled successfully and its
runtime catalog hash matches the source. Newly supported form families have
not yet been validated on live console images.

Next: validate the ability-dependent families when representative samples are
available, then implement the visual detector. Full HOME eligibility and shiny
obtainability curation remain separate work; no broad timing run is needed.

## Initial measured reference-policy coverage

| Outcome | Reference entries |
| --- | ---: |
| Unique candidate from species, types and gender | 1,171 |
| Multiple candidates | 328 |
| Blocked by incomplete species form mapping | 30 |
| Explicitly excluded from HOME | 1 |
| Total | 1,530 |

1,171 / 1,529 = 76.6% of conservatively retained reference entries. This is
**not HOME accuracy or coverage of valid HOME forms**: most eligibility fields
are unknown, and the generic reference includes variants that need normalization
or exclusion. The probe assumes perfect field readings and gmax=false. Unique
candidates remain unreviewed identities. Existing real-console evidence covers
60 individuals, 18 species and 20 candidate keys, not the full reference.

Reproduce from this directory:

```sh
python3 form_coverage.py --output /tmp/home-form-coverage.json
```

## Gaps and feasible fixes

1. **Catalog eligibility and mapping.** Minior and Ogerpon account for the 30
   blocked entries. Reconcile duplicate naming, missing types and transient
   forms. Review Totem, starter, ride, item-dependent and other special variants
   for actual HOME storage behavior. Do not equate generic form rows with HOME
   collection targets. The Pichu fix already demonstrates this approach.
2. **Use additional metadata.** The scanner reads ability but the form matcher
   does not use it. Own Tempo Rockruff, Battle Bond Greninja and Power Construct
   Zygarde are candidates for explicit ability rules. Rules require reviewed
   HOME behavior and provenance; ability alone does not resolve cosmetic forms.
3. **Add visual recognition.** Installed AllHomeSprites resources provide
   compatible sprite keys for 266 of the 358 unresolved entries; 92 lack an
   exact/explicit-alias match. These are template availability counts, not
   classifier success rates. No HOME Pokémon form classifier was found in the
   current scanner. Build a confidence-gated detector for the actual box icon
   or summary view, with repeat-frame agreement and a margin over alternatives.
   Validate representative cosmetic groups such as Unown, Vivillon, Deerling,
   flower colors and Alcremie using real captured frames.
4. **Preserve unresolved look-alikes.** RGBA pixel hashes of the 64×64 atlas
   crops are identical within each pair: Sinistea Phony/Antique,
   Polteageist Phony/Antique, Poltchageist Counterfeit/Artisan and Sinistcha
   Unremarkable/Masterpiece. ImpossibleToDiscernForms.txt independently lists
   these pairs. This establishes a limit of these sprite assets, not every
   possible HOME view. Another per-individual screen or explicit user evidence
   must be validated before assigning those identities.
5. **Separate shiny and capability attributes from form.** The atlas has seven
   non-Gmax shiny Alcremie keys, versus 63 ordinary cream/sweet combinations.
   Current resources cannot establish all shiny cream identities. The matcher
   also rejects every observed gmax=true or unknown record; this needs a reviewed
   attribute policy rather than treating Gigantamax capability as a stored
   battle form. Retain uncertainty when the view does not expose a distinction.

## Recommended next milestone

The first catalog cleanup and reviewed ability rules above are complete. Next build
and validate the visual detector against a small representative frame corpus.
Produce explicit supported/unsupported groups for normal and shiny profiles.
Do not require another broad timing run: the remaining problem is identity
evidence. Keep uncertain records for review and exclude them from automatic
form-specific placement until resolved. No release or deletion is authorized.

## Sources and evidence

- Local pinned PokeAPI tables, reviewed HOME overrides and reference matcher:
  catalog_data/ and reference_catalog.py. The helper exercises the actual Python
  matching policy; it does not independently prove native/live accuracy.
- Installed Release Resources/Pokemon/AllHomeSprites.json/.png and
  ImpossibleToDiscernForms.txt. Alias policy: compatibility_keys().
- [Official HOME features](https://home.pokemon.com/en-us/features/): Pokédex
  registration includes Mega/Gigantamax forms; registration therefore does not
  establish the current stored form of an individual. Camera features documented
  for mobile cannot be assumed available to Switch automation.
- [Official transfer form behavior](https://support.pokemon.com/hc/en-us/articles/360052041191-Do-Shiny-Pok%C3%A9mon-or-Pok%C3%A9mon-with-special-forms-retain-these-attributes-when-transferred-from-Pok%C3%A9mon-GO-to-Pok%C3%A9mon-HOME): some special forms cannot transfer or change during transfer.
- [Pokémon Showdown form taxonomy](https://github.com/smogon/pokemon-showdown/blob/master/data/FORMES.md): useful technical distinction between cosmetic,
  event-ability and battle forms; not authoritative HOME eligibility data.

## Catalog completeness recheck — 2026-10-02

The current generated reference has 1,523 form entries. Only 27 have an explicit
`home_available=true`, 84 are explicitly false, and 1,412 remain unknown. Shiny
availability is unknown for 1,486 entries (37 explicit false entries). The atlas has
normal templates for 1,482 entries and shiny templates for 1,472; this measures
sprite presence only and does not establish HOME storage or shiny availability.
These catalog gaps prevent claiming an exhaustive living-form or shiny profile.

The current secondary [Serebii depositable-Pokemon list](https://www.serebii.net/pokemonhome/depositablepokemon-sorted.shtml) remains image-based in the page view available to our tools, so it is not imported for HOME eligibility. A separate text [Serebii unavailable-shiny list](https://www.serebii.net/games/shiny.shtml) explicitly identifies forms that cannot be obtained shiny under any circumstance. On 2026-10-02, 36 exact catalog keys were matched to that negative list and marked false with per-form provenance. Omitted entries remain unknown; the result is a useful partial filter, not proof that every remaining form has an obtainable shiny. HOME eligibility and shiny availability remain separate fields.


## Bounded metadata execution — 2026-10-03

The complete form catalog is not required for a supported subset pilot.
Reviewed identities can opt into `metadata_form_v1` execution when captured
repeat-frame metadata and the current pinned matcher agree. Icon-only forms
remain blocked. Every moved copy must have a distinct observable signature;
identical extras stay in their original review slots and cannot fill collection
assignments or participate in moves. New scans re-identify forms before pickup
and after placement. Manual IV/Judge annotations remain review data.

65 Python tests and native production-executor tests pass; Windows Release
built. An actual Python-produced two-move, one-box pilot passed the native
loader and completed the console trial: one verified swap, unheld pause,
fresh-scan resume, then a second verified swap. The final checkpoint is complete
with two moves and zero releases. This establishes bounded metadata-form
execution/resume for that one-box trial, not full inventory or icon-only coverage.
