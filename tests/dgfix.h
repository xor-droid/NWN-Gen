/* Synthetic GFF templates for the dungeon tests. They carry the same field names and types as
 * the real donor ARE / GIT / IFO but no game content, so the tests need no game install. */
#ifndef NWNDGEN_TEST_DGFIX_H
#define NWNDGEN_TEST_DGFIX_H

#include <stdio.h>
#include <string.h>

#include "gff.h"
#include "util.h"

static char fx_err[ERRLEN];
#define FX(x) do { if ((x) != 0) { fprintf(stderr, "fixture: %s\n", fx_err); return -1; } } while (0)

static void fx_root(gff *g, const char *type)
{
    memset(g, 0, sizeof *g);
    memcpy(g->type, type, 5);
    g->root = gff_struct_new(0xFFFFFFFFu);
}

static int fx_are(gff *g)
{
    gff_struct *s;
    fx_root(g, "ARE ");
    s = g->root;
    FX(gff_set_int(s, "ID", GFF_INT, 0, fx_err));
    FX(gff_set_int(s, "Creator_ID", GFF_INT, 0, fx_err));
    FX(gff_set_int(s, "Version", GFF_DWORD, 1, fx_err));
    FX(gff_set_int(s, "Flags", GFF_DWORD, 3, fx_err));
    FX(gff_set_string(s, "Comments", GFF_STRING, "donor", fx_err));
    FX(gff_set_string(s, "ResRef", GFF_RESREF, "donor", fx_err));
    FX(gff_set_string(s, "Tag", GFF_STRING, "DONOR", fx_err));
    FX(gff_set_locstr(s, "Name", "Donor", fx_err));
    FX(gff_set_int(s, "Width", GFF_INT, 1, fx_err));
    FX(gff_set_int(s, "Height", GFF_INT, 1, fx_err));
    FX(gff_set_string(s, "Tileset", GFF_RESREF, "donor", fx_err));
    FX(gff_set_int(s, "ChanceRain", GFF_INT, 25, fx_err));
    FX(gff_set_int(s, "MoonAmbientColor", GFF_DWORD, 0x112233, fx_err));
    FX(gff_set_string(s, "OnEnter", GFF_RESREF, "donor_script", fx_err));
    FX(gff_set_string(s, "OnExit", GFF_RESREF, "", fx_err));
    FX(gff_set_string(s, "OnHeartbeat", GFF_RESREF, "", fx_err));
    FX(gff_set_string(s, "OnUserDefined", GFF_RESREF, "", fx_err));
    if (!gff_set_list(s, "Tile_List", fx_err))
        return -1;
    return 0;
}

static int fx_git(gff *g)
{
    static const char *const lists[] = {"Creature List", "Door List", "Encounter List", "List", "Placeable List",
                                        "SoundList", "StoreList", "TriggerList", "WaypointList"};
    gff_struct *s, *ap, *t, *p;
    gff_field *l;
    size_t i;
    fx_root(g, "GIT ");
    s = g->root;
    ap = gff_set_struct(s, "AreaProperties", 100, fx_err);
    if (!ap)
        return -1;
    FX(gff_set_int(ap, "AmbientSndDay", GFF_INT, 7, fx_err));
    FX(gff_set_int(ap, "MusicDay", GFF_INT, 9, fx_err));
    for (i = 0; i < sizeof lists / sizeof lists[0]; i++)
        if (!gff_set_list(s, lists[i], fx_err))
            return -1;
    l = gff_find(s, "TriggerList");
    t = gff_struct_new(1);
    gff_list_append(l, t);
    FX(gff_set_string(t, "Tag", GFF_STRING, "DONOR_TRIGGER", fx_err));
    FX(gff_set_string(t, "TemplateResRef", GFF_RESREF, "newtransition", fx_err));
    FX(gff_set_string(t, "LinkedTo", GFF_STRING, "somewhere", fx_err));
    FX(gff_set_int(t, "LinkedToFlags", GFF_BYTE, 1, fx_err));
    FX(gff_set_int(t, "Type", GFF_INT, 1, fx_err));
    FX(gff_set_locstr(t, "LocalizedName", "donor", fx_err));
    FX(gff_set_locstr(t, "TransitionDestin", "Donor destination", fx_err));
    FX(gff_set_float(t, "XPosition", 1.0f, fx_err));
    FX(gff_set_float(t, "YPosition", 1.0f, fx_err));
    FX(gff_set_float(t, "ZPosition", 0.0f, fx_err));
    l = gff_set_list(t, "Geometry", fx_err);
    p = gff_struct_new(3);
    gff_list_append(l, p);
    FX(gff_set_float(p, "PointX", 1.0f, fx_err));
    FX(gff_set_float(p, "PointY", 1.0f, fx_err));
    FX(gff_set_float(p, "PointZ", 0.0f, fx_err));
    return 0;
}

static int fx_ifo(gff *g)
{
    static const uint8_t id[16] = {0};
    gff_struct *s, *e;
    gff_field *l;
    fx_root(g, "IFO ");
    s = g->root;
    FX(gff_set_locstr(s, "Mod_Name", "Base", fx_err));
    FX(gff_set_locstr(s, "Mod_Description", "", fx_err));
    FX(gff_set_string(s, "Mod_Tag", GFF_STRING, "BASE", fx_err));
    FX(gff_set_void(s, "Mod_ID", id, 16, fx_err));
    FX(gff_set_string(s, "Mod_Entry_Area", GFF_RESREF, "old", fx_err));
    FX(gff_set_float(s, "Mod_Entry_X", 0.0f, fx_err));
    FX(gff_set_float(s, "Mod_Entry_Y", 0.0f, fx_err));
    FX(gff_set_float(s, "Mod_Entry_Z", 0.0f, fx_err));
    FX(gff_set_float(s, "Mod_Entry_Dir_X", 0.0f, fx_err));
    FX(gff_set_float(s, "Mod_Entry_Dir_Y", 1.0f, fx_err));
    FX(gff_set_int(s, "Mod_MinPerHour", GFF_BYTE, 2, fx_err));
    l = gff_set_list(s, "Mod_Area_list", fx_err);
    e = gff_struct_new(6);
    gff_list_append(l, e);
    FX(gff_set_string(e, "Area_Name", GFF_RESREF, "old", fx_err));
    return 0;
}

#endif
