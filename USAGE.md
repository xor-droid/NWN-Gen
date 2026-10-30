# nwndgen usage

Deterministic generator for NWN:EE and NWN2:EE `.mod` adventures: it overlays
seed-selected room variants onto a base module of the game you select, or (NWN1 only)
generates brand-new tile dungeons from the seed with `--recipe`. C11/POSIX, no runtime dependencies.
Everything here is C and POSIX sh: the generator, the install reader (`build/nwnpack`), the validator
(`build/dgcheck`) and all tests. No Python is needed anywhere (the old scripts are archived in `attic/`).

## Build and test

```sh
make            # build/nwndgen plus nwnpack, dgcheck, dgdump, erfpack, erfcheck
make test       # unit + e2e tests for both games, plus the dungeon (--recipe) e2e
make sanitize   # same tests under ASan + UBSan (separate build-san/ tree)
```

## Run

```sh
build/nwndgen --game nwn1 --base assets/shifting_cellars.mod --templates assets/rooms.tsv \
  --seed 12345 --party-size 1 --min-level 1 --max-level 2 \
  --output out/cellars-12345.mod --manifest out/cellars-12345.txt
```

Defaults: party size 1, levels 1-2. `--game`, `--seed`, `--base`, `--templates`,
`--output` are required.

### The game gate (`--game nwn1|nwn2`)

There is no default: you always state the target. The gate selects a profile and
every archive read or written must match it; anything else is rejected before
any output exists (usage error, exit 2, for a missing/unknown value).

| | `nwn1` (NWN:EE) | `nwn2` (NWN2:EE) |
|---|---|---|
| ERF version | V1.0 | V1.1 |
| resref / key entry size | 16 / 24 bytes | 32 / 40 bytes |
| an area to replace is | ARE + GIT + GIC | ARE + GIT + GIC + TRN (3002) + TRX (3035) |

A base or variant built for the other game fails with a message such as
`archive is ERF V1.1 (nwn2 format) but nwn1 was selected`. NWN2 terrain
(TRN/TRX) is treated as opaque data (only the `NWN2` magic is checked); stock NWN2
areas carry 16-17 MB of it each, hence the 1 GiB per-file / 2 GiB aggregate caps. Seeds are unsigned 64-bit decimals. Existing outputs are never
replaced unless `--force` is given; the module and manifest are written to temp
files and each is published atomically. If the manifest cannot be published
after the module was, the new module is removed again; with `--force` the module
it replaced is already gone by then (known limitation). Exit status: 0 success, 1 input/generation error,
2 usage error.

A try-it example with the **format-test fixture** (not playable):

```sh
build/fixtool fixture /tmp/fx nwn1
build/nwndgen --base /tmp/fx/base.mod --templates /tmp/fx/rooms.tsv \
  --seed 12345 --output /tmp/fx/out.mod
build/fixtool verify /tmp/fx/out.mod /tmp/fx/base.mod /tmp/fx/rooms.tsv
sha256sum /tmp/fx/out.mod   # 0d9ceee0...0dc77c
```

## Procedural dungeons (`--recipe`, NWN1 only)

```sh
build/nwndgen --game nwn1 --base base_module.mod --recipe recipe.txt --seed 12345 \
  --output out/cellars-12345.mod --manifest out/cellars-12345.txt
```

`--recipe` replaces `--templates`: instead of choosing premade rooms, every area gets a **fresh
layout from the seed**. The base module only supplies `module.ifo` (edited: name, tag, id, area
list, entry area/position/facing) and extras such as `repute.fac`. Each area is a lattice of
wall/floor vertices (rooms joined by corridors); every cell is tiled from a pack of tiles that
stock areas really place, chosen by its four corner labels, then areas are chained with
transition triggers and waypoints. Recipe (`#` comments; single spaces between fields; paths are
relative to the recipe file):

```
nwndgen-recipe 1
tilepack tilepack_tdc01.txt         tile pool + per-tile lighting (see tools/README.md)
                                    (tilepack_tdc01_rich.txt from `nwnpack tilepack-rich` adds decorative floors, see tools/README.md)
template_are tpl_are.are            donor ARE (header, lighting, weather; Tile_List emptied)
template_git tpl_git.git            donor GIT (AreaProperties + one stock transition trigger)
module_name The Shifting Cellars
module_tag SHIFTING_CELLARS
module_description ...              (optional)
support 1 2 1 1                     min/max level, min/max party size the recipe is meant for
area scel_a 10 10 3 Upper Cellars   resref, width, height (6-32 tiles), rooms (2-8), display name
area scel_b 10 10 3 Lower Cellars   areas are chained in order: a -> b -> ...
```

Requested `--min-level/--max-level/--party-size` must lie inside `support`, otherwise the run
fails. Too many rooms for a small area can make a seed unlayoutable; that is a clean error naming
the area (10x10 with 3 rooms never failed in 500 seeds; 8x8 with 3 rooms fails about 12%).
The recipe path is NWN1 only because the tile pack is an NWN1 tileset; `--game nwn2 --recipe`
is a usage error.

Per-area RNG: `SplitMix64(first 8 BE bytes of SHA-256("nwndgen/layout/1\0" || seed_be64 || resref))`,
so renaming or adding an area never reshuffles the others. `Mod_ID` is the first 16 bytes of
`SHA-256("nwndgen/modid/1\0" || seed_be64 || module_tag)`. The recipe algorithm is
`dungeon-chain/1` (`DUNGEON_ALGORITHM_VERSION` in `src/dungeon.h`); the manifest adds recipe,
tilepack and template hashes and per-area layout attempts.

`build/dgdump RECIPE SEED BASE.ifo` prints the layouts, tile ids and portal cells as text. The
tests compare it against dumps (`tests/data/expected_*.txt`) written by
the original Python reference model (now in `attic/`), which also matched the C output on 300 further seeds when the
port was made. **Nothing here is claimed playable until loaded in the game.**


### Corridor kit and doors (optional: `corridors kit`, `doors plain`, `template_door`)

By default corridors are strips of 10 m full-floor cells, which cannot hold a door (a stock tile only offers a door where it has
an edge crosser, and the corner lattice has none). Two opt-in recipe keys change that:

```
tilepack tilepack_tdc01_kit.txt     from `nwnpack tilepack-kit` (the rich pack plus `kit` records)
template_door tpl_door.git          donor GIT whose first Door List entry is cloned for every door (`nwnpack templates` writes it)
corridors kit                       rooms are joined by stock kit corridors (5 m wide) instead of wide strips; default `wide`
doors plain                         one closed, unlocked, non-plot door in each corridor long enough; default `none`
```

`corridors kit` lays out rooms with a free cell between their wall rings (no floor outside rooms), puts a connector tile
(`tdc01_c13_01`, a room-wall tile whose side is a Corridor edge) on a wall of each room, and joins every room to the next by
one-cell-wide paths of kit tiles (`g01` elbows, `g02` straights). Neighbouring tiles must carry the same edge crosser, which the
generator and `dgcheck` both enforce. `doors plain` swaps one straight corridor cell per link (not beside a room) for `g07_01`,
the straight with a door, and clones the stock door `nw_door_ttr_08` onto it: closed, unlocked, not plot, no trap. `doors plain`
needs `corridors kit` and `template_door`; `corridors kit` needs a pack with `kit` records. A recipe without these keys builds
exactly the same module as before. Kit builds record `dungeon_algorithm=dungeon-chain/2`, `corridors=kit`, `doors=...`,
`template_door_sha256=` and `doors.<area>=N` in the manifest; the door choice has its own RNG stream
(`SHA-256("nwndgen/doors/1\0" || ...)`), so it never reshuffles layouts. Rooms need more space than with wide corridors: use at
least 14x14 for 3 rooms and 16x16 for 4 (a cramped area is a clean error that says to use a larger one). Sweeping 300 seeds of a
14/14/16 recipe never failed to lay out. **How these corridors and doors look and behave in game is unseen until loaded.**

### Creatures and an objective (optional: `roster` + `scripts`)

Add two lines to the recipe and the dungeon gets monsters and a goal:

```
roster ../assets/roster.txt         creature roster, see below
scripts ../assets/scripts           directory with scel_enter.{nss,ncs} and scel_acquire.{nss,ncs}
spawn_mode full                     optional: full (default) or boss_only
```

Both keys together, or neither. **Without them the output is byte-identical to what earlier versions
produced** (no scripts, no markers, empty area/module event fields); a golden hash in `tests/dungeon_e2e.sh` pins that (and the real seed 1 and 2 modules matched v0.3.0 byte for byte).

What a generated module contains with them:

* **Markers, not objects.** Each area's GIT gets waypoints tagged `SP<kind>_<blueprint>`
  (`SPC_` creature, `SPB_` boss, `SPV_` vault, `SPK_` key item, `SPR_` relic item). The compiled area
  `OnEnter` script `scel_enter` runs on the first PC to enter an area (ignores non-PC entrants such as the
  creatures it spawns) and creates the stock blueprints at the markers. Nothing is hand-built UTC data.
* **The objective, "relic".** In the last room of the last area stands the boss, who carries a key
  (tag `SCEL_KEY`), beside a locked chest (tag `SCEL_VAULT`, key-required, plot-flagged so it cannot be
  picked or bashed) holding the relic (tag `SCEL_RELIC`). `scel_acquire` (module `OnAcquireItem`) advances
  the journal quest `scel_main` (`module.jrl`): entry 1 on entering any area, 2 when the key is picked up,
  3 (end) when the relic is. The vault blueprint (`plc_chest1`) runs its stock treasure script on open, so
  it may hold extra loot besides the relic. A base module that already sets `Mod_OnAcquirItem` to something
  else is refused rather than overwritten.
* **Theme per seed.** `SHA-256("nwndgen/theme/1\0" || seed_be64 || module_tag)` picks one roster theme
  (goblins / kobolds / undead in `assets/roster.txt`) among those that have a boss under the CR cap. Each
  area uses `SHA-256("nwndgen/spawn/1\0" || seed_be64 || resref)`; these are separate RNG domains, so layouts
  never change when the roster does. Algorithm id `spawn/2` (`SPAWN_ALGORITHM_VERSION`; `spawn/1` had no scenery).
* **Budget and caps** (heuristics, **not playtested for balance**): with `avg = (min_level+max_level)/2`
  and the configured party, regular creatures may have CR up to `avg`, the boss up to `avg + 0.5`
  (`+1` more if the party is larger than one). Each area has a CR budget
  `party × avg × 2 × (0.5 + 0.5·(i+1)/n)` (area `i` of `n`), at most 24 creatures, one per full-floor
  tile, never in the arrival room of area 0 or within one tile of a transition. If no theme has a boss
  under the cap (for instance a level-1-only solo run with this roster) the run fails and says so.

Optional `spawn_mode boss_only` in the recipe (default `full`) is for quick test runs: no minions at all, and the
boss (marker `SPT_`) is set to 1 hit point by the script, so the key/vault/relic chain can be exercised in a minute.
The manifest records `spawn_mode=`. `dgcheck` reports such a module as test mode.

Roster format (`#` comments):

```
nwndgen-roster 1
vault plc_chest1                    locked chest holding the relic
key nw_it_msmlmisc11                  non-stacking item the boss carries (renamed "Vault Key" by the script)
relic nw_it_msmlmisc05              the objective item
theme goblins Goblins               id, then the text used in the journal ("Goblins hold this place.")
minion nw_goblina 0.25              role (minion|standard|elite|boss), UTC resref, CR (two decimals max)
prop|light|container <utp resref> <weight>   scenery for the theme (optional, see Scenery below)
ambience RRGGBB RRGGBB RRGGBB amount clip       area light for the theme (optional)
```

Every blueprint must be a stock creature in the hostile faction with the CR written in the roster;
`build/nwnpack roster-check assets/roster.txt` verifies that against **your** install (blueprint exists, faction,
CR, event scripts present) and `build/dgcheck --root INSTALL --roster assets/roster.txt MODULE.mod` re-checks
a built module (markers on floor and on walkable ground, clear of transitions, one boss/key/vault/relic,
creatures all from one theme, scripts and journal present). The compiled `.ncs` files are committed; recompile
with `make scripts NWN_ROOT=... NWN_USERDIR=...` (official `nwn_script_comp`) after editing an `.nss`.
The manifest records the roster and script hashes, the chosen theme and per-area marker counts.
**Generated and validated, not playable until loaded in the game.**


### End-of-run summary

A successful `--recipe` run prints a description of what it built after the `wrote` line: the character levels and party size it was built for
(your `--min-level/--max-level/--party-size`; the CR caps and area budgets are scaled to them), the recipe's supported band, the number of
levels (areas), rooms, doors and corridor style, the theme, and per level: size, rooms, door count, creatures with their CR total and how many
stand in each room, and scenery counts. Then the creature types with role, CR and count, total CR, the CR limits used, the boss and the
objective chain (boss, vault, relic), lootable containers and decor, then the command line that reproduces the module and the SHA-256 of the output. It is printed only; the manifest keeps the machine-readable record.
All figures are the generator's heuristics, not playtested balance.

#### Scenery and area light (same `roster`, so no new recipe key)

Rosters may also give each theme scenery and an area light; a roster without those lines behaves exactly as before.

```
prop plc_bedrolls 4                 decor: a stock placeable with no inventory (weight 1-100)
light plc_brazier 3                 light source: brazier, torch, candelabra
container plc_barrel 3              lootable barrel/crate: a stock container that runs its usual loot script when opened
ambience 3a3020 e0802a 4a2810 10 55 MoonAmbient, MoonDiffuse and Moon fog colours (RRGGBB), fog amount 0-15, fog clip 20-150
```

* **Markers again.** Scenery is planned into the same `SP*` markers as creatures (`SPD_` decor, `SPL_` light,
  `SPX_` container); `scel_enter` creates them from the stock blueprints in the same first-entry pass. Nothing is
  placed by hand and no blueprint is built by the generator.
* **Where.** Only on full-floor tiles inside rooms, never in a doorway, within one tile of a transition, or
  within one tile of the boss/vault. Each 10 m tile has four corners that sit 3.5 m off its centre; scenery takes
  corners first (wall-side corners favoured) so it can share a tile with a creature, and the tile centre only as a
  last resort and only if removing that tile keeps the whole floor one connected region. Lights always take a
  corner and keep 3 tiles apart. Per area: up to `min(rooms, 4)` lights, `min((rooms+1)/2, 2)` containers and
  `min(3 + 2·rooms, 12)` props. `boss_only` runs get no scenery.
* **RNG.** Domain `SHA-256("nwndgen/props/1\0" || seed_be64 || resref)`, separate from layout and creature
  placement: creature, boss and vault markers are identical with and without scenery lines (a unit test compares
  them). Algorithm id `spawn/2`.
* **Area light.** If the chosen theme has an `ambience` line, each area's ARE gets `MoonAmbientColor`,
  `MoonDiffuseColor`, `MoonFogColor`, `MoonFogAmount` and `FogClipDist` from it. The donor area is permanent night,
  so only the Moon values show; it has no sound or music fields, so this is lighting and fog only.
* **Validation.** `nwnpack roster-check` also checks that props and lights are inert (not useable, no inventory,
  not trapped) and containers have an inventory and are unlocked. `dgcheck` checks each scenery marker's position
  on the walkmesh (rotated back into the tile's own frame), that markers are at least 2 m apart, that scenery comes
  from the chosen theme, and that the floor stays connected. The manifest lists `ambience=` and
  `scenery.<area>=decor N light N container N`.
* **Not verified:** how any of it looks in game, whether a prop's collision blocks a doorway-adjacent corner,
  or whether a torch bracket wants a wall orientation. Those need a playtest.

## Catalog format (`rooms.tsv`)

Seven TAB-separated columns, no header; blank lines and `#` lines ignored; LF or
CRLF. Paths are relative to the catalog file (spaces allowed, absolute paths
rejected).

```
slot  variant  min_level  max_level  min_party  max_party  erf_path
```

IDs are 1-32 chars of `[a-z0-9_]`. Levels 1-40, party 1-32. A variant is
eligible when its level range covers the **whole** requested band and its party
range covers the party size; each slot needs at least one eligible variant.
Row order never matters (rows are sorted by slot, variant).

## Determinism contract

Output bytes are a function of: seed, party size, level band, generator
algorithm (`splitmix64-sha256slot/1`), and the bytes of the base and variant
files. Not inputs: output path, cwd, locale, timezone, mtimes, catalog row order,
variant file names/locations, compiler. Each slot draws from its own stream
`SplitMix64(first 8 BE bytes of SHA-256("nwndgen/1\0" || seed_be64 || slot))`
with rejection-sampled selection among eligible variants sorted by ID, so adding
a slot never reshuffles others. Archives are written canonically (sorted keys,
sequential IDs, zero build year/day, zeroed reserved bytes).

Changing the PRNG, stream derivation, ordering or writer layout requires bumping
`ALGORITHM_VERSION` in `src/plan.h` and re-pinning the golden hash in
`tests/e2e.sh`. The seed is not embedded in the `.mod` (no engine-verified
location for it exists yet); it lives in the sidecar manifest.

## Limits

1 GiB per file, 2 GiB aggregate, 200000 archive entries, 4096 catalog rows.
Only the selected game's ERF version (`V1.0` for nwn1, `V1.1` for nwn2) with
`MOD`/`ERF`/`HAK` signatures is read; GFF is checked for
`V3.2` header sanity only (content is not interpreted). The generator replaces
area content; it does not change the area graph, `module.ifo` or scripts. (`--recipe` mode edits
`module.ifo` as described above and builds the area graph itself; it adds scripts only when the recipe has
`roster`/`scripts`.)

## Known limitations / untested

- Real-file validation (`tests/realfiles.sh`, `build/erfcheck`): every stock NWN1
  module and campaign (22 files, `data/mod` + `data/nwm`) and all 37 NWN2:EE
  modules (up to 457 MB) parse with the strict reader and re-serialize with every
  payload intact (59/59). That found real resrefs containing spaces and `! ' +`,
  so names are accepted as printable ASCII (lowercased). Stock `km_resources.hak`
  has duplicate keys and is rejected on purpose (HAKs are not inputs).
- Independent tool check (NWN1 only; optional, needs neverwinter.nim binaries;
  its `nwn_erf` cannot read NWN2's V1.1, so NWN2 has no independent-tool check):
  `sh tests/indep_nwn_tools.sh build/nwndgen /abs/path/to/neverwinter-tools`.
  Passed 34/34 with neverwinter.nim 2.3.1 (x86_64 Linux release) on the fixture.
- Reader divergence: the C reader honors each key's resource ID (as the ERF spec
  says); neverwinter.nim ignores it and pairs key i with resource entry i. They
  agree on every file with identity IDs (all real tool output, and everything
  nwndgen writes). Only the default `fixtool fixture` output shuffles IDs.
- No game load or playtest has happened; no playable content pack exists for
  either game. NWN2 areas need toolset-authored terrain (TRN/TRX) and cannot be
  synthesized here.
