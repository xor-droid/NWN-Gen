// nwndgen object event handler, attached by scel_enter (SetEventScript): to a lever (ON_USED, recipe key levers), whose use opens its gate,
// and to the chest that springs an ambush group (ON_OPEN, recipe key ambush), whose opening makes the group's hidden creatures appear.
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
    if (nGroup > 0)
        FireAmbush(GetArea(oBox), nGroup, GetLastOpenedBy());
}
