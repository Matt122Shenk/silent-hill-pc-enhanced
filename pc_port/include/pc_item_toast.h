/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * pc_item_toast.h - fast item-pickup toast for takes without the animated
 * take screen (debug grants, console/rando gifts, special unlocks).
 *
 * Animated world takes keep their freeze-frame untouched; this never fires
 * for those. Same self-contained-GL discipline as pc_ra_toast.c.
 */
#ifndef PC_ITEM_TOAST_H
#define PC_ITEM_TOAST_H

void Pc_ItemToast_Show(unsigned char itemId, unsigned char itemCount);
void Pc_ItemToast_ShowConsumed(unsigned char itemId);
void Pc_ItemToast_Draw(void);
int  Pc_ItemToast_IsCommon(unsigned char itemId);

/* One-line display name for an item: English, or Spanish when the PC
 * language is Spanish. Writes into buf and returns it ("" if unnamed). */
const char* Pc_ItemToast_Name(unsigned char itemId, char* buf, int bufsz);

#endif /* PC_ITEM_TOAST_H */
