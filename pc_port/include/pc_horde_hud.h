/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PC_HORDE_HUD_H
#define PC_HORDE_HUD_H

/* Top-center run panel: wave, alive, kills, threat. Horde-only. */
void Pc_HordeHud_Draw(void);

/* Claim the GL program + glyph atlas on the first frame the context exists
 * (same reason as Pc_QuickOptions_PreloadGL). */
void Pc_HordeHud_PreloadGL(void);

/* Inter-wave reward draft: 3 large cards, pad + mouse. Drawn center screen
 * from the post-capture hook; input driven from the main loop. */
void Pc_HordeDraft_Draw(void);
void Pc_HordeDraft_Update(int up, int down, int left, int right, int confirm, int cancel);

#endif /* PC_HORDE_HUD_H */
