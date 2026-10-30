/* ERF container (NWN1 V1.0 / NWN2 V1.1; MOD / ERF / HAK) reader and
 * canonical writer, selected by a game profile.
 * All on-disk integers are little-endian and handled explicitly. */
#ifndef NWNDGEN_ERF_H
#define NWNDGEN_ERF_H

#include <stddef.h>
#include <stdint.h>

#include "game.h"

#define ERF_HEADER_SIZE 160
#define ERF_MAX_ENTRIES 200000u

typedef struct {
    char name[ERF_NAME_BUF]; /* lowercase resref, NUL-terminated */
    uint16_t type;
    const uint8_t *data; /* points into an owning buffer */
    uint32_t size;
} erf_res;

typedef struct {
    const game_profile *game;
    char sig[5];             /* "MOD ", "ERF " or "HAK " */
    uint32_t desc_strref;
    uint32_t loc_count;
    const uint8_t *loc;      /* localized-string block, preserved verbatim */
    uint32_t loc_size;
    erf_res *res;            /* sorted by (name, type) after erf_parse */
    size_t nres, cap;
    uint8_t *owned;          /* file buffer owned by this archive, or NULL */
} erf;

void erf_init(erf *e, const char *sig, const game_profile *g);
/* Takes ownership of buf on success AND on failure. Resources keep pointing
 * into buf, so the archive must outlive nothing but itself. */
int erf_parse(uint8_t *buf, size_t len, erf *e, const game_profile *g,
              char *err);
void erf_free(erf *e);

/* Name is lowercased; returns -1 (with err) if it is not a valid resref. */
int erf_add(erf *e, const char *name, uint16_t type, const uint8_t *data,
            uint32_t size, char *err);
const erf_res *erf_find(const erf *e, const char *name, uint16_t type);

int erf_name_valid(const char *name, unsigned max);

/* Serialize canonically: resources sorted by (name, type), sequential
 * resource IDs, fixed build metadata, zeroed reserved bytes. */
int erf_write(const erf *e, uint8_t **out, size_t *outlen, char *err);

#endif
