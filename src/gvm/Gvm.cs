using System;
using System.Drawing;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Windows.Forms;

namespace Gvm
{
    internal static class Theme
    {
        public static readonly Color Bg = Color.FromArgb(16, 17, 20);
        public static readonly Color Panel = Color.FromArgb(22, 24, 28);
        public static readonly Color OutBg = Color.FromArgb(12, 13, 16);
        public static readonly Color TextPrimary = Color.FromArgb(224, 228, 236);
        public static readonly Color TextDim = Color.FromArgb(140, 146, 156);
        public static readonly Color Accent = Color.FromArgb(79, 179, 217);
        public static readonly Color Warn = Color.FromArgb(224, 164, 92);
        public static readonly Color Err = Color.FromArgb(228, 106, 106);
        public static readonly Color Ok = Color.FromArgb(140, 200, 120);
        public static readonly Color Line = Color.FromArgb(44, 49, 59);

        public static readonly Font Mono = new Font("Consolas", 13f, FontStyle.Regular, GraphicsUnit.Pixel);
        public static readonly Font Ui = new Font("Microsoft YaHei UI", 13f, FontStyle.Regular, GraphicsUnit.Pixel);
    }

    internal sealed class DropPanel : Panel
    {
        public static readonly Color Idle = Color.FromArgb(26, 62, 38);
        public static readonly Color Hot = Color.FromArgb(44, 104, 60);
        public static readonly Color Busy = Color.FromArgb(38, 40, 46);
        public static readonly Color LabelIdle = Color.FromArgb(150, 230, 165);
        public static readonly Color LabelLoaded = Color.FromArgb(170, 240, 185);

        public event Action<string[]> FilesDropped;

        public DropPanel()
        {
            AllowDrop = true;
            DragEnter += delegate (object s, DragEventArgs e)
            {
                if (e.Data.GetDataPresent(DataFormats.FileDrop))
                {
                    e.Effect = DragDropEffects.Copy;
                    BackColor = Hot;
                }
                else
                {
                    e.Effect = DragDropEffects.None;
                }
            };
            DragLeave += delegate { BackColor = Idle; };
            DragDrop += delegate (object s, DragEventArgs e)
            {
                BackColor = Idle;
                string[] files = e.Data.GetData(DataFormats.FileDrop) as string[];
                if (files != null && files.Length > 0 && FilesDropped != null)
                    FilesDropped(files);
            };
        }
    }

    internal sealed class MainForm : Form
    {
        private readonly string baseDir_;
        private readonly string engineDll_;

        private DropPanel drop_;
        private Label fileLabel_;
        private Button runBtn_;
        private RichTextBox output_;

        private string currentScript_;
        private IntPtr engineModule_ = IntPtr.Zero;
        private GvtcierRunFn runFn_;
        private Thread worker_;
        private volatile bool running_;
        private FileStream captureStream_;
        private string capturePath_;
        private Thread tailThread_;
        private volatile bool tailing_;
        private long tailPos_;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate int GvtcierRunFn(IntPtr hostModule, IntPtr gamePathUtf8, IntPtr gameArgsUtf8);

        [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        private static extern IntPtr LoadLibraryW(string path);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern IntPtr GetProcAddress(IntPtr module, string name);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool FreeLibrary(IntPtr module);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool SetConsoleOutputCP(int codePage);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool SetStdHandle(int nStdHandle, IntPtr handle);

        [DllImport("ucrtbase.dll", SetLastError = true, CharSet = CharSet.Ansi)]
        private static extern IntPtr freopen(string path, string mode, IntPtr stream);

        [DllImport("ucrtbase.dll", EntryPoint = "__acrt_iob_func", SetLastError = true)]
        private static extern IntPtr __acrt_iob_func(int index);

        private const int STD_OUTPUT_HANDLE = -11;
        private const int STD_ERROR_HANDLE = -12;

        public MainForm()
        {
            baseDir_ = AppDomain.CurrentDomain.BaseDirectory;
            engineDll_ = Path.Combine(baseDir_, "Gvtcier.dll");

            Text = "Gvm";
            ClientSize = new Size(880, 560);
            MinimumSize = new Size(640, 400);
            StartPosition = FormStartPosition.CenterScreen;
            BackColor = Theme.Bg;
            ForeColor = Theme.TextPrimary;
            Font = Theme.Ui;

            drop_ = new DropPanel();
            drop_.Dock = DockStyle.Top;
            drop_.Height = 92;
            drop_.BackColor = DropPanel.Idle;
            drop_.Paint += DropPaint;
            drop_.FilesDropped += OnFilesDropped;

            fileLabel_ = new Label();
            fileLabel_.Text = "将 .gvts 脚本拖入此处";
            fileLabel_.Dock = DockStyle.Fill;
            fileLabel_.TextAlign = ContentAlignment.MiddleCenter;
            fileLabel_.ForeColor = DropPanel.LabelIdle;
            fileLabel_.Font = Theme.Ui;
            fileLabel_.BackColor = Color.Transparent;

            runBtn_ = new Button();
            runBtn_.Text = "运行";
            runBtn_.Dock = DockStyle.Right;
            runBtn_.Width = 110;
            runBtn_.FlatStyle = FlatStyle.Flat;
            runBtn_.FlatAppearance.BorderColor = Theme.Accent;
            runBtn_.ForeColor = Theme.Accent;
            runBtn_.BackColor = Theme.Panel;
            runBtn_.Enabled = false;
            runBtn_.Click += delegate { StartRun(); };

            drop_.Controls.Add(fileLabel_);
            drop_.Controls.Add(runBtn_);

            output_ = new RichTextBox();
            output_.Dock = DockStyle.Fill;
            output_.ReadOnly = true;
            output_.BackColor = Theme.OutBg;
            output_.ForeColor = Theme.TextPrimary;
            output_.BorderStyle = BorderStyle.None;
            output_.Font = Theme.Mono;
            output_.WordWrap = false;
            output_.ScrollBars = RichTextBoxScrollBars.Both;
            output_.DetectUrls = false;
            output_.HideSelection = false;

            Controls.Add(output_);
            Controls.Add(drop_);

            Load += delegate
            {
                try { Directory.SetCurrentDirectory(baseDir_); }
                catch { }
                Print("Gvm 就绪。将 .gvts 脚本拖入上方区域，然后点击运行。", Theme.TextDim);
                Print("资源文件自动从 Gvts 脚本所在目录加载。", Theme.TextDim);
            };
            FormClosing += delegate { UnloadEngine(); };
        }

        private void DropPaint(object sender, PaintEventArgs e)
        {
            using (Pen p = new Pen(Theme.Line))
                e.Graphics.DrawLine(p, 0, drop_.Height - 1, drop_.Width, drop_.Height - 1);
        }

        private void OnFilesDropped(string[] files)
        {
            if (running_) return;
            string path = null;
            foreach (string f in files)
            {
                if (f.EndsWith(".gvts", StringComparison.OrdinalIgnoreCase)) { path = f; break; }
            }
            if (path == null) path = files[0];
            if (!File.Exists(path)) return;

            currentScript_ = path;
            fileLabel_.Text = Path.GetFileName(path);
            fileLabel_.ForeColor = DropPanel.LabelLoaded;
            runBtn_.Enabled = true;
            Print("已载入: " + path, Theme.Accent);
        }

        private void StartRun()
        {
            if (running_) return;
            if (string.IsNullOrEmpty(currentScript_) || !File.Exists(currentScript_)) return;
            if (!EnsureEngine()) return;

            running_ = true;
            runBtn_.Enabled = false;
            drop_.AllowDrop = false;
            drop_.BackColor = DropPanel.Busy;
            fileLabel_.ForeColor = Theme.TextDim;

            string script = currentScript_;
            worker_ = new Thread(delegate () { RunWorker(script); });
            worker_.IsBackground = true;
            worker_.Start();
        }

        private void RunWorker(string scriptPath)
        {
            int rc = 1;
            string oldCwd = Directory.GetCurrentDirectory();
            try
            {
                try { File.Delete(Path.Combine(baseDir_, "gvtcier.log")); } catch { }
                try { File.Delete(Path.Combine(baseDir_, "bug.log")); } catch { }
                BeginCapture();
                string scriptDir = Path.GetDirectoryName(Path.GetFullPath(scriptPath));
                try { Directory.SetCurrentDirectory(scriptDir); }
                catch { Directory.SetCurrentDirectory(baseDir_); }

                IntPtr pPath = Utf8Alloc(scriptPath);
                IntPtr pArgs = Utf8Alloc("--dev");
                try
                {
                    rc = runFn_(engineModule_, pPath, pArgs);
                }
                finally
                {
                    Marshal.FreeHGlobal(pPath);
                    Marshal.FreeHGlobal(pArgs);
                }
            }
            catch (Exception ex)
            {
                AppendLine("运行异常: " + ex.Message, Theme.Err);
            }
            finally
            {
                try { Directory.SetCurrentDirectory(oldCwd); } catch { }
                EndCapture();
                string bug = CollectBugLog();
                AppendLine("", Theme.TextPrimary);
                AppendLine("[完成] 引擎返回 " + rc, rc == 0 ? Theme.Ok : Theme.Err);
                if (bug != null) AppendLine("[错误] 已写入 bug.log", Theme.Err);
                running_ = false;
                BeginInvoke(new MethodInvoker(delegate
                {
                    runBtn_.Enabled = true;
                    drop_.AllowDrop = true;
                    drop_.BackColor = DropPanel.Idle;
                    fileLabel_.ForeColor = DropPanel.LabelIdle;
                }));
            }
        }

        private string CollectBugLog()
        {
            string logPath = Path.Combine(baseDir_, "gvtcier.log");
            if (!File.Exists(logPath)) return null;
            string[] lines;
            try { lines = File.ReadAllLines(logPath, Encoding.UTF8); }
            catch { return null; }

            StringBuilder sb = new StringBuilder();
            foreach (string line in lines)
            {
                if (line.Contains("[脚本错误]") || line.Contains("[GvtcierGvts]"))
                {
                    sb.AppendLine(line.Trim());
                    AppendLine(line.Trim(), Theme.Err);
                }
            }
            if (sb.Length == 0) return null;

            try
            {
                string bugPath = Path.Combine(baseDir_, "bug.log");
                StringBuilder outText = new StringBuilder();
                outText.AppendLine("Gvm 脚本错误报告");
                outText.AppendLine("时间: " + DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss"));
                outText.AppendLine("脚本: " + currentScript_);
                outText.AppendLine("--------------------------------");
                outText.Append(sb.ToString());
                File.WriteAllText(bugPath, outText.ToString(), new UTF8Encoding(false));
                return bugPath;
            }
            catch { return null; }
        }

        private bool EnsureEngine()
        {
            if (runFn_ != null) return true;
            if (!File.Exists(engineDll_))
            {
                AppendLine("找不到引擎: " + engineDll_, Theme.Err);
                return false;
            }
            if (engineModule_ == IntPtr.Zero)
            {
                engineModule_ = LoadLibraryW(engineDll_);
                if (engineModule_ == IntPtr.Zero)
                {
                    AppendLine("加载引擎失败，错误码 " + Marshal.GetLastWin32Error(), Theme.Err);
                    return false;
                }
            }
            IntPtr proc = GetProcAddress(engineModule_, "Gvtcier_Run");
            if (proc == IntPtr.Zero)
            {
                AppendLine("引擎缺少 Gvtcier_Run 导出。", Theme.Err);
                return false;
            }
            runFn_ = (GvtcierRunFn)Marshal.GetDelegateForFunctionPointer(proc, typeof(GvtcierRunFn));
            return true;
        }

        private void UnloadEngine()
        {
            runFn_ = null;
            if (engineModule_ != IntPtr.Zero)
            {
                FreeLibrary(engineModule_);
                engineModule_ = IntPtr.Zero;
            }
        }

        private void BeginCapture()
        {
            string tempPath = Path.GetTempPath();
            if (!TryOpenCapture(tempPath)) TryOpenCapture(baseDir_);
        }

        private bool TryOpenCapture(string dir)
        {
            try
            {
                capturePath_ = Path.Combine(dir, "gvm_" + Guid.NewGuid().ToString("N") + ".log");
                captureStream_ = new FileStream(capturePath_, FileMode.Create, FileAccess.ReadWrite, FileShare.ReadWrite);
                IntPtr osHandle = captureStream_.SafeFileHandle.DangerousGetHandle();
                SetStdHandle(STD_OUTPUT_HANDLE, osHandle);
                SetStdHandle(STD_ERROR_HANDLE, osHandle);
                IntPtr stdoutFile = __acrt_iob_func(1);
                IntPtr stderrFile = __acrt_iob_func(2);
                freopen(capturePath_, "w", stdoutFile);
                freopen(capturePath_, "w", stderrFile);
                SetConsoleOutputCP(65001);
                StartTail();
                return true;
            }
            catch
            {
                capturePath_ = null;
                captureStream_ = null;
                return false;
            }
        }

        private void StartTail()
        {
            if (tailing_) return;
            tailing_ = true;
            tailPos_ = 0;
            tailThread_ = new Thread(TailLoop);
            tailThread_.IsBackground = true;
            tailThread_.Start();
        }

        private void TailLoop()
        {
            byte[] buf = new byte[8192];
            while (tailing_)
            {
                try
                {
                    if (captureStream_ != null && captureStream_.Length > tailPos_)
                    {
                        captureStream_.Seek(tailPos_, SeekOrigin.Begin);
                        int n = captureStream_.Read(buf, 0, buf.Length);
                        if (n > 0)
                        {
                            tailPos_ += n;
                            string text = Encoding.UTF8.GetString(buf, 0, n);
                            BeginInvoke(new MethodInvoker(delegate { AppendRaw(text); }));
                        }
                    }
                }
                catch { }
                Thread.Sleep(40);
            }
        }

        private void EndCapture()
        {
            Thread.Sleep(120);
            tailing_ = false;
            if (tailThread_ != null) { try { tailThread_.Join(500); } catch { } tailThread_ = null; }
            try
            {
                if (captureStream_ != null && captureStream_.Length > tailPos_)
                {
                    captureStream_.Seek(tailPos_, SeekOrigin.Begin);
                    byte[] buf = new byte[captureStream_.Length - tailPos_];
                    int n = captureStream_.Read(buf, 0, buf.Length);
                    if (n > 0) AppendRaw(Encoding.UTF8.GetString(buf, 0, n));
                }
            }
            catch { }
            if (captureStream_ != null) { try { captureStream_.Dispose(); } catch { } captureStream_ = null; }
            try { if (capturePath_ != null && File.Exists(capturePath_)) File.Delete(capturePath_); } catch { }
            capturePath_ = null;
        }

        private IntPtr Utf8Alloc(string s)
        {
            byte[] bytes = Encoding.UTF8.GetBytes(s);
            IntPtr p = Marshal.AllocHGlobal(bytes.Length + 1);
            Marshal.Copy(bytes, 0, p, bytes.Length);
            Marshal.WriteByte(p, bytes.Length, 0);
            return p;
        }

        private void AppendLine(string text, Color color)
        {
            AppendRaw(text + Environment.NewLine, color);
        }

        private void Print(string text, Color color)
        {
            AppendRaw(text + Environment.NewLine, color);
        }

        private void AppendRaw(string text, Color? color = null)
        {
            if (InvokeRequired) { BeginInvoke(new MethodInvoker(delegate { AppendRaw(text, color); })); return; }
            output_.SelectionStart = output_.TextLength;
            output_.SelectionLength = 0;
            output_.SelectionColor = color ?? Theme.TextPrimary;
            output_.AppendText(text);
            output_.SelectionStart = output_.TextLength;
            output_.ScrollToCaret();
        }
    }

    internal static class Program
    {
        [STAThread]
        private static void Main()
        {
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Application.Run(new MainForm());
        }
    }
}
