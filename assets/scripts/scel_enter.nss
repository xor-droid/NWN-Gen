// nwndgen area OnEnter: the first PC to enter an area populates it from the SP* marker
// waypoints the generator placed. Marker tag = 3-letter kind + '_' + blueprint resref:
//   SPC_ creature   SPB_ boss (holds the vault key)   SPT_ boss with 1 hit point (test runs)
//   SPV_ vault chest (locked, plot)
//   SPK_ key item (given to the boss)                 SPR_ relic item (put in the vault)
//   SPD_ decor prop   SPL_ light (brazier, torch ...)   SPX_ lootable container (barrel, crate)
// A creature marker may also carry scel_key / scel_keybp (a door key item to carry: tag and blueprint), and a container marker
// scel_lockdc (it starts locked, pick DC) -- recipe key locks. Locked doors themselves are written into the area GIT.
// A creature or boss marker may carry local variables set by the generator: scel_loot / scel_loot2 (item blueprints the
// creature carries) and scel_gold (gold pieces). Their presence turns off the stock random monster treasure (X2_L_NOTREASURE
// on the module, set before anything is created because the stock OnSpawn script rolls it inside CreateObject).
// A marker with scel_weak (spawn_mode loot_test) makes its creature a pushover: 1 hit point and no weapons.
// The markers are copied into local variables first and objects are created afterwards, because creating
// objects while walking the area's object list can make the walk revisit the same waypoint (TMI).
// Pass 1 creates creatures, boss and vault; pass 2 fills them, so marker order is irrelevant.
// Quest ID "scel_main": entry 1 on the first area entry.
// Henchmen (recipe key henchmen N): the generator stores the count in the module variable scel_hn and the stock henchman
// blueprints in scel_h1..scel_hN; the first PC to enter gets them as companions (AddHenchman), once.

int MAX_MARKERS = 200;

// The first PC to enter takes the generator's henchmen along. Stock henchmen follow, fight and can be spoken to as usual.
void JoinHenchmen(object oPC)
{
    object oMod = GetModule();
    int nCount = GetLocalInt(oMod, "scel_hn");
    int i;
    if (nCount < 1 || GetLocalInt(oMod, "scel_hdone"))
        return;
    SetLocalInt(oMod, "scel_hdone", TRUE);
    if (GetMaxHenchmen() < nCount)
        SetMaxHenchmen(nCount);
    for (i = 1; i <= nCount; i++)
    {
        string sRes = GetLocalString(oMod, "scel_h" + IntToString(i));
        object oHen;
        if (sRes == "")
            continue;
        oHen = CreateObject(OBJECT_TYPE_CREATURE, sRes, GetLocation(oPC));
        if (GetIsObjectValid(oHen))
            AddHenchman(oPC, oHen);
    }
}

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

void main()
{
    object oPC = GetEnteringObject();
    object oArea = OBJECT_SELF;
    object oMod = GetModule();
    object o;
    string sTag;
    string sKind;
    string sBp;
    int nMark = 0;
    int bLoot = FALSE;
    int nSeen = 0;
    int i;

    // Area OnEnter also fires for creatures we spawn here; only a PC may trigger it.
    if (!GetIsPC(oPC))
        return;
    if (!GetLocalInt(oMod, "scel_j"))
    {
        SetLocalInt(oMod, "scel_j", TRUE);
        AddJournalQuestEntry("scel_main", 1, oPC);
    }
    JoinHenchmen(oPC);
    // Set the flag before spawning so nothing can re-enter.
    if (GetLocalInt(oArea, "scel_done"))
        return;
    SetLocalInt(oArea, "scel_done", TRUE);

    // Collect: no object is created inside this loop.
    o = GetFirstObjectInArea(oArea, OBJECT_TYPE_WAYPOINT);
    while (GetIsObjectValid(o) && nMark < MAX_MARKERS && nSeen < 512)
    {
        nSeen++;
        sTag = GetTag(o);
        if (GetStringLeft(sTag, 2) == "SP" && GetStringLength(sTag) > 4)
        {
            SetLocalString(oArea, "scel_t" + IntToString(nMark), sTag);
            SetLocalLocation(oArea, "scel_l" + IntToString(nMark), GetLocation(o));
            SetLocalString(oArea, "scel_a" + IntToString(nMark), GetLocalString(o, "scel_loot"));
            SetLocalString(oArea, "scel_b" + IntToString(nMark), GetLocalString(o, "scel_loot2"));
            SetLocalInt(oArea, "scel_g" + IntToString(nMark), GetLocalInt(o, "scel_gold"));
            SetLocalInt(oArea, "scel_w" + IntToString(nMark), GetLocalInt(o, "scel_weak"));
            SetLocalString(oArea, "scel_k" + IntToString(nMark), GetLocalString(o, "scel_key"));
            SetLocalString(oArea, "scel_y" + IntToString(nMark), GetLocalString(o, "scel_keybp"));
            SetLocalInt(oArea, "scel_d" + IntToString(nMark), GetLocalInt(o, "scel_lockdc"));
            if (GetLocalString(o, "scel_loot") != "" || GetLocalInt(o, "scel_gold") > 0)
                bLoot = TRUE;
            nMark++;
        }
        o = GetNextObjectInArea(oArea, OBJECT_TYPE_WAYPOINT);
    }

    // The generator decided every drop; keep the stock random treasure from adding to it.
    if (bLoot)
        SetLocalInt(oMod, "X2_L_NOTREASURE", TRUE);

    // Pass 1: creatures, boss, vault.
    for (i = 0; i < nMark; i++)
    {
        sTag = GetLocalString(oArea, "scel_t" + IntToString(i));
        sKind = GetStringLeft(sTag, 4);
        sBp = GetStringRight(sTag, GetStringLength(sTag) - 4);
        location lLoc = GetLocalLocation(oArea, "scel_l" + IntToString(i));
        if (sKind == "SPC_")
            GiveSpawnLoot(CreateObject(OBJECT_TYPE_CREATURE, sBp, lLoc), oArea, i);
        else if (sKind == "SPX_")
        {
            // Lootable container; scel_lockdc (recipe key locks) starts it locked, pickable at that DC. Not plot, so it can be bashed.
            object oBox = CreateObject(OBJECT_TYPE_PLACEABLE, sBp, lLoc);
            int nDc = GetLocalInt(oArea, "scel_d" + IntToString(i));
            if (nDc > 0 && GetIsObjectValid(oBox))
            {
                SetLockLockable(oBox, TRUE);
                SetLockUnlockDC(oBox, nDc);
                SetLocked(oBox, TRUE);
            }
        }
        else if (sKind == "SPD_" || sKind == "SPL_")
            CreateObject(OBJECT_TYPE_PLACEABLE, sBp, lLoc); // scenery: props, lights, lootable containers
        else if (sKind == "SPB_" || sKind == "SPT_")
        {
            object oBoss = CreateObject(OBJECT_TYPE_CREATURE, sBp, lLoc, FALSE, "SCEL_BOSS");
            GiveSpawnLoot(oBoss, oArea, i);
            // SPT_ is the quick-test boss: one hit point.
            if (sKind == "SPT_")
            {
                SetCurrentHitPoints(oBoss, 1);
                SendMessageToPC(oPC, "[test mode] " + GetName(oBoss) + " has appeared with 1 hit point.");
            }
            SetLocalObject(oArea, "scel_boss", oBoss);
        }
        else if (sKind == "SPV_")
        {
            object oVault = CreateObject(OBJECT_TYPE_PLACEABLE, sBp, lLoc, FALSE, "SCEL_VAULT");
            SetLockLockable(oVault, TRUE);
            SetLockKeyRequired(oVault, TRUE);
            SetLockKeyTag(oVault, "SCEL_KEY");
            SetLocked(oVault, TRUE);
            SetPlotFlag(oVault, TRUE);
            SetLocalObject(oArea, "scel_vault", oVault);
        }
    }

    // Pass 2: the key goes on the boss, the relic in the vault.
    for (i = 0; i < nMark; i++)
    {
        sTag = GetLocalString(oArea, "scel_t" + IntToString(i));
        sKind = GetStringLeft(sTag, 4);
        sBp = GetStringRight(sTag, GetStringLength(sTag) - 4);
        if (sKind == "SPK_")
        {
            object oBoss = GetLocalObject(oArea, "scel_boss");
            if (GetIsObjectValid(oBoss))
            {
                // Droppable, so it lands in the corpse loot; renamed because the blueprint is an arbitrary
                // non-stacking stock item.
                object oKey = CreateItemOnObject(sBp, oBoss, 1, "SCEL_KEY");
                SetName(oKey, "Vault Key");
                SetIdentified(oKey, TRUE);
                SetDroppableFlag(oKey, TRUE);
            }
        }
        else if (sKind == "SPR_")
        {
            object oChest = GetLocalObject(oArea, "scel_vault");
            if (GetIsObjectValid(oChest))
            {
                // Identified, otherwise a valuable item shows up as just its base type ("Miscellaneous Small").
                object oRelic = CreateItemOnObject(sBp, oChest, 1, "SCEL_RELIC");
                SetIdentified(oRelic, TRUE);
            }
        }
    }
}
