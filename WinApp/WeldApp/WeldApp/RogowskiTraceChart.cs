using System.Globalization;
using System.Windows;
using System.Windows.Input;
using System.Windows.Media;

namespace WeldApp;

public sealed class RogowskiTraceChart : FrameworkElement
{
    IReadOnlyList<double> timesMs = Array.Empty<double>();
    IReadOnlyList<double> values = Array.Empty<double>();
    string unit = "V";
    bool averageBuckets;
    double viewStart;
    double viewEnd = 1;
    Point? dragStart;
    double dragViewStart;
    double dragViewEnd;
    const double PlotLeft = 78;
    const double PlotRight = 18;
    static readonly Pen GridPen = MakePen(Color.FromRgb(48, 63, 81), 1);
    static readonly Pen TracePen = MakePen(Color.FromRgb(56, 189, 248), 1.5);
    static readonly Pen ZeroPen = MakePen(Color.FromRgb(245, 158, 11), 1);
    static readonly Typeface Typeface = new("Segoe UI");

    public RogowskiTraceChart()
    {
        MinHeight = 400;
        Focusable = true;
        Cursor = Cursors.Cross;
        MouseWheel += Chart_MouseWheel;
        MouseLeftButtonDown += Chart_MouseLeftButtonDown;
        MouseLeftButtonUp += Chart_MouseLeftButtonUp;
        MouseMove += Chart_MouseMove;
    }

    public void SetData(IReadOnlyList<double> timeValuesMs, IReadOnlyList<double> samples, string valueUnit, bool useAverageBuckets = false)
    {
        timesMs = timeValuesMs;
        values = samples;
        unit = valueUnit;
        averageBuckets = useAverageBuckets;
        ResetZoom();
        InvalidateVisual();
    }

    public void Clear()
    {
        timesMs = Array.Empty<double>();
        values = Array.Empty<double>();
        viewStart = 0;
        viewEnd = 1;
        InvalidateVisual();
    }

    public void ZoomIn() => Zoom(0.5, 0.5);
    public void ZoomOut() => Zoom(2.0, 0.5);
    public void ResetZoom()
    {
        viewStart = 0;
        viewEnd = 1;
        InvalidateVisual();
    }

    public void SetTimeWindow(double startMs, double durationMs)
    {
        if (timesMs.Count < 2 || durationMs <= 0)
            return;
        double fullStart = timesMs[0], fullDuration = timesMs[^1] - fullStart;
        double span = Math.Clamp(durationMs / fullDuration, 32.0 / timesMs.Count, 1);
        viewStart = Math.Clamp((startMs - fullStart) / fullDuration, 0, 1 - span);
        viewEnd = viewStart + span;
        InvalidateVisual();
    }

    void Chart_MouseWheel(object sender, MouseWheelEventArgs e)
    {
        double plotWidth = Math.Max(1, ActualWidth - PlotLeft - PlotRight);
        double anchor = Math.Clamp((e.GetPosition(this).X - PlotLeft) / plotWidth, 0, 1);
        Zoom(e.Delta > 0 ? 0.65 : 1.0 / 0.65, anchor);
        e.Handled = true;
    }

    void Zoom(double factor, double anchor)
    {
        if (values.Count < 2)
            return;
        double oldSpan = viewEnd - viewStart;
        double minimumSpan = Math.Min(1, 32.0 / values.Count);
        double newSpan = Math.Clamp(oldSpan * factor, minimumSpan, 1);
        double fixedPoint = viewStart + oldSpan * anchor;
        double start = fixedPoint - newSpan * anchor;
        viewStart = Math.Clamp(start, 0, 1 - newSpan);
        viewEnd = viewStart + newSpan;
        InvalidateVisual();
    }

    void Chart_MouseLeftButtonDown(object sender, MouseButtonEventArgs e)
    {
        if (e.ClickCount == 2)
        {
            ResetZoom();
            e.Handled = true;
            return;
        }
        dragStart = e.GetPosition(this);
        dragViewStart = viewStart;
        dragViewEnd = viewEnd;
        CaptureMouse();
        Cursor = Cursors.SizeWE;
        e.Handled = true;
    }

    void Chart_MouseMove(object sender, MouseEventArgs e)
    {
        if (dragStart is not Point origin || e.LeftButton != MouseButtonState.Pressed)
            return;
        double plotWidth = Math.Max(1, ActualWidth - PlotLeft - PlotRight);
        double span = dragViewEnd - dragViewStart;
        double shift = -(e.GetPosition(this).X - origin.X) / plotWidth * span;
        viewStart = Math.Clamp(dragViewStart + shift, 0, 1 - span);
        viewEnd = viewStart + span;
        InvalidateVisual();
    }

    void Chart_MouseLeftButtonUp(object sender, MouseButtonEventArgs e)
    {
        dragStart = null;
        ReleaseMouseCapture();
        Cursor = Cursors.Cross;
        e.Handled = true;
    }

    protected override void OnRender(DrawingContext dc)
    {
        const double left = PlotLeft, right = PlotRight, top = 18, bottom = 52;
        double plotWidth = Math.Max(1, ActualWidth - left - right);
        double plotHeight = Math.Max(1, ActualHeight - top - bottom);
        var plot = new Rect(left, top, plotWidth, plotHeight);
        dc.DrawRectangle(Brushes.Black, null, plot);

        if (values.Count == 0 || timesMs.Count != values.Count)
        {
            DrawText(dc, "ADC 재샘플 CSV를 불러오세요.", new Point(left + 15, top + 15), Brushes.LightSteelBlue, 13);
            return;
        }

        int firstIndex = Math.Clamp((int)Math.Floor(viewStart * (values.Count - 1)), 0, values.Count - 2);
        int lastIndex = Math.Clamp((int)Math.Ceiling(viewEnd * (values.Count - 1)), firstIndex + 1, values.Count - 1);
        double start = timesMs[firstIndex], duration = Math.Max(1e-9, timesMs[lastIndex] - start);
        double minimum = values[firstIndex], maximum = values[firstIndex];
        for (int i = firstIndex + 1; i <= lastIndex; i++)
        {
            minimum = Math.Min(minimum, values[i]);
            maximum = Math.Max(maximum, values[i]);
        }
        if (minimum == maximum) { minimum -= 1; maximum += 1; }
        double padding = (maximum - minimum) * 0.08;
        minimum -= padding; maximum += padding;
        double range = maximum - minimum;

        for (int i = 0; i <= 6; i++)
        {
            double x = left + plotWidth * i / 6;
            dc.DrawLine(GridPen, new Point(x, top), new Point(x, top + plotHeight));
            DrawText(dc, (start + duration * i / 6).ToString("0.###", CultureInfo.InvariantCulture),
                new Point(x - 22, top + plotHeight + 7), Brushes.LightGray, 11);
        }
        for (int i = 0; i <= 5; i++)
        {
            double y = top + plotHeight * i / 5;
            dc.DrawLine(GridPen, new Point(left, y), new Point(left + plotWidth, y));
            DrawText(dc, (maximum - range * i / 5).ToString("0.###", CultureInfo.InvariantCulture),
                new Point(4, y - 8), Brushes.LightGray, 11);
        }
        if (minimum <= 0 && maximum >= 0)
        {
            double zeroY = top + maximum / range * plotHeight;
            dc.DrawLine(ZeroPen, new Point(left, zeroY), new Point(left + plotWidth, zeroY));
        }

        // Preserve narrow peaks while reducing large captures to screen pixels.
        // Emit extrema in their original time order so the result remains a
        // continuous waveform instead of disconnected vertical envelope bars.
        int columns = Math.Max(1, (int)plotWidth);
        int visibleCount = lastIndex - firstIndex + 1;
        var trace = new PathFigure { StartPoint = new Point(X(firstIndex), Y(values[firstIndex])) };
        dc.PushClip(new RectangleGeometry(plot));
        if (averageBuckets && visibleCount > columns * 2)
        {
            for (int column = 0; column < columns; column++)
            {
                int bucketStart = firstIndex + (int)((long)column * visibleCount / columns);
                int bucketEnd = Math.Min(lastIndex + 1,
                    firstIndex + (int)((long)(column + 1) * visibleCount / columns));
                if (bucketEnd <= bucketStart)
                    continue;
                double sum = 0;
                for (int i = bucketStart; i < bucketEnd; i++) sum += values[i];
                int centreIndex = (bucketStart + bucketEnd - 1) / 2;
                trace.Segments.Add(new LineSegment(new Point(X(centreIndex), Y(sum / (bucketEnd - bucketStart))), true));
            }
        }
        else if (visibleCount <= columns * 2)
        {
            for (int i = firstIndex + 1; i <= lastIndex; i++)
                trace.Segments.Add(new LineSegment(new Point(X(i), Y(values[i])), true));
        }
        else
        {
            for (int column = 0; column < columns; column++)
            {
                int bucketStart = firstIndex + (int)((long)column * visibleCount / columns);
                int bucketEnd = Math.Min(lastIndex + 1,
                    firstIndex + (int)((long)(column + 1) * visibleCount / columns));
                if (bucketEnd <= bucketStart)
                    continue;
                int lowIndex = bucketStart, highIndex = bucketStart;
                for (int i = bucketStart + 1; i < bucketEnd; i++)
                {
                    if (values[i] < values[lowIndex]) lowIndex = i;
                    if (values[i] > values[highIndex]) highIndex = i;
                }
                int firstExtreme = Math.Min(lowIndex, highIndex);
                int secondExtreme = Math.Max(lowIndex, highIndex);
                if (firstExtreme > bucketStart)
                    trace.Segments.Add(new LineSegment(new Point(X(firstExtreme), Y(values[firstExtreme])), true));
                if (secondExtreme != firstExtreme)
                    trace.Segments.Add(new LineSegment(new Point(X(secondExtreme), Y(values[secondExtreme])), true));
                int bucketLast = bucketEnd - 1;
                if (bucketLast > secondExtreme)
                    trace.Segments.Add(new LineSegment(new Point(X(bucketLast), Y(values[bucketLast])), true));
            }
        }
        dc.DrawGeometry(null, TracePen, new PathGeometry([trace]));
        dc.Pop();

        double zoom = 1.0 / (viewEnd - viewStart);
        DrawText(dc, $"시간 (ms) · 측정값 ({unit}) · {zoom:0.##}×", new Point(left + plotWidth / 2 - 95, top + plotHeight + 28), Brushes.LightGray, 12);
        double X(int index) => left + (timesMs[index] - start) / duration * plotWidth;
        double Y(double value) => top + (maximum - value) / range * plotHeight;
    }

    void DrawText(DrawingContext dc, string text, Point point, Brush brush, double size) =>
        dc.DrawText(new FormattedText(text, CultureInfo.CurrentCulture, FlowDirection.LeftToRight,
            Typeface, size, brush, VisualTreeHelper.GetDpi(this).PixelsPerDip), point);

    static Pen MakePen(Color color, double width)
    {
        var pen = new Pen(new SolidColorBrush(color), width);
        pen.Freeze();
        return pen;
    }
}

public sealed record RogowskiCapture(string FileName, double[] TimesMs, string[] ChannelNames, double[][] Channels, string Source);
