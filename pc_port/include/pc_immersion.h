/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * pc_immersion.h - weapon visibility immersion: the equipped weapon stays
 * hidden unless recently equipped, aimed, fired, reloaded, or radio-danger
 * is up. Drives the same hide/show bit the map scripts use for cutscenes.
 */
#ifndef PC_IMMERSION_H
#define PC_IMMERSION_H

void Pc_Immersion_Update(void);

/* 1 while the flicker's blackout holds the beam dead: the light button must
 * not toggle the flashlight (game_sys_states.c). */
int  Pc_Immersion_FlashlightLocked(void);

#endif /* PC_IMMERSION_H */
