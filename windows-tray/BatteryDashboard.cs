// SPDX-License-Identifier: GPL-3.0-only
using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Windows.Forms;

namespace USBPodsTray {
    internal sealed class BatteryDashboard : Control {
        private readonly ToolTip tooltip = new ToolTip();
        private readonly Button[] modeButtons = new Button[4];
        public event Action<int> ModeRequested;
        private BatterySnapshot snapshot;
        private bool alive, connected;
        private string mode = "Unavailable", notice = "", connection = "Connecting…";
        private string deviceName = "Your AirPods", deviceAddress = "";
        public BatteryDashboard() {
            DoubleBuffered = true;
            BackColor = Color.FromArgb(245, 247, 250);
            AccessibleName = "AirPods battery and noise control status";
            string[] labels = {"Off", "Noise cancel", "Transparency", "Adaptive"};
            for (int i = 0; i < 4; i++) {
                int target = i + 1;
                Button button = new Button();
                button.Text = labels[i];
                button.AccessibleName = target == 2 ? "Noise cancellation" : labels[i];
                button.FlatStyle = FlatStyle.Flat;
                button.FlatAppearance.BorderColor = Color.FromArgb(205,215,230);
                button.Font = new Font("Segoe UI", 9);
                button.Enabled = false;
                button.Click += delegate { if (ModeRequested != null) ModeRequested(target); };

                modeButtons[i] = button;
                Controls.Add(button);
            }
        }
        public void SetModes(bool current, AncSnapshot anc, bool busy) {
            for (int i=0; i<4; i++) {
                bool selected = current && anc != null && anc.Mode == i+1;
                modeButtons[i].Enabled = current && anc != null && anc.CanSet(i+1) && !selected && !busy;
                modeButtons[i].BackColor = selected ? Color.FromArgb(206,224,251) : Color.White;
                modeButtons[i].AccessibleDescription = selected ? "Current mode" : modeButtons[i].Enabled ? "Select mode" : "Unavailable";
            }
        }
        protected override void OnResize(EventArgs e) {
            base.OnResize(e);
            if (modeButtons == null) return;
            for (int i=0; i<4; i++) if (modeButtons[i] != null)
                modeButtons[i].Bounds = new Rectangle((int)((24+i*115)*Width/500f), (int)(338*Height/450f),
                    (int)(107*Width/500f), (int)(34*Height/450f));
        }
        public void SetStatus(BatterySnapshot value, bool isAlive, bool isConnected,
            string ancMode, string message, string connectionMessage, string details, string name, string address) {
            snapshot = value; alive = isAlive; connected = isConnected;
            mode = ancMode; notice = message; connection = connectionMessage;
            deviceName = name; deviceAddress = address ?? "";
            AccessibleDescription = details;
            tooltip.SetToolTip(this, details);
            Invalidate();
        }
        private static GraphicsPath Rounded(float x, float y, float w, float h, float r) {
            GraphicsPath p = new GraphicsPath();
            p.AddArc(x,y,r*2,r*2,180,90); p.AddArc(x+w-r*2,y,r*2,r*2,270,90);
            p.AddArc(x+w-r*2,y+h-r*2,r*2,r*2,0,90); p.AddArc(x,y+h-r*2,r*2,r*2,90,90);
            p.CloseFigure(); return p;
        }
        private static void Box(Graphics g, Color color, float x, float y, float w, float h, float r) {
            using (GraphicsPath p = Rounded(x,y,w,h,r)) using (Brush b = new SolidBrush(color)) g.FillPath(b,p);
        }
        private static void DrawText(Graphics g, string text, float size, FontStyle style, Color color,
            float x, float y, float w, float h, bool centered) {
            using (Font f = new Font("Segoe UI",size,style,GraphicsUnit.Pixel))
            using (Brush b = new SolidBrush(color)) using (StringFormat s = new StringFormat()) {
                s.Alignment = centered ? StringAlignment.Center : StringAlignment.Near;
                s.LineAlignment = StringAlignment.Center;
                s.Trimming = StringTrimming.EllipsisCharacter;
                g.DrawString(text,f,b,new RectangleF(x,y,w,h),s);
            }
        }
        private static void Device(Graphics g, int index, float x, float y) {
            using (Pen outline = new Pen(Color.FromArgb(148,163,184),2)) {
                if (index == 2) {
                    using (GraphicsPath p = Rounded(x-36,y-20,72,52,16)) {
                        g.FillPath(Brushes.White,p); g.DrawPath(outline,p);
                    }
                    g.DrawLine(outline,x-33,y-3,x+33,y-3);
                    g.FillEllipse(Brushes.LightSlateGray,x-2,y+12,4,4);
                } else {
                    float stem = index == 0 ? x+3 : x-15;
                    using (GraphicsPath p = Rounded(stem,y-9,12,49,6)) {
                        g.FillPath(Brushes.White,p); g.DrawPath(outline,p);
                    }
                    g.FillEllipse(Brushes.White,x-24,y-28,46,34);
                    g.DrawEllipse(outline,x-24,y-28,46,34);
                    g.FillEllipse(Brushes.SlateGray,index == 0 ? x-19 : x+5,y-20,12,18);
                }
            }
        }
        protected override void OnPaint(PaintEventArgs e) {
            base.OnPaint(e);
            Graphics g = e.Graphics;
            g.ScaleTransform(ClientSize.Width/500f,ClientSize.Height/450f);
            g.SmoothingMode = SmoothingMode.AntiAlias;
            Color ink = Color.FromArgb(25,35,50), muted = Color.FromArgb(100,116,139);
            DrawText(g,deviceName,25,FontStyle.Bold,ink,24,16,360,38,false);
            using (Brush b = new SolidBrush(connected ? Color.SeaGreen : Color.DarkOrange)) g.FillEllipse(b,26,65,8,8);
            DrawText(g,connected ? "Connected" + (deviceAddress.Length == 0 ? "" : " · " + deviceAddress) : "Not connected",13,FontStyle.Regular,muted,42,55,390,28,false);
            DateTime now = DateTime.UtcNow;
            string[] names = {"LEFT","RIGHT","CASE"};
            for (int i=0;i<3;i++) {
                float x=24+i*154;
                Box(g,Color.White,x,96,144,174,14);
                DrawText(g,names[i],11,FontStyle.Bold,muted,x,104,144,22,true);
                Device(g,i,x+72,164);
                bool fresh = alive && snapshot != null && snapshot.Fresh(i,now);
                BatteryValue v = snapshot == null ? null : snapshot.Values[i];
                string percent = fresh ? v.Level+"%" : "—";
                DrawText(g,percent,27,FontStyle.Bold,ink,x,208,144,36,true);
                Color battery = !fresh ? Color.LightGray : v.Level<10 ? Color.IndianRed : v.Level<20 ? Color.Goldenrod : Color.SeaGreen;
                Box(g,Color.FromArgb(233,238,244),x+30,250,84,5,2);
                if (fresh && v.Level>0) Box(g,battery,x+30,250,Math.Max(4,84*v.Level/100f),5,2);
                if (fresh && v.Status==1) using (Brush charge = new SolidBrush(Color.SeaGreen)) g.FillPolygon(charge, new PointF[] { new PointF(x+122,215), new PointF(x+115,228), new PointF(x+121,228), new PointF(x+118,238), new PointF(x+128,223), new PointF(x+122,223) });
            }
            Box(g,Color.FromArgb(229,236,248),24,284,452,42,12);
            DrawText(g,"ANC",12,FontStyle.Bold,muted,40,284,42,42,false);
            DrawText(g,mode,16,FontStyle.Bold,ink,88,284,370,42,false);
            string footer = connected ? notice : connection;
            DrawText(g,"Change modes on your AirPods first to enable them here.",12,FontStyle.Regular,muted,26,380,448,26,false);
            DrawText(g,footer,12,FontStyle.Regular,muted,26,411,448,34,false);
        }
        protected override void Dispose(bool disposing) {
            if (disposing) tooltip.Dispose();
            base.Dispose(disposing);
        }
    }
}
