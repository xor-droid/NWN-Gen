/* nwndgen - deterministic NWN1:EE .mod dungeon generator (CLI). */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "catalog.h"
#include "config.h"
#include "dungeon.h"
#include "erf.h"
#include "game.h"
#include "gff.h"
#include "plan.h"
#include "recipe.h"
#include "roster.h"
#include "spawn.h"
#include "summary.h"
#include "sha256.h"
#include "util.h"

#define EXIT_FAIL 1
#define EXIT_USAGE 2

static const char USAGE[] =
    "usage: nwndgen --game nwn1|nwn2 --base BASE.mod\n"
    "               (--templates CATALOG.tsv | --recipe RECIPE.txt) [--seed N]\n"
    "               --output OUT.mod [--party-size N] [--min-level N]\n"
    "               [--max-level N] [--manifest OUT.txt] [--force]\n"
    "       nwndgen --help | --version\n"
    "\n"
    "  --game GAME        target game gate (required): nwn1 = NWN:EE (ERF V1.0),\n"
    "                     nwn2 = NWN2:EE (ERF V1.1). Inputs of the other game are\n"
    "                     rejected.\n"
    "  --base PATH        base module of the selected game, holding the area slots\n"
    "  --templates PATH   variant catalog (7 tab-separated columns, no header)\n"
    "  --recipe PATH      procedural dungeon recipe (nwn1 only): areas are generated\n"
    "                     fresh from the seed; replaces --templates\n"
    "  --seed N           unsigned 64-bit decimal seed (required unless the recipe\n"
    "                     has a seed line)\n"
    "  --party-size N     adventuring player characters, 1-32 (default 1; a recipe\n"
    "                     may set party_size, min_level, max_level and seed as\n"
    "                     defaults, and these options override them)\n"
    "  --min-level N      lowest supported character level, 1-40 (default 1)\n"
    "  --max-level N      highest supported character level, 1-40 (default 2)\n"
    "  --output PATH      module to write (required); existing files are kept\n"
    "                     unless --force is given\n"
    "  --manifest PATH    also write a reproducibility manifest\n"
    "  --force            replace existing output files\n";

enum { O_GAME, O_BASE, O_TEMPLATES, O_RECIPE, O_SEED, O_PARTY, O_MINL, O_MAXL, O_OUTPUT,
       O_MANIFEST, O_FORCE, O_COUNT };

static const struct { const char *name; int takes_value; } OPTS[O_COUNT] = {
    {"--game", 1}, {"--base", 1},     {"--templates", 1}, {"--recipe", 1}, {"--seed", 1},
    {"--party-size", 1}, {"--min-level", 1}, {"--max-level", 1},
    {"--output", 1},   {"--manifest", 1},  {"--force", 0}};

static int usage_error(const char *what, const char *arg)
{
    /* what is a short phrase; arg (if any) is quoted after it. */
    if (arg)
        fprintf(stderr, "nwndgen: %s '%s'\nTry 'nwndgen --help'.\n", what, arg);
    else
        fprintf(stderr, "nwndgen: %s\nTry 'nwndgen --help'.\n", what);
    return EXIT_USAGE;
}

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

/* print a path single-quoted when the shell would split or expand it */
static void print_arg(const char *s)
{
    const char *p;
    int plain = *s != '\0';
    for (p = s; *p; p++)
        plain &= (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || strchr("._-/+=,:@", *p) != NULL;
    if (plain) {
        fputs(s, stdout);
        return;
    }
    putchar('\'');
    for (p = s; *p; p++) {
        if (*p == '\'')
            fputs("'\\''", stdout);
        else
            putchar(*p);
    }
    putchar('\'');
}

static void append(char **buf, size_t *len, size_t *cap, const char *fmt, ...)
    PRINTF_LIKE(4, 5);

static void append(char **buf, size_t *len, size_t *cap, const char *fmt, ...)
{
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    if (*len + (size_t)n + 1 > *cap) {
        *cap = (*len + (size_t)n + 1) * 2;
        *buf = xrealloc(*buf, *cap);
    }
    va_start(ap, fmt);
    vsnprintf(*buf + *len, *cap - *len, fmt, ap);
    va_end(ap);
    *len += (size_t)n;
}


/* ---- recipe (procedural dungeon) mode ---- */

/* Scripts the objective needs: area OnEnter spawns from the markers, module OnAcquireItem drives the journal. */
static const struct { const char *name; uint16_t type; } SCRIPT_FILES[] = {
    {"scel_enter", RES_NSS}, {"scel_enter", RES_NCS}, {"scel_acquire", RES_NSS}, {"scel_acquire", RES_NCS},
    {"scel_use", RES_NSS}, {"scel_use", RES_NCS}, {"scel_beat", RES_NSS}, {"scel_beat", RES_NCS},
    {"scel_rest", RES_NSS}, {"scel_rest", RES_NCS}}; /* scel_use and scel_beat only with recipe keys ambush, levers or patrols, scel_rest only with encounters */
#define N_SCRIPT_FILES 10
#define N_BASE_SCRIPTS 4
#define N_USE_SCRIPTS 8
#define SCRIPTS_FOR(r) ((r)->encounters ? N_SCRIPT_FILES : (r)->ambush || (r)->levers || (r)->patrols || (r)->reactions ? N_USE_SCRIPTS : N_BASE_SCRIPTS)

typedef struct {
    recipe r;
    tilepack tp;
    gff tare, tgit, tdoor, ifo;
    dungeon d;
    uint8_t *rbuf, *pbuf, *abuf, *gbuf, *dbuf;
    uint8_t rsha[32], psha[32], asha[32], gsha[32], ysha[32], dsha[32];
    roster ro;
    uint8_t *ybuf, *sbuf[N_SCRIPT_FILES];
    size_t slen[N_SCRIPT_FILES];
    uint8_t ssha[N_SCRIPT_FILES][32];
    int have_tp, have_are, have_git, have_door, have_ifo, have_d, have_ro;
} rctx;

static void rctx_free(rctx *c)
{
    if (c->have_d)
        dungeon_free(&c->d);
    if (c->have_ifo)
        gff_free(&c->ifo);
    if (c->have_git)
        gff_free(&c->tgit);
    if (c->have_door)
        gff_free(&c->tdoor);
    if (c->have_are)
        gff_free(&c->tare);
    if (c->have_tp)
        tilepack_free(&c->tp);
    free(c->ybuf);
    for (int k = 0; k < N_SCRIPT_FILES; k++)
        free(c->sbuf[k]);
    free(c->rbuf);
    free(c->pbuf);
    free(c->abuf);
    free(c->gbuf);
    free(c->dbuf);
}

static int read_gff_file(const char *path, const char *type4, uint8_t **buf, uint8_t sha[32], gff *g, char *err)
{
    size_t len;
    char m[ERRLEN + 600];
    if (read_file(path, buf, &len, err) != 0)
        return -1;
    sha256(*buf, len, sha);
    if (gff_check(*buf, len, type4, err) != 0 || gff_parse(*buf, len, g, err) != 0) {
        snprintf(m, sizeof m, "'%s': %s", path, err);
        set_err(err, "%s", m);
        return -1;
    }
    return 0;
}

static int recipe_run(rctx *c, const char *recipe_path, const erf *base, const config *cfg, erf *out, char *err)
{
    size_t rlen, plen, i;
    char *dir = dir_of(recipe_path), m[ERRLEN + RECIPE_PATH_MAX + 64];
    const erf_res *ifo_res;
    int rc = -1;

    if (read_file(recipe_path, &c->rbuf, &rlen, err) != 0)
        goto done;
    sha256(c->rbuf, rlen, c->rsha);
    if (recipe_parse((const char *)c->rbuf, rlen, dir, &c->r, err) != 0) {
        snprintf(m, sizeof m, "recipe '%s': %s", recipe_path, err);
        set_err(err, "%s", m);
        goto done;
    }
    if (cfg->min_level < c->r.min_level || cfg->max_level > c->r.max_level || cfg->party_size < c->r.min_party ||
        cfg->party_size > c->r.max_party) {
        set_err(err, "recipe supports levels %u-%u and party sizes %u-%u; requested levels %u-%u, party %u",
                c->r.min_level, c->r.max_level, c->r.min_party, c->r.max_party, (unsigned)cfg->min_level,
                (unsigned)cfg->max_level, (unsigned)cfg->party_size);
        goto done;
    }
    if (read_file(c->r.tilepack, &c->pbuf, &plen, err) != 0)
        goto done;
    sha256(c->pbuf, plen, c->psha);
    if (tilepack_parse((const char *)c->pbuf, plen, &c->tp, err) != 0) {
        snprintf(m, sizeof m, "tilepack '%s': %s", c->r.tilepack, err);
        set_err(err, "%s", m);
        goto done;
    }
    c->have_tp = 1;
    if (read_gff_file(c->r.template_are, "ARE ", &c->abuf, c->asha, &c->tare, err) != 0)
        goto done;
    c->have_are = 1;
    if (read_gff_file(c->r.template_git, "GIT ", &c->gbuf, c->gsha, &c->tgit, err) != 0)
        goto done;
    c->have_git = 1;
    if (c->r.template_door[0]) {
        /* the stock door goes into the in-memory GIT template, where the builder clones it; the template file itself is untouched */
        gff_field *dl = gff_find(c->tgit.root, "Door List"), *src;
        if (read_gff_file(c->r.template_door, "GIT ", &c->dbuf, c->dsha, &c->tdoor, err) != 0)
            goto done;
        c->have_door = 1;
        src = gff_find(c->tdoor.root, "Door List");
        if (!dl) dl = gff_set_list(c->tgit.root, "Door List", err);
        if (!dl || !src || src->type != GFF_LIST || !src->nlist) {
            set_err(err, "template_door '%s' has no Door List entry", c->r.template_door);
            goto done;
        }
        gff_list_append(dl, gff_struct_clone(src->list[0]));
        if (c->r.doors == 3) { /* doors doorway: the generic-look donors follow the corridor door */
            size_t di;
            if (src->nlist < 2) {
                set_err(err, "template_door '%s' has no generic-door donors (run nwnpack templates again)", c->r.template_door);
                goto done;
            }
            for (di = 1; di < src->nlist; di++)
                gff_list_append(dl, gff_struct_clone(src->list[di]));
        }
    }
    ifo_res = erf_find(base, "module", RES_IFO);
    if (!ifo_res || gff_parse(ifo_res->data, ifo_res->size, &c->ifo, err) != 0) {
        if (ifo_res) {
            snprintf(m, sizeof m, "base module.ifo: %s", err);
            set_err(err, "%s", m);
        }
        goto done;
    }
    c->have_ifo = 1;
    for (i = 0; i < c->r.nareas; i++) {
        static const uint16_t parts[3] = {RES_ARE, RES_GIT, RES_GIC};
        int k;
        for (k = 0; k < 3; k++)
            if (erf_find(base, c->r.areas[i].resref, parts[k])) {
                set_err(err, "base module already contains '%s.%s'", c->r.areas[i].resref, res_ext(parts[k]));
                goto done;
            }
    }
    if (c->r.roster[0]) {
        size_t yl;
        for (i = 0; i < SCRIPTS_FOR(&c->r); i++) {
            char path[RECIPE_PATH_MAX + 64];
            if (erf_find(base, SCRIPT_FILES[i].name, SCRIPT_FILES[i].type)) {
                set_err(err, "base module already contains '%s.%s'", SCRIPT_FILES[i].name, res_ext(SCRIPT_FILES[i].type));
                goto done;
            }
            snprintf(path, sizeof path, "%s/%s.%s", c->r.scripts, SCRIPT_FILES[i].name, res_ext(SCRIPT_FILES[i].type));
            if (read_file(path, &c->sbuf[i], &c->slen[i], err) != 0)
                goto done;
            sha256(c->sbuf[i], c->slen[i], c->ssha[i]);
            if (SCRIPT_FILES[i].type == RES_NCS && (c->slen[i] < 13 || memcmp(c->sbuf[i], "NCS V1.0B", 9))) {
                snprintf(m, sizeof m, "script '%s' is not a compiled NCS V1.0B file", path);
                set_err(err, "%s", m);
                goto done;
            }
        }
        if (erf_find(base, "module", RES_JRL)) {
            set_err(err, "base module already contains 'module.jrl'");
            goto done;
        }
        if (read_file(c->r.roster, &c->ybuf, &yl, err) != 0)
            goto done;
        sha256(c->ybuf, yl, c->ysha);
        if (roster_parse((const char *)c->ybuf, yl, &c->ro, err) != 0) {
            snprintf(m, sizeof m, "roster '%s': %s", c->r.roster, err);
            set_err(err, "%s", m);
            goto done;
        }
        c->have_ro = 1;
    }
    {
        spawn_cfg sc;
        sc.ro = &c->ro;
        sc.party = cfg->party_size;
        sc.min_level = cfg->min_level;
        sc.max_level = cfg->max_level;
        sc.boss_only = c->r.boss_only;
        sc.loot_test = c->r.loot_test;
        sc.henchmen = c->r.henchmen;
        sc.loot_amount = c->r.loot_amount;
        sc.dressing = c->r.dressing;
        sc.corridor_dressing = c->r.corridor_dressing;
        sc.locks = c->r.locks;
        sc.ambush = c->r.ambush;
        sc.traps = c->r.traps;
        sc.levers = c->r.levers;
        sc.patrols = c->r.patrols;
        sc.encounters = c->r.encounters;
        sc.reactions = c->r.reactions;
        if (dungeon_build_ex(&c->r, &c->tp, &c->tare, &c->tgit, &c->ifo, cfg->seed, c->have_ro ? &sc : NULL, &c->d,
                             err) != 0)
            goto done;
    }
    c->have_d = 1;

    erf_init(out, base->sig, base->game);
    out->desc_strref = base->desc_strref;
    out->loc_count = base->loc_count;
    out->loc = base->loc;
    out->loc_size = base->loc_size;
    for (i = 0; i < base->nres; i++)
        if (!(base->res[i].type == RES_IFO && !strcmp(base->res[i].name, "module")) &&
            erf_add(out, base->res[i].name, base->res[i].type, base->res[i].data, base->res[i].size, err) != 0)
            goto done_out;
    if (erf_add(out, "module", RES_IFO, c->d.ifo, (uint32_t)c->d.nifo, err) != 0)
        goto done_out;
    for (i = 0; i < c->d.n; i++) {
        const dungeon_area *a = &c->d.area[i];
        if (erf_add(out, a->resref, RES_ARE, a->are, (uint32_t)a->nare, err) != 0 ||
            erf_add(out, a->resref, RES_GIT, a->git, (uint32_t)a->ngit, err) != 0 ||
            erf_add(out, a->resref, RES_GIC, a->gic, (uint32_t)a->ngic, err) != 0)
            goto done_out;
    }
    if (c->d.has_spawn) {
        if (erf_add(out, "module", RES_JRL, c->d.jrl, (uint32_t)c->d.njrl, err) != 0)
            goto done_out;
        for (i = 0; i < SCRIPTS_FOR(&c->r); i++)
            if (erf_add(out, SCRIPT_FILES[i].name, SCRIPT_FILES[i].type, c->sbuf[i], (uint32_t)c->slen[i], err) != 0)
                goto done_out;
    }
    rc = 0;
    goto done;
done_out:
    free(out->res);
    out->res = NULL;
done:
    free(dir);
    return rc;
}

int main(int argc, char **argv)
{
    const char *val[O_COUNT] = {0};
    int seen[O_COUNT] = {0};
    config cfg;
    int have_rseed;
    const game_profile *game;
    char err[ERRLEN] = "";
    int i, o, rc = EXIT_FAIL;

    uint8_t *bbuf = NULL, *tbuf = NULL, *obuf = NULL;
    size_t blen = 0, tlen = 0, olen = 0;
    erf base, out;
    catalog cat;
    plan pl;
    char *tmp_mod = NULL, *tmp_man = NULL, *cat_dir = NULL;
    char *manifest = NULL;
    size_t mlen = 0, mcap = 0;
    int have_base = 0, have_out = 0, have_cat = 0, have_plan = 0;
    rctx rc_ctx;

    memset(&cat, 0, sizeof cat);
    memset(&pl, 0, sizeof pl);
    memset(&rc_ctx, 0, sizeof rc_ctx);
    config_defaults(&cfg);

    for (i = 1; i < argc; i++) {
        const char *a = argv[i], *eq;
        size_t nl;
        if (!strcmp(a, "--help")) {
            fputs(USAGE, stdout);
            return 0;
        }
        if (!strcmp(a, "--version")) {
            printf("nwndgen %s (algorithm %s)\n", GENERATOR_VERSION,
                   ALGORITHM_VERSION);
            return 0;
        }
        eq = strchr(a, '=');
        nl = eq ? (size_t)(eq - a) : strlen(a);
        for (o = 0; o < O_COUNT; o++)
            if (strlen(OPTS[o].name) == nl && !strncmp(a, OPTS[o].name, nl))
                break;
        if (o == O_COUNT)
            return usage_error("unknown option or argument", a);
        if (seen[o])
            return usage_error("option given more than once:", OPTS[o].name);
        seen[o] = 1;
        if (!OPTS[o].takes_value) {
            if (eq)
                return usage_error("option takes no value:", OPTS[o].name);
            continue;
        }
        if (eq)
            val[o] = eq + 1;
        else if (i + 1 < argc)
            val[o] = argv[++i];
        else
            return usage_error("option needs a value:", OPTS[o].name);
        if (!*val[o])
            return usage_error("option has an empty value:", OPTS[o].name);
    }
    if (!val[O_GAME])
        return usage_error("missing required option", "--game");
    game = game_by_name(val[O_GAME]);
    if (!game)
        return usage_error("unknown game (use nwn1 or nwn2):", val[O_GAME]);
    if (!val[O_BASE])
        return usage_error("missing required option", "--base");
    if (!val[O_TEMPLATES] && !val[O_RECIPE])
        return usage_error("need one of --templates or --recipe", NULL);
    if (val[O_TEMPLATES] && val[O_RECIPE])
        return usage_error("--templates and --recipe are mutually exclusive", NULL);
    if (val[O_RECIPE] && game != &GAME_NWN1)
        return usage_error("--recipe supports --game nwn1 only (the tile pack is an NWN1 tileset)", NULL);
    have_rseed = 0;
    if (val[O_RECIPE]) { /* run defaults written in the recipe; the options below override them */
        recipe pre;
        uint8_t *pb = NULL;
        size_t plen0 = 0;
        char *pd = dir_of(val[O_RECIPE]), m[ERRLEN + 600];
        int prc = read_file(val[O_RECIPE], &pb, &plen0, err);
        if (prc == 0 && recipe_parse((const char *)pb, plen0, pd, &pre, err) != 0) {
            snprintf(m, sizeof m, "recipe '%s': %s", val[O_RECIPE], err);
            set_err(err, "%s", m);
            prc = -1;
        }
        free(pb);
        free(pd);
        if (prc != 0) { /* same failure as recipe_run would report */
            fprintf(stderr, "nwndgen: %s\n", err);
            return EXIT_FAIL;
        }
        if (pre.has_seed) { cfg.seed = pre.run_seed; have_rseed = 1; }
        if (pre.has_party) cfg.party_size = pre.run_party;
        if (pre.has_min_lvl) cfg.min_level = pre.run_min_lvl;
        if (pre.has_max_lvl) cfg.max_level = pre.run_max_lvl;
    }
    if (!val[O_SEED] && !have_rseed)
        return usage_error("missing required option --seed (or a seed line in the recipe)", NULL);
    if (!val[O_OUTPUT])
        return usage_error("missing required option", "--output");

    if (val[O_SEED] && parse_u64(val[O_SEED], &cfg.seed) != 0)
        return usage_error("invalid seed (need unsigned 64-bit decimal):", val[O_SEED]);
    if (val[O_PARTY] &&
        parse_uint_in(val[O_PARTY], 1, MAX_PARTY_LIMIT, &cfg.party_size) != 0)
        return usage_error("invalid party size (1-32):", val[O_PARTY]);
    if (val[O_MINL] &&
        parse_uint_in(val[O_MINL], MIN_LEVEL_LIMIT, MAX_LEVEL_LIMIT,
                      &cfg.min_level) != 0)
        return usage_error("invalid minimum level (1-40):", val[O_MINL]);
    if (val[O_MAXL] &&
        parse_uint_in(val[O_MAXL], MIN_LEVEL_LIMIT, MAX_LEVEL_LIMIT,
                      &cfg.max_level) != 0)
        return usage_error("invalid maximum level (1-40):", val[O_MAXL]);
    if (config_validate(&cfg, err) != 0)
        return usage_error(err, NULL);
    if (val[O_MANIFEST] && !strcmp(val[O_MANIFEST], val[O_OUTPUT]))
        return usage_error("--manifest and --output name the same path", NULL);

    /* Load and validate everything before touching the filesystem output. */
    if (read_file(val[O_BASE], &bbuf, &blen, err) != 0)
        goto done;
    if (erf_parse(bbuf, blen, &base, game, err) != 0) {
        char m[ERRLEN + 600];
        snprintf(m, sizeof m, "base '%s': %s", val[O_BASE], err);
        set_err(err, "%s", m); /* truncates only absurdly long paths */
        goto done;
    }
    have_base = 1;
    if (base_validate(&base, err) != 0)
        goto done;
    if (val[O_RECIPE]) {
        if (recipe_run(&rc_ctx, val[O_RECIPE], &base, &cfg, &out, err) != 0)
            goto done;
        have_out = 1;
    } else {
        if (read_file(val[O_TEMPLATES], &tbuf, &tlen, err) != 0)
            goto done;
        if (catalog_parse((const char *)tbuf, tlen, &cat, err) != 0)
            goto done;
        have_cat = 1;
        cat_dir = dir_of(val[O_TEMPLATES]);
        if (plan_load_variants(&cat, cat_dir, &base, err) != 0)
            goto done;
        if (plan_select(&cat, &cfg, &pl, err) != 0)
            goto done;
        have_plan = 1;
        if (plan_apply(&base, &cat, &pl, &out, err) != 0)
            goto done;
        have_out = 1;
    }
    if (erf_write(&out, &obuf, &olen, err) != 0)
        goto done;

    {
        uint8_t bsha[32], csha[32], osha[32];
        char bh[65], ch[65], oh[65], vh[65];
        size_t s;

        sha256(bbuf, blen, bsha);
        if (have_cat)
            catalog_canonical_sha(&cat, csha);
        sha256(obuf, olen, osha);
        hex_encode(bsha, 32, bh);
        hex_encode(csha, 32, ch);
        hex_encode(osha, 32, oh);

        append(&manifest, &mlen, &mcap, "nwndgen-manifest 1\n");
        append(&manifest, &mlen, &mcap, "generator_version=%s\n", GENERATOR_VERSION);
        append(&manifest, &mlen, &mcap, "algorithm=%s\n", ALGORITHM_VERSION);
        append(&manifest, &mlen, &mcap, "game=%s\n", game->name);
        append(&manifest, &mlen, &mcap, "seed=%llu\n", (unsigned long long)cfg.seed);
        append(&manifest, &mlen, &mcap, "party_size=%u\n", (unsigned)cfg.party_size);
        append(&manifest, &mlen, &mcap, "min_level=%u\n", (unsigned)cfg.min_level);
        append(&manifest, &mlen, &mcap, "max_level=%u\n", (unsigned)cfg.max_level);
        append(&manifest, &mlen, &mcap, "base_sha256=%s\n", bh);
        if (val[O_RECIPE]) {
            const rctx *c = &rc_ctx;
            hex_encode(c->rsha, 32, vh);
            append(&manifest, &mlen, &mcap, "dungeon_algorithm=%s\n", c->r.kit_corridors ? DUNGEON_ALGORITHM_VERSION_KIT : DUNGEON_ALGORITHM_VERSION);
            append(&manifest, &mlen, &mcap, "recipe_sha256=%s\n", vh);
            hex_encode(c->psha, 32, vh);
            append(&manifest, &mlen, &mcap, "tilepack_sha256=%s\n", vh);
            hex_encode(c->asha, 32, vh);
            append(&manifest, &mlen, &mcap, "template_are_sha256=%s\n", vh);
            hex_encode(c->gsha, 32, vh);
            append(&manifest, &mlen, &mcap, "template_git_sha256=%s\n", vh);
            if (c->r.template_door[0]) {
                hex_encode(c->dsha, 32, vh);
                append(&manifest, &mlen, &mcap, "template_door_sha256=%s\n", vh);
            }
            if (c->r.kit_corridors) {
                append(&manifest, &mlen, &mcap, "corridors=kit\ndoors=%s\n", c->r.doors == 3 ? "doorway" : c->r.doors == 2 ? "mixed" : c->r.doors ? "plain" : "none");
                for (s = 0; s < c->d.n; s++)
                    append(&manifest, &mlen, &mcap, "doors.%s=%zu\n", c->d.area[s].resref, c->d.area[s].ndoor);
                if (c->r.doors == 2) {
                    int ln[256] = {0}, first = 1, lk;
                    size_t dm;
                    for (s = 0; s < c->d.n; s++)
                        for (dm = 0; dm < c->d.area[s].ndoor; dm++)
                            ln[c->d.area[s].door[dm].app & 255]++;
                    append(&manifest, &mlen, &mcap, "door_looks=");
                    for (lk = 0; lk < 256; lk++)
                        if (ln[lk]) {
                            append(&manifest, &mlen, &mcap, "%s%d:%d", first ? "" : ",", lk, ln[lk]);
                            first = 0;
                        }
                    append(&manifest, &mlen, &mcap, "\n");
                }
                if (c->r.doors == 3) { /* doors doorway: generic looks (genericdoors.2da rows), counted per look */
                    int ln[64] = {0}, first = 1, lk;
                    size_t dm;
                    for (s = 0; s < c->d.n; s++)
                        for (dm = 0; dm < c->d.area[s].ndoor; dm++)
                            ln[c->d.area[s].door[dm].gen & 63]++;
                    append(&manifest, &mlen, &mcap, "door_looks=");
                    for (lk = 0; lk < 64; lk++)
                        if (ln[lk]) {
                            append(&manifest, &mlen, &mcap, "%sgeneric%d:%d", first ? "" : ",", lk, ln[lk]);
                            first = 0;
                        }
                    append(&manifest, &mlen, &mcap, "\n");
                }
            }
            for (s = 0; s < c->d.n; s++)
                append(&manifest, &mlen, &mcap, "area.%s=%dx%d rooms=%d layout_attempts=%d\n", c->d.area[s].resref,
                       c->d.area[s].lay.w, c->d.area[s].lay.h, c->d.area[s].lay.nrooms, c->d.area[s].lay.attempts);
            if (c->d.has_spawn) {
                size_t k;
                append(&manifest, &mlen, &mcap, "spawn_algorithm=%s\n", SPAWN_ALGORITHM_VERSION);
                hex_encode(c->ysha, 32, vh);
                append(&manifest, &mlen, &mcap, "roster_sha256=%s\n", vh);
                append(&manifest, &mlen, &mcap, "theme=%s\n", c->ro.theme[c->d.theme].id);
                append(&manifest, &mlen, &mcap, "spawn_mode=%s\n", c->r.boss_only ? "boss_only" : c->r.loot_test ? "loot_test" : "full");
                if (c->d.nhench) {
                    int hq;
                    append(&manifest, &mlen, &mcap, "hench_algorithm=%s\nhenchmen=%d\n", HENCH_ALGORITHM_VERSION, c->d.nhench);
                    for (hq = 0; hq < c->d.nhench; hq++)
                        append(&manifest, &mlen, &mcap, "henchman.%d=%s\n", hq + 1, c->d.hench[hq]);
                }
                for (k = 0; k < SCRIPTS_FOR(&c->r); k++) {
                    hex_encode(c->ssha[k], 32, vh);
                    append(&manifest, &mlen, &mcap, "script.%s.%s_sha256=%s\n", SCRIPT_FILES[k].name,
                           res_ext(SCRIPT_FILES[k].type), vh);
                }
                for (s = 0; s < c->d.n; s++)
                    append(&manifest, &mlen, &mcap, "spawns.%s=%zu\n", c->d.area[s].resref, c->d.area[s].nsp);
                append(&manifest, &mlen, &mcap, "ambience=%s\n", c->ro.theme[c->d.theme].has_ambience ? "theme" : "none");
                for (s = 0; s < c->d.n; s++) {
                    int nd = 0, nl = 0, nx = 0;
                    for (k = 0; k < c->d.area[s].nsp; k++) {
                        char kd = c->d.area[s].sp[k].kind;
                        nd += kd == 'D';
                        nl += kd == 'L';
                        nx += kd == 'X';
                    }
                    append(&manifest, &mlen, &mcap, "scenery.%s=decor %d light %d container %d\n", c->d.area[s].resref, nd, nl, nx);
                }
                if (c->r.dressing) {
                    append(&manifest, &mlen, &mcap, "dressing=%s\n", c->r.dressing == 1 ? "low" : c->r.dressing == 2 ? "mid" : "high");
                    append(&manifest, &mlen, &mcap, "dressing_algorithm=%s\n", DRESSING_ALGORITHM_VERSION);
                    for (s = 0; s < c->d.n; s++) {
                        int rr;
                        append(&manifest, &mlen, &mcap, "rooms.%s=", c->d.area[s].resref);
                        for (rr = 0; rr < c->d.area[s].lay.nrooms; rr++)
                            append(&manifest, &mlen, &mcap, "%s%s", rr ? "," : "", c->d.area[s].rtype[rr] ? RT_NAME[c->d.area[s].rtype[rr] - 1] : "plain");
                        append(&manifest, &mlen, &mcap, "\n");
                    }
                }
                if (c->r.corridor_dressing) {
                    append(&manifest, &mlen, &mcap, "corridor_dressing=%s\n", c->r.corridor_dressing == 1 ? "low" : c->r.corridor_dressing == 2 ? "mid" : "high");
                    append(&manifest, &mlen, &mcap, "corridor_algorithm=%s\n", CORRIDOR_ALGORITHM_VERSION);
                    for (s = 0; s < c->d.n; s++) {
                        append(&manifest, &mlen, &mcap, "corridor_props.%s=%d\n", c->d.area[s].resref, spawn_corridor_props(&c->d.area[s]));
                    }
                }
                if (c->r.locks) {
                    append(&manifest, &mlen, &mcap, "locks=%s\n", c->r.locks == 1 ? "low" : c->r.locks == 2 ? "mid" : "high");
                    append(&manifest, &mlen, &mcap, "locks_algorithm=%s\n", LOCKS_ALGORITHM_VERSION);
                    for (s = 0; s < c->d.n; s++) {
                        int nd, nk, nc;
                        spawn_lock_counts(&c->d.area[s], &nd, &nk, &nc);
                        append(&manifest, &mlen, &mcap, "locks.%s=doors %d, keyed %d, chests %d\n", c->d.area[s].resref, nd, nk, nc);
                    }
                }
                if (c->r.traps) {
                    append(&manifest, &mlen, &mcap, "traps=%s\n", c->r.traps == 1 ? "low" : c->r.traps == 2 ? "mid" : "high");
                    append(&manifest, &mlen, &mcap, "traps_algorithm=%s\n", TRAPS_ALGORITHM_VERSION);
                    for (s = 0; s < c->d.n; s++) {
                        int nf, nc;
                        spawn_trap_counts(&c->d.area[s], &nf, &nc);
                        append(&manifest, &mlen, &mcap, "traps.%s=floor %d, chests %d\n", c->d.area[s].resref, nf, nc);
                    }
                }
                if (c->r.patrols) {
                    append(&manifest, &mlen, &mcap, "patrols=%s\n", c->r.patrols == 1 ? "low" : c->r.patrols == 2 ? "mid" : "high");
                    append(&manifest, &mlen, &mcap, "patrols_algorithm=%s\n", PATROLS_ALGORITHM_VERSION);
                    for (s = 0; s < c->d.n; s++) {
                        int np;
                        spawn_patrol_counts(&c->d.area[s], &np);
                        append(&manifest, &mlen, &mcap, "patrols.%s=patrols %d\n", c->d.area[s].resref, np);
                    }
                }
                if (c->r.reactions) {
                    append(&manifest, &mlen, &mcap, "reactions=on\n");
                    append(&manifest, &mlen, &mcap, "reactions_algorithm=%s\n", REACTIONS_ALGORITHM_VERSION);
                    append(&manifest, &mlen, &mcap, "reactions_config=pool %d, group %d\n", c->d.rea_n, c->d.rea_group);
                }
                if (c->r.encounters) {
                    append(&manifest, &mlen, &mcap, "encounters=%s\n", c->r.encounters == 1 ? "low" : c->r.encounters == 2 ? "mid" : "high");
                    append(&manifest, &mlen, &mcap, "encounters_algorithm=%s\n", ENCOUNTERS_ALGORITHM_VERSION);
                    append(&manifest, &mlen, &mcap, "encounters_config=pool %d, group %d, cap %d, rest %d, quiet %d\n", c->d.enc_n, c->d.enc_group, c->d.enc_cap, c->d.enc_pct, c->d.enc_tick);
                }
                if (c->r.levers) {
                    append(&manifest, &mlen, &mcap, "levers=on\n");
                    append(&manifest, &mlen, &mcap, "levers_algorithm=%s\n", LEVERS_ALGORITHM_VERSION);
                    for (s = 0; s < c->d.n; s++) {
                        int ng;
                        spawn_lever_counts(&c->d.area[s], &ng);
                        append(&manifest, &mlen, &mcap, "levers.%s=gates %d\n", c->d.area[s].resref, ng);
                    }
                }
                if (c->r.ambush) {
                    append(&manifest, &mlen, &mcap, "ambush=%s\n", c->r.ambush == 1 ? "low" : c->r.ambush == 2 ? "mid" : "high");
                    append(&manifest, &mlen, &mcap, "ambush_algorithm=%s\n", AMBUSH_ALGORITHM_VERSION);
                    for (s = 0; s < c->d.n; s++) {
                        int ng, nh, nc;
                        spawn_ambush_counts(&c->d.area[s], &ng, &nh, &nc);
                        append(&manifest, &mlen, &mcap, "ambush.%s=groups %d, hidden %d, chests %d\n", c->d.area[s].resref, ng, nh, nc);
                    }
                }
                if (c->ro.theme[c->d.theme].nloot || c->ro.theme[c->d.theme].gold_per_cr) {
                    append(&manifest, &mlen, &mcap, "loot_algorithm=%s\n", LOOT_ALGORITHM_VERSION);
                    append(&manifest, &mlen, &mcap, "loot_amount=%s\n", c->r.loot_amount == 1 ? "low" : c->r.loot_amount == 2 ? "high" : "mid");
                    for (s = 0; s < c->d.n; s++) {
                        long ni = 0, gp = 0;
                        for (k = 0; k < c->d.area[s].nsp; k++) {
                            ni += c->d.area[s].sp[k].loot[0][0] != 0;
                            ni += c->d.area[s].sp[k].loot[1][0] != 0;
                            gp += c->d.area[s].sp[k].gold;
                        }
                        append(&manifest, &mlen, &mcap, "loot.%s=items %ld gold %ld\n", c->d.area[s].resref, ni, gp);
                    }
                }
            }
        } else {
            append(&manifest, &mlen, &mcap, "catalog_sha256=%s\n", ch);
        }
        for (s = 0; !val[O_RECIPE] && s < pl.nslots; s++) {
            const cat_row *r = &cat.rows[pl.chosen[s]];
            hex_encode(r->sha, 32, vh);
            append(&manifest, &mlen, &mcap, "slot.%s=%s sha256=%s\n", r->slot,
                   r->variant, vh);
        }
        append(&manifest, &mlen, &mcap, "output_sha256=%s\n", oh);
        append(&manifest, &mlen, &mcap, "output_bytes=%zu\n", olen);
    }

    if (write_temp(val[O_OUTPUT], obuf, olen, &tmp_mod, err) != 0)
        goto done;
    if (val[O_MANIFEST] &&
        write_temp(val[O_MANIFEST], (const uint8_t *)manifest, mlen, &tmp_man,
                   err) != 0)
        goto done;
    if (publish_temp(tmp_mod, val[O_OUTPUT], seen[O_FORCE], err) != 0) {
        free(tmp_mod);
        tmp_mod = NULL;
        goto done;
    }
    free(tmp_mod);
    tmp_mod = NULL;
    if (tmp_man) {
        if (publish_temp(tmp_man, val[O_MANIFEST], seen[O_FORCE], err) != 0) {
            free(tmp_man);
            tmp_man = NULL;
            unlink(val[O_OUTPUT]); /* do not leave a half-published pair */
            goto done;
        }
        free(tmp_man);
        tmp_man = NULL;
    }

    {
        size_t s;
        printf("wrote %s (%zu bytes)\n", val[O_OUTPUT], olen);
        for (s = 0; !val[O_RECIPE] && s < pl.nslots; s++)
            printf("  %s: %s\n", cat.rows[pl.chosen[s]].slot,
                   cat.rows[pl.chosen[s]].variant);
        if (val[O_RECIPE]) {
            char *sum = dungeon_summary(&rc_ctx.r, &rc_ctx.d, rc_ctx.d.has_spawn ? &rc_ctx.ro : NULL, &cfg);
            if (sum)
                fputs(sum, stdout);
            free(sum);
            printf("\n  Reproduce:  nwndgen --game nwn1 --base ");
            print_arg(val[O_BASE]);
            printf(" --recipe ");
            print_arg(val[O_RECIPE]);
            printf(" --seed %llu --party-size %u --min-level %u --max-level %u --output ", (unsigned long long)cfg.seed, (unsigned)cfg.party_size,
                   (unsigned)cfg.min_level, (unsigned)cfg.max_level);
            print_arg(val[O_OUTPUT]);
            printf("\n              (add --force to overwrite; same working directory and same recipe, tile pack, template, roster and script files give the same bytes)\n");
            {
                uint8_t osha2[32];
                char oh2[65];
                sha256(obuf, olen, osha2);
                hex_encode(osha2, 32, oh2);
                printf("  Output SHA-256: %s\n", oh2);
            }
        }
    }
    rc = 0;
done:
    if (rc) {
        fprintf(stderr, "nwndgen: %s\n", err[0] ? err : "failed");
        if (tmp_mod)
            unlink(tmp_mod);
        if (tmp_man)
            unlink(tmp_man);
    }
    free(tmp_mod);
    free(tmp_man);
    free(manifest);
    free(cat_dir);
    free(obuf);
    if (have_out)
        free(out.res); /* payloads belong to base/catalog */
    if (have_plan)
        plan_free(&pl);
    if (have_cat)
        catalog_free(&cat);
    free(tbuf);
    rctx_free(&rc_ctx);
    if (have_base)
        erf_free(&base); /* erf_parse already freed bbuf if it failed */
    return rc;
}
