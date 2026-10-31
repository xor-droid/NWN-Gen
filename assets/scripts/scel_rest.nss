// nwndgen module OnPlayerRest (set only with recipe key encounters): when a player starts to rest, a random encounter may turn up
// (module local scel_epct is the chance in percent). The encounter itself is ScelEncounter in scel_inc.
#include "scel_inc"

void main()
{
    object oPC = GetLastPCRested();
    if (GetLastRestEventType() != REST_EVENTTYPE_REST_STARTED || !GetIsPC(oPC))
        return;
    if (Random(100) < GetLocalInt(GetModule(), "scel_epct"))
        ScelEncounter(oPC);
}
