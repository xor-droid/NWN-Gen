#include "game.h"

#include <string.h>

static const uint16_t NWN1_PARTS[] = {RES_ARE, RES_GIT, RES_GIC};
static const uint16_t NWN2_PARTS[] = {RES_ARE, RES_GIT, RES_GIC, RES_TRN,
                                      RES_TRX};

const game_profile GAME_NWN1 = {"nwn1", "Neverwinter Nights: Enhanced Edition",
                                "V1.0", 16, 24, NWN1_PARTS, 3};
const game_profile GAME_NWN2 = {"nwn2", "Neverwinter Nights 2: Enhanced Edition",
                                "V1.1", 32, 40, NWN2_PARTS, 5};

const game_profile *game_by_name(const char *name)
{
    if (!strcmp(name, "nwn1"))
        return &GAME_NWN1;
    if (!strcmp(name, "nwn2"))
        return &GAME_NWN2;
    return NULL;
}

const game_profile *game_by_erf_version(const char *v)
{
    if (!memcmp(v, GAME_NWN1.erf_version, 4))
        return &GAME_NWN1;
    if (!memcmp(v, GAME_NWN2.erf_version, 4))
        return &GAME_NWN2;
    return NULL;
}

const char *res_ext(uint16_t t)
{
    switch (t) {
    case RES_NSS: return "nss";
    case RES_NCS: return "ncs";
    case RES_ARE: return "are";
    case RES_IFO: return "ifo";
    case RES_GIT: return "git";
    case RES_GIC: return "gic";
    case RES_TRN: return "trn";
    case RES_TRX: return "trx";
    default: return "?";
    }
}

const char *res_gff_type(uint16_t t)
{
    switch (t) {
    case RES_ARE: return "ARE ";
    case RES_GIT: return "GIT ";
    case RES_GIC: return "GIC ";
    case RES_IFO: return "IFO ";
    default: return NULL;
    }
}

/* Extension <-> type table (NWN1 IDs from the neverwinter.nim restype table, plus
 * the NWN2 terrain types seen in stock NWN2 modules). Used by erfpack. */
static const struct { uint16_t id; const char *ext; } EXT[] = {
    {1, "bmp"},
    {2, "mve"},
    {3, "tga"},
    {4, "wav"},
    {5, "wfx"},
    {6, "plt"},
    {7, "ini"},
    {8, "bmu"},
    {9, "mpg"},
    {10, "txt"},
    {2000, "plh"},
    {2001, "tex"},
    {2002, "mdl"},
    {2003, "thg"},
    {2005, "fnt"},
    {2007, "lua"},
    {2008, "slt"},
    {2009, "nss"},
    {2010, "ncs"},
    {2012, "are"},
    {2013, "set"},
    {2014, "ifo"},
    {2015, "bic"},
    {2016, "wok"},
    {2017, "2da"},
    {2018, "tlk"},
    {2022, "txi"},
    {2023, "git"},
    {2024, "bti"},
    {2025, "uti"},
    {2026, "btc"},
    {2027, "utc"},
    {2029, "dlg"},
    {2030, "itp"},
    {2031, "btt"},
    {2032, "utt"},
    {2033, "dds"},
    {2034, "bts"},
    {2035, "uts"},
    {2036, "ltr"},
    {2037, "gff"},
    {2038, "fac"},
    {2039, "bte"},
    {2040, "ute"},
    {2041, "btd"},
    {2042, "utd"},
    {2043, "btp"},
    {2044, "utp"},
    {2045, "dft"},
    {2046, "gic"},
    {2047, "gui"},
    {2048, "css"},
    {2049, "ccs"},
    {2050, "btm"},
    {2051, "utm"},
    {2052, "dwk"},
    {2053, "pwk"},
    {2054, "btg"},
    {2055, "utg"},
    {2056, "jrl"},
    {2057, "sav"},
    {2058, "utw"},
    {2059, "4pc"},
    {2060, "ssf"},
    {2063, "bik"},
    {2064, "ndb"},
    {2065, "ptm"},
    {2066, "ptt"},
    {2067, "bak"},
    {2068, "dat"},
    {2069, "shd"},
    {2070, "xbc"},
    {2071, "wbm"},
    {2072, "mtr"},
    {2073, "ktx"},
    {2074, "ttf"},
    {2075, "sql"},
    {2076, "tml"},
    {2077, "sq3"},
    {2078, "lod"},
    {2079, "gif"},
    {2080, "png"},
    {2081, "jpg"},
    {2082, "caf"},
    {2083, "jui"},
    {3002, "trn"},
    {3035, "trx"},
};

int res_type_by_ext(const char *ext)
{
    size_t i;
    for (i = 0; i < sizeof EXT / sizeof EXT[0]; i++)
        if (!strcmp(EXT[i].ext, ext))
            return EXT[i].id;
    return -1;
}
