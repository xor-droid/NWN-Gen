#include "prng.h"

void sm64_seed(sm64 *r, uint64_t seed)
{
    r->s = seed;
}

uint64_t sm64_next(sm64 *r)
{
    uint64_t z = (r->s += UINT64_C(0x9E3779B97F4A7C15));
    z = (z ^ (z >> 30)) * UINT64_C(0xBF58476D1CE4E5B9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94D049BB133111EB);
    return z ^ (z >> 31);
}

uint64_t sm64_bounded(sm64 *r, uint64_t n)
{
    /* Reject the first (2^64 mod n) values so the remainder is uniform. */
    uint64_t threshold = (UINT64_C(0) - n) % n;
    uint64_t v;
    do {
        v = sm64_next(r);
    } while (v < threshold);
    return v % n;
}
