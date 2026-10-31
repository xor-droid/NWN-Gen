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
