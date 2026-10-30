/* nwnpack - build the data files `nwndgen --recipe` needs from YOUR NWN:EE install.
 * Authoring time only; nothing from the game is stored in this repository.
 *
 *   nwnpack [--root INSTALL] check               verify the tile-index / rotation conventions on stock areas
 *   nwnpack [--root INSTALL] tilepack OUT.txt    stock-proven tdc01 tile pool + lighting table
 *   nwnpack [--root INSTALL] tilepack-rich OUT.txt  the same plus decorative full-floor tiles proven walkable
 *   nwnpack [--root INSTALL] tile-report         survey of every four-Floor-corner tile and its walkmesh
 *   nwnpack [--root INSTALL] templates OUTDIR    tpl_are.are, tpl_git.git, base_module.mod
 *   nwnpack [--root INSTALL] roster-check FILE   every roster blueprint exists, hostile faction, CR as listed, scripts present
 *
 * INSTALL defaults to $NWN_ROOT. Replaces the earlier python scripts (the former tools/pack scripts). */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "erf.h"
#include "game.h"
#include "gff.h"
#include "key.h"
#include "roster.h"
#include "tileset.h"
#include "util.h"

#define SET_TYPE 2013
#define WOK_TYPE 2016
#define TWODA_TYPE 2017
#define TILESET "tdc01"
/* Floor models whose own walkmeshes are verified fully walkable; plain floor dominates. */
static const char *const CLEAN_FLOOR[5] = {"tdc01_a09_01", "tdc01_o01_01", "tdc01_o05_01", "tdc01_o06_01", "tdc01_o01_02"};

static char err[ERRLEN];
static int die(const char *what) { fprintf(stderr, "nwnpack: %s%s%s\n", what ? what : "", what ? ": " : "", err); return 1; }

/* ---------- stock areas ---------- */
typedef struct {
    char arc[64], name[20], tileset[20];
    int w, h, n;
    int *tid, *orient;
    uint8_t (*light)[7];
    gff_struct **door;      /* clones of the GIT Door List structs */
    size_t ndoor;
} area_t;

typedef struct { area_t *a; size_t n; } areas_t;

static int cmp_str(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

static int ends_ci(const char *s, const char *ext)
{
    size_t l = strlen(s), e = strlen(ext), i;
    if (l < e) return 0;
    for (i = 0; i < e; i++) {
        char c = s[l - e + i];
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        if (c != ext[i]) return 0;
    }
    return 1;
}

static size_t list_dir(const char *dir, const char *ext, char **out, size_t max)
{
    DIR *d = opendir(dir);
    struct dirent *de;
    size_t n = 0, start = 0;
    if (!d) return 0;
    start = n;
    while ((de = readdir(d)) && n < max)
        if (ends_ci(de->d_name, ext)) out[n++] = xstrdup(de->d_name);
    closedir(d);
    qsort(out + start, n - start, sizeof *out, cmp_str);
    return n;
}

static void lower(char *s) { for (; *s; s++) if (*s >= 'A' && *s <= 'Z') *s = (char)(*s + 32); }

/* Collect every stock ARE whose tileset is one of `wanted` (lowercase). */
static int scan_stock(const char *root, const char *const *wanted, size_t nwanted, areas_t *as)
{
    static const struct { const char *sub, *ext; } SRC[2] = {{"data/mod", ".mod"}, {"data/nwm", ".nwm"}};
    int s;
    memset(as, 0, sizeof *as);
    for (s = 0; s < 2; s++) {
        char dir[700], *names[256];
        size_t nn, i;
        snprintf(dir, sizeof dir, "%s/%s", root, SRC[s].sub);
        nn = list_dir(dir, SRC[s].ext, names, 256);
        for (i = 0; i < nn; i++) {
            char path[1000];
            uint8_t *buf;
            size_t len, k;
            erf e;
            size_t before = as->n;
            snprintf(path, sizeof path, "%s/%s", dir, names[i]);
            mem_reset();
            if (read_file(path, &buf, &len, err) != 0) return -1;
            if (erf_parse(buf, len, &e, &GAME_NWN1, err) != 0) { fprintf(stderr, "%s: ", path); return -1; }
            for (k = 0; k < e.nres; k++) {
                gff g;
                const char *ts;
                char tsl[20];
                const gff_field *tl;
                size_t j, w;
                area_t *a;
                if (e.res[k].type != RES_ARE) continue;
                if (gff_parse(e.res[k].data, e.res[k].size, &g, err) != 0) { fprintf(stderr, "%s/%s: ", names[i], e.res[k].name); erf_free(&e); return -1; }
                ts = gff_get_string(g.root, "Tileset");
                snprintf(tsl, sizeof tsl, "%.19s", ts ? ts : "");
                lower(tsl);
                for (w = 0; w < nwanted && strcmp(wanted[w], tsl); w++) ;
                tl = gff_find(g.root, "Tile_List");
                if (w == nwanted || !tl || tl->type != GFF_LIST) { gff_free(&g); continue; }
                as->a = xrealloc(as->a, (as->n + 1) * sizeof *as->a);
                a = &as->a[as->n++];
                memset(a, 0, sizeof *a);
                snprintf(a->arc, sizeof a->arc, "%.63s", names[i]);
                snprintf(a->name, sizeof a->name, "%.19s", e.res[k].name);
                strcpy(a->tileset, tsl);
                a->w = (int)gff_get_int(g.root, "Width", 0);
                a->h = (int)gff_get_int(g.root, "Height", 0);
                a->n = (int)tl->nlist;
                a->tid = xcalloc(tl->nlist + 1, sizeof(int));
                a->orient = xcalloc(tl->nlist + 1, sizeof(int));
                a->light = xcalloc(tl->nlist + 1, 7);
                for (j = 0; j < tl->nlist; j++) {
                    const gff_struct *t = tl->list[j];
                    static const char *const LK[7] = {"Tile_MainLight1", "Tile_MainLight2", "Tile_SrcLight1", "Tile_SrcLight2",
                                                      "Tile_AnimLoop1", "Tile_AnimLoop2", "Tile_AnimLoop3"};
                    int q;
                    a->tid[j] = (int)gff_get_int(t, "Tile_ID", -1);
                    a->orient[j] = (int)gff_get_int(t, "Tile_Orientation", 0);
                    for (q = 0; q < 7; q++) a->light[j][q] = (uint8_t)gff_get_int(t, LK[q], 0);
                }
                {   /* the area GIT, if the archive has one: keep its Door List structs */
                    const erf_res *gr = erf_find(&e, e.res[k].name, RES_GIT);
                    gff gg;
                    if (gr && gff_parse(gr->data, gr->size, &gg, err) == 0) {
                        const gff_field *dl = gff_find(gg.root, "Door List");
                        for (j = 0; dl && dl->type == GFF_LIST && (size_t)j < dl->nlist; j++) {
                            a->door = xrealloc(a->door, (a->ndoor + 1) * sizeof *a->door);
                            a->door[a->ndoor++] = gff_struct_clone(dl->list[j]);
                        }
                        gff_free(&gg);
                    }
                }
                gff_free(&g);
            }
            erf_free(&e);
            fprintf(stderr, "%s: %zu areas\n", names[i], as->n - before);
            free(names[i]);
        }
    }
    return 0;
}

static void free_areas(areas_t *as)
{
    size_t i, j;
    for (i = 0; i < as->n; i++) { free(as->a[i].tid); free(as->a[i].orient); free(as->a[i].light); for (j = 0; j < as->a[i].ndoor; j++) gff_struct_free(as->a[i].door[j]); free(as->a[i].door); }
    free(as->a);
    memset(as, 0, sizeof *as);
}

static int load_tileset(resman *rm, const char *name, tileset *ts)
{
    uint8_t *b;
    size_t l;
    if (resman_get(rm, name, SET_TYPE, &b, &l, err) != 0) return -1;
    {
        int rc = tileset_parse((const char *)b, l, ts, err);
        free(b);
        return rc;
    }
}

/* ---------- check ---------- */
static int cmd_check(const char *root)
{
    static const char *const WANT[2] = {"tdc01", "tin01"};
    areas_t as;
    resman rm;
    size_t t;
    int bad_gate = 0;
    if (resman_open(&rm, root, err) != 0) return die(NULL);
    if (scan_stock(root, WANT, 2, &as) != 0) return die(NULL);
    printf("convention                     areas  violating-areas  total-violations\n");
    for (t = 0; t < 2; t++) {
        tileset ts;
        int o, ccw;
        size_t i, nareas = 0;
        if (load_tileset(&rm, WANT[t], &ts) != 0) return die(NULL);
        printf("%s %zu tiles\n", WANT[t], ts.n);
        for (i = 0; i < as.n; i++) nareas += !strcmp(as.a[i].tileset, WANT[t]);
        for (o = 0; o < ORD_COUNT; o++)
            for (ccw = 1; ccw >= 0; ccw--) {
                long tot = 0;
                size_t bad_areas = 0;
                for (i = 0; i < as.n; i++) {
                    long v;
                    const area_t *a = &as.a[i];
                    if (strcmp(a->tileset, WANT[t])) continue;
                    v = a->n == a->w * a->h ? tileset_violations(&ts, a->w, a->h, a->tid, a->orient, (index_order)o, ccw, 1) : 1;
                    tot += v;
                    bad_areas += v > 0;
                }
                printf("%-6s %-11s %-4s %9zu %12zu %14ld\n", WANT[t], INDEX_ORDER_NAME[o], ccw ? "ccw" : "cw", nareas, bad_areas, tot);
                /* the generator relies on exactly one convention: rows from the south, ccw */
                if ((o == ORD_ROW_BOTTOM && ccw) != (tot == 0)) bad_gate = 1;
            }
        tileset_free(&ts);
    }
    free_areas(&as);
    resman_close(&rm);
    if (bad_gate) { fprintf(stderr, "nwnpack: GATE FAILED: row_bottom/ccw is not the unique zero-violation convention\n"); return 1; }
    printf("gate ok: row_bottom/ccw is the unique zero-violation convention\n");
    return 0;
}

/* ---------- tilepack ---------- */
typedef struct { int tid, o; } uo;
typedef struct { int tid; uint8_t v[7]; unsigned n; } lt;
static int cmp_uo(const void *a, const void *b) { const uo *x = a, *y = b; return x->tid != y->tid ? x->tid - y->tid : x->o - y->o; }
static int cmp_lt(const void *a, const void *b)
{
    const lt *x = a, *y = b;
    if (x->tid != y->tid) return x->tid - y->tid;
    return memcmp(x->v, y->v, 7);
}

typedef struct { unsigned w; int tid, o, decor; } pe;
static int cmp_pe(const void *a, const void *b) { const pe *x = a, *y = b; return x->tid != y->tid ? x->tid - y->tid : x->o - y->o; }
static int cmp_sig(const void *a, const void *b) { return strcmp((const char *)a, (const char *)b); }

static int verify_clean_floors(resman *rm, const tileset *ts)
{
    unsigned char ok[512];
    uint8_t *b;
    size_t l, i;
    (void)ts;
    if (resman_get(rm, "surfacemat", TWODA_TYPE, &b, &l, err) != 0) return -1;
    if (surfacemat_walkable((const char *)b, ok, sizeof ok, err) < 0) { free(b); return -1; }
    free(b);
    for (i = 0; i < 5; i++) {
        wok w;
        if (resman_get(rm, CLEAN_FLOOR[i], WOK_TYPE, &b, &l, err) != 0) return -1;
        if (wok_parse((const char *)b, &w, err) != 0) { free(b); return -1; }
        free(b);
        if (!(wok_walk_area(&w, ok, sizeof ok) > 99.5 && wok_point_walkable(&w, ok, sizeof ok, 0, 0))) {
            wok_free(&w);
            set_err(err, "walkmesh of '%s' is not fully walkable; refusing to use it as a clean floor", CLEAN_FLOOR[i]);
            return -1;
        }
        wok_free(&w);
    }
    return 0;
}

/* Where the generator puts things on a fully-floor tile (tile-local metres): the centre (creatures), the four corner
 * slots 3.5 m off centre (scenery), and a point just inside each of the four edges (where the neighbours connect).
 * A decorative tile is only safe if all of them are walkable and joined. */
static const double SAFE_POINTS[9][2] = {{0, 0}, {3.5, 3.5}, {-3.5, 3.5}, {3.5, -3.5}, {-3.5, -3.5}, {4.5, 0}, {-4.5, 0}, {0, 4.5}, {0, -4.5}};
#define SAFE_NPOINTS 9

static void lower_model(const char *m, char *out, size_t n)
{
    size_t i;
    for (i = 0; m[i] && i + 1 < n; i++) out[i] = (m[i] >= 'A' && m[i] <= 'Z') ? (char)(m[i] + 32) : m[i];
    out[i] = 0;
}

/* A full-floor tile is safe for the generator when the walkmesh covers every point it uses and joins them. */
static int tile_safe(resman *rm, const char *model, const unsigned char *ok)
{
    char wn[48];
    uint8_t *b;
    size_t l;
    wok w;
    int r = 0;
    lower_model(model, wn, sizeof wn);
    if (resman_get(rm, wn, WOK_TYPE, &b, &l, err) != 0) return 0;
    if (wok_parse((const char *)b, &w, err) == 0) {
        r = wok_points_connected(&w, ok, 512, SAFE_POINTS, SAFE_NPOINTS);
        wok_free(&w);
    }
    free(b);
    return r;
}

/* rich: also admit decorative full-floor tiles (pillars, statues ...) whose walkmesh is proven safe, see SAFE_POINTS. */
/* A corridor-kit tile is safe when the walkmesh joins the midpoint of each Corridor edge, the midpoint of any all-Floor edge
 * (the room side of a connector) and, for the door straight, the door slot. Checked at orientation 0, in the tile frame. */
static int kit_tile_safe(resman *rm, const set_tile *t, const unsigned char *ok, int door)
{
    static const double DIR[4][2] = {{0, 4.5}, {4.5, 0}, {0, -4.5}, {-4.5, 0}}; /* N E S W */
    tile_sig_t sg;
    double pts[6][2];
    size_t np = 0;
    char wn[48];
    uint8_t *b;
    size_t l;
    wok w;
    int k, r = 0;
    tile_sig(t, 0, 1, &sg);
    for (k = 0; k < 4; k++) {
        const int c1 = k == E_N ? C_NW : k == E_E ? C_NE : k == E_S ? C_SW : C_NW, c2 = k == E_N ? C_NE : k == E_E ? C_SE : k == E_S ? C_SE : C_SW;
        int floor_edge = !strcmp(sg.corner[c1], "Floor") && !strcmp(sg.corner[c2], "Floor");
        if (!strcmp(sg.edge[k], "Corridor") || floor_edge) { pts[np][0] = DIR[k][0]; pts[np++][1] = DIR[k][1]; }
    }
    if (door) { pts[np][0] = t->dslot[0].x; pts[np++][1] = t->dslot[0].y; }
    lower_model(t->model, wn, sizeof wn);
    if (resman_get(rm, wn, WOK_TYPE, &b, &l, err) != 0) return 0;
    if (wok_parse((const char *)b, &w, err) == 0) {
        r = wok_points_connected(&w, ok, 512, (const double (*)[2])pts, np);
        wok_free(&w);
    }
    free(b);
    return r;
}

static int cmd_tilepack(const char *root, const char *outpath, int rich, int kit)
{
    static const char *const WANT[1] = {TILESET};
    areas_t as;
    resman rm;
    tileset ts;
    uo *use = NULL;
    lt *lts = NULL;
    size_t nuse = 0, nlt = 0, i, q;
    pe *pool[16] = {0};
    size_t npool[16] = {0};
    char sigs[16][5];
    unsigned char ok[512];
    unsigned long decor_w = 0, plain_w = 0;
    int ndecor_tiles = 0, last_decor = -1;
    {
        char *out = NULL;
        size_t olen = 0, ocap = 0;
        uint8_t *odata;
        char *tmp = NULL;
        int rc = 1;
#define APP(...) do { char ln_[256]; int n_ = snprintf(ln_, sizeof ln_, __VA_ARGS__); \
            if (olen + (size_t)n_ + 1 > ocap) { ocap = (olen + (size_t)n_ + 1) * 2; out = xrealloc(out, ocap); } \
            memcpy(out + olen, ln_, (size_t)n_ + 1); olen += (size_t)n_; } while (0)
        if (resman_open(&rm, root, err) != 0) return die(NULL);
        if (scan_stock(root, WANT, 1, &as) != 0) return die(NULL);
        if (load_tileset(&rm, TILESET, &ts) != 0) return die(NULL);
        if (verify_clean_floors(&rm, &ts) != 0) return die("walkmesh check");
        if (rich || kit) {
            uint8_t *sb;
            size_t sl;
            if (resman_get(&rm, "surfacemat", TWODA_TYPE, &sb, &sl, err) != 0) return die(NULL);
            if (surfacemat_walkable((const char *)sb, ok, sizeof ok, err) < 0) { free(sb); return die(NULL); }
            free(sb);
        }
        /* usage of every (tile, orientation) and the most common lighting per tile */
        for (i = 0; i < as.n; i++) {
            const area_t *a = &as.a[i];
            int j;
            for (j = 0; j < a->n; j++) {
                use = xrealloc(use, (nuse + 1) * sizeof *use);
                use[nuse].tid = a->tid[j]; use[nuse].o = a->orient[j]; nuse++;
                lts = xrealloc(lts, (nlt + 1) * sizeof *lts);
                lts[nlt].tid = a->tid[j]; memcpy(lts[nlt].v, a->light[j], 7); lts[nlt].n = 1; nlt++;
            }
        }
        qsort(use, nuse, sizeof *use, cmp_uo);
        qsort(lts, nlt, sizeof *lts, cmp_lt);
        for (i = 0; i < 16; i++) {
            /* sorted-sig order of the exported file: 'F' < 'W' */
            int k;
            for (k = 0; k < 4; k++) sigs[i][k] = (i >> (3 - k)) & 1 ? 'W' : 'F';
            sigs[i][4] = 0;
        }
        qsort(sigs, 16, sizeof sigs[0], cmp_sig);
        for (i = 0; i < nuse;) {
            size_t j = i;
            unsigned n;
            const set_tile *t;
            tile_sig_t sg;
            char sig[5];
            int k, skip = 0, idx;
            while (j < nuse && use[j].tid == use[i].tid && use[j].o == use[i].o) j++;
            n = (unsigned)(j - i);
            if (use[i].tid < 0 || (size_t)use[i].tid >= ts.n) { i = j; continue; }
            t = &ts.t[use[i].tid];
            tile_sig(t, use[i].o, 1, &sg);
            if (t->doors > 0) skip = 1;
            for (k = 0; k < 4; k++) {
                if (sg.edge[k][0] || sg.cheight[k]) skip = 1;
                if (!strcmp(sg.corner[k], "Floor")) sig[k] = 'F';
                else if (!strcmp(sg.corner[k], "Wall")) sig[k] = 'W';
                else skip = 1;
            }
            sig[4] = 0;
            if (!skip) {
                /* sig order here is NW NE SW SE = C_NW.. */
                unsigned weight = 0;
                int decor = 0;
                if (!strcmp(sig, "FFFF")) {
                    for (k = 0; k < 5; k++)
                        if (!strcmp(t->model, CLEAN_FLOOR[k])) weight = !strcmp(t->model, "tdc01_a09_01") ? 6 : 1;
                    if (!weight && rich && !strncmp(t->model, "tdc01_", 6) && tile_safe(&rm, t->model, ok)) {
                        weight = 1;  /* decorative full-floor tile: one share per placed orientation */
                        decor = 1;
                        if (last_decor != use[i].tid) { ndecor_tiles++; last_decor = use[i].tid; }
                    }
                } else if (!strncmp(t->model, "tdc01_a", 7)) weight = n;
                if (weight) {
                    for (idx = 0; idx < 16 && strcmp(sigs[idx], sig); idx++) ;
                    pool[idx] = xrealloc(pool[idx], (npool[idx] + 1) * sizeof **pool);
                    pool[idx][npool[idx]].w = weight; pool[idx][npool[idx]].tid = use[i].tid; pool[idx][npool[idx]].o = use[i].o; pool[idx][npool[idx]].decor = decor;
                    npool[idx]++;
                }
            }
            i = j;
        }
        if (rich) { /* plain floor stays about three quarters of the full-floor cells */
            int fi;
            for (fi = 0; fi < 16 && strcmp(sigs[fi], "FFFF"); fi++) ;
            for (q = 0; fi < 16 && q < npool[fi]; q++) {
                if (pool[fi][q].decor) decor_w += pool[fi][q].w; else plain_w += pool[fi][q].w;
            }
            if (decor_w && plain_w) {
                unsigned long k2 = (3 * decor_w + plain_w - 1) / plain_w;
                for (q = 0; q < npool[fi]; q++)
                    if (!pool[fi][q].decor) pool[fi][q].w = (unsigned)(pool[fi][q].w * k2);
            }
            fprintf(stderr, "rich: %d decorative tile(s), decorative weight %lu, plain weight %lu before scaling\n", ndecor_tiles, decor_w, plain_w);
        }
        APP("nwndgen-tilepack 1\ntileset %s\n", TILESET);
        for (i = 0; i < 16; i++) {
            qsort(pool[i], npool[i], sizeof **pool, cmp_pe);
            for (q = 0; q < npool[i]; q++) APP("cell %s %u %d %d\n", sigs[i], pool[i][q].w, pool[i][q].tid, pool[i][q].o);
        }
        if (kit) {
            /* Corridor kit: stock-placed straight/elbow corridor pieces, the room-wall connector and the door straight.
             * Each record is one (tile, orientation) with its EFFECTIVE corners and edges, so the generator needs no
             * rotation maths; door slots are rotated here as well. */
            static const struct { const char *model; int door; } KIT[8] = {
                {"tdc01_g01_01", 0}, {"tdc01_g01_02", 0}, {"tdc01_g01_03", 0}, {"tdc01_g02_01", 0}, {"tdc01_g02_02", 0}, {"tdc01_g02_03", 0},
                {"tdc01_c13_01", 0}, {"tdc01_g07_01", 1}};
            size_t kt, nk = 0;
            for (kt = 0; kt < 8; kt++) {
                size_t tid;
                long placed = 0;
                int o;
                const set_tile *t = NULL;
                for (tid = 0; tid < ts.n; tid++)
                    if (!strcmp(ts.t[tid].model, KIT[kt].model)) { t = &ts.t[tid]; break; }
                if (!t) { fprintf(stderr, "kit: %s is not in the tileset\n", KIT[kt].model); continue; }
                for (i = 0; i < nuse; i++) if (use[i].tid == (int)tid) placed++;
                if (!placed) { fprintf(stderr, "kit: %s is never placed by stock areas, skipped\n", KIT[kt].model); continue; }
                if (!kit_tile_safe(&rm, t, ok, KIT[kt].door)) { fprintf(stderr, "kit: %s fails the walkmesh rule, skipped\n", KIT[kt].model); continue; }
                for (o = 0; o < 4; o++) {
                    tile_sig_t sg;
                    char cs[5], es[5];
                    int k, bad = 0, nce = 0;
                    long n = 0;
                    tile_sig(t, o, 1, &sg);
                    for (k = 0; k < 4; k++) {
                        cs[k] = !strcmp(sg.corner[k], "Floor") ? 'F' : !strcmp(sg.corner[k], "Wall") ? 'W' : 'P';
                        bad |= cs[k] == 'P' || sg.cheight[k] != 0;
                        if (sg.edge[k][0]) { if (strcmp(sg.edge[k], "Corridor")) bad = 1; else nce++; }
                        es[k] = sg.edge[k][0] ? 'C' : '-';
                    }
                    cs[4] = es[4] = 0;
                    if (bad || !nce) continue;
                    for (i = 0; i < nuse; i++) if (use[i].tid == (int)tid && use[i].o == o) n++;
                    if (KIT[kt].door) {
                        double x = t->dslot[0].x, y = t->dslot[0].y, z = t->dslot[0].z, br = t->dslot[0].orient + 90.0 * o;
                        int r;
                        if (t->ndslot != 1 || t->dslot[0].type != 22) continue;
                        for (r = 0; r < o; r++) { double nx = -y; y = x; x = nx; }
                        while (br > 180.0) br -= 360.0;
                        if (x == 0) x = 0;
                        if (y == 0) y = 0;
                        if (z == 0) z = 0;
                        APP("kit %s %s %ld %d %d door %.2f %.2f %.2f %.4f\n", cs, es, n ? n : 1, (int)tid, o, x, y, z, br * 3.14159265358979 / 180.0);
                    } else
                        APP("kit %s %s %ld %d %d\n", cs, es, n ? n : 1, (int)tid, o);
                    nk++;
                }
            }
            fprintf(stderr, "kit: %zu corridor record(s)\n", nk);
        }
        for (i = 0; i < nlt;) {          /* per tile: most frequent lighting tuple, ties to the smallest tuple */
            size_t j = i, best = i, run;
            unsigned bestn = 0;
            while (j < nlt && lts[j].tid == lts[i].tid) j++;
            for (run = i; run < j;) {
                size_t e = run;
                while (e < j && !memcmp(lts[e].v, lts[run].v, 7)) e++;
                if ((unsigned)(e - run) > bestn) { bestn = (unsigned)(e - run); best = run; }
                run = e;
            }
            APP("light %d %d %d %d %d %d %d %d\n", lts[best].tid, lts[best].v[0], lts[best].v[1], lts[best].v[2],
                lts[best].v[3], lts[best].v[4], lts[best].v[5], lts[best].v[6]);
            i = j;
        }
        odata = (uint8_t *)out;
        if (write_temp(outpath, odata, olen, &tmp, err) != 0 || publish_temp(tmp, outpath, 1, err) != 0) { free(tmp); rc = die(NULL); }
        else { free(tmp); rc = 0; fprintf(stderr, "wrote %s\n", outpath); }
        free(out);
#undef APP
        for (i = 0; i < 16; i++) free(pool[i]);
        free(use); free(lts);
        tileset_free(&ts); free_areas(&as); resman_close(&rm);
        return rc;
    }
}

/* ---------- templates ---------- */
static int open_erf(const char *root, const char *sub, const char *file, erf *e)
{
    char path[1100];
    uint8_t *buf;
    size_t len;
    snprintf(path, sizeof path, "%s/%s/%s", root, sub, file);
    mem_reset();
    if (read_file(path, &buf, &len, err) != 0) return -1;
    if (erf_parse(buf, len, e, &GAME_NWN1, err) != 0) { fprintf(stderr, "%s: ", path); return -1; }
    return 0;
}

static int get_gff(const erf *e, const char *name, uint16_t type, gff *g)
{
    const erf_res *r = erf_find(e, name, type);
    if (!r) { set_err(err, "stock resource '%s.%s' not found", name, res_ext(type)); return -1; }
    return gff_parse(r->data, r->size, g, err);
}

static int write_out(const char *dir, const char *name, const uint8_t *d, size_t n)
{
    char path[1100], *tmp = NULL;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    if (write_temp(path, d, n, &tmp, err) != 0 || publish_temp(tmp, path, 1, err) != 0) { free(tmp); return -1; }
    free(tmp);
    fprintf(stderr, "wrote %s (%zu bytes)\n", path, n);
    return 0;
}

static int cmd_templates(const char *root, const char *outdir)
{
    static const char *const GIT_LISTS[9] = {"Creature List", "Door List", "Encounter List", "List", "Placeable List",
                                             "SoundList", "StoreList", "TriggerList", "WaypointList"};
    erf c1, c2, dr;
    gff are, git, tgit, ifo, tmp;
    uint8_t *buf;
    size_t len, i;
    gff_struct *trig = NULL;

    /* donor ARE: stock tdc01 area Chapter1.nwm:map_m1s1e (Flags=3, lit, no scripts), tiles removed */
    if (open_erf(root, "data/nwm", "Chapter1.nwm", &c1) != 0 || get_gff(&c1, "map_m1s1e", RES_ARE, &are) != 0) return die("donor ARE");
    if (strcmp(gff_get_string(are.root, "Tileset") ? gff_get_string(are.root, "Tileset") : "", TILESET)) { set_err(err, "donor area is not a tdc01 area"); return die(NULL); }
    if (!gff_set_list(are.root, "Tile_List", err)) return die(NULL);
    if (gff_write(&are, &buf, &len, err) != 0 || write_out(outdir, "tpl_are.are", buf, len) != 0) return die(NULL);
    free(buf);

    /* donor GIT: its AreaProperties, empty object lists, and one real stock transition trigger to clone */
    if (get_gff(&c1, "map_m1s1e", RES_GIT, &git) != 0) return die("donor GIT");
    if (open_erf(root, "data/nwm", "Chapter2E.nwm", &c2) != 0 || get_gff(&c2, "m2q4d_06", RES_GIT, &tmp) != 0) return die("donor trigger");
    {
        const gff_field *tl = gff_find(tmp.root, "TriggerList");
        for (i = 0; tl && i < tl->nlist; i++) {
            const char *lk = gff_get_string(tl->list[i], "LinkedTo");
            if (lk && *lk && gff_get_int(tl->list[i], "Type", -1) == 1) { trig = gff_struct_clone(tl->list[i]); break; }
        }
    }
    if (!trig) { set_err(err, "stock transition trigger not found"); return die(NULL); }
    gff_free(&tmp);
    tgit = git;
    tgit.root = gff_struct_clone(git.root);
    gff_free(&git);
    {
        size_t k = 0;
        while (k < tgit.root->nf) {          /* keep only AreaProperties, then add the empty lists */
            const char *lb = tgit.root->f[k].label;
            if (strcmp(lb, "AreaProperties")) gff_remove(tgit.root, lb); else k++;
        }
    }
    if (!gff_find(tgit.root, "AreaProperties")) { set_err(err, "donor GIT has no AreaProperties"); return die(NULL); }
    for (i = 0; i < 9; i++)
        if (!gff_set_list(tgit.root, GIT_LISTS[i], err)) return die(NULL);
    gff_list_append(gff_find(tgit.root, "TriggerList"), trig);
    if (gff_write(&tgit, &buf, &len, err) != 0 || write_out(outdir, "tpl_git.git", buf, len) != 0) return die(NULL);
    free(buf);

    /* donor door: the plain corridor door stock areas put on a tdc01 corridor door tile (g07_01): nw_door_ttr_08, Appearance 22,
     * closed, unlocked, not plot. Written as a GIT holding just that Door List entry. */
    {
        erf xp1;
        gff dg, dgit;
        const gff_field *dl;
        gff_struct *door = NULL;
        if (open_erf(root, "data/nwm", "XP1-Interlude.nwm", &xp1) != 0 || get_gff(&xp1, "area005", RES_GIT, &dg) != 0) return die("donor door");
        dl = gff_find(dg.root, "Door List");
        for (i = 0; dl && i < dl->nlist; i++) {
            const gff_struct *d = dl->list[i];
            const char *tr = gff_get_string(d, "TemplateResRef"), *oo = gff_get_string(d, "OnOpen");
            if (tr && !strcmp(tr, "nw_door_ttr_08") && gff_get_int(d, "Appearance", -1) == 22 && !gff_get_int(d, "Locked", 1) && !gff_get_int(d, "Plot", 1) &&
                !gff_get_int(d, "AnimationState", 1) && !gff_get_int(d, "TrapFlag", 1) && oo && !*oo) { door = gff_struct_clone(d); break; }
        }
        if (!door) { set_err(err, "stock corridor door (nw_door_ttr_08, appearance 22, closed, unlocked) not found in XP1-Interlude area005"); return die(NULL); }
        memset(&dgit, 0, sizeof dgit);
        memcpy(dgit.type, "GIT ", 5);
        dgit.root = gff_struct_new(0xFFFFFFFFu);
        if (!gff_set_list(dgit.root, "Door List", err)) return die(NULL);
        gff_list_append(gff_find(dgit.root, "Door List"), door);
        if (gff_write(&dgit, &buf, &len, err) != 0 || write_out(outdir, "tpl_door.git", buf, len) != 0) return die(NULL);
        free(buf);
        gff_free(&dgit); gff_free(&dg);
        erf_free(&xp1);
    }

    /* base module: stock IFO of a real module with areas and event scripts cleared, plus repute.fac */
    if (open_erf(root, "data/mod", "The Dark Ranger's Treasure.mod", &dr) != 0 || get_gff(&dr, "module", RES_IFO, &ifo) != 0) return die("donor IFO");
    for (i = 0; i < ifo.root->nf; i++)
        if (!strncmp(ifo.root->f[i].label, "Mod_On", 6) && gff_set_string(ifo.root, ifo.root->f[i].label, GFF_RESREF, "", err) != 0) return die(NULL);
    if (gff_set_string(ifo.root, "Mod_OnPlrDeath", GFF_RESREF, "nw_o0_death", err) || gff_set_string(ifo.root, "Mod_OnPlrDying", GFF_RESREF, "nw_o0_dying", err) ||
        gff_set_string(ifo.root, "Mod_OnSpawnBtnDn", GFF_RESREF, "nw_o0_respawn", err) || !gff_set_list(ifo.root, "Mod_Area_list", err))
        return die(NULL);
    {
        erf out;
        const erf_res *fac = erf_find(&dr, "repute", (uint16_t)res_type_by_ext("fac"));
        uint8_t *ib, *mb;
        size_t il, ml;
        if (!fac) { set_err(err, "stock module has no repute.fac"); return die(NULL); }
        if (gff_write(&ifo, &ib, &il, err) != 0) return die(NULL);
        erf_init(&out, "MOD ", &GAME_NWN1);
        if (erf_add(&out, "module", RES_IFO, ib, (uint32_t)il, err) || erf_add(&out, "repute", (uint16_t)res_type_by_ext("fac"), fac->data, fac->size, err) ||
            erf_write(&out, &mb, &ml, err) || write_out(outdir, "base_module.mod", mb, ml))
            return die(NULL);
        free(ib); free(mb); free(out.res);
    }
    gff_free(&are); gff_free(&tgit); gff_free(&ifo);
    erf_free(&c1); erf_free(&c2); erf_free(&dr);
    return 0;
}


/* ---------- ls / extract (exploration helpers) ---------- */
static void print_name(const char *name, uint16_t type, void *ctx)
{
    const char *prefix = ctx;
    (void)type;
    if (!prefix || !strncmp(name, prefix, strlen(prefix)))
        printf("%s\n", name);
}

static int cmd_ls(const char *root, const char *ext, const char *prefix)
{
    resman rm;
    int t = res_type_by_ext(ext);
    if (t < 0) { set_err(err, "unknown resource extension '%s'", ext); return die(NULL); }
    if (resman_open(&rm, root, err) != 0) return die(NULL);
    resman_each(&rm, (uint16_t)t, print_name, (void *)prefix);
    resman_close(&rm);
    return 0;
}

static int cmd_extract(const char *root, const char *name, const char *ext, const char *out)
{
    resman rm;
    uint8_t *b;
    size_t l;
    int t = res_type_by_ext(ext);
    FILE *f;
    if (t < 0) { set_err(err, "unknown resource extension '%s'", ext); return die(NULL); }
    if (resman_open(&rm, root, err) != 0) return die(NULL);
    if (resman_get(&rm, name, (uint16_t)t, &b, &l, err) != 0) return die(NULL);
    f = fopen(out, "wb");
    if (!f || fwrite(b, 1, l, f) != l || fclose(f)) { set_err(err, "cannot write '%s'", out); return die(NULL); }
    free(b);
    resman_close(&rm);
    return 0;
}

/* ---------- roster-check ---------- */

static int rc_problems;
#define RBAD(...) do { rc_problems++; printf("BAD  " __VA_ARGS__); printf("\n"); } while (0)

/* Every non-empty event script field (Script* on creatures, On* on placeables) must exist as an NCS. */
static void rc_scripts(resman *rm, const char *who, const gff_struct *s, const char *prefix)
{
    size_t i;
    for (i = 0; i < s->nf; i++) {
        const gff_field *f = &s->f[i];
        uint8_t *b;
        size_t l;
        char e2[ERRLEN];
        if (f->type != GFF_RESREF || strncmp(f->label, prefix, strlen(prefix)) || !f->blen) continue;
        {
            char nm[17];
            snprintf(nm, sizeof nm, "%.16s", (const char *)f->bytes);
            if (resman_get(rm, nm, RES_NCS, &b, &l, e2) != 0) RBAD("%s: %s '%s' has no compiled script in the install", who, f->label, nm);
            else free(b);
        }
    }
}

static int rc_load(resman *rm, const char *name, uint16_t type, gff *g, uint8_t **b, char *who)
{
    size_t l;
    char e2[ERRLEN];
    if (resman_get(rm, name, type, b, &l, e2) != 0) { RBAD("%s does not resolve in the install: %s", name, e2); return -1; }
    if (gff_parse(*b, l, g, e2) != 0) { RBAD("%s (type %u) does not parse: %s", name, (unsigned)type, e2); free(*b); return -1; }
    (void)who;
    return 0;
}

/* Stacking value of base item `base` from baseitems.2da, or -1 if the table or row cannot be read. Tokens are
 * whitespace separated; the header has no row-id column, rows start with one. */
static int rc_stacking(resman *rm, int base)
{
    uint8_t *b;
    size_t l;
    char e2[ERRLEN], *text, *line, *save = NULL;
    int col = -1, res = -1, ln = 0;
    if (resman_get(rm, "baseitems", TWODA_TYPE, &b, &l, e2) != 0) return -1;
    text = xmalloc(l + 1);
    memcpy(text, b, l);
    text[l] = '\0';
    free(b);
    for (line = strtok_r(text, "\n", &save); line; line = strtok_r(NULL, "\n", &save), ln++) {
        char *tok, *s2 = NULL;
        int k = 0;
        (void)ln;
        if (!strncmp(line, "2DA", 3) || !strncmp(line, "DEFAULT", 7)) continue;
        if (col < 0) { /* header */
            for (tok = strtok_r(line, " \t\r", &s2); tok; tok = strtok_r(NULL, " \t\r", &s2), k++)
                if (!strcmp(tok, "Stacking")) col = k + 1;
            if (col < 0) break;
            continue;
        }
        for (tok = strtok_r(line, " \t\r", &s2); tok; tok = strtok_r(NULL, " \t\r", &s2), k++) {
            if (k == 0 && atoi(tok) != base) break;
            if (k == col) { res = atoi(tok); break; }
        }
        if (res >= 0) break;
    }
    free(text);
    return res;
}

static int cmd_roster_check(const char *root, const char *path)
{
    resman rm;
    roster ro;
    uint8_t *buf, *b;
    size_t len, t;
    int role;
    size_t i;
    gff g;
    int n = 0;

    if (read_file(path, &buf, &len, err) != 0 || roster_parse((const char *)buf, len, &ro, err) != 0) return die(NULL);
    free(buf);
    if (resman_open(&rm, root, err) != 0) return die(NULL);
    for (t = 0; t < ro.ntheme; t++)
        for (role = 0; role < ROLE_N; role++)
            for (i = 0; i < ro.theme[t].n[role]; i++) {
                const roster_entry *e = &ro.theme[t].e[role][i];
                if (rc_load(&rm, e->resref, 2027, &g, &b, NULL) != 0) continue;
                {
                    double cr = gff_get_float(g.root, "ChallengeRating", -1);
                    if (gff_get_int(g.root, "FactionID", -1) != 1) RBAD("%s/%s: %s is not in the hostile faction", ro.theme[t].id, ROLE_NAME[role], e->resref);
                    if ((int)(cr * 100.0 + 0.5) != e->cr) RBAD("%s/%s: %s has CR %.3f, roster says %d.%02d", ro.theme[t].id, ROLE_NAME[role], e->resref, cr, e->cr / 100, e->cr % 100);
                    rc_scripts(&rm, e->resref, g.root, "Script");
                    if (ro.theme[t].nloot || ro.theme[t].gold_per_cr) { /* only the stock spawn script is known to honour X2_L_NOTREASURE */
                        const char *sp = gff_get_string(g.root, "ScriptSpawn");
                        if (!sp || strcmp(sp, "nw_c2_default9"))
                            RBAD("%s/%s: %s has spawn script '%s', not nw_c2_default9, so its random treasure may not switch off", ro.theme[t].id, ROLE_NAME[role], e->resref, sp ? sp : "");
                    }
                }
                n++;
                gff_free(&g);
                free(b);
            }
    if (rc_load(&rm, ro.vault, 2044, &g, &b, NULL) == 0) {
        if (gff_get_int(g.root, "HasInventory", 0) != 1) RBAD("vault %s has no inventory", ro.vault);
        if (gff_get_int(g.root, "Plot", 0) != 0 && gff_get_int(g.root, "Useable", 0) != 1) RBAD("vault %s cannot be used", ro.vault);
        rc_scripts(&rm, ro.vault, g.root, "On");
        n++;
        gff_free(&g);
        free(b);
    }
    for (t = 0; t < ro.ntheme; t++) { /* scenery: props and lights are inert, containers hold items and are neither locked nor trapped */
        int kind;
        for (kind = 0; kind < DECO_N; kind++)
            for (i = 0; i < ro.theme[t].ndeco[kind]; i++) {
                const char *nm = ro.theme[t].deco[kind][i].resref;
                if (rc_load(&rm, nm, 2044, &g, &b, NULL) != 0) continue;
                if (kind == DECO_CONTAINER) {
                    if (gff_get_int(g.root, "HasInventory", 0) != 1) RBAD("%s/%s: %s has no inventory", ro.theme[t].id, DECO_NAME[kind], nm);
                    if (gff_get_int(g.root, "Locked", 0) || gff_get_int(g.root, "Plot", 0) || gff_get_int(g.root, "TrapFlag", 0))
                        RBAD("%s/%s: %s is locked, plot or trapped", ro.theme[t].id, DECO_NAME[kind], nm);
                } else if (gff_get_int(g.root, "HasInventory", 0) || gff_get_int(g.root, "Useable", 0)) {
                    RBAD("%s/%s: %s is usable or holds items", ro.theme[t].id, DECO_NAME[kind], nm);
                }
                rc_scripts(&rm, nm, g.root, "On");
                n++;
                gff_free(&g);
                free(b);
            }
    }
    for (t = 0; t < ro.ntheme; t++) { /* loot: each item exists, is not plot or cursed, and is a single droppable stack; its value is printed for review */
        for (i = 0; i < ro.theme[t].nloot; i++) {
            const roster_loot *li = &ro.theme[t].loot[i];
            if (rc_load(&rm, li->resref, 2025, &g, &b, NULL) != 0) continue;
            if (gff_get_int(g.root, "Plot", 0)) RBAD("%s/loot: %s is a plot item", ro.theme[t].id, li->resref);
            if (gff_get_int(g.root, "Cursed", 0)) RBAD("%s/loot: %s is cursed", ro.theme[t].id, li->resref);
            printf("     %s/loot %-18s value %5lld gp, CR %d.%02d-%d.%02d, %d%%\n", ro.theme[t].id, li->resref, (long long)gff_get_int(g.root, "Cost", 0), li->min_cr / 100, li->min_cr % 100, li->max_cr / 100, li->max_cr % 100, li->chance);
            n++;
            gff_free(&g);
            free(b);
        }
        if (ro.theme[t].gold_per_cr && rc_load(&rm, "nw_it_gold001", 2025, &g, &b, NULL) == 0) {
            n++;
            gff_free(&g);
            free(b);
        }
    }
    {
        const char *items[2] = {ro.key, ro.relic};
        for (i = 0; i < 2; i++)
            if (rc_load(&rm, items[i], 2025, &g, &b, NULL) == 0) {
                int base = (int)gff_get_int(g.root, "BaseItem", -1), st = rc_stacking(&rm, base);
                /* key and relic are told apart by tag, so they must never merge into a stack */
                if (gff_get_int(g.root, "StackSize", 1) != 1 || st != 1)
                    RBAD("%s: must not stack (StackSize %lld, base item %d Stacking %d)", items[i], (long long)gff_get_int(g.root, "StackSize", 1), base, st);
                n++;
                gff_free(&g);
                free(b);
            }
    }
    resman_close(&rm);
    printf("roster-check: %d blueprint(s) checked, %d problem(s)\n", n, rc_problems);
    return rc_problems ? 1 : 0;
}

/* ---------- tile-report: every tdc01 tile with four Floor corners (read-only survey) ---------- */
static int cmd_tile_report(const char *root)
{
    static const char *const WANT[1] = {TILESET};
    areas_t as;
    resman rm;
    tileset ts;
    unsigned char ok[512];
    uint8_t *b;
    size_t l, i, a;
    long *uses;
    int j, n = 0;

    if (resman_open(&rm, root, err) != 0) return die(NULL);
    if (scan_stock(root, WANT, 1, &as) != 0) return die(NULL);
    if (load_tileset(&rm, TILESET, &ts) != 0) return die(NULL);
    if (resman_get(&rm, "surfacemat", TWODA_TYPE, &b, &l, err) != 0) return die(NULL);
    if (surfacemat_walkable((const char *)b, ok, sizeof ok, err) < 0) { free(b); return die(NULL); }
    free(b);
    uses = xcalloc(ts.n ? ts.n : 1, sizeof *uses);
    for (a = 0; a < as.n; a++)
        for (j = 0; j < as.a[a].n; j++)
            if (as.a[a].tid[j] >= 0 && (size_t)as.a[a].tid[j] < ts.n) uses[as.a[a].tid[j]]++;
    printf("%d stock %s areas\n", (int)as.n, TILESET);
    printf("%-4s %-16s %5s %5s %5s %6s %6s %5s %s\n", "tid", "model", "doors", "edges", "hts", "walk%", "centre", "safe", "stock uses");
    for (i = 0; i < ts.n; i++) {
        const set_tile *t = &ts.t[i];
        char wn[48];
        wok w;
        int k, edges = 0, hts = 0;
        double pct = -1;
        int centre = -1, safe = -1;
        for (k = 0; k < 4; k++) {
            if (strcmp(t->corner[k], "Floor")) break;
            edges += t->edge[k][0] != 0;
            hts += t->cheight[k] != 0;
        }
        if (k < 4) continue;
        lower_model(t->model, wn, sizeof wn);
        if (resman_get(&rm, wn, WOK_TYPE, &b, &l, err) == 0) {
            if (wok_parse((const char *)b, &w, err) == 0) {
                pct = wok_walk_area(&w, ok, sizeof ok);
                centre = wok_point_walkable(&w, ok, sizeof ok, 0, 0);
                safe = wok_points_connected(&w, ok, sizeof ok, SAFE_POINTS, SAFE_NPOINTS);
                wok_free(&w);
            }
            free(b);
        }
        printf("%-4d %-16s %5d %5d %5d %6.1f %6d %5d %ld\n", (int)i, t->model, t->doors, edges, hts, pct, centre, safe, uses[i]);
        n++;
    }
    printf("%d tiles with four Floor corners\n", n);
    free(uses);
    free_areas(&as);
    resman_close(&rm);
    return 0;
}

/* ---------- kit-report: corridor-kit tiles and the doors stock areas put on them (read-only survey) ---------- */
static void print_field(const gff_struct *s, const gff_field *f)
{
    switch (f->type) {
    case GFF_FLOAT: case GFF_DOUBLE: printf(" %s=%.4f", f->label, gff_get_float(s, f->label, 0)); break;
    case GFF_STRING: case GFF_RESREF: printf(" %s='%s'", f->label, gff_get_string(s, f->label)); break;
    case GFF_LOCSTR: printf(" %s=<locstr>", f->label); break;
    case GFF_VOID: printf(" %s=<%zu bytes>", f->label, f->blen); break;
    case GFF_STRUCT: printf(" %s=<struct>", f->label); break;
    case GFF_LIST: printf(" %s=<list %zu>", f->label, f->nlist); break;
    default: printf(" %s=%lld", f->label, (long long)gff_get_int(s, f->label, 0)); break;
    }
}

static int cmd_kit_report(const char *root)
{
    static const char *const WANT[1] = {TILESET};
    areas_t as;
    resman rm;
    tileset ts;
    long (*uses)[4];
    size_t i, a, nshown = 0;
    int j;

    if (resman_open(&rm, root, err) != 0) return die(NULL);
    if (scan_stock(root, WANT, 1, &as) != 0) return die(NULL);
    if (load_tileset(&rm, TILESET, &ts) != 0) return die(NULL);
    uses = xcalloc(ts.n ? ts.n : 1, sizeof *uses);
    for (a = 0; a < as.n; a++)
        for (j = 0; j < as.a[a].n; j++) {
            int t = as.a[a].tid[j], o = as.a[a].orient[j];
            if (t >= 0 && (size_t)t < ts.n && o >= 0 && o < 4) uses[t][o]++;
        }
    printf("%zu stock %s areas\n\ntiles with an edge crosser (corners NW NE SW SE as placed at orientation 0):\n", as.n, TILESET);
    printf("%-4s %-14s %-5s %-9s %5s  uses by orientation 0/1/2/3\n", "tid", "model", "corn", "edgesTRBL", "doors");
    for (i = 0; i < ts.n; i++) {
        const set_tile *t = &ts.t[i];
        tile_sig_t sg;
        char cs[5], es[5];
        int k, any = 0;
        tile_sig(t, 0, 1, &sg);
        for (k = 0; k < 4; k++) {
            cs[k] = !strcmp(sg.corner[k], "Floor") ? 'F' : !strcmp(sg.corner[k], "Wall") ? 'W' : 'P';
            es[k] = sg.edge[k][0] ? sg.edge[k][0] : '-';
            any |= sg.edge[k][0] != 0;
        }
        cs[4] = es[4] = 0;
        if (!any) continue;
        printf("%-4zu %-14s %-5s %-9s %5d  %ld/%ld/%ld/%ld\n", i, t->model, cs, es, t->doors, uses[i][0], uses[i][1], uses[i][2], uses[i][3]);
    }
    printf("\nGIT doors on tiles with a Door slot (area, tile, orientation, then every field of the Door List struct):\n");
    for (a = 0; a < as.n; a++) {
        const area_t *ar = &as.a[a];
        size_t d;
        for (d = 0; d < ar->ndoor; d++) {
            const gff_struct *s = ar->door[d];
            int cx = (int)(gff_get_float(s, "X", -1) / 10.0), cy = (int)(gff_get_float(s, "Y", -1) / 10.0), tid = -1, o = -1;
            size_t f;
            if (cx >= 0 && cy >= 0 && cx < ar->w && cy < ar->h && cy * ar->w + cx < ar->n) {
                tid = ar->tid[cy * ar->w + cx];
                o = ar->orient[cy * ar->w + cx];
            }
            if (tid < 0 || (size_t)tid >= ts.n || strncmp(ts.t[tid].model, "tdc01_g07", 9)) continue;
            if (nshown++ >= 12) continue;
            printf("%s/%s cell %d,%d tile %s orient %d:", ar->arc, ar->name, cx, cy, ts.t[tid].model, o);
            for (f = 0; f < s->nf; f++) print_field(s, &s->f[f]);
            printf("\n");
        }
    }
    printf("(%zu door(s) on g07 tiles in total)\n", nshown);
    free(uses);
    tileset_free(&ts);
    free_areas(&as);
    resman_close(&rm);
    return 0;
}

int main(int argc, char **argv)
{
    const char *root = getenv("NWN_ROOT");
    int a = 1;
    if (a + 1 < argc && !strcmp(argv[a], "--root")) { root = argv[a + 1]; a += 2; }
    if (!root || a >= argc) {
        fputs("usage: nwnpack [--root INSTALL] check | tilepack OUT.txt | tilepack-rich OUT.txt | tilepack-kit OUT.txt | templates OUTDIR | ls EXT [PREFIX] | extract NAME EXT OUT | roster-check ROSTER.txt | tile-report | kit-report   (INSTALL defaults to $NWN_ROOT)\n", stderr);
        return 2;
    }
    if (!strcmp(argv[a], "check") && a + 1 == argc) return cmd_check(root);
    if (!strcmp(argv[a], "tilepack") && a + 2 == argc) return cmd_tilepack(root, argv[a + 1], 0, 0);
    if (!strcmp(argv[a], "tilepack-rich") && a + 2 == argc) return cmd_tilepack(root, argv[a + 1], 1, 0);
    if (!strcmp(argv[a], "tilepack-kit") && a + 2 == argc) return cmd_tilepack(root, argv[a + 1], 1, 1);
    if (!strcmp(argv[a], "templates") && a + 2 == argc) return cmd_templates(root, argv[a + 1]);
    if (!strcmp(argv[a], "ls") && (a + 2 == argc || a + 3 == argc)) return cmd_ls(root, argv[a + 1], a + 3 == argc ? argv[a + 2] : NULL);
    if (!strcmp(argv[a], "extract") && a + 4 == argc) return cmd_extract(root, argv[a + 1], argv[a + 2], argv[a + 3]);
    if (!strcmp(argv[a], "kit-report") && a + 1 == argc) return cmd_kit_report(root);
    if (!strcmp(argv[a], "tile-report") && a + 1 == argc) return cmd_tile_report(root);
    if (!strcmp(argv[a], "roster-check") && a + 2 == argc) return cmd_roster_check(root, argv[a + 1]);
    fputs("nwnpack: unknown command or arguments\n", stderr);
    return 2;
}
