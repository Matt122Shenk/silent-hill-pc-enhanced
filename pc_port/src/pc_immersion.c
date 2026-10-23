/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * pc_immersion.c - weapon visibility immersion.
 *
 * With immersion_weapons on, the held-weapon model hides a few seconds after
 * its last use and comes back for equips, aiming, firing, reloading, and
 * radio danger (SH2R-style readiness). Only the display bit moves; inventory,
 * models and async loads are untouched, so showing is instant. Outside
 * settled gameplay the bit is left alone (cutscenes/menus own it there).
 */
#include "game.h"

#include <SDL2/SDL.h>

#include "bodyprog/bodyprog.h"
#include "bodyprog/chara/chara.h"
#include "bodyprog/events/bgm.h"
#include "bodyprog/items.h"
#include "bodyprog/math/math.h"
#include "bodyprog/player.h"
#include "bodyprog/screen/screen_data.h"
#include "bodyprog/savegame.h"
#include "bodyprog/sound/sfx_id_enum.h"
#include "bodyprog/sound/sfx_id_enum.h"
#include "pc_config.h"
#include "pc_immersion.h"
#include "sh_log.h"

extern u16 g_Player_IsAiming;
extern u16 g_Player_IsShooting;

#define IMM_EQUIP_GRACE 4.0f
#define IMM_IDLE_HIDE   6.0f

static void Immersion_Weapons(int inGame);
static void Immersion_Fear(int inGame, Uint32 now);
static void Immersion_Flicker(int inGame, Uint32 now);

static void Immersion_Weapons(int inGame)
{
    static float s_idle = 0.0f;
    static float s_grace = 0.0f;
    static int   s_lastInvId = -1000;
    static int   s_lastEquipped = -1000;
    static int   s_wasOn = 0;
    int idx, invId, eqp, want = 0;
    float dt = (float)g_DeltaTime / 4096.0f; /* Q12 seconds */
    if (dt < 0.0f) dt = 0.0f;

    if (!g_PcConfig.immersionWeapons)
    {
        if (s_wasOn)
        {
            func_8003D01C();
            s_wasOn = 0;
        }
        s_idle = 0.0f;
        s_grace = 0.0f;
        return;
    }
    s_wasOn = 1;

    if (!inGame)
        return;

    /* Track WHICH item is equipped, not its slot: every pickup re-sorts the
     * inventory (func_8004F190) and moves weaponInventoryIdx while the same
     * weapon stays in hand, which used to re-show a hidden weapon. */
    idx   = g_SysWork.playerCombat.weaponInventoryIdx;
    invId = (idx >= 0 && idx < INV_ITEM_COUNT_MAX) ? g_SavegamePtr->items[idx].id_0 : -1;
    eqp   = g_SavegamePtr->equippedWeapon;
    if (invId != s_lastInvId || eqp != s_lastEquipped)
    {
        s_lastInvId = invId;
        s_lastEquipped = eqp;
        s_grace = IMM_EQUIP_GRACE;
        s_idle = 0.0f;
    }
    if (s_grace > 0.0f)
    {
        s_grace -= dt;
        if (s_grace < 0.0f) s_grace = 0.0f;
    }

    if (g_SysWork.playerCombat.isAiming || g_Player_IsAiming ||
        g_Player_IsShooting ||
        g_SysWork.playerWork.extra.upperBodyState == PlayerUpperBodyState_Reload)
        want = 1;
    else if (s_grace > 0.0f)
        want = 1;
    else if ((g_SysWork.bgmStatusFlags & BgmStatusFlag_RadioActive) &&
             eqp != InvItemId_Unequipped)
        want = 1;

    if (want)
    {
        func_8003D01C();
        s_idle = 0.0f;
    }
    else
    {
        s_idle += dt;
        if (s_idle >= IMM_IDLE_HIDE)
            func_8003D03C();
    }
}

static unsigned int s_fearRand = 0x9E3779B9u;
static int s_fearRadioWas = 0;
static Uint32 s_fearCooldownUntil = 0;
static Uint32 s_fearNextRoll = 0;

static unsigned int FearRand(void)
{
    s_fearRand ^= s_fearRand << 13;
    s_fearRand ^= s_fearRand >> 17;
    s_fearRand ^= s_fearRand << 5;
    return s_fearRand;
}

static void Immersion_Fear(int inGame, Uint32 now)
{
    int radio;

    if (!inGame)
    {
        s_fearRadioWas = 0;
        return;
    }
    if (!g_PcConfig.immersionFear)
    {
        s_fearRadioWas = 0;
        s_fearCooldownUntil = 0;
        s_fearNextRoll = 0;
        return;
    }

    radio = (g_SysWork.bgmStatusFlags & BgmStatusFlag_RadioActive) ? 1 : 0;
    if (radio && (!s_fearRadioWas || (Sint32)(now - s_fearNextRoll) >= 0))
    {
        extern void func_8005DC1C(int sfxId, const void* pos, int vol, int type);
        s_fearNextRoll = now + 8000 + (FearRand() % 4000);
        if ((Sint32)(now - s_fearCooldownUntil) >= 0 && (FearRand() % 100) < 35)
        {
            func_8005DC1C(Sfx_HarryHeavyBreath, &g_SysWork.playerWork.player.position, 0x40, 2);
            s_fearCooldownUntil = now + 20000 + (FearRand() % 10000);
        }
    }
    if (!radio)
        s_fearNextRoll = 0;
    s_fearRadioWas = radio;
}

/* Flashlight flicker: a failing contact near live enemies that ends in a
 * short blackout. Proximity arms the roll (15%), facing the enemy pays the
 * full 45%. The burst is an irregular stutter for ~900 ms: random 25-90 ms
 * segments alternating between a near-dead dip (0..8%) and a weak-to-full
 * beam (45..100%). Then the beam dies for 500 ms, and while it is dead the
 * light button is locked (Pc_Immersion_FlashlightLocked), so the player
 * cannot just switch it back on.
 *
 * Display only: the game still treats the flashlight as on (same AI, same
 * room lighting). That is also why the blackout reads as real darkness: the
 * per-pixel dark-room dim stays applied with no cone left to cut through it.
 * Per-pixel modes only: Classic bakes light per-vertex for PSX parity and has
 * no live intensity to scale (g_PsyX_FlashlightFlicker rides the shader
 * uniform in PsyX_render.cpp). */
#define FLICK_NEAR_DIST   Q12(8.0f)
#define FLICK_FACE_COS    Q12(0.9f)
#define FLICK_ROLL_MS     8000u
#define FLICK_ROLL_JITTER 3000u
#define FLICK_BURST_MS    900u
#define FLICK_SEG_MIN_MS  25u
#define FLICK_SEG_JITTER  66u     /* segments last 25..90 ms */
#define FLICK_DIP_MAX     0.08f   /* dark segments: 0..8% */
#define FLICK_LIT_MIN     0.45f   /* lit segments: 45..100% */
#define FLICK_BLACKOUT_MS 500u
#define FLICK_COOL_MS     18000u
#define FLICK_COOL_JITTER 8000u
#define FLICK_P_NEAR      15
#define FLICK_P_FACE      45

enum { FLICK_IDLE = 0, FLICK_BURST, FLICK_BLACKOUT };

extern float g_PsyX_FlashlightFlicker;
extern int   g_PsyX_UsePerPixelFlashlight;

static unsigned int s_flickRand = 0x51ED270Bu;
static Uint32 s_flickNextRoll = 0;
static Uint32 s_flickCooldownUntil = 0;
static int    s_flickPhase = FLICK_IDLE;
static Uint32 s_flickPhaseStart = 0;
static Uint32 s_flickSegEnd = 0;
static int    s_flickSegDark = 0;

static unsigned int FlickRand(void)
{
    s_flickRand ^= s_flickRand << 13;
    s_flickRand ^= s_flickRand >> 17;
    s_flickRand ^= s_flickRand << 5;
    return s_flickRand;
}

/* Uniform 0..1. */
static float FlickFrac(void)
{
    return (float)(FlickRand() % 1000u) / 999.0f;
}

int Pc_Immersion_FlashlightLocked(void)
{
    return s_flickPhase == FLICK_BLACKOUT;
}

static void Immersion_Flicker(int inGame, Uint32 now)
{
    if (!inGame || !g_PcConfig.immersionFlicker || !g_PsyX_UsePerPixelFlashlight ||
        !g_SysWork.field_2388.isFlashlightOn_15)
    {
        if (g_PsyX_FlashlightFlicker != 1.0f)
            g_PsyX_FlashlightFlicker = 1.0f;
        s_flickPhase = FLICK_IDLE;
        s_flickNextRoll = 0;
        return;
    }

    if (s_flickPhase == FLICK_BURST)
    {
        if (now - s_flickPhaseStart >= FLICK_BURST_MS)
        {
            s_flickPhase = FLICK_BLACKOUT;
            s_flickPhaseStart = now;
            g_PsyX_FlashlightFlicker = 0.0f;
            return;
        }
        if ((Sint32)(now - s_flickSegEnd) >= 0)
        {
            s_flickSegDark = !s_flickSegDark;
            s_flickSegEnd = now + FLICK_SEG_MIN_MS + (FlickRand() % FLICK_SEG_JITTER);
            g_PsyX_FlashlightFlicker = s_flickSegDark
                ? FLICK_DIP_MAX * FlickFrac()
                : FLICK_LIT_MIN + (1.0f - FLICK_LIT_MIN) * FlickFrac();
        }
        return;
    }

    if (s_flickPhase == FLICK_BLACKOUT)
    {
        if (now - s_flickPhaseStart >= FLICK_BLACKOUT_MS)
        {
            s_flickPhase = FLICK_IDLE;
            g_PsyX_FlashlightFlicker = 1.0f;
            s_flickNextRoll = now + FLICK_ROLL_MS + (FlickRand() % FLICK_ROLL_JITTER);
        }
        else
        {
            g_PsyX_FlashlightFlicker = 0.0f;
        }
        return;
    }

    if ((Sint32)(now - s_flickNextRoll) < 0 || (Sint32)(now - s_flickCooldownUntil) < 0)
        return;
    s_flickNextRoll = now + FLICK_ROLL_MS + (FlickRand() % FLICK_ROLL_JITTER);

    {
        s_SubCharacter* player = &g_SysWork.playerWork.player;
        s32 fx = Math_Sin(player->rotation.vy);
        s32 fz = Math_Cos(player->rotation.vy);
        int near = 0;
        int faced = 0;
        s32 i;

        for (i = 0; i < (s32)ARRAY_SIZE(g_SysWork.npcs); i++)
        {
            s_SubCharacter* npc = &g_SysWork.npcs[i];
            s32 ex, ez, dist;

            if (npc->model.charaId < Chara_AirScreamer || npc->model.charaId >= Chara_LockerDeadBody)
                continue;
            if (npc->collision.state == CharaCollisionState_Ignore)
                continue;
            if (npc->health <= Q12(0.0f))
                continue;

            ex = npc->position.vx - player->position.vx;
            ez = npc->position.vz - player->position.vz;
            dist = Math_Vector2MagCalc(ex, ez);
            if (dist > FLICK_NEAR_DIST)
                continue;
            near = 1;
            if (dist <= Q12(0.5f))
            {
                faced = 1;
                break;
            }
            if ((s32)(((s64)fx * ex + (s64)fz * ez) / dist) > FLICK_FACE_COS)
            {
                faced = 1;
                break;
            }
        }

        if (near && (FlickRand() % 100) < (unsigned)(faced ? FLICK_P_FACE : FLICK_P_NEAR))
        {
            s_flickPhase = FLICK_BURST;
            s_flickPhaseStart = now;
            s_flickSegDark = 0;
            s_flickSegEnd = now; /* first tick opens on a dark segment */
            s_flickCooldownUntil = now + FLICK_COOL_MS + (FlickRand() % FLICK_COOL_JITTER);
        }
    }
}

void Pc_Immersion_Update(void)
{
    int inGame = (g_GameWork.gameState == GameState_InGame &&
                  g_SysWork.sysState   == SysState_Gameplay);
    Uint32 now = SDL_GetTicks();
    Immersion_Weapons(inGame);
    Immersion_Fear(inGame, now);
    Immersion_Flicker(inGame, now);
}
