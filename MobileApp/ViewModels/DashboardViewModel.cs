using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using ProiectVentilatie.Mobile.Models;
using ProiectVentilatie.Mobile.Services;

namespace ProiectVentilatie.Mobile.ViewModels;

public partial class DashboardViewModel : ObservableObject, IDisposable
{
    private readonly IMqttService _mqttService;
    private readonly IDispatcherTimer _agoTimer;
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

    // ── Conexiune + Online ───────────────────────────
    [ObservableProperty] private string _connectionStatus = "Deconectat";
    [ObservableProperty] private bool _isOnline;
    [ObservableProperty] private Color _onlineBadgeColor = Colors.Red;
    [ObservableProperty] private string _onlineText = "OFFLINE";

    // ── Lock ─────────────────────────────────────────
    [ObservableProperty] private bool _lockBannerVisible;
    [ObservableProperty] private string _lockBannerText = string.Empty;
    [ObservableProperty] private bool _isControlEnabled = true;

    // ── Ago ──────────────────────────────────────────
    [ObservableProperty] private string _lastUpdateText = "Niciodată";

    // ── Sistem ───────────────────────────────────────
    [ObservableProperty] private int _fwBuild;
    [ObservableProperty] private long _uptimeSec;
    [ObservableProperty] private int _heapBytes;

    public DashboardViewModel(IMqttService mqttService)
    {
        _mqttService = mqttService;
        _mqttService.OnStateReceived += UpdateState;
        _mqttService.OnConnectionChanged += UpdateConnection;
        _mqttService.OnOnlineStatusChanged += UpdateOnlineStatus;

        // Timer periodic 10s pentru actualizare "ago" text
        _agoTimer = Application.Current!.Dispatcher.CreateTimer();
        _agoTimer.Interval = TimeSpan.FromSeconds(10);
        _agoTimer.Tick += (s, e) => UpdateAgoText();
        _agoTimer.Start();

        _ = ConnectAsync();
    }

    private async Task ConnectAsync()
    {
        ConnectionStatus = "Conectare...";
        await _mqttService.ConnectAsync();
    }

    private void UpdateConnection(bool isConnected)
    {
        ConnectionStatus = isConnected ? "Conectat la HiveMQ" : "Deconectat";
        if (isConnected)
            RefreshCommand.Execute(null);
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
        RelayRightText = state.Right.Relay ? "PORNIT" : "OPRIT";

        // Override
        LeftOverride = state.Left.Override;
        RightOverride = state.Right.Override;
        OverrideLeftText = state.Left.Override ? "Manual ON" : "Auto";
        OverrideRightText = state.Right.Override ? "Manual ON" : "Auto";

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

    [RelayCommand]
    private async Task RefreshAsync()
    {
        await _mqttService.SendCommandAsync(new { cmd = "refresh" });
    }

    [RelayCommand]
    private async Task ToggleLeftAsync()
    {
        // Toggle override: dacă e activ → clear (2), dacă e oprit → ON (1)
        int value = LeftOverride ? 2 : 1;
        await _mqttService.SendCommandAsync(new { cmd = "setOverride", zone = "left", value });
    }

    [RelayCommand]
    private async Task ToggleRightAsync()
    {
        int value = RightOverride ? 2 : 1;
        await _mqttService.SendCommandAsync(new { cmd = "setOverride", zone = "right", value });
    }

    public void Dispose()
    {
        _agoTimer.Stop();
        _mqttService.OnStateReceived -= UpdateState;
        _mqttService.OnConnectionChanged -= UpdateConnection;
        _mqttService.OnOnlineStatusChanged -= UpdateOnlineStatus;
    }
}
