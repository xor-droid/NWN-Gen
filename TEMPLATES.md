# Template and asset contract

## Missing prerequisite

No valid base NWN1:EE module or room variants accompany this archive. Locate them
on the Linux host or create a small base with the NWN Aurora Toolset or verified
compatible tools. Do not use NWN2:EE assets as a substitute.

## Proposed pack layout

```text
assets/
  PACK.md
  shifting_cellars.mod
  rooms.tsv
  variants/
    encounter_a_rats.erf
    encounter_a_goblins.erf
    encounter_b_beetles.erf
    encounter_b_undead.erf
    objective_rescue.erf
    objective_supplies.erf
```

The base should include a safe entry, room slots, safe/rest space, final challenge,
exit/return route, compiled adventure scripts, and all referenced custom resources.
Use stock tilesets and game assets where practical; record precise dependencies.

## Proposed catalog

Seven tab-separated columns, no header; `#` comments and blank lines allowed:

```text
# slot  variant  min_level  max_level  min_party  max_party  relative_erf_path
```

Example records below are shown with actual tabs:

```tsv
encounter_a	rats	1	2	1	1	variants/encounter_a_rats.erf
encounter_a	goblins	1	2	1	1	variants/encounter_a_goblins.erf
encounter_b	beetles	1	2	1	1	variants/encounter_b_beetles.erf
encounter_b	undead	1	2	1	1	variants/encounter_b_undead.erf
objective	rescue	1	2	1	1	variants/objective_rescue.erf
objective	supplies	1	2	1	1	variants/objective_supplies.erf
```

Resolve paths relative to the catalog, independent of the working directory.
Allow spaces in paths by using tabs, not generic whitespace splitting. Stable IDs
can use lowercase ASCII letters, digits, and underscores. Reject duplicate slot/
variant pairs. All slots need at least one eligible variant for the requested band.

This schema handles independent slots only. Add compatibility/objective-bundle
metadata before independently selecting dependent quest components.

## Slot compatibility requirements

Each variant replaces complete ARE/GIT/GIC triplets for existing base area names.
All variants for a slot must own the same area-resource set. Different slots must
not own overlapping resources. Validate this contract before choosing variants,
so a seed cannot unpredictably discover a collision.

All variants must preserve or deliberately satisfy:

- Valid, walkable arrival coordinates and stable entrance/exit connections.
- Required door and waypoint tags, transition destinations, and scripts.
- Encounter placement reachable by the player and correctly aligned to terrain.
- A safe path for mandatory quest interactions and a reachable escape route.
- Consistent quest item identifiers, completion checks, and rewards.
- Any resting restrictions and shortcut activation rules.
- All referenced resources available in the base, game, or declared dependencies.

Compiled scripts must not use unseeded random generation for content advertised
as fixed by the offline seed. Ordinary game combat randomness is expected.

## Authoring workflow

1. Create and playtest the base module with fixed rooms and one complete objective.
2. Make room variants that retain the connection/quest contracts.
3. Export or pack only the owned ARE/GIT/GIC resources into each variant ERF.
4. Ensure shared scripts/blueprints are already in the base and compile scripts.
5. Declare supported level/party bands based on playtests, not a formula alone.
6. Validate every variant and document tested dependencies in PACK.md.
7. Generate several seeds and load the resulting modules in NWN:EE.

If a test pack is synthetic and has no playable areas or real module metadata,
label it **format-test fixture only**. Do not ship it as the initial adventure.
