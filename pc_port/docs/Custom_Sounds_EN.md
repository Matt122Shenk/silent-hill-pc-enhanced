# Custom gunshot and notification sounds

Silent Hill PC Enhanced lets you replace the gunshot of each weapon and the
sounds of the item notification (toast) and quick heal with your own `.wav`
files. Out of the box the game uses its original sounds; nothing is replaced
until you pick a file.

*Versión en español: [Sonidos_Personalizados.md](Sonidos_Personalizados.md).*

---

## 1. Prepare your sound files

Each sound must be a **WAV file, 16-bit PCM, mono or stereo**. Any sample rate
works.

If your file is in another format (MP3, OGG, 24-bit or 32-bit float WAV…),
convert it first. For example, in [Audacity](https://www.audacityteam.org/):

1. Open the file.
2. **File → Export Audio…**
3. Format: **WAV (Microsoft)**, Encoding: **Signed 16-bit PCM**.
4. Save.

Tips:

- Trim the silence at the start so the shot plays the instant you fire.
- Keep gunshots short (under 1–2 seconds).
- Use sounds you have the right to use. This project does not ship any.

## 2. Open the sound tools

1. Run `SilentHillPC_Launcher.exe`.
2. Click the **Mod Manager** button in the main window.
3. In the Mod Manager, click **Audio**.
4. In the Audio window, open the **File** menu. You will find:
   - **Gunshot sounds…** (section 3)
   - **Interface sounds…** (section 4)

## 3. Gunshot sounds

The **Gunshot Sounds** window has one row per weapon: **Handgun**, **Rifle**,
**Shotgun** and **Blaster** (Hyper Blaster).

1. Check the bottom line, **Loose files**. If it says
   `OFF (swaps will not load)`, click **Enable**. The config shipped with
   Silent Hill PC Enhanced already has it on.
2. On the weapon's row, click **Browse…** and pick your `.wav`.
3. Make sure **Enabled** is ticked on that row.
4. The new sound plays the **next time the weapon's sound bank loads**: equip
   another weapon and back, or change maps. No restart needed.

To go back to the original sound, click **×** on that row. To keep your file
but use the original sound for now, untick **Enabled**.

Where the files go (`gamedata/load/SND/` inside the game folder):

| Weapon | File | Config key |
|---|---|---|
| Handgun | `PISTOL.001.wav` | `sfx_handgun` |
| Rifle | `RIFEL.004.wav` | `sfx_rifle` |
| Shotgun | `SHOTGUN.001.wav` | `sfx_shotgun` |
| Hyper Blaster | `HYPER.002.wav` | `sfx_blaster` |

`RIFEL` is not a typo: it is the name the original game uses for the rifle's
sound bank.

## 4. Notification and quick heal sounds

The **Interface Sounds** window has two rows:

- **Item toast**: the quick "Obtained" / "Used" notification.
- **Quick heal**: the sound when you heal with the quick-heal button.

1. On the row, click **Browse…** and pick your `.wav`.
2. Make sure **Enabled** is ticked.
3. **Restart the game** if it was running: these sounds are loaded once per
   session, the first time they play.

To go back to the default sound (the game's confirm sound for the toast, the
heal sound for quick heal), click **×** or untick **Enabled**.

Where the files go (`gamedata/sound/custom/` inside the game folder):

| Sound | File | Config key |
|---|---|---|
| Item toast | `toast_cue.wav` | `toast_cue` |
| Quick heal | `heal_cue.wav` | `heal_cue` |

The item toast for ammo and heals you pick up needs **silent pickup**
(`silent_take = 1`), which is on in the shipped config.

## 5. Without the launcher (manual method)

1. Convert your file as in section 1.
2. Copy it to the folder from the tables above, with **exactly** that file
   name.
3. In `config.cfg` (next to `SilentHillPC.exe`), make sure these are set:
   - gunshots: `allow_loose_files = 1` and the weapon's key, e.g.
     `sfx_handgun = 1`;
   - notifications: `toast_cue = 1` and/or `heal_cue = 1`.
4. Start the game (or re-equip the weapon, for gunshots).

## 6. Troubleshooting

| Problem | Fix |
|---|---|
| "That file cannot be used" | The file is not 16-bit PCM WAV, or has more than 2 channels. Convert it (section 1). |
| The gunshot did not change | Check **Loose files** is ON and **Enabled** is ticked, then switch weapons or change maps. |
| The toast or heal sound did not change | Check **Enabled** is ticked and restart the game. |
| No toast appears when picking up ammo | Turn on silent pickup (`silent_take = 1`) in `config.cfg`. |
| I want the original sounds back | Click **×** on the row, or delete the file from the folders above. |
