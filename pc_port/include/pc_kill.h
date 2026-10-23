/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PC_KILL_H
#define PC_KILL_H

/* Player-kill watcher feeding the hitmarker + kill flash (config keys
 * hitmarker / kill_flash / kill_flash_mode, all off by default).
 *
 * The engine has no single death choke (each AI drops its own HP), so kills
 * are detected by polling: a damageable NPC slot transitioning from live to
 * dead while its last received blow was a player weapon counts as a player
 * kill. Boss kills never count (same chara list as the quick-save block).
 */

/* Per-frame poll. Call once per frame (next to Pc_Immersion_Update). */
void Pc_Kill_Update(void);

/* Hitmarker state: 1 while the crosshair should draw red. */
int Pc_Kill_Hitmarker(void);

/* Kill-flash envelope 0..1 (0 = idle). */
float Pc_Kill_FlashLevel(void);

/* Kill-flash look while flashing: 0 = black & white, 1 = black vignette. */
int Pc_Kill_FlashMode(void);

#endif /* PC_KILL_H */
