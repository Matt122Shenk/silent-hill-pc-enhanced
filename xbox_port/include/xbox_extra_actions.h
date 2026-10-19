#pragma once

/* Physical Xbox buttons the PC port's extra actions (quick heal, weapon cycle,
 * manual reload, quick turn) can be bound to. The values are what the
 * xb_bind_* config keys store; 0 = unbound. */
enum {
    XB_PHYS_NONE  = 0,
    XB_PHYS_L3    = 1,
    XB_PHYS_R3    = 2,
    XB_PHYS_WHITE = 3,
    XB_PHYS_BLACK = 4,
    XB_PHYS_BACK  = 5,
    XB_PHYS_UP    = 6,
    XB_PHYS_RIGHT = 7,
    XB_PHYS_DOWN  = 8,
    XB_PHYS_LEFT  = 9,
    XB_PHYS_COUNT
};

/* 1 << XB_PHYS_* for each bound button while gameplay owns the pad, else 0:
 * pad_xbox.c then sends no PSX button for those. Set by Pc_ExtraActionsUpdate. */
extern unsigned g_XbExtraMute;

unsigned Pad_XboxPhysButtons(void);
