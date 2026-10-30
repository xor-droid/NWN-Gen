#include "spawn.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sha256.h"
#include "util.h"

static uint64_t domain_seed(const char *tag, size_t taglen, uint64_t seed, const char *name)
{
    sha256_ctx c;
    uint8_t s[8], d[32];
    uint64_t v = 0;
    int i;
    for (i = 0; i < 8; i++)
        s[i] = (uint8_t)(seed >> (56 - 8 * i));
    sha256_init(&c);
    sha256_update(&c, tag, taglen); /* includes the NUL */
    sha256_update(&c, s, 8);
    sha256_update(&c, name, strlen(name));
    sha256_final(&c, d);
    for (i = 0; i < 8; i++)
        v = v << 8 | d[i];
    return v;
}

int spawn_regular_cap(const spawn_cfg *sc) { return (int)(sc->min_level + sc->max_level) * 50; }
int spawn_boss_cap(const spawn_cfg *sc) { return spawn_regular_cap(sc) + 50 + (sc->party > 1 ? 100 : 0); }

static int rnd(sm64 *r, int n) { return (int)sm64_bounded(r, (uint64_t)n); }

static int theme_ok(const roster_theme *t, int rcap, int bcap)
{
    size_t i;
    int minion = 0, boss = 0;
    for (i = 0; i < t->n[ROLE_MINION]; i++)
        minion |= t->e[ROLE_MINION][i].cr <= rcap;
    for (i = 0; i < t->n[ROLE_BOSS]; i++)
        boss |= t->e[ROLE_BOSS][i].cr <= bcap;
    return minion && boss;
}

typedef struct { int x, y; } cell;

static void exclude_around(char occ[RECIPE_AREA_MAX][RECIPE_AREA_MAX], const layout *l, int cx, int cy)
{
    int x, y;
    for (y = cy - 1; y <= cy + 1; y++)
        for (x = cx - 1; x <= cx + 1; x++)
            if (x >= 0 && y >= 0 && x < l->w && y < l->h)
                occ[y][x] = 1;
}

static void put(dungeon_area *a, char kind, const char *bp, int cx, int cy, int face)
{
    spawn_pt *p = &a->sp[a->nsp++];
    p->kind = kind;
    strcpy(p->bp, bp);
    p->cx = cx;
    p->cy = cy;
    p->face = face;
    p->ox = p->oy = 0;
    memset(p->loot, 0, sizeof p->loot);
    p->gold = 0;
    p->custom = p->xcm = p->ycm = 0;
    p->weak = 0;
}

/* Boss in the middle of the last room of the last area, vault chest on a free neighbouring cell. */
static int place_objective(dungeon_area *a, const roster *ro, const roster_theme *t, int bcap, int boss_only, int room, sm64 *rng,
                           char occ[RECIPE_AREA_MAX][RECIPE_AREA_MAX], char *err)
{
    const layout *l = &a->lay;
    const int *r = l->room[room];
    static const int dx[4] = {1, 0, -1, 0}, dy[4] = {0, 1, 0, -1};
    int bx = -1, by = -1, best = 0, x, y, d, n, pick, cx = -1, cy = -1, nb = 0, nbx[4], nby[4];
    size_t i;
    const roster_entry *ok[ROSTER_POOL];

    for (y = r[1]; y < r[3]; y++)
        for (x = r[0]; x < r[2]; x++) {
            int ex = 2 * x - (r[0] + r[2] - 1), ey = 2 * y - (r[1] + r[3] - 1), dist = ex * ex + ey * ey;
            if (!layout_full_floor(l, x, y) || occ[y][x])
                continue;
            if (bx < 0 || dist < best) {
                bx = x;
                by = y;
                best = dist;
            }
        }
    if (bx < 0) {
        set_err(err, "last room of '%s' has no free full-floor cell for the boss", a->resref);
        return -1;
    }
    for (d = 0; d < 4; d++) {
        x = bx + dx[d];
        y = by + dy[d];
        if (x >= r[0] && x < r[2] && y >= r[1] && y < r[3] && layout_full_floor(l, x, y) && !occ[y][x]) {
            nbx[nb] = x;
            nby[nb++] = y;
        }
    }
    if (nb) {
        pick = rnd(rng, nb);
        cx = nbx[pick];
        cy = nby[pick];
    } else { /* nearest free full-floor cell in the room */
        int bd = 0;
        for (y = r[1]; y < r[3]; y++)
            for (x = r[0]; x < r[2]; x++) {
                int dd = (x - bx) * (x - bx) + (y - by) * (y - by);
                if ((x == bx && y == by) || !layout_full_floor(l, x, y) || occ[y][x])
                    continue;
                if (cx < 0 || dd < bd) {
                    cx = x;
                    cy = y;
                    bd = dd;
                }
            }
        if (cx < 0) {
            set_err(err, "last room of '%s' has no free cell for the vault", a->resref);
            return -1;
        }
    }
    for (i = 0, n = 0; i < t->n[ROLE_BOSS]; i++)
        if (t->e[ROLE_BOSS][i].cr <= bcap)
            ok[n++] = &t->e[ROLE_BOSS][i];
    put(a, boss_only ? 'T' : 'B', ok[rnd(rng, n)]->resref, bx, by, rnd(rng, 8));
    put(a, 'K', ro->key, bx, by, 0);
    put(a, 'V', ro->vault, cx, cy, rnd(rng, 8));
    put(a, 'R', ro->relic, cx, cy, 0);
    exclude_around(occ, l, bx, by);
    exclude_around(occ, l, cx, cy);
    return 0;
}

/* ---- scenery (props, lights, lootable containers) ---- */

#define DECO_LIGHT_MAX 4
#define DECO_CONTAINER_MAX 2
#define DECO_PROP_MAX 12

typedef char grid_t[RECIPE_AREA_MAX][RECIPE_AREA_MAX];

/* Kit layouts: walkable cells (a side is open when both its corners are floor or a kit corridor edge crosses it) joined across open
 * sides. Rooms are separate regions joined by corridors, so a centre prop is refused only if it raises the region count. Only regions
 * holding a full-floor or kit cell count: a lone wall-ring cell hanging off the prop cell is not a region. */
static int walk_regions(const layout *l, grid_t props)
{
    static const int dx[4] = {0, 1, 0, -1}, dy[4] = {1, 0, -1, 0};     /* T R B L */
    static const int cx0[4] = {0, 1, 0, 0}, cy0[4] = {1, 1, 0, 0};     /* first corner of each side, then the second */
    static const int cx1[4] = {1, 1, 1, 0}, cy1[4] = {1, 0, 0, 1};
    grid_t seen;
    unsigned char open[RECIPE_AREA_MAX][RECIPE_AREA_MAX];
    cell *stack = xcalloc((size_t)l->w * (size_t)l->h, sizeof *stack);
    int comps = 0, x, y, d;
    grid_t hubc;

    memset(seen, 0, sizeof seen);
    memset(hubc, 0, sizeof hubc);
    for (y = 0; y < l->h; y++)
        for (x = 0; x < l->w; x++) {
            open[y][x] = 0;
            if (props[y][x]) continue;
            for (d = 0; d < 4; d++)
                if ((l->lat[y + cy0[d]][x + cx0[d]] == 'F' && l->lat[y + cy1[d]][x + cx1[d]] == 'F') || (l->kedge[y][x] >> d & 1))
                    open[y][x] |= (unsigned char)(1 << d);
            hubc[y][x] = (char)(layout_full_floor(l, x, y) || l->kedge[y][x]);
        }
    for (y = 0; y < l->h; y++)
        for (x = 0; x < l->w; x++) {
            int sp = 0, hub = hubc[y][x];
            if (!open[y][x] || seen[y][x]) continue;
            stack[sp].x = x; stack[sp++].y = y;
            seen[y][x] = 1;
            while (sp) {
                cell c = stack[--sp];
                for (d = 0; d < 4; d++) {
                    int nx = c.x + dx[d], ny = c.y + dy[d];
                    if (!(open[c.y][c.x] >> d & 1) || nx < 0 || ny < 0 || nx >= l->w || ny >= l->h || seen[ny][nx] || !(open[ny][nx] >> ((d + 2) & 3) & 1))
                        continue;
                    seen[ny][nx] = 1;
                    hub |= hubc[ny][nx];
                    stack[sp].x = nx; stack[sp++].y = ny;
                }
            }
            comps += hub;
        }
    free(stack);
    return comps;
}

/* 4-connectivity of the full-floor cells that are not a prop cell: one region or the prop is refused (kit layouts: no new region). */
static int floor_stays_connected(const layout *l, grid_t props)
{
    static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
    int total = 0, seen_n = 0, sx = -1, sy = -1, x, y, d, sp = 0;
    grid_t seen;
    cell *stack;

    if (l->kit) {
        grid_t none;
        memset(none, 0, sizeof none);
        return walk_regions(l, props) == walk_regions(l, none);
    }
    stack = xcalloc((size_t)l->w * (size_t)l->h, sizeof *stack);

    memset(seen, 0, sizeof seen);
    for (y = 0; y < l->h; y++)
        for (x = 0; x < l->w; x++)
            if (layout_full_floor(l, x, y) && !props[y][x]) {
                total++;
                if (sx < 0) { sx = x; sy = y; }
            }
    if (sx >= 0) {
        stack[sp].x = sx; stack[sp++].y = sy;
        seen[sy][sx] = 1;
        while (sp) {
            cell c = stack[--sp];
            seen_n++;
            for (d = 0; d < 4; d++) {
                int nx = c.x + dx[d], ny = c.y + dy[d];
                if (nx < 0 || ny < 0 || nx >= l->w || ny >= l->h || seen[ny][nx] || props[ny][nx] || !layout_full_floor(l, nx, ny))
                    continue;
                seen[ny][nx] = 1;
                stack[sp].x = nx; stack[sp++].y = ny;
            }
        }
    }
    free(stack);
    return seen_n == total;
}

static const roster_entry *deco_pick(const roster_theme *t, int kind, sm64 *rng)
{
    uint64_t total = 0, r;
    size_t i;
    for (i = 0; i < t->ndeco[kind]; i++)
        total += (uint64_t)t->deco[kind][i].cr;
    r = sm64_bounded(rng, total);
    for (i = 0; i < t->ndeco[kind]; i++) {
        if (r < (uint64_t)t->deco[kind][i].cr)
            return &t->deco[kind][i];
        r -= (uint64_t)t->deco[kind][i].cr;
    }
    return &t->deco[kind][0];
}

/* Scenery goes on full-floor room cells, never in a doorway mouth, never near a transition or beside the boss/vault.
 * A tile is 10 m wide, so scenery takes one of its four corners (3.5 m off centre, wall corners favoured) and can
 * share a tile with a creature; only the rare centre slot needs a free tile and must keep the floor connected.
 * The tiles are the fully walkable stock floors, so every slot is walkable ground. */
typedef struct { int x, y, ox, oy; } slot;

/* The scenery grids and slots of an area: blocked tiles, tiles already holding a marker, the room of each tile, and every
 * usable corner slot (plus the centre of empty tiles) with its weight. With edges, the middle of every wall side of a tile is a slot too
 * (offset (+-1,0) or (0,+-1), placed 4.5 m from the centre along it, 0.5 m from the wall: the tile walkmesh check proves 4.5 m, not 3.5 m). cand and wt need w*h*5 entries, w*h*9 with edges. */
static size_t deco_slots(const dungeon_area *a, grid_t blocked, grid_t marked, grid_t roomof, slot *cand, int *wt, int edges)
{
    const layout *l = &a->lay;
    static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
    size_t nc = 0, k;
    int x, y, d, room;

    memset(blocked, 0, sizeof(grid_t));
    memset(marked, 0, sizeof(grid_t));
    memset(roomof, -1, sizeof(grid_t));
    for (room = 0; room < l->nrooms; room++)
        for (y = l->room[room][1]; y < l->room[room][3]; y++)
            for (x = l->room[room][0]; x < l->room[room][2]; x++)
                roomof[y][x] = (char)room;
    for (y = 0; y < l->h; y++)
        for (x = 0; x < l->w; x++)
            if (!layout_full_floor(l, x, y))
                blocked[y][x] = 1;
    if (a->has_start) {
        exclude_around(blocked, l, a->pair_start[0], a->pair_start[1]);
        exclude_around(blocked, l, a->pair_start[2], a->pair_start[3]);
    }
    if (a->has_prev) {
        exclude_around(blocked, l, a->pair_prev[0], a->pair_prev[1]);
        exclude_around(blocked, l, a->pair_prev[2], a->pair_prev[3]);
    }
    if (a->has_next) {
        exclude_around(blocked, l, a->pair_next[0], a->pair_next[1]);
        exclude_around(blocked, l, a->pair_next[2], a->pair_next[3]);
    }
    for (k = 0; k < a->nsp; k++) {
        const spawn_pt *p = &a->sp[k];
        if (p->kind == 'B' || p->kind == 'T' || p->kind == 'V')
            exclude_around(blocked, l, p->cx, p->cy);
        marked[p->cy][p->cx] = 1;
    }
    for (y = 0; y < l->h; y++)
        for (x = 0; x < l->w; x++) {
            int mouth = 0, solid[4], s;
            if (blocked[y][x] || roomof[y][x] < 0)
                continue;
            for (d = 0; d < 4; d++) {
                int nx = x + dx[d], ny = y + dy[d];
                solid[d] = nx < 0 || ny < 0 || nx >= l->w || ny >= l->h || !layout_full_floor(l, nx, ny);
                if (!solid[d] && roomof[ny][nx] != roomof[y][x])
                    mouth = 1;
            }
            if (mouth)
                continue;
            for (s = 0; s < 4; s++) { /* corners: (+-1, +-1); solid[] is E, W, N, S */
                int sx = s & 1 ? 1 : -1, sy = s & 2 ? 1 : -1;
                int walled = (sx > 0 ? solid[0] : solid[1]) + (sy > 0 ? solid[2] : solid[3]);
                cand[nc].x = x; cand[nc].y = y; cand[nc].ox = sx; cand[nc].oy = sy;
                wt[nc++] = walled == 2 ? 8 : walled == 1 ? 4 : 1;
            }
            for (s = 0; edges && s < 4; s++) /* wall-side slots, solid[] is E, W, N, S */
                if (solid[s]) {
                    cand[nc].x = x; cand[nc].y = y; cand[nc].ox = s == 0 ? 1 : s == 1 ? -1 : 0; cand[nc].oy = s == 2 ? 1 : s == 3 ? -1 : 0;
                    wt[nc++] = 5;
                }
            if (!marked[y][x]) { /* the centre of an empty tile, last resort */
                cand[nc].x = x; cand[nc].y = y; cand[nc].ox = 0; cand[nc].oy = 0;
                wt[nc++] = 1;
            }
        }
    return nc;
}

/* thin_props: dressing is on, so the theme's generic props are cut to a third (at least 2) and the room furniture carries the look */
static void plan_deco(dungeon_area *a, const roster_theme *t, uint64_t seed, int thin_props)
{
    const layout *l = &a->lay;
    grid_t blocked, marked, props, roomof;
    sm64 rng;
    size_t cap = (size_t)l->w * (size_t)l->h * 5;
    slot *cand = xcalloc(cap, sizeof *cand);
    int *wt = xcalloc(cap, sizeof *wt);
    size_t nc = 0, m;
    int quota[DECO_N] = {0, 0, 0};
    slot lights[DECO_LIGHT_MAX];
    int nlights = 0;
    static const char KIND_CH[DECO_N] = {'D', 'L', 'X'};

    if (!t->ndeco[DECO_PROP] && !t->ndeco[DECO_LIGHT] && !t->ndeco[DECO_CONTAINER]) {
        free(cand);
        free(wt);
        return;
    }
    sm64_seed(&rng, domain_seed("nwndgen/props/1", 16, seed, a->resref));
    memset(props, 0, sizeof props);
    nc = deco_slots(a, blocked, marked, roomof, cand, wt, 0);
    quota[DECO_LIGHT] = t->ndeco[DECO_LIGHT] ? (l->nrooms < DECO_LIGHT_MAX ? l->nrooms : DECO_LIGHT_MAX) : 0;
    quota[DECO_CONTAINER] = t->ndeco[DECO_CONTAINER] ? ((l->nrooms + 1) / 2 < DECO_CONTAINER_MAX ? (l->nrooms + 1) / 2 : DECO_CONTAINER_MAX) : 0;
    quota[DECO_PROP] = t->ndeco[DECO_PROP] ? (3 + 2 * l->nrooms < DECO_PROP_MAX ? 3 + 2 * l->nrooms : DECO_PROP_MAX) : 0;
    if (thin_props && quota[DECO_PROP] > 2)
        quota[DECO_PROP] = quota[DECO_PROP] / 3 < 2 ? 2 : quota[DECO_PROP] / 3;
    {
        /* order: lights, containers, props (the enum order is prop, light, container) */
        static const int order[3] = {DECO_LIGHT, DECO_CONTAINER, DECO_PROP};
        int oi;
        for (oi = 0; oi < 3; oi++) {
            int which = order[oi], placed = 0, tries;
            int *kw = xcalloc(nc ? nc : 1, sizeof *kw); /* weights for this kind: rejected slots are dropped here only */
            for (m = 0; m < nc; m++)
                kw[m] = wt[m];
            for (tries = 0; tries < (int)nc && placed < quota[which] && a->nsp < SPAWN_PT_MAX; tries++) {
                uint64_t total = 0, r;
                int pick = -1;
                const roster_entry *e;
                const slot *sl;
                for (m = 0; m < nc; m++)
                    total += (uint64_t)kw[m];
                if (!total)
                    break;
                r = sm64_bounded(&rng, total);
                for (m = 0; m < nc; m++) {
                    if (r < (uint64_t)kw[m]) { pick = (int)m; break; }
                    r -= (uint64_t)kw[m];
                }
                if (pick < 0)
                    break;
                sl = &cand[pick];
                kw[pick] = 0; /* never retry this slot for this kind */
                if (which == DECO_LIGHT) { /* lights sit in corners and are spread out */
                    int j, near = !sl->ox;
                    for (j = 0; j < nlights; j++)
                        near |= (abs(lights[j].x - sl->x) < 3 && abs(lights[j].y - sl->y) < 3);
                    if (near)
                        continue;
                }
                if (!sl->ox) { /* a centre slot occupies the whole tile: keep the floor connected */
                    props[sl->y][sl->x] = 1;
                    if (!floor_stays_connected(l, props)) {
                        props[sl->y][sl->x] = 0;
                        continue;
                    }
                }
                e = deco_pick(t, which, &rng);
                put(a, KIND_CH[which], e->resref, sl->x, sl->y, rnd(&rng, 8));
                wt[pick] = 0; /* taken for every kind */
                a->sp[a->nsp - 1].ox = sl->ox;
                a->sp[a->nsp - 1].oy = sl->oy;
                if (which == DECO_LIGHT)
                    lights[nlights++] = *sl;
                placed++;
            }
            free(kw);
        }
    }
    free(cand);
    free(wt);
}

/* Room dressing (recipe key `dressing`): every room gets a type (barracks, storage, ...) and furniture of that type from the
 * roster's roomprop lines. Own RNG domain, run after scenery and loot, so nothing earlier moves. Each room first gets one
 * centrepiece (a `centre` roomprop in the middle of the tile nearest the room's centre, kept only if the floor stays connected),
 * then wall pieces on corners and wall-side middles (0.5 m off the wall), 3, 5 or 8 at low, mid, high, facing into the room. A corner piece may take a second corner across the nearest wall of the
 * neighbouring tile (3 m away) to make a pair: two beds, a bookcase row (a third of the pieces at mid, half at high). */
static const int DRESS_PIECES[4] = {0, 3, 5, 8};

/* Facing (8 compass steps, 0 = east, 2 = north) of a piece that looks into the room: away from its wall; a corner piece picks one of its two walls. */
static int dress_face(const slot *s, sm64 *rng)
{
    int ox = s->ox, oy = s->oy;
    if (ox && oy)
        return rnd(rng, 2) ? (ox > 0 ? 4 : 0) : (oy > 0 ? 6 : 2);
    if (ox)
        return ox > 0 ? 4 : 0;
    if (oy)
        return oy > 0 ? 6 : 2;
    return 2 * rnd(rng, 4);
}

static const roster_entry *rprop_pick(const roster *ro, int rt, int centre, sm64 *rng)
{
    uint64_t total = 0, r;
    size_t i;
    for (i = 0; i < ro->nrprop[rt]; i++)
        if (ro->rcentre[rt][i] == centre)
            total += (uint64_t)ro->rprop[rt][i].cr;
    if (!total)
        return NULL;
    r = sm64_bounded(rng, total);
    for (i = 0; i < ro->nrprop[rt]; i++) {
        if (ro->rcentre[rt][i] != centre)
            continue;
        if (r < (uint64_t)ro->rprop[rt][i].cr)
            return &ro->rprop[rt][i];
        r -= (uint64_t)ro->rprop[rt][i].cr;
    }
    return NULL;
}

static int has_rprop(const roster *ro)
{
    int rt;
    for (rt = 0; rt < RT_N; rt++)
        if (ro->nrprop[rt])
            return 1;
    return 0;
}

static void plan_rooms(dungeon_area *a, const roster *ro, int level, uint64_t seed)
{
    const layout *l = &a->lay;
    grid_t blocked, marked, roomof, props;
    sm64 rng;
    size_t cap = (size_t)l->w * (size_t)l->h * 9, nc, k, m;
    slot *cand = xcalloc(cap, sizeof *cand);
    int *wt = xcalloc(cap, sizeof *wt);
    char *taken = xcalloc(cap, 1);
    int avail[RT_N], navail = 0, pool[RT_N], np = 0, rt, room, item;

    for (rt = 0; rt < RT_N; rt++)
        if (ro->nrprop[rt])
            avail[navail++] = rt;
    if (!navail || level < 1) {
        free(cand); free(wt); free(taken);
        return;
    }
    sm64_seed(&rng, domain_seed("nwndgen/rooms/1", 16, seed, a->resref));
    memset(props, 0, sizeof props);
    nc = deco_slots(a, blocked, marked, roomof, cand, wt, 1);
    for (k = 0; k < a->nsp; k++) { /* corners already holding scenery, tiles already filled by a centre prop */
        const spawn_pt *p = &a->sp[k];
        if (strchr("DLX", p->kind) && !p->ox && !p->oy)
            props[p->cy][p->cx] = 1;
        for (m = 0; m < nc; m++)
            if (cand[m].x == p->cx && cand[m].y == p->cy && cand[m].ox == p->ox && cand[m].oy == p->oy && (cand[m].ox || cand[m].oy))
                taken[m] = 1;
    }
    for (room = 0; room < l->nrooms; room++) {
        int type;
        if (!np) { /* every type once before any repeats */
            for (np = 0; np < navail; np++)
                pool[np] = avail[np];
        }
        {
            int j = rnd(&rng, np);
            type = pool[j];
            pool[j] = pool[--np];
        }
        a->rtype[room] = (unsigned char)(type + 1);
        /* the centrepiece: the free centre slot nearest the room's middle (ties: first), refused if it would cut the floor */
        if (level >= 2 && a->nsp < SPAWN_PT_MAX) {
            const roster_entry *e = rprop_pick(ro, type, 1, &rng);
            int mx = l->room[room][0] + l->room[room][2], my = l->room[room][1] + l->room[room][3], best = -1, bd = 0;
            for (m = 0; e && m < nc; m++) {
                int d;
                if (cand[m].ox || cand[m].oy || roomof[cand[m].y][cand[m].x] != room)
                    continue;
                d = abs(2 * cand[m].x + 1 - mx) + abs(2 * cand[m].y + 1 - my);
                if (best < 0 || d < bd) { best = (int)m; bd = d; }
            }
            if (e && best >= 0) {
                props[cand[best].y][cand[best].x] = 1;
                if (floor_stays_connected(l, props)) {
                    put(a, 'D', e->resref, cand[best].x, cand[best].y, dress_face(&cand[best], &rng));
                    taken[best] = 1;
                } else
                    props[cand[best].y][cand[best].x] = 0;
            }
        }
        for (item = 0; item < DRESS_PIECES[level]; item++) {
            uint64_t total = 0, r;
            int pick = -1, pair = -1;
            int want_pair = level == 2 ? rnd(&rng, 3) == 0 : level == 3 ? rnd(&rng, 2) == 0 : 0;
            for (m = 0; m < nc; m++)
                if ((cand[m].ox || cand[m].oy) && !taken[m] && roomof[cand[m].y][cand[m].x] == room)
                    total += (uint64_t)wt[m];
            if (!total)
                break;
            r = sm64_bounded(&rng, total);
            for (m = 0; m < nc; m++) {
                if (!(cand[m].ox || cand[m].oy) || taken[m] || roomof[cand[m].y][cand[m].x] != room)
                    continue;
                if (r < (uint64_t)wt[m]) { pick = (int)m; break; }
                r -= (uint64_t)wt[m];
            }
            if (pick < 0)
                break;
            if (want_pair && cand[pick].ox && cand[pick].oy) { /* the facing corner of the next tile along x or y */
                int along_x = rnd(&rng, 2), nx = cand[pick].x + (along_x ? cand[pick].ox : 0), ny = cand[pick].y + (along_x ? 0 : cand[pick].oy);
                int pox = along_x ? -cand[pick].ox : cand[pick].ox, poy = along_x ? cand[pick].oy : -cand[pick].oy;
                for (m = 0; m < nc; m++)
                    if (cand[m].x == nx && cand[m].y == ny && cand[m].ox == pox && cand[m].oy == poy && !taken[m] && roomof[ny][nx] == room) {
                        pair = (int)m;
                        break;
                    }
            }
            for (k = 0; k < (pair >= 0 ? 2u : 1u); k++) {
                int slot_i = k ? pair : pick;
                const roster_entry *e = rprop_pick(ro, type, 0, &rng);
                if (!e || a->nsp >= SPAWN_PT_MAX)
                    break;
                put(a, 'D', e->resref, cand[slot_i].x, cand[slot_i].y, dress_face(&cand[slot_i], &rng));
                a->sp[a->nsp - 1].ox = cand[slot_i].ox;
                a->sp[a->nsp - 1].oy = cand[slot_i].oy;
                if (!cand[slot_i].ox || !cand[slot_i].oy) { /* wall-side middle: 4.5 m, the walkmesh check proves 4.5 m mid-wall, not 3.5 m */
                    a->sp[a->nsp - 1].custom = 1;
                    a->sp[a->nsp - 1].xcm = 450 * cand[slot_i].ox;
                    a->sp[a->nsp - 1].ycm = 450 * cand[slot_i].oy;
                }
                taken[slot_i] = 1;
            }
            taken[pick] = 1;
        }
    }
    free(cand); free(wt); free(taken);
}

/* Corridor dressing (recipe key `corridor_dressing`, needs kit corridors and a pack made by a current `nwnpack tilepack-kit`): props
 * along the straight kit corridor cells, from the roster's corridorprop lines. A kit corridor is 5 m wide, so a prop stands 1.6 m off
 * the centre line (against the wall, facing the path) at -2, 0 or +2 m along the cell, on one of the spots the pack proved walkable for
 * that tile (`props` bits), at most one per cell (two at high, on opposite walls). Cells with a door, cells beside the
 * arrival and transition points, elbows and the first and last cell of a link stay clear. Own RNG domain, run after the room dressing,
 * so nothing earlier moves. low/mid/high furnish about 4, 6.5 and 9 in 10 of the eligible cells. */
static void plan_corridor(dungeon_area *a, const roster *ro, int level, uint64_t seed)
{
    static const int PCT[4] = {0, 40, 65, 90};
    const layout *l = &a->lay;
    sm64 rng;
    uint64_t tw = 0;
    size_t i, q;
    int link;

    if (!l->kit || !ro->ncprop || level < 1)
        return;
    for (i = 0; i < ro->ncprop; i++)
        tw += (uint64_t)ro->cprop[i].cr;
    sm64_seed(&rng, domain_seed("nwndgen/corridor/1", 19, seed, a->resref));
    for (link = 0; link < l->nlink; link++)
        for (i = 1; i + 1 < (size_t)l->link_n[link]; i++) {
            int x = l->link[link][i][0], y = l->link[link][i][1], mask = l->kedge[y][x], clear = 1, bits = a->kprops[y][x];
            int vertical = mask == 5, avail[6], na = 0, k, first = -1;
            if (mask != 5 && mask != 10)
                continue; /* straights only */
            for (q = 0; q < a->ndoor; q++)
                if (a->door[q].cx == x && a->door[q].cy == y)
                    clear = 0;
            if (a->has_start && abs(a->pair_start[0] - x) <= 1 && abs(a->pair_start[1] - y) <= 1) clear = 0;
            if (a->has_start && abs(a->pair_start[2] - x) <= 1 && abs(a->pair_start[3] - y) <= 1) clear = 0;
            if (a->has_prev && abs(a->pair_prev[0] - x) <= 1 && abs(a->pair_prev[1] - y) <= 1) clear = 0;
            if (a->has_next && abs(a->pair_next[0] - x) <= 1 && abs(a->pair_next[1] - y) <= 1) clear = 0;
            if (rnd(&rng, 100) >= PCT[level])
                continue;
            for (k = 0; k < 6; k++)
                if (bits >> k & 1)
                    avail[na++] = k;
            if (!clear || !na)
                continue;
            for (k = 0; k < (level == 3 && rnd(&rng, 4) == 0 ? 2 : 1); k++) {
                uint64_t w;
                const roster_entry *e = &ro->cprop[0];
                int spot = -1, side, lon;
                size_t j;
                spawn_pt *p;
                if (!k)
                    spot = avail[rnd(&rng, na)];
                else { /* the second prop goes on the opposite wall */
                    int opp[6], no = 0, v;
                    for (v = 0; v < na; v++)
                        if ((avail[v] < 3) != (first < 3))
                            opp[no++] = avail[v];
                    if (!no)
                        break;
                    spot = opp[rnd(&rng, no)];
                }
                first = spot;
                w = sm64_bounded(&rng, tw);
                for (j = 0; j < ro->ncprop; j++) {
                    if (w < (uint64_t)ro->cprop[j].cr) { e = &ro->cprop[j]; break; }
                    w -= (uint64_t)ro->cprop[j].cr;
                }
                if (a->nsp >= SPAWN_PT_MAX)
                    break;
                side = spot < 3 ? -1 : 1;
                lon = 200 * ((spot % 3) - 1);
                put(a, 'D', e->resref, x, y, vertical ? (side > 0 ? 4 : 0) : (side > 0 ? 6 : 2)); /* faces the path */
                p = &a->sp[a->nsp - 1];
                p->custom = 1;
                p->xcm = vertical ? 160 * side : lon;
                p->ycm = vertical ? lon : 160 * side;
                p->ox = vertical ? side : 0;
                p->oy = vertical ? 0 : side;
            }
        }
}

/* CR (1/100) of a creature blueprint within the theme; 0 when the theme does not list it */
static int theme_cr(const roster_theme *t, const char *bp)
{
    int role;
    size_t i;
    for (role = 0; role < ROLE_N; role++)
        for (i = 0; i < t->n[role]; i++)
            if (!strcmp(t->e[role][i].resref, bp))
                return t->e[role][i].cr;
    return 0;
}

/* Level-scaled loot: what each creature carries is fixed here, from the seed, so a module always drops the same things.
 * Loot entries apply by the creature's CR; gold grows with CR and with the band's average character level; a boss always
 * carries two items. Own RNG domain, drawn after
 * everything else, so layouts, creatures and scenery never move when the loot lines change. */
/* An item's chance after the loot_amount scale: halved, or 1.5x capped at a certain drop; never below 1%. */
static int scaled_chance(int chance, int pct)
{
    int c = chance * pct / 100;
    return c > 100 ? 100 : c < 1 ? 1 : c;
}

static void plan_loot(dungeon_area *a, const roster_theme *t, uint64_t seed, const spawn_cfg *sc)
{
    sm64 rng;
    size_t k, e;
    long avg = ((long)sc->min_level + (long)sc->max_level) / 2; /* gold grows 25% per character level above 1 */
    int ipct, gpct;
    if (avg < 1)
        avg = 1;
    if (!t->nloot && !t->gold_per_cr)
        return;
    ipct = sc->loot_amount == 1 ? 50 : sc->loot_amount == 2 ? 150 : 100; /* item chance scale; draws are unchanged, so mid stays as it was */
    gpct = sc->loot_amount == 1 ? 50 : sc->loot_amount == 2 ? 200 : 100; /* gold scale */
    sm64_seed(&rng, domain_seed("nwndgen/loot/1", 15, seed, a->resref));
    for (k = 0; k < a->nsp; k++) {
        spawn_pt *p = &a->sp[k];
        int cr, got = 0, boss = p->kind == 'B' || p->kind == 'T';
        if (p->kind != 'C' && !boss)
            continue;
        cr = theme_cr(t, p->bp);
        if (!boss) {
            for (e = 0; e < t->nloot && got < 2; e++) {
                const roster_loot *lt = &t->loot[e];
                if (cr >= lt->min_cr && cr <= lt->max_cr && rnd(&rng, 100) < scaled_chance(lt->chance, ipct))
                    strcpy(p->loot[got++], lt->resref);
            }
        } else {
            char taken[ROSTER_POOL] = {0};
            while (got < 2) {
                int total = 0, w;
                for (e = 0; e < t->nloot; e++)
                    if (!taken[e] && cr >= t->loot[e].min_cr && cr <= t->loot[e].max_cr)
                        total += t->loot[e].chance;
                if (!total)
                    break;
                w = rnd(&rng, total);
                for (e = 0; e < t->nloot; e++) {
                    if (taken[e] || cr < t->loot[e].min_cr || cr > t->loot[e].max_cr)
                        continue;
                    if (w < t->loot[e].chance)
                        break;
                    w -= t->loot[e].chance;
                }
                taken[e] = 1;
                strcpy(p->loot[got++], t->loot[e].resref);
            }
        }
        if (t->gold_per_cr) {
            long crh = cr < 25 ? 25 : cr;
            long gp = (long)t->gold_per_cr * crh * (50 + rnd(&rng, 101)) * (100 + 25 * (avg - 1)) / 1000000;
            if (boss)
                gp *= 3;
            gp = gp * gpct / 100;
            p->gold = (int)(gp < 1 ? 1 : gp);
        }
    }
}

int spawn_corridor_props(const dungeon_area *a)
{
    size_t k;
    int n = 0;
    for (k = 0; k < a->nsp; k++)
        n += a->sp[k].kind == 'D' && !layout_full_floor(&a->lay, a->sp[k].cx, a->sp[k].cy);
    return n;
}

int spawn_plan(dungeon *d, const spawn_cfg *sc, uint64_t seed, const char *module_tag, char *err)
{
    const roster *ro = sc->ro;
    int rcap = spawn_regular_cap(sc), bcap = spawn_boss_cap(sc), eligible[ROSTER_THEMES], ne = 0;
    size_t i, t;
    sm64 trng;
    const roster_theme *th;

    for (t = 0; t < ro->ntheme; t++)
        if (theme_ok(&ro->theme[t], rcap, bcap))
            eligible[ne++] = (int)t;
    if (!ne) {
        set_err(err, "roster has no theme with a boss at or under CR %d.%02d and a minion at or under CR %d.%02d "
                     "for levels %u-%u, party %u",
                bcap / 100, bcap % 100, rcap / 100, rcap % 100, sc->min_level, sc->max_level, sc->party);
        return -1;
    }
    sm64_seed(&trng, domain_seed("nwndgen/theme/1", 16, seed, module_tag));
    d->theme = eligible[rnd(&trng, ne)];
    th = &ro->theme[d->theme];

    d->nhench = 0;
    if (sc->henchmen > 0) { /* N distinct stock families, each at the level nearest the recommended average */
        int pool[6], np = (int)ro->nhench, avg = (int)(sc->min_level + sc->max_level) / 2, nn, k;
        sm64 hrng;
        if (np < sc->henchmen) {
            set_err(err, "henchmen %d needs at least %d 'henchman' lines in the roster (it has %d)", sc->henchmen, sc->henchmen, np);
            return -1;
        }
        for (k = 0; k < np; k++)
            pool[k] = k;
        sm64_seed(&hrng, domain_seed("nwndgen/hench/1", 15, seed, module_tag));
        nn = avg - 3 < 1 ? 1 : avg - 3 > 11 ? 11 : avg - 3; /* stock nw_hen_xxx_NN is a level NN+3 character */
        for (k = 0; k < sc->henchmen; k++) {
            int j = k + rnd(&hrng, np - k), tmp = pool[k];
            pool[k] = pool[j];
            pool[j] = tmp;
            snprintf(d->hench[k], sizeof d->hench[k], "%s_%02d", ro->hench[pool[k]], nn);
            d->nhench++;
        }
    }

    for (i = 0; i < d->n; i++) {
        dungeon_area *a = &d->area[i];
        const layout *l = &a->lay;
        char occ[RECIPE_AREA_MAX][RECIPE_AREA_MAX];
        cell *cand = xcalloc((size_t)l->w * (size_t)l->h, sizeof *cand);
        size_t nc = 0, k;
        sm64 rng;
        uint64_t budget = (uint64_t)sc->party * (uint64_t)rcap * 2u * (uint64_t)(50 * d->n + 50 * (i + 1)) /
                          (100u * d->n);
        int x, y;

        memset(occ, 0, sizeof occ);
        sm64_seed(&rng, domain_seed("nwndgen/spawn/1", 16, seed, a->resref));
        a->sp = xcalloc(SPAWN_PT_MAX, sizeof *a->sp);
        for (y = 0; y < l->h; y++)
            for (x = 0; x < l->w; x++)
                if (!layout_full_floor(l, x, y))
                    occ[y][x] = 1;
        if (a->has_start) {
            if (sc->boss_only || sc->loot_test) { /* test runs: the boss may stand right beside the arrival point, just not on it */
                occ[a->pair_start[1]][a->pair_start[0]] = 1;
                occ[a->pair_start[3]][a->pair_start[2]] = 1;
            } else {
                exclude_around(occ, l, a->pair_start[0], a->pair_start[1]);
                exclude_around(occ, l, a->pair_start[2], a->pair_start[3]);
            }
        }
        if (a->has_prev) {
            exclude_around(occ, l, a->pair_prev[0], a->pair_prev[1]);
            exclude_around(occ, l, a->pair_prev[2], a->pair_prev[3]);
        }
        if (a->has_next) {
            exclude_around(occ, l, a->pair_next[0], a->pair_next[1]);
            exclude_around(occ, l, a->pair_next[2], a->pair_next[3]);
        }
        /* The objective goes in the last room of the last area; boss_only test runs put it in the start room
         * of the first area instead (still clear of the arrival point). */
        if (((sc->boss_only || sc->loot_test) ? i == 0 : i + 1 == d->n) &&
            place_objective(a, ro, th, bcap, sc->boss_only || sc->loot_test, (sc->boss_only || sc->loot_test) ? 0 : l->nrooms - 1, &rng, occ, err) != 0) {
            free(cand);
            return -1;
        }
        if (i == 0) /* the arrival room stays quiet */
            for (y = l->room[0][1]; y < l->room[0][3]; y++)
                for (x = l->room[0][0]; x < l->room[0][2]; x++)
                    occ[y][x] = 1;
        for (y = 0; y < l->h; y++)
            for (x = 0; x < l->w; x++)
                if (!occ[y][x]) {
                    cand[nc].x = x;
                    cand[nc++].y = y;
                }
        for (k = nc; k > 1; k--) { /* Fisher-Yates */
            size_t j = (size_t)sm64_bounded(&rng, k);
            cell tmp = cand[k - 1];
            cand[k - 1] = cand[j];
            cand[j] = tmp;
        }
        for (k = 0; !sc->boss_only && k < nc && k < (sc->loot_test ? (i == 0 ? 5u : 0u) : SPAWN_AREA_MAX); k++) {
            static const int weight[3] = {6, 3, 1};
            const roster_entry *pool[3][ROSTER_POOL];
            size_t cnt[3] = {0, 0, 0}, e;
            int role, total = 0, w;
            for (role = 0; role < 3; role++) {
                for (e = 0; e < th->n[role]; e++) {
                    const roster_entry *en = &th->e[role][e];
                    if (en->cr <= rcap && (sc->loot_test || (uint64_t)en->cr <= budget))
                        pool[role][cnt[role]++] = en;
                }
                if (cnt[role])
                    total += weight[role];
            }
            if (!total)
                break;
            w = rnd(&rng, total);
            for (role = 0; role < 3; role++) {
                if (!cnt[role])
                    continue;
                if (w < weight[role])
                    break;
                w -= weight[role];
            }
            {
                const roster_entry *en = pool[role][sm64_bounded(&rng, cnt[role])];
                put(a, 'C', en->resref, cand[k].x, cand[k].y, rnd(&rng, 8));
                if (!sc->loot_test)
                    budget -= (uint64_t)en->cr;
            }
        }
        if (!sc->boss_only)
            plan_deco(a, th, seed, sc->dressing && !sc->loot_test && has_rprop(ro));
        plan_loot(a, th, seed, sc);
        if (sc->dressing && !sc->boss_only && !sc->loot_test)
            plan_rooms(a, ro, sc->dressing, seed);
        if (sc->corridor_dressing && !sc->boss_only && !sc->loot_test)
            plan_corridor(a, ro, sc->corridor_dressing, seed);
        if (sc->loot_test)
            for (k = 0; k < a->nsp; k++)
                a->sp[k].weak = a->sp[k].kind == 'C' || a->sp[k].kind == 'T';
        free(cand);
    }
    return 0;
}
