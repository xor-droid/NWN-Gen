# Authoring tools (C)

Build everything with `make`. All of it is POSIX C; none of it runs when you generate an adventure.

| tool | purpose |
|---|---|
| `build/nwnpack` | reads **your** NWN:EE install (`data/*.key`/`*.bif`, `data/mod`, `data/nwm`) and writes the inputs `nwndgen --recipe` needs. Nothing from the game is stored in this repository. |
| `build/dgcheck` | independent validator for a built module: tile adjacency re-derived from the tileset's own `.set`, one connected floor region, objects on floor, transitions resolve, GIC mirrors GIT; with `--roster` also the creature, objective and scenery markers (floor, walkable ground at the marker position, one boss/key/vault/relic, scripts, journal, scenery from the chosen theme, floor still connected) |
| `nwnpack roster-check FILE` | checks every roster blueprint against your install: creatures (exists, hostile faction, CR, event scripts) and scenery (props and lights inert, containers unlocked with an inventory) |
| `build/dgdump` | prints layouts / tile ids / portal cells for a recipe + seed (compared with `tests/data/expected_*.txt`) |
| `build/erfpack` | deterministic directory → ERF/MOD packer |
| `build/erfcheck` | strict ERF round-trip check on real files |
| `tools/sweep_seeds.sh` | generate + `dgcheck` a range of seeds against a real install (optional ROSTER argument, `$NWNDGEN_ARGS` for level/party) |

## Build a pack from your install

```sh
export NWN_ROOT="/mnt/c/Program Files (x86)/Steam/steamapps/common/Neverwinter Nights"
build/nwnpack check                               # hard gate: tile index order / rotation conventions on all stock areas
build/nwnpack tilepack out/tilepack_tdc01.txt     # stock-proven tdc01 tile pool + lighting
build/nwnpack tilepack-rich out/tilepack_tdc01_rich.txt   # the same plus decorative full-floor tiles proven walkable (opt-in)
build/nwnpack tile-report                          # survey: every tdc01 tile with four Floor corners, walkmesh coverage, stock uses
build/nwnpack templates out/                      # tpl_are.are, tpl_git.git, base_module.mod
# write out/recipe.txt (format in USAGE.md), then:
build/nwndgen --game nwn1 --base out/base_module.mod --recipe out/recipe.txt --seed 1 --output out/cellars-1.mod
build/dgcheck --root "$NWN_ROOT" out/cellars-1.mod
sh tools/sweep_seeds.sh build out/recipe.txt out/base_module.mod "$NWN_ROOT" 1 300
```

## Why these steps

* `nwnpack check` is a hard gate: of 8 tile-index/rotation conventions exactly one (rows from the south,
  west→east; orientation = counter-clockwise quarter turns) gives zero corner/edge/height mismatches over all
  232 stock `tdc01`/`tin01` areas. It exits non-zero if that ever stops being true.
* Layouts live on the **corner lattice**: choose Wall/Floor for each vertex, and every cell then needs a tile with
  those four corners. Only tiles that stock areas actually place (basic `tdc01_a*` pieces; fully-walkable floors
  verified from the game's own `.wok` walkmeshes and `surfacemat.2da`) are used. Diagonal corner patterns, which
  stock never tiles, are forbidden.
* The ARE header, AreaProperties, per-tile lighting, IFO and `repute.fac` are copied from stock donors
  (`Chapter1.nwm:map_m1s1e`, `Chapter2E.nwm:m2q4d_06`'s transition trigger, *The Dark Ranger's Treasure*).
* If several `.key` files hold the same resource with different bytes, `nwnpack` refuses to guess.

## Status

`nwnpack` output was checked against the earlier Python scripts: the tilepack is byte-identical, and modules built
from the C-made and the Python-made templates are semantically identical in all 11 resources. Modules from
`nwndgen --recipe` (300 seeds × 3 areas) pass `dgcheck`. The plain-corridor modules with creatures and the objective have been loaded and played by the author; the rich floors load; the corridor kit and doors are not yet game-tested.

## Richer floors (`tilepack-rich`, opt-in)

The plain pack uses five fully walkable stock floors. `nwnpack tilepack-rich` adds decorative full-floor `tdc01_` tiles
(pillars, statues, uneven floors and the like; I have not seen what they look like in game) only when the game's own
walkmesh proves them safe for what the generator puts on a floor tile: the centre (creatures), the four scenery slots
3.5 m off centre, and a point 4.5 m out on each of the four edges (where neighbours join) must all be on walkable faces
of **one** edge-connected walkable region (`wok_points_connected`). Doors, edge crossers, height changes and tiles no
stock area places are excluded. `nwnpack tile-report` prints the survey behind that rule: of 55 four-Floor-corner tiles
in `tdc01`, 12 beyond the current pool pass; the rest have a blocked centre or slot or a split walkable area. Decorative
tiles get about a quarter of the full-floor weight. Use it by pointing a recipe's `tilepack` line at the rich file;
the default pack and every existing recipe are unchanged (the plain pack is still byte-identical).

`dgcheck --root INSTALL` now re-applies the same walkmesh test to every full-floor tile it finds, independent of the
generator, and prints which full-floor tile models the module uses. A pack with an unsafe tile fails it (checked by
forcing `tdc01_o03_01`, whose centre is blocked, into the pool).

## Corridor kit and doors (`tilepack-kit`, `kit-report`, opt-in)

Corridors in the plain and rich packs are wide strips of full-floor cells, which cannot carry a door. The stock `tdc01` corridor kit
can: all-Wall tiles whose edges carry the `Corridor` crosser (`g01` elbows, `g02` straights, `g07_01` a straight with a plain door) and
the room-wall connector `c13_01`. `nwnpack kit-report` prints how stock areas use these tiles and dumps every stock `Door List` entry on a
door tile (type, position relative to the tile, bearing), which is where the generator's door placement comes from.
`nwnpack tilepack-kit OUT` writes the rich pack plus `kit SIG EDGES weight tile orient [door X Y Z BEARING]` records, one per
orientation of each admitted tile. A tile is admitted only if the game's own walkmesh joins the midpoint of each Corridor edge, each
all-Floor edge and the door slot into one walkable region. `nwnpack templates` now also writes `tpl_door.git` (stock `nw_door_ttr_08`,
verified closed, unlocked and not plot in the source module). Use them through the recipe keys `corridors kit`, `doors plain` and
`template_door` (see USAGE.md).

`dgcheck --root` validates kit modules independently: edge adjacency from the `.set`, one walkable region over cells joined by shared
open sides (full-floor sides and Corridor edges), every kit tile's walkmesh, and every door (one per door-tile slot read from the `.set`,
at the rotated slot position and bearing, closed, unlocked, not plot, nothing stray). Negative cases (a wrong edge, a missing or extra door,
a wrong door type or facing) are exercised in `tests/dungeon_e2e.sh`.
