#include "erf.h"

#include <stdlib.h>
#include <string.h>

#include "util.h"

void erf_init(erf *e, const char *sig, const game_profile *g)
{
    memset(e, 0, sizeof *e);
    e->game = g;
    memcpy(e->sig, sig, 4);
    e->sig[4] = '\0';
}

void erf_free(erf *e)
{
    free(e->res);
    free(e->owned);
    memset(e, 0, sizeof *e);
}

/* Canonical (lowercased) resref: printable ASCII. Real toolset output contains
 * spaces and punctuation such as "!", "'" and "+" (seen in stock modules). */
int erf_name_valid(const char *s, unsigned max)
{
    size_t n = strlen(s), i;
    if (n == 0 || n > max)
        return 0;
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x20 || c > 0x7e || (c >= 'A' && c <= 'Z'))
            return 0;
    }
    return 1;
}

static int res_cmp(const void *a, const void *b)
{
    const erf_res *x = a, *y = b;
    int c = strcmp(x->name, y->name);
    if (c)
        return c;
    return (int)x->type - (int)y->type;
}

static int dup_check(const erf_res *r, size_t n, char *err)
{
    size_t i;
    for (i = 1; i < n; i++)
        if (!res_cmp(&r[i - 1], &r[i])) {
            set_err(err, "duplicate resource key '%s' (type %u)", r[i].name,
                    (unsigned)r[i].type);
            return -1;
        }
    return 0;
}

int erf_add(erf *e, const char *name, uint16_t type, const uint8_t *data,
            uint32_t size, char *err)
{
    char lc[ERF_NAME_BUF];
    size_t i, n = strlen(name);
    if (n > e->game->name_max) {
        set_err(err, "resource name '%.40s' is longer than %u characters",
                name, e->game->name_max);
        return -1;
    }
    for (i = 0; i < n; i++)
        lc[i] = (name[i] >= 'A' && name[i] <= 'Z') ? (char)(name[i] + 32)
                                                    : name[i];
    lc[n] = '\0';
    if (!erf_name_valid(lc, e->game->name_max)) {
        set_err(err, "invalid resource name '%s'", name);
        return -1;
    }
    if (e->nres == e->cap) {
        e->cap = e->cap ? e->cap * 2 : 16;
        e->res = xrealloc(e->res, e->cap * sizeof *e->res);
    }
    strcpy(e->res[e->nres].name, lc);
    e->res[e->nres].type = type;
    e->res[e->nres].data = data;
    e->res[e->nres].size = size;
    e->nres++;
    return 0;
}

const erf_res *erf_find(const erf *e, const char *name, uint16_t type)
{
    size_t lo = 0, hi = e->nres;
    erf_res key;
    memset(&key, 0, sizeof key);
    strncpy(key.name, name, ERF_NAME_BUF - 1);
    key.type = type;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int c = res_cmp(&e->res[mid], &key);
        if (c == 0)
            return &e->res[mid];
        if (c < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    return NULL;
}

static int in_table(uint64_t off, uint64_t bytes, size_t len)
{
    return off >= ERF_HEADER_SIZE && off <= len && bytes <= len - off;
}

int erf_parse(uint8_t *buf, size_t len, erf *e, const game_profile *g,
              char *err)
{
    uint32_t nloc, locsz, n, off_loc, off_key, off_res, i;
    uint8_t *seen = NULL;
    uint64_t pos;

    memset(e, 0, sizeof *e);
    e->owned = buf;
    e->game = g;

    if (len < ERF_HEADER_SIZE) {
        set_err(err, "truncated ERF header (%zu bytes, need %d)", len,
                ERF_HEADER_SIZE);
        goto fail;
    }
    if (memcmp(buf, "MOD ", 4) && memcmp(buf, "ERF ", 4) &&
        memcmp(buf, "HAK ", 4)) {
        set_err(err, "unsupported container signature (expected MOD/ERF/HAK)");
        goto fail;
    }
    memcpy(e->sig, buf, 4);
    e->sig[4] = '\0';
    if (memcmp(buf + 4, g->erf_version, 4)) {
        const game_profile *other = game_by_erf_version((const char *)buf + 4);
        if (other)
            set_err(err, "archive is ERF %s (%s format) but %s was selected "
                         "(needs ERF %s)",
                    other->erf_version, other->name, g->name, g->erf_version);
        else
            set_err(err, "unsupported ERF version (%s needs %s)", g->name,
                    g->erf_version);
        goto fail;
    }
    nloc = rd32le(buf + 8);
    locsz = rd32le(buf + 12);
    n = rd32le(buf + 16);
    off_loc = rd32le(buf + 20);
    off_key = rd32le(buf + 24);
    off_res = rd32le(buf + 28);
    e->desc_strref = rd32le(buf + 40);

    if (n > ERF_MAX_ENTRIES) {
        set_err(err, "entry count %u exceeds the limit of %u", (unsigned)n,
                ERF_MAX_ENTRIES);
        goto fail;
    }
    if (!in_table(off_loc, locsz, len)) {
        set_err(err, "localized string table is out of range");
        goto fail;
    }
    if (!in_table(off_key, (uint64_t)n * g->key_size, len)) {
        set_err(err, "key list is out of range");
        goto fail;
    }
    if (!in_table(off_res, (uint64_t)n * 8, len)) {
        set_err(err, "resource list is out of range");
        goto fail;
    }

    /* Localized strings: validate structure, keep verbatim. */
    pos = 0;
    for (i = 0; i < nloc; i++) {
        uint32_t sz;
        if (locsz - pos < 8) {
            set_err(err, "malformed localized string table (entry %u)",
                    (unsigned)i);
            goto fail;
        }
        sz = rd32le(buf + off_loc + pos + 4);
        pos += 8;
        if (sz > locsz - pos) {
            set_err(err, "localized string %u overruns its table",
                    (unsigned)i);
            goto fail;
        }
        pos += sz;
    }
    if (pos != locsz) {
        set_err(err, "localized string table has %u trailing bytes",
                (unsigned)(locsz - pos));
        goto fail;
    }
    e->loc_count = nloc;
    e->loc_size = locsz;
    e->loc = buf + off_loc;

    e->res = xcalloc(n ? n : 1, sizeof *e->res);
    e->cap = n ? n : 1;
    seen = xcalloc(n ? n : 1, 1);

    for (i = 0; i < n; i++) {
        const uint8_t *k = buf + off_key + (size_t)i * g->key_size;
        const uint8_t *rl;
        erf_res *r = &e->res[i];
        uint32_t resid, off, size;
        size_t nl = 0, j;

        while (nl < g->name_max && k[nl])
            nl++;
        if (nl == 0) {
            set_err(err, "key %u has an empty resource name", (unsigned)i);
            goto fail;
        }
        for (j = nl; j < g->name_max; j++)
            if (k[j]) {
                set_err(err, "key %u has bytes after the name terminator",
                        (unsigned)i);
                goto fail;
            }
        for (j = 0; j < nl; j++)
            r->name[j] = (k[j] >= 'A' && k[j] <= 'Z') ? (char)(k[j] + 32)
                                                      : (char)k[j];
        r->name[nl] = '\0';
        if (!erf_name_valid(r->name, g->name_max)) {
            set_err(err, "key %u has an invalid resource name", (unsigned)i);
            goto fail;
        }
        resid = rd32le(k + g->name_max);
        r->type = rd16le(k + g->name_max + 4);
        if (resid >= n || seen[resid]) {
            set_err(err, "key '%s' has an out-of-range or reused resource ID %u",
                    r->name, (unsigned)resid);
            goto fail;
        }
        seen[resid] = 1;
        rl = buf + off_res + (size_t)resid * 8;
        off = rd32le(rl);
        size = rd32le(rl + 4);
        if (size == 0) {
            if (off > len) {
                set_err(err, "resource '%s' offset is out of range", r->name);
                goto fail;
            }
        } else if (!in_table(off, size, len)) {
            set_err(err, "resource '%s' data is out of range", r->name);
            goto fail;
        }
        r->data = buf + off;
        r->size = size;
        e->nres++;
    }
    qsort(e->res, e->nres, sizeof *e->res, res_cmp);
    if (dup_check(e->res, e->nres, err) != 0)
        goto fail;
    free(seen);
    return 0;
fail:
    free(seen);
    erf_free(e);
    return -1;
}

int erf_write(const erf *e, uint8_t **out, size_t *outlen, char *err)
{
    erf_res *s;
    uint64_t total, off_key, off_res, off_data, dpos;
    uint8_t *b;
    size_t i;

    s = xmalloc((e->nres ? e->nres : 1) * sizeof *s);
    if (e->nres)
        memcpy(s, e->res, e->nres * sizeof *s);
    qsort(s, e->nres, sizeof *s, res_cmp);
    if (dup_check(s, e->nres, err) != 0)
        goto fail;

    total = ERF_HEADER_SIZE + (uint64_t)e->loc_size +
            (uint64_t)e->nres * (e->game->key_size + 8);
    for (i = 0; i < e->nres; i++) {
        if (!erf_name_valid(s[i].name, e->game->name_max)) {
            set_err(err, "invalid resource name '%s'", s[i].name);
            goto fail;
        }
        total += s[i].size;
        if (total > UINT32_MAX) {
            set_err(err, "archive exceeds the 4 GiB ERF offset limit");
            goto fail;
        }
    }
    if (total > MAX_FILE_BYTES) {
        set_err(err, "output of %llu bytes exceeds the %zu byte limit",
                (unsigned long long)total, (size_t)MAX_FILE_BYTES);
        goto fail;
    }
    if (mem_reserve((size_t)total, err) != 0)
        goto fail;

    b = xcalloc((size_t)total, 1);
    off_key = ERF_HEADER_SIZE + (uint64_t)e->loc_size;
    off_res = off_key + (uint64_t)e->nres * e->game->key_size;
    off_data = off_res + (uint64_t)e->nres * 8;

    memcpy(b, e->sig, 4);
    memcpy(b + 4, e->game->erf_version, 4);
    wr32le(b + 8, e->loc_count);
    wr32le(b + 12, e->loc_size);
    wr32le(b + 16, (uint32_t)e->nres);
    wr32le(b + 20, ERF_HEADER_SIZE);
    wr32le(b + 24, (uint32_t)off_key);
    wr32le(b + 28, (uint32_t)off_res);
    wr32le(b + 32, 0); /* build year: fixed for reproducibility */
    wr32le(b + 36, 0); /* build day: fixed for reproducibility */
    wr32le(b + 40, e->desc_strref);
    if (e->loc_size)
        memcpy(b + ERF_HEADER_SIZE, e->loc, e->loc_size);

    dpos = off_data;
    for (i = 0; i < e->nres; i++) {
        uint8_t *k = b + off_key + i * e->game->key_size;
        uint8_t *r = b + off_res + i * 8;
        memcpy(k, s[i].name, strlen(s[i].name));
        wr32le(k + e->game->name_max, (uint32_t)i);
        wr16le(k + e->game->name_max + 4, s[i].type);
        wr32le(r, (uint32_t)dpos);
        wr32le(r + 4, s[i].size);
        if (s[i].size)
            memcpy(b + dpos, s[i].data, s[i].size);
        dpos += s[i].size;
    }
    free(s);
    *out = b;
    *outlen = (size_t)total;
    return 0;
fail:
    free(s);
    return -1;
}
