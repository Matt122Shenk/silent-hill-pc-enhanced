/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * pc_ammo_hud.h - real-time ammo readout (magazine / reserve) for firearms.
 *
 * Shown while aiming, or always while a gun is equipped when ammo_always is
 * set. Melee weapons never count. Same self-contained-GL discipline as the
 * item toast; corner/scale/opacity mirror the minimap/toast options.
 */
#ifndef PC_AMMO_HUD_H
#define PC_AMMO_HUD_H

void Pc_AmmoHud_Draw(void);

#endif /* PC_AMMO_HUD_H */
