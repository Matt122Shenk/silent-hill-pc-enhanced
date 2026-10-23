/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PC_SHAKE_H
#define PC_SHAKE_H

/* Camera kick immersion (config keys shake_hurt / shake_fire / shake_dealt
 * plus shake_*_int strengths 0..200).
 *
 * Trauma 0..1 with per-frame decay; each event adds its base kick scaled by
 * its intensity. Pc_Shake_Offsets resolves the current world-space offset
 * (trauma squared times a max amplitude, two incommensurate Q12 sines) and
 * also samples the fire trigger edge, so vcExecCamera is the only call site.
 * All off by default.
 */

/* Trauma kicks. Each reads its own flag + intensity. Fire is kicked by the
 * gun FSMs at true discharge (not on trigger press), so dry-fire, menu
 * confirm and item grabs never shake. Safe to call unconditionally
 * (self-gated to live gameplay). */
void Pc_ShakeKickHurt(void);
void Pc_ShakeKickFire(void);
void Pc_ShakeKickDealt(void);

/* Current camera offset in Q12 world units (0,0,0 at rest). Advances decay
 * and phase once per MainLoop tick; repeat calls within the tick (classic +
 * TPS paths) reuse the resolved values. Call once per camera commit. */
void Pc_Shake_Offsets(s32* ox, s32* oy, s32* oz);

#endif /* PC_SHAKE_H */
