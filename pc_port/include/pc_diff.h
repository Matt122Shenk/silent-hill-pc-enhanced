/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PC_DIFF_H
#define PC_DIFF_H

/* Difficulty accessibility (config keys diff_ammo / diff_heals / diff_enemy).
 * Ammo and heals take 0..3 presets (Low / Medium / Normal / High); enemy
 * damage takes 0..5 (plus Very High / Hell). All applied multiplicatively
 * AFTER the native calculation, so Normal (2) is byte-identical vanilla and
 * the native difficulty setting is untouched.
 *
 * Requires the engine types (s32/q19_12): include after "game.h".
 */

/* Scale a loot ammo count (percent table 15/50/100/170). Never yields 0 from
 * a positive pickup. */
s32 Pc_Diff_AmmoCount(s32 count);

/* Scale a heal amount in Q12 (percent table 25/50/100/150). */
q19_12 Pc_Diff_HealAmount(q19_12 amount);

/* Scale enemy->player damage (percent table 50/75/100/250/400/700). */
s32 Pc_Diff_EnemyDamage(s32 damageAmount);

#endif /* PC_DIFF_H */
