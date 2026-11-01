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
    static void RateBox(string label,string rate,string detail,int x,int y) {
        using(var b=new SolidBrush(Color.FromArgb(239,246,255))) g.FillRectangle(b,x,y,700,240);
        using(var p=new Pen(Color.FromArgb(138,173,213),3)) g.DrawRectangle(p,x,y,700,240);
        Text(label,x+20,y+12,660,60,37,true,ink);
        Text(rate,x+15,y+80,670,80,60,true,blue);
        Text(detail,x+20,y+165,660,60,33,false,ink);
    }
    public static void Render(string dir) {
        Begin(550);
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
        End(System.IO.Path.Combine(dir,"hardware-connection.png"));
        Begin(590); Title("Microphone ON: what changes?");
        using(var brush=new SolidBrush(Color.FromArgb(244,246,249))) g.FillRectangle(brush,60,160,700,370);
        using(var brush=new SolidBrush(Color.FromArgb(232,243,255))) g.FillRectangle(brush,840,160,700,370);
        Text("BLUETOOTH CALL MODE",85,185,650,75,42,true,ink);
        Text("USB AIRPODS",865,185,650,75,42,true,blue);
        Text("Speaker",90,295,640,60,34,false,ink);
        Text("8 / 16 kHz mono",90,350,640,80,52,true,ink);
        Text("Mic: 8 / 16 kHz mono",90,450,640,55,34,false,ink);
        Text("Speaker",870,295,640,60,34,false,ink);
        Text("48 kHz stereo",870,350,640,80,52,true,blue);
        Text("Mic: separate AACP stream",870,450,640,55,34,false,ink);
        End(System.IO.Path.Combine(dir,"bluetooth-comparison.png"));
    }
}
'@ -ReferencedAssemblies System.Drawing
[ReadableDiagrams]::Render((Join-Path $PSScriptRoot '../docs/images'))
