/* dgcheck - independent validator for modules built by `nwndgen --recipe`.
 *
 *   dgcheck (--root INSTALL | --set TILESET.set) [--tileset NAME] MODULE.mod
 *
 * Re-derives the tile rules from the tileset's own .set file (corner/edge/height matching under
 * counter-clockwise rotation, Tile_List rows from the south) and checks the built module against them:
 *   - every listed area has ARE+GIT+GIC; Tile_List length = Width*Height; the area's tileset is NAME
 *   - zero adjacency violations; no door tiles
 *   - the cells with all four corners Floor form ONE 4-connected region
 *   - every waypoint/trigger/entry position lies on such a cell, inside the area
 *   - no waypoint sits inside a trigger polygon
 *   - every transition trigger links (LinkedToFlags 2) to a waypoint tag found in ANOTHER area
 *   - the GIC mirrors the GIT list lengths and struct ids
 *   - module.ifo: entry area is listed, entry position is on floor
 * With a roster (and an install) it also checks the creature/objective markers (see check_spawn).
 * It shares no code with src/dungeon.c (only the generic GFF/ERF readers and the tileset model).
 * Exit 0 = all checks passed, 1 = a problem was found, 2 = usage. */
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

static double absd(double v) { return v < 0 ? -v : v; }

static int problems;
static char err[ERRLEN];

#define BAD(...) do { problems++; printf("BAD  " __VA_ARGS__); printf("\n"); } while (0)

static const char *const GIT_LISTS[9] = {"Creature List", "Door List", "Encounter List", "List", "Placeable List",
                                         "SoundList", "StoreList", "TriggerList", "WaypointList"};

typedef struct {
    char name[20], on_enter[40], on_beat[40];
    int w, h;
    unsigned char *floor;            /* w*h: 1 where the tile has Floor on all four corners */
    int *tid;                        /* w*h tile ids */
    int *ori;                        /* w*h tile orientations */
    unsigned char *open;             /* w*h: sides (T=1 R=2 B=4 L=8) a walker can cross: both corners Floor, or a Corridor edge; 16 = full-floor or corridor tile */
    gff git;
} area_info;

static int load_gff(const erf *m, const char *name, uint16_t type, gff *g)
{
    const erf_res *r = erf_find(m, name, type);
    if (!r) { BAD("%s.%s is missing from the module", name, res_ext(type)); return -1; }
    if (gff_parse(r->data, r->size, g, err) != 0) { BAD("%s.%s does not parse: %s", name, res_ext(type), err); return -1; }
    return 0;
}

static int on_floor(const area_info *a, double x, double y)
{
    int cx = (int)(x / 10.0), cy = (int)(y / 10.0);
    if (x < 0 || y < 0 || cx >= a->w || cy >= a->h) return 0;
    return a->floor[cy * a->w + cx];
}

/* A kit corridor cell: walkable (hub bit) but not a full-floor tile. Decor may stand there (recipe key corridor_dressing). */
static int on_corridor(const area_info *a, double x, double y)
{
    int cx = (int)(x / 10.0), cy = (int)(y / 10.0);
    if (x < 0 || y < 0 || cx >= a->w || cy >= a->h || !a->open) return 0;
    return !a->floor[cy * a->w + cx] && (a->open[cy * a->w + cx] & 16);
}

static int in_poly(const gff_struct *trig, double x, double y)
{
    const gff_field *g = gff_find(trig, "Geometry");
    double cx = gff_get_float(trig, "XPosition", 0), cy = gff_get_float(trig, "YPosition", 0);
    size_t i, n = g && g->type == GFF_LIST ? g->nlist : 0;
    int inside = 0;
    for (i = 0; i < n; i++) {
        const gff_struct *p1 = g->list[i], *p2 = g->list[(i + 1) % n];
        double x1 = cx + gff_get_float(p1, "PointX", 0), y1 = cy + gff_get_float(p1, "PointY", 0);
        double x2 = cx + gff_get_float(p2, "PointX", 0), y2 = cy + gff_get_float(p2, "PointY", 0);
        if ((y1 > y) != (y2 > y) && x < (x2 - x1) * (y - y1) / (y2 - y1) + x1) inside = !inside;
    }
    return inside;
}

static int floor_components(const area_info *a, int *nfloor)
{
    int n = a->w * a->h, i, comps = 0, sp;
    unsigned char *seen = calloc((size_t)n, 1);
    int *stack = malloc((size_t)n * sizeof *stack);
    *nfloor = 0;
    for (i = 0; i < n; i++) *nfloor += a->floor[i];
    for (i = 0; i < n; i++) {
        if (!a->floor[i] || seen[i]) continue;
        comps++;
        sp = 0;
        stack[sp++] = i;
        seen[i] = 1;
        while (sp) {
            int c = stack[--sp], x = c % a->w, y = c / a->w, d;
            static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
            for (d = 0; d < 4; d++) {
                int nx = x + dx[d], ny = y + dy[d];
                if (nx < 0 || ny < 0 || nx >= a->w || ny >= a->h || !a->floor[ny * a->w + nx] || seen[ny * a->w + nx]) continue;
                seen[ny * a->w + nx] = 1;
                stack[sp++] = ny * a->w + nx;
            }
        }
    }
    free(seen);
    free(stack);
    return comps;
}

/* Cells with a side a walker can cross (both corners Floor, or a Corridor edge) joined across matching open sides: one region expected.
 * hubs_only counts just regions holding a full-floor or corridor tile (bit 16): a lone wall-ring tile is not a region worth counting. */
static int walk_components(const area_info *a, int hubs_only)
{
    int n = a->w * a->h, i, comps = 0, sp, hub;
    unsigned char *seen = calloc((size_t)n, 1);
    int *stack = malloc((size_t)n * sizeof *stack);
    static const int DX[4] = {0, 1, 0, -1}, DY[4] = {1, 0, -1, 0}; /* T R B L */
    for (i = 0; i < n; i++) {
        if (!(a->open[i] & 15) || seen[i]) continue;
        hub = (a->open[i] & 16) != 0;
        sp = 0;
        stack[sp++] = i;
        seen[i] = 1;
        while (sp) {
            int c = stack[--sp], x = c % a->w, y = c / a->w, d;
            for (d = 0; d < 4; d++) {
                int nx = x + DX[d], ny = y + DY[d];
                if (!(a->open[c] >> d & 1) || nx < 0 || ny < 0 || nx >= a->w || ny >= a->h) continue;
                if (!(a->open[ny * a->w + nx] >> ((d + 2) & 3) & 1) || seen[ny * a->w + nx]) continue;
                seen[ny * a->w + nx] = 1;
                hub |= (a->open[ny * a->w + nx] & 16) != 0;
                stack[sp++] = ny * a->w + nx;
            }
        }
        comps += !hubs_only || hub;
    }
    free(seen);
    free(stack);
    return comps;
}

static int angle_close(double a, double b)
{
    double d = a - b;
    while (d > 3.14159265358979) d -= 6.28318530717959;
    while (d < -3.14159265358979) d += 6.28318530717959;
    return d > -0.01 && d < 0.01;
}

/* Every tile door slot in the tileset (read from the .set, not from the generator) has exactly one door at its position and facing,
 * closed, unlocked and not plot; no door stands anywhere else. Returns the number of doors in the GIT. */
static size_t check_doors(const area_info *a, const int *tid, const int *ori, const tileset *ts)
{
    const gff_field *dl = a->git.root ? gff_find(a->git.root, "Door List") : NULL;
    size_t nd = dl && dl->type == GFF_LIST ? dl->nlist : 0, q;
    unsigned char *used = calloc(nd ? nd : 1, 1);
    int x, y;

    for (y = 0; y < a->h; y++)
        for (x = 0; x < a->w; x++) {
            const set_tile *t = &ts->t[tid[y * a->w + x]];
            int o = ori[y * a->w + x], s;
            if (t->doors <= 0) continue;
            if (t->ndslot != t->doors) { BAD("%s: tile %s declares %d doors but the set lists %d slots", a->name, t->model, t->doors, t->ndslot); continue; }
            for (s = 0; s < t->doors && s < 4; s++) {
                double lx = t->dslot[s].x, ly = t->dslot[s].y, ex, ey, eb;
                int r, found = 0;
                for (r = 0; r < o; r++) { double nx = -ly; ly = lx; lx = nx; }
                ex = x * 10.0 + 5.0 + lx;
                ey = y * 10.0 + 5.0 + ly;
                eb = (t->dslot[s].orient + 90.0 * o) * 3.14159265358979 / 180.0;
                for (q = 0; q < nd && !found; q++) {
                    const gff_struct *d = dl->list[q];
                    double dx = gff_get_float(d, "X", -1000) - ex, dy = gff_get_float(d, "Y", -1000) - ey;
                    if ((used[q] && t->dslot[s].type != 0) || dx < -0.2 || dx > 0.2 || dy < -0.2 || dy > 0.2) continue; /* two Doorway tiles share the one door on their seam */
                    used[q] = found = 1;
                    if (!angle_close(gff_get_float(d, "Bearing", 99), eb) && !(t->dslot[s].type == 0 && angle_close(gff_get_float(d, "Bearing", 99), eb + 3.14159265358979))) BAD("%s: door %s faces the wrong way for its tile", a->name, gff_get_string(d, "Tag"));
                    if (gff_get_int(d, "Appearance", -1) != t->dslot[s].type) BAD("%s: door %s has appearance %lld, the slot wants %d", a->name, gff_get_string(d, "Tag"), (long long)gff_get_int(d, "Appearance", -1), t->dslot[s].type);
                    if (t->dslot[s].type == 0 && (gff_get_int(d, "GenericType_New", -1) < 0 || gff_get_int(d, "GenericType_New", -1) > 27)) BAD("%s: door %s on a generic door slot has GenericType_New %lld", a->name, gff_get_string(d, "Tag"), (long long)gff_get_int(d, "GenericType_New", -1));
                    if (gff_get_int(d, "Plot", 1) || gff_get_int(d, "AnimationState", 1) || gff_get_int(d, "TrapFlag", 1) || gff_get_int(d, "Locked", -1) < 0)
                        BAD("%s: door %s is plot, trapped or not closed", a->name, gff_get_string(d, "Tag"));
                    if (gff_get_int(d, "Locked", 0)) { /* recipe key locks: a pickable or keyed door; never plot, so it can always be bashed */
                        const char *kn = gff_get_string(d, "KeyName");
                        int64_t dc = gff_get_int(d, "OpenLockDC", 0);
                        if (gff_get_int(d, "Lockable", 0) != 1 || dc < 12 || dc > 30 || gff_get_int(d, "CloseLockDC", 0) != dc) BAD("%s: locked door %s is not lockable or has a pick DC outside 12-30", a->name, gff_get_string(d, "Tag"));
                        if ((gff_get_int(d, "KeyRequired", 0) != 0) != (kn && *kn) || (kn && strlen(kn) > 32)) BAD("%s: locked door %s has a key requirement that does not match its key tag", a->name, gff_get_string(d, "Tag"));
                    } else if (gff_get_int(d, "KeyRequired", 0) || gff_get_int(d, "Lockable", 0) > 1) BAD("%s: unlocked door %s wants a key", a->name, gff_get_string(d, "Tag"));
                    if (gff_get_float(d, "Z", 99) - t->dslot[s].z > 0.2 || t->dslot[s].z - gff_get_float(d, "Z", 99) > 0.2) BAD("%s: door %s is at the wrong height", a->name, gff_get_string(d, "Tag"));
                }
                if (!found) BAD("%s: door tile %s at (%d,%d) has no door at its slot (%.1f, %.1f)", a->name, t->model, x, y, ex, ey);
            }
        }
    for (q = 0; q < nd; q++)
        if (!used[q]) BAD("%s: door %s is not on a door tile slot", a->name, gff_get_string(dl->list[q], "Tag"));
    free(used);
    return nd;
}

/* ---- creatures and objective (only when the module was built with a roster) ---- */

typedef struct {
    char kind, bp[17];
    int area, cx, cy;
    double ox, oy; /* metres from the cell centre */
    char loot[2][17];
    char key[40], keybp[17]; /* a creature carrying a door key: its tag and blueprint */
    int lockdc;              /* a container that starts locked: the pick DC */
    int amb;                 /* ambush group id (creature in the group, or the container that springs it), 0 = none */
    int rt;                  /* a patrolling creature (kind C): the packed route cells x1,y1,x2,y2 (8 bits each), 0 = none */
    char gate[48];           /* a lever (kind E): tag of the gate door it opens */
    int trap, trapdc;        /* a floor trap (kind P) or a trapped container: TRAP_BASE_TYPE number and detect/disarm DC; -1 = absent */
    int gold;
} marker;

/* A local variable on a marker waypoint: returns its Type (1 int, 3 string) or 0 when absent. */
static int var_get(const gff_struct *wp, const char *name, const char **sval, int64_t *ival)
{
    const gff_field *vt = gff_find(wp, "VarTable");
    size_t i;
    for (i = 0; vt && vt->type == GFF_LIST && i < vt->nlist; i++) {
        const char *nm = gff_get_string(vt->list[i], "Name");
        if (nm && !strcmp(nm, name)) {
            *sval = gff_get_string(vt->list[i], "Value");
            *ival = gff_get_int(vt->list[i], "Value", 0);
            return (int)gff_get_int(vt->list[i], "Type", 0);
        }
    }
    return 0;
}

static int cheb(int ax, int ay, int bx, int by)
{
    int dx = abs(ax - bx), dy = abs(ay - by);
    return dx > dy ? dx : dy;
}

static int resref_ok(const char *s)
{
    size_t i, n = strlen(s);
    if (!n || n > 16) return 0;
    for (i = 0; i < n; i++)
        if (!((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= '0' && s[i] <= '9') || s[i] == '_')) return 0;
    return 1;
}

static int in_roster_role(const roster_theme *t, int role, const char *bp, int *cr)
{
    size_t i;
    for (i = 0; i < t->n[role]; i++)
        if (!strcmp(t->e[role][i].resref, bp)) { *cr = t->e[role][i].cr; return 1; }
    return 0;
}

/* Blueprint must exist; creatures must be hostile (faction 1) and, with a roster, match its CR. */
static void check_blueprint(resman *rm, const char *where, char kind, const char *bp, int roster_cr)
{
    uint16_t type = strchr("VDLXE", kind) ? 2044 : (kind == 'C' || kind == 'B') ? 2027 : 2025;
    uint8_t *b;
    size_t bl;
    gff g;
    char e2[ERRLEN];
    if (resman_get(rm, bp, type, &b, &bl, e2) != 0) { BAD("%s: blueprint %s does not resolve: %s", where, bp, e2); return; }
    if (gff_parse(b, bl, &g, e2) != 0) { BAD("%s: blueprint %s does not parse: %s", where, bp, e2); free(b); return; }
    if (type == 2027) {
        double cr = gff_get_float(g.root, "ChallengeRating", -1);
        if (gff_get_int(g.root, "FactionID", -1) != 1) BAD("%s: creature %s is not in the hostile faction (FactionID %lld)", where, bp, (long long)gff_get_int(g.root, "FactionID", -1));
        if (roster_cr >= 0 && (int)(cr * 100.0 + 0.5) != roster_cr) BAD("%s: creature %s has CR %.3f but the roster says %d.%02d", where, bp, cr, roster_cr / 100, roster_cr % 100);
    } else if (type == 2044) {
        int inv = (int)gff_get_int(g.root, "HasInventory", 0);
        if ((kind == 'V' || kind == 'X') && inv != 1) BAD("%s: %s %s has no inventory", where, kind == 'V' ? "vault" : "container", bp);
        if ((kind == 'D' || kind == 'L') && (inv || gff_get_int(g.root, "Useable", 0))) BAD("%s: scenery %s is usable or holds items", where, bp);
        if (kind == 'E' && (inv || !gff_get_int(g.root, "Useable", 0) || gff_get_int(g.root, "Locked", 0) || gff_get_int(g.root, "Plot", 0) || gff_get_int(g.root, "TrapFlag", 0))) BAD("%s: lever %s is not usable, or holds items, or is locked, plot or trapped", where, bp);
        if (kind == 'X' && (gff_get_int(g.root, "Locked", 0) || gff_get_int(g.root, "Plot", 0) || gff_get_int(g.root, "TrapFlag", 0))) BAD("%s: container %s is locked, plot or trapped", where, bp);
    }
    gff_free(&g);
    free(b);
}

/* With an install: every tile with four Floor corners must have a walkmesh that is walkable at its centre, at the four
 * scenery slots and just inside each edge, all joined (so no placed tile can cut off a room or sit a creature on a
 * pillar). Checked per tile model, independent of how the generator chose the tile. */
static void check_floor_tiles(area_info *ai, size_t na, const tileset *ts, resman *rm)
{
    static const double PTS[9][2] = {{0, 0}, {3.5, 3.5}, {-3.5, 3.5}, {3.5, -3.5}, {-3.5, -3.5}, {4.5, 0}, {-4.5, 0}, {0, 4.5}, {0, -4.5}};
    unsigned char ok[512];
    uint8_t *sm = NULL;
    size_t sml, a;
    signed char *state; /* per tile id: 0 unknown, 1 safe, -1 reported */
    long *count, *kcount;
    size_t t;
    char e2[ERRLEN];

    if (!rm || !ts->n) return;
    if (resman_get(rm, "surfacemat", 2017, &sm, &sml, e2) != 0 || surfacemat_walkable((const char *)sm, ok, sizeof ok, e2) < 0) {
        BAD("cannot read surfacemat.2da: %s", e2);
        free(sm);
        return;
    }
    free(sm);
    state = calloc(ts->n, 1);
    count = calloc(ts->n, sizeof *count);
    kcount = calloc(ts->n, sizeof *kcount);
    for (a = 0; a < na; a++) {
        int x, y;
        if (!ai[a].floor || !ai[a].tid) continue;
        for (y = 0; y < ai[a].h; y++)
            for (x = 0; x < ai[a].w; x++) {
                int tid = ai[a].tid[y * ai[a].w + x];
                if (ai[a].floor[y * ai[a].w + x] && tid >= 0 && (size_t)tid < ts->n) count[tid]++;
                char wname[48];
                uint8_t *wb;
                size_t wl, c2;
                wok w;
                if (!ai[a].floor[y * ai[a].w + x] || tid < 0 || (size_t)tid >= ts->n || state[tid]) continue;
                snprintf(wname, sizeof wname, "%s", ts->t[tid].model);
                for (c2 = 0; wname[c2]; c2++) if (wname[c2] >= 'A' && wname[c2] <= 'Z') wname[c2] = (char)(wname[c2] + 32);
                if (resman_get(rm, wname, 2016, &wb, &wl, e2) != 0 || wok_parse((const char *)wb, &w, e2) != 0) {
                    BAD("%s: walkmesh %s: %s", ai[a].name, wname, e2);
                    state[tid] = -1;
                    continue;
                }
                free(wb);
                if (wok_points_connected(&w, ok, sizeof ok, PTS, 9)) state[tid] = 1;
                else {
                    BAD("%s: full-floor tile %s at (%d,%d) is not walkable and connected at its centre, corners and edges", ai[a].name, ts->t[tid].model, x, y);
                    state[tid] = -1;
                }
                wok_free(&w);
            }
    }
    /* kit tiles (a Corridor edge): in the tile's own frame, every Corridor or all-Floor edge midpoint and the door slot must lie on one
     * connected walkable region of the game's own walkmesh */
    for (a = 0; a < na; a++) {
        int x, y;
        if (!ai[a].tid) continue;
        for (y = 0; y < ai[a].h; y++)
            for (x = 0; x < ai[a].w; x++) {
                static const double DIR[4][2] = {{0, 4.5}, {4.5, 0}, {0, -4.5}, {-4.5, 0}};
                static const int K1[4] = {C_NW, C_NE, C_SW, C_NW}, K2[4] = {C_NE, C_SE, C_SE, C_SW};
                const int tid = ai[a].tid[y * ai[a].w + x];
                const set_tile *st;
                tile_sig_t sg;
                double kp[6][2];
                size_t np = 0, c2;
                char wname[48];
                uint8_t *wb;
                size_t wl;
                wok w;
                int k, corr = 0;
                if (tid < 0 || (size_t)tid >= ts->n || ai[a].floor[y * ai[a].w + x]) continue;
                st = &ts->t[tid];
                tile_sig(st, 0, 1, &sg);
                for (k = 0; k < 4; k++) {
                    int c = !strcmp(sg.edge[k], "Corridor") || !strcmp(sg.edge[k], "Doorway");
                    corr |= c;
                    if (c || (!strcmp(sg.corner[K1[k]], "Floor") && !strcmp(sg.corner[K2[k]], "Floor"))) { kp[np][0] = DIR[k][0]; kp[np++][1] = DIR[k][1]; }
                }
                if (!corr) continue;
                kcount[tid]++;
                if (state[tid]) continue;
                if (st->ndslot > 0 && st->dslot[0].type != 0) { kp[np][0] = st->dslot[0].x; kp[np++][1] = st->dslot[0].y; } /* a generic door sits on the tile edge, its Doorway edge midpoint is checked instead */
                snprintf(wname, sizeof wname, "%s", st->model);
                for (c2 = 0; wname[c2]; c2++) if (wname[c2] >= 'A' && wname[c2] <= 'Z') wname[c2] = (char)(wname[c2] + 32);
                if (resman_get(rm, wname, 2016, &wb, &wl, e2) != 0 || wok_parse((const char *)wb, &w, e2) != 0) {
                    BAD("%s: walkmesh %s: %s", ai[a].name, wname, e2);
                    state[tid] = -1;
                    continue;
                }
                free(wb);
                if (wok_points_connected(&w, ok, sizeof ok, (const double (*)[2])kp, np)) state[tid] = 1;
                else {
                    BAD("%s: corridor tile %s at (%d,%d) is not walkable and connected at its corridor edges and door", ai[a].name, st->model, x, y);
                    state[tid] = -1;
                }
                wok_free(&w);
            }
    }
    printf("full-floor tiles:");
    for (t = 0; t < ts->n; t++)
        if (count[t]) printf(" %s x%ld", ts->t[t].model, count[t]);
    printf("\n");
    printf("corridor tiles:");
    for (t = 0; t < ts->n; t++)
        if (kcount[t]) printf(" %s x%ld", ts->t[t].model, kcount[t]);
    printf("\n");
    free(count);
    free(kcount);
    free(state);
}

/* Cells a walker reaches from `start` across matching open sides, never entering a blocked cell. */
static void flood(const area_info *a, int start, const unsigned char *blocked, unsigned char *seen)
{
    static const int DX[4] = {0, 1, 0, -1}, DY[4] = {1, 0, -1, 0}; /* T R B L */
    int n = a->w * a->h, sp = 0;
    int *stack = malloc((size_t)n * sizeof *stack);
    memset(seen, 0, (size_t)n);
    if (start >= 0 && start < n && !blocked[start]) {
        stack[sp++] = start;
        seen[start] = 1;
    }
    while (sp) {
        int c = stack[--sp], x = c % a->w, y = c / a->w, d;
        for (d = 0; d < 4; d++) {
            int nx = x + DX[d], ny = y + DY[d], nc = ny * a->w + nx;
            if (!(a->open[c] >> d & 1) || nx < 0 || ny < 0 || nx >= a->w || ny >= a->h || seen[nc] || blocked[nc] || !(a->open[nc] >> ((d + 2) & 3) & 1)) continue;
            seen[nc] = 1;
            stack[sp++] = nc;
        }
    }
    free(stack);
}

/* Locked doors (recipe key locks). A keyed door names its key by tag: exactly one creature in the same area carries that tag, and the creature
 * can be reached from where the party arrives without passing a keyed door whose own key is still out of reach. Keyless locked doors can be
 * picked or bashed, and keyed doors are bashable too (not plot), so they never block this walk. */
static void check_locks(const gff *ifo, area_info *ai, size_t na, const marker *mk, size_t nm, const roster *ro)
{
    size_t a, q, i;
    for (a = 0; a < na; a++) {
        const gff_field *dl = ai[a].git.root ? gff_find(ai[a].git.root, "Door List") : NULL, *wl = ai[a].git.root ? gff_find(ai[a].git.root, "WaypointList") : NULL;
        size_t nd = dl && dl->type == GFF_LIST ? dl->nlist : 0, nb = 0;
        int n = ai[a].w * ai[a].h, start = -1, changed;
        int *dcell = calloc(nd ? nd : 1, sizeof *dcell), *hcell = calloc(nd ? nd : 1, sizeof *hcell), *dq = calloc(nd ? nd : 1, sizeof *dq);
        unsigned char *blocked = calloc((size_t)(n ? n : 1), 1), *seen = calloc((size_t)(n ? n : 1), 1);
        for (q = 0; q < nd; q++) {
            const gff_struct *d = dl->list[q];
            const char *kn = gff_get_string(d, "KeyName");
            int cx = (int)(gff_get_float(d, "X", -1) / 10.0), cy = (int)(gff_get_float(d, "Y", -1) / 10.0), found = 0;
            int gate = kn && !strncmp(kn, "GATE_", 5);
            if (!gff_get_int(d, "Locked", 0) || !gff_get_int(d, "KeyRequired", 0) || !kn || !*kn || cx < 0 || cy < 0 || cx >= ai[a].w || cy >= ai[a].h) continue;
            if (gate) { /* a gate: its "key" is a lever, exactly one marker names this door */
                const char *dt = gff_get_string(d, "Tag");
                for (i = 0; i < nm; i++)
                    if (mk[i].area == (int)a && mk[i].kind == 'E' && dt && !strcmp(mk[i].gate, dt)) {
                        if (!found++) hcell[nb] = mk[i].cy * ai[a].w + mk[i].cx;
                    }
                if (found != 1) BAD("%s: gate door %s is opened by %d levers", ai[a].name, dt ? dt : "", found);
                else {
                    dcell[nb] = cy * ai[a].w + cx;
                    dq[nb++] = (int)q;
                }
                continue;
            }
            for (i = 0; i < nm; i++)
                if (mk[i].area == (int)a && mk[i].key[0] && !strcmp(mk[i].key, kn)) {
                    if (!found++) hcell[nb] = mk[i].cy * ai[a].w + mk[i].cx;
                }
            if (found != 1) BAD("%s: keyed door %s needs key '%s' but %d creatures in the area carry it", ai[a].name, gff_get_string(d, "Tag"), kn, found);
            else {
                dcell[nb] = cy * ai[a].w + cx;
                dq[nb++] = (int)q;
            }
        }
        for (i = 0; i < nm; i++) { /* every lever opens a locked gate door of its own area */
            int ok = 0;
            if (mk[i].area != (int)a || mk[i].kind != 'E') continue;
            for (q = 0; q < nd; q++) {
                const char *kn = gff_get_string(dl->list[q], "KeyName");
                ok |= !strcmp(gff_get_string(dl->list[q], "Tag") ? gff_get_string(dl->list[q], "Tag") : "", mk[i].gate) && gff_get_int(dl->list[q], "Locked", 0) && gff_get_int(dl->list[q], "KeyRequired", 0) && kn && !strncmp(kn, "GATE_", 5) && !gff_get_int(dl->list[q], "Plot", 0);
            }
            if (!ok) BAD("%s: lever at (%d,%d) names %s, which is not a locked, bashable gate door of the area", ai[a].name, mk[i].cx, mk[i].cy, mk[i].gate);
            if (ro && strcmp(mk[i].bp, ro->lever)) BAD("%s: lever %s is not the roster's lever %s", ai[a].name, mk[i].bp, ro->lever);
        }
        for (i = 0; i < nm; i++) {
            int f = 0;
            if (mk[i].area != (int)a || !mk[i].key[0]) continue;
            for (q = 0; q < nd; q++) {
                const char *kn = gff_get_string(dl->list[q], "KeyName");
                f += gff_get_int(dl->list[q], "KeyRequired", 0) && kn && !strcmp(kn, mk[i].key);
            }
            if (f != 1) BAD("%s: a creature carries key '%s' that opens %d doors", ai[a].name, mk[i].key, f);
            if (ro && strcmp(mk[i].keybp, ro->key)) BAD("%s: door key item %s is not the roster's key %s", ai[a].name, mk[i].keybp, ro->key);
        }
        if (a == 0) start = (int)(gff_get_float(ifo->root, "Mod_Entry_Y", -1) / 10.0) * ai[a].w + (int)(gff_get_float(ifo->root, "Mod_Entry_X", -1) / 10.0);
        for (q = 0; wl && q < wl->nlist; q++) {
            const char *t = gff_get_string(wl->list[q], "Tag");
            size_t tl = t ? strlen(t) : 0;
            if (t && tl > 4 && !strncmp(t, "WP_", 3) && !strcmp(t + tl - 4, "_FWD"))
                start = (int)(gff_get_float(wl->list[q], "YPosition", -1) / 10.0) * ai[a].w + (int)(gff_get_float(wl->list[q], "XPosition", -1) / 10.0);
        }
        if (nb && start >= 0 && start < n) {
            size_t b;
            for (b = 0; b < nb; b++) blocked[dcell[b]] = 1;
            do {
                changed = 0;
                flood(&ai[a], start, blocked, seen);
                for (b = 0; b < nb; b++)
                    if (blocked[dcell[b]] && seen[hcell[b]]) {
                        blocked[dcell[b]] = 0;
                        changed = 1;
                    }
            } while (changed);
            for (b = 0; b < nb; b++)
                if (blocked[dcell[b]]) BAD("%s: the key for door %s cannot be reached from the arrival point without passing a locked keyed door", ai[a].name, gff_get_string(dl->list[dq[b]], "Tag"));
        }
        free(dcell);
        free(hcell);
        free(dq);
        free(blocked);
        free(seen);
    }
}

/* TRAP_BASE_TYPE families the generator uses (spike, tangle, acid, fire, electrical, gas, frost, sonic), each with tiers 0-3. */
static int trap_family_ok(int fam)
{
    static const int F[8] = {0, 8, 12, 16, 20, 24, 28, 36};
    int i;
    for (i = 0; i < 8; i++)
        if (F[i] == fam) return 1;
    return 0;
}

/* Patrols (recipe key patrols): the creature is a plain one (no key, not hidden in an ambush group, not weak), and both route cells are full-floor cells of
 * the same room (reachable from its own cell over full-floor cells without crossing a door cell), with no door, floor trap or other creature's route on them. */
static void check_patrols(area_info *ai, size_t na, const marker *mk, size_t nm)
{
    size_t a, i, j;
    for (a = 0; a < na; a++) {
        int n = ai[a].w * ai[a].h, c;
        unsigned char *blocked = calloc((size_t)(n ? n : 1), 1), *seen = calloc((size_t)(n ? n : 1), 1);
        const gff_field *dl = ai[a].git.root ? gff_find(ai[a].git.root, "Door List") : NULL;
        for (c = 0; c < n; c++) blocked[c] = !ai[a].floor[c];
        for (j = 0; dl && j < dl->nlist; j++) {
            int cx = (int)(gff_get_float(dl->list[j], "X", -1) / 10.0), cy = (int)(gff_get_float(dl->list[j], "Y", -1) / 10.0);
            if (cx >= 0 && cy >= 0 && cx < ai[a].w && cy < ai[a].h) blocked[cy * ai[a].w + cx] = 1;
        }
        for (i = 0; i < nm; i++) {
            int k, pts[2][2];
            if (mk[i].area != (int)a || mk[i].rt <= 0) continue;
            if (mk[i].key[0] || mk[i].amb) BAD("%s: patrol creature %s at (%d,%d) carries a key or hides in an ambush group", ai[a].name, mk[i].bp, mk[i].cx, mk[i].cy);
            pts[0][0] = mk[i].rt >> 24;
            pts[0][1] = (mk[i].rt >> 16) & 255;
            pts[1][0] = (mk[i].rt >> 8) & 255;
            pts[1][1] = mk[i].rt & 255;
            flood(&ai[a], mk[i].cy * ai[a].w + mk[i].cx, blocked, seen);
            for (k = 0; k < 2; k++) {
                if (k == 1 && !pts[1][0] && !pts[1][1]) break;
                if (pts[k][0] < 0 || pts[k][1] < 0 || pts[k][0] >= ai[a].w || pts[k][1] >= ai[a].h || !seen[pts[k][1] * ai[a].w + pts[k][0]])
                    BAD("%s: patrol route cell (%d,%d) of %s at (%d,%d) is not in the creature's room", ai[a].name, pts[k][0], pts[k][1], mk[i].bp, mk[i].cx, mk[i].cy);
                else if (pts[k][0] == mk[i].cx && pts[k][1] == mk[i].cy) BAD("%s: patrol route cell (%d,%d) is the creature's own cell", ai[a].name, pts[k][0], pts[k][1]);
                for (j = 0; j < nm; j++)
                    if (mk[j].area == (int)a && mk[j].kind == 'P' && mk[j].cx == pts[k][0] && mk[j].cy == pts[k][1])
                        BAD("%s: patrol route cell (%d,%d) holds a floor trap", ai[a].name, pts[k][0], pts[k][1]);
            }
        }
        free(blocked);
        free(seen);
    }
}

/* Ambush groups (recipe key ambush). Group ids run 1..n in an area; a group has 2 or 3 plain creatures (no key carrier) and at most one
 * container, which stands near them (the same room) and springs the group when opened; a group without a container is sprung by proximity.
 * Only areas with a group run the heartbeat script, and the extra scripts ship in the module exactly then. */
static void check_ambush(area_info *ai, size_t na, const marker *mk, size_t nm, int enc)
{
    size_t a, i;
    for (a = 0; a < na; a++) {
        int maxg = 0, g, pat = 0;
        for (i = 0; i < nm; i++) {
            if (mk[i].area == (int)a && mk[i].amb > maxg) maxg = mk[i].amb;
            pat += mk[i].area == (int)a && mk[i].rt > 0;
        }
        if ((maxg > 0 || pat > 0 || enc) != !strcmp(ai[a].on_beat, "scel_beat") || (*ai[a].on_beat && strcmp(ai[a].on_beat, "scel_beat")))
            BAD("%s: OnHeartbeat is '%s' but the area has %d ambush groups, %d patrols and encounters are %s", ai[a].name, ai[a].on_beat, maxg, pat, enc ? "on" : "off");
        for (g = 1; g <= maxg; g++) {
            int members = 0, chests = 0;
            size_t cm = 0;
            for (i = 0; i < nm; i++) {
                if (mk[i].area != (int)a || mk[i].amb != g) continue;
                if (mk[i].kind == 'C') {
                    members++;
                    if (mk[i].key[0]) BAD("%s: ambush group %d hides a key carrier (%s)", ai[a].name, g, mk[i].bp);
                } else if (mk[i].kind == 'X') {
                    chests++;
                    cm = i;
                } else
                    BAD("%s: ambush group %d includes a marker of kind %c", ai[a].name, g, mk[i].kind);
            }
            if (members < 2 || members > 3) BAD("%s: ambush group %d has %d creatures (expected 2-3)", ai[a].name, g, members);
            if (chests > 1) BAD("%s: ambush group %d has %d chests", ai[a].name, g, chests);
            for (i = 0; chests == 1 && i < nm; i++)
                if (mk[i].area == (int)a && mk[i].amb == g && mk[i].kind == 'C' && cheb(mk[i].cx, mk[i].cy, mk[cm].cx, mk[cm].cy) > 8)
                    BAD("%s: ambush group %d springs from a chest too far from its creatures", ai[a].name, g);
        }
    }
}

static void check_spawn(const erf *m, const gff *ifo, area_info *ai, size_t na, const tileset *ts, resman *rm, const roster *ro)
{
    static const struct { const char *name; uint16_t type; } SC[10] = {{"scel_enter", RES_NSS}, {"scel_enter", RES_NCS}, {"scel_acquire", RES_NSS}, {"scel_acquire", RES_NCS},
                                                                      {"scel_use", RES_NSS}, {"scel_use", RES_NCS}, {"scel_beat", RES_NSS}, {"scel_beat", RES_NCS},
                                                                       {"scel_rest", RES_NSS}, {"scel_rest", RES_NCS}};
    const char *acq = gff_get_string(ifo->root, "Mod_OnAcquirItem");
    int nweak = 0, spawn = erf_find(m, "module", RES_JRL) != NULL || (acq && *acq), cnt[128] = {0}, k, ncreat = 0, ndecor = 0, nlight = 0, ncont = 0, theme = -1, enc_on = 0;
    size_t a, q, nm = 0, i, j;
    marker *mk = NULL, *boss = NULL, *key = NULL, *vault = NULL, *relic = NULL;
    gff jrl;

    for (a = 0; a < na; a++) spawn |= !strcmp(ai[a].on_enter, "scel_enter");
    if (!spawn) return;

    for (a = 0; a < na; a++)
        if (strcmp(ai[a].on_enter, "scel_enter")) BAD("%s: OnEnter is '%s', expected scel_enter", ai[a].name, ai[a].on_enter);
    if (!acq || strcmp(acq, "scel_acquire")) BAD("Mod_OnAcquirItem is '%s', expected scel_acquire", acq ? acq : "");
    for (k = 0; k < 10; k++) {
        if (k >= 8) { /* scel_rest ships, and is required, exactly when the module hooks OnPlayerRest */
            const char *pr = gff_get_string(ifo->root, "Mod_OnPlayerRest");
            if (!pr || !*pr) continue;
            if (strcmp(pr, "scel_rest")) BAD("Mod_OnPlayerRest is '%s', expected scel_rest", pr);
        } else if (k >= 4) { /* scel_use and scel_beat ship with recipe keys ambush and levers; they are required when an area runs the heartbeat, and scel_use also when it has a lever */
            size_t b, w;
            int want = 0;
            for (b = 0; b < na; b++) {
                const gff_field *wl = ai[b].git.root ? gff_find(ai[b].git.root, "WaypointList") : NULL;
                want |= !strcmp(ai[b].on_beat, "scel_beat");
                for (w = 0; wl && k < 6 && w < wl->nlist; w++) {
                    const char *wt = gff_get_string(wl->list[w], "Tag");
                    want |= wt && !strncmp(wt, "SPE_", 4);
                }
            }
            if (!want) continue;
        }
        const erf_res *r = erf_find(m, SC[k].name, SC[k].type);
        if (!r || !r->size) BAD("script %s.%s is missing or empty", SC[k].name, res_ext(SC[k].type));
        else if (SC[k].type == RES_NCS && (r->size < 13 || memcmp(r->data, "NCS V1.0B", 9))) BAD("script %s.ncs is not NCS V1.0B", SC[k].name);
    }
    memset(&jrl, 0, sizeof jrl);
    if (load_gff(m, "module", RES_JRL, &jrl) == 0) {
        const gff_field *cats = gff_find(jrl.root, "Categories"), *el;
        const char *tg;
        if (!cats || cats->type != GFF_LIST || cats->nlist != 1) BAD("module.jrl: expected exactly one category");
        else if (!(tg = gff_get_string(cats->list[0], "Tag")) || strcmp(tg, "scel_main")) BAD("module.jrl: category tag is not scel_main");
        else if (!(el = gff_find(cats->list[0], "EntryList")) || el->type != GFF_LIST || el->nlist != 3) BAD("module.jrl: expected 3 journal entries");
        else
            for (q = 0; q < 3; q++) {
                if (gff_get_int(el->list[q], "ID", -1) != (int64_t)q + 1) BAD("module.jrl: entry %zu has ID %lld", q, (long long)gff_get_int(el->list[q], "ID", -1));
                if (gff_get_int(el->list[q], "End", -1) != (q == 2)) BAD("module.jrl: entry %zu has the wrong End flag", q);
            }
        gff_free(&jrl);
    }

    /* markers */
    for (a = 0; a < na; a++) {
        const gff_field *wl = ai[a].git.root ? gff_find(ai[a].git.root, "WaypointList") : NULL;
        for (q = 0; wl && q < wl->nlist; q++) {
            const char *t = gff_get_string(wl->list[q], "Tag");
            marker *p;
            if (!t || strncmp(t, "SP", 2) || !t[2] || !strchr("CBTVKRDLXPE", t[2]) || t[3] != '_') continue;
            if (!resref_ok(t + 4)) { BAD("%s: marker %s has a bad blueprint resref", ai[a].name, t); continue; }
            mk = realloc(mk, (nm + 1) * sizeof *mk);
            p = &mk[nm++];
            p->kind = t[2] == 'T' ? 'B' : t[2]; /* T = the 1-hit-point test boss */
            nweak += t[2] == 'T';
            strcpy(p->bp, t + 4);
            p->area = (int)a;
            p->cx = (int)(gff_get_float(wl->list[q], "XPosition", -1) / 10.0);
            p->cy = (int)(gff_get_float(wl->list[q], "YPosition", -1) / 10.0);
            p->ox = gff_get_float(wl->list[q], "XPosition", -1) - (p->cx * 10.0 + 5.0);
            p->oy = gff_get_float(wl->list[q], "YPosition", -1) - (p->cy * 10.0 + 5.0);
            p->loot[0][0] = p->loot[1][0] = '\0';
            p->gold = 0;
            p->key[0] = p->keybp[0] = '\0';
            p->lockdc = 0;
            p->amb = 0;
            p->trap = -1;
            p->trapdc = 0;
            p->gate[0] = '\0';
            p->rt = 0;
            {
                const char *sv = NULL;
                int64_t iv = 0;
                int ty = var_get(wl->list[q], "scel_loot", &sv, &iv);
                if (ty == 3 && sv && resref_ok(sv)) strcpy(p->loot[0], sv);
                else if (ty) BAD("%s: marker %s has a malformed scel_loot", ai[a].name, t);
                ty = var_get(wl->list[q], "scel_loot2", &sv, &iv);
                if (ty == 3 && sv && resref_ok(sv)) strcpy(p->loot[1], sv);
                else if (ty) BAD("%s: marker %s has a malformed scel_loot2", ai[a].name, t);
                ty = var_get(wl->list[q], "scel_gold", &sv, &iv);
                if (ty == 1) p->gold = (int)iv;
                else if (ty) BAD("%s: marker %s has a malformed scel_gold", ai[a].name, t);
                ty = var_get(wl->list[q], "scel_key", &sv, &iv);
                if (ty == 3 && sv && *sv && strlen(sv) <= 32 && p->kind == 'C') strcpy(p->key, sv);
                else if (ty) BAD("%s: marker %s has a malformed scel_key", ai[a].name, t);
                ty = var_get(wl->list[q], "scel_keybp", &sv, &iv);
                if (ty == 3 && sv && resref_ok(sv) && p->key[0]) strcpy(p->keybp, sv);
                else if (ty) BAD("%s: marker %s has a malformed scel_keybp", ai[a].name, t);
                if (p->key[0] && !p->keybp[0]) BAD("%s: marker %s carries a key without a key blueprint", ai[a].name, t);
                ty = var_get(wl->list[q], "scel_lockdc", &sv, &iv);
                if (ty == 1 && p->kind == 'X' && iv >= 12 && iv <= 30) p->lockdc = (int)iv;
                else if (ty) BAD("%s: marker %s has a malformed scel_lockdc", ai[a].name, t);
                ty = var_get(wl->list[q], "scel_amb", &sv, &iv);
                if (ty == 1 && (p->kind == 'C' || p->kind == 'X') && iv >= 1 && iv <= 8) p->amb = (int)iv;
                else if (ty) BAD("%s: marker %s has a malformed scel_amb", ai[a].name, t);
                ty = var_get(wl->list[q], "scel_trap", &sv, &iv);
                if (ty == 1 && (p->kind == 'P' || p->kind == 'X') && iv >= 0 && iv <= 40 && (iv & 3) == iv - (iv & ~3) && trap_family_ok((int)(iv & ~3))) p->trap = (int)iv;
                else if (ty) BAD("%s: marker %s has a malformed scel_trap", ai[a].name, t);
                ty = var_get(wl->list[q], "scel_trapdc", &sv, &iv);
                if (ty == 1 && (p->kind == 'P' || p->kind == 'X') && iv >= 12 && iv <= 30) p->trapdc = (int)iv;
                else if (ty) BAD("%s: marker %s has a malformed scel_trapdc", ai[a].name, t);
                ty = var_get(wl->list[q], "scel_rt", &sv, &iv);
                if (ty == 1 && p->kind == 'C' && iv >= 16777216 && iv <= 2147483647) p->rt = (int)iv;
                else if (ty) BAD("%s: marker %s has a malformed scel_rt", ai[a].name, t);
                ty = var_get(wl->list[q], "scel_gate", &sv, &iv);
                if (ty == 3 && sv && p->kind == 'E' && !strncmp(sv, "DOOR_", 5) && strlen(sv) < sizeof p->gate) strcpy(p->gate, sv);
                else if (ty) BAD("%s: marker %s has a malformed scel_gate", ai[a].name, t);
                if (p->kind == 'E' && !p->gate[0]) BAD("%s: lever marker %s names no gate door", ai[a].name, t);
                if (p->kind == 'P' && (p->trap < 0 || !p->trapdc || strcmp(p->bp, "trap"))) BAD("%s: trap marker %s lacks a valid type, DC or the blueprint name 'trap'", ai[a].name, t);
                if (p->kind == 'X' && ((p->trapdc > 0) != (p->trap >= 0))) BAD("%s: container marker %s has a trap type without a DC or the other way round", ai[a].name, t);
            }
            cnt[(int)p->kind]++;
            if (p->kind == 'C') ncreat++;
            if (p->kind == 'D') ndecor++;
            if (p->kind == 'L') nlight++;
            if (p->kind == 'X') ncont++;
        }
    }
    /* take pointers only once the array has stopped growing */
    for (i = 0; i < nm; i++) {
        if (mk[i].kind == 'B') boss = &mk[i];
        else if (mk[i].kind == 'K') key = &mk[i];
        else if (mk[i].kind == 'V') vault = &mk[i];
        else if (mk[i].kind == 'R') relic = &mk[i];
    }
    if (cnt['B'] != 1 || cnt['K'] != 1 || cnt['V'] != 1 || cnt['R'] != 1)
        BAD("expected exactly one boss, key, vault and relic marker; found %d/%d/%d/%d", cnt['B'], cnt['K'], cnt['V'], cnt['R']);
    else {
        if (nweak ? (boss->area != 0 || vault->area != 0) : (boss->area != (int)na - 1 || vault->area != (int)na - 1)) BAD("boss and vault must be in the %s area", nweak ? "first" : "last");
        if (key->area != boss->area || key->cx != boss->cx || key->cy != boss->cy) BAD("key marker is not on the boss marker");
        if (relic->area != vault->area || relic->cx != vault->cx || relic->cy != vault->cy) BAD("relic marker is not on the vault marker");
        if (cheb(boss->cx, boss->cy, vault->cx, vault->cy) < 1 || cheb(boss->cx, boss->cy, vault->cx, vault->cy) > 2) BAD("vault is not next to the boss");
    }
    for (i = 0; i < nm; i++) {
        const marker *p = &mk[i];
        const gff_field *wl = gff_find(ai[p->area].git.root, "WaypointList"), *tl = gff_find(ai[p->area].git.root, "TriggerList");
        if (strchr("KR", p->kind)) continue;
        for (j = i + 1; j < nm; j++)
            if (mk[j].area == p->area && !strchr("KR", mk[j].kind) && absd((mk[j].cx - p->cx) * 10.0 + mk[j].ox - p->ox) < 2.0 &&
                absd((mk[j].cy - p->cy) * 10.0 + mk[j].oy - p->oy) < 2.0)
                BAD("%s: two markers within 2 m of each other at (%d,%d)", ai[p->area].name, p->cx, p->cy);
        for (q = 0; wl && q < wl->nlist; q++) {
            const char *t = gff_get_string(wl->list[q], "Tag");
            if (t && !strncmp(t, "SP", 2)) continue;
            if (cheb(p->cx, p->cy, (int)(gff_get_float(wl->list[q], "XPosition", -1) / 10.0), (int)(gff_get_float(wl->list[q], "YPosition", -1) / 10.0)) < 2)
                BAD("%s: marker %s_%s at (%d,%d) is next to waypoint %s", ai[p->area].name, p->kind == 'C' ? "SPC" : "SP?", p->bp, p->cx, p->cy, t ? t : "");
        }
        for (q = 0; tl && q < tl->nlist; q++)
            if (cheb(p->cx, p->cy, (int)(gff_get_float(tl->list[q], "XPosition", -1) / 10.0), (int)(gff_get_float(tl->list[q], "YPosition", -1) / 10.0)) < 2)
                BAD("%s: marker at (%d,%d) is next to trigger %s", ai[p->area].name, p->cx, p->cy, gff_get_string(tl->list[q], "Tag"));
        if (p->area == 0 && cheb(p->cx, p->cy, (int)(gff_get_float(ifo->root, "Mod_Entry_X", -1) / 10.0), (int)(gff_get_float(ifo->root, "Mod_Entry_Y", -1) / 10.0)) < (nweak ? 1 : 2))
            BAD("marker at (%d,%d) is next to the module entry", p->cx, p->cy);
        if (!on_floor(&ai[p->area], p->cx * 10.0 + 5.0, p->cy * 10.0 + 5.0) && !((p->kind == 'D' || p->kind == 'P') && on_corridor(&ai[p->area], p->cx * 10.0 + 5.0, p->cy * 10.0 + 5.0))) BAD("%s: marker at (%d,%d) is not on a floor cell", ai[p->area].name, p->cx, p->cy);
        if (p->kind == 'E' && (absd(absd(p->ox) - 3.5) > 0.02 || absd(absd(p->oy) - 3.5) > 0.02)) BAD("%s: lever at (%d,%d) is not on a corner spot of its cell", ai[p->area].name, p->cx, p->cy);
        if (p->kind == 'P') { /* floor trap: the middle of a full-floor cell or of a straight corridor cell */
            int sides = ai[p->area].open[p->cy * ai[p->area].w + p->cx] & 15;
            int corr = !on_floor(&ai[p->area], p->cx * 10.0 + 5.0, p->cy * 10.0 + 5.0);
            if (absd(absd(p->ox) - 3.5) > 0.02 && absd(p->ox) > 0.02) BAD("%s: trap at (%d,%d) is not at the middle or a corner spot of its cell", ai[p->area].name, p->cx, p->cy);
            else if (absd(absd(p->oy) - 3.5) > 0.02 && absd(p->oy) > 0.02) BAD("%s: trap at (%d,%d) is not at the middle or a corner spot of its cell", ai[p->area].name, p->cx, p->cy);
            else if (corr && (absd(p->ox) > 0.02 || absd(p->oy) > 0.02)) BAD("%s: corridor trap at (%d,%d) is off the middle of its cell", ai[p->area].name, p->cx, p->cy);
            if (!on_floor(&ai[p->area], p->cx * 10.0 + 5.0, p->cy * 10.0 + 5.0) && sides != 5 && sides != 10) BAD("%s: trap at (%d,%d) is on a corridor cell that is not straight", ai[p->area].name, p->cx, p->cy);
        }
        if (p->kind == 'D' && on_corridor(&ai[p->area], p->cx * 10.0 + 5.0, p->cy * 10.0 + 5.0)) { /* corridor props: a straight corridor cell, against a wall (1.6 m off the centre line), 0 or 2 m along it */
            int sides = ai[p->area].open[p->cy * ai[p->area].w + p->cx] & 15, vert = sides == 5;
            double lat = vert ? p->ox : p->oy, lon = vert ? p->oy : p->ox;
            if (sides != 5 && sides != 10) BAD("%s: corridor prop %s at (%d,%d) is not on a straight corridor cell", ai[p->area].name, p->bp, p->cx, p->cy);
            else if (absd(absd(lat) - 1.6) > 0.02 || (absd(lon) > 0.02 && absd(absd(lon) - 2.0) > 0.02)) BAD("%s: corridor prop %s at (%d,%d) is off its spot (offset %.2f, %.2f)", ai[p->area].name, p->bp, p->cx, p->cy, p->ox, p->oy);
        }
    }

    /* scenery must not cut the floor: with every centre-slot D/L/X tile removed the walkable area has no more regions than before
     * (a corner slot, 3.5 m off the tile centre, leaves the tile walkable) */
    for (a = 0; a < na; a++) {
        area_info tmp = ai[a];
        int any = 0;
        if (!ai[a].floor) continue;
        tmp.open = malloc((size_t)ai[a].w * (size_t)ai[a].h);
        memcpy(tmp.open, ai[a].open, (size_t)ai[a].w * (size_t)ai[a].h);
        for (i = 0; i < nm; i++)
            if (mk[i].area == (int)a && strchr("DLX", mk[i].kind) && absd(mk[i].ox) < 1.0 && absd(mk[i].oy) < 1.0 && mk[i].cx >= 0 && mk[i].cy >= 0 && mk[i].cx < tmp.w && mk[i].cy < tmp.h) {
                tmp.open[mk[i].cy * tmp.w + mk[i].cx] = 0;
                any = 1;
            }
        if (any && walk_components(&tmp, 1) != walk_components(&ai[a], 1)) BAD("%s: scenery splits the walkable area into several regions", ai[a].name);
        free(tmp.open);
    }
    {
        const char *esv = NULL;
        int64_t eiv = 0;
        enc_on = var_get(ifo->root, "scel_ec", &esv, &eiv) != 0;
    }
    check_locks(ifo, ai, na, mk, nm, ro);
    check_ambush(ai, na, mk, nm, enc_on);
    check_patrols(ai, na, mk, nm);
    /* roster consistency: one theme supplies every creature; objective items match */
    if (ro && boss && cnt['B'] == 1) {
        size_t t;
        int cr;
        for (t = 0; t < ro->ntheme; t++)
            if (in_roster_role(&ro->theme[t], ROLE_BOSS, boss->bp, &cr)) { theme = (int)t; break; }
        if (theme < 0) BAD("boss %s is not a boss in the roster", boss->bp);
        for (i = 0; theme >= 0 && i < nm; i++)
            if (mk[i].kind == 'C') {
                int role, f = 0;
                for (role = 0; role < ROLE_BOSS && !f; role++) f = in_roster_role(&ro->theme[theme], role, mk[i].bp, &cr);
                if (!f) BAD("%s: creature %s is not in theme '%s' of the roster", ai[mk[i].area].name, mk[i].bp, ro->theme[theme].id);
            }
        for (i = 0; theme >= 0 && i < nm; i++) {
            int kd = mk[i].kind == 'D' ? DECO_PROP : mk[i].kind == 'L' ? DECO_LIGHT : mk[i].kind == 'X' ? DECO_CONTAINER : -1, f = 0;
            size_t e;
            if (kd < 0) continue;
            for (e = 0; e < ro->theme[theme].ndeco[kd]; e++) f |= !strcmp(ro->theme[theme].deco[kd][e].resref, mk[i].bp);
            if (kd == DECO_PROP) { /* furniture from a roomprop line (recipe key dressing) is a decor prop too */
                int rt;
                for (rt = 0; rt < RT_N; rt++)
                    for (e = 0; e < ro->nrprop[rt]; e++) f |= !strcmp(ro->rprop[rt][e].resref, mk[i].bp);
                for (e = 0; e < ro->ncprop; e++) f |= !strcmp(ro->cprop[e].resref, mk[i].bp);
            }
            if (!f) BAD("%s: %s %s is not in the %s list of theme '%s'", ai[mk[i].area].name, DECO_NAME[kd], mk[i].bp, DECO_NAME[kd], ro->theme[theme].id);
        }
        /* loot: only creature and boss markers carry it, only from the theme's list and CR range; gold only if the theme gives gold */
        for (i = 0; theme >= 0 && i < nm; i++) {
            const roster_theme *th = &ro->theme[theme];
            int mcr = 0, role, q2;
            if (mk[i].kind != 'C' && mk[i].kind != 'B') {
                if (mk[i].loot[0][0] || mk[i].loot[1][0] || mk[i].gold) BAD("%s: %c marker %s carries loot", ai[mk[i].area].name, mk[i].kind, mk[i].bp);
                continue;
            }
            for (role = 0; role < ROLE_N; role++) if (in_roster_role(th, role, mk[i].bp, &mcr)) break;
            if (mk[i].gold && !th->gold_per_cr) BAD("%s: %s carries gold but theme '%s' has none", ai[mk[i].area].name, mk[i].bp, th->id);
            if (mk[i].gold < 0) BAD("%s: %s carries negative gold", ai[mk[i].area].name, mk[i].bp);
            if (mk[i].loot[1][0] && !mk[i].loot[0][0]) BAD("%s: %s has a second item but no first", ai[mk[i].area].name, mk[i].bp);
            if (mk[i].loot[0][0] && !strcmp(mk[i].loot[0], mk[i].loot[1]) && mk[i].kind == 'B') BAD("%s: boss %s carries the same item twice", ai[mk[i].area].name, mk[i].bp);
            for (q2 = 0; q2 < 2; q2++) {
                size_t e;
                int f = 0;
                if (!mk[i].loot[q2][0]) continue;
                for (e = 0; e < th->nloot; e++)
                    if (!strcmp(th->loot[e].resref, mk[i].loot[q2]) && mcr >= th->loot[e].min_cr && mcr <= th->loot[e].max_cr) f = 1;
                if (!f) BAD("%s: %s carries %s outside its loot list or CR range for theme '%s'", ai[mk[i].area].name, mk[i].bp, mk[i].loot[q2], th->id);
            }
        }
        if (key && strcmp(key->bp, ro->key)) BAD("key marker %s != roster key %s", key->bp, ro->key);
        if (vault && strcmp(vault->bp, ro->vault)) BAD("vault marker %s != roster vault %s", vault->bp, ro->vault);
        if (relic && strcmp(relic->bp, ro->relic)) BAD("relic marker %s != roster relic %s", relic->bp, ro->relic);
    }
    /* encounters (recipe key encounters): module variables scel_ec / scel_e1.. name the creatures that may turn up (the theme's minions and standard creatures),
     * scel_eg the group size, scel_ecap the encounters per area, scel_epct the chance when a player rests, scel_etick the quiet heartbeats; OnPlayerRest runs scel_rest */
    {
        const char *sv = NULL, *pr = gff_get_string(ifo->root, "Mod_OnPlayerRest");
        int64_t iv = 0, ne = 0;
        int ek, ty = var_get(ifo->root, "scel_ec", &sv, &iv);
        if (!!ty != !!(pr && *pr)) BAD("Mod_OnPlayerRest is '%s' but scel_ec is %s", pr ? pr : "", ty ? "set" : "absent");
        if (ty) {
            ne = iv;
            if (ty != 1 || ne < 1 || ne > 16) BAD("scel_ec must be an int 1-16 (got %lld)", (long long)ne);
            else {
                struct { const char *nm; int lo, hi; } NUM[4] = {{"scel_eg", 2, 4}, {"scel_ecap", 1, 8}, {"scel_epct", 1, 100}, {"scel_etick", 20, 1000}};
                for (ek = 0; ek < 4; ek++) {
                    ty = var_get(ifo->root, NUM[ek].nm, &sv, &iv);
                    if (ty != 1 || iv < NUM[ek].lo || iv > NUM[ek].hi) BAD("%s must be an int %d-%d", NUM[ek].nm, NUM[ek].lo, NUM[ek].hi);
                }
                for (ek = 1; ek <= (int)ne; ek++) {
                    char enm[24];
                    int role, f = 0;
                    snprintf(enm, sizeof enm, "scel_e%d", ek);
                    ty = var_get(ifo->root, enm, &sv, &iv);
                    if (ty != 3 || !sv || !resref_ok(sv)) { BAD("%s is missing or not a resref string", enm); continue; }
                    for (role = ROLE_MINION; ro && theme >= 0 && role <= ROLE_STANDARD && !f; role++) {
                        int ecr;
                        f = in_roster_role(&ro->theme[theme], role, sv, &ecr);
                    }
                    if (ro && theme >= 0 && !f) BAD("encounter creature %s is not a minion or standard creature of theme '%s'", sv, ro->theme[theme].id);
                }
            }
        }
    }
    /* henchmen: module variables scel_hn / scel_h1.. name stock henchman blueprints (<roster family>_NN, level NN+3) */
    {
        const char *sv = NULL;
        int64_t iv = 0, nh = 0;
        int hk, ty = var_get(ifo->root, "scel_hn", &sv, &iv);
        if (ty) {
            nh = iv;
            if (ty != 1 || nh < 1 || nh > 5) BAD("scel_hn must be an int 1-5 (got %lld)", (long long)nh);
            else {
                for (hk = 1; hk <= (int)nh; hk++) {
                    char hnm[24];
                    const char *bp;
                    int hj, fam = 0, d1, d2;
                    snprintf(hnm, sizeof hnm, "scel_h%d", hk);
                    ty = var_get(ifo->root, hnm, &sv, &iv);
                    if (ty != 3 || !sv || !resref_ok(sv)) { BAD("%s is missing or not a resref string", hnm); continue; }
                    bp = sv;
                    d1 = bp[strlen(bp) - 2] - '0';
                    d2 = bp[strlen(bp) - 1] - '0';
                    if (strlen(bp) < 4 || bp[strlen(bp) - 3] != '_' || d1 < 0 || d1 > 9 || d2 < 0 || d2 > 9 || d1 * 10 + d2 < 1 || d1 * 10 + d2 > 11)
                        BAD("henchman %s does not end in _01 .. _11", bp);
                    for (hj = 0; ro && hj < hk - 1; hj++) {
                        snprintf(hnm, sizeof hnm, "scel_h%d", hj + 1);
                        if (var_get(ifo->root, hnm, &sv, &iv) == 3 && !strcmp(sv, bp)) BAD("henchman %s listed twice", bp);
                    }
                    if (ro) {
                        size_t hq;
                        for (hq = 0; hq < ro->nhench; hq++)
                            if (!strncmp(bp, ro->hench[hq], strlen(ro->hench[hq])) && strlen(bp) == strlen(ro->hench[hq]) + 3) fam = 1;
                        if (!fam) BAD("henchman %s is not from a family in the roster", bp);
                    }
                    if (rm) {
                        uint8_t *ub;
                        size_t ul;
                        char e3[ERRLEN];
                        if (resman_get(rm, bp, 2027, &ub, &ul, e3) != 0) BAD("henchman %s does not resolve in the install", bp);
                        else free(ub);
                    }
                }
                printf("henchmen: %d\n", (int)nh);
            }
            if (!acq || strcmp(acq, "scel_acquire")) BAD("henchmen are set but the module has no spawn scripts");
        }
    }
    /* install: every blueprint resolves */
    if (rm)
        for (i = 0; i < nm; i++) {
            int cr = -1, role;
            if (ro && theme >= 0 && (mk[i].kind == 'C' || mk[i].kind == 'B'))
                for (role = 0; role < ROLE_N && cr < 0; role++) (void)in_roster_role(&ro->theme[theme], role, mk[i].bp, &cr);
            for (j = 0; j < i; j++) if (mk[j].kind == mk[i].kind && !strcmp(mk[j].bp, mk[i].bp)) break;
            if (j == i && mk[i].kind != 'P') check_blueprint(rm, ai[mk[i].area].name, mk[i].kind, mk[i].bp, cr);
            for (j = 0; j < 2; j++)
                if (mk[i].loot[j][0]) {
                    uint8_t *ub;
                    size_t ul;
                    char e3[ERRLEN];
                    if (resman_get(rm, mk[i].loot[j], 2025, &ub, &ul, e3) != 0) BAD("%s: loot item %s does not resolve in the install", ai[mk[i].area].name, mk[i].loot[j]);
                    else free(ub);
                }
        }
    /* install: the tile under every creature/boss/vault marker has a walkable centre (the marker is at the
     * tile centre, which rotation does not move) */
    if (rm) {
        unsigned char ok[512];
        uint8_t *sm = NULL;
        size_t sml;
        char e2[ERRLEN];
        if (resman_get(rm, "surfacemat", 2017, &sm, &sml, e2) != 0 || surfacemat_walkable((const char *)sm, ok, sizeof ok, e2) < 0)
            BAD("cannot read surfacemat.2da: %s", e2);
        else {
            for (i = 0; i < nm; i++) {
                const area_info *ar = &ai[mk[i].area];
                int tid;
                char wname[48];
                uint8_t *wb;
                size_t wl;
                wok w;
                if (strchr("KR", mk[i].kind) || !ar->tid || mk[i].cx < 0 || mk[i].cy < 0 || mk[i].cx >= ar->w || mk[i].cy >= ar->h) continue;
                tid = ar->tid[mk[i].cy * ar->w + mk[i].cx];
                if (tid < 0 || (size_t)tid >= ts->n || !*ts->t[tid].model) { BAD("%s: marker tile has no walkmesh", ar->name); continue; }
                { size_t c2; snprintf(wname, sizeof wname, "%s", ts->t[tid].model); for (c2 = 0; wname[c2]; c2++) if (wname[c2] >= 'A' && wname[c2] <= 'Z') wname[c2] = (char)(wname[c2] + 32); }
                if (resman_get(rm, wname, 2016, &wb, &wl, e2) != 0) { BAD("%s: walkmesh %s: %s", ar->name, wname, e2); continue; }
                if (wok_parse((const char *)wb, &w, e2) != 0) { BAD("%s: walkmesh %s: %s", ar->name, wname, e2); free(wb); continue; }
                {
                    /* the marker's offset from the tile centre, turned back into the tile's own frame (orientation is CCW) */
                    int o = ar->ori ? ar->ori[mk[i].cy * ar->w + mk[i].cx] : 0;
                    double lx = mk[i].ox, ly = mk[i].oy;
                    double tx = o == 0 ? lx : o == 1 ? ly : o == 2 ? -lx : -ly, ty = o == 0 ? ly : o == 1 ? -lx : o == 2 ? -ly : lx;
                    if (!wok_point_walkable(&w, ok, sizeof ok, tx, ty))
                        BAD("%s: marker %c_%s at (%d,%d) offset (%.1f,%.1f) stands on unwalkable ground (%s)", ar->name, mk[i].kind, mk[i].bp, mk[i].cx, mk[i].cy, mk[i].ox, mk[i].oy, wname);
                }
                wok_free(&w);
                free(wb);
            }
        }
        if (sm) free(sm);
    }
    if (nweak) printf("spawn: test mode, the boss has 1 hit point\n");
    printf("spawn: %d creature marker(s), %d decor, %d light, %d container, boss %s, vault %s%s%s\n", ncreat, ndecor, nlight, ncont, boss ? boss->bp : "-", vault ? vault->bp : "-",
           theme >= 0 ? ", theme " : "", theme >= 0 ? ro->theme[theme].id : "");
    {
        int nl = 0, ng = 0;
        for (i = 0; i < nm; i++) {
            nl += mk[i].loot[0][0] != 0;
            nl += mk[i].loot[1][0] != 0;
            ng += mk[i].gold > 0;
        }
        if (nl || ng) printf("loot: %d item(s) and gold on %d creature(s)\n", nl, ng);
    }
    free(mk);
}

static int usage(void)
{
    fputs("usage: dgcheck (--root INSTALL | --set TILESET.set) [--tileset NAME] [--roster ROSTER.txt] MODULE.mod\n", stderr);
    return 2;
}

int main(int argc, char **argv)
{
    const char *root = NULL, *setfile = NULL, *tsname = "tdc01", *modpath = NULL, *rosterfile = NULL;
    resman rm;
    int have_rm = 0;
    roster ro;
    int have_ro = 0;
    int i;
    tileset ts;
    erf m;
    gff ifo;
    area_info *ai = NULL;
    size_t na = 0, k;
    const gff_field *al;
    uint8_t *buf, *setbuf = NULL;
    size_t len;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--root") && i + 1 < argc) root = argv[++i];
        else if (!strcmp(argv[i], "--set") && i + 1 < argc) setfile = argv[++i];
        else if (!strcmp(argv[i], "--tileset") && i + 1 < argc) tsname = argv[++i];
        else if (!strcmp(argv[i], "--roster") && i + 1 < argc) rosterfile = argv[++i];
        else if (argv[i][0] != '-' && !modpath) modpath = argv[i];
        else return usage();
    }
    if (!modpath || (!root && !setfile)) return usage();

    if (setfile) {
        if (read_file(setfile, &setbuf, &len, err) != 0) { fprintf(stderr, "dgcheck: %s\n", err); return 1; }
        if (tileset_parse((const char *)setbuf, len, &ts, err) != 0) { fprintf(stderr, "dgcheck: %s\n", err); return 1; }
    } else {
        if (resman_open(&rm, root, err) != 0 || resman_get(&rm, tsname, 2013, &setbuf, &len, err) != 0 ||
            tileset_parse((const char *)setbuf, len, &ts, err) != 0) { fprintf(stderr, "dgcheck: %s\n", err); return 1; }
        have_rm = 1;
    }
    if (rosterfile) {
        uint8_t *rb;
        size_t rl;
        if (read_file(rosterfile, &rb, &rl, err) != 0 || roster_parse((const char *)rb, rl, &ro, err) != 0) {
            fprintf(stderr, "dgcheck: %s\n", err);
            return 1;
        }
        free(rb);
        have_ro = 1;
    }
    if (read_file(modpath, &buf, &len, err) != 0 || erf_parse(buf, len, &m, &GAME_NWN1, err) != 0) {
        fprintf(stderr, "dgcheck: %s\n", err);
        return 1;
    }
    if (load_gff(&m, "module", RES_IFO, &ifo) != 0) return 1;
    al = gff_find(ifo.root, "Mod_Area_list");
    if (!al || al->type != GFF_LIST || !al->nlist) { BAD("module.ifo lists no areas"); return 1; }
    ai = calloc(al->nlist, sizeof *ai);

    for (k = 0; k < al->nlist; k++) {
        const char *nm = gff_get_string(al->list[k], "Area_Name");
        area_info *a = &ai[na];
        gff are, gic;
        const gff_field *tl, *wl, *trl;
        int *tid, *ori, j, comps, nfloor;
        long viol;
        size_t q, ndoors;
        if (!nm) { BAD("Mod_Area_list entry %zu has no Area_Name", k); continue; }
        snprintf(a->name, sizeof a->name, "%.19s", nm);
        na++;
        memset(&are, 0, sizeof are);
        memset(&gic, 0, sizeof gic);
        if (load_gff(&m, a->name, RES_ARE, &are) || load_gff(&m, a->name, RES_GIT, &a->git) || load_gff(&m, a->name, RES_GIC, &gic)) {
            gff_free(&are);
            gff_free(&gic);
            continue;
        }
        snprintf(a->on_enter, sizeof a->on_enter, "%.39s", gff_get_string(are.root, "OnEnter") ? gff_get_string(are.root, "OnEnter") : "");
        snprintf(a->on_beat, sizeof a->on_beat, "%.39s", gff_get_string(are.root, "OnHeartbeat") ? gff_get_string(are.root, "OnHeartbeat") : "");
        a->w = (int)gff_get_int(are.root, "Width", 0);
        a->h = (int)gff_get_int(are.root, "Height", 0);
        if (strcmp(gff_get_string(are.root, "Tileset") ? gff_get_string(are.root, "Tileset") : "", tsname))
            BAD("%s: tileset is not %s", a->name, tsname);
        tl = gff_find(are.root, "Tile_List");
        if (!tl || tl->type != GFF_LIST || (int)tl->nlist != a->w * a->h || a->w <= 0 || a->h <= 0) {
            BAD("%s: Tile_List length %zu != Width*Height (%d*%d)", a->name, tl ? tl->nlist : 0, a->w, a->h);
            a->w = a->h = 0;
            gff_free(&are);
            gff_free(&gic);
            continue;
        }
        tid = malloc(tl->nlist * sizeof *tid);
        ori = malloc(tl->nlist * sizeof *ori);
        a->floor = calloc(tl->nlist, 1);
        a->open = calloc(tl->nlist, 1);
        for (q = 0; q < tl->nlist; q++) {
            tid[q] = (int)gff_get_int(tl->list[q], "Tile_ID", -1);
            ori[q] = (int)gff_get_int(tl->list[q], "Tile_Orientation", -1);
            if (tid[q] < 0 || (size_t)tid[q] >= ts.n || ori[q] < 0 || ori[q] > 3) {
                BAD("%s: tile %zu has invalid id/orientation (%d/%d)", a->name, q, tid[q], ori[q]);
                tid[q] = 0; ori[q] = 0;
                continue;
            }
            {
                static const int EC[4][2] = {{C_NW, C_NE}, {C_NE, C_SE}, {C_SW, C_SE}, {C_NW, C_SW}}; /* T R B L */
                tile_sig_t sg;
                int c, f = 1;
                tile_sig(&ts.t[tid[q]], ori[q], 1, &sg);
                for (c = 0; c < 4; c++) f &= !strcmp(sg.corner[c], "Floor");
                a->floor[q] = (unsigned char)f;
                for (c = 0; c < 4; c++)
                    if ((!strcmp(sg.corner[EC[c][0]], "Floor") && !strcmp(sg.corner[EC[c][1]], "Floor")) || !strcmp(sg.edge[c], "Corridor") || !strcmp(sg.edge[c], "Doorway"))
                        a->open[q] |= (unsigned char)(1 << c);
                for (c = 0; c < 4; c++)
                    if (f || !strcmp(sg.edge[c], "Corridor") || !strcmp(sg.edge[c], "Doorway")) a->open[q] |= 16;
            }
        }
        viol = tileset_violations(&ts, a->w, a->h, tid, ori, ORD_ROW_BOTTOM, 1, 1);
        if (viol) BAD("%s: %ld tile adjacency violations", a->name, viol);
        floor_components(a, &nfloor);
        comps = walk_components(a, 0);
        if (comps != 1) BAD("%s: walkable tiles form %d regions (expected 1)", a->name, comps);
        ndoors = check_doors(a, tid, ori, &ts);

        /* GIT objects */
        wl = gff_find(a->git.root, "WaypointList");
        trl = gff_find(a->git.root, "TriggerList");
        for (q = 0; wl && q < wl->nlist; q++) {
            double x = gff_get_float(wl->list[q], "XPosition", -1), y = gff_get_float(wl->list[q], "YPosition", -1);
            size_t t2;
            {
                const char *wt = gff_get_string(wl->list[q], "Tag");
                if (!on_floor(a, x, y) && !(wt && (!strncmp(wt, "SPD_", 4) || !strncmp(wt, "SPP_", 4)) && on_corridor(a, x, y))) BAD("%s: waypoint %s is not on a floor cell", a->name, wt);
            }
            for (t2 = 0; trl && t2 < trl->nlist; t2++)
                if (in_poly(trl->list[t2], x, y)) BAD("%s: waypoint %s lies inside trigger %s", a->name, gff_get_string(wl->list[q], "Tag"), gff_get_string(trl->list[t2], "Tag"));
        }
        for (q = 0; trl && q < trl->nlist; q++) {
            double x = gff_get_float(trl->list[q], "XPosition", -1), y = gff_get_float(trl->list[q], "YPosition", -1);
            if (!on_floor(a, x, y)) BAD("%s: trigger %s is not on a floor cell", a->name, gff_get_string(trl->list[q], "Tag"));
        }
        /* GIC mirrors GIT */
        for (j = 0; j < 9; j++) {
            const gff_field *g1 = gff_find(a->git.root, GIT_LISTS[j]), *g2 = gff_find(gic.root, GIT_LISTS[j]);
            size_t n1 = g1 && g1->type == GFF_LIST ? g1->nlist : 0, n2 = g2 && g2->type == GFF_LIST ? g2->nlist : 0;
            if (n1 != n2) { BAD("%s: GIC %s has %zu entries, GIT has %zu", a->name, GIT_LISTS[j], n2, n1); continue; }
            for (q = 0; q < n1; q++) if (g1->list[q]->id != g2->list[q]->id) BAD("%s: GIC %s[%zu] struct id differs", a->name, GIT_LISTS[j], q);
        }
        printf("area %-10s %2dx%-2d  adjacency violations %ld, %d full-floor cells, %d walkable region(s), %zu doors, %zu waypoints, %zu triggers\n", a->name,
               a->w, a->h, viol, nfloor, comps, ndoors, wl ? wl->nlist : 0, trl ? trl->nlist : 0);
        a->tid = tid; a->ori = ori;
        gff_free(&are); gff_free(&gic);
    }

    /* transitions resolve to waypoints in a different area */
    for (k = 0; k < na; k++) {
        const gff_field *trl = ai[k].git.root ? gff_find(ai[k].git.root, "TriggerList") : NULL;
        size_t q, o;
        for (q = 0; trl && q < trl->nlist; q++) {
            const char *link = gff_get_string(trl->list[q], "LinkedTo");
            int found = 0;
            if (gff_get_int(trl->list[q], "LinkedToFlags", 0) != 2) BAD("%s: trigger %s is not linked to a waypoint (flags)", ai[k].name, gff_get_string(trl->list[q], "Tag"));
            for (o = 0; link && o < na; o++) {
                const gff_field *wl = o != k && ai[o].git.root ? gff_find(ai[o].git.root, "WaypointList") : NULL;
                size_t w;
                for (w = 0; wl && w < wl->nlist; w++)
                    if (!strcmp(gff_get_string(wl->list[w], "Tag") ? gff_get_string(wl->list[w], "Tag") : "", link)) found++;
            }
            if (found != 1) BAD("%s: trigger %s links to '%s' which matches %d waypoints in other areas", ai[k].name, gff_get_string(trl->list[q], "Tag"), link ? link : "", found);
        }
    }
    /* module entry */
    {
        const char *ea = gff_get_string(ifo.root, "Mod_Entry_Area");
        double x = gff_get_float(ifo.root, "Mod_Entry_X", -1), y = gff_get_float(ifo.root, "Mod_Entry_Y", -1);
        for (k = 0; k < na && (!ea || strcmp(ai[k].name, ea)); k++) ;
        if (k == na) BAD("module entry area '%s' is not in Mod_Area_list", ea ? ea : "");
        else if (ai[k].floor && !on_floor(&ai[k], x, y)) BAD("module entry position (%.1f, %.1f) is not on a floor cell of %s", x, y, ea);
    }
    check_floor_tiles(ai, na, &ts, have_rm ? &rm : NULL);
    check_spawn(&m, &ifo, ai, na, &ts, have_rm ? &rm : NULL, have_ro ? &ro : NULL);
    if (problems)
        printf("dgcheck: %d problem(s)\n", problems);
    else
        printf("dgcheck: ok (%zu areas)\n", na);
    for (k = 0; k < na; k++) {
        free(ai[k].floor);
        free(ai[k].tid);
        free(ai[k].ori);
        free(ai[k].open);
        gff_free(&ai[k].git);
    }
    free(ai);
    gff_free(&ifo);
    if (have_rm) resman_close(&rm);
    erf_free(&m);
    tileset_free(&ts);
    free(setbuf);
    return fflush(stdout) ? 1 : (problems ? 1 : 0);
}
