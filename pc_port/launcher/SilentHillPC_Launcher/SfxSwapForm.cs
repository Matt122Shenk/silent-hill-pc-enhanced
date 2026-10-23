/* SPDX-License-Identifier: GPL-3.0-or-later */
using System;
using System.Drawing;
using System.IO;
using System.Text;
using System.Windows.Forms;

namespace SilentHillPC_Launcher
{
    /// <summary>Gunshot sound swaps: one .wav per weapon (16-bit PCM), copied
    /// into gamedata/load/SND/ as BANK.NNN.wav (NNN = the fire sample: 001
    /// everywhere except RIFEL.004 and HYPER.002), plus a per-weapon enable flag.
    /// Applies on the next bank load (equip/map) — no restart needed. Opened
    /// from the Audio tool's File menu; one reusable window like it.</summary>
    internal sealed class SfxSwapForm : Form
    {
        private static SfxSwapForm s_open;

        private static readonly string[] Names = { "Handgun", "Rifle", "Shotgun", "Blaster" };
        private static readonly string[] Banks = { "PISTOL", "RIFEL", "SHOTGUN", "HYPER" };
        // Fire-sample number inside each bank (verified against the VAB tone
        // tables: note 48 plays VAG#1 everywhere except RIFEL (#4) and HYPER (#2).
        private static readonly string[] Samples = { "001", "004", "001", "002" };
        private static readonly string[] Keys  = { "sfx_handgun", "sfx_rifle", "sfx_shotgun", "sfx_blaster" };

        private readonly string _gameRoot;
        private readonly ConfigManager _config;
        private readonly Label[] _file = new Label[4];
        private readonly CheckBox[] _on = new CheckBox[4];
        private readonly Label _info = new Label();
        private readonly Label _loose = new Label();
        private readonly Button _looseBtn = new Button();
        private bool _quiet;

        public static void ShowTool(IWin32Window owner, string gameRoot, ConfigManager config)
        {
            if (string.IsNullOrEmpty(gameRoot) || config == null)
                return;
            SfxSwapForm f = s_open;
            if (f == null || f.IsDisposed)
            {
                f = new SfxSwapForm(gameRoot, config);
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

        private SfxSwapForm(string gameRoot, ConfigManager config)
        {
            _gameRoot = gameRoot;
            _config = config;

            Text = "Gunshot Sounds";
            ClientSize = new Size(620, 320);
            FormBorderStyle = FormBorderStyle.FixedDialog;
            MaximizeBox = false;
            MinimizeBox = false;
            StartPosition = FormStartPosition.CenterParent;
            ShowInTaskbar = false;

            var head = new Label
            {
                Location = new Point(12, 12),
                Size = new Size(596, 30),
                Text = "Pick a .wav per weapon (16-bit PCM mono/stereo). Applies on the next bank load (equip/map)."
            };
            Controls.Add(head);

            for (int i = 0; i < 4; i++)
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
                tip.SetToolTip(clear, "Remove the replacement (back to the disc sound).");
                Controls.Add(name);
                Controls.Add(_file[i]);
                Controls.Add(browse);
                Controls.Add(clear);
                Controls.Add(_on[i]);
            }

            _loose.Location = new Point(12, 236);
            _loose.Size = new Size(380, 22);
            Controls.Add(_loose);

            _looseBtn.Location = new Point(396, 232);
            _looseBtn.Size = new Size(100, 26);
            _looseBtn.Text = "Enable";
            _looseBtn.Click += (s, e) =>
            {
                _config.Set("allow_loose_files", "1");
                _config.Save();
                RefreshAll();
            };
            Controls.Add(_looseBtn);

            _info.Location = new Point(12, 262);
            _info.Size = new Size(596, 18);
            Controls.Add(_info);

            var close = new Button { Location = new Point(520, 284), Size = new Size(88, 26), Text = "Close", DialogResult = DialogResult.OK };
            Controls.Add(close);
            AcceptButton = close;

            RefreshAll();
            Loc.Apply(this);
        }

        private string SndDir()
        {
            return Path.Combine(_gameRoot, "gamedata", "load", "SND");
        }

        private string CurrentFile(int row)
        {
            string dot = Path.Combine(SndDir(), Banks[row] + "." + Samples[row] + ".wav");
            string under = Path.Combine(SndDir(), Banks[row] + "_" + Samples[row] + ".wav");
            if (File.Exists(dot)) return dot;
            if (File.Exists(under)) return under;
            return null;
        }

        private void RefreshAll()
        {
            for (int i = 0; i < 4; i++)
                RefreshRow(i);
            bool loose = _config.Get("allow_loose_files", "0") == "1";
            _loose.Text = "Loose files: " + (loose ? "ON" : "OFF (swaps will not load)");
            _looseBtn.Visible = !loose;
        }

        private void RefreshRow(int i)
        {
            string f = CurrentFile(i);
            if (f == null)
            {
                _file[i].Text = "(default sound)";
            }
            else
            {
                long kb = new FileInfo(f).Length / 1024;
                _file[i].Text = Path.GetFileName(f) + " · " + kb + " KB";
            }
            _quiet = true;
            _on[i].Checked = _config.Get(Keys[i], "1") == "1";
            _quiet = false;
        }

        private void OnToggled(int row)
        {
            if (_quiet) return;
            _config.Set(Keys[row], _on[row].Checked ? "1" : "0");
            _config.Save();
        }

        private void OnClear(int row)
        {
            string dir = SndDir();
            bool gone = false;
            var names = new System.Collections.Generic.List<string>();
            names.Add(Banks[row] + "." + Samples[row] + ".wav");
            names.Add(Banks[row] + "_" + Samples[row] + ".wav");
            // Legacy slot from before the per-bank mapping was known.
            names.Add(Banks[row] + ".001.wav");
            names.Add(Banks[row] + "_001.wav");
            foreach (string name in names)
            {
                string p = Path.Combine(dir, name);
                try { if (File.Exists(p)) { File.Delete(p); gone = true; } }
                catch (Exception ex)
                {
                    MessageBox.Show(this, "Could not delete " + name + ":\n" + ex.Message,
                        "Gunshot Sounds", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                    return;
                }
            }
            _info.Text = gone ? Banks[row] + ": back to the disc sound." : "Nothing to clear.";
            RefreshRow(row);
        }

        private void OnBrowse(int row)
        {
            using (var dlg = new OpenFileDialog())
            {
                dlg.Title = "Pick a replacement for the " + Names[row];
                dlg.Filter = "WAV files (*.wav)|*.wav|All files (*.*)|*.*";
                if (dlg.ShowDialog(this) != DialogResult.OK)
                    return;
                string why;
                if (!IsPcm16Wav(dlg.FileName, out why))
                {
                    MessageBox.Show(this, "That file cannot be used:\n\n" + why +
                        "\n\nUse 16-bit PCM mono/stereo WAV (any sample rate).",
                        "Gunshot Sounds", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                    return;
                }
                try
                {
                    Directory.CreateDirectory(SndDir());
                    string dst = Path.Combine(SndDir(), Banks[row] + "." + Samples[row] + ".wav");
                    string twin = Path.Combine(SndDir(), Banks[row] + "_" + Samples[row] + ".wav");
                    if (File.Exists(twin)) File.Delete(twin);
                    File.Copy(dlg.FileName, dst, true);
                }
                catch (Exception ex)
                {
                    MessageBox.Show(this, "Could not install the file:\n" + ex.Message,
                        "Gunshot Sounds", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                    return;
                }
                _config.Set(Keys[row], "1");
                _config.Save();
                _info.Text = Names[row] + " will play the new sound (replacing on next bank load).";
                RefreshAll();
            }
        }

        internal static bool IsPcm16Wav(string path, out string why)
        {
            why = "";
            try
            {
                FileInfo fi = new FileInfo(path);
                if (!fi.Exists) { why = "File not found."; return false; }
                if (fi.Length > 8 * 1024 * 1024) { why = "File is over 8 MB."; return false; }
                if (fi.Length < 44) { why = "File is too small to be a WAV."; return false; }
                byte[] d = File.ReadAllBytes(path);
                if (d[0] != 'R' || d[1] != 'I' || d[2] != 'F' || d[3] != 'F' ||
                    d[8] != 'W' || d[9] != 'A' || d[10] != 'V' || d[11] != 'E')
                {
                    why = "Not a RIFF/WAVE file.";
                    return false;
                }
                int fmt = -1;
                for (int i = 12; i + 8 < d.Length && i < 1024; i++)
                {
                    if (d[i] == 'f' && d[i + 1] == 'm' && d[i + 2] == 't' && d[i + 3] == ' ')
                    {
                        fmt = i;
                        break;
                    }
                }
                if (fmt < 0 || fmt + 24 > d.Length) { why = "No fmt chunk found."; return false; }
                int audioFormat = d[fmt + 8] | (d[fmt + 9] << 8);
                int channels = d[fmt + 10] | (d[fmt + 11] << 8);
                int bits = d[fmt + 22] | (d[fmt + 23] << 8);
                if (audioFormat != 1) { why = "Not PCM (format " + audioFormat + ")."; return false; }
                if (channels < 1 || channels > 2) { why = channels + " channels (need 1-2)."; return false; }
                if (bits != 16) { why = bits + "-bit (need 16-bit)."; return false; }
                return true;
            }
            catch (Exception ex)
            {
                why = ex.Message;
                return false;
            }
        }
    }
}
