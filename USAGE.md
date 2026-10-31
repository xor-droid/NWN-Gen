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
seed 12345                         optional run defaults: seed, party_size 1-32, min_level, max_level 1-40
party_size 1
min_level 1
max_level 2
```

`assets/recipe_example.txt` is a commented recipe that uses every key; its header says how to generate the donor files it points at.

The four run-default lines (`seed`, `party_size`, `min_level`, `max_level`) are optional and stand in for the matching command-line options, so a recipe can describe
a whole run: `nwndgen --game nwn1 --base base.mod --recipe recipe.txt --output out.mod`. A command-line option always overrides the recipe line. The seed is
still required from one place or the other; party size and levels fall back to 1 and 1-2 when neither gives them. They never change the layout algorithm: the same
values given on the command line produce the same bytes. The manifest records the values actually used.

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
14/14/16 recipe never failed to lay out. The corridors and doors have been played in game by the author and work.

`doors mixed` is `doors plain` plus a second door look. The corridor kit has two door tiles: `tdc01_g07_01` (door type 22, the look in every stock area) and
`tdc01_g07_02` (door type 23, "CorryDoor1", which stock areas never use, so I had not seen its look before it was checked in game; its slot sits 0.51 m higher). With `mixed` each door picks one of
the looks the pack holds for that corridor with equal chance, then a facing as usual; the door object is the same plain, closed, unlocked clone with only its `Appearance`
changed. `plain` still uses type 22 only and its output bytes do not change when the pack also lists type 23. The pack record is `kit ... door X Y Z BEARING [LOOK]`
(LOOK defaults to 22; `nwnpack tilepack-kit` adds the type-23 records, borrowing g07_01's lighting). The summary prints `Door looks:` and the manifest `door_looks=22:n,23:m`.
Confirmed in game by the author: the type-23 door looks right and works.

`doors doorway` puts a door in the entrance of every corridor, in one of several looks, using the stock "Doorway" tile kit. A corridor has a room-wall connector cell at each
end and the first corridor cell behind it; for each end the pack swaps the plain connector `c13` for `a13_01` (a room wall with a Doorway edge) and the cell behind it for a
tile with one Doorway and one Corridor edge (`g16_01`, a straight, or `f18_01`, an elbow). Stock areas put exactly one generic door on the seam between the two tiles
(162 of the 199 Doorway seams in the stock modules have one door, none has two); the generator does the same, at the seam midpoint, 0.2 m up. The look is one of seven stock
generic doors picked per door from the seed in its own RNG domain (`nwndgen/doorway/1`): wood, portcullis, rusted, metal, evil stone, stone and jeweled (`GenericType_New`
0, 2, 4, 5, 6, 7, 8 of `genericdoors.2da`; `Appearance` stays 0). The portcullis is see-through. Each door is a clone of a clean stock door (unlocked, not plot, untrapped,
no scripts) with its position and tag changed and `Locked`, `Plot` and `AnimationState` reset. The elbow tile `f19_01` fails the walkmesh rule, so a corridor whose first cell is
an elbow of that handedness keeps its plain connector at that end and gets no door there. There are no doors in the middle of the corridor in this mode.
It needs `corridors kit` and a `template_door` made by the current `nwnpack templates` (the file now holds one donor per look after the corridor door; an old file is refused with a
message), and a pack from the current `nwnpack tilepack-kit` (an old pack builds fine, it simply has no doorway doors). `plain`, `mixed` and `none` output does not change when the
pack holds the Doorway records. The summary prints `Door looks:` by name and the manifest `door_looks=generic4:2,...`. Confirmed in game by the author: the doorway doors work.

### Creatures and an objective (optional: `roster` + `scripts`)

Add two lines to the recipe and the dungeon gets monsters and a goal:

```
roster ../assets/roster.txt         creature roster, see below
scripts ../assets/scripts           directory with scel_enter.{nss,ncs} and scel_acquire.{nss,ncs}
spawn_mode full                     optional: full (default) or boss_only
henchmen 0                        optional: 0-5 stock AI henchmen join the first player, see Henchmen below
loot_amount mid                    optional: low, mid (default) or high, see Level-scaled loot
dressing off                       optional: off (default), low, mid or high, see Room dressing
corridor_dressing off              optional: off (default), low, mid or high, needs corridors kit, see Corridor dressing
locks off                         optional: off (default), low, mid or high, needs corridors kit and doors, see Locks
ambush off                        optional: off (default), low, mid or high, see Ambush
traps off                         optional: off (default), low, mid or high, see Traps
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

`spawn_mode loot_test` is for checking drops quickly: exactly five creatures (ignoring the CR budget) and the boss, all in the first area, and the script gives every one of
them 1 hit point and takes their weapons (right and left hand), so they die in one blow and cannot fight back. Scenery, the key, the vault and the loot lines work as in full
mode; the manifest records `spawn_mode=loot_test`. Like `boss_only`, it needs `roster` and `scripts`.

Roster format (`#` comments):

```
nwndgen-roster 1
vault plc_chest1                    locked chest holding the relic
key nw_it_msmlmisc11                  non-stacking item the boss carries (renamed "Vault Key" by the script)
relic nw_it_msmlmisc05              the objective item
theme goblins Goblins               id, then the text used in the journal ("Goblins hold this place.")
minion nw_goblina 0.25              role (minion|standard|elite|boss), UTC resref, CR (two decimals max)
prop|light|container <utp resref> <weight>   scenery for the theme (optional, see Scenery below)
roomprop <type> <utp resref> <weight>      global: furniture of a room type (barracks storage shrine library crypt prison), see Room dressing
corridorprop <utp resref> <weight>       global: scenery along kit corridors, see Corridor dressing
ambience RRGGBB RRGGBB RRGGBB amount clip       area light for the theme (optional)
loot <uti resref> <chance> <min CR> <max CR>   level-scaled item a creature may carry (optional, see Level-scaled loot)
gold <gp per CR>                    gold the theme's creatures carry (optional)
```

Every blueprint must be a stock creature in the hostile faction with the CR written in the roster;
`build/nwnpack roster-check assets/roster.txt` verifies that against **your** install (blueprint exists, faction,
CR, event scripts present) and `build/dgcheck --root INSTALL --roster assets/roster.txt MODULE.mod` re-checks
a built module (markers on floor and on walkable ground, clear of transitions, one boss/key/vault/relic,
creatures all from one theme, scripts and journal present). The compiled `.ncs` files are committed; recompile
with `make scripts NWN_ROOT=... NWN_USERDIR=...` (official `nwn_script_comp`) after editing an `.nss`.
The manifest records the roster and script hashes, the chosen theme and per-area marker counts.
**Generated and validated, not playable until loaded in the game.**



#### Room dressing (same `roster`, recipe key `dressing`)

`dressing off|low|mid|high` (default `off`, needs `roster` and `scripts`) gives every room a type and furnishes it. The types are barracks, storage, shrine, library, crypt and
prison; each is a list of global `roomprop <type> <utp resref> <weight> [centre]` lines in the roster (inert stock placeables only, `nwnpack roster-check` rejects usable, locked, plot or
trapped ones). A room gets a random type, every type once before any repeats. Each room then gets, from its type's lists:

* a **centrepiece** (mid and high): a `centre` line (altar, table, tomb, cage ...) in the middle of the tile nearest the room's centre, refused if it would cut the floor in two;
* **wall pieces**: 3 (low), 5 (mid) or 8 (high) on room corners (wall corners favoured) or in the middle of a wall side (0.5 m off the wall, facing into the room), never in a doorway mouth or beside the boss, the vault or a transition. At mid (a third of the
  pieces) and high (half) a piece may be paired with the facing corner of the next tile (3 m away) so beds and bookcases stand in rows.

With dressing on (and `roomprop` lines in the roster) the theme's generic `prop` scenery is cut to a third so the room furniture carries the look; lights and containers are unchanged.
Candelabras and torches in the lists also light the rooms. Small rooms hold fewer pieces than the quota because doorway mouths and the boss, vault and arrival tiles are kept clear.
The pieces are ordinary decor markers (`SPD_`), so `scel_enter` needs no change. A roster without `roomprop` lines, or `off`, is byte-identical to leaving the key out;
layouts, creatures and loot never move (own RNG domain `nwndgen/rooms/1`, algorithm id `rooms/1`, run after the loot plan). The summary prints `room types` per area
and the manifest records `dressing=`, `dressing_algorithm=` and `rooms.<area>=type,type,...`. The marker limit per area is now 192.
Generated and validated; **not yet confirmed in game.**

#### Corridor dressing (same `roster`, recipe key `corridor_dressing`)

`corridor_dressing off|low|mid|high` (default `off`, needs `roster`, `scripts` and `corridors kit`) puts props along the stock kit corridors from global
`corridorprop <utp resref> <weight>` roster lines (rubble, bones, torches, barrels, cobwebs ...; inert blueprints only, checked by `nwnpack roster-check`).
A kit corridor is 5 m wide, so a prop stands against a wall (1.6 m off the centre line, facing the path) at -2, 0 or +2 m along a straight cell, one per cell (two on opposite walls
at `high`, sometimes). low, mid and high furnish about 4, 6.5 and 9 in 10 of the eligible cells. Elbows, the first and last cell of every corridor (the room entrances),
cells with a door and cells beside the arrival or a transition stay clear.
The spots come from the pack: `nwnpack tilepack-kit` now ends each straight corridor record with `props N`, six bits naming the spots whose point is walkable on that tile in that
orientation and where the corridor is its full width (some stock straights bulge or narrow), so a prop is never placed on a pit or in a narrowing; the validator re-proves every prop against the real
walkmesh. **Rerun `nwnpack tilepack-kit` after updating**: an older pack still builds, it simply has no prop spots (no corridor props). Pieces are ordinary decor markers (`SPD_`), so `scel_enter` needs no change.
Own RNG domain `nwndgen/corridor/1`, algorithm id `corridor/1`, run after room dressing; without the key, or `off`, output is byte-identical. The summary prints `corridor props` per area and the manifest
records `corridor_dressing=`, `corridor_algorithm=` and `corridor_props.<area>=N`. Generated and validated; **not yet confirmed in game.**

#### Locks (same `roster`, recipe key `locks`)

`locks off|low|mid|high` (default `off`, needs `roster`, `scripts`, `corridors kit` and a `doors` mode) locks some corridor doors and some lootable containers.
The rooms form a chain, so a door in the corridor between two rooms separates the arrival room (and every room before the door) from the rest. The corridor out of the arrival room never gets a lock; every later corridor with a door can.
`low` locks one corridor in about half the areas, `mid` half of the eligible corridors (at least one), `high` all of them; one door per chosen corridor.
A locked door is either **keyless** (pick it, cast Knock, or bash it) or **keyed**: roughly half of them name a key by tag and a creature in a room before the door (never the quiet arrival room) carries that key (stock key
blueprint from the roster's `key` line, renamed "Door Key", dropped on death like the other loot). A keyed door is not plot, so it can be bashed as well; a keyless one is never impossible either. The pick DC is 10 plus the average
level of the level band (12-30). Containers (`X` markers) start locked at 25, 40 and 60 in 100 for low, mid and high with the same DC, pickable or bashable.
Door locks are written into the area GIT (`Locked`, `Lockable`, `OpenLockDC`, `KeyRequired`, `KeyName`); the key and the container locks are applied by `scel_enter` from marker variables
(`scel_key`, `scel_keybp`, `scel_lockdc`). The marker limit in the script is now 200 (the generator's limit is 192).
`dgcheck` re-proves it: a locked door is lockable, not plot, DC 12-30, its key requirement matches its key tag, exactly one creature in the area carries each key and no creature carries a key that opens nothing,
and a flood fill from the arrival point reaches every key holder without passing a keyed door whose own key is still out of reach. Henchmen may not path through locked doors well; use `low` with henchmen.
Own RNG domain `nwndgen/locks/1`, algorithm id `locks/1`, run after the other planners; without the key, or `off`, output is byte-identical. The summary prints `locks` per area and the manifest records
`locks=`, `locks_algorithm=` and `locks.<area>=doors N, keyed N, chests N`. Generated and validated; **not yet confirmed in game.**

#### Ambush (same `roster`, recipe key `ambush`)

`ambush off|low|mid|high` (default `off`, needs `roster` and `scripts`) keeps some room creatures hidden until a group is sprung, then they all appear and attack. A group is 2-3 plain creatures of one room
(never the arrival room, the boss room or a creature carrying a door key). If the room has a lootable container, **opening it** springs the group (also if it is found open or destroyed); otherwise a **player coming within 8 m**
of the group's first creature springs it (checked every heartbeat, 6 s). `low` makes one group in about half the areas, `mid` one per area, `high` up to two. The hidden creatures are the ones the spawn plan already budgeted and gave their loot:
creature counts, CR, XP and drops do not change, only when the creatures appear. Hidden creatures still carry their loot when they appear.
Run time: `scel_enter` keeps the group's markers instead of creating the creatures and attaches `scel_use` to the chest's OnOpen (`SetEventScript`); the area OnHeartbeat is `scel_beat` only in areas with a group.
The module ships `scel_use` and `scel_beat` (and the compile-time include `scel_inc.nss` lives next to them in `assets/scripts`; it is not shipped) only with this key. `make scripts` compiles all four scripts.
`dgcheck` checks that every group has 2-3 plain creatures and at most one container nearby, no group hides a key carrier, and the heartbeat script is set exactly on the areas that have a group.
Own RNG domain `nwndgen/ambush/1`, algorithm id `ambush/1`, run after the locks; without the key, or `off`, output is byte-identical. The summary prints `ambush` per area and the manifest records `ambush=`, `ambush_algorithm=` and
`ambush.<area>=groups N, hidden N, chests N`. Generated and validated; **not yet confirmed in game** (open points: whether `SetEventScript` on a stock container's OnOpen fires, and how the ambushers behave).

#### Traps (same `roster`, recipe key `traps`)

`traps off|low|mid|high` (default `off`, needs `roster` and `scripts`) adds hidden floor traps and trapped containers. low, mid and high place 1, 3 and 5 floor traps per area and trap 0, 25 and 40 in 100 of the lootable containers.
A floor trap stands in the middle or on a corner spot (3.5 m) of a full-floor room cell, or in the middle of a straight kit corridor cell (a corridor is 5 m wide, so it can be walked around once found). It never stands in the quiet arrival room of the first area,
in a cell with a door, within one cell of an arrival or transition point, or within 2 m of another marker. The strength follows the average character level of the band: minor up to level 4, average to 10, strong to 20, deadly above, in one of eight families
(spike, tangle, acid, fire, electrical, gas, frost, sonic; the holy and negative kinds are left out because they barely hurt or help). Every trap is **detectable and disarmable** at DC 10 + the average level (12-30) and **springs once**.
`scel_enter` creates floor traps with `CreateTrapAtLocation` from `SPP_trap` markers (`scel_trap` = TRAP_BASE_TYPE number, `scel_trapdc`) and trapped containers with `CreateTrapOnObject`. Henchmen walk into traps like anyone else.
Traps can hurt: a first-level party meets minor traps (a few points of damage), but use `low` with weak parties. `dgcheck` checks the marker rules (middle or corner spot of a floor cell or the middle of a straight corridor cell, a known trap family and tier, DC 12-30)
and the generic ones (clear of waypoints, triggers and the module entry, 2 m from other markers, walkable ground).
Own RNG domain `nwndgen/traps/1`, algorithm id `traps/1`, run after the ambush plan; without the key, or `off`, output is byte-identical. The summary prints `traps` per area and the manifest records `traps=`, `traps_algorithm=` and
`traps.<area>=floor N, chests N`. Generated and validated; **not yet confirmed in game** (open points: whether the trap appears and can be found and disarmed, and the damage at low levels).

#### Level-scaled loot (same `roster`)

`loot_amount low|mid|high` (recipe key, default `mid`, needs `roster` and `scripts`) scales how much creatures carry without changing which creature gets what:
the random draws are the same, only the item chances and the gold are scaled afterwards, so `mid` is byte-identical to leaving the key out and a creature that carries
an item at `low` carries it at `mid` and `high` too. `low` halves every item chance and the gold; `high` multiplies the item chances by 1.5 (a drop is never above 100%)
and doubles the gold. A creature still carries at most two items, and the boss always carries two (weighted picks, not chances), so for the boss only the gold scales.
The summary names a non-default amount and the manifest records `loot_amount=`. Scenery containers and the vault are not affected.
Confirmed in game by the author at `high` (kobold minions dropped 4 and 6 gold, which `mid` cannot produce at level 4; the planned items matched what dropped).

Creatures can carry items and gold, decided at generation time from the seed:

```
gold 8                              creatures carry about 8 gp per point of CR (50%-150%), bosses three times that
loot nw_it_mpotion001 10 0.00 1.50  item (UTI resref), chance in percent per creature, min CR, max CR
loot nw_it_gem010 8 1.50 4.00       an entry applies only to creatures whose CR is in its range
```

Each regular creature rolls every entry whose CR range includes its own CR and keeps at most two hits; a boss gets up to two eligible items
chosen by chance weight. Gold is `gold × CR` (CR below 0.25 counts as 0.25), randomised 50%-150%, and grows 25% per character level of the band's
average above 1 (so a level 5-6 party finds more gold than a level 1-2 one); a boss carries three times as much. Items scale by the creature's
CR, which itself follows the level band and party size through the CR caps and budgets, so stronger creatures carry better things. With the shipped
roster, creature CRs top out at 2.00, so the bands above CR 2.50 start to matter only once stronger creatures are added to a theme. The decisions use
their own RNG stream (`SHA-256("nwndgen/loot/1\0" || seed_be64 || resref)`), so layouts, creatures and scenery never move when loot lines change;
the algorithm id is `loot/1` (manifest `loot_algorithm=`, `loot.<area>=items N gold G`). A theme without `loot`/`gold` lines behaves as before.

How it reaches the game: the generator puts `scel_loot`, `scel_loot2` (item resrefs) and `scel_gold` on the creature's marker waypoint as local variables;
`scel_enter` creates the items and a stack of `nw_it_gold001` on the creature, and, when any marker carries loot, sets `X2_L_NOTREASURE` on the module
before spawning anything so the stock random monster treasure (an OnSpawn roll that is not seeded) does not add to it. This relies on the stock spawn
script (`nw_c2_default9`) being the creature's spawn script; `nwnpack roster-check` reports any roster creature that uses another one, checks that every loot item
exists and is not plot or cursed, and prints each item's value so the bands can be reviewed. `dgcheck` verifies that only creature and boss markers carry
loot, that each item is in its theme's list and CR range, and (with `--root`) that each item exists. **Whether the items and gold show up in the corpse in
game, and whether the random treasure is really suppressed, is unverified until a module is loaded.** Changing `scel_enter` changes the bytes of every
module built with roster and scripts, including older ones rebuilt from the same seed.

### Henchmen (optional: `henchmen N`, same `roster` + `scripts`)

NWN1:EE cannot give one player direct control of several characters, so a party is built from **AI henchmen**: stock companions that follow the player,
fight, and can be spoken to (the stock conversation handles orders and dismissal). Add to the recipe:

```
henchmen 2                          0-5 (default 0 = none); needs roster and scripts
```

and to the roster one `henchman <prefix>` line per stock family the generator may pick from (up to 6; `assets/roster.txt` lists the six stock ones,
`nw_hen_dae`, `bod`, `gal`, `gri`, `lin`, `sha`). A family `<prefix>` has blueprints `<prefix>_01` .. `<prefix>_11`, each a character of level NN+3 (levels 4-14).
The seed picks N **distinct** families (own RNG domain `nwndgen/hench/1`, so nothing else moves) and the level index NN follows the recommended level:
`NN = clamp(average of --min-level and --max-level - 3, 1, 11)`, so the minimum henchman level is 4. The choice is stored in the module's variables
(`scel_hn`, `scel_h1`..), and `scel_enter` creates them next to the first player who enters the first area and joins them with `AddHenchman` once
(it raises `SetMaxHenchmen` if needed). Only the first player gets them; later players in a multiplayer game do not. A roster with fewer families than `henchmen N` is an error.
The summary prints a `Companions:` line and the manifest records `henchmen=`, `henchman.<n>=` and `hench_algorithm=hench/1`. `nwnpack roster-check` verifies each family
(all eleven levels exist and have NN+3 class levels), `dgcheck` that the module's list matches the roster, is distinct and resolves in the install.
Confirmed in game by the author: the henchmen join the first player, follow and fight. Not yet checked: the level band against a character above level 4, and multiplayer.

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
