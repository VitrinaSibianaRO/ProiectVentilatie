using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using ProiectVentilatie.Mobile.Services;

namespace ProiectVentilatie.Mobile.ViewModels;

// Settings UI = sursa locala (Preferences). NU se mai sincronizeaza din MQTT state.
// Toate comenzile catre ESP32 (setConfig, setLed, setLedSchedule, reset) se trimit
// EXCLUSIV la apasarea butonului Save / Reset Defaults (user-triggered).
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

    // ── LED schedule (persistat în Preferences) ──────
    [ObservableProperty] private TimeSpan _ledOnTime  = new TimeSpan(18, 0, 0);
    [ObservableProperty] private TimeSpan _ledOffTime = new TimeSpan(23, 30, 0);
    [ObservableProperty] private int  _ledMaxIntensity    = 80;
    [ObservableProperty] private bool _ledScheduleEnabled = false;

    // Dirty flags — controleaza ce comenzi se trimit la Save
    private bool _threshDirty;
    private bool _ledIntensityDirty;
    private bool _ledScheduleDirty;

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
    private const string PrefLedSchedEn = "led_schedEn";
    private const string PrefLedIntensity = "led_intensity";

    // ── State UI ─────────────────────────────────────
    [ObservableProperty] private bool _hasChanges;
    [ObservableProperty] private bool _isConnected;
    // IsLocked: pastrat pentru binding XAML; Blynk eliminat, deci mereu false
    [ObservableProperty] private bool _isLocked;
    [ObservableProperty] private string _statusMessage = string.Empty;
    [ObservableProperty] private Color _statusColor = Colors.Gray;

    public SettingsViewModel(IMqttService mqttService)
    {
        _mqttService = mqttService;
        _mqttService.OnConnectionChanged += OnConnectionChanged;

        IsConnected = _mqttService.IsConnected;

        // Toate valorile UI sunt INCARCATE EXCLUSIV din Preferences locale.
        // Nu mai exista sincronizare automata cu ESP32 state.
        TempThreshold  = Preferences.Get(PrefThreshT,  45.0f);
        HumThreshold   = Preferences.Get(PrefThreshH,  60.0f);
        IntervalSec    = Preferences.Get(PrefInterval, 300);
        TempHysteresis = Preferences.Get(PrefHystT,    2.0f);
        HumHysteresis  = Preferences.Get(PrefHystH,    5.0f);

        LedOnTime  = new TimeSpan(Preferences.Get(PrefLedOnH, 18), Preferences.Get(PrefLedOnM, 0), 0);
        LedOffTime = new TimeSpan(Preferences.Get(PrefLedOffH, 23), Preferences.Get(PrefLedOffM, 30), 0);
        LedMaxIntensity    = Preferences.Get(PrefLedMaxI, 80);
        LedScheduleEnabled = Preferences.Get(PrefLedSchedEn, false);
        LedIntensity       = Preferences.Get(PrefLedIntensity, 0);

        // Property changed handler-ele au setat dirty flags la incarcarea Preferences — resetam.
        _threshDirty = false;
        _ledIntensityDirty = false;
        _ledScheduleDirty = false;
        HasChanges = false;
    }

    private void OnConnectionChanged(bool isConnected)
    {
        IsConnected = isConnected;
        SaveCommand.NotifyCanExecuteChanged();
    }

    // Threshold handlers — marcheaza dirty, NU salveaza in Preferences (commit-on-Save)
    partial void OnTempThresholdChanged(float value)  { _threshDirty = true; RecomputeHasChanges(); }
    partial void OnHumThresholdChanged(float value)   { _threshDirty = true; RecomputeHasChanges(); }
    partial void OnIntervalSecChanged(int value)      { _threshDirty = true; RecomputeHasChanges(); }
    partial void OnTempHysteresisChanged(float value) { _threshDirty = true; RecomputeHasChanges(); }
    partial void OnHumHysteresisChanged(float value)  { _threshDirty = true; RecomputeHasChanges(); }

    // LED intensity — salveaza imediat in Preferences (pastreaza pozitia sliderului la navigare),
    // dar trimite la ESP32 doar la Save
    partial void OnLedIntensityChanged(int value)
    {
        Preferences.Set(PrefLedIntensity, value);
        _ledIntensityDirty = true;
        RecomputeHasChanges();
    }

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

    private void RecomputeHasChanges()
    {
        HasChanges = _threshDirty || _ledIntensityDirty || _ledScheduleDirty;
        SaveCommand.NotifyCanExecuteChanged();
    }

    private bool CanSave() => HasChanges && IsConnected;

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

        // Reseteaza Preferences + UI locale doar pentru scope-ul cmd:reset (thresholds + interval + hysteresis)
        // LED schedule + intensity sunt independente de cmd:reset si raman neatinse.
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

    public void Dispose()
    {
        _mqttService.OnConnectionChanged -= OnConnectionChanged;
    }
}
