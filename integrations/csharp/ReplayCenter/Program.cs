// SPDX-License-Identifier: GPL-3.0-only

using System;
using System.IO;
using System.Windows.Forms;

namespace ReplayCenter
{
    internal static class Program
    {
        [STAThread]
        private static int Main(string[] args)
        {
            string game = AppDomain.CurrentDomain.BaseDirectory;
            try
            {
                if (args.Length > 0 && Path.GetFileName(args[0]).Equals("gamemd.exe",
                    StringComparison.OrdinalIgnoreCase))
                    return GameSession.RunLobbyMatch(game, args);

                if (args.Length != 0)
                    throw new ArgumentException("请直接打开 SA_Replay.exe，再选择游戏目录或录像。");

                Application.EnableVisualStyles();
                Application.SetCompatibleTextRenderingDefault(false);
                Application.Run(new ReplayWindow(game));
                return 0;
            }
            catch (Exception error)
            {
                MessageBox.Show(error.Message, "SA_Replay", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return 1;
            }
        }
    }
}
