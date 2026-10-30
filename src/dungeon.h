/* Procedural dungeon generator (NWN1 tile areas).
 *
 * Each area is a (W+1)x(H+1) lattice of wall/floor vertices; every cell is tiled from a
 * pool of stock-proven tiles keyed by its four corner labels (see assets/PACK.md). Areas
 * are chained by transition triggers. Everything is a pure function of (seed, recipe,
 * tilepack, templates); tests/data/expected_*.txt (from the retired Python reference model) pin
 * the layout algorithm. */
#ifndef NWNDGEN_DUNGEON_H
#define NWNDGEN_DUNGEON_H

#include <stddef.h>
#include <stdint.h>

#include "gff.h"
#include "prng.h"
#include "roster.h"
#include "recipe.h"

/* Bump when layout, tile choice, portal or object rules change. */
#define DUNGEON_ALGORITHM_VERSION "dungeon-chain/1"
#define LAT_MAX (RECIPE_AREA_MAX + 1)
#define LAYOUT_ATTEMPTS 200
#define LINK_MAX 200
#define DUNGEON_ALGORITHM_VERSION_KIT "dungeon-chain/2"

/* ---- tile pack ---- */
typedef struct { unsigned w; int tid, orient; } pack_tile;
typedef struct { pack_tile *t; size_t n; } pack_cell;       /* tiles for one corner pattern */
typedef struct { int tid; uint8_t v[7]; } pack_light;       /* main1 main2 src1 src2 anim1-3 */
/* Corridor-kit tile: effective corners (index as cell[]) and edges after rotation, mask bits T=1 R=2 B=4 L=8 (all Corridor). */
typedef struct {
    int sig, mask;
    int dmask;                    /* edges of mask that are Doorway edges (the doorway kit) rather than Corridor edges */
    unsigned w;
    int tid, orient;
    int has_door;
    double dx, dy, dz, bearing;   /* door slot, tile-local metres; bearing in radians */
    int app;                      /* door look: the Appearance (doortypes.2da row) of the slot, 22 unless the record says otherwise */
} pack_kit;

typedef struct {
    char tileset[17];
    pack_cell cell[16];       /* index: NW<<3 | NE<<2 | SW<<1 | SE, floor = 1 */
    pack_light *light;
    size_t nlight;
    pack_kit *kit;
    size_t nkit;
} tilepack;

int tilepack_parse(const char *text, size_t len, tilepack *p, char *err);
void tilepack_free(tilepack *p);

/* ---- layout ---- */
typedef struct {
    int w, h, nrooms, attempts;
    char lat[LAT_MAX][LAT_MAX];       /* lat[y][x], 'W' or 'F'; y grows north */
    int room[RECIPE_ROOMS_MAX][4];    /* vertex rect x0 y0 x1 y1, sorted by (x0,y0) */
    /* corridors kit only: a kit corridor cell has Corridor edges toward its path neighbours, a room-wall "connector" cell toward the
     * path that starts beside it. Bits T=1 R=2 B=4 L=8 (north = +y). link[i] lists the kit cells joining room i to room i+1. */
    int kit, nlink, link_n[RECIPE_ROOMS_MAX];
    unsigned char kedge[RECIPE_AREA_MAX][RECIPE_AREA_MAX];   /* [y][x] */
    unsigned char link[RECIPE_ROOMS_MAX][LINK_MAX][2];       /* cell x, y */
    unsigned char conn[RECIPE_ROOMS_MAX][2][3];              /* link i: room-wall connector cell at its start [0] and end [1]: x, y, side (0 E 1 N 2 W 3 S) */
} layout;

int layout_make(sm64 *rng, int w, int h, int nrooms, layout *out, char *err);
/* Corridor-kit variant: rooms keep a free cell between their wall rings and are joined by one-cell-wide kit corridors (no floor
 * outside rooms). Same errors as layout_make. */
int layout_make_kit(sm64 *rng, int w, int h, int nrooms, layout *out, char *err);
/* Two 4-adjacent full-floor cells in one room: out = {portal x,y, arrive x,y}. */
int layout_portal_pair(const layout *l, int room, sm64 *rng, int out[4], char *err);
/* 1 if all four corners of cell (x,y) are floor. */
int layout_full_floor(const layout *l, int x, int y);

/* ---- chain ---- */
uint64_t dungeon_area_seed(uint64_t seed, const char *resref);
void dungeon_module_id(uint64_t seed, const char *tag, uint8_t out[16]);

typedef struct { int tid, orient; } placed_tile;
typedef struct { int cx, cy, app, donor, gen; double x, y, z, bearing; } dungeon_door;   /* donor: index in the template's Door List, gen: its GenericType_New (-1 = not a generic door) */   /* world metres, bearing in radians */
typedef struct {
    char resref[17];
    layout lay;
    placed_tile *tiles;               /* w*h, rows from the south, west to east */
    int pair_start[4], pair_prev[4], pair_next[4];
    int has_start, has_prev, has_next;
    spawn_pt *sp;                     /* marker plan (empty unless spawning is on) */
    size_t nsp;
    dungeon_door *door;               /* doors in kit corridors (doors plain) */
    size_t ndoor;
    uint8_t *are, *git, *gic;         /* serialized GFF V3.2 */
    size_t nare, ngit, ngic;
} dungeon_area;

typedef struct {
    dungeon_area *area;
    size_t n;
    int has_spawn, theme;             /* roster in use and the theme picked for this seed */
    char hench[6][17];                /* henchmen that join the first player (resrefs), chosen by spawn_plan */
    int nhench;
    uint8_t *jrl;                     /* module.jrl when has_spawn */
    size_t njrl;
    uint8_t *ifo;                     /* edited module.ifo */
    size_t nifo;
} dungeon;

/* ifo_in: the base module's IFO. Templates are consumed read-only. */
int dungeon_build(const recipe *r, const tilepack *tp, const gff *tpl_are, const gff *tpl_git,
                  const gff *ifo_in, uint64_t seed, dungeon *out, char *err);
/* As dungeon_build, plus creature/objective markers when sc is non-NULL. */
int dungeon_build_ex(const recipe *r, const tilepack *tp, const gff *tpl_are, const gff *tpl_git,
                     const gff *ifo_in, uint64_t seed, const spawn_cfg *sc, dungeon *out, char *err);
void dungeon_free(dungeon *d);
/* Canonical text dump of layouts, tiles and portals (used to compare with the Python model). */
char *dungeon_dump(const dungeon *d, uint64_t seed);

#endif
