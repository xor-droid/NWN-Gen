/* Creature roster and quest text for generated dungeons.
 *
 *   nwndgen-roster 1
 *   vault <utp resref>              locked chest that holds the relic
 *   key <uti resref>                item the boss carries; opens the vault
 *   relic <uti resref>              the objective item
 *   theme <id> <display name...>    starts a theme; the role lines below belong to it
 *   minion|standard|elite|boss <utc resref> <CR>      CR is decimal, at most two places (0.25)
 *   prop|light|container <utp resref> <weight>        scenery for the theme (weight 1-100, optional)
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
    char vault[17], key[17], relic[17];
} roster;

/* Inputs the spawn planner needs besides the roster. */
typedef struct { const roster *ro; unsigned party, min_level, max_level; int boss_only; } spawn_cfg;

/* A spawn marker: kind is one of C (creature) B (boss) T (boss with 1 hit point, boss_only runs) V (vault)
 * K (key item) R (relic item) D (decor prop) L (light) X (lootable container). */
typedef struct {
    char kind; char bp[17]; int cx, cy, face; int ox, oy; /* tile corner offset: -1, 0 or 1 (3.5 m) */
    char loot[2][17];   /* items a creature or boss carries (empty = none), see loot in the roster */
    int gold;           /* gold pieces it carries */
} spawn_pt;

int roster_parse(const char *text, size_t len, roster *ro, char *err);
/* "0.25" -> 25. Returns -1 on bad syntax. */
int roster_parse_cr(const char *s);

/* Journal (module.jrl, GFF "JRL ") for the relic quest, tag "scel_main". */
int roster_build_jrl(const char *quest_name, const char *theme_name, uint8_t **buf, size_t *len, char *err);

#endif
