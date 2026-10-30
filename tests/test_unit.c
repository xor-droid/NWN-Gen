/* Unit tests: SHA-256/PRNG known answers, ERF reader/writer, catalog, config.
 * Known-answer values were computed with independent Python implementations. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "catalog.h"
#include "config.h"
#include "dgfix.h"
#include "dungeon.h"
#include "erf.h"
#include "gff.h"
#include "key.h"
#include "recipe.h"
#include "roster.h"
#include "spawn.h"
#include "tileset.h"
#include <sys/stat.h>
#include <unistd.h>
#include "plan.h"
#include "prng.h"
#include "sha256.h"
#include "util.h"

static int fails, checks;
#define CHECK(c)                                                           \
    do {                                                                   \
        checks++;                                                          \
        if (!(c)) {                                                        \
            fails++;                                                       \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);            \
        }                                                                  \
    } while (0)

static void hexof(const void *d, size_t n, char *out)
{
    uint8_t h[32];
    sha256(d, n, h);
    hex_encode(h, 32, out);
}

static void test_sha(void)
{
    char h[65];
    uint8_t *m = malloc(1000000);
    hexof("", 0, h);
    CHECK(!strcmp(h, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    hexof("abc", 3, h);
    CHECK(!strcmp(h, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    memset(m, 'a', 1000000);
    hexof(m, 1000000, h);
    CHECK(!strcmp(h, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
    free(m);
}

static void test_prng(void)
{
    static const struct { uint64_t seed, v[3]; } kat[] = {
        {0, {UINT64_C(0xe220a8397b1dcdaf), UINT64_C(0x6e789e6aa1b965f4), UINT64_C(0x06c45d188009454f)}},
        {1, {UINT64_C(0x910a2dec89025cc1), UINT64_C(0xbeeb8da1658eec67), UINT64_C(0xf893a2eefb32555e)}},
        {UINT64_MAX, {UINT64_C(0xe4d971771b652c20), UINT64_C(0xe99ff867dbf682c9), UINT64_C(0x382ff84cb27281e9)}},
    };
    size_t i;
    int j;
    sm64 r;
    unsigned counts[3] = {0, 0, 0};
    for (i = 0; i < sizeof kat / sizeof kat[0]; i++) {
        sm64_seed(&r, kat[i].seed);
        for (j = 0; j < 3; j++)
            CHECK(sm64_next(&r) == kat[i].v[j]);
    }
    sm64_seed(&r, 7);
    CHECK(sm64_bounded(&r, 1) == 0);
    for (j = 0; j < 30000; j++) {
        uint64_t v = sm64_bounded(&r, 3);
        CHECK(v < 3);
        counts[v]++;
    }
    for (j = 0; j < 3; j++)
        CHECK(counts[j] > 9500 && counts[j] < 10500);
    CHECK(sm64_bounded(&r, UINT64_MAX) < UINT64_MAX);
}

static void test_config(void)
{
    uint64_t v;
    uint32_t u;
    config c;
    char err[ERRLEN];
    CHECK(parse_u64("0", &v) == 0 && v == 0);
    CHECK(parse_u64("18446744073709551615", &v) == 0 && v == UINT64_MAX);
    CHECK(parse_u64("18446744073709551616", &v) != 0);
    CHECK(parse_u64("99999999999999999999999", &v) != 0);
    CHECK(parse_u64("", &v) != 0);
    CHECK(parse_u64("-1", &v) != 0);
    CHECK(parse_u64("+1", &v) != 0);
    CHECK(parse_u64(" 1", &v) != 0);
    CHECK(parse_u64("1x", &v) != 0);
    CHECK(parse_u64("0x10", &v) != 0);
    CHECK(parse_uint_in("40", 1, 40, &u) == 0 && u == 40);
    CHECK(parse_uint_in("41", 1, 40, &u) != 0);
    CHECK(parse_uint_in("0", 1, 40, &u) != 0);
    config_defaults(&c);
    CHECK(c.party_size == 1 && c.min_level == 1 && c.max_level == 2);
    CHECK(config_validate(&c, err) == 0);
    c.min_level = 3;
    CHECK(config_validate(&c, err) != 0);
    c.min_level = 1; c.max_level = 41;
    CHECK(config_validate(&c, err) != 0);
    c.max_level = 2; c.party_size = 0;
    CHECK(config_validate(&c, err) != 0);
}

/* Helpers to hand-build ERF bytes. */
static uint8_t *mk_erf(size_t *len, uint32_t n_claimed)
{
    static const char payload[] = "hello";
    uint8_t *b = calloc(1, 256);
    memcpy(b, "MOD V1.0", 8);
    wr32le(b + 16, n_claimed);
    wr32le(b + 20, 160);
    wr32le(b + 24, 160);
    wr32le(b + 28, 160 + 24);
    memcpy(b + 160, "module", 6);
    wr32le(b + 160 + 16, 0);
    wr16le(b + 160 + 20, RES_IFO);
    wr32le(b + 184, 192);
    wr32le(b + 188, 5);
    memcpy(b + 192, payload, 5);
    *len = 197;
    return b;
}

static int parse_copy_g(const uint8_t *b, size_t len, erf *e,
                        const game_profile *g, char *err)
{
    uint8_t *c = malloc(len ? len : 1);
    memcpy(c, b, len);
    return erf_parse(c, len, e, g, err);
}

static int parse_copy(const uint8_t *b, size_t len, erf *e, char *err)
{
    return parse_copy_g(b, len, e, &GAME_NWN1, err);
}

static void test_erf_parse(void)
{
    size_t len;
    uint8_t *b = mk_erf(&len, 1);
    erf e;
    char err[ERRLEN];
    const erf_res *r;

    CHECK(parse_copy(b, len, &e, err) == 0);
    r = erf_find(&e, "module", RES_IFO);
    CHECK(r && r->size == 5 && !memcmp(r->data, "hello", 5));
    CHECK(erf_find(&e, "module", RES_ARE) == NULL);
    erf_free(&e);

    CHECK(parse_copy(b, 100, &e, err) != 0);              /* truncated header */
    b[0] = 'X';
    CHECK(parse_copy(b, len, &e, err) != 0);              /* bad signature */
    b[0] = 'M';
    memcpy(b + 4, "V1.1", 4);
    CHECK(parse_copy(b, len, &e, err) != 0);              /* version */
    memcpy(b + 4, "E1.0", 4);
    CHECK(parse_copy(b, len, &e, err) != 0);
    memcpy(b + 4, "V1.0", 4);

    wr32le(b + 16, 0xFFFFFFFFu);                          /* huge count */
    CHECK(parse_copy(b, len, &e, err) != 0);
    wr32le(b + 16, 1);

    wr32le(b + 24, 0xFFFFFFF0u);                          /* key offset */
    CHECK(parse_copy(b, len, &e, err) != 0);
    wr32le(b + 24, 160);
    wr32le(b + 28, 0xFFFFFFFFu);                          /* res offset */
    CHECK(parse_copy(b, len, &e, err) != 0);
    wr32le(b + 28, 184);

    wr32le(b + 184, 0xFFFFFFFFu);                         /* data offset */
    CHECK(parse_copy(b, len, &e, err) != 0);
    wr32le(b + 184, 192);
    wr32le(b + 188, 0xFFFFFFFFu);                         /* overflowing size */
    CHECK(parse_copy(b, len, &e, err) != 0);
    wr32le(b + 188, 6);                                   /* past end */
    CHECK(parse_copy(b, len, &e, err) != 0);
    wr32le(b + 188, 5);

    wr32le(b + 160 + 16, 1);                              /* resid out of range */
    CHECK(parse_copy(b, len, &e, err) != 0);
    wr32le(b + 160 + 16, 0);

    b[160 + 8] = 'x';                                     /* bytes after NUL */
    CHECK(parse_copy(b, len, &e, err) != 0);
    b[160 + 8] = 0;
    b[160 + 2] = 0x01;                                    /* control character */
    CHECK(parse_copy(b, len, &e, err) != 0);
    b[160 + 2] = 'd';
    b[160] = 0;                                           /* empty name */
    CHECK(parse_copy(b, len, &e, err) != 0);
    b[160] = 'm';

    wr32le(b + 8, 1);                                     /* loc count w/o data */
    CHECK(parse_copy(b, len, &e, err) != 0);
    wr32le(b + 8, 0);
    wr32le(b + 12, 3);                                    /* stray loc bytes */
    CHECK(parse_copy(b, len, &e, err) != 0);
    wr32le(b + 12, 0);

    CHECK(parse_copy(b, len, &e, err) == 0);              /* restored: valid */
    erf_free(&e);
    free(b);
}

static void test_erf_dup_and_loc(void)
{
    /* two keys with the same name/type but different case -> duplicate */
    uint8_t b[400];
    erf e;
    char err[ERRLEN];
    int i;
    memset(b, 0, sizeof b);
    memcpy(b, "ERF V1.0", 8);
    wr32le(b + 16, 2);
    wr32le(b + 20, 160);
    wr32le(b + 24, 160);
    wr32le(b + 28, 160 + 48);
    for (i = 0; i < 2; i++) {
        memcpy(b + 160 + 24 * i, i ? "MODULE" : "module", 6);
        wr32le(b + 160 + 24 * i + 16, (uint32_t)i);
        wr16le(b + 160 + 24 * i + 20, RES_IFO);
        wr32le(b + 208 + 8 * i, 224);
        wr32le(b + 208 + 8 * i + 4, 1);
    }
    CHECK(parse_copy(b, 300, &e, err) != 0 && strstr(err, "duplicate"));
    /* reused resource ID */
    memcpy(b + 160 + 24, "other", 5);
    memset(b + 160 + 24 + 5, 0, 1);
    wr32le(b + 160 + 24 + 16, 0);
    CHECK(parse_copy(b, 300, &e, err) != 0);
}

static void test_erf_roundtrip(void)
{
    erf in, back;
    uint8_t *out1, *out2;
    size_t l1, l2;
    char err[ERRLEN];
    static const uint8_t loc[] = {0, 0, 0, 0, 3, 0, 0, 0, 'a', 'b', 'c'};
    erf in2;

    erf_init(&in, "MOD ", &GAME_NWN1);
    in.loc_count = 1;
    in.loc = loc;
    in.loc_size = sizeof loc;
    in.desc_strref = 77;
    CHECK(erf_add(&in, "ZED", RES_ARE, (const uint8_t *)"z", 1, err) == 0);
    CHECK(erf_add(&in, "alpha", RES_GIT, (const uint8_t *)"gg", 2, err) == 0);
    CHECK(erf_add(&in, "alpha", RES_ARE, (const uint8_t *)"aa", 2, err) == 0);
    CHECK(erf_add(&in, "bad\x01name", RES_ARE, (const uint8_t *)"", 0, err) != 0);
    CHECK(erf_add(&in, "waytoolongresourcename", RES_ARE, (const uint8_t *)"", 0, err) != 0);

    /* Same logical content, different insertion order -> identical bytes. */
    erf_init(&in2, "MOD ", &GAME_NWN1);
    in2.loc_count = 1; in2.loc = loc; in2.loc_size = sizeof loc; in2.desc_strref = 77;
    erf_add(&in2, "alpha", RES_ARE, (const uint8_t *)"aa", 2, err);
    erf_add(&in2, "zed", RES_ARE, (const uint8_t *)"z", 1, err);
    erf_add(&in2, "alpha", RES_GIT, (const uint8_t *)"gg", 2, err);

    CHECK(erf_write(&in, &out1, &l1, err) == 0);
    CHECK(erf_write(&in2, &out2, &l2, err) == 0);
    CHECK(l1 == l2 && !memcmp(out1, out2, l1));
    CHECK(rd32le(out1 + 32) == 0 && rd32le(out1 + 36) == 0); /* fixed metadata */
    CHECK(rd32le(out1 + 40) == 77);
    {
        size_t i;
        int zero = 1;
        for (i = 44; i < 160; i++)
            zero &= out1[i] == 0;
        CHECK(zero);
    }
    /* keys: alpha/are, alpha/git, zed/are with sequential IDs */
    CHECK(!memcmp(out1 + 160 + sizeof loc, "alpha", 5));
    CHECK(rd16le(out1 + 160 + sizeof loc + 20) == RES_ARE);
    CHECK(rd16le(out1 + 160 + sizeof loc + 24 + 20) == RES_GIT);
    CHECK(rd32le(out1 + 160 + sizeof loc + 48 + 16) == 2);

    CHECK(erf_parse(out1, l1, &back, &GAME_NWN1, err) == 0);
    CHECK(back.nres == 3 && back.desc_strref == 77 && back.loc_size == sizeof loc);
    CHECK(!memcmp(back.loc, loc, sizeof loc));
    CHECK(erf_find(&back, "alpha", RES_GIT)->size == 2);
    {   /* real-world resrefs: spaces and punctuation are legal */
        erf q;
        erf_init(&q, "MOD ", &GAME_NWN1);
        CHECK(erf_add(&q, "Large Fire", RES_ARE, (const uint8_t *)"", 0, err) == 0);
        CHECK(!strcmp(q.res[0].name, "large fire"));
        CHECK(erf_add(&q, "i'mbored", RES_ARE, (const uint8_t *)"", 0, err) == 0);
        CHECK(erf_add(&q, "con_x_!y", RES_ARE, (const uint8_t *)"", 0, err) == 0);
        CHECK(erf_add(&q, "\xe9" "x", RES_ARE, (const uint8_t *)"", 0, err) != 0);
        erf_free(&q);
    }
    {
        uint8_t *o3;
        size_t l3;
        CHECK(erf_write(&back, &o3, &l3, err) == 0 && l3 == l1 && !memcmp(o3, out2, l3));
        free(o3);
    }
    erf_free(&back);
    free(out2);

    /* duplicate in writer input rejected */
    erf_add(&in, "alpha", RES_ARE, (const uint8_t *)"dup", 3, err);
    CHECK(erf_write(&in, &out1, &l1, err) != 0);
    erf_free(&in);
    erf_free(&in2);
}

static void test_nwn2_and_gate(void)
{
    erf e, back, back2;
    uint8_t *o, *o2;
    size_t l, l2;
    char err[ERRLEN];
    const char *longname = "enc_a_cellar_room_with_long_nm"; /* 30 chars */

    erf_init(&e, "MOD ", &GAME_NWN2);
    CHECK(erf_add(&e, longname, RES_TRX, (const uint8_t *)"NWN2xxxxxxxxxxxx", 16, err) == 0);
    CHECK(erf_add(&e, longname, RES_ARE, (const uint8_t *)"a", 1, err) == 0);
    CHECK(erf_add(&e, "module", RES_IFO, (const uint8_t *)"i", 1, err) == 0);
    CHECK(erf_add(&e, "this_name_is_way_too_long_for_even_nwn2", RES_ARE, (const uint8_t *)"", 0, err) != 0);
    CHECK(erf_write(&e, &o, &l, err) == 0);
    CHECK(!memcmp(o, "MOD V1.1", 8));
    /* 160 header + 3 keys*40 + 3 res*8 + 18 data bytes */
    CHECK(l == 160 + 3 * 40 + 3 * 8 + 18);
    CHECK(rd32le(o + 28) == 160 + 3 * 40);              /* resource list offset */
    CHECK(!memcmp(o + 160, longname, 30));              /* sorted: enc_a... first */
    CHECK(rd32le(o + 160 + 32) == 0 && rd16le(o + 160 + 36) == RES_ARE);
    CHECK(rd16le(o + 160 + 40 + 36) == RES_TRX);

    o2 = malloc(l);
    memcpy(o2, o, l);
    CHECK(erf_parse(o2, l, &back, &GAME_NWN2, err) == 0);
    CHECK(back.nres == 3 && erf_find(&back, longname, RES_TRX) != NULL);
    CHECK(erf_write(&back, &o2, &l2, err) == 0 && l2 == l && !memcmp(o, o2, l));
    free(o2);
    erf_free(&back);

    /* The gate: each game's reader refuses the other game's archive. */
    CHECK(parse_copy_g(o, l, &back2, &GAME_NWN1, err) != 0 && strstr(err, "nwn2") &&
          strstr(err, "was selected"));
    {
        size_t n1;
        uint8_t *p1;
        erf e1;
        erf_init(&e1, "MOD ", &GAME_NWN1);
        erf_add(&e1, "module", RES_IFO, (const uint8_t *)"i", 1, err);
        CHECK(erf_write(&e1, &p1, &n1, err) == 0 && !memcmp(p1, "MOD V1.0", 8));
        CHECK(parse_copy_g(p1, n1, &back2, &GAME_NWN2, err) != 0 && strstr(err, "nwn1"));
        CHECK(parse_copy_g(p1, n1, &back2, &GAME_NWN1, err) == 0);
        erf_free(&back2);
        free(p1);
        erf_free(&e1);
    }
    /* A 20-char resref is fine for NWN2 but not NWN1. */
    {
        erf e1;
        erf_init(&e1, "MOD ", &GAME_NWN1);
        CHECK(erf_add(&e1, "twenty_chars_resref_", RES_ARE, (const uint8_t *)"", 0, err) != 0);
        erf_free(&e1);
    }
    CHECK(game_by_name("nwn1") == &GAME_NWN1 && game_by_name("nwn2") == &GAME_NWN2);
    CHECK(game_by_name("nwn3") == NULL && game_by_name("") == NULL);
    CHECK(GAME_NWN2.n_area_parts == 5 && GAME_NWN1.n_area_parts == 3);
    free(o);
    erf_free(&e);
}

static void test_catalog(void)
{
    catalog c;
    char err[ERRLEN];
    const char *ok =
        "# comment\r\n\r\n"
        "b_slot\tv2\t1\t2\t1\t1\tdir with space/v2.erf\r\n"
        "a_slot\tv1\t1\t2\t1\t1\tv1.erf\n"
        "b_slot\tv1\t1\t40\t1\t32\tv1.erf";            /* no final newline */
    CHECK(catalog_parse(ok, strlen(ok), &c, err) == 0);
    CHECK(c.n == 3);
    CHECK(!strcmp(c.rows[0].slot, "a_slot"));
    CHECK(!strcmp(c.rows[1].slot, "b_slot") && !strcmp(c.rows[1].variant, "v1"));
    CHECK(!strcmp(c.rows[2].relpath, "dir with space/v2.erf"));
    catalog_free(&c);

#define BAD(s) do { const char *t = s; CHECK(catalog_parse(t, strlen(t), &c, err) != 0); } while (0)
    BAD("");
    BAD("# only comments\n");
    BAD("a\tb\t1\t2\t1\t1\n");                    /* 6 fields */
    BAD("a\tb\t1\t2\t1\t1\tx\ty\n");              /* 8 fields */
    BAD("a b\tb\t1\t2\t1\t1\tx\n");               /* spaces split on tab only: bad id */
    BAD("A\tb\t1\t2\t1\t1\tx\n");                 /* uppercase id */
    BAD("a\tb\t3\t2\t1\t1\tx\n");                 /* min > max */
    BAD("a\tb\t0\t2\t1\t1\tx\n");                 /* level 0 */
    BAD("a\tb\t1\t41\t1\t1\tx\n");                /* level 41 */
    BAD("a\tb\t1\t2\t1\t33\tx\n");                /* party 33 */
    BAD("a\tb\t1\t2\t1\t1\t\n");                  /* empty path */
    BAD("a\tb\t1\t2\t1\t1\t/abs\n");              /* absolute path */
    BAD("a\tb\t1\tx\t1\t1\tp\n");                 /* non-numeric */
    BAD("a\tb\t1\t2\t1\t1\tp\na\tb\t1\t2\t1\t1\tq\n"); /* duplicate pair */
    BAD("a\tb\t1\t2\t1\t1\tp\n \n");              /* whitespace-only line */
    {
        char big[CAT_MAX_LINE + 64];
        memset(big, 'a', sizeof big);
        big[sizeof big - 1] = '\n';
        CHECK(catalog_parse(big, sizeof big, &c, err) != 0);
    }
    CHECK(catalog_parse("a\0b\n", 4, &c, err) != 0);
#undef BAD
}

static void test_gff(void)
{
    uint8_t g[64];
    char err[ERRLEN];
    memset(g, 0, sizeof g);
    memcpy(g, "ARE V3.2", 8);
    wr32le(g + 8, 56);  wr32le(g + 12, 0);      /* zero structs: invalid root */
    wr32le(g + 16, 56); wr32le(g + 24, 56);
    wr32le(g + 32, 56); wr32le(g + 40, 56); wr32le(g + 48, 56);
    CHECK(gff_check(g, sizeof g, "ARE ", err) != 0);
    wr32le(g + 12, 0);
    memset(g, 0, sizeof g);
    memcpy(g, "ARE V3.2", 8);
    wr32le(g + 8, 56); wr32le(g + 12, 0);
    CHECK(gff_check(g, 40, "ARE ", err) != 0);  /* short */
    CHECK(gff_check(g, sizeof g, "GIT ", err) != 0); /* wrong type */
    memcpy(g + 4, "V3.3", 4);
    CHECK(gff_check(g, sizeof g, "ARE ", err) != 0); /* version */
}

/* ---------- GFF tree / writer ---------- */

static void test_gff_tree(void)
{
    gff a, b, c;
    gff_struct *s, *sub, *e;
    gff_field *l;
    uint8_t *o1, *o2, *o3;
    size_t n1, n2, n3;
    char err[ERRLEN] = "";
    const uint8_t blob[3] = {1, 2, 3};

    fx_root(&a, "ARE ");
    s = a.root;
    CHECK(gff_set_int(s, "Num", GFF_INT, -5, err) == 0);
    CHECK(gff_set_int(s, "Byte", GFF_BYTE, 200, err) == 0);
    CHECK(gff_set_float(s, "F", 1.5f, err) == 0);
    CHECK(gff_set_string(s, "Str", GFF_STRING, "hello", err) == 0);
    CHECK(gff_set_string(s, "Ref", GFF_RESREF, "abc", err) == 0);
    CHECK(gff_set_locstr(s, "Loc", "text", err) == 0);
    CHECK(gff_set_locstr(s, "NoLoc", NULL, err) == 0);
    CHECK(gff_set_void(s, "Blob", blob, 3, err) == 0);
    sub = gff_set_struct(s, "Sub", 7, err);
    CHECK(sub && gff_set_int(sub, "X", GFF_DWORD, 0xFFFFFFFFu, err) == 0);
    l = gff_set_list(s, "List", err);
    CHECK(l != NULL);
    e = gff_struct_new(3);
    gff_list_append(l, e);
    CHECK(gff_set_string(e, "T", GFF_STRING, "x", err) == 0);
    /* an existing field keeps its type */
    CHECK(gff_set_string(s, "Num", GFF_STRING, "no", err) != 0);
    CHECK(gff_set_string(s, "Ref", GFF_RESREF, "0123456789abcdefg", err) != 0); /* 17 chars */
    CHECK(gff_get_int(s, "Num", 0) == -5 && gff_get_int(s, "Missing", 42) == 42);
    CHECK(!strcmp(gff_get_string(s, "Str"), "hello"));

    CHECK(gff_write(&a, &o1, &n1, err) == 0);
    CHECK(gff_check(o1, n1, "ARE ", err) == 0);
    CHECK(gff_parse(o1, n1, &b, err) == 0);
    CHECK(gff_get_int(b.root, "Num", 0) == -5 && gff_get_int(b.root, "Byte", 0) == 200);
    CHECK(gff_write(&b, &o2, &n2, err) == 0);
    CHECK(n1 == n2 && !memcmp(o1, o2, n1)); /* parse -> write is the identity on our own output */
    gff_free(&b);
    /* a clone writes the same bytes and is independent of the original */
    c.root = gff_struct_clone(a.root);
    memcpy(c.type, a.type, 5);
    CHECK(gff_write(&c, &o3, &n3, err) == 0 && n1 == n3 && !memcmp(o1, o3, n1));
    CHECK(gff_set_int(c.root, "Num", GFF_INT, 9, err) == 0 && gff_get_int(a.root, "Num", 0) == -5);
    /* writes are not affected by how the tree was built */
    CHECK(o1[0] == 'A' && !memcmp(o1 + 4, "V3.2", 4));
    /* truncated and corrupt input is rejected, not trusted */
    CHECK(gff_parse(o1, 40, &b, err) != 0);
    {
        uint8_t *bad = malloc(n1);
        memcpy(bad, o1, n1);
        wr32le(bad + 8, 0xFFFFFF00u); /* struct offset out of range */
        CHECK(gff_parse(bad, n1, &b, err) != 0);
        free(bad);
    }
    free(o1); free(o2); free(o3);
    gff_free(&a); gff_free(&c);
}

/* ---------- recipe ---------- */

static const char GOOD_RECIPE[] =
    "nwndgen-recipe 1\n# comment\ntilepack tp.txt\ntemplate_are a.are\ntemplate_git /abs/g.git\n"
    "module_name My Module\nmodule_tag MY_TAG\nsupport 1 2 1 3\narea a_1 10 8 3 First Area\narea b_2 12 12 4 Second\n";

static void test_recipe(void)
{
    recipe r;
    char err[ERRLEN] = "";
    char big[2048];
#define RP(t) recipe_parse((t), strlen(t), "dir", &r, err)
    CHECK(RP(GOOD_RECIPE) == 0);
    CHECK(RP(GOOD_RECIPE) == 0 && r.loot_amount == 0);
    {
        char la[1024];
        snprintf(la, sizeof la, "%sroster r.txt\nscripts s\nloot_amount high\n", GOOD_RECIPE);
        CHECK(RP(la) == 0 && r.loot_amount == 2);
        snprintf(la, sizeof la, "%sroster r.txt\nscripts s\nloot_amount low\n", GOOD_RECIPE);
        CHECK(RP(la) == 0 && r.loot_amount == 1);
        snprintf(la, sizeof la, "%sroster r.txt\nscripts s\nloot_amount mid\n", GOOD_RECIPE);
        CHECK(RP(la) == 0 && r.loot_amount == 0);
        snprintf(la, sizeof la, "%sroster r.txt\nscripts s\nloot_amount lots\n", GOOD_RECIPE);
        CHECK(RP(la) != 0 && strstr(err, "low, mid or high"));
        snprintf(la, sizeof la, "%sroster r.txt\nscripts s\nloot_amount low\nloot_amount high\n", GOOD_RECIPE);
        CHECK(RP(la) != 0 && strstr(err, "twice"));
        snprintf(la, sizeof la, "%sloot_amount low\n", GOOD_RECIPE);
        CHECK(RP(la) != 0 && strstr(err, "needs roster"));
        snprintf(la, sizeof la, "%sroster r.txt\nscripts s\ncorridors kit\ncorridor_dressing high\n", GOOD_RECIPE);
        CHECK(RP(la) == 0 && r.corridor_dressing == 3);
        snprintf(la, sizeof la, "%sroster r.txt\nscripts s\ncorridors kit\ncorridor_dressing off\n", GOOD_RECIPE);
        CHECK(RP(la) == 0 && r.corridor_dressing == 0);
        snprintf(la, sizeof la, "%sroster r.txt\nscripts s\ncorridor_dressing low\n", GOOD_RECIPE);
        CHECK(RP(la) != 0 && strstr(err, "corridors kit"));
        snprintf(la, sizeof la, "%scorridors kit\ncorridor_dressing low\n", GOOD_RECIPE);
        CHECK(RP(la) != 0 && strstr(err, "needs roster"));
        snprintf(la, sizeof la, "%sroster r.txt\nscripts s\ncorridors kit\ncorridor_dressing lots\n", GOOD_RECIPE);
        CHECK(RP(la) != 0 && strstr(err, "off, low, mid or high"));
        snprintf(la, sizeof la, "%sroster r.txt\nscripts s\ncorridors kit\ncorridor_dressing low\ncorridor_dressing high\n", GOOD_RECIPE);
        CHECK(RP(la) != 0 && strstr(err, "twice"));
        snprintf(la, sizeof la, "%sroster r.txt\nscripts s\ncorridors kit\ndoors plain\ntemplate_door d.git\nlocks high\n", GOOD_RECIPE);
        CHECK(RP(la) == 0 && r.locks == 3);
        snprintf(la, sizeof la, "%sroster r.txt\nscripts s\ncorridors kit\ndoors plain\ntemplate_door d.git\nlocks low\n", GOOD_RECIPE);
        CHECK(RP(la) == 0 && r.locks == 1);
        snprintf(la, sizeof la, "%sroster r.txt\nscripts s\ncorridors kit\ndoors plain\ntemplate_door d.git\nlocks off\n", GOOD_RECIPE);
        CHECK(RP(la) == 0 && r.locks == 0);
        snprintf(la, sizeof la, "%sroster r.txt\nscripts s\ncorridors kit\nlocks mid\n", GOOD_RECIPE);
        CHECK(RP(la) != 0 && strstr(err, "doors"));
        snprintf(la, sizeof la, "%sroster r.txt\nscripts s\nlocks mid\n", GOOD_RECIPE);
        CHECK(RP(la) != 0 && strstr(err, "corridors kit"));
        snprintf(la, sizeof la, "%scorridors kit\ndoors plain\ntemplate_door d.git\nlocks mid\n", GOOD_RECIPE);
        CHECK(RP(la) != 0 && strstr(err, "needs roster"));
        snprintf(la, sizeof la, "%sroster r.txt\nscripts s\ncorridors kit\ndoors plain\ntemplate_door d.git\nlocks lots\n", GOOD_RECIPE);
        CHECK(RP(la) != 0 && strstr(err, "off, low, mid or high"));
        snprintf(la, sizeof la, "%sroster r.txt\nscripts s\ncorridors kit\ndoors plain\ntemplate_door d.git\nlocks low\nlocks high\n", GOOD_RECIPE);
        CHECK(RP(la) != 0 && strstr(err, "twice"));
        snprintf(la, sizeof la, "%sroster r.txt\nscripts s\ndressing high\n", GOOD_RECIPE);
        CHECK(RP(la) == 0 && r.dressing == 3);
        snprintf(la, sizeof la, "%sroster r.txt\nscripts s\ndressing mid\n", GOOD_RECIPE);
        CHECK(RP(la) == 0 && r.dressing == 2);
        snprintf(la, sizeof la, "%sroster r.txt\nscripts s\ndressing low\n", GOOD_RECIPE);
        CHECK(RP(la) == 0 && r.dressing == 1);
        snprintf(la, sizeof la, "%sroster r.txt\nscripts s\ndressing off\n", GOOD_RECIPE);
        CHECK(RP(la) == 0 && r.dressing == 0);
        snprintf(la, sizeof la, "%sroster r.txt\nscripts s\ndressing lots\n", GOOD_RECIPE);
        CHECK(RP(la) != 0 && strstr(err, "off, low, mid or high"));
        snprintf(la, sizeof la, "%sroster r.txt\nscripts s\ndressing low\ndressing high\n", GOOD_RECIPE);
        CHECK(RP(la) != 0 && strstr(err, "twice"));
        snprintf(la, sizeof la, "%sdressing low\n", GOOD_RECIPE);
        CHECK(RP(la) != 0 && strstr(err, "needs roster"));
        snprintf(la, sizeof la, "%sdoors mixed\ntemplate_door d.git\ncorridors kit\n", GOOD_RECIPE);
        CHECK(RP(la) == 0 && r.doors == 2);
    }
    CHECK(RP(GOOD_RECIPE) == 0 && !r.has_seed && !r.has_party && !r.has_min_lvl && !r.has_max_lvl); /* no run defaults unless written */
    {
        char rd[1024];
        snprintf(rd, sizeof rd, "%sseed 18446744073709551615\nparty_size 3\nmin_level 2\nmax_level 40\n", GOOD_RECIPE);
        CHECK(RP(rd) == 0 && r.has_seed && r.run_seed == 18446744073709551615ull && r.run_party == 3 && r.run_min_lvl == 2 && r.run_max_lvl == 40);
        CHECK(r.has_party && r.has_min_lvl && r.has_max_lvl);
        snprintf(rd, sizeof rd, "%sseed 0\n", GOOD_RECIPE);
        CHECK(RP(rd) == 0 && r.has_seed && r.run_seed == 0);
#define BAD_RUN(line, frag) \
    do { snprintf(rd, sizeof rd, "%s%s", GOOD_RECIPE, line); err[0] = 0; CHECK(RP(rd) != 0 && strstr(err, frag)); } while (0)
        BAD_RUN("seed 18446744073709551616\n", "seed");
        BAD_RUN("seed -1\n", "seed");
        BAD_RUN("seed 1\nseed 2\n", "twice");
        BAD_RUN("party_size 0\n", "party_size");
        BAD_RUN("party_size 33\n", "party_size");
        BAD_RUN("min_level 0\n", "min_level");
        BAD_RUN("max_level 41\n", "max_level");
        BAD_RUN("max_level x\n", "max_level");
        BAD_RUN("min_level 1\nmin_level 2\n", "twice");
    }
    CHECK(r.nareas == 2 && r.areas[0].w == 10 && r.areas[0].h == 8 && r.areas[0].rooms == 3);
    CHECK(!strcmp(r.areas[0].name, "First Area") && !strcmp(r.areas[1].resref, "b_2"));
    CHECK(!strcmp(r.tilepack, "dir/tp.txt") && !strcmp(r.template_git, "/abs/g.git"));
    CHECK(r.min_level == 1 && r.max_level == 2 && r.min_party == 1 && r.max_party == 3);
    CHECK(!strcmp(r.module_tag, "MY_TAG") && !strcmp(r.module_desc, ""));
    CHECK(recipe_parse(GOOD_RECIPE, strlen(GOOD_RECIPE), "", &r, err) == 0 && !strcmp(r.tilepack, "tp.txt"));
    CHECK(RP("") != 0);
    CHECK(RP("nwndgen-recipe 2\n") != 0);
    CHECK(RP("nwndgen-recipe 1\n") != 0); /* nothing required given */
    {
        const char *bad[] = {
            "tilepack tp.txt\n", /* no header */
            "nwndgen-recipe 1\nbogus x\n",
            "nwndgen-recipe 1\ntilepack\n",
            "nwndgen-recipe 1\ntilepack a\ntilepack b\n",
            "nwndgen-recipe 1\nmodule_tag has space\n",
            "nwndgen-recipe 1\nsupport 3 2 1 1\n",
            "nwndgen-recipe 1\nsupport 1 2 1\n",
            "nwndgen-recipe 1\nsupport 0 2 1 1\n",
            "nwndgen-recipe 1\nsupport 1 41 1 1\n",
            "nwndgen-recipe 1\narea UPPER 10 10 3 x\n",
            "nwndgen-recipe 1\narea a 5 10 3 x\n",
            "nwndgen-recipe 1\narea a 33 10 3 x\n",
            "nwndgen-recipe 1\narea a 10 10 1 x\n",
            "nwndgen-recipe 1\narea a 10 10 9 x\n",
            "nwndgen-recipe 1\narea a 10 10 3\n",
            "nwndgen-recipe 1\narea a 10 10 3 x\narea a 10 10 3 y\n",
            "nwndgen-recipe 1\narea a 10x 10 3 x\n",
            "nwndgen-recipe 1\narea a 10 10 3 caf\xc3\xa9\n",
        };
        size_t i;
        char t[4096];
        for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
            CHECK(RP(bad[i]) != 0);
            /* a valid recipe with one extra bad line is rejected too */
            snprintf(t, sizeof t, "%s%s", GOOD_RECIPE, strchr(bad[i], '\n') + 1);
            if (strncmp(bad[i], "nwndgen-recipe", 14) == 0)
                CHECK(RP(t) != 0 || i == 0);
        }
    }
    CHECK(RP("nwndgen-recipe 1\r\ntilepack t\r\ntemplate_are a\r\ntemplate_git g\r\nmodule_name n\r\n"
             "module_tag T\r\nsupport 1 1 1 1\r\narea a 6 6 2 n\r\n") == 0); /* CRLF tolerated */
    memset(big, 'x', sizeof big);
    memcpy(big, "nwndgen-recipe 1\ntilepack ", 26);
    big[sizeof big - 1] = '\0';
    CHECK(RP(big) != 0); /* over-long line */
#undef RP
}

/* ---------- tile pack ---------- */

static char *slurp(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    char *b;
    long sz;
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    b = malloc((size_t)sz + 1);
    *n = fread(b, 1, (size_t)sz, f);
    b[*n] = '\0';
    fclose(f);
    return b;
}

static void test_tilepack(void)
{
    size_t n;
    char *t = slurp("tests/data/tilepack_test.txt", &n);
    tilepack p;
    char err[ERRLEN] = "";
    CHECK(t != NULL);
    if (!t)
        return;
    CHECK(tilepack_parse(t, n, &p, err) == 0);
    CHECK(!strcmp(p.tileset, "tsttest") && p.nlight > 0 && p.cell[15].n >= 1 && p.cell[0].n >= 1);
    CHECK(p.cell[9].n >= 1 && p.cell[6].n >= 1); /* diagonal patterns are accepted (and unused) */
    tilepack_free(&p);
    CHECK(tilepack_parse("", 0, &p, err) != 0);
    CHECK(tilepack_parse("nwndgen-tilepack 2\ntileset x\n", 30, &p, err) != 0);
    CHECK(tilepack_parse("nwndgen-tilepack 1\ntileset x\ncell WWWW 1 1 0\n", 44, &p, err) != 0); /* missing patterns */
    {
        /* bad record variants, each appended to the good pack */
        static const char *bad[] = {"cell WWWQ 1 1 0\n", "cell WWWW 0 1 0\n", "cell WWWW 1 1 4\n", "cell WWWW 1 1\n",
                                    "cell WWWW 1 1 0 9\n", "light 1 1 2 3\n", "light 1 1 2 3 4 5 6 256\n", "junk\n"};
        size_t i, m = strlen(t);
        char *u = malloc(m + 64);
        for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
            memcpy(u, t, m);
            strcpy(u + m, bad[i]);
            CHECK(tilepack_parse(u, strlen(u), &p, err) != 0);
        }
        /* a tile without a light record is refused */
        memcpy(u, t, m);
        strcpy(u + m, "cell FFFF 1 9999 0\n");
        CHECK(tilepack_parse(u, strlen(u), &p, err) != 0);
        free(u);
    }
    free(t);
}

/* ---------- layout ---------- */

static int floor_cell(const layout *l, int x, int y)
{
    return l->lat[y][x] == 'F' && l->lat[y][x + 1] == 'F' && l->lat[y + 1][x] == 'F' && l->lat[y + 1][x + 1] == 'F';
}

static void test_layout(void)
{
    uint64_t seed;
    char err[ERRLEN] = "";
    unsigned fails_seen = 0;
    for (seed = 1; seed <= 400; seed++) {
        layout l, l2;
        sm64 r, r2;
        int x, y, nfull = 0, k;
        int dims[3][3] = {{10, 10, 3}, {12, 9, 4}, {16, 16, 5}};
        int *d = dims[seed % 3];
        sm64_seed(&r, seed * 2654435761u);
        sm64_seed(&r2, seed * 2654435761u);
        if (layout_make(&r, d[0], d[1], d[2], &l, err) != 0) {
            CHECK(strstr(err, "could not generate") != NULL);
            fails_seen++;
            continue;
        }
        CHECK(layout_make(&r2, d[0], d[1], d[2], &l2, err) == 0 && !memcmp(&l, &l2, sizeof l)); /* deterministic */
        CHECK(l.nrooms == d[2]);
        for (x = 0; x <= l.w; x++)
            CHECK(l.lat[0][x] == 'W' && l.lat[l.h][x] == 'W');
        for (y = 0; y <= l.h; y++)
            CHECK(l.lat[y][0] == 'W' && l.lat[y][l.w] == 'W');
        for (y = 0; y < l.h; y++)
            for (x = 0; x < l.w; x++) {
                char nw = l.lat[y + 1][x], ne = l.lat[y + 1][x + 1], sw = l.lat[y][x], se = l.lat[y][x + 1];
                CHECK(!(nw == 'F' && se == 'F' && ne == 'W' && sw == 'W')); /* no diagonal corner patterns */
                CHECK(!(ne == 'F' && sw == 'F' && nw == 'W' && se == 'W'));
                nfull += floor_cell(&l, x, y);
            }
        CHECK(nfull > 0);
        /* rooms are sorted by (x0, y0) and portal pairs are adjacent full-floor cells inside their room */
        for (k = 1; k < l.nrooms; k++)
            CHECK(l.room[k - 1][0] < l.room[k][0] || (l.room[k - 1][0] == l.room[k][0] && l.room[k - 1][1] < l.room[k][1]));
        for (k = 0; k < l.nrooms; k++) {
            int p[4];
            sm64 pr;
            sm64_seed(&pr, seed + (uint64_t)k);
            if (layout_portal_pair(&l, k, &pr, p, err) != 0)
                continue;
            CHECK(abs(p[0] - p[2]) + abs(p[1] - p[3]) == 1);
            CHECK(floor_cell(&l, p[0], p[1]) && floor_cell(&l, p[2], p[3]));
            CHECK(p[0] >= l.room[k][0] && p[0] < l.room[k][2] && p[1] >= l.room[k][1] && p[1] < l.room[k][3]);
            CHECK(p[2] >= l.room[k][0] && p[2] < l.room[k][2] && p[3] >= l.room[k][1] && p[3] < l.room[k][3]);
        }
    }
    CHECK(fails_seen < 20); /* these sizes almost never fail */
    /* a cramped area cannot fit many rooms: a clean error, never a bad layout */
    {
        layout l;
        sm64 r;
        int ok = 0, bad = 0;
        for (seed = 1; seed <= 40; seed++) {
            sm64_seed(&r, seed);
            if (layout_make(&r, 6, 6, 8, &l, err) == 0)
                ok++;
            else
                bad++;
        }
        CHECK(bad == 40 && ok == 0 && strstr(err, "too small") != NULL);
    }
    {
        layout l;
        sm64 r;
        sm64_seed(&r, 1);
        CHECK(layout_make(&r, 5, 10, 3, &l, err) != 0);
        CHECK(layout_make(&r, 10, 33, 3, &l, err) != 0);
        CHECK(layout_make(&r, 10, 10, 0, &l, err) != 0);
    }
}


/* ---------- corridor kit ---------- */

static int bits(int m) { return (m & 1) + (m >> 1 & 1) + (m >> 2 & 1) + (m >> 3 & 1); }

static void test_kit_pack(void)
{
    size_t n, m;
    char *t = slurp("tests/data/tilepack_test.txt", &n), *u;
    tilepack p;
    char err[ERRLEN] = "";
    size_t i;
    static const char *const bad[] = {"kit WWWW C-C- 0 101 0\n", "kit WWWW ---- 1 101 0\n", "kit WWWW C-C- 1 101 4\n", "kit WWWQ C-C- 1 101 0\n",
                                      "kit FWWF C-C- 1 101 0\n", "kit WWWW C-C- 1 101 0 door 1 2\n", "kit WWWW C-C- 1 101 0 junk\n",
                                      "kit WWWW CxC- 1 101 0\n", "kit WWWW C-C 1 101 0\n", "kit WWWW C-C- 1 9999 0\n", "kit WWWW C-C- 1 101 0 door 0 0 0 0 -1\n", "kit WWWW C-C- 1 101 0 door 0 0 0 0 256\n", "kit WWWW C-C- 1 101 0 door 0 0 0 0 23 x\n", "kit WWWW C-C- 1 101 0 door 0 0 0 0 2.5\n", "kit WWWW C-C-\n", "kit WWWW C-C- 1 101 0 props 0\n", "kit WWWW C-C- 1 101 0 props 64\n", "kit WWWW C-C- 1 101 0 props x\n", "kit WWWW C-C- 1 101 0 props 5 junk\n", "kit WWWW C-C- 1 101 0 door 0 0 0 0 props\n"};
    CHECK(t != NULL);
    if (!t)
        return;
    m = strlen(t);
    u = malloc(m + 512);
    memcpy(u, t, m);
    strcpy(u + m, "kit WWWW C-C- 2 101 1\nkit WWWW -C-C 1 102 0 door 1.50 -0.25 0.10 1.5708\nkit FWFW -C-- 3 103 2\nkit WWWW C-C- 1 104 0 door 0 0 0.51 0 23\n");
    CHECK(tilepack_parse(u, strlen(u), &p, err) == 0 && p.nkit == 4);
    if (p.nkit == 4) {
        CHECK(p.kit[1].app == 22 && p.kit[3].has_door && p.kit[3].app == 23 && p.kit[3].dz == 0.51); /* the look defaults to 22 */
        CHECK(p.kit[0].sig == 0 && p.kit[0].mask == 5 && p.kit[0].w == 2 && p.kit[0].tid == 101 && p.kit[0].orient == 1 && !p.kit[0].has_door);
        CHECK(p.kit[1].mask == 10 && p.kit[1].has_door && p.kit[1].dx == 1.5 && p.kit[1].dy == -0.25 && p.kit[1].dz == 0.1 && p.kit[1].bearing > 1.57 && p.kit[1].bearing < 1.58);
        CHECK(p.kit[2].sig == 10 && p.kit[2].mask == 2); /* FWFW: NW SW floor = index 8|2 */
    }
    tilepack_free(&p);
    for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        memcpy(u, t, m);
        strcpy(u + m, bad[i]);
        CHECK(tilepack_parse(u, strlen(u), &p, err) != 0);
    }
    strcpy(u + m, "kit WWWW C-C- 1 101 0 props 63\nkit WWWW -C-C 1 102 1 props 5\nkit WWWW C-C- 1 103 0 door 0 0 0 0 23 props 9\nkit WWWW -C-C 1 104 0\n");
    CHECK(tilepack_parse(u, strlen(u), &p, err) == 0 && p.nkit == 4 && p.kit[0].props == 63 && p.kit[1].props == 5 && p.kit[2].props == 9 && p.kit[2].app == 23 && p.kit[2].has_door && p.kit[3].props == 0);
    tilepack_free(&p);
    CHECK(tilepack_parse(t, n, &p, err) == 0 && p.nkit == 0); /* a pack without kit lines is unchanged */
    tilepack_free(&p);
    free(u);
    free(t);
}

static void test_doorway_pack(void)
{
    size_t n, m;
    char *t = slurp("tests/data/tilepack_test.txt", &n), *u;
    tilepack p;
    char err[ERRLEN] = "";
    recipe r;
    char rd[1024];

    CHECK(t != NULL);
    if (!t)
        return;
    m = strlen(t);
    u = malloc(m + 512);
    memcpy(u, t, m);
    strcpy(u + m, "kit FWFW -D-- 2 105 0 door 5.00 0.00 0.20 -1.5708 0\nkit WWWW D-C- 3 106 1\nkit WWWW C-C- 1 107 0\n");
    CHECK(tilepack_parse(u, strlen(u), &p, err) == 0 && p.nkit == 3);
    if (p.nkit == 3) {
        CHECK(p.kit[0].mask == 2 && p.kit[0].dmask == 2 && p.kit[0].has_door && p.kit[0].app == 0 && p.kit[0].dz == 0.2);
        CHECK(p.kit[1].mask == 5 && p.kit[1].dmask == 1 && !p.kit[1].has_door); /* one Doorway edge, one Corridor edge */
        CHECK(p.kit[2].mask == 5 && p.kit[2].dmask == 0); /* a pack without D keeps dmask 0 */
    }
    tilepack_free(&p);
    memcpy(u, t, m);
    strcpy(u + m, "kit WWWW D-X- 1 105 0\n");
    CHECK(tilepack_parse(u, strlen(u), &p, err) != 0);
    free(u);
    free(t);
    snprintf(rd, sizeof rd, "%scorridors kit\ndoors doorway\ntemplate_door d.git\n", GOOD_RECIPE);
    CHECK(recipe_parse(rd, strlen(rd), "dir", &r, err) == 0 && r.doors == 3 && r.kit_corridors);
    snprintf(rd, sizeof rd, "%scorridors kit\ndoors doorwayx\ntemplate_door d.git\n", GOOD_RECIPE);
    CHECK(recipe_parse(rd, strlen(rd), "dir", &r, err) != 0 && strstr(err, "doorway"));
    snprintf(rd, sizeof rd, "%sdoors doorway\ntemplate_door d.git\n", GOOD_RECIPE);
    CHECK(recipe_parse(rd, strlen(rd), "dir", &r, err) != 0); /* needs corridors kit */
}

static void test_kit_layout(void)
{
    static const int DIMS[5][3] = {{14, 14, 3}, {16, 16, 4}, {12, 12, 2}, {20, 20, 5}, {24, 18, 6}};
    static const int DX[4] = {0, 1, 0, -1}, DY[4] = {1, 0, -1, 0}; /* T R B L */
    uint64_t seed;
    char err[ERRLEN] = "";
    unsigned made = 0, failed = 0;

    for (seed = 1; seed <= 300; seed++) {
        const int *d = DIMS[seed % 5];
        layout l, l2;
        sm64 r, r2;
        int x, y, i, k, ncon = 0, linked[RECIPE_AREA_MAX][RECIPE_AREA_MAX] = {{0}};
        sm64_seed(&r, seed * 40503u);
        sm64_seed(&r2, seed * 40503u);
        if (layout_make_kit(&r, d[0], d[1], d[2], &l, err) != 0) {
            CHECK(strstr(err, "larger area") != NULL);
            failed++;
            continue;
        }
        made++;
        CHECK(layout_make_kit(&r2, d[0], d[1], d[2], &l2, err) == 0 && !memcmp(&l, &l2, sizeof l)); /* deterministic */
        CHECK(l.kit == 1 && l.nrooms == d[2] && l.nlink == d[2] - 1);
        for (x = 0; x <= l.w; x++)
            CHECK(l.lat[0][x] == 'W' && l.lat[l.h][x] == 'W');
        for (y = 0; y <= l.h; y++)
            CHECK(l.lat[y][0] == 'W' && l.lat[y][l.w] == 'W');
        /* no floor outside the rooms, and no diagonal patterns */
        for (y = 0; y <= l.h; y++)
            for (x = 0; x <= l.w; x++) {
                int inside = 0;
                for (k = 0; k < l.nrooms; k++)
                    inside |= x >= l.room[k][0] && x <= l.room[k][2] && y >= l.room[k][1] && y <= l.room[k][3];
                CHECK(l.lat[y][x] == 'W' || inside);
            }
        for (y = 0; y < l.h; y++)
            for (x = 0; x < l.w; x++) {
                char nw = l.lat[y + 1][x], ne = l.lat[y + 1][x + 1], sw = l.lat[y][x], se = l.lat[y][x + 1];
                CHECK(!(nw == 'F' && se == 'F' && ne == 'W' && sw == 'W') && !(ne == 'F' && sw == 'F' && nw == 'W' && se == 'W'));
            }
        /* wall rings stay one free cell apart so a corridor cell fits between them */
        for (i = 0; i < l.nrooms; i++)
            for (k = i + 1; k < l.nrooms; k++) {
                const int *a = l.room[i], *b = l.room[k];
                CHECK(b[0] - a[2] >= 3 || a[0] - b[2] >= 3 || b[1] - a[3] >= 3 || a[1] - b[3] >= 3);
            }
        /* each link: a 4-connected path of corridor cells with exactly two Corridor edges, none shared with another link */
        for (i = 0; i < l.nlink; i++) {
            CHECK(l.link_n[i] >= 1 && l.link_n[i] <= LINK_MAX);
            for (k = 0; k < l.link_n[i]; k++) {
                const int cx = l.link[i][k][0], cy = l.link[i][k][1];
                CHECK(cx < l.w && cy < l.h && !linked[cy][cx]);
                linked[cy][cx] = 1;
                CHECK(bits(l.kedge[cy][cx]) == 2);
                CHECK(l.lat[cy][cx] == 'W' && l.lat[cy][cx + 1] == 'W' && l.lat[cy + 1][cx] == 'W' && l.lat[cy + 1][cx + 1] == 'W');
                if (k)
                    CHECK(abs(cx - l.link[i][k - 1][0]) + abs(cy - l.link[i][k - 1][1]) == 1);
            }
        }
        /* every Corridor edge meets the opposite edge of the neighbour; the remaining ones are room connectors */
        for (y = 0; y < l.h; y++)
            for (x = 0; x < l.w; x++) {
                int mask = l.kedge[y][x], dir;
                for (dir = 0; dir < 4; dir++) {
                    int nx = x + DX[dir], ny = y + DY[dir];
                    if (!(mask >> dir & 1))
                        continue;
                    CHECK(nx >= 0 && ny >= 0 && nx < l.w && ny < l.h && (l.kedge[ny][nx] >> ((dir + 2) & 3) & 1));
                }
                if (mask && !linked[y][x]) { /* connector: one edge, floor on the opposite side, wall on the corridor side */
                    int nwf = l.lat[y + 1][x] == 'F', nef = l.lat[y + 1][x + 1] == 'F', swf = l.lat[y][x] == 'F', sef = l.lat[y][x + 1] == 'F';
                    ncon++;
                    CHECK(bits(mask) == 1);
                    CHECK(mask == 1 ? (!nwf && !nef && swf && sef) : mask == 2 ? (nwf && !nef && swf && !sef) : mask == 4 ? (nwf && nef && !swf && !sef) : (!nwf && nef && !swf && sef));
                }
            }
        CHECK(ncon == 2 * l.nlink);
    }
    CHECK(made > 250 && failed < 50); /* these sizes seldom fail */
    {
        layout l;
        sm64 r;
        int ok = 0, bad = 0;
        for (seed = 1; seed <= 20; seed++) {
            sm64_seed(&r, seed);
            if (layout_make_kit(&r, 7, 7, 5, &l, err) == 0) ok++; else bad++;
        }
        CHECK(ok == 0 && bad == 20 && strstr(err, "larger area") != NULL);
        sm64_seed(&r, 1);
        CHECK(layout_make_kit(&r, 5, 10, 3, &l, err) != 0 && layout_make_kit(&r, 10, 10, 0, &l, err) != 0);
    }
}

static void test_kit_set(void)
{
    static const char SET[] = "[TILES]\nCount=3\n[TILE0]\nModel=a\nDoors=1\n[TILE0DOOR0]\nType=22\nX=1.5\nY=-2.0\nZ=0.25\nOrientation=90.0\n"
                              "[TILE1]\nModel=b\nTop=Corridor\nBottom=Corridor\nDoors=0\n[TILE2]\nModel=c\nDoors=5\n[TILE2DOOR4]\nType=9\nX=7\n";
    tileset ts;
    char err[ERRLEN] = "";
    CHECK(tileset_parse(SET, sizeof SET - 1, &ts, err) == 0 && ts.n == 3);
    CHECK(ts.t[0].doors == 1 && ts.t[0].ndslot == 1 && ts.t[0].dslot[0].type == 22 && ts.t[0].dslot[0].x == 1.5 && ts.t[0].dslot[0].y == -2.0 &&
          ts.t[0].dslot[0].z == 0.25 && ts.t[0].dslot[0].orient == 90.0);
    CHECK(!strcmp(ts.t[1].edge[E_N], "Corridor") && !strcmp(ts.t[1].edge[E_S], "Corridor") && ts.t[1].edge[E_E][0] == '\0' && ts.t[1].ndslot == 0);
    CHECK(ts.t[2].ndslot == 0 && ts.t[2].dslot[0].type == 0); /* slots beyond four are ignored, not stored */
    tileset_free(&ts);
}

/* ---------- dungeon ---------- */

static int count_list(const gff_struct *s, const char *label)
{
    const gff_field *f = gff_find(s, label);
    return f && f->type == GFF_LIST ? (int)f->nlist : -1;
}

static void test_dungeon(void)
{
    size_t n, rn;
    char *tp_text = slurp("tests/data/tilepack_test.txt", &n), *rtext = slurp("tests/data/recipe_test.txt", &rn);
    tilepack tp;
    recipe r;
    gff are, git, ifo, out;
    dungeon d, d2;
    char err[ERRLEN] = "";
    static const uint64_t seeds[] = {0, 12345, UINT64_C(18446744073709551615)};
    size_t si, i;

    CHECK(tp_text && rtext);
    if (!tp_text || !rtext)
        return;
    CHECK(tilepack_parse(tp_text, n, &tp, err) == 0);
    CHECK(recipe_parse(rtext, rn, "tests/data", &r, err) == 0);
    CHECK(fx_are(&are) == 0 && fx_git(&git) == 0 && fx_ifo(&ifo) == 0);

    for (si = 0; si < 3; si++) {
        char path[128], *want, *got;
        size_t wn;
        CHECK(dungeon_build(&r, &tp, &are, &git, &ifo, seeds[si], &d, err) == 0);
        /* the C generator reproduces the Python reference model (frozen in tests/data/expected_*.txt) exactly */
        snprintf(path, sizeof path, "tests/data/expected_%llu.txt", (unsigned long long)seeds[si]);
        want = slurp(path, &wn);
        got = dungeon_dump(&d, seeds[si]);
        CHECK(want && !strcmp(want, got));
        free(want);
        free(got);

        CHECK(d.n == 3 && d.area[0].has_start && !d.area[0].has_prev && d.area[0].has_next);
        CHECK(!d.area[1].has_start && d.area[1].has_prev && d.area[1].has_next);
        CHECK(d.area[2].has_prev && !d.area[2].has_next);
        for (i = 0; i < d.n; i++) {
            const dungeon_area *a = &d.area[i];
            const recipe_area *ra = &r.areas[i];
            int tc, wc;
            CHECK(gff_parse(a->are, a->nare, &out, err) == 0);
            CHECK(!strcmp(out.type, "ARE ") && gff_get_int(out.root, "Width", 0) == (int)ra->w &&
                  gff_get_int(out.root, "Height", 0) == (int)ra->h);
            CHECK(count_list(out.root, "Tile_List") == (int)(ra->w * ra->h));
            CHECK(!strcmp(gff_get_string(out.root, "ResRef"), ra->resref) && !strcmp(gff_get_string(out.root, "Tileset"), "tsttest"));
            CHECK(!strcmp(gff_get_string(out.root, "OnEnter"), "")); /* donor script must not leak */
            CHECK(gff_get_int(out.root, "ChanceRain", 0) == 25);     /* donor lighting/weather kept */
            gff_free(&out);
            CHECK(gff_parse(a->git, a->ngit, &out, err) == 0);
            wc = count_list(out.root, "WaypointList");
            tc = count_list(out.root, "TriggerList");
            CHECK(wc == a->has_prev + a->has_next && tc == wc);
            CHECK(gff_find(out.root, "AreaProperties") != NULL);
            {
                const gff_field *tl = gff_find(out.root, "TriggerList");
                size_t k;
                for (k = 0; k < tl->nlist; k++) {
                    CHECK(gff_get_int(tl->list[k], "LinkedToFlags", 0) == 2);
                    CHECK(!strncmp(gff_get_string(tl->list[k], "Tag"), "TR_", 3));
                }
            }
            gff_free(&out);
            CHECK(gff_parse(a->gic, a->ngic, &out, err) == 0);
            CHECK(!strcmp(out.type, "GIC ") && count_list(out.root, "WaypointList") == wc && count_list(out.root, "TriggerList") == tc);
            gff_free(&out);
        }
        CHECK(gff_parse(d.ifo, d.nifo, &out, err) == 0);
        CHECK(!strcmp(gff_get_string(out.root, "Mod_Entry_Area"), "ca") && !strcmp(gff_get_string(out.root, "Mod_Tag"), "CHAIN_TEST"));
        CHECK(count_list(out.root, "Mod_Area_list") == 3);
        CHECK(gff_get_int(out.root, "Mod_MinPerHour", 0) == 2); /* untouched base fields survive */
        {
            const gff_field *id = gff_find(out.root, "Mod_ID");
            uint8_t want_id[16];
            dungeon_module_id(seeds[si], "CHAIN_TEST", want_id);
            CHECK(id && id->blen == 16 && !memcmp(id->bytes, want_id, 16));
        }
        gff_free(&out);

        /* same inputs -> same bytes */
        CHECK(dungeon_build(&r, &tp, &are, &git, &ifo, seeds[si], &d2, err) == 0);
        for (i = 0; i < d.n; i++)
            CHECK(d.area[i].nare == d2.area[i].nare && !memcmp(d.area[i].are, d2.area[i].are, d.area[i].nare) &&
                  d.area[i].ngit == d2.area[i].ngit && !memcmp(d.area[i].git, d2.area[i].git, d.area[i].ngit));
        CHECK(d.nifo == d2.nifo && !memcmp(d.ifo, d2.ifo, d.nifo));
        dungeon_free(&d2);
        dungeon_free(&d);
    }
    /* different seeds give different dungeons and different module ids */
    {
        dungeon x, y;
        CHECK(dungeon_build(&r, &tp, &are, &git, &ifo, 1, &x, err) == 0 && dungeon_build(&r, &tp, &are, &git, &ifo, 2, &y, err) == 0);
        CHECK(x.area[0].nare != y.area[0].nare || memcmp(x.area[0].are, y.area[0].are, x.area[0].nare) != 0);
        CHECK(x.nifo != y.nifo || memcmp(x.ifo, y.ifo, x.nifo) != 0);
        dungeon_free(&x);
        dungeon_free(&y);
    }
    /* area seeds depend on the area name, so renaming an area gives it a new layout */
    CHECK(dungeon_area_seed(1, "ca") != dungeon_area_seed(1, "cb") && dungeon_area_seed(1, "ca") != dungeon_area_seed(2, "ca"));
    /* a template without a transition trigger is refused cleanly */
    {
        gff g2;
        dungeon x;
        CHECK(fx_git(&g2) == 0);
        CHECK(gff_set_list(g2.root, "TriggerList", err) != NULL);
        CHECK(dungeon_build(&r, &tp, &are, &g2, &ifo, 1, &x, err) != 0 && strstr(err, "transition trigger"));
        gff_free(&g2);
    }
    /* a recipe whose area cannot be laid out fails with the area named */
    {
        recipe r2 = r;
        dungeon x;
        r2.nareas = 1;
        r2.areas[0].w = r2.areas[0].h = 6;
        r2.areas[0].rooms = 8;
        CHECK(dungeon_build(&r2, &tp, &are, &git, &ifo, 1, &x, err) != 0 && strstr(err, "area 'ca'"));
    }
    tilepack_free(&tp);
    gff_free(&are);
    gff_free(&git);
    gff_free(&ifo);
    free(tp_text);
    free(rtext);
}


/* ---------- roster / spawn / objective ---------- */

static int parse_roster(const char *text, roster *ro, char *err) { return roster_parse(text, strlen(text), ro, err); }

static const roster_entry *find_entry(const roster_theme *t, int role, const char *bp)
{
    size_t i;
    for (i = 0; i < t->n[role]; i++)
        if (!strcmp(t->e[role][i].resref, bp))
            return &t->e[role][i];
    return NULL;
}

static int cheb(int ax, int ay, int bx, int by)
{
    int dx = ax > bx ? ax - bx : bx - ax, dy = ay > by ? ay - by : by - ay;
    return dx > dy ? dx : dy;
}

static void test_roster(void)
{
    roster ro;
    char err[ERRLEN] = "";
    size_t n;
    char *text = slurp("tests/data/roster_test.txt", &n);
    static const char HEAD[] = "nwndgen-roster 1\nvault v\nkey k\nrelic r\n";
    char buf[512];

    CHECK(roster_parse_cr("0") == 0 && roster_parse_cr("2") == 200 && roster_parse_cr("0.25") == 25);
    CHECK(roster_parse_cr("0.3") == 30 && roster_parse_cr("1.5") == 150 && roster_parse_cr("40") == 4000);
    CHECK(roster_parse_cr("") < 0 && roster_parse_cr(".5") < 0 && roster_parse_cr("1.") < 0 && roster_parse_cr("1.234") < 0);
    CHECK(roster_parse_cr("-1") < 0 && roster_parse_cr("41") < 0 && roster_parse_cr("1e2") < 0 && roster_parse_cr("1 ") < 0);

    CHECK(text && roster_parse(text, n, &ro, err) == 0);
    CHECK(ro.ntheme == 2 && !strcmp(ro.vault, "t_vault") && !strcmp(ro.key, "t_key") && !strcmp(ro.relic, "t_relic"));
    CHECK(!strcmp(ro.theme[0].id, "alpha") && !strcmp(ro.theme[0].name, "Alpha Raiders"));
    CHECK(ro.theme[0].n[ROLE_MINION] == 2 && ro.theme[0].n[ROLE_STANDARD] == 1 && ro.theme[0].n[ROLE_ELITE] == 1 &&
          ro.theme[0].n[ROLE_BOSS] == 2);
    CHECK(ro.theme[1].e[ROLE_STANDARD][0].cr == 150 && ro.theme[1].e[ROLE_MINION][0].cr == 33);
    free(text);

#define BAD_ROSTER(body, frag) \
    do { snprintf(buf, sizeof buf, "%s%s", HEAD, body); err[0] = 0; \
         CHECK(parse_roster(buf, &ro, err) != 0 && strstr(err, frag)); } while (0)
    snprintf(buf, sizeof buf, "%stheme a A\nminion m 1\nboss b 1\n", HEAD);
    CHECK(parse_roster(buf, &ro, err) == 0);
    BAD_ROSTER("theme a A\nminion m 1\n", "needs at least one minion and one boss");
    BAD_ROSTER("theme a A\nboss b 1\n", "needs at least one minion and one boss");
    BAD_ROSTER("minion m 1\n", "before any theme");
    BAD_ROSTER("theme a A\ntheme a B\n", "duplicate theme");
    BAD_ROSTER("theme a A\nminion m 1\nminion m 2\nboss b 1\n", "duplicate entry");
    BAD_ROSTER("theme a A\nminion M 1\n", "resref");
    BAD_ROSTER("theme a A\nminion m 1.234\n", "CR");
    BAD_ROSTER("theme a A\nminion m\n", "resref CR");
    BAD_ROSTER("theme a\n", "needs 'id name'");
    BAD_ROSTER("vault again\n", "given twice");
    BAD_ROSTER("monster m 1\n", "unknown key");
    CHECK(parse_roster("nwndgen-roster 1\nvault v\nkey k\nrelic r\n", &ro, err) != 0 && strstr(err, "at least one theme"));
    CHECK(parse_roster("nwndgen-roster 1\nvault v\nkey k\nrelic k\ntheme a A\nminion m 1\nboss b 1\n", &ro, err) != 0 &&
          strstr(err, "different items"));
    CHECK(parse_roster("nwndgen-roster 2\n", &ro, err) != 0 && strstr(err, "first line"));
    CHECK(parse_roster("", &ro, err) != 0);
}

static void test_spawn(void)
{
    size_t n, rn, yn, si, i, k;
    char *tp_text = slurp("tests/data/tilepack_test.txt", &n), *rtext = slurp("tests/data/recipe_test.txt", &rn),
         *ytext = slurp("tests/data/roster_test.txt", &yn);
    tilepack tp;
    recipe r;
    roster ro;
    gff are, git, ifo, g;
    dungeon d, plain;
    char err[ERRLEN] = "";
    static const uint64_t seeds[] = {0, 1, 12345, UINT64_C(18446744073709551615)};
    spawn_cfg sc = {0};
    int themes_seen[2] = {0, 0};
    uint64_t sd;

    CHECK(tp_text && rtext && ytext);
    if (!tp_text || !rtext || !ytext)
        return;
    CHECK(tilepack_parse(tp_text, n, &tp, err) == 0 && recipe_parse(rtext, rn, "tests/data", &r, err) == 0);
    CHECK(roster_parse(ytext, yn, &ro, err) == 0);
    CHECK(fx_are(&are) == 0 && fx_git(&git) == 0 && fx_ifo(&ifo) == 0);

    /* caps follow the configured band */
    sc.ro = &ro; sc.party = 1; sc.min_level = 1; sc.max_level = 2;
    CHECK(spawn_regular_cap(&sc) == 150 && spawn_boss_cap(&sc) == 200);
    sc.party = 4; sc.min_level = 3; sc.max_level = 4;
    CHECK(spawn_regular_cap(&sc) == 350 && spawn_boss_cap(&sc) == 500);

    for (sd = 1; sd <= 48; sd++) {
        sc.party = 1 + (unsigned)(sd % 4); sc.min_level = 1 + (unsigned)(sd % 3); sc.max_level = sc.min_level + (unsigned)(sd % 2);
        CHECK(dungeon_build_ex(&r, &tp, &are, &git, &ifo, sd, &sc, &d, err) == 0);
        themes_seen[d.theme] = 1;
        dungeon_free(&d);
    }
    CHECK(themes_seen[0] && themes_seen[1]); /* the theme comes from the seed */

    for (si = 0; si < sizeof seeds / sizeof *seeds; si++) {
        uint64_t seed = seeds[si];
        int bands[3][3] = {{1, 2, 1}, {2, 4, 3}, {3, 4, 4}}, bi;
        for (bi = 0; bi < 3; bi++) {
            const roster_theme *th;
            char *dump_a, *dump_b;
            int rcap, bcap;
            sc.ro = &ro; sc.min_level = (unsigned)bands[bi][0]; sc.max_level = (unsigned)bands[bi][1]; sc.party = (unsigned)bands[bi][2];
            rcap = spawn_regular_cap(&sc);
            bcap = spawn_boss_cap(&sc);
            CHECK(dungeon_build_ex(&r, &tp, &are, &git, &ifo, seed, &sc, &d, err) == 0);
            CHECK(dungeon_build(&r, &tp, &are, &git, &ifo, seed, &plain, err) == 0);
            CHECK(d.has_spawn && !plain.has_spawn && plain.jrl == NULL && d.jrl != NULL && d.theme >= 0 && d.theme < 2);
            th = &ro.theme[d.theme];

            /* layouts, tiles and portals are exactly what the spawn-less build makes */
            dump_a = dungeon_dump(&d, seed);
            dump_b = dungeon_dump(&plain, seed);
            CHECK(!strcmp(dump_a, dump_b));
            free(dump_a);
            free(dump_b);

            {
                int nb = 0, nk = 0, nv = 0, nr = 0;
                for (i = 0; i < d.n; i++) {
                    const dungeon_area *a = &d.area[i];
                    uint64_t spent = 0, budget = (uint64_t)sc.party * (uint64_t)rcap * 2u * (uint64_t)(50 * d.n + 50 * (i + 1)) / (100u * d.n);
                    int ncreat = 0;
                    for (k = 0; k < a->nsp; k++) {
                        const spawn_pt *p = &a->sp[k];
                        size_t j;
                        CHECK(layout_full_floor(&a->lay, p->cx, p->cy));
                        CHECK(p->face >= 0 && p->face < 8);
                        /* clear of the transitions */
                        if (a->has_start) CHECK(cheb(p->cx, p->cy, a->pair_start[0], a->pair_start[1]) >= 2 && cheb(p->cx, p->cy, a->pair_start[2], a->pair_start[3]) >= 2);
                        if (a->has_prev) CHECK(cheb(p->cx, p->cy, a->pair_prev[0], a->pair_prev[1]) >= 2 && cheb(p->cx, p->cy, a->pair_prev[2], a->pair_prev[3]) >= 2);
                        if (a->has_next) CHECK(cheb(p->cx, p->cy, a->pair_next[0], a->pair_next[1]) >= 2 && cheb(p->cx, p->cy, a->pair_next[2], a->pair_next[3]) >= 2);
                        if (i == 0) /* quiet arrival room */
                            CHECK(!(p->cx >= a->lay.room[0][0] && p->cx < a->lay.room[0][2] && p->cy >= a->lay.room[0][1] && p->cy < a->lay.room[0][3]));
                        if (p->kind != 'K' && p->kind != 'R')
                            for (j = k + 1; j < a->nsp; j++)
                                if (a->sp[j].kind != 'K' && a->sp[j].kind != 'R')
                                    CHECK(a->sp[j].cx != p->cx || a->sp[j].cy != p->cy);
                        if (p->kind == 'C') {
                            const roster_entry *e = find_entry(th, ROLE_MINION, p->bp);
                            if (!e) e = find_entry(th, ROLE_STANDARD, p->bp);
                            if (!e) e = find_entry(th, ROLE_ELITE, p->bp);
                            CHECK(e && e->cr <= rcap);
                            if (e) spent += (uint64_t)e->cr;
                            ncreat++;
                        } else if (p->kind == 'B') {
                            const roster_entry *e = find_entry(th, ROLE_BOSS, p->bp);
                            CHECK(e && e->cr <= bcap && i + 1 == d.n);
                            nb++;
                        } else if (p->kind == 'K') {
                            CHECK(!strcmp(p->bp, ro.key) && i + 1 == d.n);
                            nk++;
                        } else if (p->kind == 'V') {
                            CHECK(!strcmp(p->bp, ro.vault) && i + 1 == d.n);
                            nv++;
                        } else if (p->kind == 'R') {
                            CHECK(!strcmp(p->bp, ro.relic) && i + 1 == d.n);
                            nr++;
                        } else
                            CHECK(0);
                    }
                    CHECK(spent <= budget && ncreat <= SPAWN_AREA_MAX);
                    if (i + 1 == d.n) { /* boss and vault share the last room; key rides the boss, relic the vault */
                        const int *rm = a->lay.room[a->lay.nrooms - 1];
                        const spawn_pt *b = NULL, *v = NULL, *kk = NULL, *rr = NULL;
                        for (k = 0; k < a->nsp; k++) {
                            if (a->sp[k].kind == 'B') b = &a->sp[k];
                            if (a->sp[k].kind == 'V') v = &a->sp[k];
                            if (a->sp[k].kind == 'K') kk = &a->sp[k];
                            if (a->sp[k].kind == 'R') rr = &a->sp[k];
                        }
                        CHECK(b && v && kk && rr);
                        if (b && v && kk && rr) {
                            CHECK(b->cx >= rm[0] && b->cx < rm[2] && b->cy >= rm[1] && b->cy < rm[3]);
                            CHECK(v->cx >= rm[0] && v->cx < rm[2] && v->cy >= rm[1] && v->cy < rm[3]);
                            CHECK(kk->cx == b->cx && kk->cy == b->cy && rr->cx == v->cx && rr->cy == v->cy);
                            CHECK(cheb(b->cx, b->cy, v->cx, v->cy) >= 1);
                        }
                    }
                }
                CHECK(nb == 1 && nk == 1 && nv == 1 && nr == 1);
            }

            /* wiring: area OnEnter, module acquire hook, markers in the GIT, GIC mirrored */
            for (i = 0; i < d.n; i++) {
                const gff_field *wl;
                const char *oe;
                size_t nsp_in_git = 0;
                CHECK(gff_parse(d.area[i].are, d.area[i].nare, &g, err) == 0);
                oe = gff_get_string(g.root, "OnEnter");
                CHECK(oe && !strcmp(oe, "scel_enter"));
                gff_free(&g);
                CHECK(gff_parse(d.area[i].git, d.area[i].ngit, &g, err) == 0);
                wl = gff_find(g.root, "WaypointList");
                for (k = 0; wl && k < wl->nlist; k++) {
                    const char *t = gff_get_string(wl->list[k], "Tag");
                    if (t && !strncmp(t, "SP", 2)) nsp_in_git++;
                }
                CHECK(nsp_in_git == d.area[i].nsp);
                gff_free(&g);
                CHECK(gff_parse(plain.area[i].are, plain.area[i].nare, &g, err) == 0);
                oe = gff_get_string(g.root, "OnEnter");
                CHECK(oe && !*oe);
                gff_free(&g);
            }
            CHECK(gff_parse(d.ifo, d.nifo, &g, err) == 0);
            CHECK(!strcmp(gff_get_string(g.root, "Mod_OnAcquirItem"), "scel_acquire"));
            gff_free(&g);
            CHECK(gff_parse(plain.ifo, plain.nifo, &g, err) == 0);
            {
                const char *acq = gff_get_string(g.root, "Mod_OnAcquirItem");
                CHECK(!acq || !*acq);
            }
            gff_free(&g);

            /* journal */
            CHECK(gff_parse(d.jrl, d.njrl, &g, err) == 0 && !strcmp(g.type, "JRL "));
            {
                const gff_field *cats = gff_find(g.root, "Categories"), *el;
                CHECK(cats && cats->nlist == 1 && !strcmp(gff_get_string(cats->list[0], "Tag"), "scel_main"));
                el = cats ? gff_find(cats->list[0], "EntryList") : NULL;
                CHECK(el && el->nlist == 3);
                for (k = 0; el && k < el->nlist; k++)
                    CHECK(gff_get_int(el->list[k], "ID", -1) == (int64_t)k + 1 && gff_get_int(el->list[k], "End", -1) == (k == 2));
            }
            gff_free(&g);

            /* same inputs -> same bytes (GIT, GIC, IFO, JRL) */
            {
                dungeon d2;
                CHECK(dungeon_build_ex(&r, &tp, &are, &git, &ifo, seed, &sc, &d2, err) == 0);
                for (i = 0; i < d.n; i++)
                    CHECK(d.area[i].ngit == d2.area[i].ngit && !memcmp(d.area[i].git, d2.area[i].git, d.area[i].ngit) &&
                          d.area[i].ngic == d2.area[i].ngic && !memcmp(d.area[i].gic, d2.area[i].gic, d.area[i].ngic));
                CHECK(d.nifo == d2.nifo && !memcmp(d.ifo, d2.ifo, d.nifo) && d.njrl == d2.njrl && !memcmp(d.jrl, d2.jrl, d.njrl));
                dungeon_free(&d2);
            }
            dungeon_free(&d);
            dungeon_free(&plain);
        }
    }

    /* boss_only: no minions, one T boss with the usual key/vault/relic, same theme */
    {
        dungeon x, y;
        sc.ro = &ro; sc.party = 1; sc.min_level = 1; sc.max_level = 2;
        sc.boss_only = 0;
        CHECK(dungeon_build_ex(&r, &tp, &are, &git, &ifo, 12345, &sc, &y, err) == 0);
        sc.boss_only = 1;
        CHECK(dungeon_build_ex(&r, &tp, &are, &git, &ifo, 12345, &sc, &x, err) == 0);
        CHECK(x.theme == y.theme);
        for (i = 0; i < x.n; i++) {
            int nt = 0, nb = 0, nc = 0;
            for (k = 0; k < x.area[i].nsp; k++) {
                nt += x.area[i].sp[k].kind == 'T';
                nb += x.area[i].sp[k].kind == 'B';
                nc += x.area[i].sp[k].kind == 'C';
            }
            CHECK(nc == 0 && nb == 0 && nt == (i == 0 ? 1 : 0));
            CHECK(x.area[i].nsp == (i == 0 ? 4u : 0u));
        }
        dungeon_free(&x);
        dungeon_free(&y);
        sc.boss_only = 0;
    }

    /* a band no theme can serve fails with the caps named; so does an unusable acquire hook in the base */
    {
        dungeon x;
        gff i2;
        sc.min_level = 1; sc.max_level = 1; sc.party = 1;
        {
            roster tough = ro;
            tough.theme[0].e[ROLE_BOSS][0].cr = tough.theme[0].e[ROLE_BOSS][1].cr = 900;
            tough.theme[1].e[ROLE_BOSS][0].cr = 900;
            sc.ro = &tough;
            CHECK(dungeon_build_ex(&r, &tp, &are, &git, &ifo, 1, &sc, &x, err) != 0 && strstr(err, "no theme with a boss"));
        }
        sc.ro = &ro; sc.max_level = 2;
        CHECK(fx_ifo(&i2) == 0);
        CHECK(gff_set_string(i2.root, "Mod_OnAcquirItem", GFF_RESREF, "other_handler", err) == 0);
        CHECK(dungeon_build_ex(&r, &tp, &are, &git, &i2, 1, &sc, &x, err) != 0 && strstr(err, "Mod_OnAcquirItem"));
        gff_free(&i2);
    }
    tilepack_free(&tp);
    gff_free(&are);
    gff_free(&git);
    gff_free(&ifo);
    free(tp_text);
    free(rtext);
    free(ytext);
}

/* ---------- scenery: roster lines, planner invariants, ambience ---------- */

static int deco_has(const roster_theme *t, int kind, const char *bp)
{
    size_t i;
    for (i = 0; i < t->ndeco[kind]; i++)
        if (!strcmp(t->deco[kind][i].resref, bp))
            return 1;
    return 0;
}


static void test_roster_loot(void)
{
    roster ro;
    char err[ERRLEN] = "";
    size_t n;
    char *text = slurp("tests/data/roster_loot_test.txt", &n);
    static const char HEAD[] = "nwndgen-roster 1\nvault v\nkey k\nrelic r\ntheme a A\nminion m 1\nboss b 1\n";
    char buf[1024];

    CHECK(text && roster_parse(text, n, &ro, err) == 0);
    CHECK(ro.theme[0].nloot == 3 && ro.theme[0].gold_per_cr == 10);
    CHECK(!strcmp(ro.theme[0].loot[0].resref, "t_l_low") && ro.theme[0].loot[0].chance == 100 && ro.theme[0].loot[0].min_cr == 0 &&
          ro.theme[0].loot[0].max_cr == 60);
    CHECK(ro.theme[0].loot[2].chance == 50 && ro.theme[0].loot[2].min_cr == 300 && ro.theme[0].loot[2].max_cr == 900);
    CHECK(ro.theme[1].nloot == 0 && ro.theme[1].gold_per_cr == 0);
    free(text);

#define BAD_LOOT(body, frag) \
    do { snprintf(buf, sizeof buf, "%s%s", HEAD, body); err[0] = 0; CHECK(parse_roster(buf, &ro, err) != 0 && strstr(err, frag)); } while (0)
#define GOOD_LOOT(body) \
    do { snprintf(buf, sizeof buf, "%s%s", HEAD, body); err[0] = 0; CHECK(parse_roster(buf, &ro, err) == 0); } while (0)
    GOOD_LOOT("loot i 1 0 0\nloot j 100 0.25 40\ngold 1\n");
    GOOD_LOOT("gold 1000\n");
    CHECK(parse_roster("nwndgen-roster 1\nvault v\nkey k\nrelic r\nloot i 5 0 1\ntheme a A\nminion m 1\nboss b 1\n", &ro, err) != 0 && strstr(err, "before any theme"));
    CHECK(parse_roster("nwndgen-roster 1\nvault v\nkey k\nrelic r\ngold 5\ntheme a A\nminion m 1\nboss b 1\n", &ro, err) != 0 && strstr(err, "before any theme"));
    BAD_LOOT("loot i\n", "needs");
    BAD_LOOT("loot i 5\n", "needs");
    BAD_LOOT("loot i 5 0\n", "needs");
    BAD_LOOT("loot I 5 0 1\n", "resref");
    BAD_LOOT("loot i 0 0 1\n", "chance");
    BAD_LOOT("loot i 101 0 1\n", "chance");
    BAD_LOOT("loot i x 0 1\n", "chance");
    BAD_LOOT("loot i 5 0.001 1\n", "CR");
    BAD_LOOT("loot i 5 0 abc\n", "CR");
    BAD_LOOT("loot i 5 2 1\n", "above max");
    BAD_LOOT("loot i 5 0 1\nloot i 6 0 2\n", "duplicate");
    BAD_LOOT("gold\n", "without a value");
    BAD_LOOT("gold 0\n", "gold per CR");
    BAD_LOOT("gold 1001\n", "gold per CR");
    BAD_LOOT("gold 5x\n", "gold per CR");
    BAD_LOOT("gold 5\ngold 6\n", "twice");
    {
        char many[1024] = "";
        int i;
        for (i = 0; i < 17; i++)
            snprintf(many + strlen(many), sizeof many - strlen(many), "loot i%d 5 0 1\n", i);
        BAD_LOOT(many, "too many loot");
    }
}


static int unit_theme_cr(const roster_theme *t, const char *bp)
{
    int role;
    size_t i;
    for (role = 0; role < ROLE_N; role++)
        for (i = 0; i < t->n[role]; i++)
            if (!strcmp(t->e[role][i].resref, bp))
                return t->e[role][i].cr;
    return -1;
}

static void test_loot_amount(void)
{
    size_t n, rn, ln, i, k;
    char *tp_text = slurp("tests/data/tilepack_test.txt", &n), *rtext = slurp("tests/data/recipe_test.txt", &rn),
         *ltext = slurp("tests/data/roster_loot_amount_test.txt", &ln);
    tilepack tp;
    recipe r;
    roster rl;
    gff are, git, ifo;
    dungeon d[3];
    char err[ERRLEN] = "";
    spawn_cfg sc = {0};
    uint64_t seed;
    int more_items = 0, less_items = 0;

    CHECK(tp_text && rtext && ltext);
    if (!tp_text || !rtext || !ltext)
        return;
    CHECK(tilepack_parse(tp_text, n, &tp, err) == 0 && recipe_parse(rtext, rn, "tests/data", &r, err) == 0);
    CHECK(roster_parse(ltext, ln, &rl, err) == 0);
    CHECK(fx_are(&are) == 0 && fx_git(&git) == 0 && fx_ifo(&ifo) == 0);
    for (seed = 1; seed <= 40; seed++) {
        int amt;
        sc.ro = &rl;
        sc.party = 1 + (unsigned)(seed % 4);
        sc.min_level = 1 + (unsigned)(seed % 4);
        sc.max_level = sc.min_level + 1;
        for (amt = 0; amt < 3; amt++) { /* 0 mid, 1 low, 2 high */
            sc.loot_amount = amt;
            CHECK(dungeon_build_ex(&r, &tp, &are, &git, &ifo, seed, &sc, &d[amt], err) == 0);
        }
        for (i = 0; i < d[0].n; i++) {
            CHECK(d[0].area[i].nsp == d[1].area[i].nsp && d[0].area[i].nsp == d[2].area[i].nsp);
            for (k = 0; k < d[0].area[i].nsp; k++) {
                const spawn_pt *m = &d[0].area[i].sp[k], *lo = &d[1].area[i].sp[k], *hi = &d[2].area[i].sp[k];
                int im = (m->loot[0][0] != 0) + (m->loot[1][0] != 0), il = (lo->loot[0][0] != 0) + (lo->loot[1][0] != 0),
                    ih = (hi->loot[0][0] != 0) + (hi->loot[1][0] != 0);
                CHECK(m->kind == lo->kind && m->kind == hi->kind && !strcmp(m->bp, lo->bp) && !strcmp(m->bp, hi->bp) && m->cx == hi->cx && m->cy == lo->cy);
                if (m->kind != 'C' && m->kind != 'B')
                    continue;
                if (m->kind == 'B') /* a boss always carries two items; only gold scales */
                    CHECK(il == im && ih == im);
                else
                    CHECK(il <= im && im <= ih); /* same draws, scaled chances: the sets only grow with the amount */
                more_items += ih > im;
                less_items += il < im;
                if (m->gold) { /* low halves, high doubles (floor of 1) */
                    CHECK(lo->gold >= 1 && lo->gold <= m->gold && lo->gold == (m->gold / 2 < 1 ? 1 : m->gold / 2));
                    CHECK(hi->gold >= m->gold && hi->gold <= 2 * m->gold);
                }
            }
        }
        dungeon_free(&d[0]);
        dungeon_free(&d[1]);
        dungeon_free(&d[2]);
    }
    CHECK(more_items > 0 && less_items > 0); /* the fixture has a 50% band, so the scale must change something */
    gff_free(&are);
    gff_free(&git);
    gff_free(&ifo);
    tilepack_free(&tp);
    free(tp_text);
    free(rtext);
    free(ltext);
}

static void test_hench(void)
{
    size_t n, rn, hn, i;
    char *tp_text = slurp("tests/data/tilepack_test.txt", &n), *rtext = slurp("tests/data/recipe_test.txt", &rn),
         *htext = slurp("tests/data/roster_hench_test.txt", &hn);
    tilepack tp;
    recipe r;
    roster ro;
    gff are, git, ifo;
    dungeon a, b, c;
    char err[ERRLEN] = "";
    spawn_cfg sc = {0};
    uint64_t seed;
    static const char HEAD[] = "nwndgen-roster 1\nvault v\nkey k\nrelic r\n";
    static const char TAIL[] = "theme a A\nminion m 1\nboss b 1\n";
    char buf[512];

    CHECK(tp_text && rtext && htext);
    if (!tp_text || !rtext || !htext)
        return;
    CHECK(roster_parse(htext, hn, &ro, err) == 0 && ro.nhench == 3 && !strcmp(ro.hench[0], "t_hen_a") && !strcmp(ro.hench[2], "t_hen_c"));
#define BAD_HEN(body, frag) \
    do { snprintf(buf, sizeof buf, "%s%s%s", HEAD, body, TAIL); err[0] = 0; CHECK(parse_roster(buf, &ro, err) != 0 && strstr(err, frag)); } while (0)
    BAD_HEN("henchman A\n", "prefix");
    BAD_HEN("henchman abcdefghijklmn\n", "prefix"); /* 14 characters: <prefix>_NN would not fit a resref */
    BAD_HEN("henchman a\nhenchman a\n", "duplicate");
    BAD_HEN("henchman a\nhenchman b\nhenchman c\nhenchman d\nhenchman e\nhenchman f\nhenchman g\n", "too many");
    snprintf(buf, sizeof buf, "%shenchman abcdefghijklm\n%s", HEAD, TAIL);
    CHECK(parse_roster(buf, &ro, err) == 0);
    CHECK(roster_parse(htext, hn, &ro, err) == 0);

    CHECK(tilepack_parse(tp_text, n, &tp, err) == 0 && recipe_parse(rtext, rn, "tests/data", &r, err) == 0);
    CHECK(fx_are(&are) == 0 && fx_git(&git) == 0 && fx_ifo(&ifo) == 0);
    sc.ro = &ro;
    for (seed = 1; seed <= 40; seed++) {
        int want = 1 + (int)(seed % 3), nn;
        sc.party = 1;
        sc.min_level = 1 + (unsigned)(seed % 12);
        sc.max_level = sc.min_level + (unsigned)(seed % 3);
        sc.henchmen = want;
        nn = (int)(sc.min_level + sc.max_level) / 2 - 3;
        nn = nn < 1 ? 1 : nn > 11 ? 11 : nn;
        CHECK(dungeon_build_ex(&r, &tp, &are, &git, &ifo, seed, &sc, &a, err) == 0);
        CHECK(dungeon_build_ex(&r, &tp, &are, &git, &ifo, seed, &sc, &b, err) == 0);
        sc.henchmen = 0;
        CHECK(dungeon_build_ex(&r, &tp, &are, &git, &ifo, seed, &sc, &c, err) == 0);
        CHECK(a.nhench == want && b.nhench == want && c.nhench == 0);
        for (i = 0; i < (size_t)a.nhench; i++) {
            char exp[24];
            size_t j, fam = 99, f;
            CHECK(!strcmp(a.hench[i], b.hench[i])); /* deterministic */
            for (f = 0; f < ro.nhench; f++)
                if (!strncmp(a.hench[i], ro.hench[f], strlen(ro.hench[f]))) fam = f;
            CHECK(fam < ro.nhench);
            snprintf(exp, sizeof exp, "%s_%02d", fam < ro.nhench ? ro.hench[fam] : "?", nn);
            CHECK(!strcmp(a.hench[i], exp)); /* level index follows the recommended average */
            for (j = 0; j < i; j++)
                CHECK(strcmp(a.hench[i], a.hench[j])); /* distinct families */
        }
        /* the henchmen draw has its own RNG domain: nothing else moves */
        CHECK(a.theme == c.theme && a.area[0].nsp == c.area[0].nsp && (a.area[0].nsp == 0 || !strcmp(a.area[0].sp[0].bp, c.area[0].sp[0].bp)));
        dungeon_free(&a);
        dungeon_free(&b);
        dungeon_free(&c);
    }
    sc.min_level = sc.max_level = 3; /* more henchmen than the roster has families */
    sc.henchmen = 4;
    CHECK(dungeon_build_ex(&r, &tp, &are, &git, &ifo, 1, &sc, &a, err) != 0 && strstr(err, "henchman"));
    gff_free(&are);
    gff_free(&git);
    gff_free(&ifo);
    tilepack_free(&tp);
    free(tp_text);
    free(rtext);
    free(htext);
}

static void test_loot_plan(void)
{
    size_t n, rn, ln, dn, i, k;
    char *tp_text = slurp("tests/data/tilepack_test.txt", &n), *rtext = slurp("tests/data/recipe_test.txt", &rn),
         *ltext = slurp("tests/data/roster_loot_test.txt", &ln), *dtext = slurp("tests/data/roster_deco_test.txt", &dn);
    tilepack tp;
    recipe r;
    roster rl, rd;
    gff are, git, ifo;
    dungeon a, b, c;
    char err[ERRLEN] = "";
    spawn_cfg sc = {0}, sd = {0};
    uint64_t seed;
    int with_loot = 0, boss_loot = 0;

    CHECK(tp_text && rtext && ltext && dtext);
    if (!tp_text || !rtext || !ltext || !dtext)
        return;
    CHECK(tilepack_parse(tp_text, n, &tp, err) == 0 && recipe_parse(rtext, rn, "tests/data", &r, err) == 0);
    CHECK(roster_parse(ltext, ln, &rl, err) == 0 && roster_parse(dtext, dn, &rd, err) == 0);
    CHECK(fx_are(&are) == 0 && fx_git(&git) == 0 && fx_ifo(&ifo) == 0);

    for (seed = 1; seed <= 60; seed++) {
        int avg;
        sc.ro = &rl; sd.ro = &rd;
        sc.party = sd.party = 1 + (unsigned)(seed % 4);
        sc.min_level = sd.min_level = 1 + (unsigned)(seed % 4);
        sc.max_level = sd.max_level = sc.min_level + (unsigned)(seed % 2);
        avg = (int)(sc.min_level + sc.max_level) / 2;
        CHECK(dungeon_build_ex(&r, &tp, &are, &git, &ifo, seed, &sc, &a, err) == 0);
        CHECK(dungeon_build_ex(&r, &tp, &are, &git, &ifo, seed, &sc, &b, err) == 0);
        CHECK(dungeon_build_ex(&r, &tp, &are, &git, &ifo, seed, &sd, &c, err) == 0);
        CHECK(a.theme == c.theme && a.n == c.n);
        for (i = 0; i < a.n; i++) {
            const roster_theme *th = &rl.theme[a.theme];
            CHECK(a.area[i].nsp == b.area[i].nsp && a.area[i].nsp == c.area[i].nsp);
            for (k = 0; k < a.area[i].nsp && k < c.area[i].nsp; k++) {
                const spawn_pt *p = &a.area[i].sp[k], *q = &b.area[i].sp[k], *o = &c.area[i].sp[k];
                int cr = unit_theme_cr(th, p->bp), j;
                /* same seed, same loot; and the loot lines move nothing else (own RNG domain) */
                CHECK(!memcmp(p->loot, q->loot, sizeof p->loot) && p->gold == q->gold);
                CHECK(p->kind == o->kind && !strcmp(p->bp, o->bp) && p->cx == o->cx && p->cy == o->cy && p->face == o->face);
                CHECK(!o->loot[0][0] && !o->loot[1][0] && !o->gold); /* no loot lines, no loot */
                if (p->kind != 'C' && p->kind != 'B') {
                    CHECK(!p->loot[0][0] && !p->loot[1][0] && !p->gold);
                    continue;
                }
                if (a.theme == 1) { /* the second theme has no loot lines */
                    CHECK(!p->loot[0][0] && !p->loot[1][0] && !p->gold);
                    continue;
                }
                for (j = 0; j < 2; j++) {
                    size_t e;
                    int ok = !p->loot[j][0];
                    for (e = 0; e < th->nloot; e++)
                        ok |= !strcmp(th->loot[e].resref, p->loot[j]) && cr >= th->loot[e].min_cr && cr <= th->loot[e].max_cr;
                    CHECK(ok);
                    with_loot += p->loot[j][0] != 0;
                }
                if (cr <= 60)
                    CHECK(!strcmp(p->loot[0], "t_l_low")); /* a certain drop in this band */
                if (p->kind == 'B')
                    boss_loot += p->loot[0][0] != 0;
                {
                    long crh = cr < 25 ? 25 : cr, lo = 10L * crh * 50 * (100 + 25 * (avg - 1)) / 1000000, hi = 10L * crh * 150 * (100 + 25 * (avg - 1)) / 1000000;
                    if (p->kind == 'B') { lo *= 3; hi *= 3; }
                    CHECK(p->gold >= 1 && p->gold >= lo && p->gold <= hi + 3);
                }
            }
        }
        dungeon_free(&a);
        dungeon_free(&b);
        dungeon_free(&c);
    }
    CHECK(with_loot > 20 && boss_loot > 0);
    tilepack_free(&tp);
    gff_free(&are); gff_free(&git); gff_free(&ifo);
    free(tp_text); free(rtext); free(ltext); free(dtext);
}

static void test_roster_deco(void)
{
    roster ro;
    char err[ERRLEN] = "";
    size_t n;
    char *text = slurp("tests/data/roster_deco_test.txt", &n);
    static const char HEAD[] = "nwndgen-roster 1\nvault v\nkey k\nrelic r\ntheme a A\nminion m 1\nboss b 1\n";
    char buf[1024], many[512] = "";
    int i;

    CHECK(text && roster_parse(text, n, &ro, err) == 0);
    CHECK(ro.theme[0].ndeco[DECO_PROP] == 2 && ro.theme[0].ndeco[DECO_LIGHT] == 1 && ro.theme[0].ndeco[DECO_CONTAINER] == 1);
    CHECK(!strcmp(ro.theme[0].deco[DECO_PROP][0].resref, "t_a_p1") && ro.theme[0].deco[DECO_PROP][0].cr == 3);
    CHECK(ro.theme[0].has_ambience && ro.theme[0].amb_ambient == 0x302010u && ro.theme[0].amb_diffuse == 0xc0b0a0u &&
          ro.theme[0].amb_fog == 0x0000ffu && ro.theme[0].amb_fog_amount == 7 && ro.theme[0].amb_fog_clip == 60);
    CHECK(!ro.theme[1].has_ambience && ro.theme[1].ndeco[DECO_CONTAINER] == 2 && ro.theme[1].deco[DECO_CONTAINER][0].cr == 5);
    free(text);

    /* a roster without scenery lines parses to empty lists */
    text = slurp("tests/data/roster_test.txt", &n);
    CHECK(text && roster_parse(text, n, &ro, err) == 0);
    CHECK(!ro.theme[0].ndeco[DECO_PROP] && !ro.theme[0].ndeco[DECO_LIGHT] && !ro.theme[0].ndeco[DECO_CONTAINER] && !ro.theme[0].has_ambience);
    free(text);

#define BAD_DECO(body, frag) \
    do { snprintf(buf, sizeof buf, "%s%s", HEAD, body); err[0] = 0; \
         CHECK(parse_roster(buf, &ro, err) != 0 && strstr(err, frag)); } while (0)
#define GOOD_DECO(body) \
    do { snprintf(buf, sizeof buf, "%s%s", HEAD, body); err[0] = 0; CHECK(parse_roster(buf, &ro, err) == 0); } while (0)
    GOOD_DECO("prop p1 1\nlight l1 100\ncontainer c1 50\nambience 000000 ffffff 808080 0 20\n");
    GOOD_DECO("ambience 000000 FFFFFF 80aB80 15 150\n");
    CHECK(parse_roster("nwndgen-roster 1\nvault v\nkey k\nrelic r\nprop p 1\ntheme a A\nminion m 1\nboss b 1\n", &ro, err) != 0 && strstr(err, "before any theme"));
    CHECK(parse_roster("nwndgen-roster 1\nvault v\nkey k\nrelic r\nambience 000000 000000 000000 1 50\ntheme a A\nminion m 1\nboss b 1\n", &ro, err) != 0 &&
          strstr(err, "before any theme"));
    BAD_DECO("prop p\n", "resref weight");
    BAD_DECO("prop P 1\n", "resref");
    BAD_DECO("prop p 0\n", "weight");
    BAD_DECO("prop p 101\n", "weight");
    BAD_DECO("prop p 1.5\n", "weight");
    BAD_DECO("light l x\n", "weight");
    BAD_DECO("prop p 1\nprop p 2\n", "duplicate scenery");
    GOOD_DECO("prop p 1\nlight p 1\n"); /* the same resref in different lists is the roster author's business */
    BAD_DECO("ambience 000000 000000 000000 1\n", "3 colours");
    BAD_DECO("ambience 000000 000000\n", "3 colours");
    BAD_DECO("ambience 00000 000000 000000 1 50\n", "six hex digits");
    BAD_DECO("ambience 00000g 000000 000000 1 50\n", "six hex digits");
    BAD_DECO("ambience 000000 000000 000000 16 50\n", "fog amount");
    BAD_DECO("ambience 000000 000000 000000 -1 50\n", "fog amount");
    BAD_DECO("ambience 000000 000000 000000 1 19\n", "fog clip");
    BAD_DECO("ambience 000000 000000 000000 1 151\n", "fog clip");
    BAD_DECO("ambience 000000 000000 000000 1 50\nambience 000000 000000 000000 1 50\n", "twice");
    for (i = 0; i < ROSTER_POOL + 1; i++) {
        char line[40];
        snprintf(line, sizeof line, "prop p%d 1\n", i);
        strncat(many, line, sizeof many - strlen(many) - 1);
    }
    BAD_DECO(many, "too many");
#undef BAD_DECO
#undef GOOD_DECO
}

/* flood fill over full-floor tiles; centre-slot scenery tiles are removed first */
static int deco_floor_connected(const layout *l, const dungeon_area *a)
{
    enum { MAXC = 64 };
    char open[MAXC][MAXC], seen[MAXC][MAXC];
    int stack[MAXC * MAXC][2], sp = 0, total = 0, reached = 0, x, y;
    size_t k;
    memset(open, 0, sizeof open);
    memset(seen, 0, sizeof seen);
    for (y = 0; y < l->h; y++)
        for (x = 0; x < l->w; x++)
            open[y][x] = (char)layout_full_floor(l, x, y);
    for (k = 0; k < a->nsp; k++)
        if (strchr("DLX", a->sp[k].kind) && a->sp[k].ox == 0 && a->sp[k].oy == 0)
            open[a->sp[k].cy][a->sp[k].cx] = 0;
    for (y = 0; y < l->h; y++)
        for (x = 0; x < l->w; x++)
            if (open[y][x]) {
                total++;
                if (!sp && !reached) {
                    stack[sp][0] = x; stack[sp++][1] = y; seen[y][x] = 1;
                    while (sp) {
                        static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
                        int cx = stack[--sp][0], cy = stack[sp][1], d;
                        reached++;
                        for (d = 0; d < 4; d++) {
                            int nx = cx + dx[d], ny = cy + dy[d];
                            if (nx >= 0 && ny >= 0 && nx < l->w && ny < l->h && open[ny][nx] && !seen[ny][nx]) {
                                seen[ny][nx] = 1;
                                stack[sp][0] = nx; stack[sp++][1] = ny;
                            }
                        }
                    }
                }
            }
    return reached == total;
}

static void test_deco(void)
{
    size_t n, rn, yn, dn, si, i, k, j;
    char *tp_text = slurp("tests/data/tilepack_test.txt", &n), *rtext = slurp("tests/data/recipe_test.txt", &rn),
         *ytext = slurp("tests/data/roster_test.txt", &yn), *dtext = slurp("tests/data/roster_deco_test.txt", &dn);
    tilepack tp;
    recipe r;
    roster plain_ro, ro;
    gff are, git, ifo, g, g2;
    dungeon base, d;
    char err[ERRLEN] = "";
    static const uint64_t seeds[] = {0, 1, 12345, UINT64_C(18446744073709551615)};
    static const int bands[3][3] = {{1, 2, 1}, {2, 4, 3}, {3, 4, 4}};
    spawn_cfg sc = {0};
    int tot[DECO_N] = {0, 0, 0}, ambience_seen = 0, no_ambience_seen = 0, centre_seen = 0, corner_seen = 0;

    CHECK(tp_text && rtext && ytext && dtext);
    if (!tp_text || !rtext || !ytext || !dtext)
        return;
    CHECK(tilepack_parse(tp_text, n, &tp, err) == 0 && recipe_parse(rtext, rn, "tests/data", &r, err) == 0);
    CHECK(roster_parse(ytext, yn, &plain_ro, err) == 0 && roster_parse(dtext, dn, &ro, err) == 0);
    CHECK(fx_are(&are) == 0 && fx_git(&git) == 0 && fx_ifo(&ifo) == 0);

    for (si = 0; si < sizeof seeds / sizeof *seeds; si++) {
        int bi;
        for (bi = 0; bi < 3; bi++) {
            const roster_theme *th;
            char *dump_a, *dump_b;
            dungeon again;
            sc.party = (unsigned)bands[bi][2]; sc.min_level = (unsigned)bands[bi][0]; sc.max_level = (unsigned)bands[bi][1];
            sc.ro = &plain_ro;
            CHECK(dungeon_build_ex(&r, &tp, &are, &git, &ifo, seeds[si], &sc, &base, err) == 0);
            sc.ro = &ro;
            CHECK(dungeon_build_ex(&r, &tp, &are, &git, &ifo, seeds[si], &sc, &d, err) == 0);
            CHECK(dungeon_build_ex(&r, &tp, &are, &git, &ifo, seeds[si], &sc, &again, err) == 0);
            CHECK(d.theme == base.theme && d.n == base.n);
            th = &ro.theme[d.theme];

            dump_a = dungeon_dump(&base, seeds[si]);
            dump_b = dungeon_dump(&d, seeds[si]);
            CHECK(!strcmp(dump_a, dump_b)); /* layouts and tiles do not move */
            free(dump_a);
            free(dump_b);

            for (i = 0; i < d.n; i++) {
                const dungeon_area *a = &d.area[i], *b = &base.area[i];
                size_t nb = 0, ndeco[DECO_N] = {0, 0, 0};
                int lx[8], ly[8], nl = 0;

                /* creatures and the objective are exactly what the scenery-less roster makes, in the same order */
                for (k = 0; k < a->nsp; k++) {
                    const spawn_pt *p = &a->sp[k];
                    if (strchr("DLX", p->kind))
                        continue;
                    CHECK(nb < b->nsp && p->kind == b->sp[nb].kind && !strcmp(p->bp, b->sp[nb].bp) && p->cx == b->sp[nb].cx &&
                          p->cy == b->sp[nb].cy && p->face == b->sp[nb].face && p->ox == 0 && p->oy == 0);
                    nb++;
                }
                CHECK(nb == b->nsp && a->nsp <= SPAWN_PT_MAX);

                for (k = 0; k < a->nsp; k++) {
                    const spawn_pt *p = &a->sp[k];
                    int kind = p->kind == 'D' ? DECO_PROP : p->kind == 'L' ? DECO_LIGHT : p->kind == 'X' ? DECO_CONTAINER : -1, inroom = 0, rr;
                    if (kind < 0)
                        continue;
                    ndeco[kind]++;
                    tot[kind]++;
                    CHECK(deco_has(th, kind, p->bp));
                    CHECK(p->face >= 0 && p->face < 8);
                    CHECK(p->ox >= -1 && p->ox <= 1 && p->oy >= -1 && p->oy <= 1 && (p->ox == 0) == (p->oy == 0));
                    if (p->ox) corner_seen = 1; else centre_seen = 1;
                    CHECK(layout_full_floor(&a->lay, p->cx, p->cy));
                    for (rr = 0; rr < a->lay.nrooms; rr++)
                        inroom |= p->cx >= a->lay.room[rr][0] && p->cx < a->lay.room[rr][2] && p->cy >= a->lay.room[rr][1] && p->cy < a->lay.room[rr][3];
                    CHECK(inroom); /* never in a corridor */
                    if (a->has_start) CHECK(cheb(p->cx, p->cy, a->pair_start[0], a->pair_start[1]) >= 2 && cheb(p->cx, p->cy, a->pair_start[2], a->pair_start[3]) >= 2);
                    if (a->has_prev) CHECK(cheb(p->cx, p->cy, a->pair_prev[0], a->pair_prev[1]) >= 2 && cheb(p->cx, p->cy, a->pair_prev[2], a->pair_prev[3]) >= 2);
                    if (a->has_next) CHECK(cheb(p->cx, p->cy, a->pair_next[0], a->pair_next[1]) >= 2 && cheb(p->cx, p->cy, a->pair_next[2], a->pair_next[3]) >= 2);
                    for (j = 0; j < a->nsp; j++) {
                        const spawn_pt *q = &a->sp[j];
                        double dx, dy;
                        if (j == k || q->kind == 'K' || q->kind == 'R')
                            continue;
                        dx = (q->cx - p->cx) * 10.0 + 3.5 * (q->ox - p->ox);
                        dy = (q->cy - p->cy) * 10.0 + 3.5 * (q->oy - p->oy);
                        CHECK(dx >= 2.0 || dx <= -2.0 || dy >= 2.0 || dy <= -2.0); /* never stacked on another marker */
                        if (q->kind == 'B' || q->kind == 'V')
                            CHECK(cheb(p->cx, p->cy, q->cx, q->cy) >= 2);
                    }
                    if (p->kind == 'L') {
                        int m;
                        CHECK(p->ox != 0 && nl < 8); /* lights stand in a tile corner and are spread out */
                        for (m = 0; m < nl; m++)
                            CHECK(abs(lx[m] - p->cx) >= 3 || abs(ly[m] - p->cy) >= 3);
                        if (nl < 8) { lx[nl] = p->cx; ly[nl++] = p->cy; }
                    }
                }
                CHECK(ndeco[DECO_LIGHT] <= 4 && ndeco[DECO_CONTAINER] <= 2 && ndeco[DECO_PROP] <= 12);
                CHECK(deco_floor_connected(&a->lay, a));
            }

            /* area light: only a theme with an ambience line changes the ARE */
            for (i = 0; i < d.n; i++) {
                CHECK(gff_parse(d.area[i].are, d.area[i].nare, &g, err) == 0 && gff_parse(base.area[i].are, base.area[i].nare, &g2, err) == 0);
                if (th->has_ambience) {
                    CHECK(gff_get_int(g.root, "MoonAmbientColor", -1) == (int64_t)th->amb_ambient);
                    CHECK(gff_get_int(g.root, "MoonDiffuseColor", -1) == (int64_t)th->amb_diffuse);
                    CHECK(gff_get_int(g.root, "MoonFogColor", -1) == (int64_t)th->amb_fog);
                    CHECK(gff_get_int(g.root, "MoonFogAmount", -1) == th->amb_fog_amount);
                    CHECK(gff_get_float(g.root, "FogClipDist", -1) == (float)th->amb_fog_clip);
                    ambience_seen = 1;
                } else {
                    CHECK(d.area[i].nare == base.area[i].nare && !memcmp(d.area[i].are, base.area[i].are, d.area[i].nare));
                    no_ambience_seen = 1;
                }
                gff_free(&g);
                gff_free(&g2);
            }

            /* markers reach the GIT at their offset position, and the run is reproducible */
            for (i = 0; i < d.n; i++) {
                const gff_field *wl;
                size_t found = 0;
                CHECK(gff_parse(d.area[i].git, d.area[i].ngit, &g, err) == 0);
                wl = gff_find(g.root, "WaypointList");
                for (k = 0; wl && k < wl->nlist; k++) {
                    const char *t = gff_get_string(wl->list[k], "Tag");
                    if (t && !strncmp(t, "SP", 2) && strchr("DLX", t[2])) {
                        double x = gff_get_float(wl->list[k], "XPosition", -1), y = gff_get_float(wl->list[k], "YPosition", -1);
                        int cx = (int)(x / 10.0), cy = (int)(y / 10.0), hit = 0;
                        for (j = 0; j < d.area[i].nsp; j++) {
                            const spawn_pt *p = &d.area[i].sp[j];
                            double ex = p->cx * 10.0 + 5.0 + 3.5 * p->ox, ey = p->cy * 10.0 + 5.0 + 3.5 * p->oy;
                            if (p->cx == cx && p->cy == cy && p->kind == t[2] && !strcmp(p->bp, t + 4) && x > ex - 0.01 && x < ex + 0.01 && y > ey - 0.01 && y < ey + 0.01)
                                hit = 1;
                        }
                        CHECK(hit);
                        found++;
                    }
                }
                {
                    size_t want = 0;
                    for (k = 0; k < d.area[i].nsp; k++)
                        want += strchr("DLX", d.area[i].sp[k].kind) != NULL;
                    CHECK(found == want);
                }
                gff_free(&g);
                CHECK(again.area[i].ngit == d.area[i].ngit && !memcmp(again.area[i].git, d.area[i].git, d.area[i].ngit));
                CHECK(again.area[i].ngic == d.area[i].ngic && !memcmp(again.area[i].gic, d.area[i].gic, d.area[i].ngic));
            }
            dungeon_free(&base);
            dungeon_free(&d);
            dungeon_free(&again);
        }
    }
    CHECK(tot[DECO_PROP] > 0 && tot[DECO_LIGHT] > 0 && tot[DECO_CONTAINER] > 0);
    CHECK(ambience_seen && no_ambience_seen && corner_seen);
    (void)centre_seen; /* centre slots are a last resort and need not occur */

    /* boss_only runs stay bare: scenery is a full-run feature */
    sc.ro = &ro; sc.party = 1; sc.min_level = 1; sc.max_level = 2; sc.boss_only = 1;
    CHECK(dungeon_build_ex(&r, &tp, &are, &git, &ifo, 12345, &sc, &d, err) == 0);
    for (i = 0; i < d.n; i++)
        CHECK(d.area[i].nsp == (i == 0 ? 4u : 0u));
    dungeon_free(&d);

    tilepack_free(&tp);
    gff_free(&are);
    gff_free(&git);
    gff_free(&ifo);
    free(tp_text);
    free(rtext);
    free(ytext);
    free(dtext);
}

static int rprop_has(const roster *ro, const char *bp)
{
    int rt;
    size_t i;
    for (rt = 0; rt < RT_N; rt++)
        for (i = 0; i < ro->nrprop[rt]; i++)
            if (!strcmp(ro->rprop[rt][i].resref, bp))
                return 1;
    return 0;
}

static void test_roster_roomprop(void)
{
    size_t n;
    char *text = slurp("tests/data/roster_dress_test.txt", &n), err[ERRLEN] = "";
    roster ro;
    char many[2048];
    int i;

    CHECK(text != NULL);
    if (!text)
        return;
    CHECK(roster_parse(text, n, &ro, err) == 0);
    CHECK(ro.nrprop[RT_BARRACKS] == 3 && ro.nrprop[RT_STORAGE] == 1 && ro.nrprop[RT_SHRINE] == 2 && ro.nrprop[RT_LIBRARY] == 3 && ro.nrprop[RT_CRYPT] == 3 && ro.nrprop[RT_PRISON] == 2);
    CHECK(!strcmp(ro.rprop[RT_BARRACKS][0].resref, "t_bed") && ro.rprop[RT_BARRACKS][0].cr == 4 && !ro.rcentre[RT_BARRACKS][0]);
    CHECK(!strcmp(ro.rprop[RT_BARRACKS][2].resref, "t_table") && ro.rprop[RT_BARRACKS][2].cr == 2 && ro.rcentre[RT_BARRACKS][2]);
    free(text);
#define RP_BAD(body, frag) do { char t_[512]; snprintf(t_, sizeof t_, "nwndgen-roster 1\n%s", body); CHECK(roster_parse(t_, strlen(t_), &ro, err) != 0 && strstr(err, frag)); } while (0)
#define RP_GOOD(body) do { char t_[512]; snprintf(t_, sizeof t_, "nwndgen-roster 1\n%s", body); CHECK(roster_parse(t_, strlen(t_), &ro, err) == 0); } while (0)
    RP_GOOD("vault v\nkey k\nrelic r\ntheme a A\nminion m 1\nboss b 1\nroomprop crypt p 1\n");
    RP_GOOD("vault v\nkey k\nrelic r\nroomprop crypt p 100\nroomprop storage p 5\ntheme a A\nminion m 1\nboss b 1\n"); /* global, and the same resref under two types is allowed */
    RP_BAD("roomprop crypt p\n", "type resref weight");
    RP_BAD("roomprop crypt\n", "type resref weight");
    RP_BAD("roomprop dungeon p 1\n", "roomprop type");
    RP_BAD("roomprop crypt P 1\n", "resref");
    RP_BAD("roomprop crypt p 0\n", "weight");
    RP_BAD("roomprop crypt p 101\n", "weight");
    RP_BAD("roomprop crypt p 1.5\n", "weight");
    RP_BAD("roomprop crypt p 1 middle\n", "centre");
    RP_BAD("roomprop crypt p centre\n", "weight");
    RP_GOOD("vault v\nkey k\nrelic r\ntheme a A\nminion m 1\nboss b 1\nroomprop crypt p 7 centre\n");
    RP_BAD("roomprop crypt p 1\nroomprop crypt p 2\n", "duplicate roomprop");
    many[0] = '\0';
    strcat(many, "nwndgen-roster 1\n");
    for (i = 0; i < ROSTER_POOL + 1; i++)
        snprintf(many + strlen(many), sizeof many - strlen(many), "roomprop crypt p%d 1\n", i);
    CHECK(roster_parse(many, strlen(many), &ro, err) != 0 && strstr(err, "too many roomprop"));
    RP_GOOD("vault v\nkey k\nrelic r\ncorridorprop c1 5\ncorridorprop c2 100\ntheme a A\nminion m 1\nboss b 1\n");
    CHECK(ro.ncprop == 2 && !strcmp(ro.cprop[1].resref, "c2") && ro.cprop[1].cr == 100);
    RP_BAD("corridorprop c\n", "resref weight");
    RP_BAD("corridorprop C 1\n", "resref");
    RP_BAD("corridorprop c 0\n", "weight");
    RP_BAD("corridorprop c 101\n", "weight");
    RP_BAD("corridorprop c 1\ncorridorprop c 2\n", "duplicate corridorprop");
    many[0] = '\0';
    strcat(many, "nwndgen-roster 1\n");
    for (i = 0; i < ROSTER_POOL + 1; i++)
        snprintf(many + strlen(many), sizeof many - strlen(many), "corridorprop p%d 1\n", i);
    CHECK(roster_parse(many, strlen(many), &ro, err) != 0 && strstr(err, "too many corridorprop"));
#undef RP_BAD
#undef RP_GOOD
}

static void test_dressing(void)
{
    size_t n, rn, yn, dn, si, i, k, j, m;
    char *tp_text = slurp("tests/data/tilepack_test.txt", &n), *rtext = slurp("tests/data/recipe_test.txt", &rn),
         *dtext = slurp("tests/data/roster_dress_test.txt", &dn), *ytext = slurp("tests/data/roster_deco_test.txt", &yn);
    tilepack tp;
    recipe r;
    roster ro, plain;
    gff are, git, ifo;
    char err[ERRLEN] = "";
    static const uint64_t seeds[] = {0, 1, 12345, 777, UINT64_C(18446744073709551615)};
    spawn_cfg sc = {0};
    long extras[4] = {0, 0, 0, 0}, centres = 0, edges = 0;
    int pair_seen = 0, types_seen[RT_N] = {0};

    CHECK(tp_text && rtext && dtext && ytext);
    if (!tp_text || !rtext || !dtext || !ytext)
        return;
    CHECK(tilepack_parse(tp_text, n, &tp, err) == 0 && recipe_parse(rtext, rn, "tests/data", &r, err) == 0);
    CHECK(roster_parse(dtext, dn, &ro, err) == 0 && roster_parse(ytext, yn, &plain, err) == 0);
    CHECK(fx_are(&are) == 0 && fx_git(&git) == 0 && fx_ifo(&ifo) == 0);
    sc.party = 2; sc.min_level = 2; sc.max_level = 4;

    for (si = 0; si < sizeof seeds / sizeof *seeds; si++) {
        dungeon off, d[4], again;
        int lv;
        sc.ro = &ro; sc.dressing = 0;
        CHECK(dungeon_build_ex(&r, &tp, &are, &git, &ifo, seeds[si], &sc, &off, err) == 0);
        for (lv = 1; lv <= 3; lv++) {
            sc.dressing = lv;
            CHECK(dungeon_build_ex(&r, &tp, &are, &git, &ifo, seeds[si], &sc, &d[lv], err) == 0);
        }
        sc.dressing = 3;
        CHECK(dungeon_build_ex(&r, &tp, &are, &git, &ifo, seeds[si], &sc, &again, err) == 0);
        for (i = 0; i < off.n; i++)
            for (k = 0; k < (size_t)off.area[i].lay.nrooms; k++)
                CHECK(off.area[i].rtype[k] == 0);
        for (lv = 1; lv <= 3; lv++)
            for (i = 0; i < off.n; i++) {
                const dungeon_area *a = &d[lv].area[i], *b = &off.area[i];
                int perroom[RECIPE_ROOMS_MAX] = {0}, ncentre[RECIPE_ROOMS_MAX] = {0};
                CHECK(a->nsp <= SPAWN_PT_MAX);
                /* the plain run's markers that are not furniture keep their order and content; only generic props (the last
                 * markers of the scenery plan) are thinned, so what remains is a prefix of the plain run */
                {
                    size_t ia = 0, ib = 0;
                    for (ia = 0; ia < a->nsp; ia++) {
                        if (strchr("D", a->sp[ia].kind) && rprop_has(&ro, a->sp[ia].bp))
                            continue;
                        CHECK(ib < b->nsp && !memcmp(&a->sp[ia], &b->sp[ib], sizeof a->sp[ia]));
                        ib++;
                    }
                    for (; ib < b->nsp; ib++)
                        CHECK(b->sp[ib].kind == 'D');
                }
                for (k = 0; k < (size_t)a->lay.nrooms; k++) {
                    CHECK(a->rtype[k] >= 1 && a->rtype[k] <= RT_N);
                    types_seen[a->rtype[k] - 1] = 1;
                }
                for (k = 0; k < a->nsp; k++) {
                    const spawn_pt *p = &a->sp[k];
                    int room = -1, rr, rt = a->rtype[0] - 1, okbp = 0, centre_piece = p->ox == 0 && p->oy == 0;
                    if (p->kind != 'D' || !rprop_has(&ro, p->bp))
                        continue;
                    if (!centre_piece) { /* a corner (+-1,+-1) or the middle of a wall side (+-1,0), (0,+-1) whose neighbour tile is not floor */
                        int edge_piece = !p->ox || !p->oy;
                        CHECK(abs(p->ox) <= 1 && abs(p->oy) <= 1);
                        if (edge_piece) {
                            int nx = p->cx + p->ox, ny = p->cy + p->oy;
                            CHECK(nx < 0 || ny < 0 || nx >= a->lay.w || ny >= a->lay.h || !layout_full_floor(&a->lay, nx, ny));
                            edges++;
                            CHECK(p->custom == 1 && p->xcm == 450 * p->ox && p->ycm == 450 * p->oy);
                            /* faces into the room, away from its wall */
                            CHECK(p->face == (p->ox > 0 ? 4 : p->ox < 0 ? 0 : p->oy > 0 ? 6 : 2));
                        }
                    }
                    for (rr = 0; rr < a->lay.nrooms; rr++)
                        if (p->cx >= a->lay.room[rr][0] && p->cx < a->lay.room[rr][2] && p->cy >= a->lay.room[rr][1] && p->cy < a->lay.room[rr][3])
                            room = rr;
                    CHECK(room >= 0);
                    if (room >= 0) {
                        size_t e;
                        rt = a->rtype[room] - 1;
                        for (e = 0; e < ro.nrprop[rt]; e++)
                            okbp |= !strcmp(ro.rprop[rt][e].resref, p->bp) && ro.rcentre[rt][e] == (unsigned char)centre_piece;
                        CHECK(okbp); /* furniture comes from the room's own type, centrepieces from its centre list */
                        if (centre_piece) {
                            CHECK(lv >= 2 && ncentre[room] == 0); /* at most one, not at low */
                            ncentre[room]++;
                        } else
                            perroom[room]++;
                    }
                    extras[lv]++;
                    centres += centre_piece;
                    for (m = 0; m < k; m++) { /* never two pieces in one corner or on one centre tile */
                        const spawn_pt *q = &a->sp[m];
                        if (strchr("DLX", q->kind))
                            CHECK(!(q->cx == p->cx && q->cy == p->cy && q->ox == p->ox && q->oy == p->oy));
                        if (strchr("DLX", q->kind) && rprop_has(&ro, q->bp) && abs(q->cx - p->cx) + abs(q->cy - p->cy) == 1 && q->ox && p->ox && q->oy && p->oy && (q->ox == p->ox) != (q->oy == p->oy))
                            pair_seen |= lv == 3;
                    }
                }
                for (k = 0; k < (size_t)a->lay.nrooms; k++)
                    CHECK(perroom[k] <= 2 * (lv == 1 ? 3 : lv == 2 ? 5 : 8)); /* 3, 5 or 8 pieces, each possibly a pair */
                if (lv == 1) /* three pieces per room, never a pair */
                    for (k = 0; k < (size_t)a->lay.nrooms; k++)
                        CHECK(perroom[k] <= 3);
                CHECK(deco_floor_connected(&a->lay, a)); /* centrepieces never cut the floor */
            }
        for (i = 0; i < again.n; i++) {
            CHECK(again.area[i].nsp == d[3].area[i].nsp && !memcmp(again.area[i].sp, d[3].area[i].sp, d[3].area[i].nsp * sizeof(spawn_pt)));
            CHECK(!memcmp(again.area[i].rtype, d[3].area[i].rtype, sizeof again.area[i].rtype));
            CHECK(again.area[i].ngit == d[3].area[i].ngit && !memcmp(again.area[i].git, d[3].area[i].git, d[3].area[i].ngit));
        }
        for (j = 0; j < 1; j++) { /* layout and tiles never move */
            char *x = dungeon_dump(&off, seeds[si]), *y = dungeon_dump(&d[3], seeds[si]);
            CHECK(!strcmp(x, y));
            free(x);
            free(y);
        }
        dungeon_free(&off);
        for (lv = 1; lv <= 3; lv++)
            dungeon_free(&d[lv]);
        dungeon_free(&again);
    }
    CHECK(extras[1] > 0 && extras[2] > extras[1] && extras[3] > extras[2]);
    CHECK(pair_seen && centres > 0 && edges > 0);
    for (i = 0; i < RT_N; i++)
        CHECK(types_seen[i]);

    /* a roster without roomprop lines is left alone, boss_only runs stay bare */
    {
        dungeon d0, d1;
        sc.ro = &plain; sc.dressing = 0;
        CHECK(dungeon_build_ex(&r, &tp, &are, &git, &ifo, 12345, &sc, &d0, err) == 0);
        sc.dressing = 3;
        CHECK(dungeon_build_ex(&r, &tp, &are, &git, &ifo, 12345, &sc, &d1, err) == 0);
        for (i = 0; i < d0.n; i++) {
            CHECK(d0.area[i].nsp == d1.area[i].nsp && !memcmp(d0.area[i].sp, d1.area[i].sp, d0.area[i].nsp * sizeof(spawn_pt)));
            for (k = 0; k < RECIPE_ROOMS_MAX; k++)
                CHECK(d1.area[i].rtype[k] == 0);
        }
        dungeon_free(&d0);
        dungeon_free(&d1);
        sc.ro = &ro; sc.boss_only = 1;
        CHECK(dungeon_build_ex(&r, &tp, &are, &git, &ifo, 12345, &sc, &d1, err) == 0);
        for (i = 0; i < d1.n; i++)
            CHECK(d1.area[i].nsp == (i == 0 ? 4u : 0u));
        dungeon_free(&d1);
    }
    tilepack_free(&tp);
    gff_free(&are);
    gff_free(&git);
    gff_free(&ifo);
    free(tp_text);
    free(rtext);
    free(dtext);
    free(ytext);
}

/* ---------- tileset / walkmesh / 2DA ---------- */

static const char SET_TEXT[] =
    "[GENERAL]\nName=T\n[TILES]\nCount=3\n\n"
    "[TILE0]\nModel=m0\nWalkMesh=msb01\nTopLeft=Wall\nTopLeftHeight=0\nTopRight=Floor\nTopRightHeight=0\n"
    "BottomLeft=Wall\nBottomLeftHeight=0\nBottomRight=Floor\nBottomRightHeight=0\nTop=\nRight=\nBottom=\nLeft=\nDoors=0\n"
    "[TILE1]\nModel=m1\nTopLeft=Floor\nTopRight=Wall\nBottomLeft=Floor\nBottomRight=Wall\nTop=\nRight=\nBottom=\nLeft=\nDoors=1\n"
    "[TILE2]\nModel=m2\nTopLeft=Floor\nTopRight=Floor\nBottomLeft=Floor\nBottomRight=Floor\nTopLeftHeight=2\nTop=Door\nRight=\nBottom=\nLeft=\n";

static void test_tileset(void)
{
    tileset ts;
    tile_sig_t sg;
    char err[ERRLEN] = "";
    int tid[2], ori[2];
    wok w;
    unsigned char ok[16];
    static const char WOK[] = "# Exported\nnode trimesh x\nbeginwalkmeshgeom\n verts 4\n -5 -5 0\n 5 -5 0\n 5 5 0\n -5 5 0\n"
                              " faces 2\n 0 1 2 0 0 0 0 0 4\n 0 2 3 0 0 0 0 0 4\nendwalkmeshgeom\n";
    static const char TDA[] = "2DA V2.0\n\n   Label Walk Radius\n0  Nothing 0 1\n1  Dirt 1 2\n2  Wall 0 3\n3  Stone 1 4\n4 Floor 1 5\n";

    CHECK(tileset_parse(SET_TEXT, strlen(SET_TEXT), &ts, err) == 0 && ts.n == 3);
    CHECK(!strcmp(ts.t[0].model, "m0") && !strcmp(ts.t[0].corner[C_NE], "Floor") && ts.t[1].doors == 1 && ts.t[2].cheight[C_NW] == 2);
    CHECK(!strcmp(ts.t[2].edge[E_N], "Door") && ts.t[2].edge[E_E][0] == '\0');
    /* rotation: one counter-clockwise quarter turn sends NW->SW, SW->SE, SE->NE, NE->NW */
    tile_sig(&ts.t[0], 0, 1, &sg);
    CHECK(!strcmp(sg.corner[C_NW], "Wall") && !strcmp(sg.corner[C_NE], "Floor") && !strcmp(sg.corner[C_SW], "Wall"));
    tile_sig(&ts.t[0], 1, 1, &sg);    /* orig: NW=W NE=F SW=W SE=F -> NW'=F(orig NE) SW'=W(orig NW) SE'=W(orig SW) NE'=F(orig SE) */
    CHECK(!strcmp(sg.corner[C_NW], "Floor") && !strcmp(sg.corner[C_SW], "Wall") && !strcmp(sg.corner[C_SE], "Wall") && !strcmp(sg.corner[C_NE], "Floor"));
    tile_sig(&ts.t[2], 1, 1, &sg);    /* edge N -> W, heights follow corners */
    CHECK(!strcmp(sg.edge[E_W], "Door") && sg.edge[E_N][0] == '\0' && sg.cheight[C_SW] == 2);
    tile_sig(&ts.t[2], 1, 0, &sg);    /* clockwise: N -> E */
    CHECK(!strcmp(sg.edge[E_E], "Door"));
    tile_sig(&ts.t[2], 4, 1, &sg);    /* four turns = identity */
    CHECK(!strcmp(sg.edge[E_N], "Door") && sg.cheight[C_NW] == 2);
    /* adjacency: tile0 (floor on its east side) next to tile1 rotated to put floor on its west side */
    tid[0] = 0; ori[0] = 0; tid[1] = 1; ori[1] = 0;      /* tile1 floor is WEST (TopLeft/BottomLeft) */
    CHECK(tileset_violations(&ts, 2, 1, tid, ori, ORD_ROW_BOTTOM, 1, 1) == 0); /* floor meets floor on both corners */
    tid[1] = 0;                                          /* floor|wall mismatch: tile0 NE=F vs tile0 NW=W */
    CHECK(tileset_violations(&ts, 2, 1, tid, ori, ORD_ROW_BOTTOM, 1, 1) == 2);
    ori[1] = 2;                                          /* 180 degrees: NW'=F(orig SE) SW'=F(orig NE): matches */
    CHECK(tileset_violations(&ts, 2, 1, tid, ori, ORD_ROW_BOTTOM, 1, 1) == 0);
    tid[1] = 9;
    CHECK(tileset_violations(&ts, 2, 1, tid, ori, ORD_ROW_BOTTOM, 1, 1) >= 1); /* unknown tile id counted */
    tileset_free(&ts);
    CHECK(tileset_parse("[TILES]\nCount=0\n", 16, &ts, err) != 0);
    CHECK(tileset_parse("[TILES]\nCount=2\nCount=3\n", 26, &ts, err) != 0); /* repeated Count */
    CHECK(tileset_parse("[GENERAL]\n", 10, &ts, err) != 0);
    tileset_free(&ts);

    CHECK(wok_parse(WOK, &w, err) == 0 && w.nv == 4 && w.nf == 2 && w.mat[0] == 4);
    CHECK(surfacemat_walkable(TDA, ok, sizeof ok, err) == 3 && ok[1] && !ok[2] && ok[4]);
    CHECK(wok_walk_area(&w, ok, sizeof ok) > 99.99 && wok_walk_area(&w, ok, sizeof ok) < 100.01);
    CHECK(wok_point_walkable(&w, ok, sizeof ok, 0, 0) && !wok_point_walkable(&w, ok, sizeof ok, 6, 0));
    memset(ok, 0, sizeof ok);
    CHECK(wok_walk_area(&w, ok, sizeof ok) == 0 && !wok_point_walkable(&w, ok, sizeof ok, 0, 0)); /* nothing walkable */
    wok_free(&w);
    CHECK(wok_parse("garbage", &w, err) != 0);
    CHECK(wok_parse("beginwalkmeshgeom\n verts 1\n 0 0 0\n faces 1\n 0 1 2 0 0 0 0 0 4\n", &w, err) != 0); /* bad vertex index */
    CHECK(surfacemat_walkable("2DA V2.0\n\n Label Radius\n0 a 1\n", ok, sizeof ok, err) < 0);
    /* connectivity between points: one square, two squares apart, and a mesh that repeats vertex positions */
    {
        static const char SPLIT[] = "node trimesh x\nbeginwalkmeshgeom\n verts 8\n -5 -5 0\n -1 -5 0\n -1 5 0\n -5 5 0\n 1 -5 0\n 5 -5 0\n 5 5 0\n 1 5 0\n"
                                    " faces 4\n 0 1 2 0 0 0 0 0 4\n 0 2 3 0 0 0 0 0 4\n 4 5 6 0 0 0 0 0 4\n 4 6 7 0 0 0 0 0 4\nendwalkmeshgeom\n";
        static const char DUPL[] = "node trimesh x\nbeginwalkmeshgeom\n verts 6\n -5 -5 0\n 5 -5 0\n 5 5 0\n 5 5 0\n -5 5 0\n -5 -5 0\n"
                                   " faces 2\n 0 1 2 0 0 0 0 0 4\n 3 4 5 0 0 0 0 0 4\nendwalkmeshgeom\n";
        static const double AB[2][2] = {{-3, 0}, {3, 0}}, OUT[2][2] = {{-3, 0}, {8, 0}};
        wok s;
        CHECK(wok_parse(WOK, &w, err) == 0 && surfacemat_walkable(TDA, ok, sizeof ok, err) > 0);
        CHECK(wok_points_connected(&w, ok, sizeof ok, AB, 2) && wok_points_connected(&w, ok, sizeof ok, AB, 0));
        CHECK(!wok_points_connected(&w, ok, sizeof ok, OUT, 2)); /* a point off the mesh */
        wok_free(&w);
        CHECK(wok_parse(SPLIT, &s, err) == 0);
        CHECK(wok_points_connected(&s, ok, sizeof ok, AB, 1) && !wok_points_connected(&s, ok, sizeof ok, AB, 2));
        wok_free(&s);
        CHECK(wok_parse(DUPL, &s, err) == 0 && wok_points_connected(&s, ok, sizeof ok, AB, 2)); /* coincident vertices still join */
        wok_free(&s);
        ok[4] = 0;
        CHECK(wok_parse(WOK, &w, err) == 0 && !wok_points_connected(&w, ok, sizeof ok, AB, 2)); /* unwalkable surface */
        wok_free(&w);
    }
}

/* ---------- KEY / BIF reader ---------- */

static void wfile(const char *dir, const char *name, const uint8_t *d, size_t n)
{
    char p[512];
    FILE *f;
    snprintf(p, sizeof p, "%s/%s", dir, name);
    f = fopen(p, "wb");
    if (f) { fwrite(d, 1, n, f); fclose(f); }
}

/* One BIF holding `n` resources (payloads) and a KEY naming them; returns key size in *klen. */
static uint8_t *mk_key(const char *bifname, const char *const *names, const uint16_t *types, size_t n, size_t *klen)
{
    size_t ftab = 64, namelen = strlen(bifname) + 1, ktab = ftab + 12 + namelen, i;
    uint8_t *k = calloc(1, ktab + n * 22 + 16);
    memcpy(k, "KEY V1  ", 8);
    wr32le(k + 8, 1); wr32le(k + 12, (uint32_t)n); wr32le(k + 16, (uint32_t)ftab); wr32le(k + 20, (uint32_t)ktab);
    wr32le(k + ftab, 0); wr32le(k + ftab + 4, (uint32_t)(ftab + 12)); wr16le(k + ftab + 8, (uint16_t)namelen);
    memcpy(k + ftab + 12, bifname, namelen);
    for (i = 0; i < n; i++) {
        memcpy(k + ktab + i * 22, names[i], strlen(names[i]));
        wr16le(k + ktab + i * 22 + 16, types[i]);
        wr32le(k + ktab + i * 22 + 18, (uint32_t)i);   /* bif 0, index i */
    }
    *klen = ktab + n * 22;
    return k;
}

static uint8_t *mk_bif(const char *const *payloads, const uint16_t *types, size_t n, size_t *len)
{
    size_t vt = 20, data = vt + n * 16, i, pos = data;
    uint8_t *b = calloc(1, data + 256);
    memcpy(b, "BIFFV1  ", 8);
    wr32le(b + 8, (uint32_t)n); wr32le(b + 12, 0); wr32le(b + 16, (uint32_t)vt);
    for (i = 0; i < n; i++) {
        size_t l = strlen(payloads[i]);
        wr32le(b + vt + i * 16, (uint32_t)i); wr32le(b + vt + i * 16 + 4, (uint32_t)pos);
        wr32le(b + vt + i * 16 + 8, (uint32_t)l); wr32le(b + vt + i * 16 + 12, types[i]);
        memcpy(b + pos, payloads[i], l);
        pos += l;
    }
    *len = pos;
    return b;
}

static void test_resman(void)
{
    char root[] = "/tmp/nwndgen-resman-XXXXXX", dd[600], err[ERRLEN] = "";
    static const char *const NAMES[2] = {"tdc01", "other"};
    static const uint16_t TYPES[2] = {2013, 2017};
    static const char *const DATA[2] = {"[TILES]\nCount=1\n", "2DA V2.0\n"};
    resman rm;
    uint8_t *k, *b, *got;
    size_t kl, bl, gl;

    if (!mkdtemp(root)) { CHECK(0); return; }
    snprintf(dd, sizeof dd, "%s/data", root);
    mkdir(dd, 0777);
    k = mk_key("data\\one.bif", NAMES, TYPES, 2, &kl);
    b = mk_bif(DATA, TYPES, 2, &bl);
    wfile(dd, "a.key", k, kl);
    wfile(dd, "one.bif", b, bl);
    CHECK(resman_open(&rm, root, err) == 0 && rm.nk == 1);
    CHECK(resman_get(&rm, "tdc01", 2013, &got, &gl, err) == 0 && gl == strlen(DATA[0]) && !memcmp(got, DATA[0], gl) && got[gl] == 0);
    free(got);
    CHECK(resman_get(&rm, "other", 2017, &got, &gl, err) == 0 && gl == strlen(DATA[1]));
    free(got);
    CHECK(resman_get(&rm, "tdc01", 2017, &got, &gl, err) != 0);   /* name exists, wrong type */
    CHECK(resman_get(&rm, "missing", 2013, &got, &gl, err) != 0 && strstr(err, "not found"));
    CHECK(resman_get(&rm, "this_name_is_way_too_long", 2013, &got, &gl, err) != 0);
    resman_close(&rm);

    /* a second key holding the same name with different bytes is ambiguous: refuse, don't guess */
    {
        static const char *const DATA2[2] = {"[TILES]\nCount=2\n", "2DA V2.0\n"};
        uint8_t *b2;
        size_t b2l;
        b2 = mk_bif(DATA2, TYPES, 2, &b2l);
        wfile(dd, "b.key", k, kl);               /* b.key points at the same "data\one.bif" ... */
        CHECK(resman_open(&rm, root, err) == 0 && rm.nk == 2);
        CHECK(resman_get(&rm, "tdc01", 2013, &got, &gl, err) == 0);   /* same bytes via both keys: fine */
        free(got);
        resman_close(&rm);
        {
            uint8_t *k2;
            size_t k2l;
            k2 = mk_key("data\\two.bif", NAMES, TYPES, 2, &k2l);
            wfile(dd, "b.key", k2, k2l);
            wfile(dd, "two.bif", b2, b2l);
            CHECK(resman_open(&rm, root, err) == 0);
            CHECK(resman_get(&rm, "tdc01", 2013, &got, &gl, err) != 0 && strstr(err, "different contents"));
            resman_close(&rm);
            free(k2);
        }
        free(b2);
    }
    /* corrupt key files are rejected */
    wfile(dd, "b.key", k, 20);
    CHECK(resman_open(&rm, root, err) != 0);
    {
        char cmd[700];
        snprintf(cmd, sizeof cmd, "rm -rf '%s'", root);
        CHECK(system(cmd) == 0);
    }
    free(k);
    free(b);
}

int main(void)
{
    test_sha();
    test_prng();
    test_config();
    test_erf_parse();
    test_erf_dup_and_loc();
    test_erf_roundtrip();
    test_nwn2_and_gate();
    test_catalog();
    test_gff();
    test_gff_tree();
    test_recipe();
    test_tilepack();
    test_layout();
    test_kit_pack();
    test_doorway_pack();
    test_kit_layout();
    test_kit_set();
    test_dungeon();
    test_roster();
    test_roster_deco();
    test_roster_loot();
    test_loot_plan();
    test_hench();
    test_loot_amount();
    test_spawn();
    test_deco();
    test_roster_roomprop();
    test_dressing();
    test_tileset();
    test_resman();
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
