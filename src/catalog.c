#include "catalog.h"

#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "util.h"

int catalog_id_valid(const char *s)
{
    size_t n = strlen(s), i;
    if (n == 0 || n > CAT_ID_MAX)
        return 0;
    for (i = 0; i < n; i++)
        if (!((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= '0' && s[i] <= '9') ||
              s[i] == '_'))
            return 0;
    return 1;
}

static int row_cmp(const void *a, const void *b)
{
    const cat_row *x = a, *y = b;
    int c = strcmp(x->slot, y->slot);
    return c ? c : strcmp(x->variant, y->variant);
}

void catalog_free(catalog *c)
{
    size_t i;
    for (i = 0; i < c->n; i++) {
        free(c->rows[i].relpath);
        free(c->rows[i].path);
        erf_free(&c->rows[i].archive);
    }
    free(c->rows);
    c->rows = NULL;
    c->n = 0;
}

static int parse_row(char *line, size_t lineno, cat_row *r, char *err)
{
    char *f[7];
    int nf = 0;
    char *p = line;
    uint32_t v[4];
    int i;

    f[nf++] = p;
    for (; *p; p++) {
        if (*p == '\t') {
            *p = '\0';
            if (nf == 7) {
                set_err(err, "catalog line %zu: more than 7 fields", lineno);
                return -1;
            }
            f[nf++] = p + 1;
        }
    }
    if (nf != 7) {
        set_err(err, "catalog line %zu: expected 7 tab-separated fields, got %d",
                lineno, nf);
        return -1;
    }
    if (!catalog_id_valid(f[0]) || !catalog_id_valid(f[1])) {
        set_err(err,
                "catalog line %zu: slot and variant IDs must be 1-%d chars of "
                "[a-z0-9_]",
                lineno, CAT_ID_MAX);
        return -1;
    }
    for (i = 0; i < 4; i++) {
        uint32_t hi = i < 2 ? MAX_LEVEL_LIMIT : MAX_PARTY_LIMIT;
        if (parse_uint_in(f[2 + i], 1, hi, &v[i]) != 0) {
            set_err(err, "catalog line %zu: field %d must be a number in 1..%u",
                    lineno, 3 + i, (unsigned)hi);
            return -1;
        }
    }
    if (v[0] > v[1] || v[2] > v[3]) {
        set_err(err, "catalog line %zu: minimum exceeds maximum", lineno);
        return -1;
    }
    if (!*f[6] || f[6][0] == '/') {
        set_err(err, "catalog line %zu: path must be non-empty and relative",
                lineno);
        return -1;
    }
    strcpy(r->slot, f[0]);
    strcpy(r->variant, f[1]);
    r->min_level = v[0];
    r->max_level = v[1];
    r->min_party = v[2];
    r->max_party = v[3];
    r->relpath = xstrdup(f[6]);
    return 0;
}

int catalog_parse(const char *text, size_t len, catalog *c, char *err)
{
    size_t pos = 0, lineno = 0, cap = 0, i;
    char line[CAT_MAX_LINE + 1];

    c->rows = NULL;
    c->n = 0;
    if (memchr(text, '\0', len)) {
        set_err(err, "catalog contains a NUL byte");
        return -1;
    }
    while (pos < len) {
        size_t end = pos, ll;
        while (end < len && text[end] != '\n')
            end++;
        ll = end - pos;
        lineno++;
        if (ll > CAT_MAX_LINE) {
            set_err(err, "catalog line %zu is longer than %d bytes", lineno,
                    CAT_MAX_LINE);
            goto fail;
        }
        memcpy(line, text + pos, ll);
        if (ll && line[ll - 1] == '\r')
            ll--;
        line[ll] = '\0';
        pos = end < len ? end + 1 : end;
        if (ll == 0 || line[0] == '#')
            continue;
        if (c->n == CAT_MAX_ROWS) {
            set_err(err, "catalog has more than %d rows", CAT_MAX_ROWS);
            goto fail;
        }
        if (c->n == cap) {
            cap = cap ? cap * 2 : 16;
            c->rows = xrealloc(c->rows, cap * sizeof *c->rows);
        }
        memset(&c->rows[c->n], 0, sizeof c->rows[c->n]);
        if (parse_row(line, lineno, &c->rows[c->n], err) != 0)
            goto fail;
        c->n++;
    }
    if (c->n == 0) {
        set_err(err, "catalog has no records");
        goto fail;
    }
    qsort(c->rows, c->n, sizeof *c->rows, row_cmp);
    for (i = 1; i < c->n; i++)
        if (!row_cmp(&c->rows[i - 1], &c->rows[i])) {
            set_err(err, "duplicate catalog record %s/%s", c->rows[i].slot,
                    c->rows[i].variant);
            goto fail;
        }
    return 0;
fail:
    catalog_free(c);
    return -1;
}
