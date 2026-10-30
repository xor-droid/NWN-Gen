#include "recipe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

static int resolve(char *dst, const char *dir, const char *rel, const char *key, int line, char *err)
{
    int n;
    if (!*rel) {
        set_err(err, "recipe line %d: '%s' needs a path", line, key);
        return -1;
    }
    if (rel[0] == '/' || !*dir)
        n = snprintf(dst, RECIPE_PATH_MAX, "%s", rel);
    else
        n = snprintf(dst, RECIPE_PATH_MAX, "%s/%s", dir, rel);
    if (n < 0 || n >= RECIPE_PATH_MAX) {
        set_err(err, "recipe line %d: path too long", line);
        return -1;
    }
    return 0;
}

static int copy_text(char *dst, size_t cap, const char *src, const char *key, int line, char *err)
{
    size_t i;
    if (strlen(src) >= cap) {
        set_err(err, "recipe line %d: '%s' is too long", line, key);
        return -1;
    }
    for (i = 0; src[i]; i++)
        if ((unsigned char)src[i] < 0x20 || (unsigned char)src[i] > 0x7e) {
            set_err(err, "recipe line %d: '%s' must be printable ASCII", line, key);
            return -1;
        }
    strcpy(dst, src);
    return 0;
}

static int parse_uns(const char **p, unsigned lo, unsigned hi, unsigned *out)
{
    char *end;
    unsigned long v;
    if (**p < '0' || **p > '9')
        return -1;
    v = strtoul(*p, &end, 10);
    if (end == *p || (*end && *end != ' ') || v < lo || v > hi)
        return -1;
    *p = *end ? end + 1 : end;
    *out = (unsigned)v;
    return 0;
}

static int word_ok(const char *s, int upper)
{
    size_t i;
    if (!*s)
        return 0;
    for (i = 0; s[i]; i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || c == '_' || (c >= 'a' && c <= 'z') ||
              (upper && c >= 'A' && c <= 'Z')))
            return 0;
    }
    return 1;
}

int recipe_parse(const char *text, size_t len, const char *dir, recipe *r, char *err)
{
    size_t pos = 0;
    int line = 0, seen_head = 0;
    unsigned have = 0; /* bitmask of single-use keys */
    enum { H_TP = 1, H_ARE = 2, H_GIT = 4, H_NAME = 8, H_TAG = 16, H_SUP = 32, H_DESC = 64, H_ROSTER = 128, H_SCRIPTS = 256, H_MODE = 512, H_CORR = 1024, H_DOORS = 2048, H_DOOR_T = 4096 };

    memset(r, 0, sizeof *r);
    while (pos < len) {
        char buf[1024], *rest, *sp;
        size_t n = 0;
        while (pos < len && text[pos] != '\n') {
            if (n + 1 < sizeof buf)
                buf[n] = text[pos];
            n++;
            pos++;
        }
        pos++;
        line++;
        if (n >= sizeof buf) {
            set_err(err, "recipe line %d: line too long", line);
            return -1;
        }
        buf[n] = '\0';
        if (n && buf[n - 1] == '\r')
            buf[--n] = '\0';
        if (!n || buf[0] == '#')
            continue;
        if (!seen_head) {
            if (strcmp(buf, "nwndgen-recipe 1")) {
                set_err(err, "recipe: first line must be 'nwndgen-recipe 1'");
                return -1;
            }
            seen_head = 1;
            continue;
        }
        sp = strchr(buf, ' ');
        if (!sp) {
            set_err(err, "recipe line %d: key without a value", line);
            return -1;
        }
        *sp = '\0';
        rest = sp + 1;
        if (!strcmp(buf, "tilepack") || !strcmp(buf, "template_are") || !strcmp(buf, "template_git")) {
            unsigned bit = buf[1] == 'i' ? H_TP : buf[9] == 'a' ? H_ARE : H_GIT;
            char *dst = bit == H_TP ? r->tilepack : bit == H_ARE ? r->template_are : r->template_git;
            if (have & bit) {
                set_err(err, "recipe line %d: '%s' given twice", line, buf);
                return -1;
            }
            have |= bit;
            if (resolve(dst, dir, rest, buf, line, err))
                return -1;
        } else if (!strcmp(buf, "roster") || !strcmp(buf, "scripts")) {
            unsigned bit = buf[0] == 'r' ? H_ROSTER : H_SCRIPTS;
            if (have & bit) {
                set_err(err, "recipe line %d: '%s' given twice", line, buf);
                return -1;
            }
            have |= bit;
            if (resolve(bit == H_ROSTER ? r->roster : r->scripts, dir, rest, buf, line, err))
                return -1;
        } else if (!strcmp(buf, "spawn_mode")) {
            if (have & H_MODE) { set_err(err, "recipe line %d: 'spawn_mode' given twice", line); return -1; }
            have |= H_MODE;
            if (!strcmp(rest, "boss_only")) r->boss_only = 1;
            else if (strcmp(rest, "full")) { set_err(err, "recipe line %d: spawn_mode must be full or boss_only", line); return -1; }
        } else if (!strcmp(buf, "template_door")) {
            if (have & H_DOOR_T) { set_err(err, "recipe line %d: 'template_door' given twice", line); return -1; }
            have |= H_DOOR_T;
            if (resolve(r->template_door, dir, rest, buf, line, err))
                return -1;
        } else if (!strcmp(buf, "corridors")) {
            if (have & H_CORR) { set_err(err, "recipe line %d: 'corridors' given twice", line); return -1; }
            have |= H_CORR;
            if (!strcmp(rest, "kit")) r->kit_corridors = 1;
            else if (strcmp(rest, "wide")) { set_err(err, "recipe line %d: corridors must be wide or kit", line); return -1; }
        } else if (!strcmp(buf, "doors")) {
            if (have & H_DOORS) { set_err(err, "recipe line %d: 'doors' given twice", line); return -1; }
            have |= H_DOORS;
            if (!strcmp(rest, "plain")) r->doors = 1;
            else if (strcmp(rest, "none")) { set_err(err, "recipe line %d: doors must be none or plain", line); return -1; }
        } else if (!strcmp(buf, "module_name")) {
            if (have & H_NAME) { set_err(err, "recipe line %d: 'module_name' given twice", line); return -1; }
            have |= H_NAME;
            if (copy_text(r->module_name, sizeof r->module_name, rest, buf, line, err))
                return -1;
        } else if (!strcmp(buf, "module_description")) {
            if (have & H_DESC) { set_err(err, "recipe line %d: 'module_description' given twice", line); return -1; }
            have |= H_DESC;
            if (copy_text(r->module_desc, sizeof r->module_desc, rest, buf, line, err))
                return -1;
        } else if (!strcmp(buf, "module_tag")) {
            if (have & H_TAG) { set_err(err, "recipe line %d: 'module_tag' given twice", line); return -1; }
            have |= H_TAG;
            if (strlen(rest) > 32 || !word_ok(rest, 1)) {
                set_err(err, "recipe line %d: module_tag must be 1-32 of A-Z a-z 0-9 _", line);
                return -1;
            }
            strcpy(r->module_tag, rest);
        } else if (!strcmp(buf, "support")) {
            const char *p = rest;
            if (have & H_SUP) { set_err(err, "recipe line %d: 'support' given twice", line); return -1; }
            have |= H_SUP;
            if (parse_uns(&p, 1, 40, &r->min_level) || parse_uns(&p, 1, 40, &r->max_level) ||
                parse_uns(&p, 1, 32, &r->min_party) || parse_uns(&p, 1, 32, &r->max_party) || *p ||
                r->min_level > r->max_level || r->min_party > r->max_party) {
                set_err(err, "recipe line %d: support needs 'min_level max_level min_party max_party'", line);
                return -1;
            }
        } else if (!strcmp(buf, "area")) {
            recipe_area *a;
            const char *p;
            char *name;
            size_t i;
            if (r->nareas == RECIPE_MAX_AREAS) {
                set_err(err, "recipe line %d: more than %d areas", line, RECIPE_MAX_AREAS);
                return -1;
            }
            a = &r->areas[r->nareas];
            sp = strchr(rest, ' ');
            if (!sp) {
                set_err(err, "recipe line %d: area needs 'resref W H rooms name'", line);
                return -1;
            }
            *sp = '\0';
            if (strlen(rest) > 16 || !word_ok(rest, 0)) {
                set_err(err, "recipe line %d: area resref must be 1-16 of a-z 0-9 _", line);
                return -1;
            }
            strcpy(a->resref, rest);
            for (i = 0; i < r->nareas; i++)
                if (!strcmp(r->areas[i].resref, a->resref)) {
                    set_err(err, "recipe line %d: duplicate area '%s'", line, a->resref);
                    return -1;
                }
            p = sp + 1;
            if (parse_uns(&p, RECIPE_AREA_MIN, RECIPE_AREA_MAX, &a->w) ||
                parse_uns(&p, RECIPE_AREA_MIN, RECIPE_AREA_MAX, &a->h) ||
                parse_uns(&p, RECIPE_ROOMS_MIN, RECIPE_ROOMS_MAX, &a->rooms) || !*p) {
                set_err(err, "recipe line %d: area needs W,H in %d-%d, rooms in %d-%d and a name", line,
                        RECIPE_AREA_MIN, RECIPE_AREA_MAX, RECIPE_ROOMS_MIN, RECIPE_ROOMS_MAX);
                return -1;
            }
            name = (char *)p;
            if (copy_text(a->name, sizeof a->name, name, "area name", line, err))
                return -1;
            r->nareas++;
        } else {
            set_err(err, "recipe line %d: unknown key '%s'", line, buf);
            return -1;
        }
    }
    if (!seen_head) {
        set_err(err, "recipe is empty");
        return -1;
    }
    if (!(have & H_TP) || !(have & H_ARE) || !(have & H_GIT) || !(have & H_NAME) || !(have & H_TAG) ||
        !(have & H_SUP) || !r->nareas) {
        set_err(err, "recipe needs tilepack, template_are, template_git, module_name, module_tag, support "
                     "and at least one area");
        return -1;
    }
    if (!!(have & H_ROSTER) != !!(have & H_SCRIPTS)) {
        set_err(err, "recipe: 'roster' and 'scripts' must be given together");
        return -1;
    }
    if (r->doors && !r->template_door[0]) {
        set_err(err, "recipe: doors plain needs template_door (nwnpack templates writes tpl_door.git)");
        return -1;
    }
    if (r->doors && !r->kit_corridors) {
        set_err(err, "recipe: doors plain needs corridors kit (doors only exist in the corridor kit)");
        return -1;
    }
    if (r->boss_only && !(have & H_ROSTER)) {
        set_err(err, "recipe: spawn_mode boss_only needs roster and scripts");
        return -1;
    }
    return 0;
}
