using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using ProiectVentilatie.Mobile.Models;
using ProiectVentilatie.Mobile.Services;

namespace ProiectVentilatie.Mobile.ViewModels;

public partial class SettingsViewModel : ObservableObject, IDisposable
{
    private readonly IMqttService _mqttService;
    private ConfigState? _lastReceivedConfig;

    // ── Thresholds ───────────────────────────────────
    [ObservableProperty] private float _tempThreshold = 45.0f;
    [ObservableProperty] private float _humThreshold = 60.0f;
    [ObservableProperty] private int _intervalSec = 300;
    [ObservableProperty] private float _tempHysteresis = 2.0f;
    [ObservableProperty] private float _humHysteresis = 5.0f;

    // ── LED intensitate ──────────────────────────────
    [ObservableProperty] private int _ledIntensity = 0;
    private int _lastReceivedLedIntensity = -1;

    // ── LED schedule (persistat în Preferences) ──────
    [ObservableProperty] private TimeSpan _ledOnTime  = new TimeSpan(18, 0, 0);
    [ObservableProperty] private TimeSpan _ledOffTime = new TimeSpan(23, 30, 0);
    [ObservableProperty] private int  _ledMaxIntensity    = 80;
    [ObservableProperty] private bool _ledScheduleEnabled = false;
    private bool _lastReceivedSchedEnabled = false;
    private bool _ledScheduleDirty = false;

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
    private const string PrefLedIntensity = "led_intensity";

    // ── State UI ─────────────────────────────────────
    [ObservableProperty] private bool _hasChanges;
    [ObservableProperty] private bool _isLocked;
    [ObservableProperty] private bool _isConnected;
    [ObservableProperty] private string _statusMessage = string.Empty;
    [ObservableProperty] private Color _statusColor = Colors.Gray;

    public SettingsViewModel(IMqttService mqttService)
    {
        _mqttService = mqttService;
        _mqttService.OnStateReceived += OnStateReceived;
        _mqttService.OnConnectionChanged += OnConnectionChanged;

        IsConnected = _mqttService.IsConnected;

        // Thresholds din Preferences — fallback până soseşte starea ESP32
        TempThreshold  = Preferences.Get(PrefThreshT,  45.0f);
        HumThreshold   = Preferences.Get(PrefThreshH,  60.0f);
        IntervalSec    = Preferences.Get(PrefInterval, 300);
        TempHysteresis = Preferences.Get(PrefHystT,    2.0f);
        HumHysteresis  = Preferences.Get(PrefHystH,    5.0f);

        // Schedule LED din Preferences
        LedOnTime  = new TimeSpan(Preferences.Get(PrefLedOnH, 18), Preferences.Get(PrefLedOnM, 0), 0);
        LedOffTime = new TimeSpan(Preferences.Get(PrefLedOffH, 23), Preferences.Get(PrefLedOffM, 30), 0);
        LedMaxIntensity = Preferences.Get(PrefLedMaxI, 80);
        LedIntensity = Preferences.Get(PrefLedIntensity, 0);

        // Resetează flag-ul declanșat accidental de încărcarea preferințelor
        _ledScheduleDirty = false;

        if (_mqttService.LastState != null)
            OnStateReceived(_mqttService.LastState);
    }

    private void OnStateReceived(VentilationState state)
    {
        _lastReceivedConfig = state.Config;

        // Thresholds: ESP32 câştigă dacă nu există modificări pending
        if (!HasChanges)
        {
            TempThreshold  = state.Config.ThreshT;
            HumThreshold   = state.Config.ThreshH;
            IntervalSec    = state.Config.Interval;
            TempHysteresis = state.Config.HystT;
            HumHysteresis  = state.Config.HystH;
        }

        // Salvează întotdeauna ultimele valori ESP32 în Preferences
        Preferences.Set(PrefThreshT,  state.Config.ThreshT);
        Preferences.Set(PrefThreshH,  state.Config.ThreshH);
        Preferences.Set(PrefInterval, state.Config.Interval);
        Preferences.Set(PrefHystT,    state.Config.HystT);
        Preferences.Set(PrefHystH,    state.Config.HystH);

        // LED intensitate
        if (state.Led != null)
        {
            _lastReceivedLedIntensity = state.Led.Intensity;
            _lastReceivedSchedEnabled = state.Led.SchedEnabled;
            
            if (!HasChanges)
            {
                // Decuplat intensivitatea de la MQTT. Se păstrează exclusiv din Preferences.
                LedScheduleEnabled = state.Led.SchedEnabled;
            }
        }

        IsLocked = state.Lock?.Owner == "blynk";
        RecomputeHasChanges();

        if (!HasChanges && !string.IsNullOrEmpty(StatusMessage) && StatusMessage.StartsWith("Trimis"))
        {
            StatusMessage = "✓ Setări confirmate de ESP32";
            StatusColor = Colors.LimeGreen;
        }
    }

    private void OnConnectionChanged(bool isConnected)
    {
        IsConnected = isConnected;
        SaveCommand.NotifyCanExecuteChanged();
    }

    // Threshold handlers
    partial void OnTempThresholdChanged(float value)  => RecomputeHasChanges();
    partial void OnHumThresholdChanged(float value)   => RecomputeHasChanges();
    partial void OnIntervalSecChanged(int value)       => RecomputeHasChanges();
    partial void OnTempHysteresisChanged(float value) => RecomputeHasChanges();
    partial void OnHumHysteresisChanged(float value)  => RecomputeHasChanges();

    // LED handlers
    private CancellationTokenSource? _ledDebounceCts;

    partial void OnLedIntensityChanged(int value)
    {
        Preferences.Set(PrefLedIntensity, value);

        // Anulează trimiterea anterioară dacă sliderul e încă în mișcare
        _ledDebounceCts?.Cancel();
        _ledDebounceCts = new CancellationTokenSource();
        var token = _ledDebounceCts.Token;

        Task.Run(async () =>
        {
            try
            {
                await Task.Delay(300, token); // Așteaptă 300ms să te oprești din slider
                if (!token.IsCancellationRequested && _mqttService.IsConnected && _lastReceivedLedIntensity >= 0 && value != _lastReceivedLedIntensity)
                {
                    await _mqttService.SendCommandAsync(new { cmd = "setLed", percent = value });
                    _lastReceivedLedIntensity = value;
                }
            }
            catch (TaskCanceledException) { }
        });

        RecomputeHasChanges();
    }

    partial void OnLedScheduleEnabledChanged(bool value)
    {
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

    private void RecomputeHasChanges()
    {
        bool threshChanged = _lastReceivedConfig != null && (
            Math.Abs(TempThreshold  - _lastReceivedConfig.ThreshT) > 0.01f ||
            Math.Abs(HumThreshold   - _lastReceivedConfig.ThreshH) > 0.01f ||
            IntervalSec             != _lastReceivedConfig.Interval         ||
            Math.Abs(TempHysteresis - _lastReceivedConfig.HystT)   > 0.01f ||
            Math.Abs(HumHysteresis  - _lastReceivedConfig.HystH)   > 0.01f);

        // Eliminat _lastReceivedLedIntensity pentru a decupla complet UI-ul
        bool ledChanged = (LedScheduleEnabled != _lastReceivedSchedEnabled)
                       || _ledScheduleDirty;

        HasChanges = threshChanged || ledChanged;
        SaveCommand.NotifyCanExecuteChanged();
    }

    private bool CanSave() => HasChanges && !IsLocked && IsConnected;

    [RelayCommand]
    private void SetInterval10() => IntervalSec = 10;

    [RelayCommand]
    private void SetInterval60() => IntervalSec = 60;

    [RelayCommand]
    private void SetInterval300() => IntervalSec = 300;

    [RelayCommand]
    private void SetInterval900() => IntervalSec = 900;

    [RelayCommand]
    private void SetInterval3600() => IntervalSec = 3600;

    [RelayCommand(CanExecute = nameof(CanSave))]
    private async Task SaveAsync()
    {
        StatusMessage = "Trimis. Aștept confirmare ESP32...";
        StatusColor = Colors.Orange;

        bool threshChanged = _lastReceivedConfig != null && (
            Math.Abs(TempThreshold  - _lastReceivedConfig.ThreshT) > 0.01f ||
            Math.Abs(HumThreshold   - _lastReceivedConfig.ThreshH) > 0.01f ||
            IntervalSec             != _lastReceivedConfig.Interval         ||
            Math.Abs(TempHysteresis - _lastReceivedConfig.HystT)   > 0.01f ||
            Math.Abs(HumHysteresis  - _lastReceivedConfig.HystH)   > 0.01f);

        if (threshChanged)
            await _mqttService.SendCommandAsync(new
            {
                cmd      = "setConfig",
                threshT  = TempThreshold,
                threshH  = HumThreshold,
                interval = IntervalSec,
                hystT    = TempHysteresis,
                hystH    = HumHysteresis
            });

        if (_ledScheduleDirty || LedScheduleEnabled != _lastReceivedSchedEnabled)
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
        StatusMessage = "Reset trimis. Aștept confirmare ESP32...";
        StatusColor = Colors.Orange;
    }

    public void Dispose()
    {
        _mqttService.OnStateReceived -= OnStateReceived;
        _mqttService.OnConnectionChanged -= OnConnectionChanged;
    }
}
