/* Tileset model (.set), tile walkmeshes (ASCII .wok) and 2DA files for NWN1 tile areas.
 *
 * Compass names: corners NW,NE,SW,SE (set keys TopLeft,TopRight,BottomLeft,BottomRight);
 * edges N,E,S,W (set keys Top,Right,Bottom,Left). Orientation = counter-clockwise quarter turns and
 * Tile_List order = rows from the south, west to east: both verified against all stock areas by
 * `nwnpack check` (a hard gate), never assumed. */
#ifndef NWNDGEN_TILESET_H
#define NWNDGEN_TILESET_H

#include <stddef.h>

enum { C_NW, C_NE, C_SW, C_SE };
enum { E_N, E_E, E_S, E_W };

typedef struct {
    char model[48], walkmesh[48];
    char corner[4][16];   /* terrain names indexed by C_* */
    int cheight[4];
    char edge[4][24];     /* crosser names indexed by E_*, "" = none */
    int doors;
    struct { int type; double x, y, z, orient; } dslot[4];  /* [TILEnDOORm]: door type, tile-local position (m), facing (degrees) */
    int ndslot;
} set_tile;

typedef struct { set_tile *t; size_t n; } tileset;

int tileset_parse(const char *text, size_t len, tileset *ts, char *err);
void tileset_free(tileset *ts);

/* Effective corners/heights/edges of a tile after `o` quarter turns (ccw=1: counter-clockwise). */
typedef struct { char corner[4][16]; int cheight[4]; char edge[4][24]; } tile_sig_t;
void tile_sig(const set_tile *t, int o, int ccw, tile_sig_t *out);

typedef enum { ORD_ROW_BOTTOM, ORD_ROW_TOP, ORD_COL_LEFT, ORD_COL_RIGHT, ORD_COUNT } index_order;
extern const char *const INDEX_ORDER_NAME[ORD_COUNT];

/* Number of adjacency mismatches in one area (tile ids and orientations in Tile_List order). Out-of-range
 * tile ids count as one violation each. */
long tileset_violations(const tileset *ts, int w, int h, const int *tid, const int *orient, index_order ord, int ccw,
                        int use_heights);

/* ---- walkmesh / 2DA ---- */
typedef struct { double (*v)[3]; size_t nv; int (*f)[3]; int *mat; size_t nf; } wok;

int wok_parse(const char *text, wok *w, char *err);
void wok_free(wok *w);
/* Material ids whose Walk column is 1 in surfacemat.2da text. Returns count, or -1 with err. */
int surfacemat_walkable(const char *text, unsigned char *ok, size_t ok_len, char *err);
double wok_walk_area(const wok *w, const unsigned char *ok, size_t ok_len);
int wok_point_walkable(const wok *w, const unsigned char *ok, size_t ok_len, double x, double y);
/* 1 when every point lies on a walkable face and all of them are in one edge-connected walkable region. */
int wok_points_connected(const wok *w, const unsigned char *ok, size_t ok_len, const double (*pts)[2], size_t npts);

#endif
