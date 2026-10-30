/* Spawn planning: which marker waypoints each generated area gets (see assets/scripts/scel_enter.nss).
 * Uses its own RNG domains ("nwndgen/theme/1", "nwndgen/spawn/1") so layouts never change. */
#ifndef NWNDGEN_SPAWN_H
#define NWNDGEN_SPAWN_H

#include "dungeon.h"
#include "roster.h"

#define SPAWN_ALGORITHM_VERSION "spawn/2"
#define LOOT_ALGORITHM_VERSION "loot/1" /* carried items and gold per creature, see plan_loot */
#define DRESSING_ALGORITHM_VERSION "rooms/1" /* room types and furniture, see plan_rooms */
#define CORRIDOR_ALGORITHM_VERSION "corridor/1" /* props along kit corridors, see plan_corridor */
#define LOCKS_ALGORITHM_VERSION "locks/1" /* locked doors, keys and locked chests, see plan_locks */
#define HENCH_ALGORITHM_VERSION "hench/1" /* which stock henchmen join the first player, see spawn_plan */
#define SPAWN_AREA_MAX 24 /* creatures per area, boss excluded */
#define SPAWN_PT_MAX 192  /* markers per area: creatures, objective, scenery, room dressing */

/* CR caps in 1/100 CR: regular creatures up to the band's average level, the boss half a level above
 * (a full level above for parties). Heuristics, not playtested balance. */
int spawn_regular_cap(const spawn_cfg *sc);
int spawn_boss_cap(const spawn_cfg *sc);
/* Decor markers of an area that stand on kit corridor cells (corridor dressing). */
int spawn_corridor_props(const dungeon_area *a);
/* Locked doors, keyed doors and locked containers in an area (recipe key locks). */
void spawn_lock_counts(const dungeon_area *a, int *doors, int *keyed, int *chests);
/* Fills d->area[i].sp/nsp, d->theme. Fails if the roster has no theme with a boss under the cap. */
int spawn_plan(dungeon *d, const spawn_cfg *sc, uint64_t seed, const char *module_tag, char *err);

#endif
