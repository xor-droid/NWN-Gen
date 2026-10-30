/* Room-variant catalog: tab-separated, seven columns, no header.
 *   slot  variant  min_level  max_level  min_party  max_party  erf_path
 * Rows are sorted into a canonical (slot, variant) order after parsing, so
 * input row order never influences generation. */
#ifndef NWNDGEN_CATALOG_H
#define NWNDGEN_CATALOG_H

#include <stddef.h>
#include <stdint.h>

#include "erf.h"

#define CAT_ID_MAX 32
#define CAT_MAX_ROWS 4096
#define CAT_MAX_LINE 4096

typedef struct {
    char slot[CAT_ID_MAX + 1];
    char variant[CAT_ID_MAX + 1];
    uint32_t min_level, max_level, min_party, max_party;
    char *relpath;
    /* Filled in by plan_load_variants. */
    char *path;
    erf archive;
    uint8_t sha[32];
    int loaded;
} cat_row;

typedef struct {
    cat_row *rows;
    size_t n;
} catalog;

int catalog_parse(const char *text, size_t len, catalog *c, char *err);
void catalog_free(catalog *c);
int catalog_id_valid(const char *s);

#endif
