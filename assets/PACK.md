# Content pack status

See `tools/README.md` and `USAGE.md` ("Procedural dungeons") for the tile-dungeon route:
`nwndgen --recipe` generates fresh layouts per seed in C. It builds geometry and transitions only
(no creatures, scripts or objective yet) and has not been game-loaded.

**There is no playable content pack yet.** Nothing in this repository is a
playable NWN1:EE adventure.

`build/fixtool fixture` builds a **format-test fixture only**: a synthetic base
`.mod` and room-variant ERFs whose GFF payloads are structurally valid but hold
no real area data. It exists to test the archive reader/writer, catalog, seeded
selection and publication logic. It cannot be loaded in the game.

## What a real pack needs (not available on this host)

Host state (2026-10-29): NWN:EE r1284 is installed at
`C:\Program Files (x86)\Steam\steamapps\common\Neverwinter Nights` (retail data,
all campaigns, `nwscript.nss` in `ovr/`, Windows toolset `nwtoolset.exe` that cannot
be driven from Linux) and NWN2:EE at `...\common\NWN2 Enhanced Edition`. neverwinter.nim
2.3.1 (`nwn_gff`, `nwn_erf`, `nwn_script_comp`, `nwn_resman_*`) is available for
authoring. No NWN1/NWN2 adventure pack has been built yet.

NWN2 modules are ERF V1.1 and each area is ARE+GIT+GIC+TRN+TRX (terrain 16-17 MB);
they must be authored in the NWN2 toolset.

1. A base `shifting_cellars.mod` authored in the NWN1:EE toolset: `module.ifo`,
   a fixed entry area, area slots (e.g. `enc_a`, `enc_b`, `obj_room`), compiled
   `.ncs` scripts, blueprints, door/waypoint contracts, one complete objective.
2. For every slot, two or more variant ERFs holding that slot's complete
   `.are`/`.git`/`.gic` triplet(s) under the same resrefs as the base.
3. A `rooms.tsv` catalog (see USAGE.md). Declare level/party bands only after
   playtesting them.
4. A record of tested game version and dependencies in this file.

Slot contract enforced by the generator: variants may contain only complete
ARE/GIT/GIC triplets that already exist in the base; all variants of a slot own
the same resrefs; different slots never share one; `module.ifo`, scripts and
blueprints can never be overridden.
