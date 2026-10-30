/* SplitMix64 (Steele, Lea, Flood) with unbiased bounded selection.
 * Algorithm identifier: "splitmix64/1". Changing any behavior here is an
 * algorithm-version change (see ALGORITHM_VERSION in plan.h). */
#ifndef NWNDGEN_PRNG_H
#define NWNDGEN_PRNG_H

#include <stdint.h>

typedef struct {
    uint64_t s;
} sm64;

void sm64_seed(sm64 *r, uint64_t seed);
uint64_t sm64_next(sm64 *r);
/* Uniform value in [0, n) by rejection sampling. n must be > 0. */
uint64_t sm64_bounded(sm64 *r, uint64_t n);

#endif
