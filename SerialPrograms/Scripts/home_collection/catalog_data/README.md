# Reference data provenance

The `pokeapi/` CSV files come from the public
[PokéAPI repository](https://github.com/PokeAPI/pokeapi/tree/bc92d3b6029ef1abe9e7ad424c400b338f3c11fe/data/v2/csv).
`manifest.json` pins that revision and the URL/SHA-256 of every input.
The upstream BSD license is retained in `pokeapi/LICENSE.md`; distribute it
with the generated catalog. These are community-maintained reference records,
not Nintendo certification of HOME storage or shiny obtainability.

`AllFormDisplayMap.json` is the installed Pokemon Automation resource snapshot.
Its digest and purpose are recorded in `local-resource.json`. It supplies
compatibility keys only; display text is not trusted as form/type/evolution
evidence. `reference_catalog.py` has explicit vocabulary adapters. Unmapped
entries stay visible with unknown classifications and block unique recognition
for that species. Battle-only source records and separate shiny rendering
labels are excluded, rather than silently becoming stored-form identities.

`HomeCollectionReference.json` is the generated reproducible artifact shipped
to the Windows app's `Resources/Pokemon/` directory. Its digest is pinned in
`PokemonHome_CollectionObservation.inc` and tested against these inputs.
When intentionally updating the snapshot, regenerate the catalog, review
coverage/classification differences, update the pinned scanner digest and rerun
the catalog, matcher and bounded live tests. Do not overwrite private scans or
review annotations during a catalog update.

HOME eligibility and shiny availability remain unknown until reviewed with
appropriate evidence. A 2026-10-02 review matched 36 exact form keys to the
current Serebii [unavailable shiny reference](https://www.serebii.net/games/shiny.shtml).
Together with the pre-existing Spiky-eared Pichu exclusion, 37 entries now have
`shiny_available: false`. The page states that the 36 listed forms cannot be
obtained shiny under any circumstance. This negative list does not prove shiny
availability for omitted forms, and HOME eligibility remains a separate field. Evolution classifications describe the
reference graph across versions, not a currently selected game's capabilities.

`home-overrides.json` contains narrow reviewed corrections with source links.
Its digest participates in catalog provenance and the pinned generated scanner
resource. The Pichu correction excludes the nontransferable Spiky-eared form
from HOME matching without silently excluding forms whose eligibility is unknown.

The 2026-10-02 catalog cleanup also reviews HOME normalization for Totem,
partner, fused and item-dependent variants. Minior core aliases now map to
the existing local keys; its Meteor forms and Ogerpon's nonstored alternatives
remain explicit reviewed exclusions. Excluded resource-only labels are recorded
separately from pending mappings. This is still a partial eligibility review.

Pinned `abilities.csv` and `pokemon_abilities.csv` are used only for form families
whose override explicitly enables `match_ability`: Rockruff, Greninja and
Zygarde. Tokens are exact scanner dictionary slugs, never fuzzy display names.
Empty/unrecognized abilities cannot establish identity for these families.
Zygarde shape remains ambiguous even after an ability match. The native scanner
requires repeat-frame ability agreement for these families. Offline policy
agreement does not validate the OCR on previously untested live forms.

`AllHomeSprites.json` snapshots the installed atlas vocabulary.
`visual-resource.json` pins both that JSON and the existing runtime PNG; the
bitmap is not duplicated here. The catalog explicitly maps each form's normal
and shiny template, including shared renderings. Native matching verifies the
runtime assets before using them. Template presence is not evidence of visual
distinctness, HOME eligibility or shiny obtainability. Detector thresholds and
its experimental status participate in the catalog's pinned policy.
