# Quick options reference (F10) — English version

> Translation of the approved Spanish draft (`Quick_Options_Reference.md`).
> Convention: values in parentheses are shown by the menu.
> Open with F10 (rebindable). Navigation: arrows or D-pad to move,
> left/right to change values, confirm with Enter, cancel with Escape, Q/E or
> PgUp/PgDn to switch pages, mouse supported.
> Every page ends with "Prev page", "Next page" and "Close". Changes are
> saved automatically to `config.cfg`.

## GRAPHICS

- **Texture Filter** (Off / Dither / Bilinear / Trilinear / Aniso 2x–16x):
  Texture filtering. Dither is the PSX-faithful look.
- **Antialiasing** (Off / 2x / 4x / 8x): Edge smoothing. Requires restarting
  the game (marked with *).
- **Post Process** (Off / CRT / Scanlines / Vignette / Color Grade /
  Film Grain / Sharpen / PSX Retro / Cinematic): Fullscreen image filter.
- **Tone Mapping** (Off / Reinhard / ACES / Filmic): Final image tone
  mapping.
- **Fog Strength** (0–2): Fog density. 1.1 matches the PSX; lowering it lets
  you see further.
- **Flashlight Mode** (Classic / Classic + Shadows / Modern / Modern +
  Shadows): Classic is the original per-vertex flashlight; Modern is a
  stylized per-pixel spotlight, with or without real-time shadows.
- **Flashlight Intensity** (0–3): Flashlight cone brightness.
- **Flashlight Size** (0–3): Flashlight cone coverage.
- **Shadow Resolution** (256–8192): Flashlight shadow-map resolution. Higher
  means sharper shadows at a higher performance cost.
- **Bullet Decals** (Off/On): Bullet marks where the player's shots land (up
  to 64, cleared on map change).
- **Enemy HP Bars** (Off/On): Red health bars over live enemies in view
  within 12 m. Not visible through walls.

## HUD & AUDIO

- **Minimap** (Off / Square / Circle): On-screen minimap with square and
  circular styles.
- **Minimap Scale** (60–120 %): Minimap size.
- **Minimap Corner** (corners): Minimap corner.
- **Minimap Opacity** (0–100 %): Minimap opacity.
- **Minimap Require Map** (Off/On): Only draws the minimap once Harry has
  found the area's paper map.
- **Crosshair** (Off/On): Crosshair while aiming in TPS/OTS; always visible
  in first person.
- **Toast Corner / Scale / Opacity**: Corner, size (60–130 %) and opacity
  (20–100 %) of pickup and heal-use toasts. The toast sound can be customized
  from the launcher.
- **Speaker Layout** (Auto / Stereo / Quad / 5.1 / 7.1): Speaker setup.
- **Music Volume** (0–16): Music volume.
- **Effects Volume** (0–16): Effects volume.
- **FMV Volume** (0–1): Movie cutscene volume.

## HUD 2

- **Ammo Corner / Scale / Opacity**: Corner, size (60–130 %) and opacity
  (20–100 %) of the ammo counter.
- **Ammo Always** (Off/On): Counter stays up with a firearm equipped; if
  not, only while aiming.
- **Ammo HUD** (Off/On): Master switch for the counter.
- **Auto Reload** (Off/On): Animated auto reload on empty magazine with
  reserve left.
- **Manual Reload** (Off/On): The R key / Square-X button reloads the aimed
  weapon when it accepts a reload.
- **D-Pad Weapons** (Off/On): The controller D-pad picks weapons instead of
  moving. They can also be assigned from the inventory.
- **D-Pad Up / Down / Left / Right** (weapon or None): Weapon bound to each
  direction. Binding a weapon held elsewhere swaps them.
- **Quick Heal** (Off/On): Fast heal with the best item available. Uses L3.
- **Toast Background** (0–100 %): Toast background opacity.

## VIEW & ASPECT

- **Control Type** (Simple / Advanced): Picture-shape controls. Simple uses
  Aspect Trim; Advanced uses Horizontal/Vertical FOV and Pixel Aspect.
- **Aim Camera** (Auto / Classic / TPS / OTS / FPS): Lets you use a different
  camera while aiming.
- **Melee Aim Walk** (Off/On): Aiming melee in TPS/OTS forces a slow walk
  (like fixed cameras) instead of dropping the weapon to run.
- **Aspect Trim** (0.50–1.50, Simple): Fine 4:3 aspect trim.
- **Horizontal / Vertical FOV** (0.25–2.00, Advanced): Field of view.
- **Vertical Shift** (±60 rows): In-game vertical view shift.
- **Cutscene Shift** (±60 rows): Cutscene vertical view shift.
- **Pixel Aspect** (0.50–2.00, Advanced): Pixel aspect.
- **Reset View Settings**: Restores every value on this page.

## CHEATS

- **God mode** (Off/On): Invincibility (same as key 7).
- **Noclip** (Off/On): Walk through walls.
- **Enemies ignore Harry** (Off/On): Enemies ignore Harry (same as key 9).
- **Unlimited enemies** (Off/On): Raises the per-room concurrent-enemy cap.
- **Handgun bullets +15**: Grants 15 handgun bullets (instant action).
- **Hunting rifle +30**: Grants the rifle if missing + 30 shells (action).
- **Shotgun +30**: Grants the shotgun if missing + 30 shells (action).
- **Kill nearby enemies** (action): Kills nearby enemies through their normal
  death path.
- **Play as** (list): Play as another character (keeps Harry's animations).
  Browsable as a dropdown.
- **Free camera** (Off/On): Debug free camera.

## DEBUG

- **Allow console** (Off/On): Enables the console (`~`) and dev keys.
- **Collision visualizer** (Off/On): Harry's collision panel and wireframe.
- **Keyframe viewer (K)** (Off/On): Animation keyframe inspector (K, `,`/`.`).
- **Fast forward (Ctrl+F5)** (Off/On): Sticky fast-forward (the key is held;
  it switches itself off if Harry dies).
- **Wireframe (Ctrl+F1)** (Off/On): Wireframe rendering.
- **No textures (Ctrl+F2)** (Off/On): Untextured rendering.
- **Spawn** (list): Spawns a monster from the list on confirm.
- **Log Harry position** (action): Writes position and map to the log.
- **Log camera shot** (action): Writes the camera shot to the log.
- **Kill Harry** (action): Kills Harry instantly.

## TRAINER

- **Weapon** (list): Firearm to grant (handgun, rifle, shotgun, blaster).
- **Give Weapon + Ammo** (action): Grants the chosen firearm (+30 of its
  ammo).
- **Melee** (list): Melee weapon to grant (knife, pipe, drill, hammer, saw,
  katana, axe).
- **Give Melee** (action): Grants the chosen weapon.
- **Heal** (list): Healing item to grant (drink, kit, ampoule).
- **Give Heals x3** (action): Grants 3 of the chosen heal.

## IMMERSION

- **Weapon Immersion** (Off/On): The equipped weapon hides after a few
  seconds and comes back on equip, aim, fire, reload, or when Harry senses
  danger (while the radio crackles).
- **Fear Panting** (Off/On): Harry pants with fear at random while the radio
  sounds.
- **Blood Walls** (Off/On): Blood splats on the wall behind bullet-hit
  enemies.
- **Flicker** (Off/On): The flashlight flickers in short bursts near live
  enemies (15% when close, 35% while facing; per-pixel modes only, not
  Classic).

## DIFFICULTY (independent from the native difficulty; Normal = vanilla)

- **Ammo** (Low / Medium / Normal / High): Loot ammo at 15 / 50 / 100 /
  170 %. E.g. 15 bullets become 2 / 8 / 15 / 26.
- **Heals** (Low / Medium / Normal / High): Heal power raised or lowered to
  25 / 50 / 100 / 150 %.
- **Enemy Damage** (Low / Medium / Normal / High / Very High / Hell): Enemy
  damage raised or lowered to 50 / 75 / 100 / 250 / 400 / 700 %.

## HEALTH

- **Health HUD** (Off/On): Master switch for Harry's health indicator.
- **Style** (Bar / Pulse): Classic bar or animated heartbeat line (speeds up
  at low health).
- **Number** (Off/On): HP number at the right.
- **Corner** (corners): Extreme corner for the indicator.
- **Scale** (60–130 %): Size.
- **Opacity** (20–100 %): Opacity.
- **Always Show** (Off/On): Always visible; if not, only a few seconds after
  taking damage or healing.

## VISUAL FEEDBACK

- **Hurt Kick** (Off/On): Camera kick on taking damage.
- **Hurt Strength** (0–200 %): Strength of the above.
- **Fire Kick** (Off/On): Kick when firing (as the bullet leaves, same for
  all 4 guns).
- **Fire Strength** (0–200 %): Strength of the above.
- **Dealt Kick** (Off/On): Kick when damaging an enemy.
- **Dealt Strength** (0–200 %): Strength of the above.
- **Hitmarker** (Off/On): The crosshair flashes red on a kill (needs the
  crosshair enabled).
- **Kill Flash** (Off/On): Brief fullscreen flash on a kill.
- **Flash Style** (B&W / Vignette): Kill flash style. Black and white or
  black vignette.
- **Heal Flash** (0–100 %): Green flash intensity when healing (0 disables
  it).
- **Low Health Glow** (Off/On): Pulsing red edge glow at low health.
- **Low Opacity** (0–100 %): Low-health glow opacity.
- **Hurt Flash** (Off/On): Dark-red fullscreen flash when Harry takes
  damage.
- **Hurt Opacity** (0–100 %): Hurt-flash opacity.
