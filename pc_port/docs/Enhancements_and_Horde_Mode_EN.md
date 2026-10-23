# Silent Hill PC Enhanced — Enhancements and Horde Mode

This document lists what Silent Hill PC Enhanced adds on top of the original
port, **beta-2026.09.04.1**. What that version already had (alternate
cameras, minimap, crosshair, bullet decals, randomizer, HD textures, etc.) is
not covered here; see `CHANGELOG.md` for that.

Everything is optional. The `config.cfg` shipped with this version is tuned
for the best experience (especially with a controller), with many of these
options already on; delete a key and it falls back to its original value,
which is almost always off. Options are set from the **F10** quick menu and
saved to `config.cfg` automatically. Each option's config key is given in
parentheses.

*Versión en español: [Mejoras_y_Modo_Hordas.md](Mejoras_y_Modo_Hordas.md).*

---

## Contents

1. [Horde Mode](#1-horde-mode)
2. [Combat and controls](#2-combat-and-controls)
3. [HUD and notifications](#3-hud-and-notifications)
4. [Visual feedback](#4-visual-feedback)
5. [Immersion](#5-immersion)
6. [Independent difficulty](#6-independent-difficulty)
7. [F10 menu: new pages](#7-f10-menu-new-pages)
8. [Navigable pause menu](#8-navigable-pause-menu)
9. [Audio and launcher](#9-audio-and-launcher)
10. [Integrations](#10-integrations)
11. [How Horde Mode works](#11-how-horde-mode-works)

---

## 1. Horde Mode

An extra wave-survival mode with a roguelike twist, separate from the
campaign. Pick it from the main menu (**HORDE**). The full rules are in
[section 11](#11-how-horde-mode-works).

## 2. Combat and controls

- **Aim Camera** (`aim_camera_mode`): switch to another camera only while
  aiming (Auto / Classic / TPS / OTS / FPS). For example, explore with the
  classic camera and aim in first person.
- **Melee Aim Walk** (`melee_aim_walk`): aiming a melee weapon in TPS/OTS
  makes Harry walk slowly, as in the fixed cameras, instead of lowering the
  weapon when you run.
- **D-Pad Weapons** (`dpad_weapons`, `dpad_up/down/left/right`): the
  controller D-pad selects weapons instead of moving Harry. Each direction
  holds a weapon, set from F10 or from the inventory itself. Assigning a
  weapon that is already on another direction swaps the two.
- **Auto Reload** (`auto_reload`): reloads automatically, with the
  animation, when the magazine runs dry and there is reserve ammo.
- **Manual Reload** (`manual_reload`): turns reloading with R / Square while
  aiming on or off.
- **Quick Heal (improved)** (`quick_heal`): master switch for quick heal
  (L3). It now shows a notification with the item used, has an adjustable
  green flash and supports a custom sound (see
  [section 9](#9-audio-and-launcher)).

## 3. HUD and notifications

- **Ammo HUD** (`ammo_hud`, `ammo_always`, `ammo_corner`, `ammo_scale`,
  `ammo_opacity`): magazine and reserve counter for firearms. Shown while
  aiming, or always while a firearm is equipped. Corner, size and opacity are
  configurable.
- **Harry health HUD** (`hp_hud`, `hp_style`, `hp_number`, `hp_corner`,
  `hp_scale`, `hp_opacity`, `hp_always`): a classic bar or an animated
  heartbeat line that speeds up at low health. Shifts from green to red and
  can show the health number. Always visible, or only for a few seconds after
  taking damage or healing.
- **Enemy HP bars** (`enemy_hp_bars`): red bars over live enemies in view
  and within 12 m. They never show through walls.
- **Item notifications (toasts)**: a quick "Obtained" / "Used" notice for:
  - ammo and heals picked up without the pickup screen (`silent_take`);
  - items given by the console (`GIVE`), the randomizer, the trainer or Horde
    Mode;
  - heals used with quick heal.

  Options: corner, size, opacity and background (`item_toast_corner`,
  `item_toast_scale`, `item_toast_opacity`, `item_toast_bg`).
  - **Translated names:** a built-in English/Spanish table for weapons, ammo
    and heals (for example "First Aid Kit" / "Botiquín"). Other items use the
    game's own name, with the PAL disc's accents fixed (they used to render
    broken, e.g. "Malet…" instead of "Maletín").
  - **Language:** "Obtained" / "Used" in English; "Obtenido" / "Usado" when
    the game language is Spanish.

## 4. Visual feedback

All of these flashes go through the post-process shader, so they combine
with CRT, scanlines and the other filters without fighting them.

- **Camera kicks**:
  - when taking damage (`shake_hurt`);
  - when firing (`shake_fire`), at the moment the bullet actually leaves;
  - when hurting an enemy (`shake_dealt`).

  Each has its own strength, 0 to 200 % (`shake_*_int`).
- **Hitmarker** (`hitmarker`): the crosshair flashes red on a kill.
- **Kill Flash** (`kill_flash`, `kill_flash_mode`): a brief full-screen
  flash on a kill, black and white or a black vignette.
- **Heal Flash** (`heal_flash`): strength of the green flash when healing
  (0 turns it off).
- **Low Health Glow** (`low_health_glow`, `low_health_op`): a pulsing red
  glow at the screen edges at low health.
- **Hurt Flash** (`hurt_flash`, `hurt_flash_op`): a dark-red flash when
  taking damage.

## 5. Immersion

- **Weapon Immersion** (`immersion_weapons`): the equipped weapon hides a
  few seconds after its last use. It comes back when you equip, aim, fire,
  reload, or when the radio picks up danger. Picking up items no longer
  brings it back.
- **Fear Panting** (`immersion_fear`): Harry pants with fear now and then
  while the radio is crackling.
- **Flicker** (`immersion_flicker`): near live enemies the flashlight starts
  failing, stutters for about a second and then dies for half a second; while
  it is dead the light button does nothing, so you cannot switch it back on
  until it recovers by itself (15 % when close, 45 % when facing one).
  Per-pixel flashlight modes only, not Classic.
- **Blood Walls**: blood splatters on the wall behind enemies hit by
  gunfire.

## 6. Independent difficulty

Applied on top of the game's own difficulty, which is left untouched. On
**Normal** the game is identical to the original.

| Option | Values |
|---|---|
| **Ammo** (`diff_ammo`) | Low 15 % · Medium 50 % · Normal 100 % · High 170 % of the ammo picked up |
| **Heals** (`diff_heals`) | Low 25 % · Medium 50 % · Normal 100 % · High 150 % of what each item heals |
| **Enemy damage** (`diff_enemy`) | Low 50 % · Medium 75 % · Normal 100 % · High 250 % · Very High 400 % · Hell 700 % |

## 7. F10 menu: new pages

Besides the options above, F10 has these new pages: **HUD 2**,
**Immersion**, **Difficulty**, **Health**, **Visual Feedback** and
**Trainer**.

- **Trainer**: gives a chosen firearm with 30 rounds of its ammo, a melee
  weapon, or 3 of the chosen heal. No console needed.
- Every row is described in `Quick_Options_Reference_EN.md` (English) and
  `Quick_Options_Reference.md` (Spanish).

## 8. Navigable pause menu

Pausing used to show only the word "PAUSED". It is now a menu over the frozen
game frame, with these options:

**Resume · Load · Quick Options · Main Menu · Quit**

It is in English by default and in Spanish when the game language is Spanish.

## 9. Audio and launcher

- **Custom gunshots per weapon** (launcher → Audio tool → *SFX swap*): one
  `.wav` per weapon (handgun, rifle, shotgun, Hyper Blaster), copied to the
  right place in `gamedata/load/SND/`. Each weapon has its own switch
  (`sfx_handgun`, `sfx_rifle`, `sfx_shotgun`, `sfx_blaster`). The change
  applies when the sound bank reloads (on equip or map change), no restart
  needed.
- **Interface sounds** (launcher → Audio tool → *UI sounds*): custom sounds
  for the item notification (`toast_cue`) and for quick heal (`heal_cue`),
  stored in `gamedata/sound/custom/`. With no file, or with the switch off,
  the usual sounds play.
- Step-by-step guide for both tools: [Custom_Sounds_EN.md](Custom_Sounds_EN.md).
- Tweaks to the launcher's main window, Mod Manager and converters to make
  room for these tools.
- **Updates disabled**: as an unofficial fork, the launcher never checks for
  or downloads builds of the original project (they would wipe these
  changes). Links to the original project, its website and its Discord stay;
  "Report Bug" opens this fork's issues.
- **Discord Rich Presence off by default**, because the built-in Discord app
  belongs to the official project. It can be turned on in `config.cfg`.

## 10. Integrations

- **Console and randomizer**: items given with `GIVE` and randomizer gifts
  show the item notification.

---

## 11. How Horde Mode works

### Starting a run, and save isolation

- Pick **HORDE** on the main menu, then a difficulty, as for a new game.
- A horde run is **throwaway**: it starts from a fresh in-memory game and
  never saves. There is no autosave, memory card writes are blocked, and
  "Continue" ends the horde run before loading your save. **Your campaign
  saves are never touched.**
- Horde kills do not count towards the campaign's killed-enemy records
  either.

### Arena

- The streets of **Old Silent Hill**, just outside the café (MAP2_S00). The
  café exit cutscene is skipped.
- **Every door is locked** (the lock sound plays); you cannot leave the arena.
- You have every paper map from the start, and the minimap marks the live
  enemies of the wave.
- Starting gear: flashlight, radio, kitchen knife and handgun with 15 rounds.

### Waves

- A **3-second** countdown, then a group of **5, 7 or 9** enemies appears,
  mixing Grey Children, Puppet Nurses, Rompers and Groaners. The area's
  normal enemies are turned off.
- They spawn **behind or beside** Harry, 8–14 m away, never in front: on
  ground at his level (never roofs or basements), with a clear line to him
  and in open areas (streets and alleys, not interiors).
- An enemy that stays more than 20 m away for over 4 s is moved back around
  Harry, so the pressure never drops.
- The wave ends when no enemy is left alive. The **reward shop** then opens.
- **Threat**: from wave 2 on, enemy health and damage grow **x1.25 per
  wave**, up to a maximum of **x10** (wave 12).

### Loot per kill

| Chance | Result |
|---|---|
| 5 % | Nothing |
| 15 % | Heal: Health Drink (75 %) or First Aid Kit (25 %) |
| 80 % | Ammo for a gun you own: handgun 5–10, shotgun 2–4, rifle 2–4 |

### Reward shop (between waves)

The game pauses and **3 cards** appear; you pick one for free and the next
countdown starts. It cannot be skipped.

**How the cards are chosen:** the possible rewards are gathered and 3 are
drawn at random.

| Reward | When it can appear | What it gives |
|---|---|---|
| **Ammo** | If you own a firearm | 2 magazines for the gun with the **least ammo** (counted in magazines, not loose rounds): handgun +30, shotgun +12, rifle +12 |
| **Heal** | Always | Health Drink x2–3 (50 %), First Aid Kit x1–2 (35 %) or Ampoule x1 (15 %) |
| **Firearm** | If you are missing one (rifle, shotgun) | The gun with one magazine of ammo |
| **Melee weapon** | If you are missing one | Steel pipe, rock drill, hammer, chainsaw, katana or axe |
| **Damage up** | 50 % per shop (45 % if you own the handgun, rifle and shotgun but are missing a melee weapon) | x1.50, x2.50, x2.75 or x4.00, stacking up to x100 |
| **Max health** | Same chance as damage, rolled separately | +18 %, +25 % or +33 % effective health (see below) |

If fewer than 3 rewards are possible, the pool is topped up with a second,
different heal and, only if still short, a damage upgrade. The Hyper Blaster
never appears (it has infinite ammo).

**Max health:** the original game fixes Harry's health at 100, so this
upgrade cuts the damage he takes instead: +25 % health equals taking x0.80
damage. It stacks up to x5 (enemies at x10 threat still deal double damage).
Heals, the health bar and the ECG work as always.

**Shop controls:**

| Action | Keyboard | Controller | Mouse |
|---|---|---|---|
| Change card | A / D or arrows | D-pad or stick | Move the pointer over a card, or the wheel |
| Pick | Enter, Space or C | Confirm button | Left click |

- Nothing can be picked during the first half second. A button that was
  already held when the shop opened (for example the shot that killed the
  last enemy) has to be released and pressed again.
- The mouse only changes the selection when it moves, so it never fights the
  keyboard or the controller.

### Mode HUD

- **Top**: wave, enemies alive (or the countdown to the next wave), kills and
  threat.
- **Left**: your **damage** and **health** multipliers.
- The HUD and the shop are in Spanish when the game language is Spanish and
  in English otherwise.

### Technical details

- The HUD and the shop are drawn like the F10 menu: one text texture created
  at startup, no textures created or deleted during play, and all graphics
  state restored. This avoids the graphics glitches the first prototypes
  caused.
- The mode has its own random number generator, so it never alters enemy
  behaviour or the game's demos.
- Floor blood is cleaned up automatically in the arena: the original game
  only clears it on room changes, and without the cleanup new blood would
  stop appearing.
