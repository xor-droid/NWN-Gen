/* Read-only access to the base-game resource files (data key and bif files) of an NWN:EE install.
 * Authoring-time only (used by tools/nwnpack); the generator itself never touches an install. */
#ifndef NWNDGEN_KEY_H
#define NWNDGEN_KEY_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t *buf;          /* whole .key file */
    size_t len;
    uint32_t nbif, nkey, key_off;
    char (*bif)[512];      /* bif paths, '/' separators, relative to the install root */
} keyfile;

typedef struct {
    char root[512];
    keyfile *k;
    size_t nk;
} resman;

/* Mounts every <root>/data key files (sorted by name). */
int resman_open(resman *r, const char *root, char *err);
void resman_close(resman *r);
/* Fetch a resource by lowercase name and numeric type. Returns malloc'd bytes. If several key files hold
 * it with different bytes the lookup fails rather than guessing which one the game would use. */
int resman_get(resman *r, const char *name, uint16_t type, uint8_t **buf, size_t *len, char *err);


/* Visit every resource name of one type across all key files (in key-file order, then key order). */
typedef void (*resman_visit)(const char *name, uint16_t type, void *ctx);
void resman_each(const resman *r, uint16_t type, resman_visit fn, void *ctx);

#endif
