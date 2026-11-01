// SPDX-License-Identifier: GPL-3.0-only
using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.IO.Ports;
using System.Runtime.InteropServices;
using System.Threading;
using System.Windows.Forms;
using Microsoft.Win32;

[assembly: System.Reflection.AssemblyTitle("USBAirPods")]
[assembly: System.Reflection.AssemblyProduct("USBAirPods")]

namespace USBPodsTray {
    internal static class Program {
        [DllImport("shell32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern int SetCurrentProcessExplicitAppUserModelID(string appID);

        [STAThread]
        private static void Main(string[] args) {
            bool created;
            using (Mutex mutex = new Mutex(true, @"Local\USBPodsBatteryTray", out created)) {
                if (!created) return;
                SetCurrentProcessExplicitAppUserModelID("ermanon.USBAirPods");
                Application.EnableVisualStyles();
                Application.SetCompatibleTextRenderingDefault(false);
                using (TrayContext context = new TrayContext()) {
                    if (Array.IndexOf(args, "--smoke-test") >= 0) {
                        System.Windows.Forms.Timer quit = new System.Windows.Forms.Timer();
                        quit.Interval = 3000;
                        quit.Tick += delegate { quit.Stop(); quit.Dispose(); context.ExitThread(); };
                        quit.Start();
                    }
                    Application.Run(context);
                }
                mutex.ReleaseMutex();
            }
        }
    }

    internal sealed class TrayContext : ApplicationContext {
        private readonly SerialMonitor monitor = new SerialMonitor();
        private readonly NotifyIcon icon = new NotifyIcon();
        private readonly string[] names = {"Left earbud", "Right earbud", "Charging case"};
        private readonly ToolStripMenuItem[] rows = new ToolStripMenuItem[3];
        private readonly ToolStripMenuItem connection = new ToolStripMenuItem();
        private readonly ToolStripMenuItem deviceName = new ToolStripMenuItem("AirPods device");
        private readonly ToolStripMenuItem renameDevice = new ToolStripMenuItem("Rename connected AirPods…");
        private readonly ToolStripMenuItem ports = new ToolStripMenuItem("USB connection");
        private readonly ToolStripMenuItem ancMenu = new ToolStripMenuItem("ANC");
        private readonly ToolStripMenuItem[] ancModes = new ToolStripMenuItem[4];
        private readonly string[] ancNames = {"Unknown", "Off", "Noise cancellation", "Transparency", "Adaptive"};
        private readonly ContextMenuStrip menu = new ContextMenuStrip();
        private readonly System.Windows.Forms.Timer timer = new System.Windows.Forms.Timer();
        private Form details;
        private BatteryDashboard dashboard;
        private string iconState;
        private bool textIcon;
        [DllImport("user32.dll")] private static extern bool DestroyIcon(IntPtr handle);

        public TrayContext() {
            connection.Enabled = false;
            menu.Items.Add(connection);
            deviceName.Enabled = false;
            renameDevice.Enabled = false;
            renameDevice.Click += delegate { RenameCurrentDevice(); };
            menu.Items.Add(deviceName);
            menu.Items.Add(renameDevice);
            menu.Items.Add(new ToolStripSeparator());
            for (int i = 0; i < 3; i++) { rows[i] = new ToolStripMenuItem(); rows[i].Enabled = false; menu.Items.Add(rows[i]); }
            menu.Items.Add(new ToolStripSeparator());
            menu.Items.Add("Battery status", null, delegate { ShowDetails(); });
            for (int mode = 1; mode <= 4; mode++) {
                int target = mode;
                ToolStripMenuItem item = new ToolStripMenuItem(ancNames[mode]);
                item.Click += delegate { monitor.RequestAnc(target); Update(); };
                item.ToolTipText = "This mode becomes available after the headphones report it at least once.";
                ancModes[mode - 1] = item;
                ancMenu.DropDownItems.Add(item);
            }
            menu.Items.Add(ancMenu);
            menu.Items.Add(ports);
            ToolStripMenuItem styles = new ToolStripMenuItem("Tray icon");
            ToolStripMenuItem bars = new ToolStripMenuItem("Earbuds · smooth fill (default)");
            ToolStripMenuItem numbers = new ToolStripMenuItem("Battery percentages · two rows");
            bars.Checked = true;
            bars.Click += delegate { textIcon = false; bars.Checked = true; numbers.Checked = false; Update(); };
            numbers.Click += delegate { textIcon = true; bars.Checked = false; numbers.Checked = true; Update(); };
            styles.DropDownItems.Add(bars); styles.DropDownItems.Add(numbers);
            menu.Items.Add(styles);
            ports.DropDownOpening += delegate { PopulatePorts(); };
            menu.Items.Add("Exit", null, delegate { ExitThread(); });
            icon.ContextMenuStrip = menu;
            icon.MouseClick += delegate(object sender, MouseEventArgs e) { if (e.Button == MouseButtons.Left) ShowDetails(); };
            Update();
            icon.Visible = true;
            timer.Interval = 1000;
            timer.Tick += delegate { Update(); };
            timer.Start();
        }
        private void PopulatePorts() {
            ports.DropDownItems.Clear();
            ToolStripMenuItem auto = new ToolStripMenuItem("Automatic (USBAirPods)");
            auto.Checked = monitor.SelectedPort.Length == 0;
            auto.Click += delegate { monitor.SelectPort(""); };
            ports.DropDownItems.Add(auto);
            string[] available = SerialPort.GetPortNames();
            Array.Sort(available);
            foreach (string name in available) {
                string selected = name;
                ToolStripMenuItem item = new ToolStripMenuItem(name);
                item.Checked = monitor.SelectedPort == name;
                item.Click += delegate { monitor.SelectPort(selected); };
                ports.DropDownItems.Add(item);
            }
        }
        private void ShowDetails() {
            if (details == null || details.IsDisposed) {
                details = new Form();
                details.Text = "USBAirPods · Battery status";
                details.ClientSize = new Size(500, 450);
                details.FormBorderStyle = FormBorderStyle.FixedDialog;
                details.MaximizeBox = false;
                details.StartPosition = FormStartPosition.Manual;
                details.Font = new Font("Segoe UI", 10);
                dashboard = new BatteryDashboard();
                dashboard.Dock = DockStyle.Fill;
                dashboard.ModeRequested += delegate(int mode) { ancModes[mode - 1].PerformClick(); };
                details.Controls.Add(dashboard);
            }
            Update();
            // Use the tray click's monitor and exclude its taskbar area.
            Rectangle area = Screen.FromPoint(Cursor.Position).WorkingArea;
            int margin = Math.Max(8, (int)Math.Round(12 * details.DeviceDpi / 96.0));
            details.Location = new Point(
                Math.Max(area.Left, area.Right - details.Width - margin),
                Math.Max(area.Top, area.Bottom - details.Height - margin));
            details.Show();
            details.Activate();
        }
        private static string AliasFor(string address) {
            if (String.IsNullOrEmpty(address)) return "Your AirPods";
            using (RegistryKey key = Registry.CurrentUser.CreateSubKey(@"Software\USBAirPods\DeviceAliases")) {
                string alias = key.GetValue(address.Replace(':', '-')) as string;
                return String.IsNullOrWhiteSpace(alias) ? "Your AirPods" : alias.Trim();
            }
        }
        private static void SaveAlias(string address, string alias) {
            using (RegistryKey key = Registry.CurrentUser.CreateSubKey(@"Software\USBAirPods\DeviceAliases")) {
                string valueName = address.Replace(':', '-');
                if (String.IsNullOrWhiteSpace(alias)) key.DeleteValue(valueName, false);
                else key.SetValue(valueName, alias.Trim(), RegistryValueKind.String);
            }
        }
        private void RenameCurrentDevice() {
            DeviceSnapshot device = monitor.Device;
            if (device == null || !device.Connected || String.IsNullOrEmpty(device.Address)) return;
            string result;
            if (!AskForName(AliasFor(device.Address), out result)) return;
            SaveAlias(device.Address, result);
            Update();
        }
        private static bool AskForName(string current, out string result) {
            result = current;
            using (Form dialog = new Form())
            using (TextBox input = new TextBox())
            using (Button save = new Button())
            using (Button cancel = new Button()) {
                dialog.Text = "Name your AirPods";
                dialog.ClientSize = new Size(330, 108);
                dialog.FormBorderStyle = FormBorderStyle.FixedDialog;
                dialog.MaximizeBox = false; dialog.MinimizeBox = false;
                dialog.StartPosition = FormStartPosition.CenterScreen;
                Label label = new Label(); label.Text = "Name for this paired AirPods set:"; label.SetBounds(12, 12, 300, 20);
                input.Text = current == "Your AirPods" ? "" : current; input.SetBounds(12, 36, 306, 24); input.MaxLength = 40;
                save.Text = "Save"; save.DialogResult = DialogResult.OK; save.SetBounds(162, 72, 75, 26);
                cancel.Text = "Cancel"; cancel.DialogResult = DialogResult.Cancel; cancel.SetBounds(243, 72, 75, 26);
                dialog.Controls.AddRange(new Control[] { label, input, save, cancel });
                dialog.AcceptButton = save; dialog.CancelButton = cancel;
                if (dialog.ShowDialog() != DialogResult.OK) return false;
                result = input.Text; return true;
            }
        }
        private void Update() {
            BatterySnapshot snapshot = monitor.Snapshot;
            DateTime now = DateTime.UtcNow;
            bool alive = snapshot != null && (now - snapshot.ReceivedUtc).TotalSeconds < 8;
            bool connected = alive && snapshot.Connected;
            DeviceSnapshot device = monitor.Device;
            string address = device != null && device.Connected &&
                (now - device.ReceivedUtc).TotalSeconds < 8 ? device.Address : null;
            string displayName = AliasFor(address);
            deviceName.Text = address == null ? "AirPods device: waiting for address" : displayName + " · " + address;
            renameDevice.Enabled = address != null;
            Color left = BatteryColor(snapshot, 0, now, connected);
            Color right = BatteryColor(snapshot, 1, now, connected);
            int leftLevel = connected && snapshot.Fresh(0, now) ? snapshot.Values[0].Level : -1;
            int rightLevel = connected && snapshot.Fresh(1, now) ? snapshot.Values[1].Level : -1;
            string nextIconState = leftLevel + ":" + rightLevel + ":" + connected + ":" + textIcon;
            if (iconState != nextIconState) {
                Icon old = icon.Icon;
                icon.Icon = DrawIcon(left, right, !connected, leftLevel, rightLevel, textIcon);
                if (old != null) old.Dispose();
                iconState = nextIconState;
            }
            connection.Text = monitor.Message;
            string text = (connected ? "Connected" : connection.Text) + "\n\n";
            string tip = address == null ? "USBAirPods" : displayName;
            string[] shortNames = {"Left", "Right", "Case"};
            for (int i = 0; i < 3; i++) {
                bool fresh = alive && snapshot.Fresh(i, now);
                BatteryValue value = alive ? snapshot.Values[i] : null;
                string number = fresh ? value.Level.ToString() : "--";
                bool charging = fresh && value.Status == 1;
                string state = !alive ? "Waiting for data" : !snapshot.Connected ? "Disconnected" :
                    value.Level < 0 ? (value.Status == 4 ? "Disconnected" : "Unknown") :
                    !fresh ? "Outdated reading" : charging ? "Charging" : "On battery";
                rows[i].Text = names[i] + ": " + (fresh ? number + "%" : "—") + " · " + state;
                text += shortNames[i] + ": " + (fresh ? number + "%" : "—") + (charging ? " · Charging" : "") + "\n";
                if (alive && value.Level >= 0 && !fresh)
                    text += "  Last known: " + value.Level + "% (" + Math.Floor((value.Age + (now - snapshot.ReceivedUtc).TotalMilliseconds) / 60000) + " min ago)\n";
                tip += "\n" + shortNames[i] + ": " + (fresh ? number + "%" : "--") + (charging ? " +" : "");
            }
            icon.Text = tip;
            AncSnapshot anc = monitor.Anc;
            bool ancCurrent = connected && anc != null && anc.Connected &&
                (now - anc.ReceivedUtc).TotalSeconds < 8;
            string ancLabel = ancCurrent && anc.Mode != 0 ? ancNames[anc.Mode] : "Unavailable";
            ancMenu.Text = "ANC: " + ancLabel;
            for (int i = 0; i < 4; i++) {
                ancModes[i].Checked = ancCurrent && anc.Mode == i + 1;
                ancModes[i].Enabled = ancCurrent && anc.CanSet(i + 1) && anc.Mode != i + 1 &&
                    !monitor.AncBusy;
            }
            text += "\nANC: " + ancLabel;
            string ancStatus = monitor.AncMessage;
            if (connected && ancStatus != "Mode confirmed by the headphones" &&
                ancStatus != "Mode received from the headphones")
                text += "\n" + ancStatus;

            if (dashboard != null && !dashboard.IsDisposed) {
                string notice = ancStatus == "Mode confirmed by the headphones" ||
                    ancStatus == "Mode received from the headphones" ? "" : ancStatus;
                dashboard.SetStatus(snapshot, alive, connected, ancLabel, notice, connection.Text, text, displayName, address);
                dashboard.SetModes(ancCurrent, anc, monitor.AncBusy);
            }
        }
        private static Color BatteryColor(BatterySnapshot snapshot, int index, DateTime now, bool connected) {
            if (!connected || !snapshot.Fresh(index, now)) return Color.WhiteSmoke;
            int level = snapshot.Values[index].Level;
            return level < 10 ? Color.FromArgb(255, 65, 65) :
                   level < 20 ? Color.FromArgb(255, 215, 0) : Color.WhiteSmoke;
        }
        internal static Icon DrawIcon(Color left, Color right, bool disconnected, int leftLevel, int rightLevel, bool textStyle) {
            using (Bitmap bmp = new Bitmap(64, 64)) {
                using (Graphics g = Graphics.FromImage(bmp)) {
                    g.SmoothingMode = SmoothingMode.AntiAlias;
                    g.Clear(Color.Transparent);
                    if (textStyle) {
                        g.Clear(Color.FromArgb(25,35,50));
                        for (int i=0;i<2;i++) {
                            int level = i==0 ? leftLevel : rightLevel;
                            string label = level < 0 ? "--" : level.ToString();
                            using (GraphicsPath glyphs = new GraphicsPath())
                            using (FontFamily family = new FontFamily("Arial")) {
                                glyphs.AddString(label,family,(int)FontStyle.Bold,32,PointF.Empty,StringFormat.GenericTypographic);
                                RectangleF bounds = glyphs.GetBounds();
                                float sx=62/bounds.Width, sy=29/bounds.Height;
                                using (Matrix fit=new Matrix(sx,0,0,sy,1-bounds.X*sx,i*32+1-bounds.Y*sy))
                                    glyphs.Transform(fit);
                                using (Brush fill = new SolidBrush(disconnected ? Color.FromArgb(255,100,100) : i==0 ? left : right))
                                    g.FillPath(fill,glyphs);
                            }
                        }
                    } else {
                        using (Pen outline = new Pen(Color.FromArgb(35,45,60),2)) {
                            for (int i=0;i<2;i++) {
                                float stem=i==0 ? 16 : 36, head=i==0 ? 4 : 36;
                                int level=i==0 ? leftLevel : rightLevel;
                                using (GraphicsPath shape = new GraphicsPath()) {
                                    shape.AddArc(stem,20,12,12,180,180);
                                    shape.AddArc(stem,46,12,12,0,180); shape.CloseFigure();
                                    shape.AddEllipse(head,6,24,26);
                                    using (Region silhouette = new Region(shape)) {
                                        silhouette.MakeEmpty();
                                        using (GraphicsPath body = new GraphicsPath()) {
                                            body.AddArc(stem,20,12,12,180,180);
                                            body.AddArc(stem,46,12,12,0,180); body.CloseFigure();
                                            silhouette.Union(body);
                                        }
                                        silhouette.Union(new RectangleF(stem,26,12,20));
                                        using (GraphicsPath cap = new GraphicsPath()) { cap.AddEllipse(head,6,24,26); silhouette.Union(cap); }
                                        var saved=g.Save(); g.SetClip(silhouette, CombineMode.Replace);
                                        using (Brush empty=new SolidBrush(level<0 ? Color.FromArgb(150,160,170) : Color.FromArgb(65,75,85)))
                                            g.FillRectangle(empty,0,6,64,52);
                                        if (level>0) {
                                            float height=52*Math.Min(100,level)/100f;
                                            using (Brush fill=new SolidBrush(i==0 ? left : right))
                                                g.FillRectangle(fill,0,58-height,64,height);
                                        }
                                        g.Restore(saved);
                                    }
                                    g.DrawPath(outline,shape);
                                }
                            }
                        }
                        if (disconnected) {
                            using (Pen border = new Pen(Color.FromArgb(30,35,45),12))
                            using (Pen cross = new Pen(Color.FromArgb(255,65,65),7)) {
                                border.StartCap=border.EndCap=cross.StartCap=cross.EndCap=LineCap.Round;
                                g.DrawLine(border,12,12,52,52); g.DrawLine(border,52,12,12,52);
                                g.DrawLine(cross,12,12,52,52); g.DrawLine(cross,52,12,12,52);
                            }
                        }
                    }
                }
                IntPtr handle=bmp.GetHicon();
                try { using (Icon borrowed=Icon.FromHandle(handle)) return (Icon)borrowed.Clone(); }
                finally { DestroyIcon(handle); }
            }
        }
        protected override void ExitThreadCore() {
            timer.Stop();
            icon.Visible = false;
            if (details != null) details.Close();
            base.ExitThreadCore();
        }
        protected override void Dispose(bool disposing) {
            if (disposing) {
                timer.Dispose(); monitor.Dispose();
                Icon graphic = icon.Icon; icon.Dispose(); if (graphic != null) graphic.Dispose();
                menu.Dispose();
                if (details != null) details.Dispose();
            }
            base.Dispose(disposing);
        }
    }
}
