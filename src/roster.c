#include "roster.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gff.h"
#include "util.h"

const char *const ROLE_NAME[ROLE_N] = {"minion", "standard", "elite", "boss"};
const char *const DECO_NAME[DECO_N] = {"prop", "light", "container"};

int roster_parse_cr(const char *s)
{
    int whole = 0, frac = 0, places = 0;
    if (!*s || *s == '.')
        return -1;
    for (; *s >= '0' && *s <= '9'; s++) {
        whole = whole * 10 + (*s - '0');
        if (whole > ROSTER_CR_MAX / 100)
            return -1;
    }
    if (*s == '.') {
        for (s++; *s >= '0' && *s <= '9'; s++, places++) {
            if (places == 2)
                return -1;
            frac = frac * 10 + (*s - '0');
        }
        if (!places)
            return -1;
        if (places == 1)
            frac *= 10;
    }
    return *s ? -1 : whole * 100 + frac;
}

static int resref_ok(const char *s)
{
    size_t i, n = strlen(s);
    if (!n || n > 16)
        return 0;
    for (i = 0; i < n; i++)
        if (!((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= '0' && s[i] <= '9') || s[i] == '_'))
            return 0;
    return 1;
}

static int bad(char *err, int line, const char *msg)
{
    set_err(err, "roster line %d: %s", line, msg);
    return -1;
}

int roster_parse(const char *text, size_t len, roster *ro, char *err)
{
    size_t pos = 0;
    int line = 0, seen_head = 0;
    roster_theme *cur = NULL;

    memset(ro, 0, sizeof *ro);
    while (pos < len) {
        char buf[512], *sp, *rest, *sp2;
        size_t n = 0;
        while (pos < len && text[pos] != '\n') {
            if (n + 1 < sizeof buf)
                buf[n] = text[pos];
            n++;
            pos++;
        }
        pos++;
        line++;
        if (n >= sizeof buf)
            return bad(err, line, "line too long");
        buf[n] = '\0';
        if (n && buf[n - 1] == '\r')
            buf[--n] = '\0';
        if (!n || buf[0] == '#')
            continue;
        if (!seen_head) {
            if (strcmp(buf, "nwndgen-roster 1")) {
                set_err(err, "roster: first line must be 'nwndgen-roster 1'");
                return -1;
            }
            seen_head = 1;
            continue;
        }
        sp = strchr(buf, ' ');
        if (!sp)
            return bad(err, line, "key without a value");
        *sp = '\0';
        rest = sp + 1;
        if (!strcmp(buf, "vault") || !strcmp(buf, "key") || !strcmp(buf, "relic")) {
            char *dst = buf[0] == 'v' ? ro->vault : buf[0] == 'k' ? ro->key : ro->relic;
            if (*dst)
                return bad(err, line, "key given twice");
            if (!resref_ok(rest))
                return bad(err, line, "resref must be 1-16 of a-z 0-9 _");
            strcpy(dst, rest);
        } else if (!strcmp(buf, "theme")) {
            size_t i;
            if (ro->ntheme == ROSTER_THEMES)
                return bad(err, line, "too many themes");
            sp2 = strchr(rest, ' ');
            if (!sp2 || !sp2[1])
                return bad(err, line, "theme needs 'id name'");
            *sp2 = '\0';
            if (!resref_ok(rest))
                return bad(err, line, "theme id must be 1-16 of a-z 0-9 _");
            if (strlen(sp2 + 1) >= ROSTER_NAME_MAX)
                return bad(err, line, "theme name too long");
            for (i = 0; i < ro->ntheme; i++)
                if (!strcmp(ro->theme[i].id, rest))
                    return bad(err, line, "duplicate theme id");
            cur = &ro->theme[ro->ntheme++];
            strcpy(cur->id, rest);
            strcpy(cur->name, sp2 + 1);
        } else if (!strcmp(buf, "prop") || !strcmp(buf, "light") || !strcmp(buf, "container")) {
            int kind = buf[0] == 'p' ? DECO_PROP : buf[0] == 'l' ? DECO_LIGHT : DECO_CONTAINER, w;
            size_t i;
            char *end;
            long v;
            if (!cur)
                return bad(err, line, "scenery line before any theme");
            sp2 = strchr(rest, ' ');
            if (!sp2)
                return bad(err, line, "needs 'resref weight'");
            *sp2 = '\0';
            if (!resref_ok(rest))
                return bad(err, line, "resref must be 1-16 of a-z 0-9 _");
            v = strtol(sp2 + 1, &end, 10);
            if (end == sp2 + 1 || *end || v < 1 || v > 100)
                return bad(err, line, "weight must be a whole number 1-100");
            w = (int)v;
            if (cur->ndeco[kind] == ROSTER_POOL)
                return bad(err, line, "too many entries in this scenery list");
            for (i = 0; i < cur->ndeco[kind]; i++)
                if (!strcmp(cur->deco[kind][i].resref, rest))
                    return bad(err, line, "duplicate scenery entry");
            strcpy(cur->deco[kind][cur->ndeco[kind]].resref, rest);
            cur->deco[kind][cur->ndeco[kind]++].cr = w;
        } else if (!strcmp(buf, "ambience")) {
            uint32_t col[3];
            char *tok = rest, *next;
            int k;
            long amount, clip;
            char *end;
            if (!cur)
                return bad(err, line, "ambience before any theme");
            if (cur->has_ambience)
                return bad(err, line, "ambience given twice for this theme");
            for (k = 0; k < 3; k++) {
                unsigned long v;
                size_t j;
                next = strchr(tok, ' ');
                if (!next)
                    return bad(err, line, "ambience needs 3 colours, a fog amount and a fog clip");
                *next = '\0';
                if (strlen(tok) != 6)
                    return bad(err, line, "colours are six hex digits RRGGBB");
                for (j = 0; j < 6; j++)
                    if (!((tok[j] >= '0' && tok[j] <= '9') || (tok[j] >= 'a' && tok[j] <= 'f') || (tok[j] >= 'A' && tok[j] <= 'F')))
                        return bad(err, line, "colours are six hex digits RRGGBB");
                v = strtoul(tok, NULL, 16);
                col[k] = (uint32_t)((v >> 16 & 0xff) | (v >> 8 & 0xff) << 8 | (v & 0xff) << 16);
                tok = next + 1;
            }
            next = strchr(tok, ' ');
            if (!next)
                return bad(err, line, "ambience needs 3 colours, a fog amount and a fog clip");
            *next = '\0';
            amount = strtol(tok, &end, 10);
            if (end == tok || *end || amount < 0 || amount > 15)
                return bad(err, line, "fog amount must be 0-15");
            clip = strtol(next + 1, &end, 10);
            if (end == next + 1 || *end || clip < 20 || clip > 150)
                return bad(err, line, "fog clip must be 20-150");
            cur->has_ambience = 1;
            cur->amb_ambient = col[0];
            cur->amb_diffuse = col[1];
            cur->amb_fog = col[2];
            cur->amb_fog_amount = (int)amount;
            cur->amb_fog_clip = (int)clip;
        } else {
            int role, cr;
            size_t i;
            for (role = 0; role < ROLE_N && strcmp(buf, ROLE_NAME[role]); role++) ;
            if (role == ROLE_N) {
                set_err(err, "roster line %d: unknown key '%s'", line, buf);
                return -1;
            }
            if (!cur)
                return bad(err, line, "role line before any theme");
            sp2 = strchr(rest, ' ');
            if (!sp2)
                return bad(err, line, "needs 'resref CR'");
            *sp2 = '\0';
            if (!resref_ok(rest))
                return bad(err, line, "resref must be 1-16 of a-z 0-9 _");
            cr = roster_parse_cr(sp2 + 1);
            if (cr < 0)
                return bad(err, line, "CR must be decimal with at most two places");
            if (cur->n[role] == ROSTER_POOL)
                return bad(err, line, "too many entries in this role");
            for (i = 0; i < cur->n[role]; i++)
                if (!strcmp(cur->e[role][i].resref, rest))
                    return bad(err, line, "duplicate entry in this role");
            strcpy(cur->e[role][cur->n[role]].resref, rest);
            cur->e[role][cur->n[role]++].cr = cr;
        }
    }
    if (!seen_head) {
        set_err(err, "roster is empty");
        return -1;
    }
    if (!*ro->vault || !*ro->key || !*ro->relic || !ro->ntheme) {
        set_err(err, "roster needs vault, key, relic and at least one theme");
        return -1;
    }
    if (!strcmp(ro->key, ro->relic)) {
        set_err(err, "roster: key and relic must be different items (they are told apart by tag)");
        return -1;
    }
    {
        size_t t;
        for (t = 0; t < ro->ntheme; t++)
            if (!ro->theme[t].n[ROLE_MINION] || !ro->theme[t].n[ROLE_BOSS]) {
                set_err(err, "roster: theme '%s' needs at least one minion and one boss", ro->theme[t].id);
                return -1;
            }
    }
    return 0;
}

/* ---- journal ---- */

#define TRY(x) do { if ((x) != 0) goto fail; } while (0)

static int add_entry(gff_field *list, uint32_t id, int end, const char *text, char *err)
{
    gff_struct *e = gff_struct_new(0);
    gff_list_append(list, e);
    if (gff_set_int(e, "End", GFF_WORD, end, err) || gff_set_int(e, "ID", GFF_DWORD, id, err) ||
        gff_set_locstr(e, "Text", text, err))
        return -1;
    return 0;
}

int roster_build_jrl(const char *quest_name, const char *theme_name, uint8_t **buf, size_t *len, char *err)
{
    gff g;
    gff_struct *c;
    gff_field *cats, *ent;
    char t1[320];
    int rc;

    memset(&g, 0, sizeof g);
    memcpy(g.type, "JRL ", 5);
    g.root = gff_struct_new(0xFFFFFFFFu);
    cats = gff_set_list(g.root, "Categories", err);
    if (!cats)
        goto fail;
    c = gff_struct_new(0);
    gff_list_append(cats, c);
    TRY(gff_set_string(c, "Comment", GFF_STRING, "generated by nwndgen", err));
    TRY(gff_set_locstr(c, "Name", quest_name, err));
    TRY(gff_set_int(c, "Picture", GFF_WORD, 65535, err));
    TRY(gff_set_int(c, "Priority", GFF_DWORD, 1, err));
    TRY(gff_set_string(c, "Tag", GFF_STRING, "scel_main", err));
    TRY(gff_set_int(c, "XP", GFF_DWORD, 0, err));
    ent = gff_set_list(c, "EntryList", err);
    if (!ent)
        goto fail;
    snprintf(t1, sizeof t1,
             "I have entered the cellars. %s hold this place. Their leader carries the key to a sealed "
             "vault.",
             theme_name);
    TRY(add_entry(ent, 1, 0, t1, err));
    TRY(add_entry(ent, 2, 0,
                  "I took the vault key from the leader. The sealed vault stands in the same chamber.", err));
    TRY(add_entry(ent, 3, 1, "I opened the vault and took the relic. The cellars have given up their secret.", err));
    rc = gff_write(&g, buf, len, err);
    gff_free(&g);
    return rc;
fail:
    gff_free(&g);
    return -1;
}
