/* dgdump RECIPE SEED BASE_IFO.gff|-  : print the canonical layout/tile/portal dump.
 * Used by the tests to compare this generator with tests/data/expected_*.txt. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "dungeon.h"
#include "sha256.h"
#include "util.h"

static char *dir_of(const char *path)
{
    const char *s = strrchr(path, '/');
    char *d;
    size_t n;
    if (!s)
        return xstrdup("");
    n = s == path ? 1 : (size_t)(s - path);
    d = xmalloc(n + 1);
    memcpy(d, path, n);
    d[n] = '\0';
    return d;
}

int main(int argc, char **argv)
{
    char err[ERRLEN] = "";
    uint8_t *rb = NULL, *pb = NULL, *ab = NULL, *gb = NULL, *ib = NULL;
    size_t rl, pl, al, gl, il;
    recipe r;
    tilepack tp;
    gff tare, tgit, ifo;
    dungeon d;
    uint64_t seed;
    char *dir, *dump;

    if (argc != 4 || parse_u64(argv[2], &seed) != 0) {
        fputs("usage: dgdump RECIPE SEED BASE_IFO\n", stderr);
        return 2;
    }
    dir = dir_of(argv[1]);
    if (read_file(argv[1], &rb, &rl, err) || recipe_parse((const char *)rb, rl, dir, &r, err) ||
        read_file(r.tilepack, &pb, &pl, err) || tilepack_parse((const char *)pb, pl, &tp, err) ||
        read_file(r.template_are, &ab, &al, err) || gff_parse(ab, al, &tare, err) ||
        read_file(r.template_git, &gb, &gl, err) || gff_parse(gb, gl, &tgit, err) ||
        read_file(argv[3], &ib, &il, err) || gff_parse(ib, il, &ifo, err) ||
        dungeon_build(&r, &tp, &tare, &tgit, &ifo, seed, &d, err)) {
        fprintf(stderr, "dgdump: %s\n", err);
        return 1;
    }
    dump = dungeon_dump(&d, seed);
    fputs(dump, stdout);
    free(dump);
    dungeon_free(&d);
    gff_free(&ifo);
    gff_free(&tgit);
    gff_free(&tare);
    tilepack_free(&tp);
    free(rb); free(pb); free(ab); free(gb); free(ib);
    free(dir);
    return fflush(stdout) ? 1 : 0;
}
