using System.Globalization;
using System.IO;
using System.Net.Sockets;
using System.Text;
using System.Text.Json;
using System.Windows;
using System.Windows.Media;
using System.Windows.Threading;
using System.Collections.ObjectModel;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media.Imaging;
using Microsoft.Win32;

namespace WeldApp;

public partial class MainWindow : Window
{
    public ObservableCollection<StageRow> StageRows { get; } = [new("1단봉", 100, 50, 100, 50, 100), new("2단봉", 0, 0, 0, 0, 0), new("3단봉", 0, 0, 0, 0, 0)];
    public IReadOnlyList<string> ProfileNumbers { get; } = Enumerable.Range(0, 16).Select(x => x.ToString("00")).ToArray();
    readonly Dictionary<string, Settings> profiles = [];
    string? activeUiProfileKey;
    bool changingUiProfile;
    readonly DispatcherTimer timer = new() { Interval = TimeSpan.FromMilliseconds(250) };
    readonly SemaphoreSlim commandLock = new(1, 1);
    readonly Random random = new();
    TcpClient? client;
    NetworkStream? networkStream;
    StreamWriter? writer;
    bool simulation, polling, wasWelding, simCycleActive;
    double phase;
    DateTime simCycleStart;
    bool testBusy, testStarting, cancelTestStart;
    uint loadedTestId;
    Settings testSettings;
    double testDuty;
    readonly List<string> testTrace = [];

    sealed record WindowLayout(double Left, double Top, double Width, double Height);
    static string WindowLayoutPath => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "WeldApp", "window.json");
    public MainWindow()
    {
        InitializeComponent();
        LoadWindowLayout();
        changingUiProfile = true;
        DataContext = this;
        ProfileFileBox.SelectedIndex = 0;
        ProfileNumberBox.SelectedIndex = 0;
        changingUiProfile = false;
        timer.Tick += Poll;
        activeUiProfileKey = SelectedProfileKey();
        profiles[activeUiProfileKey] = GetUiSettings();
        UpdateWaveform();
    }
    void LoadWindowLayout()
    {
        try
        {
            if (!File.Exists(WindowLayoutPath))
                return;
            var x = JsonSerializer.Deserialize<WindowLayout>(File.ReadAllText(WindowLayoutPath));
            if (x is null || x.Width is < 1080 or > 4000 || x.Height is < 700 or > 3000)
                return;
            double screenLeft = SystemParameters.VirtualScreenLeft, screenTop = SystemParameters.VirtualScreenTop;
            Width = x.Width;
            Height = x.Height;
            if (x.Left > screenLeft - Width + 80 && x.Left < screenLeft + SystemParameters.VirtualScreenWidth - 80 && x.Top > screenTop - Height + 80 && x.Top < screenTop + SystemParameters.VirtualScreenHeight - 80)
            {
                Left = x.Left;
                Top = x.Top;
                WindowStartupLocation = WindowStartupLocation.Manual;
            }
        }
        catch { }
    }
    void SaveWindowLayout()
    {
        try
        {
            string? d = Path.GetDirectoryName(WindowLayoutPath);
            if (d is null)
                return;
            Directory.CreateDirectory(d);
            File.WriteAllText(WindowLayoutPath, JsonSerializer.Serialize(new WindowLayout(RestoreBounds.Left, RestoreBounds.Top, RestoreBounds.Width, RestoreBounds.Height)));
        }
        catch { }
    }

    static int SelectedProfileNumber(ComboBox box)
    {
        if (box.SelectedItem is string text && int.TryParse(text, out int value) && value is >= 0 and <= 15)
            return value;
        return box.SelectedIndex is >= 0 and <= 15 ? box.SelectedIndex : 0;
    }
    string SelectedProfileKey() => $"{SelectedProfileNumber(ProfileFileBox):00}-{SelectedProfileNumber(ProfileNumberBox):00}";
    void ProfileSelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (changingUiProfile || ProfileFileBox is null || ProfileNumberBox is null || ProfileFileBox.SelectedIndex < 0 || ProfileNumberBox.SelectedIndex < 0 || SettingsGrid is null)
            return;
        string nextKey = SelectedProfileKey();
        if (nextKey == activeUiProfileKey)
            return;
        changingUiProfile = true;
        try
        {
            if (activeUiProfileKey is not null)
                profiles[activeUiProfileKey] = GetUiSettings();
            activeUiProfileKey = nextKey;
            SetUiSettings(profiles.TryGetValue(nextKey, out Settings selected) ? selected : default);
            StatusText.Text = $"WinApp 설정 화면을 {nextKey}로 변경했습니다. 장비 번호 변경은 SET을 누르세요.";
        }
        finally { changingUiProfile = false; }
    }
    async void SetProfileButton_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            if (!Connected())
                return;
            Settings selectedSettings = ValidateSettings(GetUiSettings());
            profiles[SelectedProfileKey()] = selectedSettings;
            if (!simulation)
            {
                string reply = await Command($"SET PROFILE file={SelectedProfileNumber(ProfileFileBox)} number={SelectedProfileNumber(ProfileNumberBox)}");
                if (reply != "OK")
                    throw new IOException("장비 응답: " + reply);
                await SendSettings(selectedSettings);
                await LoadSettings();
            }
            StatusText.Text = $"화면의 {SelectedProfileKey()} 설정값을 펌웨어에 적용했습니다. 영구 저장은 장비 저장을 누르세요.";
        }
        catch (Exception ex) { Error("SET 실패", ex.Message); }
    }
    void SaveFileButton_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            profiles[SelectedProfileKey()] = ValidateSettings(GetUiSettings());
            var dialog = new SaveFileDialog { Filter = "Weld 전체 설정 (*.weld.json)|*.weld.json|JSON (*.json)|*.json", FileName = "Weld_All_Profiles.weld.json" };
            if (dialog.ShowDialog(this) != true)
                return;
            File.WriteAllText(dialog.FileName, JsonSerializer.Serialize(profiles, new JsonSerializerOptions { WriteIndented = true }));
            StatusText.Text = $"전체 {profiles.Count:N0}개 설정 파일 저장 완료: {dialog.FileName}";
        }
        catch (Exception ex) { Error("파일 저장 실패", ex.Message); }
    }
    void LoadFileButton_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            var dialog = new OpenFileDialog { Filter = "Weld 설정 (*.weld.json;*.json)|*.weld.json;*.json|모든 파일 (*.*)|*.*" };
            if (dialog.ShowDialog(this) != true)
                return;
            var loaded = JsonSerializer.Deserialize<Dictionary<string, Settings>>(File.ReadAllText(dialog.FileName)) ?? throw new InvalidDataException("설정 파일이 비어 있습니다.");
            var checkedProfiles = loaded.Where(x => IsProfileKey(x.Key)).ToDictionary(x => x.Key, x => ValidateSettings(x.Value));
            profiles.Clear();
            foreach (var item in checkedProfiles)
                profiles[item.Key] = item.Value;
            if (profiles.TryGetValue(SelectedProfileKey(), out Settings selected))
                SetUiSettings(selected);
            StatusText.Text = $"전체 {profiles.Count:N0}개 설정 파일 로드 완료: {dialog.FileName}";
        }
        catch (Exception ex) { Error("파일 로드 실패", ex.Message); }
    }
    async void SaveProfileButton_Click(object sender, RoutedEventArgs e)
    {
        SetProfileOperationEnabled(false);
        try
        {
            profiles[SelectedProfileKey()] = ValidateSettings(GetUiSettings());
            if (!simulation)
            {
                Status deviceStatus = ParseStatus(await Command("GET STATUS"));
                if (deviceStatus.Welding)
                    throw new InvalidOperationException("용접 동작이 끝난 후 장비 전체 저장을 실행하세요.");
                string clearReply = await Command("CLEAR PROFILES", 120000);
                if (clearReply != "OK")
                    throw new IOException("전체 초기화 응답: " + clearReply);
                int done = 0;
                foreach (var item in profiles.OrderBy(x => x.Key))
                {
                    (int file, int number) = ParseProfileKey(item.Key);
                    string reply = await Command($"SET PROFILE file={file} number={number}");
                    if (reply != "OK")
                        throw new IOException($"{item.Key} 선택 응답: {reply}");
                    await SendSettings(item.Value);
                    reply = await Command("SAVE PROFILE FAST", 10000);
                    if (reply != "OK")
                        throw new IOException($"{item.Key} 저장 응답: {reply}");
                    if ((++done % 10) == 0)
                        StatusText.Text = $"장비 전체 저장 중: {done:N0} / {profiles.Count:N0}";
                }
            }
            StatusText.Text = $"장비 전체 저장 완료: {profiles.Count:N0}개 설정";
        }
        catch (Exception ex) { Error("장비 저장 실패", ex.Message); }
        finally { SetProfileOperationEnabled(timer.IsEnabled); }
    }
    async void LoadProfileButton_Click(object sender, RoutedEventArgs e)
    {
        SetProfileOperationEnabled(false);
        try
        {
            if (simulation)
            {
                StatusText.Text = $"시뮬레이션 전체 불러오기 완료: {profiles.Count:N0}개 설정";
            }
            else
            {
                await LoadAllProfiles();
            }
            if (profiles.TryGetValue(SelectedProfileKey(), out Settings selected))
                SetUiSettings(selected);
            StatusText.Text = $"장비 전체 불러오기 완료: {profiles.Count:N0}개 설정";
        }
        catch (Exception ex) { Error("장비 불러오기 실패", ex.Message); }
        finally { SetProfileOperationEnabled(timer.IsEnabled); }
    }

    static bool IsProfileKey(string key)
    {
        try
        {
            var (file, number) = ParseProfileKey(key);
            return file is >= 0 and <= 15 && number is >= 0 and <= 15;
        }
        catch { return false; }
    }
    static (int File, int Number) ParseProfileKey(string key)
    {
        string[] p = key.Split('-');
        if (p.Length != 2 || !int.TryParse(p[0], out int file) || !int.TryParse(p[1], out int number))
            throw new InvalidDataException($"잘못된 파일/번호: {key}");
        return (file, number);
    }
    async Task LoadAllProfiles()
    {
        var connectionWriter = writer ?? throw new IOException("연결되지 않았습니다.");
        await commandLock.WaitAsync();
        try
        {
            if (!ReferenceEquals(connectionWriter, writer))
                throw new IOException("연결이 변경되거나 해제되었습니다.");
            await connectionWriter.WriteLineAsync("DUMP PROFILES");
            var loaded = new Dictionary<string, Settings>();
            while (true)
            {
                string line = await ReadLine(120000) ?? throw new IOException("전체 설정 수신 시간 초과");
                if (line.StartsWith("ERR ", StringComparison.Ordinal))
                    throw new IOException("장비 응답: " + line);
                using var document = JsonDocument.Parse(line);
                var root = document.RootElement;
                string type = root.GetProperty("type").GetString() ?? "";
                if (type == "profiles_end")
                    break;
                if (type != "profile")
                    throw new InvalidDataException("알 수 없는 전체 설정 응답입니다.");
                string key = $"{root.GetProperty("file").GetInt32():00}-{root.GetProperty("number").GetInt32():00}";
                loaded[key] = ValidateSettings(ParseSettingsJson(root));
                if ((loaded.Count % 10) == 0)
                    StatusText.Text = $"장비 전체 불러오는 중: {loaded.Count:N0}개";
            }
            profiles.Clear();
            foreach (var item in loaded)
                profiles[item.Key] = item.Value;
        }
        finally { commandLock.Release(); }
    }

    async void ConnectButton_Click(object sender, RoutedEventArgs e)
    {
        ConnectButton.IsEnabled = false;
        try
        {
            simulation = SimulationCheckBox.IsChecked == true;
            if (!simulation)
            {
                if (!int.TryParse(PortBox.Text, out int port) || port is < 1 or > 65535)
                    throw new InvalidOperationException("포트 번호가 올바르지 않습니다.");
                client = new TcpClient { ReceiveBufferSize = 1024 * 1024 };
                using var timeout = new CancellationTokenSource(4000);
                await client.ConnectAsync(IpAddressBox.Text.Trim(), port, timeout.Token);
                var stream = client.GetStream();
                networkStream = stream;
                writer = new(stream, Encoding.ASCII, 1024, true)
                {
                    AutoFlush = true,
                    NewLine = "\n"
                };
                if (await ReadLine(2000) != "WELD3 READY" || await Command("PING") != "PONG")
                    throw new IOException("Weld3 프로토콜 응답이 없습니다.");
                await LoadSettings();
                await LoadPid();
            }
            SetConnected(true);
            SettingWaveform.ClearMeasured();
            timer.Start();
            StatusText.Text = "외부 용접 시작을 기다립니다.";
        }
        catch (Exception ex) { Log($"! 연결 실패: {ex}"); Disconnect(); Error("연결 실패", ex.Message); }
    }
    void DisconnectButton_Click(object sender, RoutedEventArgs e) => Disconnect();
    void Disconnect()
    {
        timer.Stop();
        adcSummary = "연결 해제 · 실시간 값 없음";
        adcWindow?.SetReading(adcSummary);
        simCycleActive = false;
        cancelTestStart = true;
        writer?.Dispose();
        networkStream?.Dispose();
        client?.Dispose();
        networkStream = null;
        writer = null;
        client = null;
        SetConnected(false);
        SetTestBusy(false);
    }

    async void Poll(object? sender, EventArgs e)
    {
        if (polling || testStarting)
            return;
        polling = true;
        try
        {
            if (PwmTestCheckBox.IsChecked == true || testBusy)
                await PollTest();
            else
                UpdateStatus(simulation ? SimStatus() : ParseStatus(await Command("GET STATUS")));
        }
        catch (Exception ex) { StatusText.Text = "수신 오류: " + ex.Message; if (!simulation) Disconnect(); }
        finally { polling = false; }
    }
    async Task LoadSettings()
    {
        Settings x;
        if (simulation)
            x = GetUiSettings();
        else
        {
            string reply = await Command("GET SETTINGS");
            if (!reply.TrimStart().StartsWith("{", StringComparison.Ordinal))
                throw new IOException("장비가 설정 조회 명령을 지원하지 않습니다: " + reply);
            using var d = JsonDocument.Parse(JsonPayload(reply));
            x = ParseSettingsJson(d.RootElement);
        }
        SetUiSettings(x);
    }
    static Settings ParseSettingsJson(JsonElement r) => new(
        r.GetProperty("squeeze_ms").GetUInt32(),
        r.GetProperty("current1_a").GetDouble(),
        r.GetProperty("up1_ms").GetUInt32(),
        r.GetProperty("time1_ms").GetUInt32(),
        r.GetProperty("down1_ms").GetUInt32(),
        r.GetProperty("cool1_ms").GetUInt32(),
        r.GetProperty("current2_a").GetDouble(),
        r.GetProperty("up2_ms").GetUInt32(),
        r.GetProperty("time2_ms").GetUInt32(),
        r.GetProperty("down2_ms").GetUInt32(),
        r.GetProperty("cool2_ms").GetUInt32(),
        r.GetProperty("current3_a").GetDouble(),
        r.GetProperty("up3_ms").GetUInt32(),
        r.GetProperty("time3_ms").GetUInt32(),
        r.GetProperty("down3_ms").GetUInt32());
    async Task SendSettings(Settings x)
    {
        string cmd = FormattableString.Invariant($"SET SETTINGS squeeze_ms={x.Squeeze} current1_a={x.Current1:0.###} up1_ms={x.Up1} time1_ms={x.Time1} down1_ms={x.Down1} cool1_ms={x.Cool1} current2_a={x.Current2:0.###} up2_ms={x.Up2} time2_ms={x.Time2} down2_ms={x.Down2} cool2_ms={x.Cool2} current3_a={x.Current3:0.###} up3_ms={x.Up3} time3_ms={x.Time3} down3_ms={x.Down3}");
        string reply = await Command(cmd);
        if (reply != "OK")
            throw new IOException("장비 응답: " + reply);
    }
    async Task LoadPid()
    {
        string reply = await Command("GET PID");
        if (!reply.StartsWith("{", StringComparison.Ordinal))
        {
            StatusText.Text = "구형 펌웨어: PID 조회를 지원하지 않아 기본값을 사용합니다.";
            return;
        }
        try
        {
            using var d = JsonDocument.Parse(JsonPayload(reply));
            var r = d.RootElement;
            PidKpBox.Text = r.GetProperty("kp").GetDouble().ToString("0.######", CultureInfo.InvariantCulture);
            PidKiBox.Text = r.GetProperty("ki").GetDouble().ToString("0.######", CultureInfo.InvariantCulture);
            PidKdBox.Text = r.GetProperty("kd").GetDouble().ToString("0.######", CultureInfo.InvariantCulture);
            PidEnableCheckBox.IsChecked = r.GetProperty("enabled").GetUInt32() != 0;
        }
        catch { Log("! GET PID 응답 형식 오류: " + reply); StatusText.Text = "구형 펌웨어: PID 기본값을 사용합니다."; }
    }
    async void ApplyPidButton_Click(object sender, RoutedEventArgs e)
    {
        if (!Connected() || testBusy)
            return;
        try
        {
            await SendPid(PidEnableCheckBox.IsChecked == true);
            StatusText.Text = "PID 설정을 적용했습니다.";
        }
        catch (Exception ex) { Error("PID 적용 실패", ex.Message); }
    }
    async Task SendPid(bool enabled)
    {
        double ReadPid(TextBox box, double max)
        {
            if (!double.TryParse(box.Text, NumberStyles.Float, CultureInfo.InvariantCulture, out double v) || !double.IsFinite(v) || v < 0 || v > max)
                throw new InvalidOperationException("PID 범위: Kp 0~100, Ki 0~1000, Kd 0~100");
            return v;
        }
        double kp = ReadPid(PidKpBox, 100), ki = ReadPid(PidKiBox, 1000), kd = ReadPid(PidKdBox, 100);
        const double target = 0; // Legacy protocol field; stage ADC values drive PID.
        if (simulation)
            return;
        string reply = await Command(FormattableString.Invariant($"SET PID kp={kp:R} ki={ki:R} kd={kd:R} target={target:R} enabled={(enabled ? 1 : 0)}"));
        if (reply != "OK")
            throw new IOException("PID 설정 실패: " + reply);
        using var doc = JsonDocument.Parse(await Command("GET PID"));
        var r = doc.RootElement;
        if (!r.TryGetProperty("target_mode", out var mode) || mode.GetString() != "stage_adc")
            throw new IOException("단계별 ADC 목표를 지원하는 새 펌웨어가 필요합니다.");
        if (r.GetProperty("enabled").GetUInt32() != (enabled ? 1U : 0U) ||
           Math.Abs(r.GetProperty("kp").GetDouble() - kp) > 0.0001 ||
           Math.Abs(r.GetProperty("ki").GetDouble() - ki) > 0.0001 ||
           Math.Abs(r.GetProperty("kd").GetDouble() - kd) > 0.0001 ||
           Math.Abs(r.GetProperty("target").GetDouble() - target) > 0.0001)
            throw new IOException("PID 설정 읽기 확인 실패");
    }
    static Settings ValidateSettings(Settings x)
    {
        double[] currents = [x.Current1, x.Current2, x.Current3];
        uint[] hold = [x.Time1, x.Time2, x.Time3], up = [x.Up1, x.Up2, x.Up3], down = [x.Down1, x.Down2, x.Down3];
        if (x.Squeeze > 999 || currents.Any(v => !double.IsFinite(v) || v is < 0 or > 65535) || hold.Any(v => v > 999) || up.Concat(down).Any(v => v > 500) || x.Cool1 > 999 || x.Cool2 > 999)
            throw new InvalidOperationException("범위: ADC 목표 0~65535, SQ/COOL·Weld Time 0~999 ms, UP/DOWN 0~500 ms");
        if (currents.Where((v, i) => (v == 0) != (hold[i] == 0) || (v == 0 && (up[i] != 0 || down[i] != 0))).Any())
            throw new InvalidOperationException("미사용 단은 ADC 목표·UP·Weld Time·DOWN을 모두 0으로 설정하세요.");
        return x;
    }
    void SetUiSettings(Settings x)
    {
        StageRows[0].Set(x.Current1, x.Up1, x.Time1, x.Down1, x.Squeeze);
        StageRows[1].Set(x.Current2, x.Up2, x.Time2, x.Down2, x.Cool1);
        StageRows[2].Set(x.Current3, x.Up3, x.Time3, x.Down3, x.Cool2);
        SettingsGrid.Items.Refresh();
        UpdateWaveform();
    }
    Settings GetUiSettings()
    {
        var a = StageRows[0];
        var b = StageRows[1];
        var c = StageRows[2];
        return new(a.Cool, a.Current, a.Up, a.Hold, a.Down, b.Cool, b.Current, b.Up, b.Hold, b.Down, c.Cool, c.Current, c.Up, c.Hold, c.Down);
    }
    async Task<string> Command(string text, int timeoutMs = 2000)
    {
        var connectionWriter = writer ?? throw new IOException("연결되지 않았습니다.");
        var connectionStream = networkStream ?? throw new IOException("연결되지 않았습니다.");
        await commandLock.WaitAsync();
        try
        {
            // A disconnect/reconnect can occur while this command waits for the lock.
            if (!ReferenceEquals(connectionWriter, writer) || !ReferenceEquals(connectionStream, networkStream))
                throw new IOException("연결이 변경되거나 해제되었습니다.");
            Log($"> {text}");
            await connectionWriter.WriteLineAsync(text);
            string reply = await ReadLine(connectionStream, timeoutMs) ?? throw new IOException("응답 시간 초과");
            Log($"< {reply}");
            return reply;
        }
        finally { commandLock.Release(); }
    }
    void Log(string message)
    {
        string line = $"[{DateTime.Now:HH:mm:ss.fff}] {message}";
        System.Diagnostics.Debug.WriteLine(line);
        Console.WriteLine(line);
        if (LogTextBox is null)
            return;
        LogTextBox.AppendText(line + Environment.NewLine);
        LogTextBox.ScrollToEnd();
    }
    Task<string?> ReadLine(int ms)
    {
        var stream = networkStream;
        return stream is null ? Task.FromResult<string?>(null) : ReadLine(stream, ms);
    }
    static async Task<string?> ReadLine(NetworkStream stream, int ms)
    {
        using var c = new CancellationTokenSource(ms);
        var bytes = new List<byte>(128);
        var one = new byte[1];
        while (true)
        {
            int n = await stream.ReadAsync(one, c.Token);
            if (n == 0)
                return bytes.Count == 0 ? null : Encoding.ASCII.GetString(bytes.ToArray());
            if (one[0] == '\n')
                return Encoding.ASCII.GetString(bytes.ToArray()).TrimEnd('\r');
            bytes.Add(one[0]);
            if (bytes.Count > 4096)
                throw new IOException("응답 줄이 너무 깁니다.");
        }
    }
    async Task ReadExact(Memory<byte> destination, int timeoutMs)
    {
        var stream = networkStream ?? throw new IOException("연결되지 않았습니다.");
        using var c = new CancellationTokenSource(timeoutMs);
        int offset = 0;
        while (offset < destination.Length)
        {
            int n = await stream.ReadAsync(destination[offset..], c.Token);
            if (n == 0)
                throw new EndOfStreamException("LCD 화면 수신이 중단되었습니다.");
            offset += n;
        }
    }

    async void CaptureScreenButton_Click(object sender, RoutedEventArgs e)
    {
        if (!Connected() || simulation)
        {
            Error("LCD 캡처", "실제 장비 연결에서만 사용할 수 있습니다.");
            return;
        }
        string captureStage = "장비 응답 대기";
        try
        {
            CaptureScreenButton.IsEnabled = false;
            CaptureProgressPanel.Visibility = Visibility.Visible;
            CaptureProgressBar.Value = 0;
            CaptureProgressText.Text = "요청 중";
            StatusText.Text = "LCD 화면 캡처를 요청하고 있습니다.";
            await commandLock.WaitAsync();
            byte[] pixels;
            try
            {
                if (writer is null)
                    throw new IOException("연결되지 않았습니다.");
                await writer.WriteLineAsync("CAPTURE SCREEN");
                string header;
                while (true)
                {
                    header = await ReadLine(60000) ?? throw new IOException("LCD 캡처 응답이 없습니다.");
                    if (header == "CAPTURE CHECK")
                    {
                        captureStage = "프레임버퍼 확인";
                        CaptureProgressText.Text = "화면 확인";
                        StatusText.Text = "LCD 프레임버퍼 주소를 확인했습니다.";
                        continue;
                    }
                    if (header == "CAPTURE COPY")
                    {
                        captureStage = "화면 복사";
                        CaptureProgressBar.IsIndeterminate = true;
                        CaptureProgressText.Text = "화면 복사";
                        StatusText.Text = "장비에서 LCD 화면을 캡처 버퍼로 복사하고 있습니다.";
                        continue;
                    }
                    if (header == "CAPTURE HASH")
                    {
                        captureStage = "해시 계산";
                        CaptureProgressText.Text = "해시 계산";
                        StatusText.Text = "장비에서 LCD 화면 데이터를 검증할 해시를 계산하고 있습니다.";
                        continue;
                    }
                    break;
                }
                string[] part = header.Split(' ', StringSplitOptions.RemoveEmptyEntries);
                if (part.Length != 6 ||
                    part[0] != "SCREEN" ||
                    part[1] != "RGB888" ||
                    !int.TryParse(part[2], out int width) ||
                    !int.TryParse(part[3], out int height) ||
                    !int.TryParse(part[4], out int size) ||
                    !uint.TryParse(part[5], NumberStyles.HexNumber, CultureInfo.InvariantCulture, out uint expectedHash))
                    throw new IOException("잘못된 LCD 캡처 헤더: " + header);
                if (width != 480 || height != 272 || size != width * height * 3)
                    throw new IOException("지원하지 않는 LCD 화면 형식입니다.");
                captureStage = "화면 데이터 수신";
                CaptureProgressBar.IsIndeterminate = false;
                StatusText.Text = $"LCD 화면 데이터 수신 중: {size:N0} bytes";
                pixels = new byte[size];
                int received = 0, lastPercent = -1;
                while (received < size)
                {
                    if (networkStream is null)
                        throw new IOException("연결되지 않았습니다.");
                    using var timeout = new CancellationTokenSource(10000);
                    int count = await networkStream.ReadAsync(pixels.AsMemory(received), timeout.Token);
                    if (count == 0)
                        throw new EndOfStreamException("LCD 화면 수신이 중단되었습니다.");
                    received += count;
                    int percent = received * 100 / size;
                    if (percent != lastPercent)
                    {
                        lastPercent = percent;
                        CaptureProgressBar.Value = percent;
                        CaptureProgressText.Text = $"{percent}%  {received / 1024:N0} KB";
                        StatusText.Text = $"LCD 화면 수신 중: {received:N0} / {size:N0} bytes ({percent}%)";
                    }
                }
                captureStage = "화면 데이터 검증";
                CaptureProgressText.Text = "검증 중";
                uint hash = 2166136261;
                foreach (byte b in pixels)
                    hash = (hash ^ b) * 16777619;
                if (hash != expectedHash)
                    throw new IOException("LCD 화면 해시가 일치하지 않습니다.");
            }
            finally { commandLock.Release(); }
            var dialog = new SaveFileDialog { Filter = "PNG 이미지 (*.png)|*.png", FileName = $"Weld3_LCD_{DateTime.Now:yyyyMMdd_HHmmss}.png" };
            if (dialog.ShowDialog(this) == true)
            {
                var bitmap = BitmapSource.Create(480, 272, 96, 96, PixelFormats.Bgr24, null, pixels, 480 * 3);
                var encoder = new PngBitmapEncoder();
                encoder.Frames.Add(BitmapFrame.Create(bitmap));
                using var output = File.Create(dialog.FileName);
                encoder.Save(output);
                StatusText.Text = "LCD 화면 저장 완료: " + dialog.FileName;
            }
        }
        catch (OperationCanceledException) { Disconnect(); Error("LCD 캡처 실패", $"{captureStage} 시간 초과입니다. 연결을 초기화했습니다. 장비에 최신 펌웨어가 설치되어 있는지 확인하고 다시 연결하세요."); }
        catch (Exception ex) { Disconnect(); Error("LCD 캡처 실패", ex.Message + "\n연결을 초기화했습니다. 다시 연결하세요."); }
        finally { CaptureProgressBar.IsIndeterminate = false; CaptureProgressPanel.Visibility = Visibility.Collapsed; CaptureScreenButton.IsEnabled = timer.IsEnabled && !simulation; }
    }
    static string JsonPayload(string reply)
    {
        string text = reply.Trim();
        int start = text.IndexOf('{');
        if (start >= 0)
            return text[start..];
        if (text.StartsWith("\"", StringComparison.Ordinal) && text.Contains(":", StringComparison.Ordinal))
            return "{" + text + "}";
        throw new IOException("장비가 JSON 응답을 보내지 않았습니다: " + reply);
    }
    AdcWindow? adcWindow;
    string adcSummary = "측정값 수신 대기";

    void OpenAdcWindow_Click(object sender, RoutedEventArgs e)
    {
        if (adcWindow is null)
        {
            adcWindow = new AdcWindow { Owner = this };
            adcWindow.Closed += (_, _) => adcWindow = null;
        }
        adcWindow.SetReading(adcSummary);
        adcWindow.SetTrace(testTrace);
        adcWindow.Show();
        adcWindow.Activate();
    }

    void UpdateSecondary(JsonElement response)
    {
        string Raw(string name) => response.TryGetProperty(name, out var v) ? v.GetUInt32().ToString() : "미지원";
        string Filter(string name) => response.TryGetProperty(name, out var v)
            ? (v.GetUInt32() * 65535.0 / 1000000.0).ToString("0.0") : "미지원";
        adcSummary = $"수신: {DateTime.Now:HH:mm:ss.fff}\n\n" +
            $"1차 ADC 원본: {Raw("adc_mean")}\nPID 필터 ADC: {Filter("primary_filtered_micro")}\n\n" +
            $"2차 / 로고스키 ADC 원본: {Raw("secondary_adc")}\n필터 ADC: {Filter("secondary_filtered_micro")}";
        adcWindow?.SetReading(adcSummary);
    }

    Status ParseStatus(string json)
    {
        using var d = JsonDocument.Parse(JsonPayload(json));
        var r = d.RootElement;
        UpdateSecondary(r);
        return new(r.GetProperty("welding").GetByte() != 0, r.GetProperty("fault").GetByte() != 0, r.GetProperty("fault_code").GetUInt16(), r.GetProperty("current_a").GetDouble(), r.GetProperty("weld_time_ms").GetUInt32());
    }
    Status SimStatus()
    {
        phase += .13;
        if (!simCycleActive)
            return new(false, false, 0, 0, 0);
        uint t = (uint)(DateTime.Now - simCycleStart).TotalMilliseconds;
        var s = GetUiSettings();
        uint total = s.Squeeze + s.Up1 + s.Time1 + s.Down1 + s.Cool1 + s.Up2 + s.Time2 + s.Down2 + s.Cool2 + s.Up3 + s.Time3 + s.Down3;
        if (t >= total)
        {
            simCycleActive = false;
            return new(false, false, 0, 0, total);
        }
        double target = ConfiguredCurrent(s, t);
        return new(true, false, 0, Math.Max(0, target + Math.Sin(phase) * target * .03), t);
    }
    static double ConfiguredCurrent(Settings s, uint t)
    {
        (uint wait, double current, uint up, uint hold, uint down)[] stages = [(s.Squeeze, s.Current1, s.Up1, s.Time1, s.Down1), (s.Cool1, s.Current2, s.Up2, s.Time2, s.Down2), (s.Cool2, s.Current3, s.Up3, s.Time3, s.Down3)];
        uint cursor = 0;
        foreach (var q in stages)
        {
            cursor += q.wait;
            if (t < cursor)
                return 0;
            if (q.current == 0)
                continue;
            if (t < cursor + q.up)
                return q.up == 0 ? q.current : q.current * (t - cursor) / q.up;
            cursor += q.up;
            if (t < cursor + q.hold)
                return q.current;
            cursor += q.hold;
            if (t < cursor + q.down)
                return q.down == 0 ? 0 : q.current * (1.0 - (t - cursor) / (double)q.down);
            cursor += q.down;
        }
        return 0;
    }
    void UpdateStatus(Status x)
    {
        CurrentValueText.Text = $"{x.Current:0.00} A";
        WeldingStateText.Text = x.Welding ? "용접 중" : "대기 / 결과 표시";
        FaultStateText.Text = x.Fault ? $"FAULT {x.FaultCode}" : "정상";
        FaultStateText.Foreground = new SolidColorBrush(x.Fault ? Colors.OrangeRed : Color.FromRgb(74, 222, 128));
        WeldTimeText.Text = $"용접 시간: {x.Time:N0} ms";
        LastUpdateText.Text = $"마지막 수신: {DateTime.Now:HH:mm:ss.fff}";
        if (x.Welding && !wasWelding)
        {
            SettingWaveform.ClearMeasured();
            StatusText.Text = "용접 데이터를 수집하고 있습니다.";
        }
        if (x.Welding)
            SettingWaveform.AddMeasured(x.Time, x.Current);
        if (!x.Welding && wasWelding)
            StatusText.Text = "용접 완료: 설정 파형과 실제 결과를 비교하세요.";
        wasWelding = x.Welding;
    }
    void UpdateWaveform()
    {
        if (SettingWaveform is null)
            return;
        try
        {
            var x = GetUiSettings();
            SettingWaveform.SetStages(x.Squeeze, x.Current1, x.Up1, x.Time1, x.Down1, x.Cool1, x.Current2, x.Up2, x.Time2, x.Down2, x.Cool2, x.Current3, x.Up3, x.Time3, x.Down3);
        }
        catch { }
    }
    void SettingsGrid_CellEditEnding(object? sender, DataGridCellEditEndingEventArgs e) => Dispatcher.BeginInvoke(UpdateWaveform);
    void SettingsGrid_BeginningEdit(object? sender, DataGridBeginningEditEventArgs e)
    {
    }
    void SettingsGrid_PreviewMouseLeftButtonDown(object sender, MouseButtonEventArgs e)
    {
        DependencyObject? p = e.OriginalSource as DependencyObject;
        while (p is not null and not DataGridCell)
            p = VisualTreeHelper.GetParent(p);
        if (p is not DataGridCell cell || cell.Column.DisplayIndex != 0)
            return;
        object item = cell.DataContext;
        SettingsGrid.SelectedCells.Clear();
        foreach (var column in SettingsGrid.Columns.OrderBy(x => x.DisplayIndex))
            SettingsGrid.SelectedCells.Add(new DataGridCellInfo(item, column));
        SettingsGrid.CurrentCell = new DataGridCellInfo(item, SettingsGrid.Columns[0]);
        SettingsGrid.ScrollIntoView(item);
        SettingsGrid.Focus();
        Keyboard.Focus(cell);
        e.Handled = true;
    }
    void SettingsGrid_PreviewKeyDown(object sender, KeyEventArgs e)
    {
        // Let the active cell editor handle text deletion/copy/paste itself.
        if (Keyboard.FocusedElement is TextBox)
            return;

        if (e.Key is Key.Delete or Key.Back)
        {
            e.Handled = true;
            ClearGridSelection();
            return;
        }
        if ((Keyboard.Modifiers & ModifierKeys.Control) == 0)
            return;
        bool copy = e.Key == Key.C || e.SystemKey == Key.C || e.ImeProcessedKey == Key.C || Keyboard.IsKeyDown(Key.C);
        bool paste = e.Key == Key.V || e.SystemKey == Key.V || e.ImeProcessedKey == Key.V || Keyboard.IsKeyDown(Key.V);
        if (copy)
        {
            e.Handled = true;
            CopyGridSelection();
        }
        else if (paste)
        {
            e.Handled = true;
            PasteGridClipboard();
        }
    }
    void ClearGridSelection()
    {
        var selectedCells = SettingsGrid.SelectedCells.ToArray();
        if (!FinishGridEdit())
            return;

        int count = 0;
        foreach (var cell in selectedCells.Where(x => x.Column.DisplayIndex > 0))
        {
            ((StageRow)cell.Item).SetColumn(cell.Column.DisplayIndex, 0);
            count++;
        }
        SettingsGrid.Items.Refresh();
        UpdateWaveform();
        StatusText.Text = count > 0 ? $"선택한 설정값 {count}개를 0으로 변경했습니다." : "지울 설정 셀을 선택하세요.";
    }
    void CopyGridSelection()
    {
        var cells = SettingsGrid.SelectedCells.Where(x => x.Column.DisplayIndex > 0).Select(x => new { Row = SettingsGrid.Items.IndexOf(x.Item), Col = x.Column.DisplayIndex, RowData = (StageRow)x.Item }).OrderBy(x => x.Row).ThenBy(x => x.Col).ToList();
        if (cells.Count == 0)
            return;
        var lines = cells.GroupBy(x => x.Row).Select(row => string.Join("\t", row.Select(x => x.Col switch
            {
                1 => x.RowData.Current.ToString(CultureInfo.InvariantCulture),
                2 => x.RowData.Cool.ToString(CultureInfo.InvariantCulture),
                3 => x.RowData.Up.ToString(CultureInfo.InvariantCulture),
                4 => x.RowData.Hold.ToString(CultureInfo.InvariantCulture),
                5 => x.RowData.Down.ToString(CultureInfo.InvariantCulture),
                _ => ""
            })));
        Clipboard.SetText(string.Join(Environment.NewLine, lines));
        StatusText.Text = "선택한 설정값을 클립보드에 복사했습니다.";
    }
    void PasteGridClipboard()
    {
        string text = Clipboard.GetText();
        if (string.IsNullOrWhiteSpace(text))
        {
            StatusText.Text = "클립보드에 붙여넣을 숫자가 없습니다.";
            return;
        }
        int row = Math.Max(0, SettingsGrid.Items.IndexOf(SettingsGrid.CurrentItem)), col = Math.Max(1, SettingsGrid.CurrentColumn?.DisplayIndex ?? 1);
        if (!FinishGridEdit())
            return;

        string[][] cells = text.TrimEnd('\r', '\n').Split('\n').Select(x => x.TrimEnd('\r').Split('\t')).ToArray();
        int count = 0;
        for (int r = 0; r < cells.Length && row + r < 3; r++)
            for (int c = 0; c < cells[r].Length && col + c <= 5; c++)
                if (double.TryParse(cells[r][c], NumberStyles.Float, CultureInfo.InvariantCulture, out double v))
                {
                    StageRows[row + r].SetColumn(col + c, v);
                    count++;
                }
        SettingsGrid.Items.Refresh();
        UpdateWaveform();
        StatusText.Text = count > 0 ? $"설정값 {count}개를 붙여넣었습니다." : "붙여넣을 숫자를 찾지 못했습니다.";
    }
    void CopyButton_Click(object sender, RoutedEventArgs e) => CopyGridSelection();
    void PasteButton_Click(object sender, RoutedEventArgs e) => PasteGridClipboard();

    bool FinishGridEdit()
    {
        if (SettingsGrid.CommitEdit(DataGridEditingUnit.Cell, true) &&
            SettingsGrid.CommitEdit(DataGridEditingUnit.Row, true))
            return true;

        StatusText.Text = "편집 중인 셀에 올바른 숫자를 입력하거나 Esc로 취소하세요.";
        return false;
    }
    async void StartButton_Click(object sender, RoutedEventArgs e)
    {
        if (!Connected() || testStarting || testBusy)
            return;
        testStarting = true;
        cancelTestStart = false;
        try
        {
            if (!FinishGridEdit())
                return;
            var settings = ValidateSettings(GetUiSettings());
            if (PwmTestCheckBox.IsChecked == true || PidEnableCheckBox.IsChecked == true)
            {
                if (!uint.TryParse(TestDutyBox.Text, out uint limit) || limit is < 1 or > 45)
                    throw new InvalidOperationException("시험 듀티 상한은 각 상 기준 1~45% 정수입니다.");
                double peak = Math.Max(settings.Current1, Math.Max(settings.Current2, settings.Current3));
                if (peak <= 0)
                    throw new InvalidOperationException("시험할 단계를 한 개 이상 설정하세요.");
                testSettings = settings;
                testDuty = limit;
                testTrace.Clear();
                adcWindow?.SetTrace(testTrace);
                SaveTestTraceButton.IsEnabled = false;
                loadedTestId = 0;
                TestWaveform.ClearMeasured();
                SetTestPreview(settings, limit);
                WaveTabs.SelectedIndex = 1;
                SetTestBusy(true);
                if (simulation)
                {
                    simCycleStart = DateTime.Now;
                    simCycleActive = true;
                }
                else
                {
                    // Verify support before applying settings or issuing any output command.
                    using var supported = JsonDocument.Parse(await Command("TEST STATUS"));
                    if (supported.RootElement.GetProperty("active").GetUInt32() != 0)
                        throw new InvalidOperationException("장비에서 이미 시험 중입니다.");
                    await SendPid(PwmTestCheckBox.IsChecked != true);
                    await SendSettings(settings);
                    if (cancelTestStart)
                        return;
                    string reply = await Command($"TEST START duty_percent={limit}");
                    if (reply != "OK TEST")
                        throw new IOException("장비 응답: " + reply);
                }
                StatusText.Text = $"PWM 단발 시험 시작 · 각 상 최대 {limit}% · {(PwmTestCheckBox.IsChecked == true ? "PID OFF" : "PID ON · ADC 정규화")}";
            }
            else
            {
                SettingWaveform.ClearMeasured();
                if (simulation)
                {
                    simCycleStart = DateTime.Now;
                    simCycleActive = true;
                }
                else
                {
                    await SendSettings(settings);
                    string reply = await Command("START");
                    if (reply != "OK DRYRUN")
                        throw new IOException("장비 응답: " + reply);
                }
                StatusText.Text = "시간 확인 시작 · PWM 출력 없음";
            }
        }
        catch (Exception ex) { SetTestBusy(false); Error("START 실패", ex.Message); }
        finally { testStarting = false; if (cancelTestStart) SetTestBusy(false); }
    }

    void SetTestBusy(bool active)
    {
        testBusy = active;
        bool connected = timer.IsEnabled;
        SetProfileOperationEnabled(connected && !active);
        SettingsGrid.IsEnabled = ProfileFileBox.IsEnabled = ProfileNumberBox.IsEnabled = !active;
        PwmTestCheckBox.IsEnabled = TestDutyBox.IsEnabled = PidEnableCheckBox.IsEnabled = ApplyPidButton.IsEnabled = PidKpBox.IsEnabled = PidKiBox.IsEnabled = PidKdBox.IsEnabled = !active;
        StopCycleButton.IsEnabled = connected;
        CaptureScreenButton.IsEnabled = connected && !active && !simulation;
    }
    void SetTestPreview(Settings s, double limit)
    {
        double scale = limit / Math.Max(s.Current1, Math.Max(s.Current2, s.Current3));
        TestWaveform.SetStages(s.Squeeze, s.Current1 * scale, s.Up1, s.Time1, s.Down1, s.Cool1,
            s.Current2 * scale, s.Up2, s.Time2, s.Down2, s.Cool2, s.Current3 * scale, s.Up3, s.Time3, s.Down3);
    }
    async void StopButton_Click(object sender, RoutedEventArgs e)
    {
        cancelTestStart = true;
        try
        {
            if (simulation)
            {
                simCycleActive = false;
                SetTestBusy(false);
            }
            else if (await Command("STOP") != "OK")
                throw new IOException("STOP 응답 오류");
            StatusText.Text = "STOP 요청 완료";
        }
        catch (Exception ex) { Disconnect(); Error("STOP 통신 실패", ex.Message + " · 장비는 통신 감시 시간 초과 시 정지합니다."); }
    }
    async Task PollTest()
    {
        CurrentValueText.Text = "미보정";
        if (simulation)
        {
            if (!simCycleActive)
                return;
            uint total = TestDuration(testSettings), t = (uint)(DateTime.Now - simCycleStart).TotalMilliseconds;
            bool active = t < total;
            t = Math.Min(t, total);
            double peak = Math.Max(testSettings.Current1, Math.Max(testSettings.Current2, testSettings.Current3));
            double target = active ? ConfiguredCurrent(testSettings, t) : 0;
            double duty = testDuty * target / peak;
            testTrace.Add(FormattableString.Invariant($"{t},{target * 1000:0},{duty * 10:0},0,0,0"));
            TestWaveform.AddMeasured(t, duty);
            TestStateText.Text = $"시뮬레이션 {t} ms · 듀티 {duty:0.0}% · ADC 없음";
            if (!active)
            {
                simCycleActive = false;
                testTrace.Clear();
                TestWaveform.ClearMeasured();
                uint step = Math.Max(1, (total + 2045) / 2046);
                for (uint at = 0; at < total; at += step)
                {
                    double v = ConfiguredCurrent(testSettings, at), d = testDuty * v / peak;
                    testTrace.Add(FormattableString.Invariant($"{at},{v * 1000:0},{d * 10:0},0,0,0"));
                    TestWaveform.AddMeasured(at, d);
                }
                testTrace.Add($"{total},0,0,0,0,0");
                TestWaveform.AddMeasured(total, 0);
                SetTestBusy(false);
                SaveTestTraceButton.IsEnabled = true;
            }
            return;
        }
        using var doc = JsonDocument.Parse(await Command("TEST STATUS"));
        var r = doc.RootElement;
        UpdateSecondary(r);
        bool running = r.GetProperty("active").GetUInt32() != 0;
        uint id = r.GetProperty("id").GetUInt32(), reason = r.GetProperty("reason").GetUInt32();
        string result = reason switch
        {
            1 => "완료",
            2 => "STOP",
            3 => "통신 정지",
            4 => "ADC/타이밍 오류",
            _ => "대기"
        };
        TestStateText.Text = $"{(running ? "시험 중" : result)} · {r.GetProperty("time_ms").GetUInt32()} ms · 듀티 {r.GetProperty("duty_permille").GetUInt32() / 10.0:0.0}% · ADC {r.GetProperty("adc_mean").GetUInt32()}";
        if (running)
            SetTestBusy(true);
        if (!running && testBusy)
        {
            // Keep START disabled until the completed trace has been retrieved.
            try
            {
                if (id != loadedTestId && r.GetProperty("count").GetUInt32() > 0)
                {
                    await ReadTestTrace(id);
                    loadedTestId = id;
                }
            }
            finally { SetTestBusy(false); }
            StatusText.Text = $"PWM 시험 {result} · ADC는 원시값이며 전류[A]로 보정되지 않았습니다.";
        }
    }
    static uint TestDuration(Settings s) => s.Squeeze + s.Up1 + s.Time1 + s.Down1 + s.Cool1 + s.Up2 + s.Time2 + s.Down2 + s.Cool2 + s.Up3 + s.Time3 + s.Down3;
    async Task ReadTestTrace(uint expectedId)
    {
        await commandLock.WaitAsync();
        try
        {
            if (writer is null)
                throw new IOException("연결되지 않았습니다.");
            await writer.WriteLineAsync("TEST TRACE");
            string[] header = (await ReadLine(3000) ?? "").Split(' ');
            if (header.Length != 3 || header[0] != "TRACE" || !uint.TryParse(header[1], out uint id) || id != expectedId || !int.TryParse(header[2], out int count) || count < 0 || count > 2048)
                throw new IOException("시험 기록 헤더 오류");
            var rows = new List<string>(count);
            for (int i = 0; i < count; i++)
            {
                string line = await ReadLine(3000) ?? throw new IOException("시험 기록 수신 중단");
                string[] fields = line.Split(',');
                if ((fields.Length != 6 && fields.Length != 9) || fields.Any(x => !uint.TryParse(x, out _)))
                    throw new IOException("시험 기록 형식 오류");
                rows.Add(line);
            }
            if (await ReadLine(3000) != "END")
                throw new IOException("시험 기록 종료 오류");
            testTrace.Clear();
            testTrace.AddRange(rows);
            adcWindow?.SetTrace(testTrace);
            TestWaveform.ClearMeasured();
            foreach (string line in rows)
            {
                var f = line.Split(',');
                TestWaveform.AddMeasured(uint.Parse(f[0]), uint.Parse(f[2]) / 10.0);
            }
            SaveTestTraceButton.IsEnabled = rows.Count > 0;
        }
        finally { commandLock.Release(); }
    }
    void SaveTestTrace_Click(object sender, RoutedEventArgs e)
    {
        var dialog = new SaveFileDialog { Filter = "CSV (*.csv)|*.csv", FileName = $"PWM-test-{DateTime.Now:yyyyMMdd-HHmmss}.csv" };
        if (dialog.ShowDialog(this) == true)
        {
            try
            {
                string header = "time_ms,profile_target_milli,commanded_phase_duty_permille,adc_mean,adc_min,adc_max";
                if (testTrace.Count > 0 && testTrace[0].Split(',').Length == 9)
                    header += ",primary_filtered_micro,secondary_adc,secondary_filtered_micro";
                File.WriteAllLines(dialog.FileName, new[] { header }.Concat(testTrace));
                StatusText.Text = "시험 기록 저장: " + dialog.FileName;
            }
            catch (Exception ex) { Error("저장 실패", ex.Message); }
        }
    }
    bool Connected()
    {
        if (timer.IsEnabled)
            return true;
        Error("연결 필요", "먼저 장비 또는 시뮬레이션에 연결하세요.");
        return false;
    }
    void SetProfileOperationEnabled(bool enabled)
    {
        SetProfileButton.IsEnabled = enabled;
        StartCycleButton.IsEnabled = enabled;
        SaveProfileButton.IsEnabled = enabled;
        LoadProfileButton.IsEnabled = enabled;
    }
    void SetConnected(bool on)
    {
        ConnectionText.Text = on ? (simulation ? "시뮬레이션 연결됨" : $"{IpAddressBox.Text}:{PortBox.Text} 연결됨") : "연결 안 됨";
        ConnectionLed.Fill = new SolidColorBrush(on ? Color.FromRgb(34, 197, 94) : Color.FromRgb(100, 116, 139));
        ConnectButton.IsEnabled = !on;
        DisconnectButton.IsEnabled = on;
        StopCycleButton.IsEnabled = on;
        CaptureScreenButton.IsEnabled = on && !simulation;
        SetProfileButton.IsEnabled = on;
        StartCycleButton.IsEnabled = on;
        SaveProfileButton.IsEnabled = on;
        LoadProfileButton.IsEnabled = on;
        IpAddressBox.IsEnabled = PortBox.IsEnabled = SimulationCheckBox.IsEnabled = !on;
    }
    void Error(string title, string text)
    {
        StatusText.Text = text;
        MessageBox.Show(this, text, title, MessageBoxButton.OK, MessageBoxImage.Warning);
    }
    static string F(double x) => x.ToString("0.##", CultureInfo.InvariantCulture);
    void Window_Closing(object? s, System.ComponentModel.CancelEventArgs e)
    {
        SaveWindowLayout();
        Disconnect();
    }
}
readonly record struct Settings(uint Squeeze, double Current1, uint Up1, uint Time1, uint Down1, uint Cool1, double Current2, uint Up2, uint Time2, uint Down2, uint Cool2, double Current3, uint Up3, uint Time3, uint Down3);
readonly record struct Status(bool Welding, bool Fault, ushort FaultCode, double Current, uint Time);
public sealed class StageRow(string name, double current, uint up, uint hold, uint down, uint cool)
{
    public string StageName { get; set; } = name;
    public double Current { get; set; } = current;
    public uint Up { get; set; } = up;
    public uint Hold { get; set; } = hold;
    public uint Down { get; set; } = down;
    public uint Cool { get; set; } = cool;
    public void Set(double c, uint u, uint h, uint d, uint z)
    {
        Current = c;
        Up = u;
        Hold = h;
        Down = d;
        Cool = z;
    }
    public void SetColumn(int col, double value)
    {
        if (value < 0)
            return;
        switch (col)
        {
            case 1:
                Current = value;
                break;
            case 2:
                Cool = (uint)value;
                break;
            case 3:
                Up = (uint)value;
                break;
            case 4:
                Hold = (uint)value;
                break;
            case 5:
                Down = (uint)value;
                break;
        }
    }
}
