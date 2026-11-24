using System;
using System.Runtime.InteropServices;
using System.Windows.Forms;

namespace ReplayCenter
{
    internal sealed class SeekTrackBar : TrackBar
    {
        private const int LeftDown = 0x0201, LeftUp = 0x0202, LeftDoubleClick = 0x0203;
        private const int MouseMoveMessage = 0x0200, CaptureChanged = 0x0215;
        private const int GetChannelRect = 0x0400 + 26;

        [StructLayout(LayoutKind.Sequential)]
        private struct Rect
        {
            public int Left, Top, Right, Bottom;
        }

        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        private static extern IntPtr SendMessage(IntPtr window, int message, IntPtr wParam, out Rect rect);

        public bool IsDragging { get; private set; }
        public event EventHandler SeekRequested;

        protected override void WndProc(ref Message message)
        {
            switch (message.Msg)
            {
                case LeftDown:
                case LeftDoubleClick:
                    if (Enabled)
                    {
                        Focus();
                        Capture = true;
                        IsDragging = true;
                        SelectPosition(message.LParam);
                    }
                    return;
                case MouseMoveMessage when IsDragging:
                    SelectPosition(message.LParam);
                    return;
                case LeftUp when IsDragging:
                    SelectPosition(message.LParam);
                    IsDragging = false;
                    Capture = false;
                    SeekRequested?.Invoke(this, EventArgs.Empty);
                    return;
                case CaptureChanged:
                    IsDragging = false;
                    break;
            }

            base.WndProc(ref message);
        }

        protected override void OnScroll(EventArgs e)
        {
            base.OnScroll(e);
            if (!IsDragging)
                SeekRequested?.Invoke(this, e);
        }

        private void SelectPosition(IntPtr position)
        {
            SendMessage(Handle, GetChannelRect, IntPtr.Zero, out Rect channel);
            if (channel.Right <= channel.Left)
                return;
            int x = unchecked((short)position.ToInt64());
            double progress = (double)(x - channel.Left) / (channel.Right - channel.Left);
            Value = Minimum + (int)Math.Round(Math.Max(0, Math.Min(1, progress)) * (Maximum - Minimum));
        }
    }
}
