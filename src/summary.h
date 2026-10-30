/* Human-readable description of a built dungeon (printed after a successful --recipe run). */
#ifndef NWNDGEN_SUMMARY_H
#define NWNDGEN_SUMMARY_H

#include "config.h"
#include "dungeon.h"
#include "recipe.h"
#include "roster.h"

/* Levels (areas), rooms, doors, creatures by type and count, boss, objective, scenery and the level/party band the dungeon was
 * built for. `ro` is NULL when the recipe has no roster. Returns malloc'd text, or NULL if out of memory. */
char *dungeon_summary(const recipe *r, const dungeon *d, const roster *ro, const config *cfg);

#endif
