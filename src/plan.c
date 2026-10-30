#include "plan.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "prng.h"
#include "sha256.h"
#include "util.h"

int gff_check(const uint8_t *d, size_t n, const char *type4, char *err)
{
    static const struct { int off; unsigned esz; } tab[3] = {
        {8, 12}, {16, 12}, {24, 16}}; /* structs, fields, labels: count*size */
    int i;

    if (n < 56) {
        set_err(err, "GFF data is too short (%zu bytes)", n);
        return -1;
    }
    if (memcmp(d, type4, 4)) {
        set_err(err, "GFF type is not '%s'", type4);
        return -1;
    }
    if (memcmp(d + 4, "V3.2", 4)) {
        set_err(err, "unsupported GFF version (only V3.2)");
        return -1;
    }
    for (i = 0; i < 3; i++) {
        uint64_t off = rd32le(d + tab[i].off);
        uint64_t cnt = rd32le(d + tab[i].off + 4);
        if (off < 56 || off + cnt * tab[i].esz > n) {
            set_err(err, "GFF table %d is out of range", i);
            return -1;
        }
    }
    for (i = 0; i < 3; i++) { /* field data, field indices, list indices */
        uint64_t off = rd32le(d + 32 + 8 * i);
        uint64_t sz = rd32le(d + 36 + 8 * i);
        if (off < 56 || off + sz > n) {
            set_err(err, "GFF data block %d is out of range", i);
            return -1;
        }
    }
    if (rd32le(d + 12) < 1) {
        set_err(err, "GFF has no root struct");
        return -1;
    }
    return 0;
}

int base_validate(const erf *base, char *err)
{
    const erf_res *ifo;
    if (strcmp(base->sig, "MOD ")) {
        set_err(err, "base is not a MOD archive (signature '%s')", base->sig);
        return -1;
    }
    ifo = erf_find(base, "module", RES_IFO);
    if (!ifo) {
        set_err(err, "base module has no module.ifo");
        return -1;
    }
    if (gff_check(ifo->data, ifo->size, "IFO ", err) != 0) {
        char m[ERRLEN];
        snprintf(m, sizeof m, "base module.ifo: %s", err);
        set_err(err, "%s", m);
        return -1;
    }
    return 0;
}

static int is_part(const game_profile *g, uint16_t t)
{
    size_t i;
    for (i = 0; i < g->n_area_parts; i++)
        if (g->area_parts[i] == t)
            return 1;
    return 0;
}

/* Variant must hold only complete areas (every part the game requires) that
 * all exist in the base with the same type. */
static int check_variant(const cat_row *r, const erf *base, char *err)
{
    const erf *v = &r->archive;
    const game_profile *g = base->game;
    size_t i, k;

    if (v->nres == 0) {
        set_err(err, "%s/%s: variant archive is empty", r->slot, r->variant);
        return -1;
    }
    for (i = 0; i < v->nres; i++) {
        const erf_res *x = &v->res[i];
        const char *gt = res_gff_type(x->type);
        char sub[ERRLEN];
        if (!is_part(g, x->type)) {
            set_err(err,
                    "%s/%s: resource '%s' has type %u; %s variants may only "
                    "replace area parts (ARE/GIT/GIC%s)",
                    r->slot, r->variant, x->name, (unsigned)x->type, g->name,
                    g->n_area_parts > 3 ? "/TRN/TRX" : "");
            return -1;
        }
        if (!erf_find(base, x->name, x->type)) {
            set_err(err, "%s/%s: base module has no '%s.%s' to replace",
                    r->slot, r->variant, x->name, res_ext(x->type));
            return -1;
        }
        if (gt) {
            if (gff_check(x->data, x->size, gt, sub) != 0) {
                set_err(err, "%s/%s: '%s.%s': %s", r->slot, r->variant,
                        x->name, res_ext(x->type), sub);
                return -1;
            }
        } else if (x->size < 16 || memcmp(x->data, "NWN2", 4)) {
            set_err(err, "%s/%s: '%s.%s' is not NWN2 terrain data", r->slot,
                    r->variant, x->name, res_ext(x->type));
            return -1;
        }
    }
    /* Sorted by (name, type): each area is a run of exactly the game's parts. */
    if (v->nres % g->n_area_parts != 0) {
        set_err(err, "%s/%s: resources must come in complete %zu-part areas",
                r->slot, r->variant, g->n_area_parts);
        return -1;
    }
    for (i = 0; i < v->nres; i += g->n_area_parts)
        for (k = 0; k < g->n_area_parts; k++)
            if (strcmp(v->res[i].name, v->res[i + k].name) ||
                v->res[i + k].type != g->area_parts[k]) {
                set_err(err,
                        "%s/%s: area '%s' is missing part '%s' (an area needs "
                        "every part the game requires)",
                        r->slot, r->variant, v->res[i].name,
                        res_ext(g->area_parts[k]));
                return -1;
            }
    return 0;
}

static int same_set(const erf *a, const erf *b)
{
    size_t i;
    if (a->nres != b->nres)
        return 0;
    for (i = 0; i < a->nres; i++)
        if (strcmp(a->res[i].name, b->res[i].name) ||
            a->res[i].type != b->res[i].type)
            return 0;
    return 1;
}

int plan_load_variants(catalog *c, const char *cat_dir, const erf *base,
                       char *err)
{
    size_t i, j, dl = strlen(cat_dir);

    for (i = 0; i < c->n; i++) {
        cat_row *r = &c->rows[i];
        uint8_t *buf;
        size_t len;
        char sub[ERRLEN];

        r->path = xmalloc(dl + strlen(r->relpath) + 2);
        if (dl)
            sprintf(r->path, "%s/%s", cat_dir, r->relpath);
        else
            strcpy(r->path, r->relpath);
        if (read_file(r->path, &buf, &len, err) != 0)
            return -1;
        sha256(buf, len, r->sha);
        if (erf_parse(buf, len, &r->archive, base->game, sub) != 0) {
            set_err(err, "%s/%s ('%s'): %s", r->slot, r->variant, r->relpath,
                    sub);
            return -1;
        }
        r->loaded = 1;
        if (check_variant(r, base, err) != 0)
            return -1;
    }
    /* All variants of a slot own an identical resource set. */
    for (i = 1; i < c->n; i++)
        if (!strcmp(c->rows[i].slot, c->rows[i - 1].slot) &&
            !same_set(&c->rows[i].archive, &c->rows[i - 1].archive)) {
            set_err(err, "slot '%s': variants '%s' and '%s' do not own the "
                         "same resources",
                    c->rows[i].slot, c->rows[i - 1].variant,
                    c->rows[i].variant);
            return -1;
        }
    /* Different slots must not share any resource. */
    for (i = 0; i < c->n; i++) {
        if (i && !strcmp(c->rows[i].slot, c->rows[i - 1].slot))
            continue; /* representative row only */
        for (j = i + 1; j < c->n; j++) {
            size_t a, b;
            if (!strcmp(c->rows[j].slot, c->rows[j - 1].slot))
                continue;
            for (a = 0; a < c->rows[i].archive.nres; a++)
                for (b = 0; b < c->rows[j].archive.nres; b++)
                    if (!strcmp(c->rows[i].archive.res[a].name,
                                c->rows[j].archive.res[b].name)) {
                        set_err(err,
                                "slots '%s' and '%s' both own area '%s'",
                                c->rows[i].slot, c->rows[j].slot,
                                c->rows[i].archive.res[a].name);
                        return -1;
                    }
        }
    }
    return 0;
}

void catalog_canonical_sha(const catalog *c, uint8_t out[32])
{
    sha256_ctx h;
    size_t i;
    sha256_init(&h);
    for (i = 0; i < c->n; i++) {
        const cat_row *r = &c->rows[i];
        char line[256], hex[65];
        hex_encode(r->sha, 32, hex);
        snprintf(line, sizeof line, "%s\t%s\t%u\t%u\t%u\t%u\t%s\n", r->slot,
                 r->variant, (unsigned)r->min_level, (unsigned)r->max_level,
                 (unsigned)r->min_party, (unsigned)r->max_party, hex);
        sha256_update(&h, line, strlen(line));
    }
    sha256_final(&h, out);
}

static int eligible(const cat_row *r, const config *cfg)
{
    return r->min_level <= cfg->min_level && cfg->max_level <= r->max_level &&
           r->min_party <= cfg->party_size && cfg->party_size <= r->max_party;
}

/* Independent stream per slot: SHA-256("nwndgen/1\0" || seed_be64 || slot),
 * first 8 bytes big-endian. Adding a slot never reshuffles the others. */
static uint64_t slot_stream_seed(uint64_t seed, const char *slot)
{
    sha256_ctx h;
    uint8_t sb[8], d[32];
    uint64_t v = 0;
    int i;
    for (i = 0; i < 8; i++)
        sb[i] = (uint8_t)(seed >> (56 - 8 * i));
    sha256_init(&h);
    sha256_update(&h, "nwndgen/1", 10); /* includes the NUL */
    sha256_update(&h, sb, 8);
    sha256_update(&h, slot, strlen(slot));
    sha256_final(&h, d);
    for (i = 0; i < 8; i++)
        v = v << 8 | d[i];
    return v;
}

int plan_select(const catalog *c, const config *cfg, plan *p, char *err)
{
    size_t i = 0, ns = 0;

    p->nslots = 0;
    p->chosen = xcalloc(c->n, sizeof *p->chosen);
    while (i < c->n) {
        size_t j = i, ne = 0, k;
        sm64 rng;
        uint64_t pick;
        while (j < c->n && !strcmp(c->rows[j].slot, c->rows[i].slot)) {
            if (eligible(&c->rows[j], cfg))
                ne++;
            j++;
        }
        if (ne == 0) {
            set_err(err,
                    "slot '%s' has no variant supporting party size %u and "
                    "levels %u-%u",
                    c->rows[i].slot, (unsigned)cfg->party_size,
                    (unsigned)cfg->min_level, (unsigned)cfg->max_level);
            plan_free(p);
            return -1;
        }
        sm64_seed(&rng, slot_stream_seed(cfg->seed, c->rows[i].slot));
        pick = sm64_bounded(&rng, ne);
        for (k = i; k < j; k++)
            if (eligible(&c->rows[k], cfg)) {
                if (pick == 0) {
                    p->chosen[ns++] = k;
                    break;
                }
                pick--;
            }
        i = j;
    }
    p->nslots = ns;
    return 0;
}

void plan_free(plan *p)
{
    free(p->chosen);
    p->chosen = NULL;
    p->nslots = 0;
}

int plan_apply(const erf *base, const catalog *c, const plan *p, erf *out,
               char *err)
{
    size_t s, i;

    erf_init(out, base->sig, base->game);
    out->desc_strref = base->desc_strref;
    out->loc_count = base->loc_count;
    out->loc = base->loc;
    out->loc_size = base->loc_size;
    for (i = 0; i < base->nres; i++)
        if (erf_add(out, base->res[i].name, base->res[i].type,
                    base->res[i].data, base->res[i].size, err) != 0)
            return -1;
    for (s = 0; s < p->nslots; s++) {
        const erf *v = &c->rows[p->chosen[s]].archive;
        for (i = 0; i < v->nres; i++) {
            erf_res *t = (erf_res *)erf_find(out, v->res[i].name, v->res[i].type);
            if (!t) {
                set_err(err, "internal: replacement target '%s' vanished",
                        v->res[i].name);
                return -1;
            }
            t->data = v->res[i].data;
            t->size = v->res[i].size;
        }
    }
    return 0;
}
