using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using ProiectVentilatie.Mobile.Models;
using ProiectVentilatie.Mobile.Services;

namespace ProiectVentilatie.Mobile.ViewModels;

// Settings UI = sursa locala (Preferences). NU se mai sincronizeaza din MQTT state.
// Toate comenzile catre ESP32 (setConfig, setLed, setLedSchedule, setLedMode, reset)
// se trimit EXCLUSIV la apasarea butonului Save / Reset Defaults (user-triggered).
public partial class SettingsViewModel : ObservableObject, IDisposable
{
    private readonly IMqttService _mqttService;

    // ── Thresholds ───────────────────────────────────
    [ObservableProperty] private float _tempThreshold = 45.0f;
    [ObservableProperty] private float _humThreshold = 60.0f;
    [ObservableProperty] private int _intervalSec = 300;
    [ObservableProperty] private float _tempHysteresis = 2.0f;
    [ObservableProperty] private float _humHysteresis = 5.0f;

    // ── LED intensitate ──────────────────────────────
    [ObservableProperty] private int _ledIntensity = 0;

    // ── LED schedule ─────────────────────────────────
    [ObservableProperty] private TimeSpan _ledOnTime  = new TimeSpan(18, 0, 0);
    [ObservableProperty] private TimeSpan _ledOffTime = new TimeSpan(23, 30, 0);
    [ObservableProperty] private int  _ledMaxIntensity    = 80;
    [ObservableProperty] private bool _ledScheduleEnabled = false;

    // ── LED pattern mode ─────────────────────────────
    [ObservableProperty] private LedPattern _ledMode = LedPattern.Steady;

    // Breathing / Triangle / Sawtooth
    [ObservableProperty] private int _breathingMin = 10;
    [ObservableProperty] private int _breathingDur = 4;
    [ObservableProperty] private int _triangleMin = 10;
    [ObservableProperty] private int _triangleDur = 4;
    [ObservableProperty] private int _sawtoothMin = 10;
    [ObservableProperty] private int _sawtoothDur = 4;
    [ObservableProperty] private bool _sawtoothDirection = false;  // false=rise/snap, true=snap/fall

    // Strobe
    [ObservableProperty] private int _strobeDuty = 50;
    [ObservableProperty] private double _strobeFreq = 2.0;  // Hz

    // Heartbeat
    [ObservableProperty] private int _heartbeatBpm = 65;
    [ObservableProperty] private int _heartbeatIntensity = 80;

    // Candle
    [ObservableProperty] private int _candleVariation = 50;

    // Lightning
    [ObservableProperty] private int _lightningFreq = 5;
    [ObservableProperty] private int _lightningBaseline = 20;

    // SOS Morse
    [ObservableProperty] private int _morseDitMs = 200;

    // Sunrise / Sunset
    [ObservableProperty] private int _sunriseDurMin = 30;
    [ObservableProperty] private int _sunriseFinal = 100;
    [ObservableProperty] private int _sunsetDurMin = 30;
    [ObservableProperty] private int _sunsetStart = 100;

    // Random
    [ObservableProperty] private int _randomMin = 10;
    [ObservableProperty] private int _randomMax = 100;
    [ObservableProperty] private int _randomSpeed = 50;

    // ── Collapse state ───────────────────────────────
    [ObservableProperty] private bool _thresholdsExpanded = false;
    [ObservableProperty] private bool _ledExpanded = false;

    // ── State UI ─────────────────────────────────────
    [ObservableProperty] private bool _hasChanges;
    [ObservableProperty] private bool _isConnected;
    [ObservableProperty] private bool _isLocked;
    [ObservableProperty] private string _statusMessage = string.Empty;
    [ObservableProperty] private Color _statusColor = Colors.Gray;

    // Strobe safety computed property
    public bool IsStrobeFreqDangerous => StrobeFreq > 3.0;

    // Picker source
    public LedPattern[] AvailableModes => LedPatternInfo.All;

    // Dirty flags
    private bool _threshDirty;
    private bool _ledIntensityDirty;
    private bool _ledScheduleDirty;
    private bool _ledModeDirty;

    // ── Preference keys ──────────────────────────────
    private const string PrefThreshT  = "cfg_threshT";
    private const string PrefThreshH  = "cfg_threshH";
    private const string PrefInterval = "cfg_interval";
    private const string PrefHystT    = "cfg_hystT";
    private const string PrefHystH    = "cfg_hystH";
    private const string PrefLedOnH   = "led_onH";
    private const string PrefLedOnM   = "led_onM";
    private const string PrefLedOffH  = "led_offH";
    private const string PrefLedOffM  = "led_offM";
    private const string PrefLedMaxI  = "led_maxI";
    private const string PrefLedSchedEn    = "led_schedEn";
    private const string PrefLedIntensity  = "led_intensity";
    private const string PrefLedMode       = "led_mode";
    private const string PrefThresholdsExp = "ui_threshExp";
    private const string PrefLedExp        = "ui_ledExp";

    public SettingsViewModel(IMqttService mqttService)
    {
        _mqttService = mqttService;
        _mqttService.OnConnectionChanged += OnConnectionChanged;

        IsConnected = _mqttService.IsConnected;

        // Thresholds from Preferences
        TempThreshold  = Preferences.Get(PrefThreshT,  45.0f);
        HumThreshold   = Preferences.Get(PrefThreshH,  60.0f);
        IntervalSec    = Preferences.Get(PrefInterval, 300);
        TempHysteresis = Preferences.Get(PrefHystT,    2.0f);
        HumHysteresis  = Preferences.Get(PrefHystH,    5.0f);

        // Schedule from Preferences
        LedOnTime  = new TimeSpan(Preferences.Get(PrefLedOnH, 18), Preferences.Get(PrefLedOnM, 0), 0);
        LedOffTime = new TimeSpan(Preferences.Get(PrefLedOffH, 23), Preferences.Get(PrefLedOffM, 30), 0);
        LedMaxIntensity    = Preferences.Get(PrefLedMaxI, 80);
        LedScheduleEnabled = Preferences.Get(PrefLedSchedEn, false);
        LedIntensity       = Preferences.Get(PrefLedIntensity, 0);

        // Pattern mode from Preferences
        LedMode = (LedPattern)Preferences.Get(PrefLedMode, (int)LedPattern.Steady);
        _loadPatternParams();

        // Collapse state from Preferences
        ThresholdsExpanded = Preferences.Get(PrefThresholdsExp, false);
        LedExpanded        = Preferences.Get(PrefLedExp, false);

        // Reset dirty flags after loading
        _threshDirty = false;
        _ledIntensityDirty = false;
        _ledScheduleDirty = false;
        _ledModeDirty = false;
        HasChanges = false;
    }

    private void OnConnectionChanged(bool isConnected)
    {
        IsConnected = isConnected;
        SaveCommand.NotifyCanExecuteChanged();
    }

    // ── Threshold handlers ───────────────────────────
    partial void OnTempThresholdChanged(float value)  { _threshDirty = true; RecomputeHasChanges(); }
    partial void OnHumThresholdChanged(float value)   { _threshDirty = true; RecomputeHasChanges(); }
    partial void OnIntervalSecChanged(int value)      { _threshDirty = true; RecomputeHasChanges(); }
    partial void OnTempHysteresisChanged(float value) { _threshDirty = true; RecomputeHasChanges(); }
    partial void OnHumHysteresisChanged(float value)  { _threshDirty = true; RecomputeHasChanges(); }

    // ── LED intensity ────────────────────────────────
    partial void OnLedIntensityChanged(int value)
    {
        Preferences.Set(PrefLedIntensity, value);
        _ledIntensityDirty = true;
        RecomputeHasChanges();
    }

    // ── Schedule handlers ────────────────────────────
    partial void OnLedScheduleEnabledChanged(bool value)
    {
        Preferences.Set(PrefLedSchedEn, value);
        _ledScheduleDirty = true;
        RecomputeHasChanges();
    }

    partial void OnLedOnTimeChanged(TimeSpan value)
    {
        Preferences.Set(PrefLedOnH, value.Hours);
        Preferences.Set(PrefLedOnM, value.Minutes);
        _ledScheduleDirty = true;
        RecomputeHasChanges();
    }

    partial void OnLedOffTimeChanged(TimeSpan value)
    {
        Preferences.Set(PrefLedOffH, value.Hours);
        Preferences.Set(PrefLedOffM, value.Minutes);
        _ledScheduleDirty = true;
        RecomputeHasChanges();
    }

    partial void OnLedMaxIntensityChanged(int value)
    {
        Preferences.Set(PrefLedMaxI, value);
        _ledScheduleDirty = true;
        RecomputeHasChanges();
    }

    // ── Pattern mode handler ─────────────────────────
    partial void OnLedModeChanged(LedPattern value)
    {
        Preferences.Set(PrefLedMode, (int)value);
        _ledModeDirty = true;
        RecomputeHasChanges();
    }

    // ── Pattern param handlers (auto-persist) ────────
    partial void OnBreathingMinChanged(int v)    { Preferences.Set("led_p1_p1", v); _ledModeDirty = true; RecomputeHasChanges(); }
    partial void OnBreathingDurChanged(int v)    { Preferences.Set("led_p1_p2", v); _ledModeDirty = true; RecomputeHasChanges(); }
    partial void OnTriangleMinChanged(int v)     { Preferences.Set("led_p2_p1", v); _ledModeDirty = true; RecomputeHasChanges(); }
    partial void OnTriangleDurChanged(int v)     { Preferences.Set("led_p2_p2", v); _ledModeDirty = true; RecomputeHasChanges(); }
    partial void OnSawtoothMinChanged(int v)     { Preferences.Set("led_p3_p1", v); _ledModeDirty = true; RecomputeHasChanges(); }
    partial void OnSawtoothDurChanged(int v)     { Preferences.Set("led_p3_p2", v); _ledModeDirty = true; RecomputeHasChanges(); }
    partial void OnSawtoothDirectionChanged(bool v){ Preferences.Set("led_p3_p3", v ? 1 : 0); _ledModeDirty = true; RecomputeHasChanges(); }
    partial void OnStrobeDutyChanged(int v)      { Preferences.Set("led_p4_p1", v); _ledModeDirty = true; RecomputeHasChanges(); }
    partial void OnStrobeFreqChanged(double v)
    {
        Preferences.Set("led_p4_p2", (int)(v * 10));
        _ledModeDirty = true;
        RecomputeHasChanges();
        OnPropertyChanged(nameof(IsStrobeFreqDangerous));
    }
    partial void OnHeartbeatBpmChanged(int v)        { Preferences.Set("led_p5_p1", v); _ledModeDirty = true; RecomputeHasChanges(); }
    partial void OnHeartbeatIntensityChanged(int v)  { Preferences.Set("led_p5_p2", v); _ledModeDirty = true; RecomputeHasChanges(); }
    partial void OnCandleVariationChanged(int v)     { Preferences.Set("led_p6_p1", v); _ledModeDirty = true; RecomputeHasChanges(); }
    partial void OnLightningFreqChanged(int v)       { Preferences.Set("led_p7_p1", v); _ledModeDirty = true; RecomputeHasChanges(); }
    partial void OnLightningBaselineChanged(int v)   { Preferences.Set("led_p7_p2", v); _ledModeDirty = true; RecomputeHasChanges(); }
    partial void OnMorseDitMsChanged(int v)          { Preferences.Set("led_p8_p1", v); _ledModeDirty = true; RecomputeHasChanges(); }
    partial void OnSunriseDurMinChanged(int v)       { Preferences.Set("led_p9_p1", v); _ledModeDirty = true; RecomputeHasChanges(); }
    partial void OnSunriseFinalChanged(int v)        { Preferences.Set("led_p9_p2", v); _ledModeDirty = true; RecomputeHasChanges(); }
    partial void OnSunsetDurMinChanged(int v)        { Preferences.Set("led_p10_p1", v); _ledModeDirty = true; RecomputeHasChanges(); }
    partial void OnSunsetStartChanged(int v)         { Preferences.Set("led_p10_p2", v); _ledModeDirty = true; RecomputeHasChanges(); }
    partial void OnRandomMinChanged(int v)           { Preferences.Set("led_p11_p1", v); _ledModeDirty = true; RecomputeHasChanges(); }
    partial void OnRandomMaxChanged(int v)           { Preferences.Set("led_p11_p2", v); _ledModeDirty = true; RecomputeHasChanges(); }
    partial void OnRandomSpeedChanged(int v)         { Preferences.Set("led_p11_p3", v); _ledModeDirty = true; RecomputeHasChanges(); }

    // ── Collapse state ───────────────────────────────
    partial void OnThresholdsExpandedChanged(bool value) => Preferences.Set(PrefThresholdsExp, value);
    partial void OnLedExpandedChanged(bool value)        => Preferences.Set(PrefLedExp, value);

    private void RecomputeHasChanges()
    {
        HasChanges = _threshDirty || _ledIntensityDirty || _ledScheduleDirty || _ledModeDirty;
        SaveCommand.NotifyCanExecuteChanged();
    }

    private bool CanSave() => HasChanges && IsConnected;

    // ── Interval presets ─────────────────────────────
    [RelayCommand] private void SetInterval10()   => IntervalSec = 10;
    [RelayCommand] private void SetInterval60()   => IntervalSec = 60;
    [RelayCommand] private void SetInterval300()  => IntervalSec = 300;
    [RelayCommand] private void SetInterval900()  => IntervalSec = 900;
    [RelayCommand] private void SetInterval3600() => IntervalSec = 3600;

    [RelayCommand(CanExecute = nameof(CanSave))]
    private async Task SaveAsync()
    {
        StatusMessage = "Trimit...";
        StatusColor = Colors.Orange;

        if (_threshDirty)
        {
            Preferences.Set(PrefThreshT,  TempThreshold);
            Preferences.Set(PrefThreshH,  HumThreshold);
            Preferences.Set(PrefInterval, IntervalSec);
            Preferences.Set(PrefHystT,    TempHysteresis);
            Preferences.Set(PrefHystH,    HumHysteresis);

            await _mqttService.SendCommandAsync(new
            {
                cmd      = "setConfig",
                threshT  = TempThreshold,
                threshH  = HumThreshold,
                interval = IntervalSec,
                hystT    = TempHysteresis,
                hystH    = HumHysteresis
            });
            _threshDirty = false;
        }

        if (_ledScheduleDirty)
        {
            await _mqttService.SendCommandAsync(new
            {
                cmd     = "setLedSchedule",
                onH     = LedOnTime.Hours,
                onM     = LedOnTime.Minutes,
                offH    = LedOffTime.Hours,
                offM    = LedOffTime.Minutes,
                maxI    = LedMaxIntensity,
                enabled = LedScheduleEnabled
            });
            _ledScheduleDirty = false;
        }

        if (_ledIntensityDirty)
        {
            await _mqttService.SendCommandAsync(new { cmd = "setLed", percent = LedIntensity });
            _ledIntensityDirty = false;
        }

        if (_ledModeDirty)
        {
            var (id, p1, p2, p3, p4) = LedModeToParams();
            await _mqttService.SendCommandAsync(new
            {
                cmd  = "setLedMode",
                mode = id,
                p1, p2, p3, p4
            });
            _ledModeDirty = false;
        }

        RecomputeHasChanges();
        StatusMessage = "✓ Trimis";
        StatusColor = Colors.LimeGreen;
    }

    [RelayCommand]
    private async Task ResetDefaultsAsync()
    {
        var page = Application.Current?.Windows[0]?.Page;
        if (page == null) return;

        var confirm = await page.DisplayAlertAsync(
            "Reset la valori implicite",
            "Se vor restaura valorile default (T≥45°C, H≥60%, Interval=300s) și override-urile vor fi șterse. Continui?",
            "Da, reset",
            "Anulează");

        if (!confirm) return;

        await _mqttService.SendCommandAsync(new { cmd = "reset" });

        Preferences.Remove(PrefThreshT);
        Preferences.Remove(PrefThreshH);
        Preferences.Remove(PrefInterval);
        Preferences.Remove(PrefHystT);
        Preferences.Remove(PrefHystH);

        TempThreshold  = 45.0f;
        HumThreshold   = 60.0f;
        IntervalSec    = 300;
        TempHysteresis = 2.0f;
        HumHysteresis  = 5.0f;

        _threshDirty = false;
        RecomputeHasChanges();

        StatusMessage = "✓ Reset trimis. Valorile locale restaurate.";
        StatusColor = Colors.LimeGreen;
    }

    // ── Helpers ──────────────────────────────────────

    private (int id, int p1, int p2, int p3, int p4) LedModeToParams() => LedMode switch
    {
        LedPattern.Steady    => ((int)LedMode, 0, 0, 0, 0),
        LedPattern.Breathing => ((int)LedMode, BreathingMin, BreathingDur, 0, 0),
        LedPattern.Triangle  => ((int)LedMode, TriangleMin,  TriangleDur,  0, 0),
        LedPattern.Sawtooth  => ((int)LedMode, SawtoothMin,  SawtoothDur,  SawtoothDirection ? 1 : 0, 0),
        LedPattern.Strobe    => ((int)LedMode, StrobeDuty,   (int)(StrobeFreq * 10), 0, 0),
        LedPattern.Heartbeat => ((int)LedMode, HeartbeatBpm, HeartbeatIntensity, 0, 0),
        LedPattern.Candle    => ((int)LedMode, CandleVariation, 0, 0, 0),
        LedPattern.Lightning => ((int)LedMode, LightningFreq,   LightningBaseline, 0, 0),
        LedPattern.SosMorse  => ((int)LedMode, MorseDitMs, 0, 0, 0),
        LedPattern.Sunrise   => ((int)LedMode, SunriseDurMin, SunriseFinal, 0, 0),
        LedPattern.Sunset    => ((int)LedMode, SunsetDurMin,  SunsetStart,  0, 0),
        LedPattern.Random    => ((int)LedMode, RandomMin, RandomMax, RandomSpeed, 0),
        _                    => ((int)LedMode, 0, 0, 0, 0)
    };

    private void _loadPatternParams()
    {
        BreathingMin  = Preferences.Get("led_p1_p1", 10);
        BreathingDur  = Preferences.Get("led_p1_p2", 4);
        TriangleMin   = Preferences.Get("led_p2_p1", 10);
        TriangleDur   = Preferences.Get("led_p2_p2", 4);
        SawtoothMin   = Preferences.Get("led_p3_p1", 10);
        SawtoothDur   = Preferences.Get("led_p3_p2", 4);
        SawtoothDirection = Preferences.Get("led_p3_p3", 0) != 0;
        StrobeDuty    = Preferences.Get("led_p4_p1", 50);
        StrobeFreq    = Preferences.Get("led_p4_p2", 20) / 10.0;
        HeartbeatBpm       = Preferences.Get("led_p5_p1", 65);
        HeartbeatIntensity = Preferences.Get("led_p5_p2", 80);
        CandleVariation    = Preferences.Get("led_p6_p1", 50);
        LightningFreq      = Preferences.Get("led_p7_p1", 5);
        LightningBaseline  = Preferences.Get("led_p7_p2", 20);
        MorseDitMs         = Preferences.Get("led_p8_p1", 200);
        SunriseDurMin      = Preferences.Get("led_p9_p1", 30);
        SunriseFinal       = Preferences.Get("led_p9_p2", 100);
        SunsetDurMin       = Preferences.Get("led_p10_p1", 30);
        SunsetStart        = Preferences.Get("led_p10_p2", 100);
        RandomMin          = Preferences.Get("led_p11_p1", 10);
        RandomMax          = Preferences.Get("led_p11_p2", 100);
        RandomSpeed        = Preferences.Get("led_p11_p3", 50);
    }

    public void Dispose()
    {
        _mqttService.OnConnectionChanged -= OnConnectionChanged;
    }
}
