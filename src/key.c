#include "key.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

#define KEY_HDR 64
#define KEY_ENTRY 22
#define BIF_VAR 16

static int cmp_str(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

static int load_key(keyfile *k, const char *path, char *err)
{
    uint32_t i, ft_off;
    memset(k, 0, sizeof *k);
    if (read_file(path, &k->buf, &k->len, err) != 0)
        return -1;
    if (k->len < KEY_HDR || memcmp(k->buf, "KEY V1  ", 8)) {
        set_err(err, "'%s' is not a KEY V1 file", path);
        return -1;
    }
    k->nbif = rd32le(k->buf + 8);
    k->nkey = rd32le(k->buf + 12);
    ft_off = rd32le(k->buf + 16);
    k->key_off = rd32le(k->buf + 20);
    if ((uint64_t)ft_off + (uint64_t)k->nbif * 12 > k->len ||
        (uint64_t)k->key_off + (uint64_t)k->nkey * KEY_ENTRY > k->len || k->nbif > 4096) {
        set_err(err, "'%s': KEY tables out of range", path);
        return -1;
    }
    k->bif = xcalloc(k->nbif ? k->nbif : 1, sizeof *k->bif);
    for (i = 0; i < k->nbif; i++) {
        const uint8_t *e = k->buf + ft_off + (size_t)i * 12;
        uint32_t no = rd32le(e + 4), nl = rd16le(e + 8), j;
        if ((uint64_t)no + nl > k->len || nl >= sizeof k->bif[i]) {
            set_err(err, "'%s': bad BIF file name", path);
            return -1;
        }
        for (j = 0; j < nl && k->buf[no + j]; j++)
            k->bif[i][j] = k->buf[no + j] == '\\' ? '/' : (char)k->buf[no + j];
    }
    return 0;
}

int resman_open(resman *r, const char *root, char *err)
{
    char dir[600], *names[64];
    size_t n = 0, i;
    DIR *d;
    struct dirent *de;

    memset(r, 0, sizeof *r);
    if (strlen(root) >= sizeof r->root - 1) {
        set_err(err, "install path too long");
        return -1;
    }
    strcpy(r->root, root);
    snprintf(dir, sizeof dir, "%s/data", root);
    d = opendir(dir);
    if (!d) {
        set_err(err, "cannot open '%s' (is this an NWN:EE install?)", dir);
        return -1;
    }
    while ((de = readdir(d)) && n < 64) {
        size_t l = strlen(de->d_name);
        if (l > 4 && !strcmp(de->d_name + l - 4, ".key"))
            names[n++] = xstrdup(de->d_name);
    }
    closedir(d);
    qsort(names, n, sizeof *names, cmp_str);
    if (!n) {
        set_err(err, "no .key files in '%s'", dir);
        return -1;
    }
    r->k = xcalloc(n, sizeof *r->k);
    for (i = 0; i < n; i++) {
        char path[700];
        snprintf(path, sizeof path, "%s/%s", dir, names[i]);
        free(names[i]);
        if (load_key(&r->k[r->nk], path, err) != 0) {
            r->nk++;
            for (i++; i < n; i++)
                free(names[i]);
            resman_close(r);
            return -1;
        }
        r->nk++;
    }
    return 0;
}

void resman_close(resman *r)
{
    size_t i;
    for (i = 0; i < r->nk; i++) {
        free(r->k[i].buf);
        free(r->k[i].bif);
    }
    free(r->k);
    memset(r, 0, sizeof *r);
}

static int read_bif(const resman *r, const keyfile *k, uint32_t resid, uint16_t type, uint8_t **out, size_t *len,
                    char *err)
{
    uint32_t bi = resid >> 20, idx = resid & 0xFFFFF, nvar, voff;
    char path[1300];
    uint8_t h[20], e[BIF_VAR];
    FILE *f;
    if (bi >= k->nbif) {
        set_err(err, "KEY refers to BIF %u of %u", bi, k->nbif);
        return -1;
    }
    snprintf(path, sizeof path, "%s/%s", r->root, k->bif[bi]);
    f = fopen(path, "rb");
    if (!f) {
        set_err(err, "cannot open '%s'", path);
        return -1;
    }
    if (fread(h, 1, sizeof h, f) != sizeof h || memcmp(h, "BIFFV1  ", 8)) {
        fclose(f);
        set_err(err, "'%s' is not a BIFF V1 file", path);
        return -1;
    }
    nvar = rd32le(h + 8);
    voff = rd32le(h + 16);
    if (idx >= nvar || fseek(f, (long)voff + (long)idx * BIF_VAR, SEEK_SET) != 0 || fread(e, 1, BIF_VAR, f) != BIF_VAR) {
        fclose(f);
        set_err(err, "'%s': resource %u out of range", path, idx);
        return -1;
    }
    {
        uint32_t off = rd32le(e + 4), size = rd32le(e + 8), t = rd32le(e + 12);
        uint8_t *b;
        if (t != type || size > MAX_FILE_BYTES || mem_reserve(size, err) != 0) {
            fclose(f);
            if (t != type)
                set_err(err, "'%s': resource type mismatch", path);
            return -1;
        }
        b = xmalloc((size_t)size + 1);
        if (fseek(f, (long)off, SEEK_SET) != 0 || fread(b, 1, size, f) != size) {
            free(b);
            fclose(f);
            set_err(err, "'%s': truncated resource", path);
            return -1;
        }
        b[size] = '\0'; /* text resources can be used as C strings */
        fclose(f);
        *out = b;
        *len = size;
    }
    return 0;
}

int resman_get(resman *r, const char *name, uint16_t type, uint8_t **buf, size_t *len, char *err)
{
    size_t ki;
    uint8_t *found = NULL;
    size_t flen = 0;
    char nm[17];
    size_t nl = strlen(name);

    if (nl > 16) {
        set_err(err, "resource name '%s' is longer than 16 characters", name);
        return -1;
    }
    memset(nm, 0, sizeof nm);
    memcpy(nm, name, nl);
    for (ki = 0; ki < r->nk; ki++) {
        const keyfile *k = &r->k[ki];
        uint32_t i;
        for (i = 0; i < k->nkey; i++) {
            const uint8_t *e = k->buf + k->key_off + (size_t)i * KEY_ENTRY;
            uint8_t *b;
            size_t bl;
            if (memcmp(e, nm, 16) || rd16le(e + 16) != type)
                continue;
            if (read_bif(r, k, rd32le(e + 18), type, &b, &bl, err) != 0) {
                free(found);
                return -1;
            }
            if (found && (flen != bl || memcmp(found, b, bl))) {
                free(b);
                free(found);
                set_err(err, "resource '%s' exists in several key files with different contents", name);
                return -1;
            }
            if (found)
                free(b);
            else {
                found = b;
                flen = bl;
            }
        }
    }
    if (!found) {
        set_err(err, "resource '%s' (type %u) not found in the install's key files", name, (unsigned)type);
        return -1;
    }
    *buf = found;
    *len = flen;
    return 0;
}

void resman_each(const resman *r, uint16_t type, resman_visit fn, void *ctx)
{
    size_t ki;
    for (ki = 0; ki < r->nk; ki++) {
        const keyfile *k = &r->k[ki];
        uint32_t i;
        for (i = 0; i < k->nkey; i++) {
            const uint8_t *e = k->buf + k->key_off + (size_t)i * KEY_ENTRY;
            char nm[17];
            if (rd16le(e + 16) != type)
                continue;
            memcpy(nm, e, 16);
            nm[16] = '\0';
            fn(nm, type, ctx);
        }
    }
}
