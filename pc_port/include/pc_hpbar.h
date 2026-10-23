/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PC_HPBAR_H
#define PC_HPBAR_H

/* Red HP bars over live enemies (PC-only, config key `enemy_hp_bars`).
 *
 * One cylindrical-billboard bar per damageable alive NPC within range, drawn as two
 * flat opaque POLY_G4 prims (dark backdrop + red fill scaled by the latched
 * fraction) into the world OT. Opaque (not semi-transparent) so walls
 * depth-test correctly; a line-of-sight raycast additionally skips enemies
 * behind geometry. No TIM art needed.
 *
 * Enemy max HP is latched per NPC slot on first sight (the engine stores no
 * max), cleared when the slot empties — the same pattern as the randomizer
 * health scale. Bars persist until map change (cleared in Pc_HpBarsReset).
 *
 * Requires the engine types (VECTOR3/GsOT): include after "game.h".
 */

/* Project + emit all live bars into the world ordering table. Call once per
 * frame during world rendering (after Pc_DecalsDraw, same OT). */
void Pc_HpBarsDraw(GsOT* ot);

/* Clear the latched maxima. Call where Pc_DecalsReset runs (map init). */
void Pc_HpBarsReset(void);

#endif /* PC_HPBAR_H */
