# Proposed first adventure: The Shifting Cellars

These are design suggestions from the original discussion, not fixed engine
requirements. Prioritize a working, fair solo adventure and reproducible output.

## Premise and loop

An abandoned roadside inn conceals cellars changed by a lingering curse. Generate
a fresh module outside the game, start a new game with it, explore, complete the
objective, and escape. Generate another module with another seed to replay.

Initial target: roughly 20–30 minutes, one level 1–2 character, no companion.
Actual duration and difficulty need playtesting across classes.

## Initial structure

- Entrance with a clear objective and a route back out.
- Two encounter areas with small enemy groups and readable danger cues.
- A safe halfway space with a useful healing supply and rest opportunity.
- An objective area and a final challenge, possibly sharing one area in the MVP.
- A return shortcut on completion.

Start with a connected chain or small graph of authored room areas. Randomize
room layouts within safe doorway/arrival contracts, encounters, loot, and goals.
Later add more routes and branches after connectivity validation exists.

## Encounter and reward ideas

| Element | Candidate content |
| --- | --- |
| Creatures | Rats, beetles, goblins, deliberately weakened undead |
| Hazards | Telegraphed traps, poisonous fungi, unstable floor interactions |
| Objectives | Recover stolen supplies; rescue a traveler; break a cellar curse |
| Final challenges | Goblin scavenger leader; oversized beetle; weak skeletal guardian |
| Rewards | Modest gold, healing supplies, occasional minor useful equipment |

Do not assume that a stock blueprint is fair merely because its name appears in
this table. Check its statistics and test it in the game. Avoid unavoidable
instant death, disabling effects, or swarms against fragile level 1 characters.

## Class accessibility

Mandatory progress must not require lockpicking, spellcasting, disarming traps,
or passing a class-specific skill check. Locked chests and special interactions
may give optional rewards. Make the last fight avoidable, weakenable, or safely
retreatable where practical. Verify the rest policy and consumable usefulness
for different classes, rather than assuming healing potions solve every matchup.

## Solvability invariants

Every selected set of rooms must have a reachable entrance, objective, and exit.
Required keys/items must be reachable before any gate that requires them. A
rescue NPC must have a viable completion interaction; a curse objective must have
a supported non-spellcaster solution. Random selection must not pair unrelated
objective items and completion scripts.

Represent objectives as compatible bundles if necessary. Entire coherent quest
variants are safer than independently shuffling their dependent components.
