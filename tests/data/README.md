`tilepack_test.txt` is written by `build/mkdgfix` (the e2e test checks that the committed copy matches).
`roster_test.txt` is a synthetic roster (blueprint names that exist in no game) for the spawn tests.
`expected_<seed>.txt` are layout / tile / portal dumps of `recipe_test.txt` produced by the original Python
reference model (now in `attic/`), which also agreed with the C generator on 300+ further seeds when the port
was made. They are frozen regression anchors: `test_unit` and `dungeon_e2e.sh` require `dgdump` to reproduce them.
If the layout algorithm changes on purpose, bump `DUNGEON_ALGORITHM_VERSION` and regenerate them from `build/dgdump`.
`roster_deco_test.txt` is the same synthetic roster plus `prop`/`light`/`container` lines and an `ambience` line on the first theme, for the scenery tests.
`build/mkdgfix` also writes the corridor-kit fixtures: `tilepack_kit.txt` (the synthetic pack plus `kit` records), `tileset_kit.set` (one tile per kit
record whose raw corners/edges/door slot are the inverse rotation of what the pack says, so rotation is tested end to end) and `tpl_door.git` (a donor
door that is locked, plot and open, to prove the generator resets them). They are not committed; `dungeon_e2e.sh` builds them each run.
`roster_loot_test.txt` is `roster_deco_test.txt` plus `gold` and `loot` lines (certain drops in two CR bands, a 50% band above CR 3) on the first theme, for the loot tests.
