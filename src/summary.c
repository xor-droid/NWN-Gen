#include "summary.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "spawn.h"
#include "util.h"

typedef struct { char *s; size_t len, cap; } sb;

static void put(sb *b, const char *fmt, ...)
{
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    if (b->len + (size_t)n + 1 > b->cap) {
        b->cap = (b->len + (size_t)n + 1) * 2;
        b->s = xrealloc(b->s, b->cap);
    }
    va_start(ap, fmt);
    vsnprintf(b->s + b->len, b->cap - b->len, fmt, ap);
    va_end(ap);
    b->len += (size_t)n;
}

static const char *plural(long n, const char *one, const char *many) { return n == 1 ? one : many; }

typedef struct { char bp[17]; int cr, role, n; } kind_count;

/* role and CR of a blueprint within the chosen theme; role -1 when the roster does not list it */
static void lookup(const roster_theme *t, const char *bp, int *role, int *cr)
{
    int ro;
    size_t i;
    *role = -1;
    *cr = 0;
    for (ro = 0; ro < ROLE_N; ro++)
        for (i = 0; i < t->n[ro]; i++)
            if (!strcmp(t->e[ro][i].resref, bp)) {
                *role = ro;
                *cr = t->e[ro][i].cr;
                return;
            }
}

static int by_cr(const void *a, const void *b)
{
    const kind_count *x = a, *y = b;
    if (x->cr != y->cr)
        return y->cr - x->cr;
    return strcmp(x->bp, y->bp);
}

char *dungeon_summary(const recipe *r, const dungeon *d, const roster *ro, const config *cfg)
{
    sb b = {0};
    const roster_theme *th = (ro && d->has_spawn) ? &ro->theme[d->theme] : NULL;
    kind_count kinds[64];
    size_t nk = 0, a, k, i, nrooms = 0, ndoors = 0;
    long ncreat = 0, nboss = 0, ndecor = 0, nlight = 0, ncont = 0;
    int totcr = 0, maxreg = 0, bosscr = 0, bossarea = -1;
    long gold_total = 0, items_total = 0, boss_gold = 0;
    kind_count loot[32];
    size_t nl2 = 0;
    char bossloot[2][17] = {"", ""};
    char bossbp[17] = "";
    spawn_cfg sc = {ro, cfg->party_size, cfg->min_level, cfg->max_level, r->boss_only};

    put(&b, "\n%s (seed %llu)\n", r->module_name, (unsigned long long)cfg->seed);
    if (cfg->min_level == cfg->max_level)
        put(&b, "  Built for:  character level %u, party of %u\n", cfg->min_level, cfg->party_size);
    else
        put(&b, "  Built for:  character levels %u-%u, party of %u\n", cfg->min_level, cfg->max_level, cfg->party_size);
    put(&b, "  Recipe supports levels %u-%u, parties %u-%u\n", r->min_level, r->max_level, r->min_party, r->max_party);

    for (a = 0; a < d->n; a++) {
        nrooms += (size_t)d->area[a].lay.nrooms;
        ndoors += d->area[a].ndoor;
    }
    put(&b, "  Levels:     %zu areas, %zu rooms, %zu %s, %s corridors\n", d->n, nrooms, ndoors, plural((long)ndoors, "door", "doors"),
        r->kit_corridors ? "stock kit" : "wide");
    if (th)
        put(&b, "  Theme:      %s%s\n", th->name, r->boss_only ? " (boss_only test mode: no regular creatures, boss has 1 hit point)" : "");
    else
        put(&b, "  Theme:      none (geometry only: no roster, creatures or objective)\n");

    for (a = 0; a < d->n; a++) {
        const dungeon_area *ar = &d->area[a];
        int rooms_c[RECIPE_ROOMS_MAX] = {0}, acr = 0, nc = 0, nd = 0, nl = 0, nx = 0, ab = 0;
        for (k = 0; k < ar->nsp; k++) {
            const spawn_pt *p = &ar->sp[k];
            int ri, cr = 0, role = -1, isboss = p->kind != 'C';
            if (p->kind == 'C' || p->kind == 'B') {
                int q;
                for (q = 0; q < 2; q++) {
                    size_t z;
                    if (!p->loot[q][0])
                        continue;
                    items_total++;
                    for (z = 0; z < nl2 && strcmp(loot[z].bp, p->loot[q]); z++)
                        ;
                    if (z == nl2 && nl2 < sizeof loot / sizeof loot[0]) {
                        snprintf(loot[nl2].bp, sizeof loot[nl2].bp, "%s", p->loot[q]);
                        loot[nl2].n = 0;
                        nl2++;
                    }
                    if (z < nl2)
                        loot[z].n++;
                    if (p->kind == 'B')
                        snprintf(bossloot[q], sizeof bossloot[q], "%s", p->loot[q]);
                }
                gold_total += p->gold;
                if (p->kind == 'B')
                    boss_gold = p->gold;
            }
            if (p->kind == 'D') nd++;
            else if (p->kind == 'L') nl++;
            else if (p->kind == 'X') nx++;
            if (p->kind != 'C' && p->kind != 'B' && p->kind != 'T')
                continue;
            if (th)
                lookup(th, p->bp, &role, &cr);
            for (ri = 0; ri < ar->lay.nrooms; ri++)
                if (p->cx >= ar->lay.room[ri][0] && p->cx < ar->lay.room[ri][2] && p->cy >= ar->lay.room[ri][1] && p->cy < ar->lay.room[ri][3]) {
                    rooms_c[ri]++;
                    break;
                }
            acr += cr;
            if (p->kind == 'C') {
                nc++;
                ncreat++;
                if (cr > maxreg) maxreg = cr;
            } else {
                ab = 1;
                nboss++;
                bosscr = cr;
                bossarea = (int)a;
                snprintf(bossbp, sizeof bossbp, "%s", p->bp);
            }
            for (i = 0; i < nk && (strcmp(kinds[i].bp, p->bp) || (kinds[i].role == ROLE_BOSS) != isboss); i++)
                ;
            if (i == nk && nk < sizeof kinds / sizeof kinds[0]) {
                snprintf(kinds[nk].bp, sizeof kinds[nk].bp, "%s", p->bp);
                kinds[nk].cr = cr;
                kinds[nk].role = isboss ? ROLE_BOSS : role;
                kinds[nk].n = 0;
                nk++;
            }
            if (i < nk)
                kinds[i].n++;
        }
        totcr += acr;
        ndecor += nd; nlight += nl; ncont += nx;
        put(&b, "\n  Level %zu: %s (%s), %ux%u tiles, %d rooms", a + 1, r->areas[a].name, ar->resref, r->areas[a].w, r->areas[a].h,
            ar->lay.nrooms);
        if (r->kit_corridors)
            put(&b, ", %zu %s", ar->ndoor, plural((long)ar->ndoor, "door", "doors"));
        put(&b, "\n");
        if (th) {
            int ri;
            put(&b, "    creatures %d%s, CR total %d.%02d; per room (1..n):", nc, ab ? " + boss" : "", acr / 100, acr % 100);
            for (ri = 0; ri < ar->lay.nrooms; ri++)
                put(&b, " %d", rooms_c[ri]);
            put(&b, "\n    scenery: %d decor, %d %s, %d %s\n", nd, nl, plural(nl, "light", "lights"), nx, plural(nx, "container", "containers"));
        }
    }

    if (th) {
        qsort(kinds, nk, sizeof kinds[0], by_cr);
        put(&b, "\n  Creatures (%ld regular%s):\n", ncreat, nboss ? " + 1 boss" : "");
        for (i = 0; i < nk; i++)
            put(&b, "    %-22s %-8s CR %d.%02d  x %d\n", kinds[i].bp, kinds[i].role >= 0 ? ROLE_NAME[kinds[i].role] : "?",
                kinds[i].cr / 100, kinds[i].cr % 100, kinds[i].n);
        put(&b, "    total CR %d.%02d; strongest regular CR %d.%02d; CR limits used: regular <= %d.%02d, boss <= %d.%02d\n", totcr / 100,
            totcr % 100, maxreg / 100, maxreg % 100, spawn_regular_cap(&sc) / 100, spawn_regular_cap(&sc) % 100, spawn_boss_cap(&sc) / 100,
            spawn_boss_cap(&sc) % 100);
        if (bossarea >= 0)
            put(&b, "\n  Objective:  defeat the boss (%s, CR %d.%02d) on level %d, take the key, open the vault (%s) and recover the relic (%s)\n",
                bossbp, bosscr / 100, bosscr % 100, bossarea + 1, ro->vault, ro->relic);
        put(&b, "  Loot:       %ld lootable %s plus the vault (the vault's stock treasure script may add items)\n", ncont, plural(ncont, "container", "containers"));
        put(&b, "  Decor:      %ld props, %ld light %s\n", ndecor, nlight, plural(nlight, "source", "sources"));
        if (items_total || gold_total) {
            size_t z;
            put(&b, "\n  Carried loot (level-scaled by creature CR, fixed by the seed): %ld %s, %ld gold\n", items_total, plural(items_total, "item", "items"), gold_total);
            for (z = 0; z < nl2; z++)
                put(&b, "    %-22s x %d\n", loot[z].bp, loot[z].n);
            if (bossarea >= 0)
                put(&b, "    boss carries: %s%s%s%s and %ld gold (plus the key)\n", bossloot[0][0] ? bossloot[0] : "no item", bossloot[1][0] ? ", " : "", bossloot[1], "", boss_gold);
        }
    }
    put(&b, "\n  Creature CRs, level and party figures are heuristics from the generator, not playtested balance.\n");
    return b.s;
}
