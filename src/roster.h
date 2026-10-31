/* Creature roster and quest text for generated dungeons.
 *
 *   nwndgen-roster 1
  *   henchman <prefix>              a stock henchman family (optional, up to 6): "<prefix>_NN" must exist for NN 01-11, level NN+3
*   vault <utp resref>              locked chest that holds the relic
 *   key <uti resref>                item the boss carries; opens the vault
 *   relic <uti resref>              the objective item
 *   lever <utp resref>              (optional) usable lever placeable (plc_flrlever1); used by the recipe key levers
 *   theme <id> <display name...>    starts a theme; the role lines below belong to it
 *   minion|standard|elite|boss <utc resref> <CR>      CR is decimal, at most two places (0.25)
 *   prop|light|container <utp resref> <weight>        scenery for the theme (weight 1-100, optional)
 *   roomprop <type> <utp resref> <weight> [centre]     (global, may stand anywhere after the header) furniture for rooms of a type;
 *                                                       `centre` marks a centrepiece: at most one per room, in the middle of a tile near the room's centre
 *                                                       (barracks storage shrine library crypt prison); inert blueprints only; used by the recipe key `dressing`
 *   corridorprop <utp resref> <weight>                 (global) scenery for kit corridors (rubble, bones, torches ...); inert blueprints only; used by recipe key `corridor_dressing`
 *   ambience <ambient> <diffuse> <fog> <amount> <clip>  area light for the theme: three RRGGBB hex colours,
 *                                                       fog amount 0-15, fog clip distance 20-150 (optional)
 *   loot <uti resref> <chance> <min CR> <max CR>        item a creature of the theme may carry when its CR is in the range;
 *                                                       chance is a percent per creature (1-100); a boss gets up to two eligible items, picked by chance weight (optional)
 *   gold <gp per CR>                                    creatures carry about this much gold per point of CR (optional)
 *
 * Every blueprint is a stock hostile creature/placeable/item that exists in the base game;
 * `nwnpack roster-check` verifies that against an install. '#' starts a comment line. */
#ifndef NWNDGEN_ROSTER_H
#define NWNDGEN_ROSTER_H

#include <stddef.h>
#include <stdint.h>

#define ROSTER_THEMES 8
#define ROSTER_POOL 16
#define ROSTER_NAME_MAX 64
#define ROSTER_CR_MAX 4000 /* 1/100 CR */

enum { ROLE_MINION, ROLE_STANDARD, ROLE_ELITE, ROLE_BOSS, ROLE_N };
extern const char *const ROLE_NAME[ROLE_N];

typedef struct { char resref[17]; int cr; } roster_entry; /* cr in 1/100 */
typedef struct { char resref[17]; int chance, min_cr, max_cr; } roster_loot; /* CRs in 1/100 */
enum { DECO_PROP, DECO_LIGHT, DECO_CONTAINER, DECO_N };
extern const char *const DECO_NAME[DECO_N];
/* Room types for `dressing`: each room gets one type and is furnished from the roomprop list of that type. */
enum { RT_BARRACKS, RT_STORAGE, RT_SHRINE, RT_LIBRARY, RT_CRYPT, RT_PRISON, RT_N };
extern const char *const RT_NAME[RT_N];
typedef struct {
    char id[17], name[ROSTER_NAME_MAX];
    roster_entry e[ROLE_N][ROSTER_POOL];
    size_t n[ROLE_N];
    roster_entry deco[DECO_N][ROSTER_POOL]; /* .cr holds the weight here */
    size_t ndeco[DECO_N];
    int has_ambience;
    uint32_t amb_ambient, amb_diffuse, amb_fog; /* ARE colour DWORDs: R | G<<8 | B<<16 */
    int amb_fog_amount, amb_fog_clip;
    roster_loot loot[ROSTER_POOL];
    size_t nloot;
    int gold_per_cr;                 /* 0 = no gold */
} roster_theme;
typedef struct {
    roster_theme theme[ROSTER_THEMES];
    size_t ntheme;
    char vault[17], key[17], relic[17], lever[17]; /* lever: optional, a usable placeable for recipe key levers */
    char hench[6][14];             /* henchman family prefixes, see henchman above */
    size_t nhench;
    roster_entry rprop[RT_N][ROSTER_POOL]; /* roomprop lines by room type; .cr holds the weight */
    size_t nrprop[RT_N];
    roster_entry cprop[ROSTER_POOL];       /* corridorprop lines: scenery for kit corridors; .cr holds the weight */
    size_t ncprop;
    unsigned char rcentre[RT_N][ROSTER_POOL]; /* 1 = a centrepiece (stands in the middle of a room tile), 0 = a corner piece */
} roster;

/* Inputs the spawn planner needs besides the roster. */
typedef struct { const roster *ro; unsigned party, min_level, max_level; int boss_only, loot_test, henchmen, loot_amount, dressing, corridor_dressing, locks, ambush, traps, levers, patrols, encounters; } spawn_cfg; /* loot_amount: 0 mid, 1 low, 2 high; dressing, corridor_dressing, locks, ambush and traps: 0 off, 1 low, 2 mid, 3 high; levers: 0 off, 1 on; patrols and encounters: 0 off, 1 low, 2 mid, 3 high */

/* A spawn marker: kind is one of C (creature) B (boss) T (boss with 1 hit point, boss_only runs) V (vault)
 * K (key item) R (relic item) D (decor prop) L (light) X (lootable container) P (floor trap, recipe key traps) E (lever, recipe key levers). */
typedef struct {
    char kind; char bp[17]; int cx, cy, face; int ox, oy; /* tile corner offset: -1, 0 or 1 (3.5 m) */
    char loot[2][17];   /* items a creature or boss carries (empty = none), see loot in the roster */
    int gold;           /* gold pieces it carries */
    int custom, xcm, ycm; /* custom = 1: the offset from the tile centre is (xcm, ycm) centimetres instead of 3.5 m * (ox, oy) (wall-side and corridor pieces); ox, oy keep the direction signs */
    int weak;           /* loot_test: the script gives it 1 hit point and takes its weapons */
    char keybp[17], keytag[40]; /* locks: a creature that carries a door key: the key item blueprint and its tag (empty = none) */
    int lockdc;         /* locks: a lootable container (X) that starts locked: the DC to pick it (0 = unlocked) */
    int amb;            /* ambush: group id (1..) of a creature that stays hidden until the group is sprung, or of the chest that springs it (0 = none) */
    int trap, trapdc;   /* traps: a trap marker (P) or a container that starts trapped (trapdc > 0): the TRAP_BASE_TYPE number and the detect and disarm DC */
    int nrt, rt[2][2];  /* patrols: the extra route cells of a creature (nrt 1 or 2, cell x,y each); it walks between its own spot and these */
    int gate;           /* levers: a lever marker (E): the gate door it opens, as an index into the area's doors plus 1 */
} spawn_pt;

int roster_parse(const char *text, size_t len, roster *ro, char *err);
/* "0.25" -> 25. Returns -1 on bad syntax. */
int roster_parse_cr(const char *s);

/* Journal (module.jrl, GFF "JRL ") for the relic quest, tag "scel_main". */
int roster_build_jrl(const char *quest_name, const char *theme_name, uint8_t **buf, size_t *len, char *err);

#endif
