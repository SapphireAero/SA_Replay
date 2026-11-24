using System;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;

namespace ReplayCenter
{
    internal static class ReplayFileName
    {
        private const string StartFormat = "MM.dd'：'HH.mm";
        private const string EndFormat = "HH.mm";

        public static string MoveCompleted(string path, string mapName)
        {
            // 插件在首个游戏帧创建 .partial，完整写入后原地改名，保留创建和写入时间。
            var recording = new FileInfo(path);
            DateTime start = recording.CreationTimeUtc;
            DateTime end = recording.LastWriteTimeUtc;
            if (end < start)
                end = start;

            string stem = start.ToLocalTime().ToString(StartFormat, CultureInfo.InvariantCulture) + "-" +
                end.ToLocalTime().ToString(EndFormat, CultureInfo.InvariantCulture) + " - " + CleanMapName(mapName);
            for (int number = 1; ; number++)
            {
                string suffix = number == 1 ? string.Empty : " (" + number.ToString(CultureInfo.InvariantCulture) + ")";
                string fileName = stem + suffix + recording.Extension;
                string candidate = Path.Combine(recording.DirectoryName, fileName);
                if (File.Exists(candidate + ReplayDetails.BattleSuffix))
                    continue;
                try
                {
                    File.Move(path, candidate);
                }
                catch (IOException) when (File.Exists(candidate) || Directory.Exists(candidate))
                {
                    // 同一分钟开局、结束的同一张地图也保留各自的文件。
                    continue;
                }
                string stats = path + ReplayDetails.BattleSuffix;
                if (File.Exists(stats))
                {
                    try { File.Move(stats, candidate + ReplayDetails.BattleSuffix); }
                    catch (IOException) { File.Move(candidate, path); throw; }
                    catch (UnauthorizedAccessException) { File.Move(candidate, path); throw; }
                }
                return fileName;
            }
        }

        private static string CleanMapName(string name)
        {
            string title = Regex.Replace(name ?? string.Empty, @"^\s*\(\d+(?:-\d+)?\)\s*", string.Empty);
            char[] invalid = Path.GetInvalidFileNameChars();
            string cleaned = new string(title.Select(c => Array.IndexOf(invalid, c) >= 0 ? '-' : c).ToArray())
                .Trim().TrimEnd('.');
            return string.IsNullOrWhiteSpace(cleaned) ? "Unknown Map" : cleaned;
        }
    }
}
