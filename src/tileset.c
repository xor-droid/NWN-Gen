#include "tileset.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

const char *const INDEX_ORDER_NAME[ORD_COUNT] = {"row_bottom", "row_top", "col_left", "col_right"};

/* ---------- .set ---------- */

static void copy_to(char *dst, size_t cap, const char *v)
{
    snprintf(dst, cap, "%s", v);
}

static int set_field(set_tile *t, const char *k, const char *v)
{
    static const char *const CK[4] = {"TopLeft", "TopRight", "BottomLeft", "BottomRight"};
    static const char *const EK[4] = {"Top", "Right", "Bottom", "Left"};
    int i;
    if (!strcmp(k, "Model")) { copy_to(t->model, sizeof t->model, v); return 0; }
    if (!strcmp(k, "WalkMesh")) { copy_to(t->walkmesh, sizeof t->walkmesh, v); return 0; }
    if (!strcmp(k, "Doors")) { t->doors = atoi(v); return 0; }
    for (i = 0; i < 4; i++) {
        char hk[32];
        if (!strcmp(k, CK[i])) { copy_to(t->corner[i], sizeof t->corner[i], v); return 0; }
        snprintf(hk, sizeof hk, "%sHeight", CK[i]);
        if (!strcmp(k, hk)) { t->cheight[i] = atoi(v); return 0; }
        if (!strcmp(k, EK[i])) { copy_to(t->edge[i], sizeof t->edge[i], v); return 0; }
    }
    return 0;
}

int tileset_parse(const char *text, size_t len, tileset *ts, char *err)
{
    size_t pos = 0, count = 0;
    int in_tiles_hdr = 0;
    long cur = -1;
    int cur_door = -1;   /* inside a [TILEnDOORm] section */
    int have_count = 0;
    memset(ts, 0, sizeof *ts);
    while (pos < len) {
        char line[512], *s, *e;
        size_t n = 0;
        while (pos < len && text[pos] != '\n') {
            if (n + 1 < sizeof line)
                line[n++] = text[pos];
            pos++;
        }
        pos++;
        line[n] = '\0';
        s = line;
        while (*s == ' ' || *s == '\t' || *s == '\r') s++;
        e = s + strlen(s);
        while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) *--e = '\0';
        if (*s == '[' && e > s && e[-1] == ']') {
            e[-1] = '\0';
            in_tiles_hdr = !strcmp(s + 1, "TILES");
            cur = -1;
            cur_door = -1;
            if (!strncmp(s + 1, "TILE", 4) && s[5] >= '0' && s[5] <= '9') {
                cur = atol(s + 5);
                if (!have_count || cur < 0 || (size_t)cur >= count)
                    cur = -1;
                else {
                    const char *dp = s + 5;
                    while (*dp >= '0' && *dp <= '9') dp++;
                    if (!strncmp(dp, "DOOR", 4) && dp[4] >= '0' && dp[4] <= '9') {
                        cur_door = atoi(dp + 4);
                        if (cur_door >= 4) cur = -1;
                        else if (ts->t[cur].ndslot < cur_door + 1) ts->t[cur].ndslot = cur_door + 1;
                    }
                }
            }
        } else if (strchr(s, '=')) {
            char *eq = strchr(s, '=');
            *eq = '\0';
            if (in_tiles_hdr && !strcmp(s, "Count")) {
                long c = atol(eq + 1);
                if (c < 1 || c > 100000 || have_count) {
                    set_err(err, "tileset: bad or repeated tile count");
                    tileset_free(ts);
                    return -1;
                }
                count = (size_t)c;
                ts->t = xcalloc(count, sizeof *ts->t);
                ts->n = count;
                have_count = 1;
            } else if (cur >= 0 && cur_door >= 0) {
                const char *v = eq + 1;
                if (!strcmp(s, "Type")) ts->t[cur].dslot[cur_door].type = atoi(v);
                else if (!strcmp(s, "X")) ts->t[cur].dslot[cur_door].x = atof(v);
                else if (!strcmp(s, "Y")) ts->t[cur].dslot[cur_door].y = atof(v);
                else if (!strcmp(s, "Z")) ts->t[cur].dslot[cur_door].z = atof(v);
                else if (!strcmp(s, "Orientation")) ts->t[cur].dslot[cur_door].orient = atof(v);
            } else if (cur >= 0) {
                set_field(&ts->t[cur], s, eq + 1);
            }
        }
    }
    if (!have_count) {
        set_err(err, "tileset has no [TILES] Count");
        return -1;
    }
    return 0;
}

void tileset_free(tileset *ts)
{
    free(ts->t);
    memset(ts, 0, sizeof *ts);
}

/* ---------- rotation ---------- */

static const int CORNER_CCW[4] = {C_NW, C_SW, C_SE, C_NE}; /* as seen from above */
static const int EDGE_CCW[4] = {E_N, E_W, E_S, E_E};

static int rot(const int *order, int pos, int k, int ccw)
{
    int i = 0;
    while (order[i] != pos) i++;
    return order[((i + (ccw ? k : -k)) % 4 + 4) % 4];
}

void tile_sig(const set_tile *t, int o, int ccw, tile_sig_t *out)
{
    int p;
    for (p = 0; p < 4; p++) {
        int d = rot(CORNER_CCW, p, o, ccw);
        memcpy(out->corner[d], t->corner[p], sizeof out->corner[d]);
        out->cheight[d] = t->cheight[p];
        d = rot(EDGE_CCW, p, o, ccw);
        memcpy(out->edge[d], t->edge[p], sizeof out->edge[d]);
    }
}

static void cell_xy(int i, int w, int h, index_order ord, int *x, int *y)
{
    switch (ord) {
    case ORD_ROW_BOTTOM: *x = i % w; *y = i / w; break;
    case ORD_ROW_TOP:    *x = i % w; *y = h - 1 - i / w; break;
    case ORD_COL_LEFT:   *x = i / h; *y = i % h; break;
    default:             *x = w - 1 - i / h; *y = i % h; break;
    }
}

long tileset_violations(const tileset *ts, int w, int h, const int *tid, const int *orient, index_order ord, int ccw,
                        int use_heights)
{
    long bad = 0;
    int i, x, y, n = w * h;
    tile_sig_t *grid = xcalloc((size_t)(n ? n : 1), sizeof *grid);
    unsigned char *have = xcalloc((size_t)(n ? n : 1), 1);
    for (i = 0; i < n; i++) {
        cell_xy(i, w, h, ord, &x, &y);
        if (tid[i] < 0 || (size_t)tid[i] >= ts->n || x < 0 || y < 0 || x >= w || y >= h) {
            bad++;
            continue;
        }
        tile_sig(&ts->t[tid[i]], orient[i], ccw, &grid[y * w + x]);
        have[y * w + x] = 1;
    }
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            const tile_sig_t *a = &grid[y * w + x], *b;
            if (!have[y * w + x])
                continue;
            if (x + 1 < w && have[y * w + x + 1]) {
                b = &grid[y * w + x + 1];
                bad += strcmp(a->corner[C_NE], b->corner[C_NW]) != 0;
                bad += strcmp(a->corner[C_SE], b->corner[C_SW]) != 0;
                if (use_heights) {
                    bad += a->cheight[C_NE] != b->cheight[C_NW];
                    bad += a->cheight[C_SE] != b->cheight[C_SW];
                }
                bad += strcmp(a->edge[E_E], b->edge[E_W]) != 0;
            }
            if (y + 1 < h && have[(y + 1) * w + x]) {
                b = &grid[(y + 1) * w + x];
                bad += strcmp(a->corner[C_NW], b->corner[C_SW]) != 0;
                bad += strcmp(a->corner[C_NE], b->corner[C_SE]) != 0;
                if (use_heights) {
                    bad += a->cheight[C_NW] != b->cheight[C_SW];
                    bad += a->cheight[C_NE] != b->cheight[C_SE];
                }
                bad += strcmp(a->edge[E_N], b->edge[E_S]) != 0;
            }
        }
    free(grid);
    free(have);
    return bad;
}

/* ---------- walkmesh ---------- */

static int next_line(const char **p, char *buf, size_t cap)
{
    size_t n = 0;
    if (!**p)
        return 0;
    while (**p && **p != '\n') {
        if (n + 1 < cap)
            buf[n++] = **p;
        (*p)++;
    }
    if (**p == '\n')
        (*p)++;
    buf[n] = '\0';
    return 1;
}

static int wok_parse_inner(const char *text, wok *w, char *err);

int wok_parse(const char *text, wok *w, char *err)
{
    if (wok_parse_inner(text, w, err) != 0) {
        wok_free(w);
        return -1;
    }
    return 0;
}

static int wok_parse_inner(const char *text, wok *w, char *err)
{
    const char *p = text;
    char line[512];
    memset(w, 0, sizeof *w);
    if (!strstr(text, "beginwalkmeshgeom")) {
        set_err(err, "not an NWN1 ASCII walkmesh");
        return -1;
    }
    while (next_line(&p, line, sizeof line)) {
        char *tok = strtok(line, " \t\r");
        if (!tok)
            continue;
        if (!strcmp(tok, "verts") || !strcmp(tok, "faces")) {
            int isv = tok[0] == 'v';
            char *cnt = strtok(NULL, " \t\r");
            long n = cnt ? atol(cnt) : -1, j;
            if (n < 0 || n > 100000) {
                set_err(err, "bad walkmesh count");
                return -1;
            }
            for (j = 0; j < n; j++) {
                char row[512], *f[16];
                int nf = 0;
                char *t;
                if (!next_line(&p, row, sizeof row)) {
                    set_err(err, "truncated walkmesh");
                    return -1;
                }
                for (t = strtok(row, " \t\r"); t && nf < 16; t = strtok(NULL, " \t\r")) f[nf++] = t;
                if (nf < (isv ? 3 : 4)) {
                    set_err(err, "bad walkmesh row");
                    return -1;
                }
                if (isv) {
                    w->v = xrealloc(w->v, (w->nv + 1) * sizeof *w->v);
                    w->v[w->nv][0] = atof(f[0]); w->v[w->nv][1] = atof(f[1]); w->v[w->nv][2] = atof(f[2]);
                    w->nv++;
                } else {
                    w->f = xrealloc(w->f, (w->nf + 1) * sizeof *w->f);
                    w->mat = xrealloc(w->mat, (w->nf + 1) * sizeof *w->mat);
                    w->f[w->nf][0] = atoi(f[0]); w->f[w->nf][1] = atoi(f[1]); w->f[w->nf][2] = atoi(f[2]);
                    w->mat[w->nf] = atoi(f[nf - 1]);
                    w->nf++;
                }
            }
        }
    }
    {
        size_t i;
        for (i = 0; i < w->nf; i++)
            if (w->f[i][0] < 0 || w->f[i][1] < 0 || w->f[i][2] < 0 || (size_t)w->f[i][0] >= w->nv ||
                (size_t)w->f[i][1] >= w->nv || (size_t)w->f[i][2] >= w->nv) {
                set_err(err, "walkmesh face refers to a missing vertex");
                return -1;
            }
    }
    return 0;
}

void wok_free(wok *w)
{
    free(w->v);
    free(w->f);
    free(w->mat);
    memset(w, 0, sizeof *w);
}

int surfacemat_walkable(const char *text, unsigned char *ok, size_t ok_len, char *err)
{
    const char *p = text;
    char line[1024];
    int li = 0, wi = -1, count = 0;
    memset(ok, 0, ok_len);
    while (next_line(&p, line, sizeof line)) {
        char *f[64], *t;
        int nf = 0, k;
        for (t = strtok(line, " \t\r"); t && nf < 64; t = strtok(NULL, " \t\r")) f[nf++] = t;
        if (!nf)
            continue;
        if (li == 1) {
            for (k = 0; k < nf; k++)
                if (!strcmp(f[k], "Walk"))
                    wi = k;
            if (wi < 0) {
                set_err(err, "surfacemat.2da has no Walk column");
                return -1;
            }
        } else if (li >= 2 && nf > wi + 1 && !strcmp(f[wi + 1], "1")) {
            long id = atol(f[0]);
            if (id >= 0 && (size_t)id < ok_len) {
                ok[id] = 1;
                count++;
            }
        }
        li++;
    }
    if (wi < 0) {
        set_err(err, "surfacemat.2da is empty");
        return -1;
    }
    return count;
}

double wok_walk_area(const wok *w, const unsigned char *ok, size_t ok_len)
{
    double s = 0;
    size_t i;
    for (i = 0; i < w->nf; i++) {
        const double *a, *b, *c;
        if (w->mat[i] < 0 || (size_t)w->mat[i] >= ok_len || !ok[w->mat[i]])
            continue;
        a = w->v[w->f[i][0]]; b = w->v[w->f[i][1]]; c = w->v[w->f[i][2]];
        s += fabs((b[0] - a[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (b[1] - a[1])) / 2;
    }
    return s;
}

int wok_point_walkable(const wok *w, const unsigned char *ok, size_t ok_len, double x, double y)
{
    size_t i;
    for (i = 0; i < w->nf; i++) {
        const double *A, *B, *C;
        double d, l1, l2;
        if (w->mat[i] < 0 || (size_t)w->mat[i] >= ok_len || !ok[w->mat[i]])
            continue;
        A = w->v[w->f[i][0]]; B = w->v[w->f[i][1]]; C = w->v[w->f[i][2]];
        d = (B[1] - C[1]) * (A[0] - C[0]) + (C[0] - B[0]) * (A[1] - C[1]);
        if (d == 0)
            continue;
        l1 = ((B[1] - C[1]) * (x - C[0]) + (C[0] - B[0]) * (y - C[1])) / d;
        l2 = ((C[1] - A[1]) * (x - C[0]) + (A[0] - C[0]) * (y - C[1])) / d;
        if (l1 >= -1e-6 && l2 >= -1e-6 && 1 - l1 - l2 >= -1e-6)
            return 1;
    }
    return 0;
}

/* Which walkable face holds (x, y), or -1. */
static long wok_face_at(const wok *w, const unsigned char *ok, size_t ok_len, double x, double y)
{
    size_t i;
    for (i = 0; i < w->nf; i++) {
        const double *A, *B, *C;
        double d, l1, l2;
        if (w->mat[i] < 0 || (size_t)w->mat[i] >= ok_len || !ok[w->mat[i]])
            continue;
        A = w->v[w->f[i][0]]; B = w->v[w->f[i][1]]; C = w->v[w->f[i][2]];
        d = (B[1] - C[1]) * (A[0] - C[0]) + (C[0] - B[0]) * (A[1] - C[1]);
        if (d == 0)
            continue;
        l1 = ((B[1] - C[1]) * (x - C[0]) + (C[0] - B[0]) * (y - C[1])) / d;
        l2 = ((C[1] - A[1]) * (x - C[0]) + (A[0] - C[0]) * (y - C[1])) / d;
        if (l1 >= -1e-6 && l2 >= -1e-6 && 1 - l1 - l2 >= -1e-6)
            return (long)i;
    }
    return -1;
}

static size_t uf_find(size_t *p, size_t i)
{
    while (p[i] != i) {
        p[i] = p[p[i]];
        i = p[i];
    }
    return i;
}

/* Two faces are neighbours when they share an edge, i.e. two corner positions (compared by x and y, since a mesh
 * may repeat a position as separate vertices). */
static int faces_share_edge(const wok *w, size_t a, size_t b)
{
    int i, j, shared = 0;
    for (i = 0; i < 3; i++)
        for (j = 0; j < 3; j++) {
            const double *p = w->v[w->f[a][i]], *q = w->v[w->f[b][j]];
            if (fabs(p[0] - q[0]) < 1e-3 && fabs(p[1] - q[1]) < 1e-3)
                shared++;
        }
    return shared >= 2;
}

int wok_points_connected(const wok *w, const unsigned char *ok, size_t ok_len, const double (*pts)[2], size_t npts)
{
    size_t i, j, *parent;
    long *face;
    int good = 1;

    if (!npts)
        return 1;
    parent = malloc((w->nf ? w->nf : 1) * sizeof *parent);
    face = malloc(npts * sizeof *face);
    if (!parent || !face) {
        free(parent);
        free(face);
        return 0;
    }
    for (i = 0; i < w->nf; i++)
        parent[i] = i;
    for (i = 0; i < npts; i++) {
        face[i] = wok_face_at(w, ok, ok_len, pts[i][0], pts[i][1]);
        if (face[i] < 0)
            good = 0;
    }
    for (i = 0; good && i < w->nf; i++) {
        if (w->mat[i] < 0 || (size_t)w->mat[i] >= ok_len || !ok[w->mat[i]])
            continue;
        for (j = i + 1; j < w->nf; j++)
            if (w->mat[j] >= 0 && (size_t)w->mat[j] < ok_len && ok[w->mat[j]] && faces_share_edge(w, i, j))
                parent[uf_find(parent, i)] = uf_find(parent, j);
    }
    for (i = 1; good && i < npts; i++)
        if (uf_find(parent, (size_t)face[i]) != uf_find(parent, (size_t)face[0]))
            good = 0;
    free(parent);
    free(face);
    return good;
}
