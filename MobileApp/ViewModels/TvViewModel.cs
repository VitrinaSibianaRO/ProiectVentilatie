using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using ProiectVentilatie.Mobile.Models;
using ProiectVentilatie.Mobile.Services;

namespace ProiectVentilatie.Mobile.ViewModels;

public partial class TvViewModel : ObservableObject, IDisposable
{
    private readonly IMqttService _mqttService;

    // ── Telemetrie (read-only, din MQTT) ─────────────────────────────────
    [ObservableProperty] private bool   _tvPower;
    [ObservableProperty] private int    _tvTemperatureC;
    [ObservableProperty] private bool   _tvHasSignal;
    [ObservableProperty] private long   _tvUsageHours;
    [ObservableProperty] private string _tvSerialNumber  = string.Empty;
    [ObservableProperty] private string _tvSoftwareVersion = string.Empty;
    [ObservableProperty] private bool   _tvReachable;

    // ── Static option lists for Dropdown binding ─────────────────────────
    public static readonly string[] InputOptions        = { "HDMI 1", "HDMI 2 / OPS", "DisplayPort" };
    public static readonly string[] PictureModeOptions  = { "Vivid", "Standard", "Cinema", "Sports", "Game", "Photos" };
    public static readonly string[] EnergySavingOptions = { "Dezactivat", "Minim", "Mediu", "Maxim", "Auto", "Ecran oprit" };

    // ── Control ───────────────────────────────────────────────────────────
    [ObservableProperty] private int    _tvVolume;
    [ObservableProperty] private bool   _tvMute;
    [ObservableProperty] private int    _tvInputIndex;      // 0=HDMI1, 1=HDMI2, 2=DP
    [ObservableProperty] private int    _tvBacklight = 100;
    [ObservableProperty] private int    _tvPictureModeIndex;
    [ObservableProperty] private int    _tvEnergySavingIndex;
    [ObservableProperty] private bool   _tvNoSignalPowerOff;

    // ── SelectedItem string properties for Dropdown (derived from index) ──
    public string? TvInputSelected
    {
        get => TvInputIndex >= 0 && TvInputIndex < InputOptions.Length ? InputOptions[TvInputIndex] : null;
        set { int i = value != null ? Array.IndexOf(InputOptions, value) : -1; if (i >= 0) TvInputIndex = i; }
    }
    public string? TvPictureModeSelected
    {
        get => TvPictureModeIndex >= 0 && TvPictureModeIndex < PictureModeOptions.Length ? PictureModeOptions[TvPictureModeIndex] : null;
        set { int i = value != null ? Array.IndexOf(PictureModeOptions, value) : -1; if (i >= 0) TvPictureModeIndex = i; }
    }
    public string? TvEnergySavingSelected
    {
        get => TvEnergySavingIndex >= 0 && TvEnergySavingIndex < EnergySavingOptions.Length ? EnergySavingOptions[TvEnergySavingIndex] : null;
        set { int i = value != null ? Array.IndexOf(EnergySavingOptions, value) : -1; if (i >= 0) TvEnergySavingIndex = i; }
    }

    private bool _applyingState;

    // ── Config (IP + MAC pentru WoL) ─────────────────────────────────────
    [ObservableProperty] private string _tvIpAddress  = string.Empty;
    [ObservableProperty] private string _tvMacAddress = string.Empty;

    // ── UI state ─────────────────────────────────────────────────────────
    [ObservableProperty] private bool   _isConnected;
    [ObservableProperty] private bool   _settingsExpanded;
    [ObservableProperty] private string _statusText = "Neconfigurat";

    // Codul hex al inputId (0x90=HDMI1, 0x91=HDMI2, 0xC0=DP)
    private static readonly int[] InputCodes = { 0x90, 0x91, 0xC0 };

    public bool IsTvOverheating => TvTemperatureC > 60;

    public TvViewModel(IMqttService mqttService)
    {
        _mqttService = mqttService;
        _mqttService.OnTvStateReceived  += OnTvStateReceived;
        _mqttService.OnConnectionChanged += OnConnectionChanged;

        _isConnected = _mqttService.IsConnected;
        if (_mqttService.LastTvState != null)
            ApplyTvState(_mqttService.LastTvState);

        // Incarca IP/MAC salvate local
        _tvIpAddress  = Preferences.Get("tv_ip",  string.Empty);
        _tvMacAddress = Preferences.Get("tv_mac", string.Empty);
    }

    private void OnConnectionChanged(bool connected)
    {
        IsConnected = connected;
    }

    private void OnTvStateReceived(TvState state)
    {
        ApplyTvState(state);
    }

    private void ApplyTvState(TvState s)
    {
        _applyingState = true;
        try
        {
            TvPower           = s.Power;
            TvTemperatureC    = s.TemperatureC;
            TvHasSignal       = s.HasSignal;
            TvUsageHours      = s.UsageHours;
            TvSerialNumber    = s.Serial;
            TvSoftwareVersion = s.SwVersion;
            TvReachable       = s.Reachable;
            TvVolume          = s.Volume;
            TvMute            = s.Muted;
            TvBacklight       = s.Backlight;
            TvPictureModeIndex  = s.PictureMode;
            TvEnergySavingIndex = s.EnergySaving;
            TvNoSignalPowerOff  = s.NoSignalPowerOff;

            TvInputIndex = s.InputId switch
            {
                0x90 => 0,
                0x91 => 1,
                0xC0 => 2,
                _    => 0
            };
        }
        finally
        {
            _applyingState = false;
        }

        StatusText = s.Reachable
            ? (s.Power ? "Pornit" : "Standby")
            : "Inaccesibil";

        OnPropertyChanged(nameof(IsTvOverheating));
    }

    partial void OnTvInputIndexChanged(int value)
    {
        OnPropertyChanged(nameof(TvInputSelected));
        System.Diagnostics.Debug.WriteLine($"[TvVM] InputIndex={value} applyingState={_applyingState}");
        if (!_applyingState) _ = SetInputAsync();
    }

    partial void OnTvPictureModeIndexChanged(int value)
    {
        OnPropertyChanged(nameof(TvPictureModeSelected));
        System.Diagnostics.Debug.WriteLine($"[TvVM] PictureModeIndex={value} applyingState={_applyingState}");
        if (!_applyingState) _ = SetPictureModeAsync();
    }

    partial void OnTvEnergySavingIndexChanged(int value)
    {
        OnPropertyChanged(nameof(TvEnergySavingSelected));
        System.Diagnostics.Debug.WriteLine($"[TvVM] EnergySavingIndex={value} applyingState={_applyingState}");
        if (!_applyingState) _ = SetEnergySavingAsync();
    }

    partial void OnTvTemperatureCChanged(int value)
    {
        OnPropertyChanged(nameof(IsTvOverheating));
    }

    // ── Comenzi control ──────────────────────────────────────────────────

    [RelayCommand]
    private async Task PowerOnAsync()
    {
        System.Diagnostics.Debug.WriteLine("[TvVM] CMD power_on");
        await _mqttService.SendCommandAsync(new { cmd = "setTv", action = "power_on" });
    }

    [RelayCommand]
    private async Task PowerOffAsync()
    {
        System.Diagnostics.Debug.WriteLine("[TvVM] CMD power_off");
        await _mqttService.SendCommandAsync(new { cmd = "setTv", action = "power_off" });
    }

    [RelayCommand]
    private async Task SetVolumeAsync()
    {
        System.Diagnostics.Debug.WriteLine($"[TvVM] CMD volume={TvVolume}");
        await _mqttService.SendCommandAsync(new { cmd = "setTv", action = "volume", value = TvVolume });
    }

    [RelayCommand]
    private async Task ToggleMuteAsync()
    {
        System.Diagnostics.Debug.WriteLine($"[TvVM] CMD mute={TvMute}");
        await _mqttService.SendCommandAsync(new { cmd = "setTv", action = "mute", value = TvMute ? 1 : 0 });
    }

    [RelayCommand]
    private async Task SetInputAsync()
    {
        int code = TvInputIndex >= 0 && TvInputIndex < InputCodes.Length
            ? InputCodes[TvInputIndex]
            : InputCodes[0];
        await _mqttService.SendCommandAsync(new { cmd = "setTv", action = "input", value = code });
    }

    [RelayCommand]
    private async Task SetBacklightAsync()
    {
        await _mqttService.SendCommandAsync(new { cmd = "setTv", action = "backlight", value = TvBacklight });
    }

    [RelayCommand]
    private async Task SetPictureModeAsync()
    {
        await _mqttService.SendCommandAsync(new { cmd = "setTv", action = "pictureMode", value = TvPictureModeIndex });
    }

    [RelayCommand]
    private void PrevPictureMode()
    {
        int v = TvPictureModeIndex - 1;
        if (v < 0) v = PictureModeOptions.Length - 1;
        TvPictureModeIndex = v;
    }

    [RelayCommand]
    private void NextPictureMode()
    {
        int v = TvPictureModeIndex + 1;
        if (v >= PictureModeOptions.Length) v = 0;
        TvPictureModeIndex = v;
    }

    [RelayCommand]
    private async Task SetEnergySavingAsync()
    {
        await _mqttService.SendCommandAsync(new { cmd = "setTv", action = "energySaving", value = TvEnergySavingIndex });
    }

    [RelayCommand]
    private void PrevEnergySaving()
    {
        int v = TvEnergySavingIndex - 1;
        if (v < 0) v = EnergySavingOptions.Length - 1;
        TvEnergySavingIndex = v;
    }

    [RelayCommand]
    private void NextEnergySaving()
    {
        int v = TvEnergySavingIndex + 1;
        if (v >= EnergySavingOptions.Length) v = 0;
        TvEnergySavingIndex = v;
    }

    [RelayCommand]
    private async Task SetNoSignalPowerOffAsync()
    {
        await _mqttService.SendCommandAsync(new { cmd = "setTv", action = "noSignalOff", value = TvNoSignalPowerOff ? 1 : 0 });
    }

    // ── Configurare TV (IP + MAC) ─────────────────────────────────────────

    [RelayCommand]
    private async Task SaveTvConfigAsync()
    {
        if (string.IsNullOrWhiteSpace(TvIpAddress) || string.IsNullOrWhiteSpace(TvMacAddress))
            return;

        Preferences.Set("tv_ip",  TvIpAddress.Trim());
        Preferences.Set("tv_mac", TvMacAddress.Trim());

        await _mqttService.SendCommandAsync(new
        {
            cmd = "setTvConfig",
            ip  = TvIpAddress.Trim(),
            mac = TvMacAddress.Trim()
        });
    }

    [RelayCommand]
    private void ToggleSettings()
    {
        SettingsExpanded = !SettingsExpanded;
    }

    public void Dispose()
    {
        _mqttService.OnTvStateReceived   -= OnTvStateReceived;
        _mqttService.OnConnectionChanged -= OnConnectionChanged;
    }
}
