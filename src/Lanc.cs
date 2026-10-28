using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Text;
using System.IO;
using System.Text;
using System.Windows.Forms;
using System.Reflection;

[assembly: AssemblyTitle("Gvtcier 启动面板")]
[assembly: AssemblyDescription("Gvtcier 启动面板")]
[assembly: AssemblyCompany("Li-HuaL1n Software")]
[assembly: AssemblyProduct("Gvtcier")]
[assembly: AssemblyCopyright("Copyright (C) 2026 Li-HuaL1n Software")]
[assembly: AssemblyVersion("1.6.0.0")]
[assembly: AssemblyFileVersion("1.6.0.0")]

namespace Gvtcier
{
    internal sealed class GameEntry
    {
        public string Name = "";
        public string Description = "";
        public string Author = "";
        public string DirectoryPath = "";
        public string GpkPath = "";
        public byte[] CoverPng = null;
        public int ModCount = 0;

        private const uint GpkMagic = 0x4B415047;

        public static GameEntry Load(string gpk)
        {
            byte[] raw;
            try { raw = File.ReadAllBytes(gpk); }
            catch (Exception) { return null; }
            if (raw.Length < 12) return null;
            if (ReadU32(raw, 0) != GpkMagic) return null;
            uint count = ReadU32(raw, 8);
            int pos = 12;
            byte[] meta = null;
            byte[] cover = null;
            for (uint i = 0; i < count; ++i)
            {
                if (pos + 2 > raw.Length) return null;
                int nameLen = raw[pos] | (raw[pos + 1] << 8);
                pos += 2;
                if (pos + nameLen + 20 > raw.Length) return null;
                string name = Encoding.UTF8.GetString(raw, pos, nameLen);
                pos += nameLen;
                ulong offset = ReadU64(raw, pos);
                ulong size = ReadU64(raw, pos + 8);
                pos += 20;
                if (offset + size > (ulong)raw.Length) continue;
                if (name == "meta.json")
                {
                    meta = new byte[size];
                    Array.Copy(raw, (int)offset, meta, 0, (int)size);
                }
                else if (name == "cover.png")
                {
                    cover = new byte[size];
                    Array.Copy(raw, (int)offset, cover, 0, (int)size);
                }
            }
            GameEntry e = new GameEntry();
            e.GpkPath = gpk;
            e.DirectoryPath = Path.GetDirectoryName(gpk);
            e.Name = ParseJsonField(meta, "name");
            e.Description = ParseJsonField(meta, "description");
            e.Author = ParseJsonField(meta, "author");
            if (e.Name.Length == 0) e.Name = Path.GetFileNameWithoutExtension(gpk);
            e.CoverPng = cover;
            e.ModCount = CountMods(e.DirectoryPath);
            return e;
        }

        private static int CountMods(string dir)
        {
            try
            {
                string modDir = Path.Combine(dir, "mod");
                if (!Directory.Exists(modDir)) return 0;
                string[] files = Directory.GetFiles(modDir, "*.glc");
                int n = 0;
                foreach (string f in files)
                    if (!File.Exists(f + ".disabled")) n++;
                return n;
            }
            catch
            {
                return 0;
            }
        }

        private static uint ReadU32(byte[] b, int o)
        {
            return (uint)(b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24));
        }

        private static ulong ReadU64(byte[] b, int o)
        {
            ulong lo = ReadU32(b, o);
            ulong hi = ReadU32(b, o + 4);
            return lo | (hi << 32);
        }

        private static string ParseJsonField(byte[] json, string key)
        {
            if (json == null || json.Length == 0) return "";
            string text = Encoding.UTF8.GetString(json);
            string token = "\"" + key + "\"";
            int i = text.IndexOf(token, StringComparison.Ordinal);
            if (i < 0) return "";
            i = text.IndexOf(':', i + token.Length);
            if (i < 0) return "";
            ++i;
            while (i < text.Length && char.IsWhiteSpace(text[i])) ++i;
            if (i >= text.Length || text[i] != '"') return "";
            ++i;
            StringBuilder sb = new StringBuilder();
            while (i < text.Length && text[i] != '"')
            {
                if (text[i] == '\\' && i + 1 < text.Length)
                {
                    ++i;
                    char c = text[i];
                    if (c == 'n') sb.Append('\n');
                    else if (c == 't') sb.Append('\t');
                    else if (c == 'r') sb.Append('\r');
                    else if (c == 'u' && i + 4 < text.Length)
                    {
                        int code = Convert.ToInt32(text.Substring(i + 1, 4), 16);
                        sb.Append((char)code);
                        i += 4;
                    }
                    else sb.Append(c);
                }
                else sb.Append(text[i]);
                ++i;
            }
            return sb.ToString();
        }
    }

    internal static class Theme
    {
        public static readonly Color Bg = Color.FromArgb(16, 17, 20);
        public static readonly Color RailBg = Color.FromArgb(11, 12, 14);
        public static readonly Color RailSel = Color.FromArgb(38, 40, 46);
        public static readonly Color RailHover = Color.FromArgb(26, 28, 32);
        public static readonly Color TextPrimary = Color.FromArgb(240, 241, 244);
        public static readonly Color TextDim = Color.FromArgb(150, 154, 162);
        public static readonly Color Accent = Color.FromArgb(0, 200, 150);
        public static readonly Color AccentHover = Color.FromArgb(0, 224, 170);
        public static readonly Color Line = Color.FromArgb(40, 42, 48);

        public static readonly Font FontTitle = new Font("Microsoft YaHei UI", 34f, FontStyle.Bold, GraphicsUnit.Pixel);
        public static readonly Font FontVer = new Font("Microsoft YaHei UI", 13f, FontStyle.Regular, GraphicsUnit.Pixel);
        public static readonly Font FontBody = new Font("Microsoft YaHei UI", 14f, FontStyle.Regular, GraphicsUnit.Pixel);
        public static readonly Font FontButton = new Font("Microsoft YaHei UI", 15f, FontStyle.Bold, GraphicsUnit.Pixel);

        public static GraphicsPath Rounded(RectangleF r, float radius)
        {
            GraphicsPath path = new GraphicsPath();
            float d = radius * 2f;
            if (d <= 0f) { path.AddRectangle(r); return path; }
            path.AddArc(r.X, r.Y, d, d, 180, 90);
            path.AddArc(r.Right - d, r.Y, d, d, 270, 90);
            path.AddArc(r.Right - d, r.Bottom - d, d, d, 0, 90);
            path.AddArc(r.X, r.Bottom - d, d, d, 90, 90);
            path.CloseFigure();
            return path;
        }
    }

    internal sealed class LauncherForm : Form
    {
        private const int RailWidth = 86;
        private const int IconSize = 56;

        private readonly List<GameEntry> games_ = new List<GameEntry>();
        private readonly Dictionary<string, Image> cache_ = new Dictionary<string, Image>();
        private readonly Dictionary<string, Color> dominantCache_ = new Dictionary<string, Color>();
        private int selected_ = -1;
        private int hover_ = -1;
        private Rectangle launchBtn_;
        private Rectangle railArea_;
        private bool launchHover_;
        private bool busy_;
        private bool running_;
        private Process gameProc_;

        [System.Runtime.InteropServices.DllImport("user32.dll")]
        private static extern bool SetForegroundWindow(IntPtr hWnd);

        [System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential, CharSet = System.Runtime.InteropServices.CharSet.Unicode)]
        private struct STARTUPINFO
        {
            public int cb;
            public string lpReserved;
            public string lpDesktop;
            public string lpTitle;
            public int dwX;
            public int dwY;
            public int dwXSize;
            public int dwYSize;
            public int dwXCountChars;
            public int dwYCountChars;
            public int dwFillAttribute;
            public int dwFlags;
            public short wShowWindow;
            public short cbReserved2;
            public IntPtr lpReserved2;
            public IntPtr hStdInput;
            public IntPtr hStdOutput;
            public IntPtr hStdError;
        }

        [System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential)]
        private struct PROCESS_INFORMATION
        {
            public IntPtr hProcess;
            public IntPtr hThread;
            public int dwProcessId;
            public int dwThreadId;
        }

        [System.Runtime.InteropServices.DllImport("kernel32.dll", SetLastError = true, CharSet = System.Runtime.InteropServices.CharSet.Unicode)]
        private static extern bool CreateProcessW(
            string lpApplicationName,
            System.Text.StringBuilder lpCommandLine,
            IntPtr lpProcessAttributes,
            IntPtr lpThreadAttributes,
            bool bInheritHandles,
            uint dwCreationFlags,
            IntPtr lpEnvironment,
            string lpCurrentDirectory,
            ref STARTUPINFO lpStartupInfo,
            out PROCESS_INFORMATION lpProcessInformation);

        [System.Runtime.InteropServices.DllImport("kernel32.dll")]
        private static extern bool CloseHandle(IntPtr h);

        private static Process StartProcessUnicode(string exe, string workDir, string argument)
        {
            STARTUPINFO si = new STARTUPINFO();
            si.cb = System.Runtime.InteropServices.Marshal.SizeOf(typeof(STARTUPINFO));
            PROCESS_INFORMATION pi;
            System.Text.StringBuilder cmd = new System.Text.StringBuilder();
            cmd.Append('"').Append(exe).Append('"');
            if (!string.IsNullOrEmpty(argument)) cmd.Append(' ').Append(argument);
            if (!CreateProcessW(exe, cmd, IntPtr.Zero, IntPtr.Zero, false, 0, IntPtr.Zero, workDir, ref si, out pi))
                throw new System.ComponentModel.Win32Exception(System.Runtime.InteropServices.Marshal.GetLastWin32Error());
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            return Process.GetProcessById(pi.dwProcessId);
        }

        public LauncherForm()
        {
            Text = "Gvtcier 游戏启动器";
            ClientSize = new Size(1180, 680);
            MinimumSize = new Size(940, 560);
            StartPosition = FormStartPosition.CenterScreen;
            BackColor = Theme.Bg;
            ForeColor = Theme.TextPrimary;
            DoubleBuffered = true;
            SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer
                     | ControlStyles.UserPaint | ControlStyles.ResizeRedraw, true);

            Icon = LoadAppIcon();
            ScanGames();
            MouseMove += OnMouseMove;
            MouseLeave += delegate { hover_ = -1; launchHover_ = false; Invalidate(); };
            MouseDown += OnMouseDown;
        }

        private static Icon LoadAppIcon()
        {
            try
            {
                Icon extracted = Icon.ExtractAssociatedIcon(Application.ExecutablePath);
                if (extracted != null) return extracted;
            }
            catch { }
            return null;
        }

        private static string BaseDir
        {
            get { return Path.GetDirectoryName(Application.ExecutablePath); }
        }

        private void ScanGames()
        {
            games_.Clear();
            string root = Path.Combine(BaseDir, "Games");
            if (!Directory.Exists(root)) return;
            string[] dirs = Directory.GetDirectories(root);
            for (int i = 0; i < dirs.Length; ++i)
            {
                string[] gpks = Directory.GetFiles(dirs[i], "*.gpk", SearchOption.TopDirectoryOnly);
                for (int j = 0; j < gpks.Length; ++j)
                {
                    GameEntry e = GameEntry.Load(gpks[j]);
                    if (e != null) games_.Add(e);
                }
            }
            games_.Sort(delegate (GameEntry a, GameEntry b) { return string.Compare(a.Name, b.Name, StringComparison.CurrentCulture); });
            if (games_.Count > 0) selected_ = 0;
        }

        private Rectangle RailRect(int index)
        {
            return new Rectangle(12, 24 + index * (IconSize + 14), IconSize, IconSize);
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;
            g.TextRenderingHint = TextRenderingHint.ClearTypeGridFit;
            g.Clear(Theme.Bg);
            DrawRail(g);
            DrawDetail(g);
        }

        private void DrawRail(Graphics g)
        {
            railArea_ = new Rectangle(0, 0, RailWidth, ClientSize.Height);
            using (SolidBrush b = new SolidBrush(Theme.RailBg)) g.FillRectangle(b, railArea_);
            using (Pen p = new Pen(Theme.Line)) g.DrawLine(p, RailWidth - 1, 0, RailWidth - 1, ClientSize.Height);

            for (int i = 0; i < games_.Count; ++i)
            {
                Rectangle r = RailRect(i);
                bool sel = i == selected_;
                bool hov = i == hover_;
                if (sel || hov)
                {
                    using (GraphicsPath path = Theme.Rounded(r, 10f))
                    using (SolidBrush b = new SolidBrush(sel ? Theme.RailSel : Theme.RailHover))
                        g.FillPath(b, path);
                }

                using (GraphicsPath path = Theme.Rounded(r, 10f))
                using (SolidBrush b = new SolidBrush(Color.FromArgb(48, 52, 60)))
                    g.FillPath(b, path);
                using (Font f = new Font("Microsoft YaHei UI", 20f, FontStyle.Bold, GraphicsUnit.Pixel))
                using (SolidBrush tb = new SolidBrush(Theme.TextDim))
                {
                    StringFormat sf = new StringFormat();
                    sf.Alignment = StringAlignment.Center;
                    sf.LineAlignment = StringAlignment.Center;
                    string ch = games_[i].Name.Length > 0 ? games_[i].Name.Substring(0, 1) : "?";
                    g.DrawString(ch, f, tb, r, sf);
                }

                if (sel)
                {
                    using (Pen p = new Pen(Theme.Accent, 3f))
                    using (GraphicsPath path = Theme.Rounded(new RectangleF(r.X - 3, r.Y - 3, r.Width + 6, r.Height + 6), 12f))
                        g.DrawPath(p, path);
                }
            }
        }

        private void DrawDetail(Graphics g)
        {
            int left = railArea_.Width;
            Rectangle area = new Rectangle(left, 0, ClientSize.Width - left, ClientSize.Height);

            if (selected_ < 0 || selected_ >= games_.Count)
            {
                using (SolidBrush b = new SolidBrush(Theme.TextDim))
                {
                    StringFormat sf = new StringFormat();
                    sf.Alignment = StringAlignment.Center;
                    sf.LineAlignment = StringAlignment.Center;
                    g.DrawString("未发现游戏\n请将游戏包放入 Games/ 目录", Theme.FontBody, b, area, sf);
                }
                return;
            }

            GameEntry gp = games_[selected_];
            Image cover = LoadCover(gp);
            using (SolidBrush backdrop = new SolidBrush(Color.FromArgb(24, 26, 31)))
                g.FillRectangle(backdrop, area);
            if (cover != null)
            {
                Rectangle dest = FitContain(cover.Size, area);
                Color dominant = DominantColor(gp.GpkPath, cover);
                FillSurround(g, area, dest, dominant);
                InterpolationMode oldMode = g.InterpolationMode;
                g.InterpolationMode = InterpolationMode.HighQualityBicubic;
                g.DrawImage(cover, dest);
                g.InterpolationMode = oldMode;
            }

            int padX = left + 64;
            int baseY = ClientSize.Height - 250;

            int textH = ClientSize.Height - baseY + 70;
            Rectangle textZone = new Rectangle(left, ClientSize.Height - textH, ClientSize.Width - left, textH);
            if (textZone.Height > 0)
            {
                using (LinearGradientBrush textShade = new LinearGradientBrush(
                    new Rectangle(textZone.X, textZone.Y, Math.Max(1, textZone.Width), Math.Max(1, textZone.Height)),
                    Color.FromArgb(0, Theme.Bg), Color.FromArgb(238, Theme.Bg), LinearGradientMode.Vertical))
                {
                    ColorBlend blend = new ColorBlend(4);
                    blend.Colors = new Color[]
                    {
                        Color.FromArgb(0, Theme.Bg),
                        Color.FromArgb(120, Theme.Bg),
                        Color.FromArgb(215, Theme.Bg),
                        Color.FromArgb(245, Theme.Bg)
                    };
                    blend.Positions = new float[] { 0.0f, 0.28f, 0.6f, 1.0f };
                    textShade.InterpolationColors = blend;
                    g.FillRectangle(textShade, textZone);
                }
            }

            using (SolidBrush b = new SolidBrush(Theme.Accent))
                g.FillRectangle(b, padX, baseY - 30, 46, 3);

            using (SolidBrush b = new SolidBrush(Theme.TextPrimary))
                g.DrawString(gp.Name, Theme.FontTitle, b, padX, baseY - 8);

            string meta = gp.Author;
            if (gp.ModCount > 0)
                meta = meta.Length > 0
                    ? meta + "    此游戏包含 " + gp.ModCount + " 个模组"
                    : "此游戏包含 " + gp.ModCount + " 个模组";
            if (meta.Length > 0)
            {
                using (SolidBrush b = new SolidBrush(Theme.TextDim))
                    g.DrawString(meta, Theme.FontVer, b, padX, baseY + 54);
            }

            using (SolidBrush b = new SolidBrush(Theme.TextPrimary))
            {
                RectangleF wrap = new RectangleF(padX, baseY + 92, ClientSize.Width - padX - 120, 96);
                g.DrawString(gp.Description, Theme.FontBody, b, wrap);
            }

            DrawLaunchButton(g, padX, ClientSize.Height - 92);
        }

        private void DrawLaunchButton(Graphics g, int x, int y)
        {
            const int w = 210;
            const int h = 52;
            Rectangle r = new Rectangle(x, y, w, h);
            launchBtn_ = r;

            Color baseFill;
            Color textFill;
            string caption;
            if (busy_)
            {
                baseFill = Color.FromArgb(58, 61, 68);
                textFill = Theme.TextDim;
                caption = "启 动 中";
            }
            else if (running_)
            {
                baseFill = Color.FromArgb(44, 47, 53);
                textFill = Theme.Accent;
                caption = "运 行 中";
            }
            else
            {
                baseFill = launchHover_ ? Theme.AccentHover : Theme.Accent;
                textFill = Color.FromArgb(10, 14, 12);
                caption = "运 行";
            }

            using (GraphicsPath path = Theme.Rounded(r, 4f))
            using (SolidBrush b = new SolidBrush(baseFill))
                g.FillPath(b, path);
            if (running_ && !busy_)
            {
                using (Pen p = new Pen(Theme.Accent, 2f))
                using (GraphicsPath path = Theme.Rounded(r, 4f))
                    g.DrawPath(p, path);
            }
            using (SolidBrush b = new SolidBrush(textFill))
            {
                StringFormat sf = new StringFormat();
                sf.Alignment = StringAlignment.Center;
                sf.LineAlignment = StringAlignment.Center;
                g.DrawString(caption, Theme.FontButton, b, r, sf);
            }
        }

        private static Rectangle FitContain(Size img, Rectangle area)
        {
            if (img.Width <= 0 || img.Height <= 0) return area;
            double sx = area.Width / (double)img.Width;
            double sy = area.Height / (double)img.Height;
            double s = Math.Min(sx, sy);
            int w = Math.Min(area.Width, (int)Math.Ceiling(img.Width * s));
            int h = Math.Min(area.Height, (int)Math.Ceiling(img.Height * s));
            return new Rectangle(area.X + (area.Width - w) / 2, area.Y + (area.Height - h) / 2, w, h);
        }

        private void FillSurround(Graphics g, Rectangle area, Rectangle dest, Color dominant)
        {
            Color dark = Darken(dominant, 0.42f);
            Color mid = Darken(dominant, 0.72f);
            if (dest.X > area.X)
            {
                Rectangle leftRect = new Rectangle(area.X, area.Y, dest.X - area.X, area.Height);
                FillSideGradient(g, leftRect, mid, dark, true);
            }
            if (dest.Right < area.Right)
            {
                Rectangle rightRect = new Rectangle(dest.Right, area.Y, area.Right - dest.Right, area.Height);
                FillSideGradient(g, rightRect, mid, dark, false);
            }
            if (dest.Y > area.Y)
            {
                Rectangle topRect = new Rectangle(area.X, area.Y, area.Width, dest.Y - area.Y);
                using (LinearGradientBrush b = new LinearGradientBrush(
                    new Rectangle(topRect.X, topRect.Y, Math.Max(1, topRect.Width), Math.Max(1, topRect.Height)),
                    dark, mid, LinearGradientMode.Vertical))
                {
                    b.InterpolationColors = MakeBlend(dark, mid);
                    g.FillRectangle(b, topRect);
                }
            }
            if (dest.Bottom < area.Bottom)
            {
                Rectangle botRect = new Rectangle(area.X, dest.Bottom, area.Width, area.Bottom - dest.Bottom);
                using (LinearGradientBrush b = new LinearGradientBrush(
                    new Rectangle(botRect.X, botRect.Y, Math.Max(1, botRect.Width), Math.Max(1, botRect.Height)),
                    mid, dark, LinearGradientMode.Vertical))
                {
                    b.InterpolationColors = MakeBlend(mid, dark);
                    g.FillRectangle(b, botRect);
                }
            }
        }

        private static void FillSideGradient(Graphics g, Rectangle r, Color inner, Color outer, bool leftSide)
        {
            if (r.Width <= 0 || r.Height <= 0) return;
            using (LinearGradientBrush b = new LinearGradientBrush(
                new Rectangle(r.X, r.Y, Math.Max(1, r.Width), Math.Max(1, r.Height)),
                leftSide ? outer : inner, leftSide ? inner : outer, LinearGradientMode.Horizontal))
            {
                b.InterpolationColors = MakeBlend(leftSide ? outer : inner, leftSide ? inner : outer);
                g.FillRectangle(b, r);
            }
        }

        private static ColorBlend MakeBlend(Color a, Color b)
        {
            ColorBlend blend = new ColorBlend(3);
            blend.Colors = new Color[]
            {
                a,
                Color.FromArgb((a.R + b.R) / 2, (a.G + b.G) / 2, (a.B + b.B) / 2),
                b
            };
            blend.Positions = new float[] { 0.0f, 0.5f, 1.0f };
            return blend;
        }

        private static Color Darken(Color c, float factor)
        {
            int r = (int)(c.R * factor);
            int g = (int)(c.G * factor);
            int b = (int)(c.B * factor);
            return Color.FromArgb(
                r < 0 ? 0 : (r > 255 ? 255 : r),
                g < 0 ? 0 : (g > 255 ? 255 : g),
                b < 0 ? 0 : (b > 255 ? 255 : b));
        }

        private Color DominantColor(string path, Image cover)
        {
            if (dominantCache_.ContainsKey(path)) return dominantCache_[path];
            Color result = Color.FromArgb(24, 26, 31);
            try
            {
                using (Bitmap small = new Bitmap(48, 48))
                {
                    using (Graphics sg = Graphics.FromImage(small))
                    {
                        sg.InterpolationMode = InterpolationMode.HighQualityBilinear;
                        sg.DrawImage(cover, new Rectangle(0, 0, 48, 48));
                    }
                    Dictionary<int, int> buckets = new Dictionary<int, int>();
                    for (int y = 0; y < 48; ++y)
                    {
                        for (int x = 0; x < 48; ++x)
                        {
                            Color p = small.GetPixel(x, y);
                            if (p.A < 200) continue;
                            int max = Math.Max(p.R, Math.Max(p.G, p.B));
                            int min = Math.Min(p.R, Math.Min(p.G, p.B));
                            if (max - min < 18) continue;
                            if (max < 28) continue;
                            int key = ((p.R >> 4) << 8) | ((p.G >> 4) << 4) | (p.B >> 4);
                            int cnt;
                            buckets.TryGetValue(key, out cnt);
                            buckets[key] = cnt + 1;
                        }
                    }
                    int bestKey = -1, bestCnt = 0;
                    foreach (KeyValuePair<int, int> kv in buckets)
                    {
                        if (kv.Value > bestCnt) { bestCnt = kv.Value; bestKey = kv.Key; }
                    }
                    if (bestKey >= 0)
                    {
                        long sr = 0, sg2 = 0, sb = 0, n = 0;
                        for (int y = 0; y < 48; ++y)
                        {
                            for (int x = 0; x < 48; ++x)
                            {
                                Color p = small.GetPixel(x, y);
                                if (p.A < 200) continue;
                                int key = ((p.R >> 4) << 8) | ((p.G >> 4) << 4) | (p.B >> 4);
                                if (key != bestKey) continue;
                                sr += p.R; sg2 += p.G; sb += p.B; ++n;
                            }
                        }
                        if (n > 0)
                            result = Color.FromArgb((int)(sr / n), (int)(sg2 / n), (int)(sb / n));
                    }
                }
            }
            catch { }
            dominantCache_[path] = result;
            return result;
        }

        private Image LoadCover(GameEntry gp)
        {
            if (cache_.ContainsKey(gp.GpkPath)) return cache_[gp.GpkPath];
            Image img = null;
            if (gp.CoverPng != null && gp.CoverPng.Length > 0)
            {
                try
                {
                    using (MemoryStream ms = new MemoryStream(gp.CoverPng))
                        img = Image.FromStream(ms);
                }
                catch { img = null; }
            }
            cache_[gp.GpkPath] = img;
            return img;
        }

        private void OnMouseMove(object sender, MouseEventArgs e)
        {
            int newHover = -1;
            for (int i = 0; i < games_.Count; ++i)
                if (RailRect(i).Contains(e.Location)) { newHover = i; break; }
            bool newLaunch = launchBtn_.Contains(e.Location);
            if (newHover != hover_ || newLaunch != launchHover_)
            {
                hover_ = newHover;
                launchHover_ = newLaunch;
                Cursor = (newHover >= 0 || newLaunch) ? Cursors.Hand : Cursors.Default;
                Invalidate();
            }
        }

        private void OnMouseDown(object sender, MouseEventArgs e)
        {
            if (e.Button != MouseButtons.Left) return;
            for (int i = 0; i < games_.Count; ++i)
                if (RailRect(i).Contains(e.Location))
                {
                    if (selected_ != i) { selected_ = i; Invalidate(); }
                    return;
                }
            if (launchBtn_.Width > 0 && launchBtn_.Contains(e.Location)) LaunchGame();
        }

        private void LaunchGame()
        {
            if (selected_ < 0 || busy_) return;
            if (running_)
            {
                try
                {
                    if (gameProc_ != null && !gameProc_.HasExited)
                    {
                        IntPtr hwnd = gameProc_.MainWindowHandle;
                        if (hwnd != IntPtr.Zero) SetForegroundWindow(hwnd);
                    }
                }
                catch (Exception) { }
                return;
            }
            GameEntry gp = games_[selected_];
            if (!File.Exists(gp.GpkPath))
            {
                MessageBox.Show("未找到游戏包：\n" + gp.GpkPath, "启动失败", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return;
            }
            string host = Path.Combine(BaseDir, "main.exe");
            if (!File.Exists(host))
            {
                MessageBox.Show("未找到游戏宿主：\n" + host, "启动失败", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return;
            }
            string argument = "--gpk " + Quote(gp.GpkPath);
            busy_ = true;
            Invalidate();
            try
            {
                Process p = StartProcessUnicode(host, BaseDir, argument);
                if (p == null) throw new Exception("进程未能创建");
                gameProc_ = p;
                p.EnableRaisingEvents = true;
                p.Exited += delegate
                {
                    BeginInvoke(new MethodInvoker(delegate
                    {
                        busy_ = false;
                        running_ = false;
                        gameProc_ = null;
                        Invalidate();
                    }));
                };
                busy_ = false;
                running_ = true;
                Invalidate();
            }
            catch (Exception ex)
            {
                busy_ = false;
                running_ = false;
                gameProc_ = null;
                Invalidate();
                MessageBox.Show("启动游戏失败：\n" + ex.Message, "启动失败", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }

        private static string Quote(string s)
        {
            return "\"" + s + "\"";
        }
    }

    internal static class Program
    {
        [STAThread]
        private static void Main()
        {
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            try
            {
                Application.Run(new LauncherForm());
            }
            catch (Exception ex)
            {
                File.WriteAllText(Path.Combine(Path.GetDirectoryName(Application.ExecutablePath), "lanc_error.log"), ex.ToString());
            }
        }
    }
}
