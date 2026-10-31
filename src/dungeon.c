#include "dungeon.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sha256.h"
#include "spawn.h"
#include "util.h"

/* ---------- tile pack ---------- */

static int sig_index(const char *s)
{
    int i, v = 0;
    for (i = 0; i < 4; i++) {
        if (s[i] != 'F' && s[i] != 'W')
            return -1;
        v = v << 1 | (s[i] == 'F');
    }
    return v;
}

static int is_diagonal(int idx) { return idx == 9 || idx == 6; } /* F W W F / W F F W */

static int uint_field(const char **p, long lo, long hi, long *out)
{
    char *end;
    long v;
    if (**p < '0' || **p > '9')
        return -1;
    v = strtol(*p, &end, 10);
    if (v < lo || v > hi || (*end && *end != ' '))
        return -1;
    *p = *end ? end + 1 : end;
    *out = v;
    return 0;
}

int tilepack_parse(const char *text, size_t len, tilepack *p, char *err)
{
    size_t pos = 0, i;
    int line = 0, k;
    memset(p, 0, sizeof *p);
    while (pos < len) {
        char buf[256];
        size_t n = 0;
        const char *q;
        long a, b, c;
        while (pos < len && text[pos] != '\n') {
            if (n + 1 < sizeof buf)
                buf[n] = text[pos];
            n++;
            pos++;
        }
        pos++;
        line++;
        if (n >= sizeof buf) {
            set_err(err, "tilepack line %d: too long", line);
            goto bad;
        }
        buf[n] = '\0';
        if (n && buf[n - 1] == '\r')
            buf[--n] = '\0';
        if (!n)
            continue;
        if (line == 1) {
            if (strcmp(buf, "nwndgen-tilepack 1")) {
                set_err(err, "tilepack: first line must be 'nwndgen-tilepack 1'");
                goto bad;
            }
        } else if (line == 2) {
            if (strncmp(buf, "tileset ", 8) || strlen(buf + 8) < 1 || strlen(buf + 8) > 16) {
                set_err(err, "tilepack: second line must be 'tileset NAME'");
                goto bad;
            }
            strcpy(p->tileset, buf + 8);
        } else if (!strncmp(buf, "cell ", 5)) {
            int idx;
            pack_cell *pc;
            /* Diagonal patterns may appear (stock areas use them); layouts never produce them. */
            if (strlen(buf) < 10 || buf[9] != ' ' || (buf[9] = '\0', (idx = sig_index(buf + 5)) < 0)) {
                set_err(err, "tilepack line %d: bad corner pattern", line);
                goto bad;
            }
            q = buf + 10;
            if (uint_field(&q, 1, 1000000, &a) || uint_field(&q, 0, 100000, &b) || uint_field(&q, 0, 3, &c) || *q) {
                set_err(err, "tilepack line %d: expected 'cell SIG weight tile orientation'", line);
                goto bad;
            }
            pc = &p->cell[idx];
            pc->t = xrealloc(pc->t, (pc->n + 1) * sizeof *pc->t);
            pc->t[pc->n].w = (unsigned)a;
            pc->t[pc->n].tid = (int)b;
            pc->t[pc->n].orient = (int)c;
            pc->n++;
        } else if (!strncmp(buf, "kit ", 4)) {
            char sg[8], mk[8];
            unsigned w;
            int tid, ori, used = 0, idx, mask = 0, dmask = 0, has_door = 0, app = 22, props = 0;
            double dx = 0, dy = 0, dz = 0, br = 0;
            pack_kit *pk;
            if (sscanf(buf + 4, "%7s %7s %u %d %d%n", sg, mk, &w, &tid, &ori, &used) != 5) {
                set_err(err, "tilepack line %d: expected 'kit SIG EDGES weight tile orientation [door X Y Z BEARING [LOOK]] [props N]'", line);
                goto bad;
            }
            q = buf + 4 + used;
            if (*q && strncmp(q, " props ", 7)) {
                int used2 = 0, used3 = 0;
                if (sscanf(q, " door %lf %lf %lf %lf%n", &dx, &dy, &dz, &br, &used2) != 4) {
                    set_err(err, "tilepack line %d: bad kit door suffix", line);
                    goto bad;
                }
                if (q[used2] && strncmp(q + used2, " props ", 7) && (sscanf(q + used2, " %d%n", &app, &used3) != 1 || (q[used2 + used3] && strncmp(q + used2 + used3, " props ", 7)) || app < 0 || app > 255)) {
                    set_err(err, "tilepack line %d: bad kit door suffix (look must be 0-255)", line);
                    goto bad;
                }
                has_door = 1;
                q += used2 + used3;
            }
            if (*q) {
                int used4 = 0;
                if (sscanf(q, " props %d%n", &props, &used4) != 1 || q[used4] || props < 1 || props > 63) {
                    set_err(err, "tilepack line %d: bad kit props suffix (props must be 1-63)", line);
                    goto bad;
                }
            }
            idx = strlen(sg) == 4 ? sig_index(sg) : -1;
            for (k = 0; k < 4; k++) {
                if (strlen(mk) != 4 || (mk[k] != '-' && mk[k] != 'C' && mk[k] != 'D')) { mask = -1; break; }
                mask |= (mk[k] != '-') << k;
                dmask |= (mk[k] == 'D') << k;
            }
            if (idx < 0 || is_diagonal(idx) || mask <= 0 || w < 1 || w > 1000000 || tid < 0 || tid > 100000 || ori < 0 || ori > 3) {
                set_err(err, "tilepack line %d: bad kit record", line);
                goto bad;
            }
            p->kit = xrealloc(p->kit, (p->nkit + 1) * sizeof *p->kit);
            pk = &p->kit[p->nkit++];
            pk->sig = idx; pk->mask = mask; pk->dmask = dmask; pk->w = w; pk->tid = tid; pk->orient = ori;
            pk->props = props; pk->has_door = has_door; pk->dx = dx; pk->dy = dy; pk->dz = dz; pk->bearing = br; pk->app = app;
        } else if (!strncmp(buf, "light ", 6)) {
            pack_light *pl;
            long tid, vv;
            q = buf + 6;
            if (uint_field(&q, 0, 100000, &tid)) {
                set_err(err, "tilepack line %d: bad light line", line);
                goto bad;
            }
            p->light = xrealloc(p->light, (p->nlight + 1) * sizeof *p->light);
            pl = &p->light[p->nlight];
            pl->tid = (int)tid;
            for (k = 0; k < 7; k++) {
                if (uint_field(&q, 0, 255, &vv)) {
                    set_err(err, "tilepack line %d: light needs a tile id and 7 values 0-255", line);
                    goto bad;
                }
                pl->v[k] = (uint8_t)vv;
            }
            if (*q) {
                set_err(err, "tilepack line %d: trailing text", line);
                goto bad;
            }
            p->nlight++;
        } else {
            set_err(err, "tilepack line %d: unknown record", line);
            goto bad;
        }
        if (p->nlight > 100000 || p->cell[0].n > 100000) {
            set_err(err, "tilepack: too many records");
            goto bad;
        }
    }
    if (!p->tileset[0]) {
        set_err(err, "tilepack: missing header");
        goto bad;
    }
    for (k = 0; k < 16; k++) {
        if (is_diagonal(k) && !p->cell[k].n)
            continue;
        if (!p->cell[k].n) {
            set_err(err, "tilepack has no tile for corner pattern %c%c%c%c", k & 8 ? 'F' : 'W', k & 4 ? 'F' : 'W',
                    k & 2 ? 'F' : 'W', k & 1 ? 'F' : 'W');
            goto bad;
        }
        for (i = 0; i < p->cell[k].n; i++) {
            size_t j;
            for (j = 0; j < p->nlight && p->light[j].tid != p->cell[k].t[i].tid; j++)
                ;
            if (j == p->nlight) {
                set_err(err, "tilepack: tile %d has no light record", p->cell[k].t[i].tid);
                goto bad;
            }
        }
    }
    for (i = 0; i < p->nkit; i++) {
        size_t j;
        for (j = 0; j < p->nlight && p->light[j].tid != p->kit[i].tid; j++)
            ;
        if (j == p->nlight) {
            set_err(err, "tilepack: kit tile %d has no light record", p->kit[i].tid);
            goto bad;
        }
    }
    return 0;
bad:
    tilepack_free(p);
    return -1;
}

void tilepack_free(tilepack *p)
{
    int k;
    for (k = 0; k < 16; k++)
        free(p->cell[k].t);
    free(p->kit);
    free(p->light);
    memset(p, 0, sizeof *p);
}

static const pack_light *light_for(const tilepack *p, int tid)
{
    size_t i;
    for (i = 0; i < p->nlight; i++)
        if (p->light[i].tid == tid)
            return &p->light[i];
    return NULL;
}

/* ---------- layout (ported from the original Python reference) ---------- */

static int cell_idx(const layout *l, int x, int y)
{
    return (l->lat[y + 1][x] == 'F') << 3 | (l->lat[y + 1][x + 1] == 'F') << 2 | (l->lat[y][x] == 'F') << 1 |
           (l->lat[y][x + 1] == 'F');
}

static void carve(layout *l, int x0, int y0, int x1, int y1)
{
    int x, y;
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++)
            l->lat[y][x] = 'F';
}

static int fix_diagonals(layout *l)
{
    int pass, x, y, k;
    for (pass = 0; pass < l->w * l->h; pass++) {
        int changed = 0;
        for (y = 0; y < l->h; y++)
            for (x = 0; x < l->w; x++) {
                int idx = cell_idx(l, x, y);
                if (!is_diagonal(idx))
                    continue;
                {
                    /* vertices in cell_sig order: NW NE SW SE */
                    const int vx[4] = {x, x + 1, x, x + 1}, vy[4] = {y + 1, y + 1, y, y};
                    int found = 0;
                    for (k = 0; k < 4; k++) {
                        if (idx >> (3 - k) & 1)
                            continue; /* floor vertex */
                        if (vx[k] > 0 && vx[k] < l->w && vy[k] > 0 && vy[k] < l->h) {
                            l->lat[vy[k]][vx[k]] = 'F';
                            found = 1;
                            break;
                        }
                    }
                    if (!found)
                        return 0;
                    changed = 1;
                }
            }
        if (!changed)
            return 1;
    }
    return 0;
}

static int full_floor(const layout *l, int x, int y) { return cell_idx(l, x, y) == 15; }
int layout_full_floor(const layout *l, int x, int y) { return full_floor(l, x, y); }

static int one_floor_component(const layout *l)
{
    static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
    int seen[RECIPE_AREA_MAX][RECIPE_AREA_MAX] = {{0}};
    int stack[RECIPE_AREA_MAX * RECIPE_AREA_MAX][2], sp = 0, total = 0, reached = 0, x, y, d;
    for (y = 0; y < l->h; y++)
        for (x = 0; x < l->w; x++)
            if (full_floor(l, x, y))
                total++;
    if (!total)
        return 0;
    for (y = 0; y < l->h && !sp; y++)
        for (x = 0; x < l->w && !sp; x++)
            if (full_floor(l, x, y)) {
                stack[sp][0] = x;
                stack[sp][1] = y;
                sp++;
                seen[y][x] = 1;
            }
    while (sp) {
        int cx = stack[--sp][0], cy = stack[sp][1];
        reached++;
        for (d = 0; d < 4; d++) {
            int nx = cx + dx[d], ny = cy + dy[d];
            if (nx < 0 || ny < 0 || nx >= l->w || ny >= l->h || seen[ny][nx] || !full_floor(l, nx, ny))
                continue;
            seen[ny][nx] = 1;
            stack[sp][0] = nx;
            stack[sp][1] = ny;
            sp++;
        }
    }
    return reached == total;
}

static int rng_range(sm64 *r, int lo, int hi) { return lo + (int)sm64_bounded(r, (uint64_t)(hi - lo + 1)); }

/* Random non-overlapping rooms (vertex rects), sorted by (x0, y0). `margin` is the clearance between two rooms' wall rings:
 * 1 leaves rooms 1 cell apart (wide corridors), 2 leaves a free cell between the rings (room for a kit corridor). */
static void place_rooms(sm64 *rng, int W, int H, int nrooms, int margin, layout *l)
{
    int t, i;
    for (t = 0; t < nrooms * 20; t++) {
        int rw, rh, cx, cy, clash = 0;
        if (l->nrooms == nrooms)
            break;
        rw = rng_range(rng, 2, 4);
        rh = rng_range(rng, 2, 4);
        if (rw > W - 2 || rh > H - 2)
            continue;
        cx = rng_range(rng, 1, W - 1 - rw);
        cy = rng_range(rng, 1, H - 1 - rh);
        for (i = 0; i < l->nrooms; i++) {
            const int *r = l->room[i];
            if (!(cx + rw + margin < r[0] || r[2] + margin < cx || cy + rh + margin < r[1] || r[3] + margin < cy))
                clash = 1;
        }
        if (clash)
            continue;
        l->room[l->nrooms][0] = cx;
        l->room[l->nrooms][1] = cy;
        l->room[l->nrooms][2] = cx + rw;
        l->room[l->nrooms][3] = cy + rh;
        l->nrooms++;
    }
    if (l->nrooms < nrooms)
        return;
    for (i = 1; i < l->nrooms; i++) { /* stable insertion sort by (x0, y0) */
        int tmp[4], j = i;
        memcpy(tmp, l->room[i], sizeof tmp);
        while (j > 0 && (l->room[j - 1][0] > tmp[0] || (l->room[j - 1][0] == tmp[0] && l->room[j - 1][1] > tmp[1]))) {
            memcpy(l->room[j], l->room[j - 1], sizeof tmp);
            j--;
        }
        memcpy(l->room[j], tmp, sizeof tmp);
    }
}

int layout_make(sm64 *rng, int W, int H, int nrooms, layout *l, char *err)
{
    int attempt;
    if (W < RECIPE_AREA_MIN || H < RECIPE_AREA_MIN || W > RECIPE_AREA_MAX || H > RECIPE_AREA_MAX || nrooms < 1 ||
        nrooms > RECIPE_ROOMS_MAX) {
        set_err(err, "layout: unsupported size or room count");
        return -1;
    }
    for (attempt = 0; attempt < LAYOUT_ATTEMPTS; attempt++) {
        int x, y, i, ok = 1;
        memset(l, 0, sizeof *l);
        l->w = W;
        l->h = H;
        for (y = 0; y <= H; y++)
            memset(l->lat[y], 'W', (size_t)W + 1);
        place_rooms(rng, W, H, nrooms, 1, l);
        if (l->nrooms < nrooms)
            continue;
        for (i = 0; i < l->nrooms; i++)
            carve(l, l->room[i][0], l->room[i][1], l->room[i][2], l->room[i][3]);
        for (i = 0; i + 1 < l->nrooms; i++) {
            const int *a = l->room[i], *b = l->room[i + 1];
            int ax = (a[0] + a[2]) / 2, ay = (a[1] + a[3]) / 2, bx = (b[0] + b[2]) / 2, by = (b[1] + b[3]) / 2;
            ax = ax < 1 ? 1 : ax > W - 2 ? W - 2 : ax;
            bx = bx < 1 ? 1 : bx > W - 2 ? W - 2 : bx;
            ay = ay < 1 ? 1 : ay > H - 2 ? H - 2 : ay;
            by = by < 1 ? 1 : by > H - 2 ? H - 2 : by;
            carve(l, ax < bx ? ax : bx, ay, (ax > bx ? ax : bx) + 1, ay + 1);
            carve(l, bx, ay < by ? ay : by, bx + 1, (ay > by ? ay : by) + 1);
        }
        if (!fix_diagonals(l))
            continue;
        for (x = 0; x <= W && ok; x++)
            if (l->lat[0][x] == 'F' || l->lat[H][x] == 'F')
                ok = 0;
        for (y = 0; y <= H && ok; y++)
            if (l->lat[y][0] == 'F' || l->lat[y][W] == 'F')
                ok = 0;
        for (y = 0; y < H && ok; y++)
            for (x = 0; x < W && ok; x++)
                if (is_diagonal(cell_idx(l, x, y)))
                    ok = 0;
        if (!ok || !one_floor_component(l))
            continue;
        l->attempts = attempt + 1;
        return 0;
    }
    set_err(err, "could not generate a %dx%d layout with %d rooms (area too small for that many rooms)", W, H, nrooms);
    return -1;
}

/* ---------- corridor-kit layout ---------- */

enum { KT = 1, KR = 2, KB = 4, KL = 8 };
/* directions in a fixed order: east, north, west, south; bit of the cell's own edge, and of the neighbour's edge facing back */
static const int KDX[4] = {1, 0, -1, 0}, KDY[4] = {0, 1, 0, -1};
static const int KBIT[4] = {KR, KT, KL, KB}, KOPP[4] = {KL, KB, KR, KT};

typedef char kit_grid[RECIPE_AREA_MAX][RECIPE_AREA_MAX];

/* Shortest 4-connected path from (sx,sy) to (ex,ey) over unblocked cells; out gets the cells start..end. Returns the length, 0 if none or
 * longer than `max`. Neighbour order is fixed, so the result is deterministic. */
static int kit_bfs(int W, int H, kit_grid blocked, int sx, int sy, int ex, int ey, unsigned char (*out)[2], int max)
{
    signed char from[RECIPE_AREA_MAX][RECIPE_AREA_MAX];
    int qx[RECIPE_AREA_MAX * RECIPE_AREA_MAX], qy[RECIPE_AREA_MAX * RECIPE_AREA_MAX], qh = 0, qt = 0, x, y, d, n = 0;

    if (sx < 0 || sy < 0 || ex < 0 || ey < 0 || sx >= W || sy >= H || ex >= W || ey >= H || blocked[sy][sx] || blocked[ey][ex])
        return 0;
    memset(from, -1, sizeof from);
    from[sy][sx] = 4;
    qx[qt] = sx;
    qy[qt++] = sy;
    while (qh < qt && from[ey][ex] < 0) {
        int cx = qx[qh], cy = qy[qh++];
        for (d = 0; d < 4; d++) {
            int nx = cx + KDX[d], ny = cy + KDY[d];
            if (nx < 0 || ny < 0 || nx >= W || ny >= H || blocked[ny][nx] || from[ny][nx] >= 0)
                continue;
            from[ny][nx] = (signed char)d;
            qx[qt] = nx;
            qy[qt++] = ny;
        }
    }
    if (from[ey][ex] < 0)
        return 0;
    for (x = ex, y = ey; from[y][x] != 4; ) { /* walk back, counting */
        d = from[y][x];
        x -= KDX[d];
        y -= KDY[d];
        n++;
    }
    n++;
    if (n > max)
        return 0;
    for (x = ex, y = ey, d = n - 1; d >= 0; d--) {
        out[d][0] = (unsigned char)x;
        out[d][1] = (unsigned char)y;
        if (from[y][x] != 4) {
            int k = from[y][x];
            x -= KDX[k];
            y -= KDY[k];
        }
    }
    return n;
}

/* The wall-ring cell on `side` of room r (vertex rect) at offset t along that side, and the free cell just beyond it. */
static void kit_connector(const int *r, int side, int t, int c[2], int s[2])
{
    switch (side) {
    case 0: c[0] = r[2];     c[1] = r[1] + t; s[0] = r[2] + 1; s[1] = c[1]; break; /* east */
    case 1: c[0] = r[0] + t; c[1] = r[3];     s[0] = c[0];     s[1] = r[3] + 1; break; /* north */
    case 2: c[0] = r[0] - 1; c[1] = r[1] + t; s[0] = r[0] - 2; s[1] = c[1]; break; /* west */
    default: c[0] = r[0] + t; c[1] = r[1] - 1; s[0] = c[0];    s[1] = r[1] - 2; break; /* south */
    }
}

int layout_make_kit(sm64 *rng, int W, int H, int nrooms, layout *l, char *err)
{
    int attempt;
    if (W < RECIPE_AREA_MIN || H < RECIPE_AREA_MIN || W > RECIPE_AREA_MAX || H > RECIPE_AREA_MAX || nrooms < 1 ||
        nrooms > RECIPE_ROOMS_MAX) {
        set_err(err, "layout: unsupported size or room count");
        return -1;
    }
    for (attempt = 0; attempt < LAYOUT_ATTEMPTS; attempt++) {
        int x, y, i, ok = 1;
        kit_grid blocked, conn;
        memset(l, 0, sizeof *l);
        l->w = W;
        l->h = H;
        l->kit = 1;
        for (y = 0; y <= H; y++)
            memset(l->lat[y], 'W', (size_t)W + 1);
        place_rooms(rng, W, H, nrooms, 2, l);
        if (l->nrooms < nrooms)
            continue;
        memset(blocked, 0, sizeof blocked);
        memset(conn, 0, sizeof conn);
        for (i = 0; i < l->nrooms; i++) {
            const int *r = l->room[i];
            carve(l, r[0], r[1], r[2], r[3]);
            for (y = r[1] - 1; y <= r[3]; y++) /* the room's wall ring: every cell that touches a floor vertex */
                for (x = r[0] - 1; x <= r[2]; x++)
                    blocked[y][x] = 1;
        }
        for (i = 0; i + 1 < l->nrooms && ok; i++) {
            const int *a = l->room[i], *b = l->room[i + 1];
            int sa, sb, ca[2], sa_c[2], cb[2], sb_c[2], lenA, lenB, n, k;
            if (b[0] >= a[2] + 3) { sa = 0; sb = 2; }
            else if (b[1] >= a[3] + 3) { sa = 1; sb = 3; }
            else if (a[1] >= b[3] + 3) { sa = 3; sb = 1; }
            else if (a[0] >= b[2] + 3) { sa = 2; sb = 0; }
            else { ok = 0; break; }
            lenA = (sa & 1) ? a[2] - a[0] : a[3] - a[1];
            lenB = (sb & 1) ? b[2] - b[0] : b[3] - b[1];
            kit_connector(a, sa, (int)sm64_bounded(rng, (uint64_t)lenA), ca, sa_c);
            kit_connector(b, sb, (int)sm64_bounded(rng, (uint64_t)lenB), cb, sb_c);
            if (sa_c[0] < 0 || sa_c[1] < 0 || sb_c[0] < 0 || sb_c[1] < 0 || sa_c[0] >= W || sa_c[1] >= H || sb_c[0] >= W || sb_c[1] >= H ||
                conn[ca[1]][ca[0]] || conn[cb[1]][cb[0]]) {
                ok = 0;
                break;
            }
            n = kit_bfs(W, H, blocked, sa_c[0], sa_c[1], sb_c[0], sb_c[1], l->link[i], LINK_MAX);
            if (!n) { ok = 0; break; }
            l->link_n[i] = n;
            l->nlink = i + 1;
            conn[ca[1]][ca[0]] = conn[cb[1]][cb[0]] = 1;
            l->conn[i][0][0] = (unsigned char)ca[0]; l->conn[i][0][1] = (unsigned char)ca[1]; l->conn[i][0][2] = (unsigned char)sa;
            l->conn[i][1][0] = (unsigned char)cb[0]; l->conn[i][1][1] = (unsigned char)cb[1]; l->conn[i][1][2] = (unsigned char)sb;
            l->kedge[ca[1]][ca[0]] |= (unsigned char)KBIT[sa];
            l->kedge[sa_c[1]][sa_c[0]] |= (unsigned char)KOPP[sa];
            l->kedge[cb[1]][cb[0]] |= (unsigned char)KBIT[sb];
            l->kedge[sb_c[1]][sb_c[0]] |= (unsigned char)KOPP[sb];
            for (k = 0; k + 1 < n; k++) {
                int dx = (int)l->link[i][k + 1][0] - (int)l->link[i][k][0], dy = (int)l->link[i][k + 1][1] - (int)l->link[i][k][1], d;
                for (d = 0; d < 4 && !(KDX[d] == dx && KDY[d] == dy); d++)
                    ;
                l->kedge[l->link[i][k][1]][l->link[i][k][0]] |= (unsigned char)KBIT[d];
                l->kedge[l->link[i][k + 1][1]][l->link[i][k + 1][0]] |= (unsigned char)KOPP[d];
            }
            for (k = 0; k < n; k++)
                blocked[l->link[i][k][1]][l->link[i][k][0]] = 1;
        }
        if (!ok)
            continue;
        for (x = 0; x <= W && ok; x++)
            if (l->lat[0][x] == 'F' || l->lat[H][x] == 'F')
                ok = 0;
        for (y = 0; y <= H && ok; y++)
            if (l->lat[y][0] == 'F' || l->lat[y][W] == 'F')
                ok = 0;
        if (!ok)
            continue;
        l->attempts = attempt + 1;
        return 0;
    }
    set_err(err, "could not generate a %dx%d kit layout with %d rooms (kit corridors need more room between rooms; use a larger area or fewer rooms)", W, H, nrooms);
    return -1;
}

int layout_portal_pair(const layout *l, int room, sm64 *rng, int out[4], char *err)
{
    static const int dx[4] = {1, 0, -1, 0}, dy[4] = {0, 1, 0, -1};
    const int *r = l->room[room];
    int (*pairs)[4], np = 0, x, y, d, k;
    char in[RECIPE_AREA_MAX][RECIPE_AREA_MAX] = {{0}};

    for (y = r[1]; y < r[3]; y++)
        for (x = r[0]; x < r[2]; x++)
            in[y][x] = (char)full_floor(l, x, y);
    pairs = xmalloc((size_t)(r[2] - r[0]) * (size_t)(r[3] - r[1]) * 4 * sizeof *pairs);
    /* Python sorts room cells by (x, y); pair order follows that order. */
    for (x = r[0]; x < r[2]; x++)
        for (y = r[1]; y < r[3]; y++) {
            if (!in[y][x])
                continue;
            for (d = 0; d < 4; d++) {
                int nx = x + dx[d], ny = y + dy[d];
                if (nx < r[0] || nx >= r[2] || ny < r[1] || ny >= r[3] || !in[ny][nx])
                    continue;
                pairs[np][0] = x;
                pairs[np][1] = y;
                pairs[np][2] = nx;
                pairs[np][3] = ny;
                np++;
            }
        }
    if (!np) {
        free(pairs);
        set_err(err, "room has no adjacent full-floor cell pair");
        return -1;
    }
    k = (int)sm64_bounded(rng, (uint64_t)np);
    memcpy(out, pairs[k], 4 * sizeof(int));
    free(pairs);
    return 0;
}

/* ---------- seeds ---------- */

static void be64(uint8_t *p, uint64_t v)
{
    int i;
    for (i = 0; i < 8; i++)
        p[i] = (uint8_t)(v >> (56 - 8 * i));
}

static void domain_hash(const char *tag, size_t taglen, uint64_t seed, const char *name, uint8_t out[32])
{
    sha256_ctx c;
    uint8_t s[8];
    be64(s, seed);
    sha256_init(&c);
    sha256_update(&c, tag, taglen);
    sha256_update(&c, s, 8);
    sha256_update(&c, name, strlen(name));
    sha256_final(&c, out);
}

uint64_t dungeon_area_seed(uint64_t seed, const char *resref)
{
    uint8_t d[32];
    uint64_t v = 0;
    int i;
    domain_hash("nwndgen/layout/1", 17, seed, resref, d); /* includes the NUL */
    for (i = 0; i < 8; i++)
        v = v << 8 | d[i];
    return v;
}

void dungeon_module_id(uint64_t seed, const char *tag, uint8_t out[16])
{
    uint8_t d[32];
    domain_hash("nwndgen/modid/1", 16, seed, tag, d);
    memcpy(out, d, 16);
}

/* ---------- GFF assembly ---------- */

#define TRY(x) do { if ((x) != 0) return -1; } while (0)

static const char *const GIT_LISTS[9] = {"Creature List", "Door List", "Encounter List", "List", "Placeable List",
                                          "SoundList", "StoreList", "TriggerList", "WaypointList"};

static int add_int(gff_struct *s, const char *l, gff_type t, int64_t v, char *err) { return gff_set_int(s, l, t, v, err); }

static int build_are(const gff *tpl, const tilepack *tp, const recipe_area *a, const layout *lay,
                     const placed_tile *tiles, const char *on_enter, const char *on_beat, const roster_theme *amb, gff *out, char *err)
{
    gff_struct *s = gff_struct_clone(tpl->root);
    gff_field *tl;
    char tag[32];
    int i, n = lay->w * lay->h;
    size_t k;

    memset(out, 0, sizeof *out);
    memcpy(out->type, tpl->type, sizeof out->type);
    out->root = s;
    for (k = 0; a->resref[k]; k++)
        tag[k] = (char)(a->resref[k] >= 'a' && a->resref[k] <= 'z' ? a->resref[k] - 32 : a->resref[k]);
    tag[k] = '\0';
    TRY(add_int(s, "Width", GFF_INT, lay->w, err));
    TRY(add_int(s, "Height", GFF_INT, lay->h, err));
    TRY(gff_set_string(s, "ResRef", GFF_RESREF, a->resref, err));
    TRY(gff_set_string(s, "Tag", GFF_STRING, tag, err));
    TRY(gff_set_locstr(s, "Name", a->name, err));
    TRY(gff_set_string(s, "Comments", GFF_STRING, "generated by nwndgen", err));
    TRY(add_int(s, "ID", GFF_INT, -1, err));
    TRY(add_int(s, "Creator_ID", GFF_INT, -1, err));
    TRY(add_int(s, "Version", GFF_DWORD, 18, err));
    TRY(gff_set_string(s, "Tileset", GFF_RESREF, tp->tileset, err));
    TRY(gff_set_string(s, "OnEnter", GFF_RESREF, on_enter, err));
    TRY(gff_set_string(s, "OnExit", GFF_RESREF, "", err));
    TRY(gff_set_string(s, "OnHeartbeat", GFF_RESREF, on_beat, err));
    TRY(gff_set_string(s, "OnUserDefined", GFF_RESREF, "", err));
    if (amb && amb->has_ambience) { /* theme lighting; the donor ARE is a permanent-night area, so only the Moon values show */
        TRY(gff_set_int(s, "MoonAmbientColor", GFF_DWORD, amb->amb_ambient, err));
        TRY(gff_set_int(s, "MoonDiffuseColor", GFF_DWORD, amb->amb_diffuse, err));
        TRY(gff_set_int(s, "MoonFogColor", GFF_DWORD, amb->amb_fog, err));
        TRY(gff_set_int(s, "MoonFogAmount", GFF_BYTE, amb->amb_fog_amount, err));
        TRY(gff_set_float(s, "FogClipDist", (float)amb->amb_fog_clip, err));
    }
    tl = gff_set_list(s, "Tile_List", err);
    if (!tl)
        return -1;
    for (i = 0; i < n; i++) {
        const pack_light *lt = light_for(tp, tiles[i].tid);
        gff_struct *t = gff_struct_new(1);
        gff_list_append(tl, t);
        if (!lt) {
            set_err(err, "tile %d has no light record", tiles[i].tid);
            return -1;
        }
        TRY(add_int(t, "Tile_ID", GFF_INT, tiles[i].tid, err));
        TRY(add_int(t, "Tile_Orientation", GFF_INT, tiles[i].orient, err));
        TRY(add_int(t, "Tile_Height", GFF_INT, 0, err));
        TRY(add_int(t, "Tile_MainLight1", GFF_BYTE, lt->v[0], err));
        TRY(add_int(t, "Tile_MainLight2", GFF_BYTE, lt->v[1], err));
        TRY(add_int(t, "Tile_SrcLight1", GFF_BYTE, lt->v[2], err));
        TRY(add_int(t, "Tile_SrcLight2", GFF_BYTE, lt->v[3], err));
        TRY(add_int(t, "Tile_AnimLoop1", GFF_BYTE, lt->v[4], err));
        TRY(add_int(t, "Tile_AnimLoop2", GFF_BYTE, lt->v[5], err));
        TRY(add_int(t, "Tile_AnimLoop3", GFF_BYTE, lt->v[6], err));
    }
    return 0;
}

/* One local variable in an object's VarTable (struct id 0: Name, Type 1 = int / 3 = string, Value), the form the toolset writes. */
static int add_var(gff_struct *obj, const char *name, int is_string, const char *sval, int ival, char *err)
{
    gff_field *vt = gff_find(obj, "VarTable");
    gff_struct *v;
    if (!vt && !(vt = gff_set_list(obj, "VarTable", err)))
        return -1;
    v = gff_struct_new(0);
    gff_list_append(vt, v);
    if (gff_set_string(v, "Name", GFF_STRING, name, err) || gff_set_int(v, "Type", GFF_DWORD, is_string ? 3 : 1, err))
        return -1;
    return is_string ? gff_set_string(v, "Value", GFF_STRING, sval, err) : gff_set_int(v, "Value", GFF_INT, ival, err);
}

static gff_struct *make_waypoint(const char *tag, const char *name, double x, double y, double dx, double dy, char *err)
{
    gff_struct *w = gff_struct_new(5);
    if (add_int(w, "Appearance", GFF_BYTE, 1, err) || gff_set_locstr(w, "Description", NULL, err) ||
        add_int(w, "HasMapNote", GFF_BYTE, 0, err) || gff_set_string(w, "LinkedTo", GFF_STRING, "", err) ||
        gff_set_locstr(w, "LocalizedName", name, err) || gff_set_locstr(w, "MapNote", NULL, err) ||
        add_int(w, "MapNoteEnabled", GFF_BYTE, 1, err) || gff_set_string(w, "Tag", GFF_STRING, tag, err) ||
        gff_set_string(w, "TemplateResRef", GFF_RESREF, "nw_waypoint001", err) ||
        gff_set_float(w, "XOrientation", (float)dx, err) || gff_set_float(w, "YOrientation", (float)dy, err) ||
        gff_set_float(w, "XPosition", (float)x, err) || gff_set_float(w, "YPosition", (float)y, err) ||
        gff_set_float(w, "ZPosition", 0.0f, err)) {
        gff_struct_free(w);
        return NULL;
    }
    return w;
}

static gff_struct *make_transition(const gff_struct *donor, const char *tag, const char *linked, const char *name,
                                   double x, double y, char *err)
{
#define HALF 3.0
    static const double pts[4][2] = {{-HALF, -HALF}, {HALF, -HALF}, {HALF, HALF}, {-HALF, HALF}};
    gff_struct *t = gff_struct_clone(donor);
    gff_field *g;
    int i;
    if (gff_set_string(t, "Tag", GFF_STRING, tag, err) || gff_set_string(t, "LinkedTo", GFF_STRING, linked, err) ||
        gff_set_int(t, "LinkedToFlags", GFF_BYTE, 2, err) || gff_set_locstr(t, "LocalizedName", name, err) ||
        gff_set_float(t, "XPosition", (float)x, err) || gff_set_float(t, "YPosition", (float)y, err) ||
        gff_set_float(t, "ZPosition", 0.0f, err) || !(g = gff_set_list(t, "Geometry", err))) {
        gff_struct_free(t);
        return NULL;
    }
    for (i = 0; i < 4; i++) {
        gff_struct *p = gff_struct_new(3);
        gff_list_append(g, p);
        if (gff_set_float(p, "PointX", (float)pts[i][0], err) || gff_set_float(p, "PointY", (float)pts[i][1], err) ||
            gff_set_float(p, "PointZ", 0.0f, err)) {
            gff_struct_free(t);
            return NULL;
        }
    }
    return t;
}

static double cc(int c) { return c * 10.0 + 5.0; }

static void direction(const int *from, const int *to, double *dx, double *dy)
{
    double x = cc(to[0]) - cc(from[0]), y = cc(to[1]) - cc(from[1]), n = sqrt(x * x + y * y);
    *dx = x / n;
    *dy = y / n;
}

static void upper(char *dst, const char *src)
{
    size_t i;
    for (i = 0; src[i]; i++)
        dst[i] = (char)(src[i] >= 'a' && src[i] <= 'z' ? src[i] - 32 : src[i]);
    dst[i] = '\0';
}

/* A locked door (recipe key locks): lockable, locked, pick DC; keyed doors also name the key's tag. Never plot, so it can always be bashed. */
static int lock_door(gff_struct *dr, const dungeon_door *dd, char *err)
{
    return gff_set_int(dr, "Locked", GFF_BYTE, 1, err) || gff_set_int(dr, "Lockable", GFF_BYTE, 1, err) ||
           gff_set_int(dr, "OpenLockDC", GFF_BYTE, dd->dc, err) || gff_set_int(dr, "CloseLockDC", GFF_BYTE, dd->dc, err) ||
           gff_set_int(dr, "KeyRequired", GFF_BYTE, dd->lock == 2, err) || gff_set_int(dr, "AutoRemoveKey", GFF_BYTE, 0, err) ||
           gff_set_string(dr, "KeyName", GFF_STRING, dd->lock == 2 ? dd->keytag : "", err);
}

static int build_git_gic(const gff *tpl, const dungeon *d, size_t i, const recipe *r, gff *git, gff *gic, char *err)
{
    const dungeon_area *a = &d->area[i];
    const gff_field *tt = gff_find(tpl->root, "TriggerList");
    gff_struct *root = gff_struct_clone(tpl->root), *donor;
    gff_field *wl, *tl;
    char U[17], tag[48], link[48], nm[160], other[17];
    double dx, dy;
    int k;
    size_t m;

    if (!tt || tt->type != GFF_LIST || !tt->nlist) {
        set_err(err, "template GIT has no transition trigger to clone");
        gff_struct_free(root);
        return -1;
    }
    donor = gff_struct_clone(tt->list[0]);
    memset(git, 0, sizeof *git);
    memcpy(git->type, "GIT ", 5);
    git->root = root;
    for (k = 0; k < 9; k++) {
        if (!gff_set_list(root, GIT_LISTS[k], err)) {
            gff_struct_free(donor);
            return -1;
        }
    }
    wl = gff_find(root, "WaypointList");
    tl = gff_find(root, "TriggerList");
    upper(U, a->resref);
    if (a->has_prev) {
        gff_struct *w;
        gff_struct *t;
        direction(a->pair_prev, a->pair_prev + 2, &dx, &dy);
        snprintf(tag, sizeof tag, "WP_%s_FWD", U);
        snprintf(nm, sizeof nm, "Arrive %s", U);
        w = make_waypoint(tag, nm, cc(a->pair_prev[2]), cc(a->pair_prev[3]), dx, dy, err);
        if (!w) { gff_struct_free(donor); return -1; }
        gff_list_append(wl, w);
        upper(other, d->area[i - 1].resref);
        snprintf(tag, sizeof tag, "TR_%s_PREV", U);
        snprintf(link, sizeof link, "WP_%s_BACK", other);
        t = make_transition(donor, tag, link, "Back up", cc(a->pair_prev[0]), cc(a->pair_prev[1]), err);
        if (!t) { gff_struct_free(donor); return -1; }
        gff_list_append(tl, t);
    }
    if (a->has_next) {
        gff_struct *w;
        gff_struct *t;
        direction(a->pair_next, a->pair_next + 2, &dx, &dy);
        snprintf(tag, sizeof tag, "WP_%s_BACK", U);
        snprintf(nm, sizeof nm, "Back in %s", U);
        w = make_waypoint(tag, nm, cc(a->pair_next[2]), cc(a->pair_next[3]), dx, dy, err);
        if (!w) { gff_struct_free(donor); return -1; }
        gff_list_append(wl, w);
        upper(other, d->area[i + 1].resref);
        snprintf(tag, sizeof tag, "TR_%s_NEXT", U);
        snprintf(link, sizeof link, "WP_%s_FWD", other);
        t = make_transition(donor, tag, link, "Deeper", cc(a->pair_next[0]), cc(a->pair_next[1]), err);
        if (!t) { gff_struct_free(donor); return -1; }
        gff_list_append(tl, t);
    }
    gff_struct_free(donor);
    (void)r;

    /* Spawn markers: tag = "SP<kind>_<blueprint>", read by scel_enter.nss. Facing is one of 8 exact
     * compass vectors so the bytes do not depend on libm. */
    for (m = 0; m < a->nsp; m++) {
        static const double fx[8] = {1, 0.70710678118654752, 0, -0.70710678118654752, -1, -0.70710678118654752, 0, 0.70710678118654752};
        static const double fy[8] = {0, 0.70710678118654752, 1, 0.70710678118654752, 0, -0.70710678118654752, -1, -0.70710678118654752};
        const spawn_pt *p = &a->sp[m];
        gff_struct *w;
        snprintf(tag, sizeof tag, "SP%c_%s", p->kind, p->bp);
        w = make_waypoint(tag, "Spawn point", cc(p->cx) + (p->custom ? p->xcm / 100.0 : 3.5 * p->ox), cc(p->cy) + (p->custom ? p->ycm / 100.0 : 3.5 * p->oy), fx[p->face & 7], fy[p->face & 7], err);
        if (!w)
            return -1;
        if ((p->loot[0][0] && add_var(w, "scel_loot", 1, p->loot[0], 0, err)) || (p->loot[1][0] && add_var(w, "scel_loot2", 1, p->loot[1], 0, err)) ||
            (p->gold && add_var(w, "scel_gold", 0, NULL, p->gold, err)) ||
            (p->weak && add_var(w, "scel_weak", 0, NULL, 1, err)) ||
            (p->keytag[0] && (add_var(w, "scel_key", 1, p->keytag, 0, err) || add_var(w, "scel_keybp", 1, p->keybp, 0, err))) || (p->lockdc && add_var(w, "scel_lockdc", 0, NULL, p->lockdc, err)) || (p->amb && add_var(w, "scel_amb", 0, NULL, p->amb, err)) ||
            ((p->kind == 'P' || p->trapdc) && add_var(w, "scel_trap", 0, NULL, p->trap, err)) || (p->trapdc && add_var(w, "scel_trapdc", 0, NULL, p->trapdc, err))) {
            gff_struct_free(w);
            return -1;
        }
        gff_list_append(wl, w);
    }

    /* Doors: clones of the stock corridor door from the template, one per door tile, closed and unlocked as the donor is. */
    if (a->ndoor) {
        const gff_field *dd = gff_find(tpl->root, "Door List");
        gff_field *dl = gff_find(root, "Door List");
        if (!dd || dd->type != GFF_LIST || !dd->nlist || !dl) {
            set_err(err, "template GIT has no door to clone (recipe needs template_door for doors plain)");
            return -1;
        }
        for (m = 0; m < a->ndoor; m++) {
            if ((size_t)a->door[m].donor >= dd->nlist) {
                set_err(err, "template GIT has no door donor %d", a->door[m].donor);
                return -1;
            }
            gff_struct *dr = gff_struct_clone(dd->list[a->door[m].donor]);
            snprintf(tag, sizeof tag, "DOOR_%s_%zu", U, m + 1);
            if (gff_set_string(dr, "Tag", GFF_STRING, tag, err) || gff_set_float(dr, "X", (float)a->door[m].x, err) ||
                gff_set_float(dr, "Y", (float)a->door[m].y, err) || gff_set_float(dr, "Z", (float)a->door[m].z, err) ||
                gff_set_float(dr, "Bearing", (float)a->door[m].bearing, err) || gff_set_int(dr, "Locked", GFF_BYTE, 0, err) ||
                gff_set_int(dr, "Appearance", gff_find(dr, "Appearance") ? gff_find(dr, "Appearance")->type : GFF_DWORD, a->door[m].app, err) ||
                gff_set_int(dr, "Plot", GFF_BYTE, 0, err) || gff_set_int(dr, "AnimationState", GFF_BYTE, 0, err) ||
                (a->door[m].lock && lock_door(dr, &a->door[m], err))) {
                gff_struct_free(dr);
                return -1;
            }
            gff_list_append(dl, dr);
        }
    }

    /* GIC mirrors every GIT list in length and struct id, one Comment per object. */
    memset(gic, 0, sizeof *gic);
    memcpy(gic->type, "GIC ", 5);
    gic->root = gff_struct_new(0xFFFFFFFFu);
    for (k = 0; k < 9; k++) {
        const gff_field *src = gff_find(root, GIT_LISTS[k]);
        gff_field *dst = gff_set_list(gic->root, GIT_LISTS[k], err);
        if (!dst)
            return -1;
        for (m = 0; m < src->nlist; m++) {
            gff_struct *c = gff_struct_new(src->list[m]->id);
            gff_list_append(dst, c);
            TRY(gff_set_string(c, "Comment", GFF_STRING, "", err));
        }
    }
    return 0;
}

static int build_ifo(const gff *in, const recipe *r, const dungeon *d, uint64_t seed, uint8_t **buf, size_t *len,
                     char *err)
{
    gff g;
    gff_struct *s;
    gff_field *al;
    uint8_t id[16];
    const dungeon_area *a0 = &d->area[0];
    double dx, dy;
    size_t i;
    int rc;

    if (d->has_spawn) {
        const char *cur = gff_get_string(in->root, "Mod_OnAcquirItem");
        if (cur && *cur && strcmp(cur, "scel_acquire")) {
            set_err(err, "base module already handles Mod_OnAcquirItem ('%s'); the objective needs that hook", cur);
            return -1;
        }
    }
    memset(&g, 0, sizeof g);
    memcpy(g.type, in->type, sizeof g.type);
    g.root = s = gff_struct_clone(in->root);
    TRY(gff_set_locstr(s, "Mod_Name", r->module_name, err));
    TRY(gff_set_locstr(s, "Mod_Description", r->module_desc, err));
    TRY(gff_set_string(s, "Mod_Tag", GFF_STRING, r->module_tag, err));
    dungeon_module_id(seed, r->module_tag, id);
    TRY(gff_set_void(s, "Mod_ID", id, 16, err));
    if (d->has_spawn)
        TRY(gff_set_string(s, "Mod_OnAcquirItem", GFF_RESREF, "scel_acquire", err));
    al = gff_set_list(s, "Mod_Area_list", err);
    if (!al)
        return -1;
    for (i = 0; i < d->n; i++) {
        gff_struct *e = gff_struct_new(6);
        gff_list_append(al, e);
        TRY(gff_set_string(e, "Area_Name", GFF_RESREF, d->area[i].resref, err));
    }
    if (d->nhench) { /* the area script hands these to the first player */
        TRY(add_var(s, "scel_hn", 0, NULL, d->nhench, err));
        for (i = 0; i < (size_t)d->nhench; i++) {
            char nm[24];
            snprintf(nm, sizeof nm, "scel_h%d", (int)i + 1);
            TRY(add_var(s, nm, 1, d->hench[i], 0, err));
        }
    }
    TRY(gff_set_string(s, "Mod_Entry_Area", GFF_RESREF, a0->resref, err));
    direction(a0->pair_start, a0->pair_start + 2, &dx, &dy);
    TRY(gff_set_float(s, "Mod_Entry_X", (float)cc(a0->pair_start[0]), err));
    TRY(gff_set_float(s, "Mod_Entry_Y", (float)cc(a0->pair_start[1]), err));
    TRY(gff_set_float(s, "Mod_Entry_Z", 0.0f, err));
    TRY(gff_set_float(s, "Mod_Entry_Dir_X", (float)dx, err));
    TRY(gff_set_float(s, "Mod_Entry_Dir_Y", (float)dy, err));
    rc = gff_write(&g, buf, len, err);
    gff_free(&g);
    return rc;
}

static int write_gff(gff *g, uint8_t **buf, size_t *len, char *err)
{
    int rc = gff_write(g, buf, len, err);
    gff_free(g);
    return rc;
}

void dungeon_free(dungeon *d)
{
    size_t i;
    for (i = 0; i < d->n; i++) {
        free(d->area[i].tiles);
        free(d->area[i].sp);
        free(d->area[i].door);
        free(d->area[i].are);
        free(d->area[i].git);
        free(d->area[i].gic);
    }
    free(d->area);
    free(d->ifo);
    free(d->jrl);
    memset(d, 0, sizeof *d);
}

/* One closed door per kit corridor: a straight corridor cell that is not beside a room, swapped for the door variant of its tile.
 * Own RNG domain, so switching doors on or off leaves every other tile exactly as it was. */
static int place_doors(const tilepack *tp, uint64_t seed, const char *resref, int mixed, dungeon_area *a, char *err)
{
    const layout *l = &a->lay;
    uint8_t h[32];
    uint64_t v = 0;
    sm64 drng;
    int i, k;

    domain_hash("nwndgen/doors/1", 16, seed, resref, h);
    for (k = 0; k < 8; k++)
        v = v << 8 | h[k];
    sm64_seed(&drng, v);
    for (i = 0; i < l->nlink; i++) {
        int cand[LINK_MAX], nc = 0, pick, cx, cy, mask, look;
        size_t q, total = 0, j;
        uint64_t rr;
        for (k = 1; k + 1 < l->link_n[i]; k++) {
            mask = l->kedge[l->link[i][k][1]][l->link[i][k][0]];
            if (mask == (KT | KB) || mask == (KL | KR))
                cand[nc++] = k;
        }
        if (!nc)
            continue; /* a corridor of one or two cells, or only bends */
        pick = cand[sm64_bounded(&drng, (uint64_t)nc)];
        cx = l->link[i][pick][0];
        cy = l->link[i][pick][1];
        mask = l->kedge[cy][cx];
        look = 22; /* doors plain: the stock plain look only, whatever else the pack holds */
        if (mixed) { /* doors mixed: every look the pack has for this corridor is equally likely, then the tile as usual */
            int looks[8], nl = 0, lk;
            for (q = 0; q < tp->nkit; q++) {
                int f = 0;
                if (!tp->kit[q].has_door || tp->kit[q].dmask || tp->kit[q].sig != 0 || tp->kit[q].mask != mask)
                    continue;
                for (lk = 0; lk < nl; lk++)
                    f |= looks[lk] == tp->kit[q].app;
                if (!f && nl < 8)
                    looks[nl++] = tp->kit[q].app;
            }
            for (lk = 1; lk < nl; lk++) { /* ascending, so the draw does not depend on record order */
                int v2 = looks[lk], m2 = lk - 1;
                while (m2 >= 0 && looks[m2] > v2) { looks[m2 + 1] = looks[m2]; m2--; }
                looks[m2 + 1] = v2;
            }
            if (nl)
                look = looks[nl > 1 ? (int)sm64_bounded(&drng, (uint64_t)nl) : 0];
        }
        for (q = 0; q < tp->nkit; q++)
            if (tp->kit[q].has_door && !tp->kit[q].dmask && tp->kit[q].sig == 0 && tp->kit[q].mask == mask && tp->kit[q].app == look)
                total += tp->kit[q].w;
        if (!total) {
            set_err(err, "the tilepack has no door tile for a corridor with edges 0x%x (build it with nwnpack tilepack-kit)", mask);
            return -1;
        }
        rr = sm64_bounded(&drng, total);
        for (q = 0; q < tp->nkit; q++) {
            if (!tp->kit[q].has_door || tp->kit[q].dmask || tp->kit[q].sig != 0 || tp->kit[q].mask != mask || tp->kit[q].app != look)
                continue;
            if (rr < tp->kit[q].w)
                break;
            rr -= tp->kit[q].w;
        }
        a->tiles[cy * l->w + cx].tid = tp->kit[q].tid;
        a->tiles[cy * l->w + cx].orient = tp->kit[q].orient;
        a->door = xrealloc(a->door, (a->ndoor + 1) * sizeof *a->door);
        j = a->ndoor++;
        a->door[j].cx = cx;
        a->door[j].cy = cy;
        a->door[j].x = cc(cx) + tp->kit[q].dx;
        a->door[j].y = cc(cy) + tp->kit[q].dy;
        a->door[j].z = tp->kit[q].dz;
        a->door[j].bearing = tp->kit[q].bearing;
        a->door[j].app = tp->kit[q].app;
        a->door[j].donor = 0; /* the corridor door */
        a->door[j].gen = -1;
        a->door[j].lock = a->door[j].dc = 0;
        a->door[j].keytag[0] = '\0';
    }
    return 0;
}

/* doors doorway: a door in the entrance of every corridor. Each link end has a room-wall connector cell and the first corridor cell behind it; where
 * the pack has a Doorway version of both (a connector with the door record, and a cell with one Doorway and one Corridor edge) they replace the
 * ordinary tiles and one generic-look door stands on the seam between them, as in stock areas. An end the pack cannot serve (the elbow tile of one
 * handedness fails the walkmesh rule) keeps its plain connector. Own RNG domain, so the ordinary tile draw is untouched. */
static int place_doorways(const tilepack *tp, uint64_t seed, const char *resref, const gff *tpl_git, dungeon_area *a, char *err)
{
    const layout *l = &a->lay;
    const gff_field *dd = gff_find(tpl_git->root, "Door List");
    uint8_t h[32];
    uint64_t v = 0;
    sm64 drng;
    int i, e, k;

    if (!dd || dd->type != GFF_LIST || dd->nlist < 2) {
        set_err(err, "doors doorway needs generic-door donors in template_door (run nwnpack templates again)");
        return -1;
    }
    domain_hash("nwndgen/doorway/1", 17, seed, resref, h);
    for (k = 0; k < 8; k++)
        v = v << 8 | h[k];
    sm64_seed(&drng, v);
    for (i = 0; i < l->nlink; i++)
        for (e = 0; e < 2; e++) {
            const int n = l->link_n[i];
            const int cx = l->conn[i][e][0], cy = l->conn[i][e][1], side = l->conn[i][e][2];
            const int fx = l->link[i][e ? n - 1 : 0][0], fy = l->link[i][e ? n - 1 : 0][1];
            const int cmask = l->kedge[cy][cx], fmask = l->kedge[fy][fx], dbit = KOPP[side];
            const int cidx = cell_idx(l, cx, cy), fidx = cell_idx(l, fx, fy);
            uint64_t ctot = 0, ftot = 0, rr;
            size_t q, cq = 0, fq = 0, donor;
            if (n < 2)
                continue;
            for (q = 0; q < tp->nkit; q++) {
                const pack_kit *p = &tp->kit[q];
                if (p->has_door && p->app == 0 && p->sig == cidx && p->mask == cmask && p->dmask == cmask)
                    ctot += p->w;
                if (!p->has_door && p->sig == fidx && p->mask == fmask && p->dmask == dbit)
                    ftot += p->w;
            }
            if (!ctot || !ftot)
                continue;
            rr = sm64_bounded(&drng, ctot);
            for (q = 0; q < tp->nkit; q++) {
                const pack_kit *p = &tp->kit[q];
                if (!p->has_door || p->app != 0 || p->sig != cidx || p->mask != cmask || p->dmask != cmask)
                    continue;
                if (rr < p->w)
                    break;
                rr -= p->w;
            }
            cq = q;
            rr = sm64_bounded(&drng, ftot);
            for (q = 0; q < tp->nkit; q++) {
                const pack_kit *p = &tp->kit[q];
                if (p->has_door || p->sig != fidx || p->mask != fmask || p->dmask != dbit)
                    continue;
                if (rr < p->w)
                    break;
                rr -= p->w;
            }
            fq = q;
            donor = 1 + (size_t)sm64_bounded(&drng, (uint64_t)(dd->nlist - 1));
            a->tiles[cy * l->w + cx].tid = tp->kit[cq].tid;
            a->tiles[cy * l->w + cx].orient = tp->kit[cq].orient;
            a->tiles[fy * l->w + fx].tid = tp->kit[fq].tid;
            a->tiles[fy * l->w + fx].orient = tp->kit[fq].orient;
            a->door = xrealloc(a->door, (a->ndoor + 1) * sizeof *a->door);
            a->door[a->ndoor].cx = cx;
            a->door[a->ndoor].cy = cy;
            a->door[a->ndoor].x = cc(cx) + tp->kit[cq].dx;
            a->door[a->ndoor].y = cc(cy) + tp->kit[cq].dy;
            a->door[a->ndoor].z = tp->kit[cq].dz;
            a->door[a->ndoor].bearing = tp->kit[cq].bearing;
            a->door[a->ndoor].app = 0;
            a->door[a->ndoor].donor = (int)donor;
            a->door[a->ndoor].gen = (int)gff_get_int(dd->list[donor], "GenericType_New", -1);
            a->door[a->ndoor].lock = a->door[a->ndoor].dc = 0;
            a->door[a->ndoor].keytag[0] = '\0';
            a->ndoor++;
        }
    return 0;
}

int dungeon_build_ex(const recipe *r, const tilepack *tp, const gff *tpl_are, const gff *tpl_git,
                     const gff *ifo_in, uint64_t seed, const spawn_cfg *sc, dungeon *out, char *err)
{
    size_t i;
    int x, y;

    if (r->kit_corridors && !tp->nkit) {
        set_err(err, "recipe asks for corridors kit but the tilepack has no kit records (build one with nwnpack tilepack-kit)");
        return -1;
    }
    memset(out, 0, sizeof *out);
    out->area = xcalloc(r->nareas, sizeof *out->area);
    out->n = r->nareas;
    for (i = 0; i < r->nareas; i++) {
        const recipe_area *ra = &r->areas[i];
        dungeon_area *a = &out->area[i];
        sm64 rng;
        placed_tile *t;
        char msg[ERRLEN];

        strcpy(a->resref, ra->resref);
        sm64_seed(&rng, dungeon_area_seed(seed, ra->resref));
        if ((r->kit_corridors ? layout_make_kit : layout_make)(&rng, (int)ra->w, (int)ra->h, (int)ra->rooms, &a->lay, msg) != 0) {
            set_err(err, "area '%s': %s", ra->resref, msg);
            goto fail;
        }
        t = a->tiles = xcalloc((size_t)(ra->w * ra->h), sizeof *a->tiles);
        for (y = 0; y < (int)ra->h; y++)
            for (x = 0; x < (int)ra->w; x++) {
                const int idx = cell_idx(&a->lay, x, y), mask = a->lay.kit ? a->lay.kedge[y][x] : 0;
                const pack_cell *pc = &tp->cell[idx];
                uint64_t total = 0, rr;
                size_t k;
                if (mask) { /* a kit corridor cell or a room-wall connector: pick among the kit tiles with these corners and edges */
                    for (k = 0; k < tp->nkit; k++)
                        if (tp->kit[k].sig == idx && tp->kit[k].mask == mask && !tp->kit[k].has_door && !tp->kit[k].dmask)
                            total += tp->kit[k].w;
                    if (!total) {
                        set_err(err, "area '%s': the tilepack has no kit tile for corners %c%c%c%c with edges 0x%x at cell (%d,%d)", ra->resref,
                                idx & 8 ? 'F' : 'W', idx & 4 ? 'F' : 'W', idx & 2 ? 'F' : 'W', idx & 1 ? 'F' : 'W', mask, x, y);
                        goto fail;
                    }
                    rr = sm64_bounded(&rng, total);
                    for (k = 0; k < tp->nkit; k++) {
                        if (tp->kit[k].sig != idx || tp->kit[k].mask != mask || tp->kit[k].has_door || tp->kit[k].dmask)
                            continue;
                        if (rr < tp->kit[k].w)
                            break;
                        rr -= tp->kit[k].w;
                    }
                    t[y * (int)ra->w + x].tid = tp->kit[k].tid;
                    t[y * (int)ra->w + x].orient = tp->kit[k].orient;
                    a->kprops[y][x] = (unsigned char)tp->kit[k].props;
                    continue;
                }
                for (k = 0; k < pc->n; k++)
                    total += pc->t[k].w;
                rr = sm64_bounded(&rng, total);
                for (k = 0; k < pc->n; k++) {
                    if (rr < pc->t[k].w)
                        break;
                    rr -= pc->t[k].w;
                }
                t[y * (int)ra->w + x].tid = pc->t[k].tid;
                t[y * (int)ra->w + x].orient = pc->t[k].orient;
            }
        if (r->doors == 3 && place_doorways(tp, seed, ra->resref, tpl_git, a, msg) != 0) {
            set_err(err, "area '%s': %s", ra->resref, msg);
            goto fail;
        }
        if ((r->doors == 1 || r->doors == 2) && place_doors(tp, seed, ra->resref, r->doors == 2, a, msg) != 0) {
            set_err(err, "area '%s': %s", ra->resref, msg);
            goto fail;
        }
        if (i == 0) {
            if (layout_portal_pair(&a->lay, 0, &rng, a->pair_start, msg))
                goto pfail;
            a->has_start = 1;
        } else {
            if (layout_portal_pair(&a->lay, 0, &rng, a->pair_prev, msg))
                goto pfail;
            a->has_prev = 1;
        }
        if (i + 1 < r->nareas) {
            if (layout_portal_pair(&a->lay, a->lay.nrooms - 1, &rng, a->pair_next, msg))
                goto pfail;
            a->has_next = 1;
        }
        continue;
    pfail:
        set_err(err, "area '%s': %s", ra->resref, msg);
        goto fail;
    }
    if (sc) {
        char msg[ERRLEN];
        if (spawn_plan(out, sc, seed, r->module_tag, msg) != 0) {
            set_err(err, "%s", msg);
            goto fail;
        }
        out->has_spawn = 1;
        if (roster_build_jrl(r->module_name, sc->ro->theme[out->theme].name, &out->jrl, &out->njrl, err) != 0)
            goto fail;
    }
    for (i = 0; i < r->nareas; i++) {
        dungeon_area *a = &out->area[i];
        gff are, git, gic;
        int sprung = 0;
        size_t sq;
        for (sq = 0; sq < a->nsp; sq++)
            sprung |= a->sp[sq].amb != 0;
        if (build_are(tpl_are, tp, &r->areas[i], &a->lay, a->tiles, out->has_spawn ? "scel_enter" : "", sprung ? "scel_beat" : "",
                      out->has_spawn ? &sc->ro->theme[out->theme] : NULL, &are, err) != 0) {
            gff_free(&are);
            goto fail;
        }
        if (write_gff(&are, &a->are, &a->nare, err) != 0)
            goto fail;
        memset(&git, 0, sizeof git);
        memset(&gic, 0, sizeof gic);
        if (build_git_gic(tpl_git, out, i, r, &git, &gic, err) != 0) {
            gff_free(&git);
            gff_free(&gic);
            goto fail;
        }
        if (write_gff(&git, &a->git, &a->ngit, err) != 0) {
            gff_free(&gic);
            goto fail;
        }
        if (write_gff(&gic, &a->gic, &a->ngic, err) != 0)
            goto fail;
    }
    if (build_ifo(ifo_in, r, out, seed, &out->ifo, &out->nifo, err) != 0)
        goto fail;
    return 0;
fail:
    dungeon_free(out);
    return -1;
}

int dungeon_build(const recipe *r, const tilepack *tp, const gff *tpl_are, const gff *tpl_git, const gff *ifo_in,
                  uint64_t seed, dungeon *out, char *err)
{
    return dungeon_build_ex(r, tp, tpl_are, tpl_git, ifo_in, seed, NULL, out, err);
}

/* ---------- dump ---------- */

char *dungeon_dump(const dungeon *d, uint64_t seed)
{
    size_t cap = 4096, len = 0, i;
    char *s = xmalloc(cap);
    int x, y, k;

#define PUT(...) do { \
        char line[256]; \
        int n_ = snprintf(line, sizeof line, __VA_ARGS__); \
        if (len + (size_t)n_ + 1 > cap) { cap = (len + (size_t)n_ + 1) * 2; s = xrealloc(s, cap); } \
        memcpy(s + len, line, (size_t)n_ + 1); len += (size_t)n_; \
    } while (0)

    PUT("dungeon 1\nseed %llu\n", (unsigned long long)seed);
    for (i = 0; i < d->n; i++) {
        const dungeon_area *a = &d->area[i];
        const layout *l = &a->lay;
        PUT("area %s %d %d attempts %d\n", a->resref, l->w, l->h, l->attempts);
        for (k = 0; k < l->nrooms; k++)
            PUT("room %d %d %d %d\n", l->room[k][0], l->room[k][1], l->room[k][2], l->room[k][3]);
        for (y = l->h; y >= 0; y--) {
            char row[LAT_MAX + 1];
            memcpy(row, l->lat[y], (size_t)l->w + 1);
            row[l->w + 1] = '\0';
            PUT("lat %s\n", row);
        }
        for (y = 0; y < l->h; y++) {
            PUT("tiles");
            for (x = 0; x < l->w; x++) {
                PUT(" %d:%d", a->tiles[y * l->w + x].tid, a->tiles[y * l->w + x].orient);
            }
            PUT("\n");
        }
        if (a->has_start) PUT("pair start %d %d %d %d\n", a->pair_start[0], a->pair_start[1], a->pair_start[2], a->pair_start[3]);
        if (a->has_prev) PUT("pair prev %d %d %d %d\n", a->pair_prev[0], a->pair_prev[1], a->pair_prev[2], a->pair_prev[3]);
        if (a->has_next) PUT("pair next %d %d %d %d\n", a->pair_next[0], a->pair_next[1], a->pair_next[2], a->pair_next[3]);
    }
#undef PUT
    return s;
}
