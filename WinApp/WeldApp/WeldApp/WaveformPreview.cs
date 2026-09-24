using System.Globalization;
using System.Windows;
using System.Windows.Media;

namespace WeldApp;

public sealed class WaveformPreview : FrameworkElement
{
    readonly double[] currents = new double[3];
    uint squeeze;
    readonly uint[] up = new uint[3], hold = new uint[3], down = new uint[3], cool = new uint[2];
    readonly List<(uint Time, double Current)> measured = [];
    static readonly Pen GridPen = MakePen(Color.FromRgb(48, 63, 81), 1);
    static readonly Pen SetPen = MakePen(Color.FromRgb(34, 197, 94), 2);
    static readonly Pen MeasuredPen = MakePen(Color.FromRgb(56, 189, 248), 2);
    static readonly Typeface Typeface = new("Segoe UI");

    public WaveformPreview()
    {
        MinHeight = 400;
    }

    public void SetStages(uint squeezeMs, double c1, uint u1, uint h1, uint d1, uint z1, double c2, uint u2, uint h2, uint d2, uint z2, double c3, uint u3, uint h3, uint d3)
    {
        double[] c = [c1, c2, c3];
        uint[] u = [u1, u2, u3], h = [h1, h2, h3], d = [d1, d2, d3], z = [z1, z2];
        squeeze = squeezeMs;
        Array.Copy(c, currents, 3);
        Array.Copy(u, up, 3);
        Array.Copy(h, hold, 3);
        Array.Copy(d, down, 3);
        Array.Copy(z, cool, 2);
        InvalidateVisual();
    }
    public void ClearMeasured()
    {
        measured.Clear();
        InvalidateVisual();
    }
    public void AddMeasured(uint time, double current)
    {
        measured.Add((time, current));
        InvalidateVisual();
    }

    protected override void OnRender(DrawingContext dc)
    {
        double width = ActualWidth, height = ActualHeight, left = 68, right = 18, top = 18, bottom = 52;
        double plotWidth = Math.Max(1, width - left - right), plotHeight = Math.Max(1, height - top - bottom);
        dc.DrawRectangle(Brushes.Black, null, new Rect(left, top, plotWidth, plotHeight));

        double setTime = squeeze + up.Zip(hold, (a, b) => (double)a + b).Zip(down, (a, b) => a + b).Sum() + cool.Sum(x => (double)x);
        double maxTime = Math.Max(1, setTime * 1.1);
        double configuredPeak = Math.Max(1, currents.Max());
        double maxCurrent = configuredPeak * 1.1;
        double minCurrent = 0;
        double currentRange = maxCurrent - minCurrent;
        double zeroY = top + (maxCurrent / currentRange) * plotHeight;

        for (int i = 0; i <= 6; i++)
        {
            double x = left + i * plotWidth / 6;
            dc.DrawLine(GridPen, new Point(x, top), new Point(x, top + plotHeight));
            DrawText(dc, (maxTime * i / 6).ToString("0", CultureInfo.InvariantCulture), new Point(x - 12, top + plotHeight + 7), Brushes.LightGray, 11);
        }
        for (int i = 0; i <= 5; i++)
        {
            double y = top + i * plotHeight / 5;
            dc.DrawLine(GridPen, new Point(left, y), new Point(left + plotWidth, y));
            DrawText(dc, (maxCurrent - currentRange * i / 5).ToString("0", CultureInfo.InvariantCulture), new Point(7, y - 8), Brushes.LightGray, 11);
        }

        var configured = new PathFigure { StartPoint = new Point(left, zeroY) };
        double time = squeeze;
        configured.Segments.Add(new LineSegment(new Point(X(time), zeroY), true));
        for (int i = 0; i < 3; i++)
        {
            double rampEnd = time + up[i], holdEnd = rampEnd + hold[i], downEnd = holdEnd + down[i];
            double y = Y(currents[i]);
            configured.Segments.Add(new LineSegment(new Point(X(rampEnd), y), true));
            configured.Segments.Add(new LineSegment(new Point(X(holdEnd), y), true));
            configured.Segments.Add(new LineSegment(new Point(X(downEnd), zeroY), true));
            time = downEnd;
            if (i < 2)
            {
                time += cool[i];
                configured.Segments.Add(new LineSegment(new Point(X(time), zeroY), true));
            }
        }
        dc.DrawGeometry(null, SetPen, new PathGeometry([configured]));

        if (measured.Count > 1)
        {
            var actual = new PathFigure { StartPoint = new Point(X(measured[0].Time), Y(measured[0].Current)) };
            foreach (var point in measured.Skip(1))
                actual.Segments.Add(new LineSegment(new Point(X(point.Time), Y(point.Current)), true));
            dc.DrawGeometry(null, MeasuredPen, new PathGeometry([actual]));
        }

        DrawText(dc, $"시간 (ms)  최대 {maxTime:0}", new Point(left + plotWidth / 2 - 70, top + plotHeight + 27), Brushes.LightGray, 12);
        double X(double value) => left + Math.Min(value, maxTime) / maxTime * plotWidth;
        double Y(double value) => top + (maxCurrent - Math.Clamp(value, minCurrent, maxCurrent)) / currentRange * plotHeight;
    }

    static double NiceMaximum(double value, int divisions)
    {
        double rawStep = value / divisions;
        double magnitude = Math.Pow(10, Math.Floor(Math.Log10(rawStep)));
        double normalized = rawStep / magnitude;
        double nice = normalized <= 1 ? 1 : normalized <= 2 ? 2 : normalized <= 5 ? 5 : 10;
        return nice * magnitude * divisions;
    }
    static void DrawText(DrawingContext dc, string text, Point point, Brush brush, double size) => dc.DrawText(new FormattedText(text, CultureInfo.CurrentCulture, FlowDirection.LeftToRight, Typeface, size, brush, 1), point);
    static Pen MakePen(Color color, double width)
    {
        var pen = new Pen(new SolidColorBrush(color), width);
        pen.Freeze();
        return pen;
    }
}
