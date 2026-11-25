// SPDX-License-Identifier: GPL-3.0-only

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using System.Windows.Forms;
using RAReplay.Client;

namespace ReplayCenter
{
    internal sealed class ReplayWindow : Form
    {
        private readonly Label folder = new Label { AutoSize = true, Margin = new Padding(0, 0, 0, 6) };
        private readonly Label status = new Label { AutoSize = true, Margin = new Padding(0, 8, 0, 0) };
        private readonly ListView list = new ListView
        {
            Dock = DockStyle.Fill, View = View.Details, FullRowSelect = true,
            HideSelection = false, MultiSelect = false, UseCompatibleStateImageBehavior = false
        };
        private readonly Button choose = new Button { Text = "选择游戏目录", AutoSize = true };
        private readonly Button recording = new Button { Text = "开启自动录制", AutoSize = true };
        private readonly Button refresh = new Button { Text = "刷新", AutoSize = true };
        private readonly Button browse = new Button { Text = "打开录像文件…", AutoSize = true };
        private readonly Button play = new Button { Text = "播放选中录像", AutoSize = true };
        private readonly Button openFolder = new Button { Text = "录像文件夹", AutoSize = true };
        private readonly SeekTrackBar timelineBar = new SeekTrackBar
        {
            Dock = DockStyle.Top, TickStyle = TickStyle.None, AutoSize = false, Height = 29,
            Enabled = false, AccessibleName = "回放时间条"
        };
        private static readonly int[] PlaybackRates = { 1, 2, 4, 8, 16 };
        private readonly ComboBox ratePicker = new ComboBox
        {
            Dock = DockStyle.Top, DropDownStyle = ComboBoxStyle.DropDownList, AccessibleName = "播放倍速"
        };
        private readonly Button pause = new Button { Text = "暂停", Dock = DockStyle.Top, AutoSize = true };
        private readonly TableLayoutPanel playbackControls = new TableLayoutPanel
        {
            Dock = DockStyle.Top, Height = 35, ColumnCount = 3, Visible = false
        };
        private readonly Timer timelineTimer = new Timer { Interval = 100 };
        private PlaybackTimeline timeline;
        private string root;
        private bool busy;
        private bool recordingEnabled;

        public ReplayWindow(string programDirectory)
        {
            Text = "SA_Replay · 心灵终结回放";
            StartPosition = FormStartPosition.CenterScreen;
            Font = new Font("Microsoft YaHei UI", 9F);
            AutoScaleMode = AutoScaleMode.Dpi;
            ClientSize = new Size(830, 470);
            MinimumSize = new Size(650, 380);

            var layout = new TableLayoutPanel
            {
                Dock = DockStyle.Fill, ColumnCount = 1, RowCount = 5, Padding = new Padding(14)
            };
            layout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
            layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            layout.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
            layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            var bar = new FlowLayoutPanel { AutoSize = true, Dock = DockStyle.Fill, Margin = new Padding(0, 0, 0, 8) };
            bar.Controls.AddRange(new Control[] { choose, recording, refresh, play, browse, openFolder });
            list.Columns.Add("地图", 245);
            list.Columns.Add("录制时间", 148);
            list.Columns.Add("格式", 70);
            list.Columns.Add("文件", 310);
            layout.Controls.Add(folder, 0, 0);
            layout.Controls.Add(bar, 0, 1);
            layout.Controls.Add(list, 0, 2);
            playbackControls.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
            playbackControls.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 100));
            playbackControls.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 86));
            playbackControls.Controls.Add(timelineBar, 0, 0);
            playbackControls.Controls.Add(ratePicker, 1, 0);
            playbackControls.Controls.Add(pause, 2, 0);
            ratePicker.Items.AddRange(PlaybackRates.Select(rate => (object)(rate + "×")).ToArray());
            ratePicker.SelectedIndex = 0;
            layout.Controls.Add(playbackControls, 0, 3);
            layout.Controls.Add(status, 0, 4);
            Controls.Add(layout);

            if (GameSession.IsGameDirectory(programDirectory)) root = programDirectory;
            choose.Click += async (sender, args) => await ChooseDirectory();
            recording.Click += async (sender, args) => await ToggleRecording();
            refresh.Click += async (sender, args) => await Reload();
            play.Click += async (sender, args) => await PlaySelected();
            list.DoubleClick += (sender, args) => OpenSelectedDetails();
            list.SelectedIndexChanged += (sender, args) => UpdateButtons();
            browse.Click += async (sender, args) => await OpenReplay();
            openFolder.Click += (sender, args) => OpenReplayFolder();
            timelineTimer.Tick += (sender, args) => RefreshTimeline();
            pause.Click += (sender, args) =>
            {
                if (timeline == null) return;
                timeline.Read(out _, out _, out _, out _, out bool paused);
                timeline.SetPaused(!paused);
                RefreshTimeline();
            };
            ratePicker.SelectionChangeCommitted += (sender, args) =>
            {
                int selected = ratePicker.SelectedIndex;
                if (selected >= 0) timeline?.SetRate(PlaybackRates[selected]);
            };
            timelineBar.SeekRequested += (sender, args) => timeline?.Seek(timelineBar.Value);
            Shown += async (sender, args) => await Reload();
            FormClosing += (sender, args) => { if (busy) args.Cancel = true; };
            FormClosed += (sender, args) => timelineTimer.Dispose();
            UpdateButtons();
        }

        private async Task ChooseDirectory()
        {
            using (var picker = new FolderBrowserDialog
            {
                Description = "选择包含 gamemd.exe 的心灵终结安装目录",
                ShowNewFolderButton = false,
                SelectedPath = root ?? string.Empty
            })
            {
                if (picker.ShowDialog(this) != DialogResult.OK) return;
                if (!GameSession.IsGameDirectory(picker.SelectedPath))
                {
                    ShowError("该目录需要包含 gamemd.exe、Syringe.exe 和 Resources\\ClientDefinitions.ini。");
                    return;
                }
                root = picker.SelectedPath;
            }
            await Reload();
        }

        private async Task ToggleRecording()
        {
            bool enable = !recordingEnabled;
            if (enable && MessageBox.Show(this,
                "开启后，原版客户端开局时会调用 SA_Replay.exe，并保存录像。可以在这里随时关闭自动录制。\n\n请先确认游戏和原版启动器都已退出。",
                "开启自动录制", MessageBoxButtons.OKCancel, MessageBoxIcon.Information) != DialogResult.OK)
                return;

            try { GameSession.SetAutoRecording(root, enable); }
            catch (Exception error) { ShowError(error.Message); }
            await Reload();
        }

        private async Task Reload()
        {
            if (busy) return;
            list.Items.Clear();
            if (root == null)
            {
                folder.Text = "请选择心灵终结游戏目录";
                status.Text = "选择目录后可查看该目录中的 V2 录像。";
                UpdateButtons();
                return;
            }

            folder.Text = root;
            status.Text = "正在读取录像…";
            busy = true;
            UpdateButtons();
            try
            {
                recordingEnabled = GameSession.IsAutoRecordingEnabled(root);
                List<ReplayEntry> entries = await Task.Run(() => ReadEntries(root));
                list.Items.AddRange(entries.Select(entry => entry.Item()).ToArray());
                string summary = recordingEnabled ? "自动录制已开启" : "自动录制已关闭";
                if (GameSession.UsesGdi(root)) summary += "  ·  当前 GDI 模式不显示游戏内回放面板";
                status.Text = entries.Count == 0 ? "暂无录像  ·  " + summary : summary;
            }
            catch (Exception error)
            {
                status.Text = "读取失败";
                ShowError(error.Message);
            }
            finally
            {
                busy = false;
                UpdateButtons();
            }
        }

        private static List<ReplayEntry> ReadEntries(string game)
        {
            string directory = Path.Combine(game, "Replays");
            if (!Directory.Exists(directory)) return new List<ReplayEntry>();
            var entries = new List<ReplayEntry>();
            foreach (string path in Directory.EnumerateFiles(directory, "*.yrrep")
                .Where(path => Path.GetExtension(path).Equals(".yrrep", StringComparison.OrdinalIgnoreCase)))
            {
                try
                {
                    ReplayArchive archive = ReplayArchive.Open(path);
                    entries.Add(new ReplayEntry(path, archive.Manifest.MapName,
                        DateTimeOffset.FromUnixTimeSeconds(archive.Manifest.RecordedAtUnixSeconds).LocalDateTime,
                        "V" + archive.StreamVersion, archive.HasPlayerInputEffects));
                }
                catch (Exception error) when (error is IOException || error is InvalidDataException ||
                    error is UnauthorizedAccessException || error is ArgumentException)
                {
                    entries.Add(new ReplayEntry(path, "格式不受支持或文件损坏",
                        File.GetLastWriteTime(path), "—", false));
                }
            }
            return entries.OrderByDescending(entry => entry.Time).ToList();
        }

        private async Task OpenReplay()
        {
            using (var picker = new OpenFileDialog
            {
                Title = "打开 V2 录像",
                Filter = "回放录像 (*.yrrep)|*.yrrep",
                InitialDirectory = Directory.Exists(Path.Combine(root, "Replays")) ? Path.Combine(root, "Replays") : root
            })
            {
                if (picker.ShowDialog(this) == DialogResult.OK) await Play(picker.FileName);
            }
        }

        private async Task PlaySelected()
        {
            ReplayEntry entry = SelectedEntry;
            if (entry != null && entry.Playable && !busy) await Play(entry.Path);
        }

        private void OpenSelectedDetails()
        {
            ReplayEntry entry = SelectedEntry;
            if (entry == null || busy || root == null) return;
            using (var details = new ReplayDetailWindow(root, entry.Path))
                details.ShowDialog(this);
        }

        private async Task Play(string path)
        {
            busy = true;
            status.Text = "正在校验并播放录像…";
            UpdateButtons();
            int? result = null;
            var controller = new PlaybackTimeline();
            timeline = controller;
            ratePicker.SelectedIndex = 0;
            pause.Text = "暂停";
            playbackControls.Visible = true;
            timelineTimer.Start();
            try { result = await Task.Run(() => GameSession.Play(root, path, controller)); }
            catch (Exception error) { ShowError(error.Message); }
            finally
            {
                timelineTimer.Stop();
                timelineBar.Enabled = playbackControls.Visible = false;
                timeline = null;
                busy = false;
            }
            await Reload();
            if (result.HasValue && result.Value != 0)
                status.Text = "游戏退出代码 " + result.Value + "。请查看游戏目录下的 syringe.log 和 debug 日志。";
        }

        private void RefreshTimeline()
        {
            if (timeline == null) return;
            timeline.Read(out int frame, out int total, out int seeking, out int rate, out bool paused);
            pause.Text = paused ? "继续" : "暂停";
            int rateIndex = Array.IndexOf(PlaybackRates, rate);
            if (!ratePicker.DroppedDown && rateIndex >= 0 && ratePicker.SelectedIndex != rateIndex)
                ratePicker.SelectedIndex = rateIndex;
            timelineBar.Enabled = frame >= 0 && total > 1;
            if (total > 0 && !timelineBar.IsDragging)
            {
                timelineBar.Maximum = total - 1;
                timelineBar.LargeChange = Math.Max(1, total / 100);
                timelineBar.Value = Math.Max(0, Math.Min(seeking >= 0 ? seeking : frame, total - 1));
            }
            if (timelineBar.IsDragging)
                status.Text = "松开鼠标定位到第 " + (timelineBar.Value + 1) + " 帧";
            else if (frame < 0)
                status.Text = "正在启动或重新载入录像…";
            else if (seeking >= 0)
                status.Text = "正在定位到第 " + (seeking + 1) + " 帧 · 当前第 " + (frame + 1) + " 帧";
            else
                status.Text = (paused ? "已暂停" : "回放时间条") + " · " + (frame + 1) + " / " + total + " 帧 · " + rate + "×";
        }

        private void OpenReplayFolder()
        {
            try
            {
                string directory = Directory.CreateDirectory(Path.Combine(root, "Replays")).FullName;
                using (Process.Start(new ProcessStartInfo(directory) { UseShellExecute = true })) { }
            }
            catch (Exception error) { ShowError(error.Message); }
        }

        private ReplayEntry SelectedEntry => list.SelectedItems.Count == 0 ? null : (ReplayEntry)list.SelectedItems[0].Tag;

        private void UpdateButtons()
        {
            choose.Enabled = !busy;
            recording.Text = recordingEnabled ? "关闭自动录制" : "开启自动录制";
            recording.Enabled = refresh.Enabled = browse.Enabled = openFolder.Enabled = !busy && root != null;
            play.Enabled = !busy && SelectedEntry != null && SelectedEntry.Playable;
        }

        private void ShowError(string message)
        {
            MessageBox.Show(this, message, Text, MessageBoxButtons.OK, MessageBoxIcon.Warning);
        }

        private sealed class ReplayEntry
        {
            public readonly string Path;
            public readonly DateTime Time;
            public readonly bool Playable;
            private readonly string map;
            private readonly string version;

            public ReplayEntry(string path, string map, DateTime time, string version, bool playable)
            {
                Path = path;
                this.map = map;
                Time = time;
                this.version = version;
                Playable = playable;
            }

            public ListViewItem Item()
            {
                return new ListViewItem(new[] { map, Time.ToString("yyyy-MM-dd HH:mm:ss"),
                    version, System.IO.Path.GetFileName(Path) }) { Tag = this };
            }
        }
    }
}
