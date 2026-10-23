/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Difficulty accessibility — see pc_diff.h. All scales apply after the native
 * math, so the Normal preset (index 2) is a no-op by construction. */
#include "game.h"

#include "pc_config.h"
#include "pc_diff.h"

static int Pc_Diff_ClampPreset(int v, int max)
{
    if (v < 0) return 0;
    if (v > max) return max;
    return v;
}

static const int s_ammoPct[4]  = { 15, 50, 100, 170 };
static const int s_healPct[4]  = { 25, 50, 100, 150 };
static const int s_enemyPct[6] = { 50, 75, 100, 250, 400, 700 };

s32 Pc_Diff_AmmoCount(s32 count)
{
    int pct = s_ammoPct[Pc_Diff_ClampPreset(g_PcConfig.diffAmmo, 3)];
    s32 v;

    if (pct == 100 || count <= 0)
        return count;
    v = (s32)(((s64)count * pct) / 100);
    return (v < 1) ? 1 : v;
}

q19_12 Pc_Diff_HealAmount(q19_12 amount)
{
    int pct = s_healPct[Pc_Diff_ClampPreset(g_PcConfig.diffHeals, 3)];

    if (pct == 100 || amount <= 0)
        return amount;
    return (q19_12)(((s64)amount * pct) / 100);
}

s32 Pc_Diff_EnemyDamage(s32 damageAmount)
{
    int pct = s_enemyPct[Pc_Diff_ClampPreset(g_PcConfig.diffEnemyDmg, 5)];

    if (pct == 100 || damageAmount <= 0)
        return damageAmount;
    return (s32)(((s64)damageAmount * pct) / 100);
}
