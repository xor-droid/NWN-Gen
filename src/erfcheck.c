/* erfcheck - parse real archives with the strict reader, re-serialize them
 * canonically and verify every payload survives. Diagnostic/validation tool.
 * usage: erfcheck nwn1|nwn2 FILE... */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "erf.h"
#include "game.h"
#include "util.h"

int main(int argc, char **argv)
{
    const game_profile *g;
    int i, bad = 0;

    if (argc < 3 || !(g = game_by_name(argv[1]))) {
        fputs("usage: erfcheck nwn1|nwn2 FILE...\n", stderr);
        return 2;
    }
    for (i = 2; i < argc; i++) {
        char err[ERRLEN] = "";
        uint8_t *buf, *out = NULL, *copy;
        size_t len, olen = 0, k;
        erf a, b;
        int ok = 0;

        mem_reset();
        if (read_file(argv[i], &buf, &len, err) != 0) {
            printf("FAIL  %s: %s\n", argv[i], err);
            bad++;
            continue;
        }
        if (erf_parse(buf, len, &a, g, err) != 0) {
            printf("FAIL  %s: parse: %s\n", argv[i], err);
            bad++;
            continue;
        }
        if (erf_write(&a, &out, &olen, err) != 0) {
            printf("FAIL  %s: write: %s\n", argv[i], err);
            erf_free(&a);
            bad++;
            continue;
        }
        copy = xmalloc(olen);
        memcpy(copy, out, olen);
        if (erf_parse(copy, olen, &b, g, err) != 0) {
            printf("FAIL  %s: reparse: %s\n", argv[i], err);
        } else if (a.nres != b.nres) {
            printf("FAIL  %s: resource count changed\n", argv[i]);
            erf_free(&b);
        } else {
            ok = 1;
            for (k = 0; k < a.nres; k++)
                if (strcmp(a.res[k].name, b.res[k].name) ||
                    a.res[k].type != b.res[k].type ||
                    a.res[k].size != b.res[k].size ||
                    memcmp(a.res[k].data, b.res[k].data, a.res[k].size)) {
                    printf("FAIL  %s: payload mismatch at %s\n", argv[i],
                           a.res[k].name);
                    ok = 0;
                    break;
                }
            erf_free(&b);
        }
        if (ok)
            printf("ok    %s (%s, %zu resources, %zu loc bytes)\n", argv[i],
                   a.sig, a.nres, (size_t)a.loc_size);
        else
            bad++;
        free(out);
        erf_free(&a);
    }
    return bad ? 1 : 0;
}
