#include "gff.h"

#include <stdlib.h>
#include <string.h>

#include "util.h"

#define GFF_HDR 56
#define MAX_DEPTH 64

/* ---------- tree ---------- */

gff_struct *gff_struct_new(uint32_t id)
{
    gff_struct *s = xcalloc(1, sizeof *s);
    s->id = id;
    return s;
}

static void field_free(gff_field *f)
{
    size_t i;
    free(f->bytes);
    gff_struct_free(f->st);
    for (i = 0; i < f->nlist; i++)
        gff_struct_free(f->list[i]);
    free(f->list);
}

void gff_struct_free(gff_struct *s)
{
    size_t i;
    if (!s)
        return;
    for (i = 0; i < s->nf; i++)
        field_free(&s->f[i]);
    free(s->f);
    free(s);
}

void gff_free(gff *g)
{
    gff_struct_free(g->root);
    g->root = NULL;
}

static gff_field *add_field(gff_struct *s, const char *label, gff_type t)
{
    gff_field *f;
    if (s->nf == s->cap) {
        s->cap = s->cap ? s->cap * 2 : 8;
        s->f = xrealloc(s->f, s->cap * sizeof *s->f);
    }
    f = &s->f[s->nf++];
    memset(f, 0, sizeof *f);
    strncpy(f->label, label, 16);
    f->type = t;
    return f;
}

gff_field *gff_find(const gff_struct *s, const char *label)
{
    size_t i;
    for (i = 0; i < s->nf; i++)
        if (!strncmp(s->f[i].label, label, 16))
            return &s->f[i];
    return NULL;
}

gff_struct *gff_struct_clone(const gff_struct *s)
{
    gff_struct *c = gff_struct_new(s->id);
    size_t i, j;
    for (i = 0; i < s->nf; i++) {
        const gff_field *f = &s->f[i];
        gff_field *d = add_field(c, f->label, f->type);
        d->num = f->num;
        if (f->bytes) {
            d->bytes = xmalloc(f->blen + 1);
            memcpy(d->bytes, f->bytes, f->blen);
            d->bytes[f->blen] = '\0';
        }
        d->blen = f->blen;
        if (f->st)
            d->st = gff_struct_clone(f->st);
        for (j = 0; j < f->nlist; j++)
            gff_list_append(d, gff_struct_clone(f->list[j]));
    }
    return c;
}

static gff_field *need(gff_struct *s, const char *label, gff_type t, char *err)
{
    gff_field *f = gff_find(s, label);
    if (!f)
        return add_field(s, label, t);
    if (f->type != t) {
        set_err(err, "GFF field '%s' has type %d, expected %d", label, f->type, t);
        return NULL;
    }
    return f;
}

int gff_set_int(gff_struct *s, const char *label, gff_type t, int64_t v, char *err)
{
    gff_field *f = need(s, label, t, err);
    if (!f)
        return -1;
    f->num = (uint64_t)v;
    return 0;
}

int gff_set_float(gff_struct *s, const char *label, float v, char *err)
{
    gff_field *f = need(s, label, GFF_FLOAT, err);
    uint32_t b;
    if (!f)
        return -1;
    memcpy(&b, &v, 4);
    f->num = b;
    return 0;
}

static void set_bytes(gff_field *f, const void *d, size_t n)
{
    free(f->bytes);
    f->bytes = xmalloc(n + 1); /* always NUL-terminated so STRING/RESREF can be used as C strings */
    if (n)
        memcpy(f->bytes, d, n);
    f->bytes[n] = '\0';
    f->blen = n;
}

int gff_set_string(gff_struct *s, const char *label, gff_type t, const char *text, char *err)
{
    gff_field *f;
    if (t == GFF_RESREF && strlen(text) > 16) {
        set_err(err, "resref '%s' is longer than 16 characters", text);
        return -1;
    }
    f = need(s, label, t, err);
    if (!f)
        return -1;
    set_bytes(f, text, strlen(text));
    return 0;
}

int gff_set_locstr(gff_struct *s, const char *label, const char *text, char *err)
{
    gff_field *f = need(s, label, GFF_LOCSTR, err);
    size_t n = text ? strlen(text) : 0;
    uint8_t *b;
    if (!f)
        return -1;
    if (!text) { /* no entries at all, as stock objects store an unset string */
        uint8_t e[8];
        wr32le(e, 0xFFFFFFFFu);
        wr32le(e + 4, 0);
        set_bytes(f, e, 8);
        return 0;
    }
    b = xmalloc(16 + n);
    wr32le(b, 0xFFFFFFFFu); /* no strref */
    wr32le(b + 4, 1);       /* one entry */
    wr32le(b + 8, 0);       /* language 0 (English, male) */
    wr32le(b + 12, (uint32_t)n);
    memcpy(b + 16, text, n);
    set_bytes(f, b, 16 + n);
    free(b);
    return 0;
}

int gff_set_void(gff_struct *s, const char *label, const uint8_t *data, size_t n, char *err)
{
    gff_field *f = need(s, label, GFF_VOID, err);
    if (!f)
        return -1;
    set_bytes(f, data, n);
    return 0;
}

int gff_remove(gff_struct *s, const char *label)
{
    size_t i;
    for (i = 0; i < s->nf; i++)
        if (!strncmp(s->f[i].label, label, 16)) {
            field_free(&s->f[i]);
            memmove(&s->f[i], &s->f[i + 1], (s->nf - i - 1) * sizeof *s->f);
            s->nf--;
            return 1;
        }
    return 0;
}

gff_struct *gff_set_struct(gff_struct *s, const char *label, uint32_t id, char *err)
{
    gff_field *f = need(s, label, GFF_STRUCT, err);
    if (!f)
        return NULL;
    if (!f->st)
        f->st = gff_struct_new(id);
    return f->st;
}

gff_field *gff_set_list(gff_struct *s, const char *label, char *err)
{
    gff_field *f = need(s, label, GFF_LIST, err);
    size_t i;
    if (!f)
        return NULL;
    for (i = 0; i < f->nlist; i++)
        gff_struct_free(f->list[i]);
    f->nlist = 0;
    return f;
}

void gff_list_append(gff_field *l, gff_struct *child)
{
    if (l->nlist == l->caplist) {
        l->caplist = l->caplist ? l->caplist * 2 : 8;
        l->list = xrealloc(l->list, l->caplist * sizeof *l->list);
    }
    l->list[l->nlist++] = child;
}

const char *gff_get_string(const gff_struct *s, const char *label)
{
    gff_field *f = gff_find(s, label);
    return (f && (f->type == GFF_STRING || f->type == GFF_RESREF) && f->bytes) ? (const char *)f->bytes : NULL;
}

int64_t gff_get_int(const gff_struct *s, const char *label, int64_t dflt)
{
    gff_field *f = gff_find(s, label);
    return (f && f->type <= GFF_INT64 && f->type != GFF_FLOAT) ? (int64_t)f->num : dflt;
}

double gff_get_float(const gff_struct *s, const char *label, double dflt)
{
    gff_field *f = gff_find(s, label);
    float v;
    uint32_t b;
    if (!f || f->type != GFF_FLOAT)
        return dflt;
    b = (uint32_t)f->num;
    memcpy(&v, &b, 4);
    return v;
}

/* ---------- reader ---------- */

typedef struct {
    const uint8_t *b;
    size_t len;
    uint32_t so, sc, fo, fc, lo, lc, fdo, fds, fio, fis, lio, lis;
    unsigned visits;
    char *err;
} rd;

static int blob(const rd *r, uint64_t off, uint64_t n, uint32_t base, uint32_t size)
{
    return off <= size && n <= size - off && (uint64_t)base + size <= r->len;
}

static gff_struct *read_struct(rd *r, uint32_t idx, int depth);

static int read_field(rd *r, gff_struct *s, uint32_t fi, int depth)
{
    const uint8_t *fp;
    gff_type t;
    uint32_t li, data;
    gff_field *f;
    char label[17];
    uint64_t off;

    if (fi >= r->fc) {
        set_err(r->err, "GFF field index %u out of range", fi);
        return -1;
    }
    fp = r->b + r->fo + (size_t)fi * 12;
    t = (gff_type)rd32le(fp);
    li = rd32le(fp + 4);
    data = rd32le(fp + 8);
    if (rd32le(fp) > GFF_LIST || li >= r->lc) {
        set_err(r->err, "GFF field has a bad type or label index");
        return -1;
    }
    memset(label, 0, sizeof label);
    memcpy(label, r->b + r->lo + (size_t)li * 16, 16);
    f = add_field(s, label, t);
    off = data;
    switch (t) {
    case GFF_BYTE: case GFF_CHAR: f->num = t == GFF_CHAR ? (uint64_t)(int64_t)(int8_t)data : (data & 0xFF); break;
    case GFF_WORD: f->num = data & 0xFFFF; break;
    case GFF_SHORT: f->num = (uint64_t)(int64_t)(int16_t)data; break;
    case GFF_DWORD: case GFF_FLOAT: f->num = data; break;
    case GFF_INT: f->num = (uint64_t)(int64_t)(int32_t)data; break;
    case GFF_DWORD64: case GFF_INT64: case GFF_DOUBLE:
        if (!blob(r, off, 8, r->fdo, r->fds)) goto bad;
        f->num = (uint64_t)rd32le(r->b + r->fdo + off) | (uint64_t)rd32le(r->b + r->fdo + off + 4) << 32;
        break;
    case GFF_STRING: {
        uint32_t n;
        if (!blob(r, off, 4, r->fdo, r->fds)) goto bad;
        n = rd32le(r->b + r->fdo + off);
        if (!blob(r, off + 4, n, r->fdo, r->fds)) goto bad;
        set_bytes(f, r->b + r->fdo + off + 4, n);
        break;
    }
    case GFF_RESREF: {
        uint32_t n;
        if (!blob(r, off, 1, r->fdo, r->fds)) goto bad;
        n = r->b[r->fdo + off];
        if (!blob(r, off + 1, n, r->fdo, r->fds)) goto bad;
        set_bytes(f, r->b + r->fdo + off + 1, n);
        break;
    }
    case GFF_LOCSTR: case GFF_VOID: {
        uint32_t n;
        if (!blob(r, off, 4, r->fdo, r->fds)) goto bad;
        n = rd32le(r->b + r->fdo + off);
        if (!blob(r, off + 4, n, r->fdo, r->fds)) goto bad;
        set_bytes(f, r->b + r->fdo + off + 4, n);
        if (t == GFF_LOCSTR) { /* validate entry structure */
            size_t p = 8, e;
            uint32_t cnt;
            if (n < 8) goto bad;
            cnt = rd32le(f->bytes + 4);
            for (e = 0; e < cnt; e++) {
                uint32_t l;
                if (n - p < 8) goto bad;
                l = rd32le(f->bytes + p + 4);
                p += 8;
                if (l > n - p) goto bad;
                p += l;
            }
            if (p != n) goto bad;
        }
        break;
    }
    case GFF_STRUCT:
        f->st = read_struct(r, data, depth + 1);
        if (!f->st) return -1;
        break;
    case GFF_LIST: {
        uint32_t n, i;
        if (!blob(r, off, 4, r->lio, r->lis)) goto bad;
        n = rd32le(r->b + r->lio + off);
        if (!blob(r, off + 4, (uint64_t)n * 4, r->lio, r->lis)) goto bad;
        for (i = 0; i < n; i++) {
            gff_struct *c = read_struct(r, rd32le(r->b + r->lio + off + 4 + (size_t)i * 4), depth + 1);
            if (!c) return -1;
            gff_list_append(f, c);
        }
        break;
    }
    }
    return 0;
bad:
    set_err(r->err, "GFF field '%s' data is out of range or malformed", label);
    return -1;
}

static gff_struct *read_struct(rd *r, uint32_t idx, int depth)
{
    const uint8_t *sp;
    uint32_t nf, data, i;
    gff_struct *s;

    if (depth > MAX_DEPTH || ++r->visits > r->sc * 4u + 16u) {
        set_err(r->err, "GFF structure nesting is too deep or cyclic");
        return NULL;
    }
    if (idx >= r->sc) {
        set_err(r->err, "GFF struct index %u out of range", idx);
        return NULL;
    }
    sp = r->b + r->so + (size_t)idx * 12;
    s = gff_struct_new(rd32le(sp));
    data = rd32le(sp + 4);
    nf = rd32le(sp + 8);
    if (nf == 1) {
        if (read_field(r, s, data, depth) != 0) goto fail;
    } else if (nf > 1) {
        if (!blob(r, data, (uint64_t)nf * 4, r->fio, r->fis)) {
            set_err(r->err, "GFF field-index table is out of range");
            goto fail;
        }
        for (i = 0; i < nf; i++)
            if (read_field(r, s, rd32le(r->b + r->fio + data + (size_t)i * 4), depth) != 0)
                goto fail;
    }
    return s;
fail:
    gff_struct_free(s);
    return NULL;
}

int gff_parse(const uint8_t *buf, size_t len, gff *g, char *err)
{
    rd r;
    memset(g, 0, sizeof *g);
    if (len < GFF_HDR || len > GFF_MAX_BYTES) {
        set_err(err, "GFF data has a bad size (%zu)", len);
        return -1;
    }
    if (memcmp(buf + 4, "V3.2", 4)) {
        set_err(err, "unsupported GFF version (only V3.2)");
        return -1;
    }
    memset(&r, 0, sizeof r);
    r.b = buf; r.len = len; r.err = err;
    r.so = rd32le(buf + 8);  r.sc = rd32le(buf + 12);
    r.fo = rd32le(buf + 16); r.fc = rd32le(buf + 20);
    r.lo = rd32le(buf + 24); r.lc = rd32le(buf + 28);
    r.fdo = rd32le(buf + 32); r.fds = rd32le(buf + 36);
    r.fio = rd32le(buf + 40); r.fis = rd32le(buf + 44);
    r.lio = rd32le(buf + 48); r.lis = rd32le(buf + 52);
    if (!blob(&r, 0, (uint64_t)r.sc * 12, r.so, r.sc * 12) || (uint64_t)r.so + (uint64_t)r.sc * 12 > len ||
        (uint64_t)r.fo + (uint64_t)r.fc * 12 > len || (uint64_t)r.lo + (uint64_t)r.lc * 16 > len ||
        (uint64_t)r.fdo + r.fds > len || (uint64_t)r.fio + r.fis > len || (uint64_t)r.lio + r.lis > len ||
        r.sc < 1) {
        set_err(err, "GFF table is out of range");
        return -1;
    }
    memcpy(g->type, buf, 4);
    g->type[4] = '\0';
    g->root = read_struct(&r, 0, 0);
    return g->root ? 0 : -1;
}

/* ---------- writer ---------- */

typedef struct { uint8_t *b; size_t n, cap; } wbuf;

static void wput(wbuf *w, const void *d, size_t n)
{
    if (w->n + n > w->cap) {
        w->cap = (w->n + n) * 2 + 64;
        w->b = xrealloc(w->b, w->cap);
    }
    if (n)
        memcpy(w->b + w->n, d, n);
    w->n += n;
}

static void w32(wbuf *w, uint32_t v)
{
    uint8_t t[4];
    wr32le(t, v);
    wput(w, t, 4);
}

typedef struct {
    const gff_struct **order;  /* BFS order, index = struct id in file */
    size_t n, cap;
    char (*labels)[16];
    size_t nlab, caplab;
} wctx;

static void enqueue(wctx *c, const gff_struct *s)
{
    if (c->n == c->cap) {
        c->cap = c->cap ? c->cap * 2 : 16;
        c->order = xrealloc(c->order, c->cap * sizeof *c->order);
    }
    c->order[c->n++] = s;
}

static size_t child_index(const wctx *c, const gff_struct *s)
{
    size_t i;
    for (i = 0; i < c->n; i++)
        if (c->order[i] == s)
            return i;
    return (size_t)-1;
}

static uint32_t label_index(wctx *c, const char *l)
{
    size_t i;
    char key[16];
    memset(key, 0, 16);
    memcpy(key, l, strnlen(l, 16));
    for (i = 0; i < c->nlab; i++)
        if (!memcmp(c->labels[i], key, 16))
            return (uint32_t)i;
    if (c->nlab == c->caplab) {
        c->caplab = c->caplab ? c->caplab * 2 : 32;
        c->labels = xrealloc(c->labels, c->caplab * 16);
    }
    memcpy(c->labels[c->nlab], key, 16);
    return (uint32_t)c->nlab++;
}

int gff_write(const gff *g, uint8_t **out, size_t *outlen, char *err)
{
    wctx c;
    wbuf structs = {0}, fields = {0}, fdata = {0}, fidx = {0}, lidx = {0}, res = {0};
    size_t i, k, j, nfields = 0;
    uint8_t hdr[GFF_HDR];
    uint32_t so, fo, lo, fdo, fio, lio, total;

    memset(&c, 0, sizeof c);
    enqueue(&c, g->root);
    for (i = 0; i < c.n; i++) { /* BFS: children discovered while iterating */
        const gff_struct *s = c.order[i];
        for (k = 0; k < s->nf; k++) {
            if (s->f[k].type == GFF_STRUCT && s->f[k].st)
                enqueue(&c, s->f[k].st);
            else if (s->f[k].type == GFF_LIST)
                for (j = 0; j < s->f[k].nlist; j++)
                    enqueue(&c, s->f[k].list[j]);
        }
        if (c.n > 4000000) {
            set_err(err, "GFF tree is too large");
            goto fail;
        }
    }
    for (i = 0; i < c.n; i++) {
        const gff_struct *s = c.order[i];
        size_t first_field = nfields;
        for (k = 0; k < s->nf; k++) {
            const gff_field *f = &s->f[k];
            uint32_t data = 0;
            w32(&fields, (uint32_t)f->type);
            w32(&fields, label_index(&c, f->label));
            switch (f->type) {
            case GFF_BYTE: data = (uint32_t)(f->num & 0xFF); break;
            case GFF_CHAR: data = (uint32_t)(int32_t)(int8_t)f->num & 0xFFu; break;
            case GFF_WORD: data = (uint32_t)(f->num & 0xFFFF); break;
            case GFF_SHORT: data = (uint32_t)(int32_t)(int16_t)f->num & 0xFFFFu; break;
            case GFF_DWORD: case GFF_FLOAT: data = (uint32_t)f->num; break;
            case GFF_INT: data = (uint32_t)(int32_t)f->num; break;
            case GFF_DWORD64: case GFF_INT64: case GFF_DOUBLE:
                data = (uint32_t)fdata.n;
                w32(&fdata, (uint32_t)f->num);
                w32(&fdata, (uint32_t)(f->num >> 32));
                break;
            case GFF_STRING:
                data = (uint32_t)fdata.n;
                w32(&fdata, (uint32_t)f->blen);
                wput(&fdata, f->bytes, f->blen);
                break;
            case GFF_RESREF: {
                uint8_t n = (uint8_t)f->blen;
                data = (uint32_t)fdata.n;
                wput(&fdata, &n, 1);
                wput(&fdata, f->bytes, f->blen);
                break;
            }
            case GFF_LOCSTR: case GFF_VOID:
                data = (uint32_t)fdata.n;
                w32(&fdata, (uint32_t)f->blen);
                wput(&fdata, f->bytes, f->blen);
                break;
            case GFF_STRUCT:
                data = (uint32_t)child_index(&c, f->st);
                break;
            case GFF_LIST:
                data = (uint32_t)lidx.n;
                w32(&lidx, (uint32_t)f->nlist);
                for (j = 0; j < f->nlist; j++)
                    w32(&lidx, (uint32_t)child_index(&c, f->list[j]));
                break;
            }
            w32(&fields, data);
            nfields++;
        }
        w32(&structs, s->id);
        if (s->nf == 0)
            w32(&structs, 0xFFFFFFFFu);
        else if (s->nf == 1)
            w32(&structs, (uint32_t)first_field);
        else {
            w32(&structs, (uint32_t)fidx.n);
            for (k = 0; k < s->nf; k++)
                w32(&fidx, (uint32_t)(first_field + k));
        }
        w32(&structs, (uint32_t)s->nf);
    }
    so = GFF_HDR;
    fo = so + (uint32_t)structs.n;
    lo = fo + (uint32_t)fields.n;
    fdo = lo + (uint32_t)(c.nlab * 16);
    fio = fdo + (uint32_t)fdata.n;
    lio = fio + (uint32_t)fidx.n;
    total = lio + (uint32_t)lidx.n;
    if ((size_t)total > GFF_MAX_BYTES) {
        set_err(err, "GFF output too large");
        goto fail;
    }
    if (mem_reserve(total, err) != 0)
        goto fail;
    memset(hdr, 0, sizeof hdr);
    memcpy(hdr, g->type, 4);
    memcpy(hdr + 4, "V3.2", 4);
    wr32le(hdr + 8, so);  wr32le(hdr + 12, (uint32_t)c.n);
    wr32le(hdr + 16, fo); wr32le(hdr + 20, (uint32_t)nfields);
    wr32le(hdr + 24, lo); wr32le(hdr + 28, (uint32_t)c.nlab);
    wr32le(hdr + 32, fdo); wr32le(hdr + 36, (uint32_t)fdata.n);
    wr32le(hdr + 40, fio); wr32le(hdr + 44, (uint32_t)fidx.n);
    wr32le(hdr + 48, lio); wr32le(hdr + 52, (uint32_t)lidx.n);
    wput(&res, hdr, sizeof hdr);
    wput(&res, structs.b, structs.n);
    wput(&res, fields.b, fields.n);
    wput(&res, c.labels, c.nlab * 16);
    wput(&res, fdata.b, fdata.n);
    wput(&res, fidx.b, fidx.n);
    wput(&res, lidx.b, lidx.n);
    *out = res.b;
    *outlen = res.n;
    free(structs.b); free(fields.b); free(fdata.b); free(fidx.b); free(lidx.b);
    free(c.order); free(c.labels);
    return 0;
fail:
    free(structs.b); free(fields.b); free(fdata.b); free(fidx.b); free(lidx.b); free(res.b);
    free(c.order); free(c.labels);
    return -1;
}
