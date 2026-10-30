/* GFF V3.2 reader / tree editor / canonical writer (module.ifo, ARE, GIT, GIC ...).
 * Values are held in memory as a tree; the writer lays tables out deterministically
 * (breadth-first struct order, first-use label order), independent of input layout. */
#ifndef NWNDGEN_GFF_H
#define NWNDGEN_GFF_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    GFF_BYTE = 0, GFF_CHAR, GFF_WORD, GFF_SHORT, GFF_DWORD, GFF_INT,
    GFF_DWORD64, GFF_INT64, GFF_FLOAT, GFF_DOUBLE, GFF_STRING, GFF_RESREF,
    GFF_LOCSTR, GFF_VOID, GFF_STRUCT, GFF_LIST
} gff_type;

typedef struct gff_struct gff_struct;

typedef struct {
    char label[17];
    gff_type type;
    uint64_t num;            /* integer types (sign-extended for signed), or float bits */
    uint8_t *bytes;          /* STRING / RESREF text, VOID data, LOCSTR blob (strref,count,entries) */
    size_t blen;
    gff_struct *st;          /* STRUCT */
    gff_struct **list;       /* LIST */
    size_t nlist, caplist;
} gff_field;

struct gff_struct {
    uint32_t id;
    gff_field *f;
    size_t nf, cap;
};

typedef struct {
    char type[5];            /* "ARE ", "GIT ", "IFO " ... */
    gff_struct *root;
} gff;

#define GFF_MAX_BYTES ((size_t)256u << 20)

int gff_parse(const uint8_t *buf, size_t len, gff *g, char *err);
int gff_write(const gff *g, uint8_t **out, size_t *outlen, char *err);
void gff_free(gff *g);
gff_struct *gff_struct_new(uint32_t id);
void gff_struct_free(gff_struct *s);
gff_struct *gff_struct_clone(const gff_struct *s);

gff_field *gff_find(const gff_struct *s, const char *label);
/* Setters create the field if missing; an existing field must already have the same type. */
int gff_set_int(gff_struct *s, const char *label, gff_type t, int64_t v, char *err);
int gff_set_float(gff_struct *s, const char *label, float v, char *err);
int gff_set_string(gff_struct *s, const char *label, gff_type t, const char *text, char *err); /* STRING|RESREF */
int gff_set_locstr(gff_struct *s, const char *label, const char *text, char *err);             /* language 0; NULL text = no entries */
int gff_set_void(gff_struct *s, const char *label, const uint8_t *data, size_t n, char *err);
int gff_remove(gff_struct *s, const char *label); /* 1 if a field was removed */
gff_struct *gff_set_struct(gff_struct *s, const char *label, uint32_t id, char *err); /* creates if missing */
gff_field *gff_set_list(gff_struct *s, const char *label, char *err);  /* creates/clears list */
void gff_list_append(gff_field *list, gff_struct *child);               /* takes ownership */
const char *gff_get_string(const gff_struct *s, const char *label);    /* NUL-terminated copy slot or NULL */
double gff_get_float(const gff_struct *s, const char *label, double dflt);
int64_t gff_get_int(const gff_struct *s, const char *label, int64_t dflt);

#endif
