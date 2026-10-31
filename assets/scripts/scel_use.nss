// nwndgen object event handler, attached by scel_enter (SetEventScript): to a lever (ON_USED, recipe key levers), whose use opens its gate,
// to the chest that springs an ambush group (ON_OPEN, recipe key ambush), whose opening makes the group's hidden creatures appear,
// and to the vault (ON_OPEN, recipe key reactions), whose opening brings reinforcements. The stock OnOpen script it replaced runs first.
#include "scel_inc"

void main()
{
    object oBox = OBJECT_SELF;
    string sGate = GetLocalString(oBox, "scel_gate");
    int nGroup = GetLocalInt(oBox, "scel_amb");
    if (sGate != "")
    {
        PullLever(oBox, GetLastUsedBy());
        return;
    }
    string sOld = GetLocalString(oBox, "scel_old");
    if (sOld != "")
        ExecuteScript(sOld, oBox); // the stock OnOpen script this one replaced (usually the random treasure)
    if (nGroup > 0)
        FireAmbush(GetArea(oBox), nGroup, GetLastOpenedBy());
    if (GetLocalInt(oBox, "scel_react"))
        VaultReaction(oBox, GetLastOpenedBy());
}
