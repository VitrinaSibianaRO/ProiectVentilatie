using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using ProiectVentilatie.Mobile.Models;
using ProiectVentilatie.Mobile.Services;

namespace ProiectVentilatie.Mobile.ViewModels;

public partial class DashboardViewModel : ObservableObject, IDisposable
{
    private readonly IMqttService _mqttService;
    private IDispatcherTimer? _agoTimer;
    private DateTime _lastUpdateTime;

    // ── Senzori ──────────────────────────────────────
    [ObservableProperty] private float _leftTemp;
    [ObservableProperty] private float _leftHum;
    [ObservableProperty] private float _rightTemp;
    [ObservableProperty] private float _rightHum;

    // ── Relee + Override ─────────────────────────────
    [ObservableProperty] private bool _leftRelay;
    [ObservableProperty] private bool _rightRelay;
    [ObservableProperty] private bool _leftOverride;
    [ObservableProperty] private bool _rightOverride;

    [ObservableProperty] private string _relayLeftText = "OPRIT";
    [ObservableProperty] private string _relayRightText = "OPRIT";
    [ObservableProperty] private string _overrideLeftText = "Auto";
    [ObservableProperty] private string _overrideRightText = "Auto";

    // Aliases for MiniFanView.IsOn binding
    public bool LeftRelayActive => LeftRelay;
    public bool RightRelayActive => RightRelay;

    // Formatted uptime for hero banner
    [ObservableProperty] private string _uptimeText = "—";

    // ── Conexiune + Online ───────────────────────────
    [ObservableProperty] private string _connectionStatus = "Deconectat";
    [ObservableProperty] private bool _isOnline;
    [ObservableProperty] private Color _onlineBadgeColor = Colors.Red;
    [ObservableProperty] private string _onlineText = "OFFLINE";

    // ── Lock ─────────────────────────────────────────
    [ObservableProperty] private bool _lockBannerVisible;
    [ObservableProperty] private string _lockBannerText = string.Empty;
    [ObservableProperty] private bool _isControlEnabled = true;

    // ── Failsafe + Reboot warning ─────────────────────
    [ObservableProperty] private bool _rightFailsafe;
    [ObservableProperty] private bool _rebootWarningVisible;
    [ObservableProperty] private string _rebootWarningText = string.Empty;

    // ── Ago ──────────────────────────────────────────
    [ObservableProperty] private string _lastUpdateText = "Niciodată";

    // ── Sistem ───────────────────────────────────────
    [ObservableProperty] private int _fwBuild;
    [ObservableProperty] private long _uptimeSec;
    [ObservableProperty] private int _heapBytes;
    [ObservableProperty] private string _ledIntensityText = "—";

    // ── TV telemetrie ────────────────────────────────
    [ObservableProperty] private string _tvTempText = "—";
    [ObservableProperty] private string _tvUsageText = "—";
    [ObservableProperty] private string _tvReachableText = "—";
    [ObservableProperty] private Color  _tvReachableColor = Colors.Gray;
    [ObservableProperty] private string _tvPowerText = "—";
    [ObservableProperty] private Color  _tvPowerColor = Colors.Gray;
    [ObservableProperty] private bool _tvDataVisible = true;

    public DashboardViewModel(IMqttService mqttService)
    {
        _mqttService = mqttService;
        _mqttService.OnStateReceived += UpdateState;
        _mqttService.OnConnectionChanged += UpdateConnection;
        _mqttService.OnOnlineStatusChanged += UpdateOnlineStatus;
        _mqttService.OnTvStateReceived += UpdateTvState;

        // Timer periodic 10s pentru actualizare "ago" text
        try
        {
            var dispatcher = Application.Current?.Dispatcher;
            if (dispatcher != null)
            {
                _agoTimer = dispatcher.CreateTimer();
                _agoTimer.Interval = TimeSpan.FromSeconds(10);
                _agoTimer.Tick += (s, e) => UpdateAgoText();
                _agoTimer.Start();
            }
        }
        catch (Exception ex)
        {
            System.Diagnostics.Debug.WriteLine($"[Dashboard] Timer init failed: {ex}");
        }

        _ = ConnectAsync();

        // Aplica ultimul state cunoscut imediat (evita flash de valori default la creare VM)
        if (_mqttService.LastState != null)
            UpdateState(_mqttService.LastState);
        if (_mqttService.LastTvState != null)
            UpdateTvState(_mqttService.LastTvState);
    }

    private async Task ConnectAsync()
    {
        try
        {
            ConnectionStatus = "Conectare...";
            await _mqttService.ConnectAsync();
        }
        catch (Exception ex)
        {
            ConnectionStatus = "Eroare conexiune";
            System.Diagnostics.Debug.WriteLine($"[Dashboard] ConnectAsync failed: {ex}");
        }
    }

    private void UpdateConnection(bool isConnected)
    {
        ConnectionStatus = isConnected ? "Conectat la HiveMQ" : "Deconectat";
        if (isConnected)
        {
            // Cerinta arhitecturala: la pornire/reconectare cerem ESP32 sa publice state fresh.
            // Noul payload contine doar senzori + system info (NU config), deci e efectiv "getSensors".
            _ = _mqttService.SendCommandAsync(new { cmd = "refresh" });
        }
    }

    private void UpdateOnlineStatus(string status)
    {
        IsOnline = status == "online";
        OnlineBadgeColor = IsOnline ? Colors.LimeGreen : Colors.Red;
        OnlineText = IsOnline ? "ESP32 ONLINE" : "ESP32 OFFLINE";
    }

    private void UpdateState(VentilationState state)
    {
        // Senzori
        LeftTemp = state.Left.Temp;
        LeftHum = state.Left.Hum;
        RightTemp = state.Right.Temp;
        RightHum = state.Right.Hum;

        // Relee
        LeftRelay = state.Left.Relay;
        RightRelay = state.Right.Relay;
        RelayLeftText = state.Left.Relay ? "PORNIT" : "OPRIT";
        RightFailsafe = state.Right.Failsafe;
        RelayRightText = state.Right.Failsafe ? "FAILSAFE"
                       : (state.Right.Relay ? "PORNIT" : "OPRIT");
        OnPropertyChanged(nameof(LeftRelayActive));
        OnPropertyChanged(nameof(RightRelayActive));

        // Reboot warning
        RebootWarningVisible = state.UptimeSec < 120;
        RebootWarningText = state.UptimeSec < 120
            ? $"ESP32 a rebootat recent ({state.UptimeSec}s). Pragurile pot fi la valorile implicite."
            : string.Empty;

        // Override
        LeftOverride = state.Left.Override;
        RightOverride = state.Right.Override;
        OverrideLeftText = state.Left.Override ? "Manual ON" : "Auto";
        OverrideRightText = state.Right.Override ? "Manual ON" : "Auto";

        // Uptime formatted
        UptimeText = FormatUptime(state.UptimeSec);

        // Lock
        if (state.Lock != null && !string.IsNullOrEmpty(state.Lock.Owner))
        {
            LockBannerVisible = true;
            var ownerDisplay = state.Lock.Owner == "blynk" ? "Blynk" : "MQTT";
            LockBannerText = $"🔒 Control blocat ({ownerDisplay} activ)";
            IsControlEnabled = state.Lock.Owner != "blynk";
        }
        else
        {
            LockBannerVisible = false;
            LockBannerText = string.Empty;
            IsControlEnabled = true;
        }

        // Sistem
        FwBuild = state.Fw;
        UptimeSec = state.UptimeSec;
        HeapBytes = state.Heap;
        LedIntensityText = state.Led != null ? $"{state.Led.Intensity}%" : "—";

        // Ago
        _lastUpdateTime = DateTime.Now;
        UpdateAgoText();
    }

    private void UpdateAgoText()
    {
        if (_lastUpdateTime == default)
        {
            LastUpdateText = "Niciodată";
            return;
        }

        var elapsed = DateTime.Now - _lastUpdateTime;

        LastUpdateText = elapsed.TotalSeconds switch
        {
            < 10 => "Actualizat acum câteva secunde",
            < 60 => $"Actualizat acum {(int)elapsed.TotalSeconds}s",
            < 3600 => $"Actualizat acum {(int)elapsed.TotalMinutes} min",
            _ => $"Actualizat acum {(int)elapsed.TotalHours}h"
        };
    }

    private static string FormatUptime(long sec)
    {
        if (sec < 60) return $"{sec}s";
        if (sec < 3600) return $"{sec / 60}m {sec % 60}s";
        if (sec < 86400) return $"{sec / 3600}h {(sec % 3600) / 60}m";
        return $"{sec / 86400}d {(sec % 86400) / 3600}h";
    }

    [RelayCommand]
    private async Task RefreshAsync()
    {
        System.Diagnostics.Debug.WriteLine("[Dashboard] CMD refresh");
        if (_mqttService.LastState != null)
            UpdateState(_mqttService.LastState);
        await _mqttService.SendCommandAsync(new { cmd = "refresh" });
    }

    [RelayCommand]
    private async Task ToggleLeftAsync()
    {
        int value = LeftOverride ? 2 : 1;
        System.Diagnostics.Debug.WriteLine($"[Dashboard] CMD setOverride left value={value}");
        await _mqttService.SendCommandAsync(new { cmd = "setOverride", zone = "left", value });
    }

    [RelayCommand]
    private async Task ToggleRightAsync()
    {
        int value = RightOverride ? 2 : 1;
        System.Diagnostics.Debug.WriteLine($"[Dashboard] CMD setOverride right value={value}");
        await _mqttService.SendCommandAsync(new { cmd = "setOverride", zone = "right", value });
    }

    private void UpdateTvState(TvState tv)
    {
        TvDataVisible    = true;
        TvTempText       = tv.Reachable ? $"{tv.TemperatureC}°C" : "—";
        TvUsageText      = tv.Reachable ? $"{tv.UsageHours} ore" : "—";
        TvReachableText  = tv.Reachable ? "ACCESIBIL" : "INACCESIBIL";
        TvReachableColor = tv.Reachable ? Colors.LimeGreen : Colors.Red;
        TvPowerText      = tv.Reachable ? (tv.Power ? "PORNIT" : "STANDBY") : "—";
        TvPowerColor     = tv.Reachable && tv.Power ? Colors.LimeGreen : Colors.Gray;
    }

    public void Dispose()
    {
        _agoTimer?.Stop();
        _mqttService.OnStateReceived -= UpdateState;
        _mqttService.OnConnectionChanged -= UpdateConnection;
        _mqttService.OnOnlineStatusChanged -= UpdateOnlineStatus;
        _mqttService.OnTvStateReceived -= UpdateTvState;
    }
}
