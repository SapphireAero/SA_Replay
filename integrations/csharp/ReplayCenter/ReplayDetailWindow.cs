// SPDX-License-Identifier: GPL-3.0-only

using System;
using System.Drawing;
using System.Globalization;
using System.IO;
using System.Threading.Tasks;
using System.Windows.Forms;
using RAReplay.Client;

namespace ReplayCenter
{
    internal sealed class ReplayDetailWindow : Form
    {
        private readonly string game;
        private readonly string path;
        private readonly Label mapName = new Label { Dock = DockStyle.Top, AutoSize = true };
        private readonly Label noPreview = new Label { Dock = DockStyle.Top, Height = 36, TextAlign = ContentAlignment.MiddleLeft };
        private readonly PictureBox preview = new PictureBox { Dock = DockStyle.Top, SizeMode = PictureBoxSizeMode.Zoom };
        private readonly Label duration = new Label { Dock = DockStyle.Top, AutoSize = true, Padding = new Padding(0, 0, 0, 8) };
        private readonly Label gameDuration = new Label { Dock = DockStyle.Top, AutoSize = true, Padding = new Padding(0, 0, 0, 12) };
        private readonly Label status = new Label { AutoSize = true, Dock = DockStyle.Bottom, Padding = new Padding(0, 8, 0, 0) };
        private readonly Button analyze = new Button { Text = "重演生成战报", AutoSize = true, Enabled = false };
        private readonly ReplayPlayerDetailPanel playerDetails = new ReplayPlayerDetailPanel { Dock = DockStyle.Fill };
        private readonly ListView players = new ListView
        {
            View = View.Details, Dock = DockStyle.Fill, HeaderStyle = ColumnHeaderStyle.Nonclickable,
            FullRowSelect = true, HideSelection = false, MultiSelect = false, UseCompatibleStateImageBehavior = false
        };
        private bool generating;

        public ReplayDetailWindow(string game, string path)
        {
            this.game = game;
            this.path = path;
            Text = "录像详情";
            StartPosition = FormStartPosition.CenterParent;
            AutoScaleMode = AutoScaleMode.Dpi;
            Font = new Font("Microsoft YaHei UI", 9F);
            ClientSize = new Size(1060, 650);
            MinimumSize = new Size(940, 550);

            var layout = new TableLayoutPanel { Dock = DockStyle.Fill, Padding = new Padding(16), ColumnCount = 2 };
            layout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 31));
            layout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 69));
            layout.RowStyles.Add(new RowStyle(SizeType.Percent, 100));

            mapName.Font = new Font(Font.FontFamily, 15F, FontStyle.Bold);
            mapName.MaximumSize = new Size(280, 0);
            mapName.Padding = new Padding(0, 0, 8, 12);
            mapName.Text = Path.GetFileNameWithoutExtension(path);
            noPreview.Text = "正在读取地图预览…";
            var visual = new Panel { Dock = DockStyle.Fill, Padding = new Padding(0, 0, 14, 0) };
            preview.AccessibleName = "地图缩略图";
            preview.Visible = false;
            visual.Controls.Add(preview);
            visual.Controls.Add(noPreview);
            visual.Controls.Add(mapName);
            visual.Resize += (sender, args) => FitPreview();
            layout.Controls.Add(visual, 0, 0);

            duration.Text = "游戏时长  —";
            gameDuration.Text = "游戏实际时长  —";
            players.Columns.Add("玩家 ID", 131);
            players.Columns.Add("阵营", 108);
            players.Columns.Add("总击杀", 66, HorizontalAlignment.Right);
            players.Columns.Add("总损失", 66, HorizontalAlignment.Right);
            players.Columns.Add("总生产", 66, HorizontalAlignment.Right);
            players.Columns.Add("平均每游戏分钟指令数", 160, HorizontalAlignment.Right);
            var actions = new FlowLayoutPanel
            {
                Dock = DockStyle.Bottom, FlowDirection = FlowDirection.RightToLeft,
                AutoSize = true, Padding = new Padding(0, 12, 0, 0)
            };
            var back = new Button { Text = "返回", AutoSize = true, DialogResult = DialogResult.Cancel };
            actions.Controls.Add(back);
            actions.Controls.Add(analyze);
            CancelButton = back;
            var reportContent = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, RowCount = 2 };
            reportContent.RowStyles.Add(new RowStyle(SizeType.Percent, 41));
            reportContent.RowStyles.Add(new RowStyle(SizeType.Percent, 59));
            reportContent.Controls.Add(players, 0, 0);
            reportContent.Controls.Add(playerDetails, 0, 1);
            var report = new Panel { Dock = DockStyle.Fill };
            report.Controls.Add(reportContent);
            report.Controls.Add(status);
            report.Controls.Add(actions);
            report.Controls.Add(gameDuration);
            report.Controls.Add(duration);
            layout.Controls.Add(report, 1, 0);
            Controls.Add(layout);

            Shown += async (sender, args) => await Open();
            players.SelectedIndexChanged += (sender, args) => playerDetails.ShowPlayer(SelectedPlayer);
            analyze.Click += async (sender, args) => await Generate();
            FormClosing += (sender, args) => { if (generating) args.Cancel = true; };
            FormClosed += (sender, args) => preview.Image?.Dispose();
        }

        private ReplayDetails.Player SelectedPlayer => players.SelectedItems.Count == 0
            ? null : players.SelectedItems[0].Tag as ReplayDetails.Player;

        private async Task Open()
        {
            status.Text = "正在读取录像统计…";
            try
            {
                ReplayArchive archive = ReplayArchive.Open(path);
                mapName.Text = GameSession.RecordedMapName(archive.Manifest);
                await Task.WhenAll(LoadPreview(archive), LoadReport(archive));
            }
            catch (Exception error)
            {
                if (IsDisposed) return;
                status.Text = "无法读取录像详情。 " + error.Message;
                if (preview.Image == null) noPreview.Text = "无地图预览";
            }
        }

        private async Task LoadPreview(ReplayArchive archive)
        {
            Bitmap bitmap;
            try { bitmap = await Task.Run(() => ReplayMapPreview.Load(game, archive.Manifest)); }
            catch (Exception error) when (error is IOException || error is InvalidDataException ||
                error is UnauthorizedAccessException || error is FormatException || error is ArgumentException ||
                error is System.Reflection.TargetInvocationException || error is System.Runtime.InteropServices.ExternalException ||
                error is TypeLoadException || error is BadImageFormatException || error is NotSupportedException)
            {
                bitmap = null;
            }
            if (IsDisposed)
            {
                bitmap?.Dispose();
                return;
            }
            preview.Image = bitmap;
            FitPreview();
            preview.Visible = bitmap != null;
            noPreview.Visible = bitmap == null;
            noPreview.Text = "地图文件无可用缩略图";
        }

        private void FitPreview()
        {
            if (preview.Image == null) return;
            var parent = preview.Parent;
            int width = parent.ClientSize.Width - parent.Padding.Horizontal;
            int height = Math.Max(1, parent.ClientSize.Height - mapName.Height - parent.Padding.Vertical);
            preview.Height = Math.Min(height, Math.Max(1, width * preview.Image.Height / preview.Image.Width));
        }

        private async Task<bool> LoadReport(ReplayArchive archive)
        {
            ReplayDetails details = await Task.Run(() => ReplayDetails.Load(archive, game));
            if (IsDisposed) return false;
            duration.Text = "游戏时长（原局经过）  " + details.Duration;
            gameDuration.Text = "游戏实际时长  " + details.GameDuration;
            int? selectedHouse = SelectedPlayer?.House;
            players.BeginUpdate();
            try
            {
                players.Items.Clear();
                foreach (ReplayDetails.Player player in details.Players)
                {
                    var row = new ListViewItem(new[]
                    {
                        player.Name, string.IsNullOrWhiteSpace(player.Faction) ? "—" : player.Faction,
                        Count(player.Kills), Count(player.Losses), Count(player.Produced),
                        player.Rate.ToString("N1", CultureInfo.CurrentCulture)
                    }) { Tag = player };
                    players.Items.Add(row);
                    if (player.House == selectedHouse) row.Selected = true;
                }
                if (players.SelectedItems.Count == 0 && players.Items.Count > 0) players.Items[0].Selected = true;
            }
            finally { players.EndUpdate(); }
            playerDetails.ShowPlayer(SelectedPlayer);
            status.Text = details.HasDetailedStats
                ? "战损按引擎记录。每 3,600 个逻辑帧为 1 游戏分钟。"
                : "重演至末帧可以补齐战斗、生产和经济统计。";
            analyze.Text = details.HasBattleStats ? "补齐详细统计" : "重演生成战报";
            analyze.Enabled = archive.HasPlayerInputEffects && !details.HasDetailedStats && !generating;
            return details.HasDetailedStats;
        }

        private async Task Generate()
        {
            generating = true;
            analyze.Enabled = false;
            status.Text = "正在加速重演。游戏退出后载入战报…";
            bool ready = false;
            try
            {
                int code = await Task.Run(() => GameSession.Play(game, path, new PlaybackTimeline(), fastReport: true));
                ready = await LoadReport(ReplayArchive.Open(path));
                if (!ready)
                    status.Text = code == 0 ? "未能取得末帧战报。请确认录像已完整重演且目录可写。"
                        : "重演未完成。游戏退出代码 " + code + "。";
            }
            catch (Exception error) { status.Text = error.Message; }
            finally
            {
                generating = false;
                analyze.Enabled = !ready;
            }
        }

        private static string Count(int? value) => value?.ToString("N0", CultureInfo.CurrentCulture) ?? "—";
    }
}
