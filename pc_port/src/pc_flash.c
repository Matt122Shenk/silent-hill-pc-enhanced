/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Fullscreen flash envelopes — see pc_flash.h. */
#include "game.h"

#include "bodyprog/math/math.h"
#include "bodyprog/screen/screen_data.h"

#include "pc_config.h"
#include "pc_flash.h"

#define FLASH_HEAL_SECS  Q12(0.35f)
#define FLASH_HURT_SECS  Q12(0.60f)
#define FLASH_LOW_HP     20
#define FLASH_LOW_PERIOD Q12(1.6f)

extern s32 g_PcHealFlashTimer; /* armed by quick heal (pc_combat.c) */
extern int g_PsxCutsceneActive;
extern int g_PsxPresentLastFrame;

static s32   s_hurtTimer = 0;
static s32   s_lowPhase = 0;

static int Pc_Flash_Live(void)
{
    return g_GameWork.gameState == GameState_InGame &&
           g_SysWork.sysState == SysState_Gameplay;
}

void Pc_Flash_Update(void)
{
    if (g_PcHealFlashTimer > 0)
    {
        g_PcHealFlashTimer -= g_DeltaTime;
        if (g_PcHealFlashTimer < 0)
            g_PcHealFlashTimer = 0;
    }
    if (s_hurtTimer > 0)
    {
        s_hurtTimer -= g_DeltaTime;
        if (s_hurtTimer < 0)
            s_hurtTimer = 0;
    }
    s_lowPhase += g_DeltaTime;
    while (s_lowPhase >= FLASH_LOW_PERIOD)
        s_lowPhase -= FLASH_LOW_PERIOD;
}

void Pc_Flash_KickHurt(void)
{
    if (!g_PcConfig.hurtFlash || !Pc_Flash_Live())
        return;
    s_hurtTimer = FLASH_HURT_SECS;
}

float Pc_Flash_Heal(void)
{
    float k;
    if (g_PcHealFlashTimer <= 0 || g_PcConfig.healFlash <= 0)
        return 0.0f;
    k = (float)g_PcHealFlashTimer / (float)FLASH_HEAL_SECS;
    if (k > 1.0f) k = 1.0f;
    return k * ((float)g_PcConfig.healFlash / 100.0f);
}

float Pc_Flash_Low(void)
{
    s32 hp, pulse, strength;
    float k;
    int op;
    if (!g_PcConfig.lowHealthGlow || !Pc_Flash_Live())
        return 0.0f;
    if (g_PsxCutsceneActive || g_PsxPresentLastFrame)
        return 0.0f;
    hp = g_SysWork.playerWork.player.health >> Q12_SHIFT;
    if (hp <= 0 || hp >= FLASH_LOW_HP)
        return 0.0f;
    op = (g_PcConfig.lowHealthOp < 0) ? 0 : ((g_PcConfig.lowHealthOp > 100) ? 100 : g_PcConfig.lowHealthOp);
    if (op <= 0)
        return 0.0f;
    pulse    = (Math_Sin((s32)(((s64)s_lowPhase * 4096) / FLASH_LOW_PERIOD)) + Q12(1.0f)) >> 1;
    strength = ((FLASH_LOW_HP - hp) * Q12(1.0f)) / FLASH_LOW_HP;
    /* Half strength at 19 hp, full at the brink — same curve as the old OT2 glow. */
    k = (0.5f + ((float)strength / 4096.0f) * 0.5f) * ((float)pulse / 4096.0f);
    return k * ((float)op / 100.0f);
}

float Pc_Flash_Hurt(void)
{
    float k;
    int op;
    if (!g_PcConfig.hurtFlash || s_hurtTimer <= 0)
        return 0.0f;
    op = (g_PcConfig.hurtFlashOp < 0) ? 0 : ((g_PcConfig.hurtFlashOp > 100) ? 100 : g_PcConfig.hurtFlashOp);
    if (op <= 0)
        return 0.0f;
    k = (float)s_hurtTimer / (float)FLASH_HURT_SECS;
    if (k > 1.0f) k = 1.0f;
    return k * ((float)op / 100.0f);
}

int Pc_Flash_Active(void)
{
    return Pc_Flash_Heal() > 0.0f || Pc_Flash_Low() > 0.0f || Pc_Flash_Hurt() > 0.0f;
}
