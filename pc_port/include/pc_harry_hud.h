/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PC_HARRY_HUD_H
#define PC_HARRY_HUD_H

/* Harry's health HUD (config keys hp_style / hp_number / hp_corner /
 * hp_scale / hp_opacity / hp_always).
 *
 * Two styles sharing one widget box: a classic bar and an animated heartbeat
 * pulse line, both tinted green -> yellow -> orange -> red as HP falls, with
 * an optional numeric readout at the right. hp_always = 1 keeps it up;
 * otherwise it appears briefly after damage or healing. Same extreme-corner
 * placement, scale and opacity knobs as the ammo HUD.
 *
 * Drawn as one CPU-composed RGBA quad from the post-capture hook (same
 * self-contained-GL arrangement as pc_ammo_hud.c).
 */

/* Per-frame draw from the post-capture GL hook (cheap no-op when hidden). */
void Pc_HarryHud_Draw(void);

#endif /* PC_HARRY_HUD_H */
