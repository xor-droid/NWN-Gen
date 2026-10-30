/* gffrt - read a GFF V3.2 file with the strict reader and rewrite it canonically.
 * usage: gffrt IN OUT   (validation helper; compare with nwn_gff JSON) */
#include <stdio.h>
#include <stdlib.h>

#include "gff.h"
#include "util.h"

int main(int argc, char **argv)
{
    uint8_t *b, *o;
    size_t n, on;
    char err[ERRLEN] = "", *tmp;
    gff g;
    if (argc != 3) { fputs("usage: gffrt IN OUT\n", stderr); return 2; }
    if (read_file(argv[1], &b, &n, err) || gff_parse(b, n, &g, err) || gff_write(&g, &o, &on, err) ||
        write_temp(argv[2], o, on, &tmp, err) || publish_temp(tmp, argv[2], 1, err)) {
        fprintf(stderr, "gffrt: %s\n", err);
        return 1;
    }
    return 0;
}
