/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Player-kill watcher — see pc_kill.h. */
#include "game.h"

#include <SDL.h>

#include "bodyprog/chara/chara.h"

#include "pc_config.h"
#include "pc_kill.h"

#define KILL_HIT_MS   600u
#define KILL_FLASH_MS 750u

static s32    s_prevHp[NPC_COUNT_MAX];
static int    s_hpInit = 0;
static Uint32 s_hitAt = 0;
static Uint32 s_flashAt = 0;

/* Last-blow attribution: player melee (tap/hold/multitap) and gun taps.
 * Enemy attack ids (31/37/44+) excluded. */
static int Pc_Kill_PlayerCaused(s32 ar)
{
    return (ar >= 0 && ar <= 29) || (ar >= 32 && ar <= 35);
}

void Pc_Kill_Update(void)
{
    int   i;
    Uint32 now = SDL_GetTicks();
    int   live = (g_GameWork.gameState == GameState_InGame &&
                  g_SysWork.sysState == SysState_Gameplay);

    if (!s_hpInit)
    {
        for (i = 0; i < NPC_COUNT_MAX; i++)
            s_prevHp[i] = -1;
        s_hpInit = 1;
    }
    if (!live)
        return;

    for (i = 0; i < NPC_COUNT_MAX; i++)
    {
        s_SubCharacter* npc = &g_SysWork.npcs[i];
        s32 h;

        if (npc->model.charaId == Chara_None)
        {
            s_prevHp[i] = -1;
            continue;
        }
        if (npc->model.charaId < Chara_AirScreamer || npc->model.charaId >= Chara_LockerDeadBody)
            continue;
        if (npc->collision.state == CharaCollisionState_Ignore)
            continue;

        h = npc->health;
        if (s_prevHp[i] < 0)
        {
            s_prevHp[i] = h;
            continue;
        }
        if (s_prevHp[i] > 0 && h <= 0 && Pc_Kill_PlayerCaused(npc->attackReceived))
        {
            s_hitAt = now;
            s_flashAt = now;
        }
        s_prevHp[i] = h;
    }
}

int Pc_Kill_Hitmarker(void)
{
    Uint32 now = SDL_GetTicks();
    if (!g_PcConfig.hitmarker || s_hitAt == 0)
        return 0;
    return (now - s_hitAt) < KILL_HIT_MS ? 1 : 0;
}

float Pc_Kill_FlashLevel(void)
{
    Uint32 now = SDL_GetTicks();
    Uint32 t;
    if (!g_PcConfig.killFlash || s_flashAt == 0)
        return 0.0f;
    t = now - s_flashAt;
    if (t >= KILL_FLASH_MS)
        return 0.0f;
    return 1.0f - (float)t / (float)KILL_FLASH_MS;
}

int Pc_Kill_FlashMode(void)
{
    return (g_PcConfig.killFlashMode == 1) ? 1 : 0;
}
