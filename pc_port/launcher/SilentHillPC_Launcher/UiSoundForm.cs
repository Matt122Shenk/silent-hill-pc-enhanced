/* SPDX-License-Identifier: GPL-3.0-or-later */
using System;
using System.Drawing;
using System.IO;
using System.Windows.Forms;

namespace SilentHillPC_Launcher
{
    /// <summary>Interface sound swaps: one .wav each for the item toast cue and
    /// the quick-heal cue, stored under gamedata/sound/custom/. Without a file
    /// (or with the flag off) the game plays its default sounds. Opened from
    /// the Audio tool's File menu; one reusable window like it.</summary>
    internal sealed class UiSoundForm : Form
    {
        private static UiSoundForm s_open;

        private static readonly string[] Names = { "Item toast", "Quick heal" };
        private static readonly string[] Files = { "toast_cue.wav", "heal_cue.wav" };
        private static readonly string[] Keys  = { "toast_cue", "heal_cue" };
        private static readonly string[] Falls = { "confirm SFX", "heal SFX" };

        private readonly string _gameRoot;
        private readonly ConfigManager _config;
        private readonly Label[] _file = new Label[2];
        private readonly CheckBox[] _on = new CheckBox[2];
        private readonly Label _info = new Label();
        private bool _quiet;

        public static void ShowTool(IWin32Window owner, string gameRoot, ConfigManager config)
        {
            if (string.IsNullOrEmpty(gameRoot) || config == null)
                return;
            UiSoundForm f = s_open;
            if (f == null || f.IsDisposed)
            {
                f = new UiSoundForm(gameRoot, config);
                s_open = f;
                f.FormClosed += (s, e) => { if (s_open == f) s_open = null; };
                f.Show(owner);
            }
            else
            {
                if (f.WindowState == FormWindowState.Minimized) f.WindowState = FormWindowState.Normal;
                f.BringToFront();
                f.Activate();
            }
        }

        private UiSoundForm(string gameRoot, ConfigManager config)
        {
            _gameRoot = gameRoot;
            _config = config;

            Text = "Interface Sounds";
            ClientSize = new Size(620, 240);
            FormBorderStyle = FormBorderStyle.FixedDialog;
            MaximizeBox = false;
            MinimizeBox = false;
            StartPosition = FormStartPosition.CenterParent;
            ShowInTaskbar = false;

            var head = new Label
            {
                Location = new Point(12, 12),
                Size = new Size(596, 30),
                Text = "Pick a .wav per cue (16-bit PCM mono/stereo). Without one, the game plays its default sound."
            };
            Controls.Add(head);

            for (int i = 0; i < 2; i++)
            {
                int y = 48 + i * 46;
                int row = i;
                var name = new Label { Location = new Point(12, y + 4), Size = new Size(96, 20), Text = Names[i] };
                _file[i] = new Label { Location = new Point(112, y + 4), Size = new Size(266, 20), AutoEllipsis = true };
                var browse = new Button { Location = new Point(384, y), Size = new Size(80, 26), Text = "Browse…" };
                var clear = new Button { Location = new Point(468, y), Size = new Size(30, 26), Text = "×" };
                _on[i] = new CheckBox { Location = new Point(504, y + 4), Size = new Size(104, 20), Text = "Enabled" };
                browse.Click += (s, e) => OnBrowse(row);
                clear.Click += (s, e) => OnClear(row);
                _on[i].CheckedChanged += (s, e) => OnToggled(row);
                var tip = new ToolTip();
                tip.SetToolTip(clear, "Remove the custom sound (back to default).");
                Controls.Add(name);
                Controls.Add(_file[i]);
                Controls.Add(browse);
                Controls.Add(clear);
                Controls.Add(_on[i]);
            }

            _info.Location = new Point(12, 144);
            _info.Size = new Size(596, 36);
            Controls.Add(_info);

            var close = new Button { Location = new Point(520, 200), Size = new Size(88, 26), Text = "Close", DialogResult = DialogResult.OK };
            Controls.Add(close);
            AcceptButton = close;

            RefreshAll();
            Loc.Apply(this);
        }

        private string CueDir()
        {
            return Path.Combine(_gameRoot, "gamedata", "sound", "custom");
        }

        private void RefreshAll()
        {
            for (int i = 0; i < 2; i++)
            {
                string p = Path.Combine(CueDir(), Files[i]);
                if (File.Exists(p))
                {
                    long kb = new FileInfo(p).Length / 1024;
                    _file[i].Text = Files[i] + " · " + kb + " KB";
                }
                else
                {
                    _file[i].Text = "(default: " + Falls[i] + ")";
                }
                _quiet = true;
                _on[i].Checked = _config.Get(Keys[i], "0") == "1";
                _quiet = false;
            }
        }

        private void OnToggled(int row)
        {
            if (_quiet) return;
            _config.Set(Keys[row], _on[row].Checked ? "1" : "0");
            _config.Save();
        }

        private void OnClear(int row)
        {
            string p = Path.Combine(CueDir(), Files[row]);
            try
            {
                if (File.Exists(p)) File.Delete(p);
            }
            catch (Exception ex)
            {
                MessageBox.Show(this, "Could not delete " + Files[row] + ":\n" + ex.Message,
                    "Interface Sounds", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return;
            }
            _info.Text = Names[row] + ": back to default.";
            RefreshAll();
        }

        private void OnBrowse(int row)
        {
            using (var dlg = new OpenFileDialog())
            {
                dlg.Title = "Pick a sound for " + Names[row];
                dlg.Filter = "WAV files (*.wav)|*.wav|All files (*.*)|*.*";
                if (dlg.ShowDialog(this) != DialogResult.OK)
                    return;
                string why;
                if (!SfxSwapForm.IsPcm16Wav(dlg.FileName, out why))
                {
                    MessageBox.Show(this, "That file cannot be used:\n\n" + why +
                        "\n\nUse 16-bit PCM mono/stereo WAV (any sample rate).",
                        "Interface Sounds", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                    return;
                }
                try
                {
                    Directory.CreateDirectory(CueDir());
                    File.Copy(dlg.FileName, Path.Combine(CueDir(), Files[row]), true);
                }
                catch (Exception ex)
                {
                    MessageBox.Show(this, "Could not install the file:\n" + ex.Message,
                        "Interface Sounds", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                    return;
                }
                _config.Set(Keys[row], "1");
                _config.Save();
                _info.Text = Names[row] + " will play the new sound.";
                RefreshAll();
            }
        }
    }
}
