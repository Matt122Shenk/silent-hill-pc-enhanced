/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Camera kick immersion — see pc_shake.h. */
#include "game.h"

#include "bodyprog/math/math.h"
#include "bodyprog/screen/screen_data.h"

#include "pc_config.h"
#include "pc_shake.h"

#define SHAKE_MAX      Q12(0.30f) /* offset at full trauma (~30cm) */
#define SHAKE_DECAY    1.4f /* trauma units per second */
#define SHAKE_HURT_ADD 0.55f
#define SHAKE_FIRE_ADD 0.40f
#define SHAKE_DEALT_ADD 0.30f

static float s_trauma = 0.0f;
static s32   s_phase1 = 0; /* Q12 angles, 4096 = full circle */
static s32   s_phase2 = 0;
static s32   s_lastTick = -1;
static s32   s_lastOX = 0, s_lastOY = 0, s_lastOZ = 0;

static int Pc_Shake_Live(void)
{
    return g_GameWork.gameState == GameState_InGame &&
           g_SysWork.sysState == SysState_Gameplay;
}

static void Pc_Shake_Add(float base, int inten)
{
    float k = (float)(inten < 0 ? 0 : (inten > 200 ? 200 : inten)) / 100.0f;
    s_trauma += base * k;
    if (s_trauma > 1.0f)
        s_trauma = 1.0f;
}

void Pc_ShakeKickHurt(void)
{
    if (!g_PcConfig.shakeHurt || !Pc_Shake_Live())
        return;
    Pc_Shake_Add(SHAKE_HURT_ADD, g_PcConfig.shakeHurtInt);
}

void Pc_ShakeKickFire(void)
{
    if (!g_PcConfig.shakeFire || !Pc_Shake_Live())
        return;
    Pc_Shake_Add(SHAKE_FIRE_ADD, g_PcConfig.shakeFireInt);
}

void Pc_ShakeKickDealt(void)
{
    if (!g_PcConfig.shakeDealt || !Pc_Shake_Live())
        return;
    Pc_Shake_Add(SHAKE_DEALT_ADD, g_PcConfig.shakeDealtInt);
}

void Pc_Shake_Offsets(s32* ox, s32* oy, s32* oz)
{
    float tra, mag;
    s32   sx, sy;

    /* Both camera paths (classic vc + TPS orbit) resolve offsets through
     * here; advance decay/phase once per MainLoop tick so alt cameras don't
     * run the envelope twice as fast. Repeat calls within the tick reuse the
     * resolved values. */
    if (s_lastTick == g_TickCount)
    {
        *ox = s_lastOX; *oy = s_lastOY; *oz = s_lastOZ;
        return;
    }
    s_lastTick = g_TickCount;

    if (s_trauma > 0.0f)
    {
        float dt = (float)g_DeltaTime / 4096.0f;
        if (dt < 0.0f) dt = 0.0f;
        if (dt > 0.1f) dt = 0.1f;
        s_trauma -= dt * (float)SHAKE_DECAY;
        if (s_trauma < 0.0f)
            s_trauma = 0.0f;

        /* Incommensurate rates so the path never visibly loops. */
        s_phase1 += (s32)(((s64)g_DeltaTime * (s64)Q12(6.3f)) >> 12);
        s_phase2 += (s32)(((s64)g_DeltaTime * (s64)Q12(9.1f)) >> 12);
        while (s_phase1 >= 4096) s_phase1 -= 4096;
        while (s_phase2 >= 4096) s_phase2 -= 4096;
    }

    tra = s_trauma * s_trauma;
    if (tra <= 0.0f)
    {
        *ox = 0; *oy = 0; *oz = 0;
        s_lastOX = 0; s_lastOY = 0; s_lastOZ = 0;
        return;
    }

    mag = tra * (float)SHAKE_MAX / 4096.0f;
    sx  = Math_Sin(s_phase1);
    sy  = Math_Cos(s_phase2);
    *ox = (s32)(sx * mag);
    *oy = (s32)(sy * mag * 0.7f);
    *oz = (s32)((sx + sy) * 0.5f * mag);
    s_lastOX = *ox; s_lastOY = *oy; s_lastOZ = *oz;
}
