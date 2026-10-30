// nwndgen module OnAcquireItem: advances the "scel_main" journal quest.
// Entry 2: a PC took the vault key from the boss. Entry 3 (end): a PC took the relic.

void main()
{
    object oItem = GetModuleItemAcquired();
    object oPC = GetModuleItemAcquiredBy();
    string sTag;

    if (!GetIsPC(oPC))
        return;
    sTag = GetTag(oItem);
    if (sTag == "SCEL_KEY")
        AddJournalQuestEntry("scel_main", 2, oPC);
    else if (sTag == "SCEL_RELIC")
    {
        AddJournalQuestEntry("scel_main", 3, oPC);
        SendMessageToPC(oPC, "You have recovered the relic.");
    }
}
