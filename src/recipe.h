/* Dungeon recipe: the text file that tells the generator which areas to build.
 *
 *   nwndgen-recipe 1
 *   tilepack tilepack_tdc01.txt          (paths are relative to the recipe file)
 *   template_are tpl_are.are             (donor ARE, binary GFF)
 *   template_git tpl_git.git             (donor GIT: AreaProperties + a transition trigger)
 *   module_name <text>
 *   module_tag <TAG>
 *   module_description <text>
 *   support <min_level> <max_level> <min_party> <max_party>
 *   area <resref> <W> <H> <rooms> <display name...>      (one per area, chained in order)
 *   roster <file>                        (optional) creature roster, see roster.h; needs scripts
 *   scripts <dir>                        (optional) directory holding scel_enter/scel_acquire .nss/.ncs
 *   spawn_mode full|boss_only|loot_test  (optional) boss_only = just the boss, with 1 hit point; loot_test = five weak creatures + weak boss, all in the first area
 *   henchmen N                           (optional, 0-5, default 0) N stock henchmen from the roster join the first player
 *   loot_amount low|mid|high           (optional, default mid) how much creatures carry: low = half the item chances and gold, high = 1.5x the chances (at most 100%) and double gold
 *   dressing off|low|mid|high           (optional, default off) furnish rooms by type (barracks, storage, shrine, library, crypt, prison) from the roster's roomprop lines: low = 1 piece per room, mid = 2 and some pairs, high = 3 and more pairs; needs roster and scripts
 *   corridor_dressing off|low|mid|high  (optional, default off) props along the stock kit corridors from the roster's corridorprop lines; needs roster, scripts and corridors kit
 *   locks off|low|mid|high            (optional, default off) lock corridor doors (low: one in half the areas, mid: half the eligible links, high: all of them; never the way out of the arrival room) and some chests; some doors need a key carried by a creature in an earlier room, the others can be picked or bashed; needs roster, scripts, corridors kit and doors
 *   ambush off|low|mid|high           (optional, default off) hide some room creatures until a chest in their room is opened or a player comes near (low: one group in half the areas, mid: one per area, high: up to two); the creatures are the ones already planned, so totals do not change; needs roster and scripts
 *   traps off|low|mid|high             (optional, default off) hidden floor traps in rooms and corridors (low 1, mid 3, high 5 per area) and trapped chests (0, 25, 40 in 100), the strength grows with the character level; every trap can be found and disarmed; needs roster and scripts
 *   levers on|off                      (optional, default off) turn a keyless locked door of each area into a gate that a lever in a room before it opens (the gate can still be bashed); needs locks and the roster's lever line
 *   patrols off|low|mid|high           (optional, default off) some room creatures walk a short route inside their room instead of standing still (low: one in half the areas, mid: one per area, high: two); needs roster and scripts
 *   encounters off|low|mid|high        (optional, default off) random encounters at play time: a group of the dungeon's own creatures turns up when a player rests (15, 30, 50 in 100) or after a long quiet spell, at most 2, 4 or 6 times per area; needs roster and scripts
 *   seed N                              (optional) run defaults: unsigned 64-bit seed, party size 1-32, character levels 1-40;
 *   party_size N                         the command-line options of the same name override them
 *   min_level N | max_level N
 *   template_door tpl_door.git           (optional) donor GIT with one stock door; needed by doors plain
 *   corridors wide|kit                  (optional) kit = stock corridor-kit pieces (needs a tilepack with kit records)
 *   doors none|plain|mixed|doorway           (optional) plain = a closed, unlocked door in each kit corridor; mixed = the same with every door look the pack has; doorway = a door of one of several looks at each room entrance; all need corridors kit
 *
 * '#' starts a comment line. Fields are separated by single spaces. */
#ifndef NWNDGEN_RECIPE_H
#define NWNDGEN_RECIPE_H

#include <stddef.h>
#include <stdint.h>

#define RECIPE_MAX_AREAS 16
#define RECIPE_PATH_MAX 1024
#define RECIPE_AREA_MIN 6
#define RECIPE_AREA_MAX 32 /* engine limit on area width/height in tiles */
#define RECIPE_ROOMS_MIN 2
#define RECIPE_ROOMS_MAX 8

#define RECIPE_HENCH_MAX 5

typedef struct {
    char resref[17];
    unsigned w, h, rooms;
    char name[128];
} recipe_area;

typedef struct {
    char tilepack[RECIPE_PATH_MAX], template_are[RECIPE_PATH_MAX], template_git[RECIPE_PATH_MAX];
    char template_door[RECIPE_PATH_MAX]; /* GIT holding one stock door to clone; needed by doors plain */
    char roster[RECIPE_PATH_MAX], scripts[RECIPE_PATH_MAX]; /* both empty = no creatures or objective */
    int boss_only; /* spawn_mode boss_only: no minions, the boss has 1 hit point (quick test runs) */
    int loot_test;     /* spawn_mode loot_test: five creatures and the boss, all in the first area, 1 hit point and unarmed (loot checks) */
    int henchmen;      /* henchmen N: stock henchmen from the roster that join the first player (needs roster and scripts) */
    int loot_amount;   /* loot_amount low|mid|high: 1 = low, 0 = mid (default), 2 = high; scales item chances and gold (needs roster and scripts) */
    int corridor_dressing; /* corridor_dressing off|low|mid|high: 0 off (default), 1 low, 2 mid, 3 high; needs roster, scripts and corridors kit */
    int dressing;      /* dressing off|low|mid|high: 0 off (default), 1 low, 2 mid, 3 high; room-type furniture (needs roster and scripts) */
    int locks;         /* locks off|low|mid|high: 0 off (default), 1 low, 2 mid, 3 high; locked doors, keys and locked chests (needs roster, scripts, corridors kit and doors) */
    int ambush;        /* ambush off|low|mid|high: 0 off (default), 1 low, 2 mid, 3 high; hidden creature groups sprung by a chest or by proximity (needs roster and scripts) */
    int traps;         /* traps off|low|mid|high: 0 off (default), 1 low, 2 mid, 3 high; floor traps and trapped containers (needs roster and scripts) */
    int patrols;       /* patrols off|low|mid|high: 0 off (default), 1 low, 2 mid, 3 high; room creatures that walk a route (needs roster and scripts) */
    int encounters;    /* encounters off|low|mid|high: 0 off (default), 1 low, 2 mid, 3 high; random encounters while playing (needs roster and scripts) */
    int levers;        /* levers on|off: 1 turns some keyless locked doors into gates that a lever in an earlier room opens (needs roster, scripts and locks) */
    int has_seed, has_party, has_min_lvl, has_max_lvl; /* run defaults given in the recipe; the command line overrides them */
    uint64_t run_seed;
    unsigned run_party, run_min_lvl, run_max_lvl;
    int kit_corridors; /* corridors kit: narrow stock corridor pieces instead of full-floor 10 m corridors */
    int doors;         /* 1 = doors plain: one closed, unlocked stock door in each kit corridor; 2 = doors mixed: the same, with every door look the pack has; 3 = doors doorway: a generic-look door in every corridor entrance (all need corridors kit) */
    char module_name[128], module_tag[33], module_desc[512];
    unsigned min_level, max_level, min_party, max_party;
    recipe_area areas[RECIPE_MAX_AREAS];
    size_t nareas;
} recipe;

/* dir: directory the relative paths are resolved against ("" = current). */
int recipe_parse(const char *text, size_t len, const char *dir, recipe *r, char *err);

#endif
