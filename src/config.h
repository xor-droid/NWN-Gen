/* Command-line configuration values and strict numeric parsing. */
#ifndef NWNDGEN_CONFIG_H
#define NWNDGEN_CONFIG_H

#include <stdint.h>

#define MIN_LEVEL_LIMIT 1u
#define MAX_LEVEL_LIMIT 40u
#define MAX_PARTY_LIMIT 32u

typedef struct {
    uint64_t seed;
    uint32_t party_size;
    uint32_t min_level;
    uint32_t max_level;
} config;

void config_defaults(config *c);
/* Strict unsigned decimal: digits only, no sign/space/overflow. */
int parse_u64(const char *s, uint64_t *out);
int parse_uint_in(const char *s, uint32_t lo, uint32_t hi, uint32_t *out);
int config_validate(const config *c, char *err);

#endif
