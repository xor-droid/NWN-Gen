#include "util.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static size_t g_used;

void set_err(char *err, const char *fmt, ...)
{
    va_list ap;
    if (!err)
        return;
    va_start(ap, fmt);
    vsnprintf(err, ERRLEN, fmt, ap);
    va_end(ap);
}

static void oom(void)
{
    fputs("nwndgen: out of memory\n", stderr);
    exit(1);
}

void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p)
        oom();
    return p;
}

void *xcalloc(size_t n, size_t m)
{
    void *p = calloc(n ? n : 1, m ? m : 1);
    if (!p)
        oom();
    return p;
}

void *xrealloc(void *p, size_t n)
{
    p = realloc(p, n ? n : 1);
    if (!p)
        oom();
    return p;
}

char *xstrdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = xmalloc(n);
    memcpy(p, s, n);
    return p;
}

int mem_reserve(size_t n, char *err)
{
    if (n > MAX_FILE_BYTES) {
        set_err(err, "buffer of %zu bytes exceeds the %zu byte per-file limit",
                n, (size_t)MAX_FILE_BYTES);
        return -1;
    }
    if (n > MAX_TOTAL_BYTES - g_used) {
        set_err(err, "aggregate memory limit of %zu bytes exceeded",
                (size_t)MAX_TOTAL_BYTES);
        return -1;
    }
    g_used += n;
    return 0;
}

void mem_reset(void)
{
    g_used = 0;
}

int read_file(const char *path, uint8_t **buf, size_t *len, char *err)
{
    FILE *f = fopen(path, "rb");
    struct stat st;
    uint8_t *b;
    size_t n;
    char probe;

    *buf = NULL;
    *len = 0;
    if (!f) {
        set_err(err, "cannot open '%s': %s", path, strerror(errno));
        return -1;
    }
    if (fstat(fileno(f), &st) != 0 || !S_ISREG(st.st_mode)) {
        set_err(err, "'%s' is not a regular file", path);
        fclose(f);
        return -1;
    }
    if (st.st_size < 0 || (uint64_t)st.st_size > MAX_FILE_BYTES) {
        set_err(err, "'%s' exceeds the %zu byte input limit", path,
                (size_t)MAX_FILE_BYTES);
        fclose(f);
        return -1;
    }
    n = (size_t)st.st_size;
    if (mem_reserve(n, err) != 0) {
        fclose(f);
        return -1;
    }
    b = xmalloc(n);
    if (fread(b, 1, n, f) != n || fread(&probe, 1, 1, f) != 0) {
        set_err(err, "short or changing read of '%s'", path);
        free(b);
        fclose(f);
        return -1;
    }
    fclose(f);
    *buf = b;
    *len = n;
    return 0;
}

int write_temp(const char *final_path, const uint8_t *data, size_t len,
               char **tmp_out, char *err)
{
    const char *slash = strrchr(final_path, '/');
    size_t dlen = slash ? (size_t)(slash - final_path) : 0;
    char *tmpl;
    int fd;
    size_t off = 0;
    mode_t um;

    if (slash && dlen == 0)
        dlen = 1; /* "/name" -> "/" */
    tmpl = xmalloc(dlen + 32);
    if (slash)
        memcpy(tmpl, final_path, dlen);
    else {
        tmpl[0] = '.';
        dlen = 1;
    }
    if (tmpl[dlen - 1] == '/')
        dlen--;
    strcpy(tmpl + dlen, "/.nwndgen-XXXXXX");

    fd = mkstemp(tmpl);
    if (fd < 0) {
        set_err(err, "cannot create temp file for '%s': %s", final_path,
                strerror(errno));
        free(tmpl);
        return -1;
    }
    while (off < len) {
        ssize_t w = write(fd, data + off, len - off);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            set_err(err, "write failed for '%s': %s", final_path,
                    strerror(errno));
            goto fail;
        }
        off += (size_t)w;
    }
    um = umask(0);
    umask(um);
    if (fchmod(fd, 0666 & ~um) != 0 || fsync(fd) != 0) {
        set_err(err, "cannot finalize temp file for '%s': %s", final_path,
                strerror(errno));
        goto fail;
    }
    if (close(fd) != 0) {
        set_err(err, "close failed for '%s': %s", final_path, strerror(errno));
        unlink(tmpl);
        free(tmpl);
        return -1;
    }
    *tmp_out = tmpl;
    return 0;
fail:
    close(fd);
    unlink(tmpl);
    free(tmpl);
    return -1;
}

int publish_temp(const char *tmp, const char *final_path, int force, char *err)
{
    struct stat st;

    if (force) {
        if (rename(tmp, final_path) != 0) {
            set_err(err, "cannot write '%s': %s", final_path, strerror(errno));
            unlink(tmp);
            return -1;
        }
        return 0;
    }
    if (link(tmp, final_path) == 0) {
        unlink(tmp);
        return 0;
    }
    if (errno == EEXIST) {
        set_err(err, "'%s' already exists (use --force to replace it)",
                final_path);
        unlink(tmp);
        return -1;
    }
    /* link() unsupported on this filesystem: fall back to check + rename. */
    if (lstat(final_path, &st) == 0) {
        set_err(err, "'%s' already exists (use --force to replace it)",
                final_path);
        unlink(tmp);
        return -1;
    }
    if (rename(tmp, final_path) != 0) {
        set_err(err, "cannot write '%s': %s", final_path, strerror(errno));
        unlink(tmp);
        return -1;
    }
    return 0;
}

void hex_encode(const uint8_t *in, size_t n, char *out)
{
    static const char d[] = "0123456789abcdef";
    size_t i;
    for (i = 0; i < n; i++) {
        out[2 * i] = d[in[i] >> 4];
        out[2 * i + 1] = d[in[i] & 15];
    }
    out[2 * n] = '\0';
}

uint32_t rd32le(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
           (uint32_t)p[3] << 24;
}

uint16_t rd16le(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

void wr32le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

void wr16le(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}
