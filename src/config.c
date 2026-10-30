#include "config.h"

#include "util.h"

void config_defaults(config *c)
{
    c->seed = 0;
    c->party_size = 1;
    c->min_level = 1;
    c->max_level = 2;
}

int parse_u64(const char *s, uint64_t *out)
{
    uint64_t v = 0;
    const char *p = s;
    if (!*p)
        return -1;
    for (; *p; p++) {
        unsigned d;
        if (*p < '0' || *p > '9')
            return -1;
        d = (unsigned)(*p - '0');
        if (v > (UINT64_MAX - d) / 10)
            return -1;
        v = v * 10 + d;
    }
    *out = v;
    return 0;
}

int parse_uint_in(const char *s, uint32_t lo, uint32_t hi, uint32_t *out)
{
    uint64_t v;
    if (parse_u64(s, &v) != 0 || v < lo || v > hi)
        return -1;
    *out = (uint32_t)v;
    return 0;
}

int config_validate(const config *c, char *err)
{
    if (c->party_size < 1 || c->party_size > MAX_PARTY_LIMIT) {
        set_err(err, "party size must be between 1 and %u", MAX_PARTY_LIMIT);
        return -1;
    }
    if (c->min_level < MIN_LEVEL_LIMIT || c->max_level > MAX_LEVEL_LIMIT) {
        set_err(err, "levels must be between %u and %u", MIN_LEVEL_LIMIT,
                MAX_LEVEL_LIMIT);
        return -1;
    }
    if (c->min_level > c->max_level) {
        set_err(err, "minimum level %u exceeds maximum level %u",
                (unsigned)c->min_level, (unsigned)c->max_level);
        return -1;
    }
    return 0;
}
