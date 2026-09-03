using System.Windows;
using System.Windows.Media;
namespace WeldApp;
public sealed class TrendChart:FrameworkElement {
 const int Cap=240; readonly List<(double c,double v,double s)> data=[];
 static readonly Pen Grid=P(Color.FromRgb(39,55,78),1), Current=P(Color.FromRgb(56,189,248),2), Voltage=P(Color.FromRgb(245,158,11),2), Speed=P(Color.FromRgb(167,139,250),2);
 public TrendChart(){MinHeight=280;ClipToBounds=true;} public void AddSample(double c,double v,double s){data.Add((c,v,s));if(data.Count>Cap)data.RemoveAt(0);InvalidateVisual();}public void Clear(){data.Clear();InvalidateVisual();}
 protected override void OnRender(DrawingContext d){double w=ActualWidth,h=ActualHeight;d.DrawRectangle(new SolidColorBrush(Color.FromRgb(9,18,32)),null,new Rect(0,0,w,h));for(int i=0;i<=10;i++)d.DrawLine(Grid,new(i*w/10,0),new(i*w/10,h));for(int i=0;i<=5;i++)d.DrawLine(Grid,new(0,i*h/5),new(w,i*h/5));if(data.Count<2)return;Draw(d,Current,x=>x.c,w,h);Draw(d,Voltage,x=>x.v,w,h);Draw(d,Speed,x=>x.s,w,h);}
 void Draw(DrawingContext d,Pen p,Func<(double c,double v,double s),double> f,double w,double h){double m=Math.Max(1,data.Max(f)*1.12);var q=new PathFigure{StartPoint=new(0,h-f(data[0])/m*h)};for(int i=1;i<data.Count;i++)q.Segments.Add(new LineSegment(new(i*w/(Cap-1),h-f(data[i])/m*h),true));d.DrawGeometry(null,p,new PathGeometry([q]));}
 static Pen P(Color c,double t){var p=new Pen(new SolidColorBrush(c),t);p.Freeze();return p;}
}
