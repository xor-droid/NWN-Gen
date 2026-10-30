/* Target-game profiles. The --game gate selects one; every archive read or
 * written must match it, so an NWN1 base can never be mixed into NWN2 output. */
#ifndef NWNDGEN_GAME_H
#define NWNDGEN_GAME_H

#include <stddef.h>
#include <stdint.h>

/* NWN1 and NWN2 resource type IDs used by area contracts. */
#define RES_NSS 2009
#define RES_NCS 2010
#define RES_ARE 2012
#define RES_IFO 2014
#define RES_GIT 2023
#define RES_GIC 2046
#define RES_JRL 2056
#define RES_TRN 3002 /* NWN2 terrain source (verified in stock NWN2 modules) */
#define RES_TRX 3035 /* NWN2 compiled terrain */

#define ERF_NAME_BUF 33 /* longest resref (32, NWN2) plus NUL */

typedef struct {
    const char *name;        /* "nwn1" | "nwn2" */
    const char *label;       /* human readable */
    const char *erf_version; /* "V1.0" | "V1.1" */
    unsigned name_max;       /* resref bytes in a key entry */
    unsigned key_size;       /* bytes per key entry */
    const uint16_t *area_parts; /* resources that make one area, ascending */
    size_t n_area_parts;
} game_profile;

extern const game_profile GAME_NWN1;
extern const game_profile GAME_NWN2;

const game_profile *game_by_name(const char *name);
/* The profile whose ERF version string matches, or NULL. */
const game_profile *game_by_erf_version(const char *v4);
const char *res_ext(uint16_t type); /* "are", "trx", ... or "?" */
int res_type_by_ext(const char *ext); /* type ID for "ncs", "utc", ...; -1 if unknown */
/* GFF 4-char type for a part, or NULL if opaque (TRN/TRX use magic "NWN2"). */
const char *res_gff_type(uint16_t type);

#endif
