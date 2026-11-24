using System.Drawing;
using System.Globalization;
using System.Windows.Forms;

namespace ReplayCenter
{
    internal sealed class ReplayPlayerDetailPanel : UserControl
    {
        private readonly Label caption = new Label
        {
            Dock = DockStyle.Top, AutoSize = true, Padding = new Padding(0, 7, 0, 8), Text = "选择玩家查看详细统计"
        };
        private readonly ListView battle = Table("项目", "单位", "建筑", "合计");
        private readonly ListView production = Table("类别", "累计生产", "末帧持有");
        private readonly ListView economy = Table("项目", "数值");

        public ReplayPlayerDetailPanel()
        {
            var tabs = new TabControl { Dock = DockStyle.Fill };
            AddTab(tabs, "战斗", battle);
            AddTab(tabs, "生产与存量", production);
            AddTab(tabs, "经济", economy);
            Controls.Add(tabs);
            Controls.Add(caption);
        }

        public void ShowPlayer(ReplayDetails.Player player)
        {
            battle.Items.Clear();
            production.Items.Clear();
            economy.Items.Clear();
            if (player == null)
            {
                caption.Text = "选择玩家查看详细统计";
                return;
            }

            var details = player.Details;
            string header = player.Name;
            if (!string.IsNullOrWhiteSpace(player.Faction)) header += " · " + player.Faction;
            if (details != null) header += " · " + details.Outcome;
            caption.Text = header + "   ·   指令总数 " + player.Commands.ToString("N0", CultureInfo.CurrentCulture);
            AddRow(battle, "击杀", Count(details?.UnitKills), Count(details?.BuildingKills), Count(player.Kills));
            AddRow(battle, "战损", Count(details?.UnitLosses), Count(details?.BuildingLosses), Count(player.Losses));
            if (details == null)
            {
                AddRow(production, "所有类型", Count(player.Produced), "—");
                AddRow(economy, "待重演补齐", "—");
                return;
            }

            string[] categories = { "步兵", "载具 / 舰船", "飞机", "建筑" };
            for (int i = 0; i < categories.Length; i++)
                AddRow(production, categories[i], Count(details.Produced[i]), Count(details.Owned[i]));
            AddRow(economy, "采矿收入", Count(details.Harvested));
            AddRow(economy, "累计支出", Count(details.Spent));
            AddRow(economy, "剩余资金", Count(details.Cash));
            AddRow(economy, "电力产出", Count(details.PowerOutput));
            AddRow(economy, "电力消耗", Count(details.PowerDrain));
        }

        private static ListView Table(params string[] headers)
        {
            var table = new ListView
            {
                View = View.Details, Dock = DockStyle.Fill, FullRowSelect = true,
                HeaderStyle = ColumnHeaderStyle.Nonclickable, UseCompatibleStateImageBehavior = false,
                MultiSelect = false, BorderStyle = BorderStyle.None
            };
            for (int i = 0; i < headers.Length; i++)
                table.Columns.Add(headers[i], i == 0 ? 154 : 100,
                    i == 0 ? HorizontalAlignment.Left : HorizontalAlignment.Right);
            return table;
        }

        private static void AddTab(TabControl tabs, string title, ListView table)
        {
            var page = new TabPage(title) { BackColor = SystemColors.Window, Padding = new Padding(4) };
            page.Controls.Add(table);
            tabs.TabPages.Add(page);
        }

        private static void AddRow(ListView table, params string[] cells) => table.Items.Add(new ListViewItem(cells));

        private static string Count(int? value) => value?.ToString("N0", CultureInfo.CurrentCulture) ?? "—";
    }
}
