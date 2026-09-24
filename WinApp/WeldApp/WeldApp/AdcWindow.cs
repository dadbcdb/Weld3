using System.Globalization;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;

namespace WeldApp;

public sealed class AdcWindow : Window
{
    readonly TextBlock reading = new() { Margin = new Thickness(8), TextWrapping = TextWrapping.Wrap };
    readonly AdcTraceChart primary = new();
    readonly AdcTraceChart secondary = new();

    public AdcWindow()
    {
        Title = "ADC 시험 파형";
        Width = 950;
        Height = 780;
        MinWidth = 600;
        MinHeight = 500;
        Background = new SolidColorBrush(Color.FromRgb(11, 18, 32));
        Foreground = Brushes.White;
        var grid = new Grid { Margin = new Thickness(12) };
        grid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        grid.RowDefinitions.Add(new RowDefinition());
        grid.RowDefinitions.Add(new RowDefinition());
        grid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        grid.Children.Add(new TextBlock
        {
            Text = "시험 종료 후 기록 · 하늘색: 원본 평균 / 주황색: 필터\nX: 시간(ms) · Y: ADC 코드(자동 스케일) · 1차 PA0_C / 2차 PA1_C\n1차 PID 필터는 PID 실행 중 갱신됩니다. 기록 간격 사이의 짧은 변화는 빠질 수 있습니다.",
            TextWrapping = TextWrapping.Wrap
        });
        AddChart(grid, primary, "1차 ADC", 1);
        AddChart(grid, secondary, "2차 ADC / 로고스키 (전류 보정 전)", 2);
        Grid.SetRow(reading, 3);
        grid.Children.Add(reading);
        Content = grid;
    }

    static void AddChart(Grid grid, AdcTraceChart chart, string title, int row)
    {
        var box = new GroupBox { Header = title, Content = chart, Foreground = Brushes.White, Margin = new Thickness(0, 8, 0, 0) };
        Grid.SetRow(box, row);
        grid.Children.Add(box);
    }

    public void SetReading(string text) => reading.Text = text.Replace("\n", " / ");

    public void SetTrace(IEnumerable<string> rows)
    {
        var first = new List<(double Time, double Raw, double? Filter)>();
        var second = new List<(double Time, double Raw, double? Filter)>();
        foreach (string row in rows)
        {
            var f = row.Split(',');
            if (f.Length != 6 && f.Length != 9)
                continue;
            if (!f.All(x => uint.TryParse(x, out _)))
                continue;
            double time = uint.Parse(f[0]);
            first.Add((time, uint.Parse(f[3]), f.Length == 9 ? uint.Parse(f[6]) * 65535.0 / 1000000 : null));
            if (f.Length == 9)
                second.Add((time, uint.Parse(f[7]), uint.Parse(f[8]) * 65535.0 / 1000000));
        }
        primary.SetData(first);
        secondary.SetData(second);
    }
}

public sealed class AdcTraceChart : FrameworkElement
{
    List<(double Time, double Raw, double? Filter)> samples = [];

    public void SetData(List<(double Time, double Raw, double? Filter)> values)
    {
        samples = values;
        InvalidateVisual();
    }

    protected override void OnRender(DrawingContext dc)
    {
        base.OnRender(dc);
        if (ActualWidth < 100 || ActualHeight < 70)
            return;
        var area = new Rect(58, 12, ActualWidth - 76, ActualHeight - 42);
        dc.DrawRectangle(Brushes.Black, null, area);
        double duration = samples.Count > 0 ? Math.Max(1, samples.Max(x => x.Time)) : 1;
        // Share one vertical scale between raw and filtered data in each plot.
        double peak = samples.Count > 0
            ? samples.Max(x => Math.Max(x.Raw, x.Filter ?? x.Raw)) : 0;
        double padded = Math.Max(100, peak * 1.1);
        double step = Math.Pow(10, Math.Floor(Math.Log10(padded))) / 2;
        double maximum = Math.Min(65535, Math.Ceiling(padded / step) * step);
        void Label(string text, double x, double y) => dc.DrawText(new FormattedText(text,
            CultureInfo.InvariantCulture, FlowDirection.LeftToRight, new Typeface("Segoe UI"),
            11, Brushes.LightSteelBlue, VisualTreeHelper.GetDpi(this).PixelsPerDip), new Point(x, y));
        for (int i = 0; i <= 4; i++)
        {
            double y = area.Bottom - area.Height * i / 4;
            dc.DrawLine(new Pen(Brushes.DarkSlateGray, 1), new Point(area.Left, y), new Point(area.Right, y));
            Label((maximum * i / 4).ToString("0"), 0, y - 7);
            double x = area.Left + area.Width * i / 4;
            Label((duration * i / 4).ToString("0.#"), x - 8, area.Bottom + 5);
        }
        if (samples.Count == 0)
        {
            Label("기록 없음 · 새 시험과 최신 펌웨어가 필요합니다", area.Left + 12, area.Top + 12);
            return;
        }
        void Draw(bool filtered, Brush brush)
        {
            Point? previous = null;
            var pen = new Pen(brush, 1.5);
            foreach (var sample in samples)
            {
                double? value = filtered ? sample.Filter : sample.Raw;
                if (value is null)
                {
                    previous = null;
                    continue;
                }
                var point = new Point(area.Left + sample.Time / duration * area.Width,
                    area.Bottom - Math.Clamp(value.Value / maximum, 0, 1) * area.Height);
                if (previous is Point start)
                    dc.DrawLine(pen, start, point);
                previous = point;
            }
        }
        Draw(false, Brushes.DeepSkyBlue);
        Draw(true, Brushes.Orange);
    }
}
