using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using ProiectVentilatie.Mobile.Models;
using ProiectVentilatie.Mobile.Services;

namespace ProiectVentilatie.Mobile.ViewModels;

/// <summary>
/// Pagina Setări — sliders pentru thresholds și interval senzori.
/// Save diff-based: butonul e enabled DOAR dacă există modificări față
/// de ultima stare primită de la ESP32. La connect, valorile sunt
/// hidratate din state retained.
/// </summary>
public partial class SettingsViewModel : ObservableObject, IDisposable
{
    private readonly IMqttService _mqttService;
    private ConfigState? _lastReceivedConfig;

    // ── Sliders ──────────────────────────────────────
    [ObservableProperty] private float _tempThreshold = 45.0f;
    [ObservableProperty] private float _humThreshold = 60.0f;
    [ObservableProperty] private int _intervalSec = 300;
    [ObservableProperty] private float _tempHysteresis = 2.0f;
    [ObservableProperty] private float _humHysteresis = 5.0f;

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

        // Hidratare din ultima stare cached (ex: navigăm pe Settings după Dashboard)
        if (_mqttService.LastState != null)
        {
            OnStateReceived(_mqttService.LastState);
        }
    }

    private void OnStateReceived(VentilationState state)
    {
        _lastReceivedConfig = state.Config;

        // Doar dacă utilizatorul nu a modificat încă (HasChanges=false),
        // suprascriem cu valorile noi de la ESP32. Altfel ar pierde modificările pending.
        if (!HasChanges)
        {
            TempThreshold = state.Config.ThreshT;
            HumThreshold = state.Config.ThreshH;
            IntervalSec = state.Config.Interval;
            TempHysteresis = state.Config.HystT;
            HumHysteresis = state.Config.HystH;
        }

        IsLocked = state.Lock?.Owner == "blynk";
        RecomputeHasChanges();

        // După Save, dacă valorile s-au sincronizat → status feedback
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

    partial void OnTempThresholdChanged(float value)  => RecomputeHasChanges();
    partial void OnHumThresholdChanged(float value)   => RecomputeHasChanges();
    partial void OnIntervalSecChanged(int value)       => RecomputeHasChanges();
    partial void OnTempHysteresisChanged(float value) => RecomputeHasChanges();
    partial void OnHumHysteresisChanged(float value)  => RecomputeHasChanges();

    private void RecomputeHasChanges()
    {
        if (_lastReceivedConfig == null)
        {
            HasChanges = false;
        }
        else
        {
            HasChanges = Math.Abs(TempThreshold   - _lastReceivedConfig.ThreshT) > 0.01f
                      || Math.Abs(HumThreshold    - _lastReceivedConfig.ThreshH) > 0.01f
                      || IntervalSec              != _lastReceivedConfig.Interval
                      || Math.Abs(TempHysteresis  - _lastReceivedConfig.HystT)   > 0.01f
                      || Math.Abs(HumHysteresis   - _lastReceivedConfig.HystH)   > 0.01f;
        }
        SaveCommand.NotifyCanExecuteChanged();
    }

    private bool CanSave() => HasChanges && !IsLocked && IsConnected;

    [RelayCommand]
    private void SetInterval(int seconds)
    {
        IntervalSec = seconds;
    }

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
        await _mqttService.SendCommandAsync(new
        {
            cmd = "setConfig",
            threshT  = TempThreshold,
            threshH  = HumThreshold,
            interval = IntervalSec,
            hystT    = TempHysteresis,
            hystH    = HumHysteresis
        });
        StatusMessage = "Trimis. Aștept confirmare ESP32...";
        StatusColor = Colors.Orange;
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
