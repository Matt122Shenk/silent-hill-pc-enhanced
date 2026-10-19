/*
 * xbox_extra_actions.c - the PC port's extra actions on the Xbox pad: quick
 * heal, weapon cycle, manual reload and quick turn.
 *
 * pc_combat.c is excluded here for its SDL key/pad binds, but the actions
 * themselves are game logic; the bodies below are pc_combat.c's, unchanged
 * apart from the toast hook. Each action is bound to one physical button by
 * an xb_bind_* key (Xbox Options page 3), unbound by default. While gameplay
 * owns the pad a bound button sends only its action: pad_xbox.c drops its PSX
 * button through g_XbExtraMute, the way bw_quick_save takes White/Black.
 */
#include "game.h"
#include "bodyprog/bodyprog.h"
#include "bodyprog/screen/screen_data.h"
#include "bodyprog/player.h"
#include "bodyprog/item_screens.h"
#include "bodyprog/sound/sound_system.h"
#include "pc_combat.h"
#include "pc_config.h"
#include "sh_log.h"
#include "xbox_extra_actions.h"
#include <stdio.h>

extern void (*g_ShOverlayToastLine)(const char* line); /* dbg_overlay_xbox.c */

int      g_PcQuickTurnRequest = 0;
int      g_PcReloadRequest    = 0;
s32      g_PcHealFlashTimer   = 0;
unsigned g_XbExtraMute        = 0;

static q19_12 s_reloadLatchT = 0;

/* Weapon/ammo conditions for a manual reload, evaluated at PRESS time only. */
static bool Pc_ManualReloadConditions(void)
{
    extern u8 g_Items_GunsMaxLoadAmmo[36]; /* clip capacity by weaponAttack (item_screens_3.c) */
    return g_SysWork.playerCombat.weaponAttack >= WEAPON_ATTACK(EquippedWeaponId_Handgun, AttackInputType_Tap) &&
           g_SysWork.playerCombat.totalWeaponAmmo != 0 &&
           INV_ITEM_GROUP(g_SavegamePtr->equippedWeapon) == InvItemGroup_GunWeapons &&
           /* Don't reload an already-full clip (PSX only auto-reloaded on empty):
            * without this a press on a full clip plays a pointless reload+SFX for
            * zero rounds, and a stray 2nd controller edge right after a reload tops
            * the clip re-triggers a second reload — the "double up". Same index the
            * reload itself uses: Items_AmmoReloadCalculation(gunIdx = weaponAttack). */
           g_SysWork.playerCombat.currentWeaponAmmo <
               g_Items_GunsMaxLoadAmmo[g_SysWork.playerCombat.weaponAttack] &&
           /* Not already mid-reload — rejecting the press outright (rather than
            * latching it) is what stops a second press queueing a double reload. */
           g_SysWork.playerWork.extra.upperBodyState != PlayerUpperBodyState_Reload;
}


bool PC_PlayerManualReloadRequested(void)
{
    return g_PcReloadRequest != 0;
}

extern void GameFs_WeaponInfoUpdate(void); /* player.h — loads the equipped weapon model/anim */

/* Only the LIVE slots count. The game itself (Player_ItemRemove, the inventory
 * screen) never looks past inventorySlotCount; slots beyond it keep whatever
 * a previous save or session left there. Scanning all INV_ITEM_COUNT_MAX
 * found those ghosts: quick heal "used" a drink that was not in the
 * inventory (heal + green flash + toast, nothing removed) and weapon cycling
 * could pick a weapon that was no longer owned. */
static s32 Pc_FindItemSlot(u8 id)
{
    s32 i;
    for (i = 0; i < g_SavegamePtr->inventorySlotCount && i < INV_ITEM_COUNT_MAX; i++)
        if (g_SavegamePtr->items[i].id_0 == id) return i;
    return NO_VALUE;
}

/* Safe to swap weapon / heal outside the menu: in gameplay, control enabled, and
 * the upper body idle/moving (not aiming/mid-swing/reloading — GameFs_WeaponInfoUpdate
 * does a blocking model load, so it must not fire mid-combat). */
static int Pc_ActionSafe(void)
{
    return g_GameWork.gameState == GameState_InGame &&
           g_SysWork.sysState   == SysState_Gameplay &&
           !g_Player_DisableControl &&
           g_SysWork.playerWork.extra.upperBodyState <= PlayerUpperBodyState_SidestepRightStumble;
}

/* Equip one owned weapon — mirrors the load-time re-equip block
 * (player_control.c:10312-10343) + the model reload. */
static void Pc_EquipWeapon(u8 invItemId, s32 slot)
{
    s32 groupId = INV_ITEM_GROUP(invItemId);

    /* Silence the outgoing weapon, the way opening the inventory does.
     *
     * A quick switch is the whole of "open inventory, pick a weapon, close", and
     * the OPEN half is what stops weapon audio: item_screens_3.c calls
     * func_8004C564(0, NO_VALUE), whose NO_VALUE case runs func_8008B398 to take
     * all four weapon sound channels to zero. Cycling with the bound key skipped
     * it, so the chainsaw kept running audibly until the player opened the
     * inventory by hand -- and the same would hold for anything else holding a
     * weapon channel. Calling the game's own entry point rather than stopping
     * SFX ids by hand keeps the fade state machine (D_800C3960..63) consistent
     * with what the rest of the weapon-sound code expects to find. */
    func_8004C564(0, NO_VALUE);
    g_Inventory_EquippedItem                  = invItemId;
    g_SavegamePtr->equippedWeapon             = invItemId;
    g_SysWork.playerCombat.weaponAttack       = invItemId + InvItemId_KitchenKnife; /* +0x80; s8 truncates to EquippedWeaponId */
    g_SysWork.playerCombat.weaponInventoryIdx = slot;
    g_SysWork.playerCombat.currentWeaponAmmo  = g_SavegamePtr->items[slot].count_1;
    if (groupId == InvItemGroup_GunWeapons) {
        s32 a = Pc_FindItemSlot(invItemId + InvItemId_HealthDrink); /* ammo id = weapon + 32 */
        g_SysWork.playerCombat.totalWeaponAmmo = (a == NO_VALUE) ? 0 : (s8)g_SavegamePtr->items[a].count_1;
    } else {
        g_SysWork.playerCombat.totalWeaponAmmo = 0;
    }
    GameFs_WeaponInfoUpdate();

    /* Swap the VISIBLE held-weapon model too — GameFs_WeaponInfoUpdate only reloads
     * the info/anim/stat tables, NOT the geometry Harry holds (g_WorldGfxWork.heldItem).
     * Mirror the inventory-exit sequence: reattach the grip bones for the new weapon
     * class, point heldItem->itemId at the new weapon + queue its async model/texture
     * read, and un-hide it. WorldGfx_HeldItemDraw polls the async load every frame and
     * binds the new model when the read lands (a few frames later; no blocking wait, so
     * no stutter — a brief empty hand until it binds). */
    Gfx_PlayerHeldItemAttach(g_SysWork.playerCombat.weaponAttack);
    WorldGfx_PlayerPrevHeldItem(&g_SysWork.playerCombat);
    func_8003D01C();

    /* Shut the gas weapons down when switching off one.
     *
     * The chainsaw and the rock drill keep running state while equipped --
     * gasWeaponPowerTimer, which player_control counts down every frame, and
     * field_44.field_0 -- and that is what feeds the drill's smoke. The
     * inventory clears both on its way out (Inventory_ExitAnimEquippedItemUpdate,
     * item_screens_1.c:53), so switching there stops the effect; cycling with the
     * bound key never ran that path, and the smoke carried on until the player
     * opened the inventory and switched by hand.
     *
     * Same two conditions as the original, mirroring the inventory's EXIT half.
     * gasWeaponPowerTimer is already zeroed by the silence above (its open half
     * does it unconditionally, exactly as the inventory would); what this uniquely
     * clears is field_44.field_0, which only the exit path touches. Reads
     * g_Player_WeaponAttack for the OUTGOING weapon exactly as the inventory
     * does -- it lags the combat struct, which is why it still holds the old one
     * here. */
    {
        u8 prevId = WEAPON_ATTACK_ID_GET(g_Player_WeaponAttack);

        if ((prevId == EquippedWeaponId_Chainsaw &&
             g_SysWork.playerCombat.weaponAttack != prevId) ||
            (prevId == EquippedWeaponId_RockDrill &&
             g_SysWork.playerCombat.weaponAttack != WEAPON_ATTACK(prevId, AttackInputType_Tap)))
        {
            g_SysWork.playerWork.player.field_44.field_0 = 0;
            g_SysWork.playerWork.player.properties.player.gasWeaponPowerTimer = Q12(0.0f);
        }
    }
}

/* Cycle to the next OWNED weapon in acquisition/enum order (wraps). */
void Pc_CycleWeapons(void)
{
    static const u8 order[] = {
        InvItemId_KitchenKnife, InvItemId_SteelPipe, InvItemId_RockDrill,
        InvItemId_Hammer, InvItemId_Chainsaw, InvItemId_Katana, InvItemId_Axe,
        InvItemId_Handgun, InvItemId_HuntingRifle, InvItemId_Shotgun, InvItemId_HyperBlaster,
    };
    s32 n = (s32)(sizeof(order) / sizeof(order[0]));
    s32 cur = -1, i, k;
    for (i = 0; i < n; i++)
        if (order[i] == g_SavegamePtr->equippedWeapon) { cur = i; break; }
    for (k = 1; k <= n; k++) {
        s32 idx = cur + k;
        idx %= n; if (idx < 0) idx += n;
        s32 slot = Pc_FindItemSlot(order[idx]);
        if (slot != NO_VALUE) { Pc_EquipWeapon(order[idx], slot); return; }
    }
}

/* Quantity in the first slot holding this id, 0 if absent.
 *
 * Pc_FindItemSlot matches on id_0 alone, which is right for weapons (a knife
 * legitimately carries count 0) but wrong here: a healing slot whose count has
 * been spent keeps its id until the slot is reused, so quick heal read it as
 * owned, healed off it, and flashed green with nothing in the inventory. */
static s32 Pc_HealItemCount(u8 id)
{
    s32 i;
    for (i = 0; i < g_SavegamePtr->inventorySlotCount && i < INV_ITEM_COUNT_MAX; i++)
        if (g_SavegamePtr->items[i].id_0 == id)
            return g_SavegamePtr->items[i].count_1;
    return 0;
}

/* Auto-use the most sensible OWNED healing item for the current health. */
void Pc_QuickHeal(void)
{
    q19_12 health  = g_SysWork.playerWork.player.health;
    s32    hp      = health >> Q12_SHIFT;
    s32    deficit = 100 - hp;
    s32    drink, kit, amp;
    u8     chosen  = InvItemId_Empty;

    if (hp <= 0 || hp >= 100) return; /* dead or already full */

    drink = (Pc_HealItemCount(InvItemId_HealthDrink) > 0) ? Pc_FindItemSlot(InvItemId_HealthDrink) : NO_VALUE;
    kit   = (Pc_HealItemCount(InvItemId_FirstAidKit) > 0) ? Pc_FindItemSlot(InvItemId_FirstAidKit) : NO_VALUE;
    amp   = (Pc_HealItemCount(InvItemId_Ampoule)     > 0) ? Pc_FindItemSlot(InvItemId_Ampoule)     : NO_VALUE;

    if (hp < 10) {
        /* critical: strongest available (ampoule also refills the regen buffer) */
        if      (amp   != NO_VALUE) chosen = InvItemId_Ampoule;
        else if (kit   != NO_VALUE) chosen = InvItemId_FirstAidKit;
        else if (drink != NO_VALUE) chosen = InvItemId_HealthDrink;
    } else if (deficit <= 40) {
        /* light: a drink fills it with no waste; reserve stronger items */
        if      (drink != NO_VALUE) chosen = InvItemId_HealthDrink;
        else if (kit   != NO_VALUE) chosen = InvItemId_FirstAidKit;
        else if (amp   != NO_VALUE) chosen = InvItemId_Ampoule;
    } else {
        /* moderate: first-aid is the tight fit; keep the ampoule for emergencies */
        if      (kit   != NO_VALUE) chosen = InvItemId_FirstAidKit;
        else if (drink != NO_VALUE) chosen = InvItemId_HealthDrink;
        else if (amp   != NO_VALUE) chosen = InvItemId_Ampoule;
    }
    /* Nothing owned. This compared a u8 against the enum constant: chosen holds
     * (u8)InvItemId_Empty = 255 while InvItemId_Empty itself is NO_VALUE (-1),
     * so the test never matched. Player_ItemRemove(255) then found the first
     * EMPTY slot (id 255), "removed" one from its count (0 wrapping to 255 and
     * counting down), reported success, and Harry healed off nothing -- as
     * often as the key was pressed, with the toast falling through to
     * "Ampoule". Only the three healing items may ever be spent here. */
    if (chosen != InvItemId_HealthDrink && chosen != InvItemId_FirstAidKit && chosen != InvItemId_Ampoule)
        return;

    /* [QUICKHEAL] One line per use: which slot paid for it and the whole live
     * inventory. A heal "with no healing items" (2026-09-17, a New Game warped
     * straight to map1_s05) could not be traced from the code: this path only
     * spends an item the game's own Player_ItemRemove finds in a live slot. */
    {
        char inv[256];
        int  n = 0, i;
        for (i = 0; i < g_SavegamePtr->inventorySlotCount && i < INV_ITEM_COUNT_MAX && n < (int)sizeof(inv) - 12; i++)
            n += snprintf(inv + n, sizeof(inv) - (size_t)n, " %d:%d", (int)g_SavegamePtr->items[i].id_0,
                          (int)g_SavegamePtr->items[i].count_1);
        inv[n] = '\0';
        SH_DBG("[QUICKHEAL] hp=%d chose item %d (slot %d) | slots=%d:%s", (int)hp, (int)chosen,
               (int)Pc_FindItemSlot(chosen), (int)g_SavegamePtr->inventorySlotCount, inv);
    }

    /* Spend the item FIRST, through the game's own removal, and heal only if it
     * really came out of the inventory. Healing before removing is how a ghost
     * slot produced a full heal with feedback and no inventory change. */
    if (!Player_ItemRemove(chosen, 1)) {
        SH_DBG("[QUICKHEAL] item %d looked owned but Player_ItemRemove found none -- no heal", (int)chosen);
        return;
    }

    switch (chosen) {
        case InvItemId_FirstAidKit: health += Q12(80.0f);  break;
        case InvItemId_HealthDrink: health += Q12(40.0f);  break;
        case InvItemId_Ampoule:     health += Q12(100.0f); g_SavegamePtr->healthSaturation = Q12(300.0f); break;
    }
    g_SysWork.playerWork.player.health = CLAMP(health, Q12(0.0f), Q12(100.0f));
    Sd_PlaySfx(Sfx_Unk1325, -0x40, 0x40); /* same feedback SFX as the inventory heal */
    g_PcHealFlashTimer = Q12(0.35f); /* brief green heal pulse (drawn by Pc_HealFlashUpdate) */

    /* Report what was spent and what is left, since quick heal picks the item for
     * you and the inventory is not open to see the result. Counts are read AFTER
     * the removal so the line shows the remaining stock. */
    {
        const char* name = (chosen == InvItemId_FirstAidKit) ? "First Aid Kit"
                         : (chosen == InvItemId_HealthDrink) ? "Health Drink"
                                                             : "Ampoule";
        char line[96];
        snprintf(line, sizeof(line), "Used %s  -  Kits %d, Drinks %d, Ampoules %d",
                 name,
                 (int)Pc_HealItemCount(InvItemId_FirstAidKit),
                 (int)Pc_HealItemCount(InvItemId_HealthDrink),
                 (int)Pc_HealItemCount(InvItemId_Ampoule));
        if (g_ShOverlayToastLine)
            g_ShOverlayToastLine(line);
    }
}


/* Per-frame draw for the Quick Heal green pulse: an additive full-screen green TILE
 * that eases out over ~0.35s. Mirrors the screen-fade full-screen-tile path (static
 * double-buffered prims into OT2 bucket 4). Called from game_main.c after the fade
 * update. Self-gates on the timer, so it is byte-identical output when not healing. */
void Pc_HealFlashUpdate(void)
{
    static TILE     s_tile[2];
    static DR_TPAGE s_tp[2];
    int buf;
    s32 g;

    if (g_PcHealFlashTimer <= 0)
        return;

    g_PcHealFlashTimer -= g_DeltaTime; /* fps-independent countdown (Q12 seconds) */
    if (g_PcHealFlashTimer < 0)
        g_PcHealFlashTimer = 0;

    buf = g_ActiveBufferIdx;
    g   = (g_PcHealFlashTimer * 96) / Q12(0.35f); /* peak additive green ~96, eases to 0 */

    setTile(&s_tile[buf]);
    setSemiTrans(&s_tile[buf], 1);
    setRGB0(&s_tile[buf], 0, (u8)g, 0);
    setWH(&s_tile[buf], SCREEN_WIDTH * 4, SCREEN_HEIGHT * 2);
    setXY0(&s_tile[buf], -SCREEN_WIDTH, -SCREEN_HEIGHT); /* cover the full Hor+ width */

    setDrawTPage(&s_tp[buf], 0, 1, getTPageN(0, 1, 0, 0)); /* abr=1 = additive */

    AddPrim(&g_OtTags0[buf][4], &s_tile[buf]);
    AddPrim(&g_OtTags0[buf][4], &s_tp[buf]);
}


enum { XA_HEAL, XA_CYCLE, XA_RELOAD, XA_TURN, XA_COUNT };

static int* const XA_FIELDS[XA_COUNT] = {
    &g_PcConfig.xbBindQuickHeal, &g_PcConfig.xbBindCycleWeapon,
    &g_PcConfig.xbBindReload,    &g_PcConfig.xbBindQuickTurn,
};
static const char* const XA_KEYS[XA_COUNT] = {
    "xb_bind_quick_heal", "xb_bind_cycle_weapon", "xb_bind_reload", "xb_bind_quick_turn",
};

/* One button, one action: when a row takes a button another row holds, the
 * other row lets go of it (and the cfg is updated to match). */
static void Xa_ResolveBinds(void)
{
    static int s_prev[XA_COUNT];
    int        i, j;

    for (i = 0; i < XA_COUNT; i++)
    {
        int v = *XA_FIELDS[i];

        if (v < 0 || v >= XB_PHYS_COUNT)
            *XA_FIELDS[i] = v = XB_PHYS_NONE;
        if (v == s_prev[i])
            continue;
        s_prev[i] = v;
        if (v == XB_PHYS_NONE)
            continue;
        for (j = 0; j < XA_COUNT; j++)
        {
            if (j != i && *XA_FIELDS[j] == v)
            {
                *XA_FIELDS[j] = XB_PHYS_NONE;
                s_prev[j]     = XB_PHYS_NONE;
                PcConfig_SaveKeyValue(XA_KEYS[j], "0");
            }
        }
    }
}

/* Bound button for action `a` as a mask, or 0. White/Black belong to quick
 * save/load while bw_quick_save is on, so a bind on them waits until it is off. */
static unsigned Xa_Mask(int a)
{
    int v = *XA_FIELDS[a];

    if (v == XB_PHYS_NONE)
        return 0;
    if (g_PcConfig.bwQuickSave && (v == XB_PHYS_WHITE || v == XB_PHYS_BLACK))
        return 0;
    return 1u << v;
}

/* Per-frame dispatch, called from game_main.c before the player FSM. Edges are
 * sampled every frame so the previous state never goes stale; the actions only
 * fire in gameplay. Same gating as pc_combat.c's Pc_ExtraActionsUpdate. */
void Pc_ExtraActionsUpdate(void)
{
    static unsigned s_prevPhys;
    unsigned        phys, edge, bound = 0;
    int             a, inGameplay;

    Xa_ResolveBinds();
    for (a = 0; a < XA_COUNT; a++)
        bound |= Xa_Mask(a);

    phys       = Pad_XboxPhysButtons();
    edge       = phys & ~s_prevPhys;
    s_prevPhys = phys;

    inGameplay = g_GameWork.gameState == GameState_InGame &&
                 g_SysWork.sysState   == SysState_Gameplay;
    g_XbExtraMute = inGameplay ? bound : 0;

    /* Manual reload: latched for 0.30 s so a press on a frame the combat FSM
     * cannot act on (recoil, the frame after a reload) is not lost. */
    if ((edge & Xa_Mask(XA_RELOAD)) && Pc_ManualReloadConditions())
    {
        g_PcReloadRequest = 1;
        s_reloadLatchT    = Q12(0.30f);
    }
    else if (g_PcReloadRequest)
    {
        s_reloadLatchT -= g_DeltaTime;
        if (s_reloadLatchT <= 0)
            g_PcReloadRequest = 0;
    }
    if (!inGameplay || g_Player_DisableControl)
    {
        g_PcReloadRequest = 0;
        s_reloadLatchT    = 0;
    }

    if (!Pc_ActionSafe())
    {
        g_PcQuickTurnRequest = 0;
        return;
    }
    if (edge & Xa_Mask(XA_CYCLE))
        Pc_CycleWeapons();
    if (edge & Xa_Mask(XA_HEAL))
        Pc_QuickHeal();
    /* One-frame request, never latched (pc_combat.c). */
    g_PcQuickTurnRequest = (edge & Xa_Mask(XA_TURN)) ? 1 : 0;
}
