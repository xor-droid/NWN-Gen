/* mkdgfix DIR : write the synthetic dungeon fixtures (tpl_are.are, tpl_git.git, base.mod, base.ifo)
 * and tilepack.txt, the synthetic tile pack (the same bytes as tests/data/tilepack_test.txt). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dgfix.h"
#include "erf.h"
#include "game.h"

static int put(const char *dir, const char *name, const uint8_t *d, size_t n)
{
    char path[1024];
    FILE *f;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    f = fopen(path, "wb");
    if (!f || fwrite(d, 1, n, f) != n || fclose(f)) {
        fprintf(stderr, "mkdgfix: cannot write %s\n", path);
        return -1;
    }
    return 0;
}

/* Fake tile ids; only pattern coverage, weights and format matter. 16 corner patterns in
 * W/F order (W first), 1-3 tiles each, so weighted selection and orientation are exercised. */
static char *tilepack_text(void)
{
    size_t cap = 8192, len = 0;
    char *t = malloc(cap), light[4096];
    size_t ll = 0;
    int i, j, tid = 100;
    len += (size_t)snprintf(t, cap, "nwndgen-tilepack 1\ntileset tsttest\n");
    for (i = 0; i < 16; i++) {
        int n = 1 + (i * 7) % 3;
        for (j = 0; j < n; j++) {
            tid++;
            len += (size_t)snprintf(t + len, cap - len, "cell %c%c%c%c %d %d %d\n", i & 8 ? 'F' : 'W', i & 4 ? 'F' : 'W',
                                    i & 2 ? 'F' : 'W', i & 1 ? 'F' : 'W', 1 + (i + j * 3) % 5, tid, (i + j) % 4);
            ll += (size_t)snprintf(light + ll, sizeof light - ll, "light %d %d %d %d %d %d %d %d\n", tid, tid % 50, 7,
                                   tid % 9, 3, 0, 1, 2);
        }
    }
    memcpy(t + len, light, ll + 1);
    return t;
}

/* A tileset (.set) consistent with the synthetic pack: tile `tid` placed at orientation `o` presents exactly the
 * corners named by its pattern. Raw corners are the inverse rotation: eff[ccw^o(p)] = raw[p]. With `bad` set, the
 * only tile for the all-wall pattern gets a floor corner, which must show up as adjacency violations. */
static size_t kit_set_tiles(char *t, size_t cap, size_t len);
static char *set_text(int bad, int kit)
{
    static const int CCW[4] = {0, 2, 3, 1};               /* NW SW SE NE as indexes into NW NE SW SE */
    static const char *const KEY[4] = {"TopLeft", "TopRight", "BottomLeft", "BottomRight"};
    size_t cap = 65536, len = 0;
    char *t = malloc(cap);
    int i, j, tid = 100, k;
    len += (size_t)snprintf(t, cap, "[GENERAL]\nName=synthetic\n[TILES]\nCount=%d\n", kit ? 300 : 100 + 16 * 3 + 1);
    for (k = 0; k <= 100; k++)
        len += (size_t)snprintf(t + len, cap - len, "[TILE%d]\nModel=filler\nTopLeft=Wall\nTopRight=Wall\nBottomLeft=Wall\nBottomRight=Wall\nDoors=0\n", k);
    for (i = 0; i < 16; i++) {
        int n = 1 + (i * 7) % 3;
        for (j = 0; j < n; j++) {
            int o = (i + j) % 4, eff[4], raw[4], p;
            tid++;
            for (p = 0; p < 4; p++) eff[p] = (i >> (3 - p)) & 1;      /* 1 = floor, in NW NE SW SE order */
            for (p = 0; p < 4; p++) {
                int pos = 0;
                while (CCW[pos] != p) pos++;
                raw[p] = eff[CCW[(pos + o) % 4]];
            }
            if (bad && tid == 101) raw[0] = 1;
            len += (size_t)snprintf(t + len, cap - len, "[TILE%d]\nModel=t%d\n", tid, tid);
            for (p = 0; p < 4; p++) len += (size_t)snprintf(t + len, cap - len, "%s=%s\n", KEY[p], raw[p] ? "Floor" : "Wall");
            len += (size_t)snprintf(t + len, cap - len, "Doors=0\n");
        }
    }
    if (kit) len = kit_set_tiles(t, cap, len);
    return t;
}


/* ---- corridor-kit fixtures (opt-in; the files above stay byte-identical) ----
 * Every kit record is its own tile whose raw corners/edges are the inverse rotation of the pattern it presents at `orient`, so the
 * generator's pack and dgcheck's reading of the .set must agree through the same rotation convention as for the cell tiles. */
typedef struct { const char *sig, *edges; int tid, orient, door; } kitrec;
/* tiles 215 and 216 carry the second door look (type 23), the rest the plain one (22) */
static int k_app(const kitrec *k) { return k->tid == 215 || k->tid == 216 ? 23 : 22; }
static const kitrec KITREC[] = {
    {"WWWW", "C-C-", 200, 0, 0}, {"WWWW", "-C-C", 201, 1, 0}, {"WWWW", "C-C-", 202, 2, 0}, {"WWWW", "-C-C", 203, 3, 0},
    {"WWWW", "CC--", 204, 0, 0}, {"WWWW", "-CC-", 205, 1, 0}, {"WWWW", "--CC", 206, 2, 0}, {"WWWW", "C--C", 207, 3, 0},
    {"WWWW", "C-C-", 208, 1, 1}, {"WWWW", "-C-C", 209, 0, 1}, {"WWWW", "-C-C", 210, 2, 1},
    {"WWWW", "C-C-", 215, 3, 1}, {"WWWW", "-C-C", 216, 2, 1}, /* the second door look, used by doors mixed only */
    {"FFWW", "--C-", 211, 3, 0}, {"WWFF", "C---", 212, 1, 0}, {"FWFW", "-C--", 213, 0, 0}, {"WFWF", "---C", 214, 2, 0},
};
#define NKITREC ((int)(sizeof KITREC / sizeof KITREC[0]))
static const int K_CORNER_CCW[4] = {0, 2, 3, 1}; /* NW SW SE NE as indexes into NW NE SW SE */
static const int K_EDGE_CCW[4] = {0, 3, 2, 1};   /* N W S E as indexes into N E S W */

static int k_rot(const int *order, int pos, int k)
{
    int i = 0;
    while (order[i] != pos) i++;
    return order[(i + k) % 4];
}

/* the raw door slot (1.5, 0) facing 0 degrees, as the pack states it after `o` quarter turns */
static void k_door(int o, double *x, double *y, double *bearing)
{
    double px = 1.5, py = 0.0;
    int r;
    for (r = 0; r < o; r++) { double nx = -py; py = px; px = nx; }
    *x = px; *y = py;
    *bearing = o * 3.14159265358979 / 2.0;
    if (*bearing > 3.14159265358979 + 1e-9) *bearing -= 2 * 3.14159265358979;
}

static size_t kit_set_tiles(char *t, size_t cap, size_t len)
{
    static const char *const CKEY[4] = {"TopLeft", "TopRight", "BottomLeft", "BottomRight"};
    static const char *const EKEY[4] = {"Top", "Right", "Bottom", "Left"};
    int i, p;
    for (i = 0; i < NKITREC; i++) {
        const kitrec *k = &KITREC[i];
        len += (size_t)snprintf(t + len, cap - len, "[TILE%d]\nModel=k%d\n", k->tid, k->tid);
        for (p = 0; p < 4; p++)
            len += (size_t)snprintf(t + len, cap - len, "%s=%s\n", CKEY[p], k->sig[k_rot(K_CORNER_CCW, p, k->orient)] == 'F' ? "Floor" : "Wall");
        for (p = 0; p < 4; p++)
            if (k->edges[k_rot(K_EDGE_CCW, p, k->orient)] == 'C')
                len += (size_t)snprintf(t + len, cap - len, "%s=Corridor\n", EKEY[p]);
        len += (size_t)snprintf(t + len, cap - len, "Doors=%d\n", k->door);
        if (k->door)
            len += (size_t)snprintf(t + len, cap - len, "[TILE%dDOOR0]\nType=%d\nX=1.5\nY=0.0\nZ=0.0\nOrientation=0.0\n", k->tid, k_app(k));
    }
    return len;
}

static char *kit_pack_text(void)
{
    char *base = tilepack_text(), *t;
    size_t cap = strlen(base) + 4096, len = strlen(base);
    int i;
    t = malloc(cap);
    memcpy(t, base, len + 1);
    free(base);
    for (i = 0; i < NKITREC; i++) {
        const kitrec *k = &KITREC[i];
        len += (size_t)snprintf(t + len, cap - len, "kit %s %s %d %d %d", k->sig, k->edges, 1 + i % 3, k->tid, k->orient);
        if (k->door) {
            double x, y, b;
            k_door(k->orient, &x, &y, &b);
            len += (size_t)snprintf(t + len, cap - len, " door %.2f %.2f %.2f %.4f", x, y, 0.0, b);
            if (k_app(k) != 22)
                len += (size_t)snprintf(t + len, cap - len, " %d", k_app(k));
        }
        len += (size_t)snprintf(t + len, cap - len, "\n");
    }
    for (i = 0; i < NKITREC; i++)
        len += (size_t)snprintf(t + len, cap - len, "light %d %d 7 %d 3 0 1 2\n", KITREC[i].tid, KITREC[i].tid % 50, KITREC[i].tid % 9);
    return t;
}

/* A donor door the way the stock one looks to the generator, but locked, plot and open so the test sees them reset. */
static int fx_door_git(gff *g)
{
    gff_struct *d;
    gff_field *l;
    fx_root(g, "GIT ");
    l = gff_set_list(g->root, "Door List", fx_err);
    if (!l)
        return -1;
    d = gff_struct_new(8);
    gff_list_append(l, d);
    FX(gff_set_string(d, "Tag", GFF_STRING, "DONOR_DOOR", fx_err));
    FX(gff_set_locstr(d, "LocName", "Door", fx_err));
    FX(gff_set_string(d, "TemplateResRef", GFF_RESREF, "nw_door_ttr_08", fx_err));
    FX(gff_set_int(d, "Appearance", GFF_BYTE, 22, fx_err));
    FX(gff_set_int(d, "Locked", GFF_BYTE, 1, fx_err));
    FX(gff_set_int(d, "Plot", GFF_BYTE, 1, fx_err));
    FX(gff_set_int(d, "AnimationState", GFF_BYTE, 1, fx_err));
    FX(gff_set_int(d, "TrapFlag", GFF_BYTE, 0, fx_err));
    FX(gff_set_float(d, "X", 1.0f, fx_err));
    FX(gff_set_float(d, "Y", 1.0f, fx_err));
    FX(gff_set_float(d, "Z", 0.0f, fx_err));
    FX(gff_set_float(d, "Bearing", 0.0f, fx_err));
    return 0;
}

int main(int argc, char **argv)
{
    int rc;
    gff are, git, ifo;
    uint8_t *ab, *gb, *ib, *mb;
    size_t al, gl, il, ml;
    static const uint8_t fac[] = "repute";
    erf m;
    char err[ERRLEN] = "";

    if (argc != 2) {
        fputs("usage: mkdgfix DIR\n", stderr);
        return 2;
    }
    if (fx_are(&are) || fx_git(&git) || fx_ifo(&ifo))
        return 1;
    if (gff_write(&are, &ab, &al, err) || gff_write(&git, &gb, &gl, err) || gff_write(&ifo, &ib, &il, err)) {
        fprintf(stderr, "mkdgfix: %s\n", err);
        return 1;
    }
    erf_init(&m, "MOD ", &GAME_NWN1);
    if (erf_add(&m, "module", RES_IFO, ib, (uint32_t)il, err) || erf_add(&m, "repute", res_type_by_ext("fac"), fac, sizeof fac - 1, err) ||
        erf_write(&m, &mb, &ml, err)) {
        fprintf(stderr, "mkdgfix: %s\n", err);
        return 1;
    }
    rc = put(argv[1], "tpl_are.are", ab, al) || put(argv[1], "tpl_git.git", gb, gl) || put(argv[1], "base.mod", mb, ml) ||
         put(argv[1], "base.ifo", ib, il);
    if (!rc) {
        char *tp = tilepack_text();
        rc = put(argv[1], "tilepack.txt", (const uint8_t *)tp, strlen(tp));
        free(tp);
    }
    if (!rc) {
        char *st = set_text(0, 0);
        rc = put(argv[1], "tileset.set", (const uint8_t *)st, strlen(st));
        free(st);
        st = set_text(1, 0);
        rc = rc || put(argv[1], "tileset_bad.set", (const uint8_t *)st, strlen(st));
        free(st);
    }
    if (!rc) {
        char *kp = kit_pack_text(), *ks = set_text(0, 1);
        gff dg;
        uint8_t *db;
        size_t dl;
        rc = put(argv[1], "tilepack_kit.txt", (const uint8_t *)kp, strlen(kp)) || put(argv[1], "tileset_kit.set", (const uint8_t *)ks, strlen(ks));
        free(kp);
        free(ks);
        if (!rc && (fx_door_git(&dg) || gff_write(&dg, &db, &dl, err))) {
            fprintf(stderr, "mkdgfix: %s\n", err);
            return 1;
        }
        if (!rc) {
            rc = put(argv[1], "tpl_door.git", db, dl);
            free(db);
            gff_free(&dg);
        }
    }
    free(ab); free(gb); free(ib); free(mb);
    free(m.res);
    gff_free(&are); gff_free(&git); gff_free(&ifo);
    return rc ? 1 : 0;
}
