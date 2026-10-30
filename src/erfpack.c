/* erfpack - pack a directory into a canonical (deterministic) ERF archive.
 * usage: erfpack nwn1|nwn2 MOD|ERF|HAK OUT.mod DIR
 * Resource name/type come from each file's name ("module.ifo"); files with an
 * unknown extension are an error. Output is byte-identical for identical input. */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "erf.h"
#include "game.h"
#include "util.h"

int main(int argc, char **argv)
{
    const game_profile *g;
    char err[ERRLEN] = "";
    erf e;
    DIR *d;
    struct dirent *de;
    uint8_t *out;
    size_t olen;
    char *tmp;

    if (argc != 5 || !(g = game_by_name(argv[1])) ||
        (strcmp(argv[2], "MOD") && strcmp(argv[2], "ERF") && strcmp(argv[2], "HAK"))) {
        fputs("usage: erfpack nwn1|nwn2 MOD|ERF|HAK OUT DIR\n", stderr);
        return 2;
    }
    {
        char sig[5];
        snprintf(sig, sizeof sig, "%-4s", argv[2]);
        erf_init(&e, sig, g);
    }
    e.desc_strref = 0xFFFFFFFFu;
    d = opendir(argv[4]);
    if (!d) {
        fprintf(stderr, "erfpack: cannot open '%s'\n", argv[4]);
        return 1;
    }
    while ((de = readdir(d))) {
        char *dot, *path, *name;
        uint8_t *buf;
        size_t len;
        int type;
        if (de->d_name[0] == '.')
            continue;
        name = xstrdup(de->d_name);
        dot = strrchr(name, '.');
        if (!dot || (type = res_type_by_ext(dot + 1)) < 0) {
            fprintf(stderr, "erfpack: '%s' has no known resource extension\n", de->d_name);
            return 1;
        }
        *dot = '\0';
        path = xmalloc(strlen(argv[4]) + strlen(de->d_name) + 2);
        sprintf(path, "%s/%s", argv[4], de->d_name);
        if (read_file(path, &buf, &len, err) != 0 ||
            erf_add(&e, name, (uint16_t)type, buf, (uint32_t)len, err) != 0) {
            fprintf(stderr, "erfpack: %s\n", err);
            return 1;
        }
        free(path);
        free(name);
    }
    closedir(d);
    if (erf_write(&e, &out, &olen, err) != 0 ||
        write_temp(argv[3], out, olen, &tmp, err) != 0 ||
        publish_temp(tmp, argv[3], 1, err) != 0) {
        fprintf(stderr, "erfpack: %s\n", err);
        return 1;
    }
    printf("packed %zu resources into %s (%zu bytes)\n", e.nres, argv[3], olen);
    return 0;
}
