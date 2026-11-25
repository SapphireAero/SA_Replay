// SPDX-License-Identifier: GPL-3.0-only

using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using Microsoft.Win32.SafeHandles;
using RAReplay.Client;

namespace ReplayCenter
{
    internal static class GameSession
    {
        private const string TargetHash = "7cd005d263fde203d9c84548200a057a8df61d724da3c6bd1e521eeb61cd0747";
        private const string LauncherName = "SA_Replay.exe";
        private const string LegacyLauncherName = "ReplayCenter.exe";
        private const string SyringeName = "Syringe.exe";
        private const string SnapshotFolder = "Replays\\MapSnapshots";
        private static readonly Encoding ByteEncoding = Encoding.GetEncoding(28591);

        public static bool IsGameDirectory(string root)
        {
            return File.Exists(Path.Combine(root, "gamemd.exe")) &&
                File.Exists(Path.Combine(root, SyringeName)) &&
                File.Exists(DefinitionsPath(root));
        }

        public static bool IsAutoRecordingEnabled(string root)
        {
            return IsReplayLauncher(ReadSettings(DefinitionsPath(root)).Get("GameLauncherExecutableName"));
        }

        private static bool IsReplayLauncher(string value)
        {
            return value.Equals(LauncherName, StringComparison.OrdinalIgnoreCase) ||
                value.Equals(LegacyLauncherName, StringComparison.OrdinalIgnoreCase);
        }

        public static bool UsesGdi(string root)
        {
            string file = Path.Combine(root, "ddraw.ini");
            return File.Exists(file) && Regex.IsMatch(File.ReadAllText(file),
                @"^\s*Renderer\s*=\s*gdi\s*(?:;[^\r\n]*)?$", RegexOptions.IgnoreCase | RegexOptions.Multiline);
        }

        public static void SetAutoRecording(string root, bool enabled)
        {
            using (Acquire(root))
            {
                EnsureIdle(root, true);
                if (enabled)
                {
                    ValidateTarget(root);
                    string ownExe = Path.GetFullPath(typeof(GameSession).Assembly.Location);
                    if (!string.Equals(ownExe, Path.GetFullPath(Path.Combine(root, LauncherName)),
                        StringComparison.OrdinalIgnoreCase))
                        throw new InvalidOperationException("请把交付包里的程序、插件和启动清单复制到游戏根目录，再从那里打开 SA_Replay.exe。");
                }

                string path = DefinitionsPath(root);
                byte[] original = File.ReadAllBytes(path);
                string expected = enabled ? LauncherName : SyringeName;
                string content = ByteEncoding.GetString(original);
                bool hasUtf8Bom = original.Length >= 3 && original[0] == 0xEF &&
                    original[1] == 0xBB && original[2] == 0xBF;
                string section = string.Empty;
                int matches = 0;
                string updated = Regex.Replace(content, @"[^\r\n]+", match =>
                {
                    string line = match.Value;
                    string sectionLine = hasUtf8Bom && match.Index == 0 ? line.Substring(3) : line;
                    string nextSection = SectionName(sectionLine.Trim());
                    if (nextSection != null) section = nextSection;
                    if (!section.Equals("Settings", StringComparison.OrdinalIgnoreCase)) return line;
                    Match property = Regex.Match(line, @"^(\s*GameLauncherExecutableName\s*=\s*)([^;]*?)(\s*(?:;.*)?)$",
                        RegexOptions.IgnoreCase);
                    if (!property.Success) return line;
                    matches++;
                    string current = property.Groups[2].Value.Trim();
                    if (current.Equals(expected, StringComparison.OrdinalIgnoreCase)) return line;
                    if (!IsReplayLauncher(current) && !current.Equals(SyringeName, StringComparison.OrdinalIgnoreCase))
                        throw new InvalidDataException("启动器配置当前指向 " + current + "，无法安全切换。");
                    return property.Groups[1].Value + expected + property.Groups[3].Value;
                });
                if (matches != 1)
                    throw new InvalidDataException("找不到唯一的游戏启动器配置。");
                if (string.Equals(content, updated, StringComparison.Ordinal)) return;

                string backup = path + ".rareplay.bak";
                AtomicWrite(path, ByteEncoding.GetBytes(updated),
                    enabled && !File.Exists(backup) ? backup : null, original);
            }
        }

        public static int RunLobbyMatch(string root, string[] args)
        {
            if (!IsGameDirectory(root) || !args.Skip(1).Any(arg => arg.Equals("-SPAWN", StringComparison.OrdinalIgnoreCase)))
                throw new InvalidOperationException("客户端的游戏启动参数无效。");
            using (Acquire(root))
            {
                EnsureIdle(root, false);
                string arguments = "\"gamemd.exe\" " + string.Join(" ", args.Skip(1).Select(QuoteArgument));
                string marker = Path.Combine(root, "rareplay-runtime.bin");
                Settings settings = ReadSettings(Path.Combine(root, "spawn.ini"));
                string mapName = settings.Get("UIMapName");

                if (!settings.IsMatch())
                {
                    if (File.Exists(marker))
                        throw new IOException("游戏目录已有待处理回放请求，无法启动另一局游戏。");
                    return StartGame(root, arguments);
                }

                RecordingSession session;
                try
                {
                    ValidateTarget(root);
                    string spawnMap = Path.Combine(root, "spawnmap.ini");
                    if (new FileInfo(spawnMap).Length > 15 * 1024 * 1024)
                        throw new InvalidDataException("对局地图过大。");
                    string mapPath = StoreSnapshot(root, File.ReadAllBytes(spawnMap));
                    session = ReplayClient.PrepareRecording(root, mapPath, mapName);
                }
                catch (Exception error)
                {
                    Log(root, "未开始录制。 " + error.Message);
                    if (File.Exists(marker)) throw;
                    return StartGame(root, arguments);
                }

                using (session)
                {
                    int code = StartGame(root, arguments);
                    ReportSavedRecording(root, session);
                    return code;
                }
            }
        }

        private static void ReportSavedRecording(string root, RecordingSession session)
        {
            string originalPath = session.ReplayPath;
            if (!File.Exists(originalPath))
            {
                Log(root, "本局未生成完整录像。请检查游戏插件日志。");
                return;
            }

            try
            {
                string name = ReplayFileName.MoveCompleted(originalPath, RecordedMapName(session.Manifest));
                Log(root, "已保存 " + name);
            }
            catch (Exception error) when (error is IOException || error is UnauthorizedAccessException ||
                error is ArgumentException || error is NotSupportedException)
            {
                Log(root, "录像已保存为 " + Path.GetFileName(originalPath) + "。重命名失败：" + error.Message);
            }
        }

        public static string RecordedMapName(ReplayLaunchManifest manifest)
        {
            string name = ReadIniSection(manifest.SpawnMapBytes, "Basic").Get("Name");
            return string.IsNullOrWhiteSpace(name) ? manifest.MapName : name;
        }

        internal sealed class RecordedPlayer
        {
            public string Name;
            public bool IsSpectator;
            public int Side;
        }

        public static string[] ReadFactionNames(string game)
        {
            try
            {
                string options = Path.Combine(game, "Resources", "GameOptions.ini");
                return ReadIniSection(File.ReadAllBytes(options), "General").Get("Sides")
                    .Split(',').Select(name => name.Trim()).ToArray();
            }
            catch (IOException) { return Array.Empty<string>(); }
            catch (UnauthorizedAccessException) { return Array.Empty<string>(); }
        }

        private static int ReadSide(string value) => int.TryParse(value, out int side) ? side : -1;

        public static Dictionary<int, RecordedPlayer> ReadRecordedPlayers(ReplayLaunchManifest manifest, int localHouse)
        {
            byte[] ini = manifest.SpawnIniBytes;
            Settings local = ReadIniSection(ini, "Settings");
            var result = new Dictionary<int, RecordedPlayer>();
            string localName = local.Get("Name");
            if (localHouse >= 0 && !string.IsNullOrWhiteSpace(localName))
                result[localHouse] = new RecordedPlayer
                {
                    Name = localName, IsSpectator = local.IsSet("IsSpectator"), Side = ReadSide(local.Get("Side"))
                };

            if (!int.TryParse(local.Get("PlayerCount"), out int humans) || humans < 1 || humans > 8)
                return result;
            var players = new List<(int Color, RecordedPlayer Player, bool IsLocal)>();
            var colors = new HashSet<int>();
            for (int i = 0; i < humans; i++)
            {
                Settings player = i == 0 ? local : ReadIniSection(ini, "Other" + i);
                string name = player.Get("Name");
                if (string.IsNullOrWhiteSpace(name) || !int.TryParse(player.Get("Color"), out int color) ||
                    color < 0 || !colors.Add(color))
                    return result;
                players.Add((color, new RecordedPlayer
                {
                    Name = name, IsSpectator = player.IsSet("IsSpectator"), Side = ReadSide(player.Get("Side"))
                }, i == 0));
            }

            // Mental Omega assigns Multi slots by game color, including spectator slots.
            // The archived local house lets us verify the mapping before naming other houses.
            players.Sort((left, right) => left.Color.CompareTo(right.Color));
            if (players.FindIndex(player => player.IsLocal) != localHouse)
                return result;
            for (int i = 0; i < humans; i++)
                result[i] = players[i].Player;

            if (int.TryParse(local.Get("AIPlayers"), out int bots) && bots > 0 && humans + bots <= 8)
            {
                Settings handicaps = ReadIniSection(ini, "HouseHandicaps");
                Settings countries = ReadIniSection(ini, "HouseCountries");
                for (int i = humans; i < humans + bots; i++)
                {
                    string slot = "Multi" + (i + 1);
                    if (int.TryParse(handicaps.Get(slot), out _))
                        result[i] = new RecordedPlayer { Name = "AI", Side = ReadSide(countries.Get(slot)) };
                }
            }
            return result;
        }

        public static int Play(string root, string replay, PlaybackTimeline timeline = null, bool fastReport = false)
        {
            using (Acquire(root))
            {
                EnsureIdle(root, true);
                ValidateTarget(root);

                string extra = ReadSettings(DefinitionsPath(root)).Get("ExtraCommandLineParams");
                int target = -1;
                while (true)
                {
                    int code;
                    using (PlaybackSession session = ReplayClient.PreparePlayback(root, replay,
                        Path.Combine(root, "gamemd.exe"), manifest => ResolveMap(root, manifest)))
                    {
                        int initialTarget = fastReport && session.FrameCount > 0
                            ? checked((int)session.FrameCount - 1) : target;
                        timeline?.Attach(session, initialTarget, allowElevatedGame: true);
                        try
                        {
                            code = StartGame(root, "\"gamemd.exe\" -SPAWN " + extra, allowElevation: true);
                        }
                        finally
                        {
                            target = timeline?.Detach() ?? -1;
                        }
                        if (code != 0 || target < 0 || target >= session.FrameCount)
                            return code;
                        EnsureIdle(root, true, waitForReplayExit: true);
                    }
                }
            }
        }

        private static string ResolveMap(string root, ReplayLaunchManifest manifest)
        {
            byte[] bytes = manifest.SpawnMapBytes;
            string relative = SnapshotName(bytes);
            if (relative.Equals(manifest.BaseMapRelativePath, StringComparison.OrdinalIgnoreCase) &&
                manifest.BaseMapHash.SequenceEqual(manifest.SpawnMapHash))
                return StoreSnapshot(root, bytes);
            return manifest.FindBaseMap(root);
        }

        private static string StoreSnapshot(string root, byte[] bytes)
        {
            if (bytes.Length == 0 || bytes.Length > 15 * 1024 * 1024)
                throw new InvalidDataException("对局地图为空或过大。");
            string replayDirectory = Path.Combine(root, "Replays");
            Directory.CreateDirectory(replayDirectory);
            RequireRegularPath(replayDirectory);
            string snapshots = Path.Combine(root, SnapshotFolder);
            Directory.CreateDirectory(snapshots);
            RequireRegularPath(snapshots);
            string path = Path.Combine(root, SnapshotName(bytes));
            if (!File.Exists(path))
                AtomicWrite(path, bytes, null, null);
            else
            {
                RequireRegularPath(path);
                if (!HashFile(path).Equals(Path.GetFileNameWithoutExtension(path), StringComparison.Ordinal))
                    throw new InvalidDataException("录像地图缓存已损坏。");
            }
            return path;
        }

        private static void RequireRegularPath(string path)
        {
            if ((File.GetAttributes(path) & FileAttributes.ReparsePoint) != 0)
                throw new IOException("录像目录或地图缓存是链接，无法写入。");
        }

        private static string SnapshotName(byte[] bytes)
        {
            using (SHA256 sha = SHA256.Create())
                return Path.Combine(SnapshotFolder, Hex(sha.ComputeHash(bytes)) + ".map");
        }

        private static void ValidateTarget(string root)
        {
            if (!IsGameDirectory(root))
                throw new DirectoryNotFoundException("请选择包含 gamemd.exe 和 Syringe.exe 的心灵终结目录。");
            if (!File.Exists(Path.Combine(root, "RAReplayPlugin.dll")))
                throw new FileNotFoundException("游戏目录缺少配套的 RAReplayPlugin.dll。");
            if (!HashFile(Path.Combine(root, "gamemd.exe")).Equals(TargetHash, StringComparison.Ordinal))
                throw new InvalidDataException("当前 gamemd.exe 版本与配套插件不匹配。");
        }

        private static string HashFile(string path)
        {
            using (SHA256 sha = SHA256.Create())
            using (FileStream stream = File.OpenRead(path))
                return Hex(sha.ComputeHash(stream));
        }

        private static string Hex(byte[] bytes)
        {
            return BitConverter.ToString(bytes).Replace("-", string.Empty).ToLowerInvariant();
        }

        private static ProcessStartInfo GameStartInfo(string root, string arguments, bool elevate)
        {
            return new ProcessStartInfo(Path.Combine(root, SyringeName), arguments)
            {
                WorkingDirectory = root,
                UseShellExecute = elevate,
                Verb = elevate ? "runas" : string.Empty
            };
        }

        private static int StartGame(string root, string arguments, bool allowElevation = false)
        {
            try
            {
                return WaitForGame(GameStartInfo(root, arguments, false));
            }
            catch (Win32Exception error) when (allowElevation && error.NativeErrorCode == 740)
            {
                try { return WaitForGame(GameStartInfo(root, arguments, true)); }
                catch (Win32Exception canceled) when (canceled.NativeErrorCode == 1223)
                {
                    throw new OperationCanceledException("已取消管理员授权，可以再次启动录像。", canceled);
                }
            }
        }

        private static int WaitForGame(ProcessStartInfo info)
        {
            using (Process game = Process.Start(info))
            {
                if (game == null) throw new IOException("无法启动 Syringe。");
                game.WaitForExit();
                return game.ExitCode;
            }
        }

        private static string QuoteArgument(string value)
        {
            if (value.Length > 0 && value.IndexOfAny(new[] { ' ', '\t', '"' }) < 0)
                return value;
            return "\"" + Regex.Replace(value, "(\\\\*)(\"|$)", match =>
                new string('\\', match.Groups[1].Length * 2 + (match.Groups[2].Length > 0 ? 1 : 0))
                + match.Groups[2].Value) + "\"";
        }

        private static IDisposable Acquire(string root)
        {
            string normalized = Path.GetFullPath(root).TrimEnd(Path.DirectorySeparatorChar).ToUpperInvariant();
            string name;
            using (SHA256 sha = SHA256.Create()) name = "RAReplayCenter-" + Hex(sha.ComputeHash(Encoding.UTF8.GetBytes(normalized)));
            var mutex = new Mutex(false, name);
            try
            {
                try
                {
                    if (!mutex.WaitOne(0))
                        throw new InvalidOperationException("这个游戏目录正有一局在录制或播放。");
                }
                catch (AbandonedMutexException)
                {
                    // 前一个进程异常结束。调用方仍须检查游戏进程和启动请求。
                }
                return new SessionLock(mutex);
            }
            catch
            {
                mutex.Dispose();
                throw;
            }
        }

        private sealed class SessionLock : IDisposable
        {
            private readonly Mutex mutex;
            public SessionLock(Mutex mutex) { this.mutex = mutex; }
            public void Dispose()
            {
                mutex.ReleaseMutex();
                mutex.Dispose();
            }
        }

        private static void EnsureIdle(string root, bool requireClosedClient, bool waitForReplayExit = false)
        {
            string[] names = requireClosedClient
                ? new[] { "gamemd", "Syringe", "MentalOmegaClient", "clientdx", "clientogl", "clientxna" }
                : new[] { "gamemd", "Syringe" };
            string prefix = Path.GetFullPath(root).TrimEnd('\\') + "\\";
            foreach (string name in names)
            {
                foreach (Process process in Process.GetProcessesByName(name))
                {
                    using (process)
                    {
                        string path = GetImagePath(process);
                        if (path == null || !path.StartsWith(prefix, StringComparison.OrdinalIgnoreCase) || process.HasExited)
                            continue;
                        // 回退时 Syringe 可能先于被调试的游戏完成退出。
                        if (waitForReplayExit && name == "gamemd" && process.WaitForExit(2000))
                            continue;
                        throw new InvalidOperationException("请关闭这个目录的游戏和启动器，再执行当前操作。");
                    }
                }
            }
        }

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern SafeProcessHandle OpenProcess(uint access, bool inheritHandle, int processId);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool QueryFullProcessImageName(SafeProcessHandle process, int flags,
            StringBuilder path, ref int length);

        private static string GetImagePath(Process process)
        {
            const uint QueryLimitedInformation = 0x1000;
            using (SafeProcessHandle handle = OpenProcess(QueryLimitedInformation, false, process.Id))
            {
                var path = new StringBuilder(32768);
                int length = path.Capacity;
                if (!handle.IsInvalid && QueryFullProcessImageName(handle, 0, path, ref length))
                    return path.ToString();
                if (Marshal.GetLastWin32Error() == 87 || process.HasExited) return null;
            }
            throw new InvalidOperationException("无法确认运行中的游戏或启动器属于哪个目录，请退出后重试。");
        }

        private static void Log(string root, string message)
        {
            try
            {
                string directory = Path.Combine(root, "Replays");
                Directory.CreateDirectory(directory);
                File.AppendAllText(Path.Combine(directory, "SA_Replay.log"),
                    DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss", CultureInfo.InvariantCulture) +
                    " " + message + Environment.NewLine, Encoding.UTF8);
            }
            catch (IOException) { }
            catch (UnauthorizedAccessException) { }
        }

        private static string DefinitionsPath(string root)
        {
            return Path.Combine(root, "Resources", "ClientDefinitions.ini");
        }

        private static string SectionName(string line)
        {
            int end = line.IndexOf(']');
            return line.StartsWith("[", StringComparison.Ordinal) && end > 0
                ? line.Substring(1, end - 1).Trim()
                : null;
        }

        private static Settings ReadSettings(string path)
        {
            return ReadIniSection(File.ReadAllBytes(path), "Settings");
        }

        private static Settings ReadIniSection(byte[] bytes, string wantedSection)
        {
            string content;
            try { content = new UTF8Encoding(false, true).GetString(bytes); }
            catch (DecoderFallbackException) { content = Encoding.GetEncoding(936).GetString(bytes); }
            var values = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            string section = string.Empty;
            foreach (string raw in content.Split('\n'))
            {
                string line = raw.Trim().TrimStart('\uFEFF');
                string nextSection = SectionName(line);
                if (nextSection != null) section = nextSection;
                if (!section.Equals(wantedSection, StringComparison.OrdinalIgnoreCase) || line.StartsWith(";")) continue;
                int equals = line.IndexOf('=');
                if (equals <= 0) continue;
                values[line.Substring(0, equals).Trim()] = line.Substring(equals + 1).Split(';')[0].Trim();
            }
            return new Settings(values);
        }

        private sealed class Settings
        {
            private readonly Dictionary<string, string> values;
            public Settings(Dictionary<string, string> values) { this.values = values; }
            public string Get(string key) { return values.TryGetValue(key, out string value) ? value : string.Empty; }
            public bool IsSet(string key)
            {
                string value = Get(key);
                return value == "1" || value.Equals("yes", StringComparison.OrdinalIgnoreCase) ||
                    value.Equals("true", StringComparison.OrdinalIgnoreCase);
            }
            public bool IsMatch()
            {
                return Get("Scenario").Equals("spawnmap.ini", StringComparison.OrdinalIgnoreCase) &&
                    int.TryParse(Get("PlayerCount"), NumberStyles.None, CultureInfo.InvariantCulture, out int players) &&
                    players > 0 && Get("UIGameMode").Length > 0 &&
                    !IsSet("IsSinglePlayer") && !IsSet("LoadSaveGame");
            }
        }

        private static void AtomicWrite(string path, byte[] bytes, string backup, byte[] expected)
        {
            string temporary = path + "." + Guid.NewGuid().ToString("N") + ".tmp";
            try
            {
                using (var stream = new FileStream(temporary, FileMode.CreateNew, FileAccess.Write, FileShare.None))
                {
                    stream.Write(bytes, 0, bytes.Length);
                    stream.Flush(true);
                }
                if (expected == null)
                    File.Move(temporary, path);
                else
                {
                    if (!File.ReadAllBytes(path).SequenceEqual(expected))
                        throw new IOException("配置已被其他程序更改，请刷新后重试。");
                    File.Replace(temporary, path, backup);
                }
            }
            finally
            {
                if (File.Exists(temporary)) File.Delete(temporary);
            }
        }
    }
}
