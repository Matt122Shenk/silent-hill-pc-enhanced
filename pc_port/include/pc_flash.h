/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PC_FLASH_H
#define PC_FLASH_H

/* Fullscreen flash envelopes (heal green, low-health dark-red vignette,
 * hurt dark-red), resolved as 0..1 levels for the post-process shader
 * (same philosophy as the kill flash). Replaces the old OT2 additive
 * prims, so the flashes compose with CRT and friends instead of fighting
 * the post chain.
 *
 * Config: heal_flash (0..100 intensity, 0 = off), low_health_glow +
 * low_health_op (0..100), hurt_flash + hurt_flash_op (0..100).
 */

/* Per-frame advance (timers, low-health pulse phase). Call once per frame
 * where Pc_HealFlashUpdate/Pc_LowHealthGlowUpdate ran. */
void Pc_Flash_Update(void);

/* Arm the hurt envelope (called on player damage, like the shake kick). */
void Pc_Flash_KickHurt(void);

/* Envelope levels 0..1 for the shader uniforms (config already folded). */
float Pc_Flash_Heal(void);
float Pc_Flash_Low(void);
float Pc_Flash_Hurt(void);

/* 1 while any envelope is alive (post-process run gate). */
int Pc_Flash_Active(void);

#endif /* PC_FLASH_H */
