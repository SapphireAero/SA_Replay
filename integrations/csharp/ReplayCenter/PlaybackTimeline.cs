using System;
using System.IO;
using System.IO.MemoryMappedFiles;
using System.Security.AccessControl;
using System.Security.Principal;
using RAReplay.Client;

namespace ReplayCenter
{
    internal sealed class PlaybackTimeline
    {
        private const int Magic = 0x33544c52;
        private const int Total = 4, Completed = 8, Target = 12, Restart = 16, Seeking = 20, Rate = 24, Paused = 28, Size = 32;
        private readonly object gate = new object();
        private MemoryMappedFile mapping;
        private MemoryMappedViewAccessor view;
        private int playbackRate = 1;
        private bool paused;

        public void Attach(PlaybackSession session, int initialTarget, bool allowElevatedGame = false)
        {
            lock (gate)
            {
                if (session.FrameCount == 0)
                    return;
                string name = @"Local\RAReplayTimeline-" +
                    BitConverter.ToString(session.RequestId.ToByteArray()).Replace("-", "").ToLowerInvariant();
                mapping = CreateMapping(name, allowElevatedGame);
                try
                {
                    view = mapping.CreateViewAccessor(0, Size, MemoryMappedFileAccess.ReadWrite);
                    view.Write(Total, checked((int)session.FrameCount));
                    view.Write(Completed, -1);
                    view.Write(Target, initialTarget);
                    view.Write(Restart, -1);
                    view.Write(Seeking, initialTarget);
                    view.Write(Rate, playbackRate);
                    view.Write(Paused, paused ? 1 : 0);
                    view.Write(0, Magic);
                }
                catch
                {
                    Detach();
                    throw;
                }
            }
        }

        private static MemoryMappedFile CreateMapping(string name, bool allowElevatedGame)
        {
            if (!allowElevatedGame)
                return MemoryMappedFile.CreateNew(name, Size, MemoryMappedFileAccess.ReadWrite);

            // 标准用户可在 UAC 中使用另一个管理员账户。只给本次映射的创建者
            // 和管理员组授权，游戏仍通过随机请求 ID 查找映射。
            using (WindowsIdentity identity = WindowsIdentity.GetCurrent())
            {
                var security = new MemoryMappedFileSecurity();
                security.AddAccessRule(new AccessRule<MemoryMappedFileRights>(
                    identity.User, MemoryMappedFileRights.FullControl, AccessControlType.Allow));
                security.AddAccessRule(new AccessRule<MemoryMappedFileRights>(
                    new SecurityIdentifier(WellKnownSidType.BuiltinAdministratorsSid, null),
                    MemoryMappedFileRights.ReadWrite, AccessControlType.Allow));
                return MemoryMappedFile.CreateNew(name, Size, MemoryMappedFileAccess.ReadWrite,
                    MemoryMappedFileOptions.None, security, HandleInheritability.None);
            }
        }

        public int Detach()
        {
            lock (gate)
            {
                int restart = view == null ? -1 : view.ReadInt32(Restart);
                if (view != null)
                {
                    playbackRate = ReadRate();
                    paused = ReadPaused();
                }
                view?.Dispose();
                mapping?.Dispose();
                view = null;
                mapping = null;
                return restart;
            }
        }

        public void SetPaused(bool value)
        {
            lock (gate)
            {
                paused = value;
                view?.Write(Paused, value ? 1 : 0);
            }
        }

        public void Seek(int target)
        {
            lock (gate)
            {
                if (view == null || view.ReadInt32(Completed) < 0 || target < 0 ||
                    target >= view.ReadInt32(Total))
                    return;
                view.Write(Target, target);
            }
        }

        public void SetRate(int rate)
        {
            if (!IsValidRate(rate)) return;
            lock (gate)
            {
                playbackRate = rate;
                view?.Write(Rate, rate);
            }
        }

        private static bool IsValidRate(int rate)
        {
            return rate == 1 || rate == 2 || rate == 4 || rate == 8 || rate == 16;
        }

        private int ReadRate()
        {
            int rate = view == null ? playbackRate : view.ReadInt32(Rate);
            return IsValidRate(rate) ? rate : 1;
        }

        private bool ReadPaused()
        {
            return view == null ? paused : view.ReadInt32(Paused) == 1;
        }

        public void Read(out int frame, out int total, out int seeking, out int rate, out bool isPaused)
        {
            lock (gate)
            {
                rate = ReadRate();
                isPaused = ReadPaused();
                if (view == null)
                {
                    frame = seeking = -1;
                    total = 0;
                    return;
                }
                int completed = view.ReadInt32(Completed);
                frame = completed < 0 ? -1 : Math.Max(0, completed - 1);
                total = view.ReadInt32(Total);
                int queued = view.ReadInt32(Target);
                seeking = queued >= 0 ? queued : view.ReadInt32(Seeking);
            }
        }
    }
}
