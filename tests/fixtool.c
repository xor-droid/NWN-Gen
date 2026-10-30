/* fixtool - standalone test tool: format-test fixtures, independent verifier, selection model.
 *
 * Deliberately shares NO code with src/: it has its own little ERF/GFF writer and strict parser so that
 * it stays an independent check of nwndgen. Test tooling only (replaces the earlier python scripts).
 *
 *   fixtool fixture OUTDIR nwn1|nwn2 [plain]   write base.mod, rooms.tsv and variants/ (ERFs)
 *   fixtool verify OUT.mod BASE.mod ROOMS.tsv  print slot=variant lines, exit 1 on any violation
 *   fixtool corrupt ghost|evilifo|incomplete FILE   write a bad variant ERF (for failure tests)
 *   fixtool sel-preimage SEED SLOT             bytes hashed for the slot stream ("nwndgen/1\0" seed_be64 slot)
 *   fixtool sel-pick HEX16 N                   index chosen from SplitMix64 seeded with HEX16, bound N
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- byte buffer ---------- */
typedef struct { unsigned char *b; size_t n, cap; } bb;

static void *chk(void *p) { if (!p) { fputs("fixtool: out of memory\n", stderr); exit(1); } return p; }

static void put(bb *o, const void *d, size_t n)
{
    if (o->n + n > o->cap) { o->cap = (o->n + n) * 2 + 64; o->b = chk(realloc(o->b, o->cap)); }
    if (n) memcpy(o->b + o->n, d, n);
    o->n += n;
}
static void u32(bb *o, uint32_t v) { unsigned char c[4] = {(unsigned char)v, (unsigned char)(v >> 8), (unsigned char)(v >> 16), (unsigned char)(v >> 24)}; put(o, c, 4); }
static void u16(bb *o, unsigned v) { unsigned char c[2] = {(unsigned char)v, (unsigned char)(v >> 8)}; put(o, c, 2); }
static void zeros(bb *o, size_t n) { size_t i; for (i = 0; i < n; i++) put(o, "", 1); }
static void ljust(bb *o, const char *s, size_t w) { size_t n = strlen(s); put(o, s, n < w ? n : w); if (n < w) zeros(o, w - n); }

static uint32_t rd32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static unsigned rd16(const unsigned char *p) { return p[0] | p[1] << 8; }

#define T_NCS 2010
#define T_ARE 2012
#define T_IFO 2014
#define T_GIT 2023
#define T_GIC 2046
#define T_TRN 3002
#define T_TRX 3035

typedef struct { char name[40]; unsigned type; bb data; } ent;
typedef struct { ent *e; size_t n; } entv;

static int v11(void) { const char *g = getenv("NWNGEN_TEST_GAME"); return g && !strcmp(g, "nwn2"); }
static int NWN2;

static void add(entv *v, const char *name, unsigned type, const void *d, size_t n)
{
    v->e = chk(realloc(v->e, (v->n + 1) * sizeof *v->e));
    memset(&v->e[v->n], 0, sizeof *v->e);
    strcpy(v->e[v->n].name, name);
    v->e[v->n].type = type;
    put(&v->e[v->n].data, d, n);
    v->n++;
}

/* Minimal GFF V3.2: one root struct with one CExoString field 'Tag'. */
static void build_gff(bb *g, const char *kind, const char *text)
{
    bb fd = {0};
    size_t n = strlen(text);
    u32(&fd, (uint32_t)n);
    put(&fd, text, n);
    ljust(g, kind, 4);
    put(g, "V3.2", 4);
    /* struct, field, label, fielddata, fieldindices, listindices tables: offset,count/size */
    u32(g, 56); u32(g, 1);
    u32(g, 68); u32(g, 1);
    u32(g, 80); u32(g, 1);
    u32(g, 96); u32(g, (uint32_t)fd.n);
    u32(g, (uint32_t)(96 + fd.n)); u32(g, 0);
    u32(g, (uint32_t)(96 + fd.n)); u32(g, 0);
    u32(g, 0xFFFFFFFFu); u32(g, 0); u32(g, 1);      /* struct: type, first field, field count */
    u32(g, 10); u32(g, 0); u32(g, 0);               /* field: CExoString, label 0, data offset 0 */
    ljust(g, "Tag", 16);
    put(g, fd.b, fd.n);
    free(fd.b);
}

/* Opaque NWN2 terrain stand-in: 'NWN2' magic then padding. */
static void build_terrain(bb *g, const char *text)
{
    bb t = {0};
    put(&t, "\x02\x00\x03\x00", 4);
    put(&t, text, strlen(text));
    put(g, "NWN2", 4);
    put(g, t.b, t.n);
    if (t.n < 28) zeros(g, 28 - t.n);
    free(t.b);
}

static void area_triplet(entv *v, const char *area, const char *tag)
{
    bb g;
    static const struct { const char *k; unsigned t; } P[3] = {{"ARE ", T_ARE}, {"GIT ", T_GIT}, {"GIC ", T_GIC}};
    int i;
    for (i = 0; i < 3; i++) {
        memset(&g, 0, sizeof g);
        build_gff(&g, P[i].k, tag);
        add(v, area, P[i].t, g.b, g.n);
        free(g.b);
    }
    if (NWN2) {
        char s[96];
        memset(&g, 0, sizeof g);
        snprintf(s, sizeof s, "trn %s", tag); build_terrain(&g, s); add(v, area, T_TRN, g.b, g.n); free(g.b);
        memset(&g, 0, sizeof g);
        snprintf(s, sizeof s, "trx %s", tag); build_terrain(&g, s); add(v, area, T_TRX, g.b, g.n); free(g.b);
    }
}

/* loc: NULL for none, else one English entry. With shuffle, keys are written in reverse order and resource IDs are a
 * non-identity permutation, so a reader that confuses key position with resource ID is caught. */
static void build_erf(bb *o, const char *sig, const entv *v, int shuffle, const char *loc)
{
    size_t namew = v11() ? 32 : 16, keyw = v11() ? 40 : 24, n = v->n, i;
    size_t locsz = loc ? 8 + strlen(loc) : 0, off_loc = 160, off_key = off_loc + locsz;
    size_t off_res = off_key + keyw * n, off_data = off_res + 8 * n, pos = off_data;
    size_t *order = chk(calloc(n ? n : 1, sizeof *order)), *offs = chk(calloc(n ? n : 1, sizeof *offs)),
           *sizes = chk(calloc(n ? n : 1, sizeof *sizes)), *kidx = chk(calloc(n ? n : 1, sizeof *kidx));
    bb data = {0};
    for (i = 0; i < n; i++) {
        kidx[i] = shuffle ? n - 1 - i : i;                 /* key position -> entry */
        order[i] = shuffle ? (i + 1) % n : i;              /* key i -> resource slot */
    }
    for (i = 0; i < n; i++) {
        const ent *e = &v->e[kidx[i]];
        offs[order[i]] = pos;
        sizes[order[i]] = e->data.n;
        put(&data, e->data.b, e->data.n);
        pos += e->data.n;
    }
    put(o, sig, 4);
    put(o, v11() ? "V1.1" : "V1.0", 4);
    u32(o, loc ? 1 : 0); u32(o, (uint32_t)locsz); u32(o, (uint32_t)n); u32(o, (uint32_t)off_loc);
    u32(o, (uint32_t)off_key); u32(o, (uint32_t)off_res); u32(o, 99); u32(o, 123);
    u32(o, 0xFFFFFFFFu);
    zeros(o, 116);
    if (loc) { u32(o, 0); u32(o, (uint32_t)strlen(loc)); put(o, loc, strlen(loc)); }
    for (i = 0; i < n; i++) {
        const ent *e = &v->e[kidx[i]];
        ljust(o, e->name, namew);
        u32(o, (uint32_t)order[i]); u16(o, e->type); u16(o, 0);
    }
    for (i = 0; i < n; i++) { u32(o, (uint32_t)offs[i]); u32(o, (uint32_t)sizes[i]); }
    put(o, data.b, data.n);
    free(order); free(offs); free(sizes); free(kidx); free(data.b);
}

static void entv_free(entv *v)
{
    size_t i;
    for (i = 0; i < v->n; i++) free(v->e[i].data.b);
    free(v->e);
    memset(v, 0, sizeof *v);
}

static int write_file(const char *path, const bb *o)
{
    FILE *f = fopen(path, "wb");
    if (!f || fwrite(o->b, 1, o->n, f) != o->n || fclose(f)) { fprintf(stderr, "fixtool: cannot write %s\n", path); return -1; }
    return 0;
}

/* ---------- fixture ---------- */
static const struct { const char *slot, *area, *var[2]; } SLOTS[3] = {
    {"encounter_a", "enc_a", {"rats", "goblins"}},
    {"encounter_b", "enc_b", {"beetles", "undead"}},
    {"objective", "obj_room", {"rescue", "supplies"}},
};

static int cmd_fixture(const char *out, const char *game, int plain)
{
    int shuf = !plain, s, k;
    char path[1024], tag[96], rows[4096] = "";
    bb o;
    entv base = {0};
    NWN2 = !strcmp(game, "nwn2");
    setenv("NWNGEN_TEST_GAME", game, 1);
    snprintf(path, sizeof path, "%s/variants", out);
    {
        char cmd[1100];
        snprintf(cmd, sizeof cmd, "mkdir -p '%s'", path);
        if (system(cmd)) return 1;
    }
    {
        bb g = {0};
        build_gff(&g, "IFO ", "fixture-module");
        add(&base, "module", T_IFO, g.b, g.n);
        free(g.b);
        add(&base, "adv_main", T_NCS, "FIXTURE-NOT-REAL-NCS", 20);
    }
    area_triplet(&base, "cellar_entry", "entry");
    for (s = 0; s < 3; s++) {
        const char *area = SLOTS[s].area;
        if (NWN2 && s == 0) area = "enc_a_cellar_room_with_long_nm";
        snprintf(tag, sizeof tag, "%s-base", SLOTS[s].slot);
        area_triplet(&base, area, tag);
        for (k = 0; k < 2; k++) {
            entv v = {0};
            char row[256];
            snprintf(tag, sizeof tag, "%s-%s", SLOTS[s].slot, SLOTS[s].var[k]);
            area_triplet(&v, area, tag);
            memset(&o, 0, sizeof o);
            build_erf(&o, "ERF ", &v, shuf, NULL);
            snprintf(path, sizeof path, "%s/variants/%s_%s.erf", out, SLOTS[s].slot, SLOTS[s].var[k]);
            if (write_file(path, &o)) return 1;
            free(o.b);
            entv_free(&v);
            snprintf(row, sizeof row, "%s\t%s\t1\t2\t1\t1\tvariants/%s_%s.erf\n", SLOTS[s].slot, SLOTS[s].var[k], SLOTS[s].slot, SLOTS[s].var[k]);
            strcat(rows, row);
        }
    }
    {   /* one variant for a different band, to exercise eligibility; identity IDs, sorted as written */
        entv v = {0};
        area_triplet(&v, SLOTS[2].area, "objective-veteran");
        memset(&o, 0, sizeof o);
        build_erf(&o, "ERF ", &v, 0, NULL);
        snprintf(path, sizeof path, "%s/variants/objective_veteran.erf", out);
        if (write_file(path, &o)) return 1;
        free(o.b);
        entv_free(&v);
        strcat(rows, "objective\tveteran\t5\t8\t1\t4\tvariants/objective_veteran.erf\n");
    }
    memset(&o, 0, sizeof o);
    build_erf(&o, "MOD ", &base, shuf, "fixture");
    snprintf(path, sizeof path, "%s/base.mod", out);
    if (write_file(path, &o)) return 1;
    free(o.b);
    entv_free(&base);
    {
        bb t = {0};
        const char *hdr = "# FORMAT-TEST FIXTURE - not a playable adventure\n";
        put(&t, hdr, strlen(hdr));
        put(&t, rows, strlen(rows));
        snprintf(path, sizeof path, "%s/rooms.tsv", out);
        if (write_file(path, &t)) return 1;
        free(t.b);
    }
    return 0;
}

/* ---------- strict independent parser ---------- */
typedef struct {
    char sig[5], ver[5];
    unsigned n, off_loc, locsz, off_key, off_res, year, day, desc;
    const unsigned char *loc;
    ent *e;                       /* e[i].data aliases the file buffer: never freed */
    unsigned *resid, *unused;
} erfp;

static unsigned char *slurp(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    unsigned char *b;
    long sz;
    if (!f) { fprintf(stderr, "verify: cannot read %s\n", path); exit(1); }
    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
    b = chk(malloc((size_t)sz + 1));
    *n = fread(b, 1, (size_t)sz, f);
    b[*n] = 0;
    fclose(f);
    return b;
}

#define REQ(c, m) do { if (!(c)) { fprintf(stderr, "verify: %s\n", m); exit(1); } } while (0)

static void parse_erf(erfp *p, unsigned char *buf, size_t len)
{
    unsigned namew, keyw, i;
    memset(p, 0, sizeof *p);
    REQ(len >= 160, "short header");
    memcpy(p->sig, buf, 4); memcpy(p->ver, buf + 4, 4);
    REQ(!strcmp(p->ver, "V1.0") || !strcmp(p->ver, "V1.1"), "bad version");
    namew = !strcmp(p->ver, "V1.1") ? 32 : 16;
    keyw = namew + 8;
    p->locsz = rd32(buf + 12); p->n = rd32(buf + 16); p->off_loc = rd32(buf + 20);
    p->off_key = rd32(buf + 24); p->off_res = rd32(buf + 28); p->year = rd32(buf + 32); p->day = rd32(buf + 36);
    p->desc = rd32(buf + 40);
    p->loc = buf + p->off_loc;
    REQ((size_t)p->off_loc + p->locsz <= len, "loc block out of range");
    REQ((size_t)p->off_key + (size_t)keyw * p->n <= len && (size_t)p->off_res + 8 * (size_t)p->n <= len, "tables out of range");
    p->e = chk(calloc(p->n ? p->n : 1, sizeof *p->e));
    p->resid = chk(calloc(p->n ? p->n : 1, sizeof *p->resid));
    p->unused = chk(calloc(p->n ? p->n : 1, sizeof *p->unused));
    for (i = 0; i < p->n; i++) {
        const unsigned char *k = buf + p->off_key + (size_t)keyw * i;
        size_t nl = 0, j;
        unsigned id, off, size;
        while (nl < namew && k[nl]) nl++;
        for (j = nl; j < namew; j++) REQ(!k[j], "garbage after name");
        memcpy(p->e[i].name, k, nl);
        id = rd32(k + namew);
        p->e[i].type = rd16(k + namew + 4);
        p->unused[i] = rd16(k + namew + 6);
        p->resid[i] = id;
        REQ(id < p->n, "resource id out of range");
        off = rd32(buf + p->off_res + 8 * (size_t)id);
        size = rd32(buf + p->off_res + 8 * (size_t)id + 4);
        REQ((size_t)off + size <= len, "resource out of range");
        p->e[i].data.b = buf + off;
        p->e[i].data.n = size;
    }
}

static void erfp_free(erfp *p) { free(p->e); free(p->resid); free(p->unused); }

static int find(const erfp *p, const char *name, unsigned type)
{
    unsigned i;
    for (i = 0; i < p->n; i++)
        if (p->e[i].type == type && !strcmp(p->e[i].name, name)) return (int)i;
    return -1;
}

static int same(const ent *a, const ent *b) { return a->data.n == b->data.n && !memcmp(a->data.b, b->data.b, a->data.n); }

static int keycmp(const ent *x, const ent *y)
{
    int c = strcmp(x->name, y->name);
    return c ? c : (int)x->type - (int)y->type;
}

typedef struct { char slot[40], var[40], path[512]; } row;

static size_t read_catalog(const char *catp, row *rows, size_t max)
{
    size_t n = 0, len, pos = 0;
    unsigned char *c = slurp(catp, &len);
    char dir[512], *sl;
    snprintf(dir, sizeof dir, "%s", catp);
    sl = strrchr(dir, '/');
    if (sl) *sl = 0; else strcpy(dir, ".");
    while (pos < len) {
        char line[1024], *f[8];
        size_t L = 0;
        int nf = 0;
        char *t, *save;
        while (pos < len && c[pos] != '\n' && L + 1 < sizeof line) line[L++] = (char)c[pos++];
        while (pos < len && c[pos] != '\n') pos++;
        pos++;
        if (L && line[L - 1] == '\r') L--;
        line[L] = 0;
        if (!*line || *line == '#') continue;
        for (t = strtok_r(line, "\t", &save); t && nf < 8; t = strtok_r(NULL, "\t", &save)) f[nf++] = t;
        REQ(nf == 7 && n < max, "catalog row");
        snprintf(rows[n].slot, 40, "%.39s", f[0]);
        snprintf(rows[n].var, 40, "%.39s", f[1]);
        snprintf(rows[n].path, 512, "%.255s/%.254s", dir, f[6]);
        n++;
    }
    free(c);
    return n;
}

static int cmd_verify(const char *outp, const char *basep, const char *catp)
{
    size_t ol, bl, i, k, nrows, nslot = 0;
    unsigned char *ob = slurp(outp, &ol), *bbuf = slurp(basep, &bl), *owned;
    erfp out, base;
    row rows[32];
    char slots[16][40];

    parse_erf(&out, ob, ol);
    parse_erf(&base, bbuf, bl);
    REQ(!strcmp(out.sig, "MOD "), "output is not a MOD");
    REQ(!strcmp(out.ver, base.ver), "ERF version changed");
    REQ(out.year == 0 && out.day == 0, "non-fixed build metadata");
    for (i = 48; i < 160; i++) REQ(ob[i] == 0, "reserved bytes not zero");
    REQ(out.locsz == base.locsz && !memcmp(out.loc, base.loc, base.locsz) && out.desc == base.desc, "loc/desc changed");
    for (i = 1; i < out.n; i++) REQ(keycmp(&out.e[i - 1], &out.e[i]) < 0, "keys not in canonical order");
    for (i = 0; i < out.n; i++) {
        REQ(out.resid[i] == i, "resource IDs not sequential");
        REQ(out.unused[i] == 0, "key padding not zero");
    }
    REQ(out.off_loc == 160, "loc offset");
    REQ(out.n == base.n, "resource set changed");
    for (i = 0; i < base.n; i++) REQ(find(&out, base.e[i].name, base.e[i].type) >= 0, "resource set changed");

    nrows = read_catalog(catp, rows, 32);
    for (i = 0; i < nrows; i++) {
        for (k = 0; k < nslot && strcmp(slots[k], rows[i].slot); k++) ;
        if (k == nslot) snprintf(slots[nslot++], 40, "%.39s", rows[i].slot);
    }
    for (i = 0; i < nslot; i++)                      /* slots in sorted order */
        for (k = i + 1; k < nslot; k++)
            if (strcmp(slots[k], slots[i]) < 0) { char t[40]; strcpy(t, slots[i]); strcpy(slots[i], slots[k]); strcpy(slots[k], t); }

    owned = chk(calloc(out.n ? out.n : 1, 1));
    for (i = 0; i < nslot; i++) {
        int matches = 0;
        char chosen[40] = "";
        size_t r;
        for (r = 0; r < nrows; r++) {
            size_t vl, u;
            unsigned char *vb;
            erfp v;
            int all = 1;
            if (strcmp(rows[r].slot, slots[i])) continue;
            vb = slurp(rows[r].path, &vl);
            parse_erf(&v, vb, vl);
            for (u = 0; u < v.n && all; u++) {
                int o = find(&out, v.e[u].name, v.e[u].type), b = find(&base, v.e[u].name, v.e[u].type);
                /* a variant identical to the base bytes could not be told apart; fixture variants all differ */
                if (o < 0 || !same(&out.e[o], &v.e[u]) || b < 0 || same(&out.e[o], &base.e[b])) all = 0;
            }
            if (all) {
                matches++;
                snprintf(chosen, 40, "%.39s", rows[r].var);
                for (u = 0; u < v.n; u++) {
                    int oi = find(&out, v.e[u].name, v.e[u].type);
                    if (oi >= 0) owned[oi] = 1;
                }
            }
            erfp_free(&v);
            free(vb);
        }
        if (matches != 1) { fprintf(stderr, "verify: slot %s: %d variants match output\n", slots[i], matches); return 1; }
        printf("%s=%s\n", slots[i], chosen);
    }
    for (i = 0; i < out.n; i++) {
        int b;
        if (owned[i]) continue;
        b = find(&base, out.e[i].name, out.e[i].type);
        REQ(b >= 0 && same(&base.e[b], &out.e[i]), "untouched resource changed");
    }
    free(owned); erfp_free(&out); erfp_free(&base); free(ob); free(bbuf);
    return 0;
}

/* ---------- corrupt variants ---------- */
static int cmd_corrupt(const char *kind, const char *file)
{
    entv v = {0};
    bb o = {0}, g = {0};
    int rc;
    if (!strcmp(kind, "ghost")) {                    /* an area the base does not have */
        static const struct { const char *k; unsigned t; } P[3] = {{"ARE ", T_ARE}, {"GIT ", T_GIT}, {"GIC ", T_GIC}};
        int i;
        for (i = 0; i < 3; i++) { bb x = {0}; build_gff(&x, P[i].k, "g"); add(&v, "ghost", P[i].t, x.b, x.n); free(x.b); }
    } else if (!strcmp(kind, "evilifo")) {           /* tries to replace the module script/IFO */
        build_gff(&g, "IFO ", "evil");
        add(&v, "module", T_IFO, g.b, g.n);
    } else if (!strcmp(kind, "incomplete")) {        /* ARE + GIT without GIC */
        bb x = {0};
        build_gff(&x, "ARE ", "g"); add(&v, "obj_room", T_ARE, x.b, x.n); free(x.b);
        memset(&x, 0, sizeof x);
        build_gff(&x, "GIT ", "g"); add(&v, "obj_room", T_GIT, x.b, x.n); free(x.b);
    } else {
        fprintf(stderr, "fixtool: unknown corrupt kind %s\n", kind);
        return 2;
    }
    NWN2 = v11();
    build_erf(&o, "ERF ", &v, 0, NULL);
    rc = write_file(file, &o);
    free(o.b); free(g.b); entv_free(&v);
    return rc ? 1 : 0;
}

/* ---------- selection model (independent of src/; SHA-256 comes from sha256sum in the test) ---------- */
static int cmd_preimage(const char *seed, const char *slot)
{
    unsigned long long s = strtoull(seed, NULL, 10);
    int i;
    fwrite("nwndgen/1", 1, 9, stdout);
    putchar(0);
    for (i = 7; i >= 0; i--) putchar((int)((s >> (8 * i)) & 0xFF));
    fwrite(slot, 1, strlen(slot), stdout);
    return 0;
}

static int cmd_pick(const char *hex16, unsigned long long n)
{
    uint64_t s = strtoull(hex16, NULL, 16), thr = (0 - n) % n;
    for (;;) {
        uint64_t z;
        s += UINT64_C(0x9E3779B97F4A7C15);
        z = s;
        z = (z ^ (z >> 30)) * UINT64_C(0xBF58476D1CE4E5B9);
        z = (z ^ (z >> 27)) * UINT64_C(0x94D049BB133111EB);
        z ^= z >> 31;
        if (z >= thr) { printf("%llu\n", (unsigned long long)(z % n)); return 0; }
    }
}

int main(int argc, char **argv)
{
    if (argc >= 4 && !strcmp(argv[1], "fixture") && argc <= 5)
        return cmd_fixture(argv[2], argv[3], argc == 5 && !strcmp(argv[4], "plain"));
    if (argc == 5 && !strcmp(argv[1], "verify")) return cmd_verify(argv[2], argv[3], argv[4]);
    if (argc == 4 && !strcmp(argv[1], "corrupt")) return cmd_corrupt(argv[2], argv[3]);
    if (argc == 4 && !strcmp(argv[1], "sel-preimage")) return cmd_preimage(argv[2], argv[3]);
    if (argc == 4 && !strcmp(argv[1], "sel-pick")) return cmd_pick(argv[2], strtoull(argv[3], NULL, 10));
    fputs("usage: fixtool fixture|verify|corrupt|sel-preimage|sel-pick ...\n", stderr);
    return 2;
}
