# NWN-Gen (`nwndgen`)

A command-line generator of **randomized, replayable dungeons for Neverwinter Nights: Enhanced Edition**. Give it a seed and it writes a
ready-to-load `.mod` module: a chain of procedurally laid-out dungeon areas, monsters to fight, a boss who carries a key, a locked vault
and a relic to retrieve, plus a journal that tracks the quest. The same seed always produces the same module, byte for byte; a new seed
gives a new dungeon.

It is written in portable C11/POSIX with no runtime dependencies. Nothing from the game is stored in this repository: you point the
tools at **your own** NWN:EE install, and they read the tilesets, walkmeshes and creature blueprints from it.

## What it makes

For each seed, `nwndgen --recipe` builds a module of linked areas (for example "Upper / Middle / Lower Cellars"):

* **Layouts from the seed.** Every area gets fresh rooms and corridors, tiled only with stock `tdc01` (Dungeon) pieces that the game's own
  areas really place, so the tile edges match and the walkmesh is continuous. Areas are joined by stairs-style transition triggers.
* **Creatures and a goal.** One theme per seed (goblins, kobolds or the restless dead, from `assets/roster.txt`) supplies stock hostile
  creatures sized to the party's level and size. The boss in the last room holds a key to a plot-protected chest containing the relic.
  Small compiled scripts spawn everything when the first player enters each area and advance a three-step journal entry.
* **Less-empty rooms (optional).** Props, light sources, unlocked containers and a per-theme ambient colour; decorative floor tiles
  (`tilepack-rich`).
* **Corridors with doors (optional, newest).** Rooms joined by the stock 5 m corridor kit instead of wide strips, with plain closed,
  unlocked doors in the corridors (`corridors kit`, `doors plain`).
* **Determinism.** Output depends only on the seed, party size, level band and the input files, not on the machine, locale, time zone or
  working directory. Archives are written canonically so the bytes are stable.

## Status, stated plainly

* Confirmed in the game by the author: the generated modules load, and the creatures, key, vault, relic and journal chain work; the
  scenery and rich-floor modules also load.
* **Not yet game-tested:** the corridor kit and doors (just built), game balance, and several earlier builds' play-through reports.
  Automated checks pass, but the validator is not the game. Please report what you see.
* Balance numbers are heuristics, not playtested. NWN1 only for generated dungeons (`--game nwn2` supports only the older room-swap mode).

## Quick start

You need NWN:EE installed (the tools read `data/*.key`/`*.bif` and `data/mod`) and a C compiler and `make`.

```sh
make                      # builds build/nwndgen and the helper tools
make test                 # unit + end-to-end tests (no game install needed)

export NWN_ROOT="/path/to/Neverwinter Nights"
mkdir -p out
build/nwnpack check                                        # verify tile conventions against your install
build/nwnpack tilepack-kit out/tilepack_tdc01_kit.txt      # or `tilepack` / `tilepack-rich` for wide corridors
build/nwnpack templates out/                               # donor area/git/door templates + base module
```

Write a recipe (format in [USAGE.md](USAGE.md)), for example:

```
nwndgen-recipe 1
tilepack tilepack_tdc01_kit.txt
template_are tpl_are.are
template_git tpl_git.git
template_door tpl_door.git
corridors kit
doors plain
module_name The Shifting Cellars
module_tag SHIFTING_CELLARS
support 1 6 1 4
area scel_a 14 14 3 Shifting Cellars - Upper
area scel_b 14 14 3 Shifting Cellars - Middle
area scel_c 16 16 4 Shifting Cellars - Lower
roster ../assets/roster.txt
scripts ../assets/scripts
```

then generate and validate:

```sh
build/nwndgen --game nwn1 --base out/base_module.mod --recipe out/recipe.txt --seed 12345 \
  --output out/cellars-12345.mod --manifest out/cellars-12345.txt
build/dgcheck --root "$NWN_ROOT" --roster assets/roster.txt out/cellars-12345.mod
```

Copy the `.mod` into your `Documents/Neverwinter Nights/modules` folder and load it. Options: `--party-size`, `--min-level`,
`--max-level`. Leave out `corridors`/`doors` for the original wide corridors; leave out `roster`/`scripts` for geometry only.

## Tools

| tool | purpose |
|---|---|
| `build/nwndgen` | the generator (`--recipe` dungeons for NWN1; `--templates` room-variant overlay for NWN1/NWN2) |
| `build/nwnpack` | reads your install: tile packs, templates, roster checks, reports |
| `build/dgcheck` | independent validator for a built module (tile adjacency, walkmeshes, connectivity, doors, spawn markers, scenery) |
| `build/dgdump`, `build/erfpack`, `build/erfcheck` | layout dumps, deterministic ERF packer, strict ERF checker |
| `tools/sweep_seeds.sh` | generate and validate a range of seeds |

## Documentation

* [USAGE.md](USAGE.md): recipe keys, creatures and objective, scenery, corridor kit and doors, determinism contract, limits.
* [tools/README.md](tools/README.md): authoring tools and how packs are built from your install.
* [assets/PACK.md](assets/PACK.md): what the shipped roster and scripts are.
* Design background: [REQUIREMENTS.md](REQUIREMENTS.md), [ARCHITECTURE.md](ARCHITECTURE.md), [DUNGEON_DESIGN.md](DUNGEON_DESIGN.md),
  [VALIDATION.md](VALIDATION.md), [docs/HANDOFF.md](docs/HANDOFF.md) (original handoff notes).

## Layout

`src/` generator library and CLI, `tools/` authoring tools (C and POSIX sh), `tests/` unit and end-to-end tests with synthetic fixtures,
`assets/` roster and scripts, `attic/` retired Python scripts (superseded by C).

## Licence

No licence has been chosen yet. NWN content belongs to its owners and is never included here.
