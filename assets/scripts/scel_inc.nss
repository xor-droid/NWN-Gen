// nwndgen shared script code. Include only: scel_enter, scel_use and scel_beat compile it in; it is not shipped in the module.
// Marker n of an area is described by area locals the OnEnter script copied from the marker waypoints:
// scel_t<n> tag, scel_l<n> location, scel_a/scel_b<n> loot items, scel_g<n> gold, scel_w<n> weak, scel_k/scel_y<n> door key tag/blueprint.
// Ambush groups (recipe key ambush): scel_gc<g> member count, scel_gi<g>_<k> member marker index, scel_ga<g> anchor location,
// scel_gx<g> sprung by a chest, scel_go<g> that chest, scel_gf<g> already sprung, scel_ng number of groups.

// Items and gold the generator assigned to marker n. Droppable, so they land in the corpse loot like the key does.
void GiveSpawnLoot(object oCreature, object oArea, int n)
{
    string sA = GetLocalString(oArea, "scel_a" + IntToString(n));
    string sB = GetLocalString(oArea, "scel_b" + IntToString(n));
    int nGold = GetLocalInt(oArea, "scel_g" + IntToString(n));
    int nWeak = GetLocalInt(oArea, "scel_w" + IntToString(n));
    object oItem;
    string sKey = GetLocalString(oArea, "scel_k" + IntToString(n));
    if (!GetIsObjectValid(oCreature))
        return;
    if (nWeak)
    {
        SetCurrentHitPoints(oCreature, 1);
        DestroyObject(GetItemInSlot(INVENTORY_SLOT_RIGHTHAND, oCreature));
        DestroyObject(GetItemInSlot(INVENTORY_SLOT_LEFTHAND, oCreature));
    }
    if (sA != "")
    {
        oItem = CreateItemOnObject(sA, oCreature);
        SetDroppableFlag(oItem, TRUE);
    }
    if (sB != "")
    {
        oItem = CreateItemOnObject(sB, oCreature);
        SetDroppableFlag(oItem, TRUE);
    }
    if (sKey != "")
    {
        // A door key (recipe key locks): the blueprint is an arbitrary stock key, renamed, with the tag the locked door names.
        oItem = CreateItemOnObject(GetLocalString(oArea, "scel_y" + IntToString(n)), oCreature, 1, sKey);
        SetName(oItem, "Door Key");
        SetDroppableFlag(oItem, TRUE);
    }
    if (nGold > 0)
        CreateItemOnObject("nw_it_gold001", oCreature, nGold);
}

// Springs ambush group nGroup of oArea, once: its hidden creatures appear at their markers and go for oPC (or the nearest player).
void FireAmbush(object oArea, int nGroup, object oPC)
{
    string sG = IntToString(nGroup);
    int nCount = GetLocalInt(oArea, "scel_gc" + sG);
    int i;
    if (GetLocalInt(oArea, "scel_gf" + sG))
        return;
    SetLocalInt(oArea, "scel_gf" + sG, TRUE);
    if (!GetIsObjectValid(oPC))
        oPC = GetNearestCreatureToLocation(CREATURE_TYPE_PLAYER_CHAR, PLAYER_CHAR_IS_PC, GetLocalLocation(oArea, "scel_ga" + sG));
    for (i = 0; i < nCount; i++)
    {
        string sIdx = IntToString(GetLocalInt(oArea, "scel_gi" + sG + "_" + IntToString(i)));
        string sTag = GetLocalString(oArea, "scel_t" + sIdx);
        string sBp = GetStringRight(sTag, GetStringLength(sTag) - 4);
        object oC = CreateObject(OBJECT_TYPE_CREATURE, sBp, GetLocalLocation(oArea, "scel_l" + sIdx));
        GiveSpawnLoot(oC, oArea, StringToInt(sIdx));
        if (GetIsObjectValid(oC) && GetIsObjectValid(oPC))
            AssignCommand(oC, ActionAttack(oPC));
    }
    if (GetIsObjectValid(oPC))
        FloatingTextStringOnCreature("Ambush!", oPC, FALSE);
}

// A trap (recipe key traps): nType is a TRAP_BASE_TYPE_* number, nDc the detect and disarm DC. Findable, disarmable, springs once.
void ScelSetTrap(object oTrap, int nDc)
{
    if (!GetIsObjectValid(oTrap))
        return;
    SetTrapDetectable(oTrap, TRUE);
    SetTrapDisarmable(oTrap, TRUE);
    SetTrapDetectDC(oTrap, nDc);
    SetTrapDisarmDC(oTrap, nDc);
    SetTrapOneShot(oTrap, TRUE);
}

// A lever (recipe key levers): the first pull unlocks and opens the gate door named by the lever's scel_gate local (its tag). Call it with the lever as OBJECT_SELF.
void PullLever(object oLever, object oUser)
{
    object oDoor;
    if (GetLocalInt(oLever, "scel_pulled"))
        return;
    SetLocalInt(oLever, "scel_pulled", TRUE);
    PlayAnimation(ANIMATION_PLACEABLE_ACTIVATE);
    oDoor = GetObjectByTag(GetLocalString(oLever, "scel_gate"));
    if (GetIsObjectValid(oDoor))
    {
        SetLocked(oDoor, FALSE);
        AssignCommand(oDoor, ActionOpenDoor(oDoor));
        if (GetIsObjectValid(oUser))
            FloatingTextStringOnCreature("Somewhere a gate grinds open.", oUser, FALSE);
    }
    else if (GetIsObjectValid(oUser))
        FloatingTextStringOnCreature("The lever moves, but nothing answers.", oUser, FALSE);
}

// Patrols (recipe key patrols): marker n carries scel_rt<n>, the packed route cells (x1,y1,x2,y2 in 8 bits each, x2 = y2 = 0 when there is one extra cell).
// The creature walks between its own spot (scel_p0) and those cells (scel_p1, scel_p2); scel_pn is how many of them it has, scel_ps the one it went to last.
// Area locals: scel_pc patrol count, scel_po<k> the creatures.
location ScelCellLocation(object oArea, int nX, int nY, location lHome)
{
    vector vHome = GetPositionFromLocation(lHome);
    return Location(oArea, Vector(IntToFloat(nX) * 10.0 + 5.0, IntToFloat(nY) * 10.0 + 5.0, vHome.z), 0.0);
}

void RegisterPatrol(object oC, object oArea, int n, location lHome)
{
    int nRt = GetLocalInt(oArea, "scel_rt" + IntToString(n));
    int nCount = GetLocalInt(oArea, "scel_pc");
    if (nRt <= 0 || !GetIsObjectValid(oC))
        return;
    SetLocalObject(oArea, "scel_po" + IntToString(nCount), oC);
    SetLocalInt(oArea, "scel_pc", nCount + 1);
    SetLocalLocation(oC, "scel_p0", lHome);
    SetLocalLocation(oC, "scel_p1", ScelCellLocation(oArea, nRt / 16777216, (nRt / 65536) % 256, lHome));
    SetLocalInt(oC, "scel_pn", 2);
    if (nRt % 65536 > 0)
    {
        SetLocalLocation(oC, "scel_p2", ScelCellLocation(oArea, (nRt / 256) % 256, nRt % 256, lHome));
        SetLocalInt(oC, "scel_pn", 3);
    }
}

// Sends every idle patrol creature (not fighting, not already walking or waiting) to the next cell of its route, then waits a few seconds there.
void NudgePatrols(object oArea)
{
    int nCount = GetLocalInt(oArea, "scel_pc");
    int i;
    for (i = 0; i < nCount; i++)
    {
        object oC = GetLocalObject(oArea, "scel_po" + IntToString(i));
        int nStep;
        if (!GetIsObjectValid(oC) || GetIsDead(oC) || GetIsInCombat(oC) || GetCurrentAction(oC) != ACTION_INVALID)
            continue;
        nStep = (GetLocalInt(oC, "scel_ps") + 1) % GetLocalInt(oC, "scel_pn");
        SetLocalInt(oC, "scel_ps", nStep);
        AssignCommand(oC, ActionMoveToLocation(GetLocalLocation(oC, "scel_p" + IntToString(nStep))));
        AssignCommand(oC, ActionWait(2.0 + IntToFloat(Random(4))));
    }
}

// Random encounter (recipe key encounters), around player oPC: a group of the dungeon's own creatures appears at the spot of a planned (not hidden) creature
// 10-45 m away and goes for the player. Never next to a living boss, at most scel_ecap times per area (module locals: scel_ec pool size, scel_e<k> resrefs,
// scel_eg group size, scel_ecap, scel_epct, scel_etick). The creatures carry nothing: the stock random treasure is already switched off by scel_enter.
// Returns TRUE if a group came.
int ScelEncounter(object oPC)
{
    object oMod = GetModule();
    object oArea = GetArea(oPC);
    object oBoss = GetLocalObject(oArea, "scel_boss");
    location lPC = GetLocation(oPC);
    int nPool = GetLocalInt(oMod, "scel_ec");
    int nMark = GetLocalInt(oArea, "scel_nm");
    int nSpots = 0;
    int nPick;
    int i;
    int bCount;
    if (nPool < 1 || !GetIsObjectValid(oArea) || !GetLocalInt(oArea, "scel_done") || GetLocalInt(oArea, "scel_en") >= GetLocalInt(oMod, "scel_ecap"))
        return FALSE;
    for (bCount = TRUE; bCount >= FALSE; bCount--)
    {
        // First pass counts the usable spots, second pass walks to the chosen one.
        int nSeen = 0;
        if (!bCount && nSpots < 1)
            return FALSE;
        if (!bCount)
            nPick = Random(nSpots);
        for (i = 0; i < nMark; i++)
        {
            location lAt = GetLocalLocation(oArea, "scel_l" + IntToString(i));
            float fD;
            if (GetStringLeft(GetLocalString(oArea, "scel_t" + IntToString(i)), 4) != "SPC_" || GetLocalInt(oArea, "scel_m" + IntToString(i)) > 0)
                continue;
            fD = GetDistanceBetweenLocations(lAt, lPC);
            if (fD < 10.0 || fD > 45.0 || (GetIsObjectValid(oBoss) && GetDistanceBetweenLocations(lAt, GetLocation(oBoss)) < 12.0))
                continue;
            if (bCount)
                nSpots++;
            else if (nSeen++ == nPick)
            {
                int nGroup = GetLocalInt(oMod, "scel_eg");
                int k;
                SetLocalInt(oArea, "scel_en", GetLocalInt(oArea, "scel_en") + 1);
                for (k = 0; k < nGroup; k++)
                {
                    object oC = CreateObject(OBJECT_TYPE_CREATURE, GetLocalString(oMod, "scel_e" + IntToString(1 + Random(nPool))), lAt);
                    if (GetIsObjectValid(oC))
                        AssignCommand(oC, ActionAttack(oPC));
                }
                FloatingTextStringOnCreature("Something stirs in the dark...", oPC, FALSE);
                return TRUE;
            }
        }
    }
    return FALSE;
}

// Called by the area heartbeat: counts heartbeats in which the first player in the area has no living enemy within 20 m, and rolls an encounter when
// scel_etick of them have passed in a row.
void QuietTick(object oArea)
{
    object oMod = GetModule();
    object oPC;
    object oFoe;
    int nTick = GetLocalInt(oMod, "scel_etick");
    int nQuiet;
    if (GetLocalInt(oMod, "scel_ec") < 1)
        return;
    oPC = GetFirstPC();
    while (GetIsObjectValid(oPC) && GetArea(oPC) != oArea)
        oPC = GetNextPC();
    if (!GetIsObjectValid(oPC))
        return;
    oFoe = GetNearestCreature(CREATURE_TYPE_REPUTATION, REPUTATION_TYPE_ENEMY, oPC, 1, CREATURE_TYPE_IS_ALIVE, TRUE);
    if (GetIsObjectValid(oFoe) && GetDistanceBetween(oPC, oFoe) < 20.0)
    {
        SetLocalInt(oArea, "scel_eq", 0);
        return;
    }
    nQuiet = GetLocalInt(oArea, "scel_eq") + 1;
    SetLocalInt(oArea, "scel_eq", nQuiet);
    if (nQuiet >= nTick)
    {
        SetLocalInt(oArea, "scel_eq", 0);
        ScelEncounter(oPC);
    }
}
