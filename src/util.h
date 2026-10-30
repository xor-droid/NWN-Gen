/* Shared helpers: error reporting, checked allocation, bounded file I/O. */
#ifndef NWNDGEN_UTIL_H
#define NWNDGEN_UTIL_H

#include <stddef.h>
#include <stdint.h>

#define ERRLEN 512
#define MAX_FILE_BYTES  ((size_t)1024u << 20)  /* any single input or output */
#define MAX_TOTAL_BYTES ((size_t)2048u << 20)  /* aggregate across the process */

#if defined(__GNUC__)
#define PRINTF_LIKE(a, b) __attribute__((format(printf, a, b)))
#else
#define PRINTF_LIKE(a, b)
#endif

void set_err(char *err, const char *fmt, ...) PRINTF_LIKE(2, 3);

void *xmalloc(size_t n);
void *xcalloc(size_t n, size_t m);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);

/* Aggregate memory budget for file-sized buffers. */
int mem_reserve(size_t n, char *err);
void mem_reset(void);

/* Read a regular file fully (<= MAX_FILE_BYTES, counted against the budget). */
int read_file(const char *path, uint8_t **buf, size_t *len, char *err);

/* Write data to a fresh temp file beside final_path (fsync'd, mode 0666&~umask).
 * On success *tmp_out is a malloc'd path the caller must publish or unlink. */
int write_temp(const char *final_path, const uint8_t *data, size_t len,
               char **tmp_out, char *err);

/* Move a temp file into place. Without force an existing target is an error.
 * The temp file is removed on failure. */
int publish_temp(const char *tmp, const char *final_path, int force, char *err);

void hex_encode(const uint8_t *in, size_t n, char *out); /* out: 2n+1 bytes */

uint32_t rd32le(const uint8_t *p);
uint16_t rd16le(const uint8_t *p);
void wr32le(uint8_t *p, uint32_t v);
void wr16le(uint8_t *p, uint16_t v);

#endif
