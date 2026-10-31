// nwndgen area OnEnter: the first PC to enter an area populates it from the SP* marker
// waypoints the generator placed. Marker tag = 3-letter kind + '_' + blueprint resref:
//   SPC_ creature   SPB_ boss (holds the vault key)   SPT_ boss with 1 hit point (test runs)
//   SPV_ vault chest (locked, plot)
//   SPK_ key item (given to the boss)                 SPR_ relic item (put in the vault)
//   SPD_ decor prop   SPL_ light (brazier, torch ...)   SPX_ lootable container (barrel, crate)
// A creature marker may also carry scel_key / scel_keybp (a door key item to carry: tag and blueprint), and a container marker
// scel_lockdc (it starts locked, pick DC) -- recipe key locks. Locked doors themselves are written into the area GIT.
// scel_amb (recipe key ambush): a creature marker with it stays hidden until its group is sprung (see scel_inc, scel_use, scel_beat);
// a container marker with it springs that group when opened.
// Recipe key reactions: the vault gets scel_use on OnOpen (reinforcements), every planned creature is on the death-alert watch list, and the boss calls for help once (scel_beat).
// scel_rt (recipe key patrols): a creature marker with it walks a route inside its room (see scel_inc, scel_beat).
// SPE_ lever (recipe key levers): scel_gate = tag of the gate door it opens.
// SPP_ trap (recipe key traps): scel_trap = TRAP_BASE_TYPE, scel_trapdc = detect and disarm DC; a container marker with scel_trapdc starts trapped.
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

#include "scel_inc"

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
    int nGroups = 0;
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
            SetLocalInt(oArea, "scel_m" + IntToString(nMark), GetLocalInt(o, "scel_amb"));
            SetLocalInt(oArea, "scel_tt" + IntToString(nMark), GetLocalInt(o, "scel_trap"));
            SetLocalInt(oArea, "scel_td" + IntToString(nMark), GetLocalInt(o, "scel_trapdc"));
            SetLocalString(oArea, "scel_gt" + IntToString(nMark), GetLocalString(o, "scel_gate"));
            SetLocalInt(oArea, "scel_rt" + IntToString(nMark), GetLocalInt(o, "scel_rt"));
            if (GetLocalString(o, "scel_loot") != "" || GetLocalInt(o, "scel_gold") > 0)
                bLoot = TRUE;
            nMark++;
        }
        o = GetNextObjectInArea(oArea, OBJECT_TYPE_WAYPOINT);
    }

    SetLocalInt(oArea, "scel_nm", nMark); // ScelEncounter walks the markers again
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
        int nAmb = GetLocalInt(oArea, "scel_m" + IntToString(i));
        if (nAmb > nGroups)
            nGroups = nAmb;
        if (sKind == "SPC_" && nAmb > 0)
        {
            // Ambush member (recipe key ambush): stays hidden; FireAmbush creates it when its group is sprung.
            string sG = IntToString(nAmb);
            int nCount = GetLocalInt(oArea, "scel_gc" + sG);
            SetLocalInt(oArea, "scel_gi" + sG + "_" + IntToString(nCount), i);
            SetLocalInt(oArea, "scel_gc" + sG, nCount + 1);
            if (nCount == 0)
                SetLocalLocation(oArea, "scel_ga" + sG, lLoc);
        }
        else if (sKind == "SPC_")
        {
            object oCreature = CreateObject(OBJECT_TYPE_CREATURE, sBp, lLoc);
            GiveSpawnLoot(oCreature, oArea, i);
            RegisterPatrol(oCreature, oArea, i, lLoc); // recipe key patrols: it walks a route (scel_beat)
            RegisterWatch(oCreature, oArea); // recipe key reactions: its death may raise an alarm (scel_beat)
        }
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
            if (GetLocalInt(oArea, "scel_td" + IntToString(i)) > 0 && GetIsObjectValid(oBox))
            {
                // Trapped container (recipe key traps).
                CreateTrapOnObject(GetLocalInt(oArea, "scel_tt" + IntToString(i)), oBox);
                ScelSetTrap(oBox, GetLocalInt(oArea, "scel_td" + IntToString(i)));
            }
            if (nAmb > 0 && GetIsObjectValid(oBox))
            {
                // Opening this container springs ambush group nAmb (scel_use); scel_beat also springs it if the chest is found open or destroyed.
                SetLocalInt(oBox, "scel_amb", nAmb);
                SetLocalInt(oArea, "scel_gx" + IntToString(nAmb), TRUE);
                SetLocalObject(oArea, "scel_go" + IntToString(nAmb), oBox);
                HookOpen(oBox); // keeps the stock OnOpen script, scel_use runs it first
            }
        }
        else if (sKind == "SPE_")
        {
            // Lever (recipe key levers): usable; using it unlocks and opens the gate door whose tag is in scel_gate (see scel_use).
            object oLever = CreateObject(OBJECT_TYPE_PLACEABLE, sBp, lLoc);
            if (GetIsObjectValid(oLever))
            {
                SetLocalString(oLever, "scel_gate", GetLocalString(oArea, "scel_gt" + IntToString(i)));
                SetEventScript(oLever, EVENT_SCRIPT_PLACEABLE_ON_USED, "scel_use");
            }
        }
        else if (sKind == "SPP_")
        {
            // Floor trap (recipe key traps): type and DC come from the marker.
            ScelSetTrap(CreateTrapAtLocation(GetLocalInt(oArea, "scel_tt" + IntToString(i)), lLoc, 2.0), GetLocalInt(oArea, "scel_td" + IntToString(i)));
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
            if (GetLocalInt(oMod, "scel_rc") > 0)
            {
                // Recipe key reactions: opening the vault brings reinforcements (scel_use).
                SetLocalInt(oVault, "scel_react", TRUE);
                HookOpen(oVault);
            }
            SetLocalObject(oArea, "scel_vault", oVault);
        }
    }

    SetLocalInt(oArea, "scel_ng", nGroups);

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
