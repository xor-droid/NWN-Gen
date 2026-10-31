// nwndgen area heartbeat (set only with recipe keys ambush, patrols or encounters).
// Ambush: springs the groups that no chest guards when a player comes within 8 m of the group's first creature; a group guarded by a chest also
// springs when the chest is found open (OnOpen is the main trigger) or destroyed.
// Patrols: nudges the idle patrol creatures along their routes. Encounters: counts quiet heartbeats towards a random encounter.
// Reactions: death alerts and the boss's call for help.
#include "scel_inc"

void main()
{
    object oArea = OBJECT_SELF;
    int nGroups = GetLocalInt(oArea, "scel_ng");
    int g;
    for (g = 1; g <= nGroups; g++)
    {
        string sG = IntToString(g);
        if (GetLocalInt(oArea, "scel_gf" + sG))
            continue;
        if (GetLocalInt(oArea, "scel_gx" + sG))
        {
            object oBox = GetLocalObject(oArea, "scel_go" + sG);
            if (!GetIsObjectValid(oBox) || GetIsOpen(oBox))
                FireAmbush(oArea, g, OBJECT_INVALID);
        }
        else
        {
            location lAt = GetLocalLocation(oArea, "scel_ga" + sG);
            object oPC = GetNearestCreatureToLocation(CREATURE_TYPE_PLAYER_CHAR, PLAYER_CHAR_IS_PC, lAt);
            if (GetIsObjectValid(oPC) && GetArea(oPC) == oArea && GetDistanceBetweenLocations(GetLocation(oPC), lAt) < 8.0)
                FireAmbush(oArea, g, oPC);
        }
    }
    NudgePatrols(oArea);
    QuietTick(oArea);
    WatchDeaths(oArea);
    BossCall(oArea);
}
