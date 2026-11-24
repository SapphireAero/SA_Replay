using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.IO.Compression;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using RAReplay.Client;

namespace ReplayCenter
{
    internal static class ReplayMapPreview
    {
        public static Bitmap Load(string game, ReplayLaunchManifest manifest)
        {
            Bitmap embedded = Decode(game, manifest.SpawnMapBytes);
            if (embedded != null) return embedded;

            string map = manifest.FindBaseMap(game);
            foreach (string extension in new[] { ".png", ".jpg", ".jpeg" })
            {
                string imagePath = Path.ChangeExtension(map, extension);
                if (!File.Exists(imagePath)) continue;
                using (Image image = Image.FromFile(imagePath))
                    return image.Width <= 4096 && image.Height <= 4096 ? new Bitmap(image) : null;
            }
            return Decode(game, File.ReadAllBytes(map));
        }

        private static Bitmap Decode(string game, byte[] map)
        {
            string size = null;
            var segments = new SortedDictionary<int, string>();
            string section = "";
            using (var lines = new StringReader(Encoding.ASCII.GetString(map)))
            {
                string raw;
                while ((raw = lines.ReadLine()) != null)
                {
                    string line = raw.Split(';')[0].Trim();
                    if (line.StartsWith("[", StringComparison.Ordinal) && line.EndsWith("]", StringComparison.Ordinal))
                    {
                        section = line.Substring(1, line.Length - 2);
                        continue;
                    }
                    if (!section.Equals("Preview", StringComparison.OrdinalIgnoreCase) &&
                        !section.Equals("PreviewPack", StringComparison.OrdinalIgnoreCase)) continue;
                    int equals = line.IndexOf('=');
                    if (equals < 0) continue;
                    string key = line.Substring(0, equals).Trim();
                    string value = line.Substring(equals + 1).Trim();
                    if (section.Equals("Preview", StringComparison.OrdinalIgnoreCase))
                    {
                        if (key.Equals("Size", StringComparison.OrdinalIgnoreCase)) size = value;
                    }
                    else if (int.TryParse(key, out int order) && order > 0)
                        segments[order] = value;
                }
            }
            string[] bounds = size?.Split(',');
            if (bounds == null || bounds.Length != 4 || !int.TryParse(bounds[2], out int width) ||
                !int.TryParse(bounds[3], out int height) || width < 1 || height < 1 ||
                width > 4096 || height > 4096 || (long)width * height > 4000000 || segments.Count == 0)
                return null;
            if (segments.TryGetValue(1, out string first) &&
                first == "yAsAIAXQ5PDQ5PDQ6JQATAEE6PDQ4PDI4JgBTAFEAkgAJyAATAG0AydEAEABpAJIA0wBVA")
                return null;

            string decoder = Path.Combine(game, "Resources", "Binaries", "lzo.net.dll");
            if (!File.Exists(decoder)) return null;
            Type lzo = Assembly.LoadFrom(decoder).GetType("lzo.net.LzoStream", true);
            byte[] packed = Convert.FromBase64String(string.Concat(segments.Values));
            byte[] pixels = new byte[width * height * 3];
            int position = 0;
            using (var input = new BinaryReader(new MemoryStream(packed)))
            {
                while (input.BaseStream.Length - input.BaseStream.Position >= 4)
                {
                    int compressed = input.ReadUInt16();
                    int expanded = input.ReadUInt16();
                    if (compressed == 0 || expanded == 0) break;
                    if (expanded > pixels.Length - position ||
                        compressed > input.BaseStream.Length - input.BaseStream.Position)
                        return null;
                    using (var block = new MemoryStream(input.ReadBytes(compressed)))
                    using (var unpacked = (Stream)Activator.CreateInstance(lzo, block, CompressionMode.Decompress))
                    {
                        for (int written = 0; written < expanded;)
                        {
                            int read = unpacked.Read(pixels, position + written, expanded - written);
                            if (read <= 0) return null;
                            written += read;
                        }
                    }
                    position += expanded;
                }
            }
            if (position != pixels.Length) return null;
            for (int i = 0; i < pixels.Length; i += 3)
            {
                byte red = pixels[i];
                pixels[i] = pixels[i + 2];
                pixels[i + 2] = red;
            }

            var bitmap = new Bitmap(width, height, PixelFormat.Format24bppRgb);
            try
            {
                var data = bitmap.LockBits(new Rectangle(0, 0, width, height),
                    ImageLockMode.WriteOnly, PixelFormat.Format24bppRgb);
                try
                {
                    for (int row = 0; row < height; row++)
                        Marshal.Copy(pixels, row * width * 3, IntPtr.Add(data.Scan0, row * data.Stride), width * 3);
                }
                finally { bitmap.UnlockBits(data); }
                return bitmap;
            }
            catch
            {
                bitmap.Dispose();
                throw;
            }
        }
    }
}
