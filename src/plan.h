/* Variant validation, seeded selection and module assembly. */
#ifndef NWNDGEN_PLAN_H
#define NWNDGEN_PLAN_H

#include <stddef.h>
#include <stdint.h>

#include "catalog.h"
#include "config.h"
#include "erf.h"

#define GENERATOR_VERSION "0.12.0"
/* Bump when PRNG, stream derivation, ordering or selection rules change. */
#define ALGORITHM_VERSION "splitmix64-sha256slot/1"

typedef struct {
    size_t nslots;
    size_t *chosen; /* catalog row index per slot, slots in sorted order */
} plan;

/* Check a GFF V3.2 header (magic type, version, table bounds). */
int gff_check(const uint8_t *d, size_t n, const char *type4, char *err);

/* Validate the base module (MOD, V1.0, module.ifo present and GFF-valid). */
int base_validate(const erf *base, char *err);

/* Read every catalog variant (paths relative to cat_dir, which may be "")
 * and verify the slot/ownership contract against the base. */
int plan_load_variants(catalog *c, const char *cat_dir, const erf *base,
                       char *err);

/* Hash of the canonical catalog (rows sorted; variant bytes, not paths). */
void catalog_canonical_sha(const catalog *c, uint8_t out[32]);

int plan_select(const catalog *c, const config *cfg, plan *p, char *err);
void plan_free(plan *p);

/* Build the output archive: base resources with chosen variants overlaid.
 * Payload pointers reference base/catalog buffers; keep them alive. */
int plan_apply(const erf *base, const catalog *c, const plan *p, erf *out,
               char *err);

#endif
