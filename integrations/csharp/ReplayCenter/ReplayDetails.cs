using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using RAReplay.Client;

namespace ReplayCenter
{
    internal sealed class ReplayDetails
    {
        public const string BattleSuffix = ".battle-stats";
        private const double FramesPerGameMinute = 3600;
        public string MapName { get; private set; }
        public string Duration { get; private set; }
        public string GameDuration { get; private set; }
        public bool HasBattleStats { get; private set; }
        public bool HasDetailedStats { get; private set; }
        public List<Player> Players { get; } = new List<Player>();

        internal sealed class Player
        {
            public int House;
            public string Name;
            public string Faction;
            public int? Kills;
            public int? Losses;
            public int? Produced;
            public uint Commands;
            public double Rate;
            public Breakdown Details;
        }

        internal sealed class Breakdown
        {
            public string Faction;
            public uint Flags;
            public int UnitKills, BuildingKills;
            public int? UnitLosses, BuildingLosses;
            public int[] Produced, Owned;
            public int Harvested, Spent, Cash, PowerOutput, PowerDrain;

            public string Outcome => (Flags & 4) != 0 ? "获胜" : (Flags & 10) != 0 ? "战败" : "未判定";
        }

        public static ReplayDetails Load(ReplayArchive archive, string game)
        {
            var details = new ReplayDetails { MapName = GameSession.RecordedMapName(archive.Manifest) };
            var commands = new uint[64];
            uint frames = archive.ReadFrames((type, house) =>
            {
                if (IsGameCommand(type)) commands[house]++;
            }, out int localHouse);
            if (frames == 0) throw new InvalidDataException("这份录像没有可统计的游戏帧。");
            details.GameDuration = string.Format(CultureInfo.CurrentCulture, "{0:0.00} 游戏分钟（{1:N0} 帧）",
                frames / FramesPerGameMinute, frames);
            details.HasBattleStats = ReadBattleStats(archive, frames, details.Players, out long originalMilliseconds,
                out bool hasDetailedStats);
            details.HasDetailedStats = details.HasBattleStats && hasDetailedStats;
            details.Duration = GetRecordedDuration(archive, originalMilliseconds);
            var roster = GameSession.ReadRecordedPlayers(archive.Manifest, localHouse);
            string[] factions = GameSession.ReadFactionNames(game);
            if (!details.HasBattleStats)
            {
                for (int house = 0; house < commands.Length; house++)
                {
                    if (commands[house] == 0 && !roster.ContainsKey(house)) continue;
                    details.Players.Add(new Player { House = house });
                }
            }
            details.Players.RemoveAll(player =>
                roster.TryGetValue(player.House, out GameSession.RecordedPlayer identity) && identity.IsSpectator);
            foreach (var player in details.Players)
            {
                player.Faction = player.Details?.Faction;
                if (roster.TryGetValue(player.House, out GameSession.RecordedPlayer identity))
                {
                    player.Name = identity.Name;
                    if (string.IsNullOrWhiteSpace(player.Faction) && identity.Side >= 0 && identity.Side < factions.Length)
                        player.Faction = factions[identity.Side];
                }
                else if (player.Details != null && (player.Details.Flags & 1) == 0)
                    player.Name = "AI";
                if (string.IsNullOrWhiteSpace(player.Name)) player.Name = "未识别玩家";
                player.Commands = commands[player.House];
                player.Rate = player.Commands * FramesPerGameMinute / frames;
            }
            return details;
        }

        private static bool IsGameCommand(byte type)
        {
            switch (type)
            {
                case 0x01: // PowerOn
                case 0x02: // PowerOff
                case 0x03: // Ally
                case 0x04: // MegaMission
                case 0x05: // MegaMissionF
                case 0x06: // Idle
                case 0x07: // Scatter
                case 0x09: // Deploy
                case 0x0A: // Detonate
                case 0x0B: // Place
                case 0x0E: // Produce
                case 0x0F: // Suspend
                case 0x10: // Abandon
                case 0x11: // Primary
                case 0x12: // SpecialPlace
                case 0x15: // Repair
                case 0x16: // Sell
                case 0x17: // SellCell
                case 0x2E: // AbandonAll
                    return true;
                default:
                    return false;
            }
        }

        private static bool ReadBattleStats(ReplayArchive archive, uint frames, List<Player> players,
            out long duration, out bool detailed)
        {
            duration = -1;
            detailed = false;
            try
            {
                using (var reader = new BinaryReader(File.OpenRead(archive.Path + BattleSuffix)))
                {
                    string version = Encoding.ASCII.GetString(reader.ReadBytes(8));
                    bool versionTwo = version == "RABATLE2";
                    if ((!versionTwo && version != "RABATLE1") ||
                        !archive.GetPayloadHash().SequenceEqual(reader.ReadBytes(32)) ||
                        reader.ReadUInt32() != frames)
                        return false;
                    long milliseconds = reader.ReadInt64();
                    uint count = reader.ReadUInt32();
                    int rowBytes = versionTwo ? 232 : 64;
                    if (count > 64 || reader.BaseStream.Length != 56 + count * (long)rowBytes) return false;
                    var parsed = new List<Player>((int)count);
                    var seen = new HashSet<int>();
                    for (int i = 0; i < count; i++)
                    {
                        int house = reader.ReadInt32();
                        int kills = reader.ReadInt32();
                        int losses = reader.ReadInt32();
                        int produced = reader.ReadInt32();
                        string name = Encoding.Unicode.GetString(reader.ReadBytes(48)).TrimEnd('\0');
                        if (house < 0 || house >= 64 || !seen.Add(house) || kills < 0 || losses < -1 || produced < 0)
                            return false;
                        parsed.Add(new Player
                        {
                            House = house, Name = name, Kills = kills, Losses = losses < 0 ? (int?)null : losses,
                            Produced = produced, Details = versionTwo ? ReadBreakdown(reader) : null
                        });
                    }
                    duration = milliseconds;
                    detailed = versionTwo;
                    players.AddRange(parsed);
                    return count > 0;
                }
            }
            catch (IOException) { return false; }
            catch (UnauthorizedAccessException) { return false; }
        }

        private static Breakdown ReadBreakdown(BinaryReader reader)
        {
            return new Breakdown
            {
                Faction = Encoding.Unicode.GetString(reader.ReadBytes(96)).TrimEnd('\0'),
                UnitKills = reader.ReadInt32(),
                BuildingKills = reader.ReadInt32(),
                UnitLosses = ReadLosses(reader),
                BuildingLosses = ReadLosses(reader),
                Produced = ReadUnitCounts(reader),
                Owned = ReadUnitCounts(reader),
                Harvested = reader.ReadInt32(),
                Spent = reader.ReadInt32(),
                Cash = reader.ReadInt32(),
                PowerOutput = reader.ReadInt32(),
                PowerDrain = reader.ReadInt32(),
                Flags = reader.ReadUInt32()
            };
        }

        private static int? ReadLosses(BinaryReader reader)
        {
            int value = reader.ReadInt32();
            return value < 0 ? (int?)null : value;
        }

        private static int[] ReadUnitCounts(BinaryReader reader)
        {
            return new[] { reader.ReadInt32(), reader.ReadInt32(), reader.ReadInt32(), reader.ReadInt32() };
        }

        private static string GetRecordedDuration(ReplayArchive archive, long milliseconds)
        {
            if (milliseconds >= 0 && milliseconds <= TimeSpan.FromDays(365).TotalMilliseconds)
                return ClockText(TimeSpan.FromMilliseconds(milliseconds));
            var file = new FileInfo(archive.Path);
            DateTime manifestTime = DateTimeOffset.FromUnixTimeSeconds(archive.Manifest.RecordedAtUnixSeconds).UtcDateTime;
            TimeSpan elapsed = file.LastWriteTimeUtc - file.CreationTimeUtc;
            return Math.Abs((file.CreationTimeUtc - manifestTime).TotalMinutes) <= 2 &&
                elapsed > TimeSpan.Zero && elapsed < TimeSpan.FromDays(7)
                ? "约 " + ClockText(elapsed) : "未知";
        }

        private static string ClockText(TimeSpan elapsed)
        {
            return string.Format(CultureInfo.CurrentCulture, "{0:00}:{1:00}:{2:00}",
                (int)elapsed.TotalHours, elapsed.Minutes, elapsed.Seconds);
        }
    }
}
