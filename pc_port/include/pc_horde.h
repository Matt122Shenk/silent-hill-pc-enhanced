/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PC_HORDE_H
#define PC_HORDE_H

/* Horde mode: wave survival in the MAP2_S00 streets, picked from the title
 * menu. Every entry point below is a no-op unless a run is live, so vanilla
 * play (and the player's manual saves) is untouched.
 *
 * Save isolation, by construction:
 *  - entry goes through the normal New Game wipe (fresh RAM savegame; the
 *    memcard load path is never called);
 *  - the RAM autosave snapshot is skipped while a run is live, so quitting
 *    or dying never clobbers the player's Continue point;
 *  - the memcard write worker refuses while a run is live (defence in depth;
 *    the arena offers no save point anyway);
 *  - Continue always ends a live run first, so a loaded vanilla save plays
 *    vanilla.
 */

int  Pc_Horde_Active(void);

/* Title menu: arm a run; consumed by OnNewGame. */
void Pc_Horde_Request(void);
int  Pc_Horde_Requested(void);

/* Difficulty-confirm hook, after the savegame wipe, before MapLoad:
 * retargets the fresh savegame at the arena and starts the run. */
void Pc_Horde_OnNewGame(void);

/* End a live run (Continue, quit to title). Idempotent. */
void Pc_Horde_EndRun(void);

/* End of GameBoot_MapLoad: with the arena live, suppress natural spawns and
 * arm the starter group (which fires on the first gameplay frame). */
void Pc_Horde_OnMapLoad(int mapIdx);

/* Once per frame from the main loop. No-op unless a run is live. */
void Pc_Horde_Update(void);

/* Kill hook (npc_main corpse latch): rolls the wave loot. */
void Pc_Horde_OnEnemyKilled(void);

/* Readouts for the HUD. Alive is scanned, never cached. */
unsigned int Pc_Horde_Kills(void);
unsigned int Pc_Horde_Wave(void);
unsigned int Pc_Horde_ThreatPct(void);
unsigned int Pc_Horde_PlayerDmgPct(void);
int Pc_Horde_Alive(void);
int Pc_Horde_CountdownSecs(void);

/* Damage Harry takes, pct (draft resistance rewards; 100 off-mode). */
unsigned int Pc_Horde_PlayerResistPct(void);

/* Inter-wave draft: 3 offers, kinds below. DraftOffer's pct is the damage
 * multiplier for Dmg, the incoming-damage multiplier for Resist and the item
 * count for Heal / Ammo. */
typedef enum { HordeOffer_Dmg = 0, HordeOffer_Gun, HordeOffer_Melee, HordeOffer_Heal, HordeOffer_Ammo,
               HordeOffer_Resist } e_HordeOfferKind;

int Pc_Horde_DraftActive(void);
int Pc_Horde_DraftOffer(int i, int* kind, int* item, unsigned int* pct);
void Pc_Horde_DraftPick(int i);

/* Live roster world positions for the minimap foe dots (up to cap). */
int Pc_Horde_LiveEnemies(q19_12* outX, q19_12* outZ, int cap);

/* Map-event dispatch: 1 = swallow the door/room transition (arena lockdown).
 * Plays the locked feedback itself. */
int  Pc_Horde_VetoTransition(void);

#endif /* PC_HORDE_H */
