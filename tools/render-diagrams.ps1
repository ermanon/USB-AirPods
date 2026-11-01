$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
public static class ReadableDiagrams {
    static Bitmap canvas; static Graphics g;
    static Color ink=Color.FromArgb(20,38,65), blue=Color.FromArgb(26,96,175);
    static void Begin(int h) {
        canvas=new Bitmap(1600,h); g=Graphics.FromImage(canvas);
        g.Clear(Color.White); g.SmoothingMode=SmoothingMode.AntiAlias;
        g.TextRenderingHint=System.Drawing.Text.TextRenderingHint.AntiAliasGridFit;
    }
    static void Text(string value,int x,int y,int w,int h,int size,bool bold,Color color) {
        using(var font=new Font("Segoe UI",size, bold?FontStyle.Bold:FontStyle.Regular,GraphicsUnit.Pixel))
        using(var brush=new SolidBrush(color))
        using(var format=new StringFormat { Alignment=StringAlignment.Center,LineAlignment=StringAlignment.Center })
            g.DrawString(value,font,brush,new RectangleF(x,y,w,h),format);
    }
    static void Title(string s) { Text(s,50,24,1500,85,54,true,ink); }
    static void Box(string title,string detail,int x,int y,int w=400,int h=180) {
        using(var b=new SolidBrush(Color.FromArgb(239,246,255))) g.FillRectangle(b,x,y,w,h);
        using(var p=new Pen(Color.FromArgb(138,173,213),3)) g.DrawRectangle(p,x,y,w,h);
        Text(title,x+12,y+15,w-24,65,38,true,ink);
        Text(detail,x+15,y+80,w-30,h-90,33,false,ink);
    }
    static void Arrow(int x1,int x2,int y,string label,bool both=false) {
        using(var p=new Pen(blue,5)) {
            p.CustomEndCap=new AdjustableArrowCap(5,6);
            if(both) p.CustomStartCap=new AdjustableArrowCap(5,6);
            g.DrawLine(p,x1,y,x2,y);
        }
        Text(label,Math.Min(x1,x2)-10,y+15,Math.Abs(x2-x1)+20,80,29,true,blue);
    }
    static void Note(string text,int y,int height=85) { Text(text,65,y,1470,height,32,false,ink); }
    static void End(string path) { canvas.Save(path,ImageFormat.Png); g.Dispose(); canvas.Dispose(); }
    static void Photo(string path,int x,int y,int w,int h) {
        using(var image=Image.FromFile(path)) {
            float scale=Math.Min((float)w/image.Width,(float)h/image.Height);
            float width=image.Width*scale,height=image.Height*scale;
            g.InterpolationMode=InterpolationMode.HighQualityBicubic;
            g.DrawImage(image,x+(w-width)/2,y+(h-height)/2,width,height);
        }
    }
    public static void Render(string dir) {
        Begin(650); Title("Windows PC + USB + Pico 2 W + Bluetooth + AirPods");
        using(var p=new Pen(ink,8)) {
            g.DrawRectangle(p,112,205,296,185); g.DrawLine(p,260,390,260,435); g.DrawLine(p,195,435,325,435);
        }
        using(var b=new SolidBrush(Color.FromArgb(26,96,175))) {
            g.FillRectangle(b,210,250,42,42); g.FillRectangle(b,262,250,42,42);
            g.FillRectangle(b,210,302,42,42); g.FillRectangle(b,262,302,42,42);
        }
        Photo(System.IO.Path.Combine(dir,"pico-2-w-photo.jpg"),600,175,400,290);
        Photo(System.IO.Path.Combine(dir,"airpods-4-photo.jpg"),1140,175,400,290);
        Arrow(470,590,305,"USB",true); Arrow(1010,1130,305,"BT",true);
        using(var p=new Pen(blue,5)) {
            g.DrawLines(p,new Point[]{new Point(1067,205),new Point(1067,270),new Point(1090,249),new Point(1048,222)});
            g.DrawLines(p,new Point[]{new Point(1067,205),new Point(1090,224),new Point(1048,252)});
        }
        Text("Windows PC",60,475,400,65,39,true,ink);
        Text("Pico 2 W",600,475,400,65,39,true,ink);
        Text("AirPods",1140,475,400,65,39,true,ink);
        Note("USB cable: power + audio + status. Bluetooth (BT): Pico to AirPods.",565,65);
        End(System.IO.Path.Combine(dir,"hardware-connection.png"));
        Begin(570); Title("Computer - Pico - AirPods");
        Box("Windows computer","Audio apps + tray companion",60,180);
        Box("Pico 2 W","USB audio + Bluetooth bridge",600,180);
        Box("AirPods","Playback + microphone",1140,180);
        Arrow(470,590,255,"USB",true); Arrow(1010,1130,255,"BT",true);
        Note("USB carries power, audio and serial data. BT = Bluetooth; AirPods pair with the Pico.",425);
        End(System.IO.Path.Combine(dir,"physical-connection.png"));

        Begin(1160); Title("Audio and companion data paths");
        Text("PLAYBACK",60,125,1480,55,36,true,blue);
        Box("Windows app","Stereo playback",60,195);
        Box("Pico 2 W","AAC-ELD encoder",600,195);
        Box("AirPods","Stereo sound",1140,195);
        Arrow(470,590,265,"USB"); Arrow(1010,1130,265,"A2DP");
        Text("MICROPHONE",60,425,1480,55,36,true,blue);
        Box("Windows app","Microphone input",60,495);
        Box("Pico 2 W","Microphone decoder",600,495);
        Box("AirPods","Microphone stream",1140,495);
        Arrow(590,470,565,"USB"); Arrow(1130,1010,565,"AACP");
        Text("BATTERY STATUS + ANC CONTROL",60,725,1480,55,36,true,blue);
        Box("Windows tray app","Status display + controls",60,795);
        Box("Pico 2 W","Status cache + commands",600,795);
        Box("AirPods","Notifications + ANC",1140,795);
        Arrow(470,590,865,"USB\nserial",true); Arrow(1010,1130,865,"AACP",true);
        Note("Audio and microphone work together. The tray app uses a separate USB serial interface.",1040);
        End(System.IO.Path.Combine(dir,"audio-control-paths.png"));

        Begin(1580); Title("Speaker + microphone: three audio paths");
        Text("1  STANDARD BLUETOOTH - MUSIC PLAYBACK",45,130,1510,70,40,true,ink);
        Box("Windows apps","A2DP playback",60,230);
        Box("BT adapter","Windows Bluetooth",600,230);
        Box("AirPods","Stereo music",1140,230);
        Arrow(470,590,300,"USB /\nradio"); Arrow(1010,1130,300,"A2DP");
        Note("SPEAKER: stereo music audio    |    HEADSET MIC: inactive in A2DP",435,75);
        using(var p=new Pen(Color.FromArgb(213,224,237),3)) g.DrawLine(p,60,545,1540,545);
        Text("2  STANDARD BLUETOOTH - HEADSET MIC ACTIVE",45,575,1510,70,40,true,ink);
        Box("Windows apps","HFP voice audio",60,675);
        Box("BT adapter","Hands-free profile",600,675);
        Box("AirPods","Mono playback + mic",1140,675);
        Arrow(470,590,745,"USB /\nradio",true); Arrow(1010,1130,745,"HFP",true);
        Note("SPEAKER: mono voice-profile audio    |    MIC: HFP voice capture",880,75);
        using(var p=new Pen(Color.FromArgb(213,224,237),3)) g.DrawLine(p,60,990,1540,990);
        Text("3  USB AIRPODS - STEREO + MICROPHONE TOGETHER",45,1020,1510,70,40,true,blue);
        Box("Windows apps","USB audio device",60,1120);
        Box("Pico 2 W","A2DP + AACP",600,1120);
        Box("AirPods","Stereo playback + mic",1140,1120);
        Arrow(470,590,1190,"USB",true); Arrow(1010,1130,1190,"BT",true);
        Note("SPEAKER: 48 kHz stereo / AAC-ELD    |    MIC: AACP / AAC-ELD",1325,75);
        Note("Playback stays stereo while the microphone is active. Decoder reports 48 kHz.",1400,65);
        Text("Classic A2DP/HFP comparison. Compatible LE Audio systems can also retain stereo with a mic.",65,1490,1470,65,29,false,ink);
        End(System.IO.Path.Combine(dir,"bluetooth-comparison.png"));
    }
}
'@ -ReferencedAssemblies System.Drawing
[ReadableDiagrams]::Render((Join-Path $PSScriptRoot '../docs/images'))
