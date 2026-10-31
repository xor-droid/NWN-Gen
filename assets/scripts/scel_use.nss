// nwndgen object event handler. Attached by scel_enter (SetEventScript) to the chest that springs an ambush group
// (recipe key ambush): opening it makes the group's hidden creatures appear.
#include "scel_inc"

void main()
{
    object oBox = OBJECT_SELF;
    int nGroup = GetLocalInt(oBox, "scel_amb");
    if (nGroup > 0)
        FireAmbush(GetArea(oBox), nGroup, GetLastOpenedBy());
}
