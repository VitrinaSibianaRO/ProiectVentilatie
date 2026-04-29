using System.Text.Json;
using System.Text.Json.Serialization;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Microsoft.Extensions.Options;
using ProiectVentilatie.Mobile.Models;
using ProiectVentilatie.Mobile.Services;

namespace ProiectVentilatie.Mobile.ViewModels;

/// <summary>
/// Pagina Sistem — status broker/ESP32, refresh, reboot, OTA UI.
/// Câmpurile OTA sunt persistate în Preferences (URL repo, versiune,
/// nume fișier, SHA-256 — pentru reutilizare la update-uri ulterioare).
/// </summary>
public partial class SystemViewModel : ObservableObject, IDisposable
{
    private readonly IMqttService _mqttService;
    private readonly MqttSettings _mqttSettings;
    private readonly IDispatcherTimer _agoTimer;

    // Preference keys
    private const string PrefOtaRepoUrl    = "OtaRepoUrl";
    private const string PrefOtaVersion    = "OtaLastVersion";
    private const string PrefOtaFilename   = "OtaLastFilename";
    private const string PrefOtaSha        = "OtaLastSha";

    // ── Broker info ──────────────────────────────────
    [ObservableProperty] private string _brokerHost = string.Empty;
    [ObservableProperty] private int _brokerPort;
    [ObservableProperty] private bool _brokerConnected;
    [ObservableProperty] private string _brokerStatusText = "Deconectat";
    [ObservableProperty] private Color _brokerStatusColor = Colors.Red;

    // ── ESP32 info ───────────────────────────────────
    [ObservableProperty] private bool _espOnline;
    [ObservableProperty] private string _espStatusText = "OFFLINE";
    [ObservableProperty] private Color _espStatusColor = Colors.Red;
    [ObservableProperty] private int _espFwBuild;
    [ObservableProperty] private string _espUptimeText = "—";
    [ObservableProperty] private string _espHeapText = "—";
    [ObservableProperty] private double _espHeapProgress = 0.0;
    [ObservableProperty] private bool _isHeapCritical = false;
    [ObservableProperty] private int _leftErrors;
    [ObservableProperty] private int _rightErrors;
    [ObservableProperty] private string _lastUpdateAgoText = "Niciodată";

    // ── App version ──────────────────────────────────
    [ObservableProperty] private string _appVersionText;

    // ── OTA ──────────────────────────────────────────
    [ObservableProperty] private string _otaRepoUrl = string.Empty;
    [ObservableProperty] private string _otaVersion = string.Empty;
    [ObservableProperty] private string _otaFilename = "firmware.bin";
    [ObservableProperty] private string _otaSha = string.Empty;
    [ObservableProperty] private string _otaFinalUrl = string.Empty;
    [ObservableProperty] private double _otaProgress;
    [ObservableProperty] private bool _otaInProgress;
    [ObservableProperty] private string _otaStatusMessage = string.Empty;
    [ObservableProperty] private Color _otaStatusColor = Colors.Gray;

    public SystemViewModel(IMqttService mqttService, IOptions<MqttSettings> mqttOptions)
    {
        _mqttService = mqttService;
        _mqttSettings = mqttOptions.Value;

        BrokerHost = _mqttSettings.Host;
        BrokerPort = _mqttSettings.Port;
        AppVersionText = $"{AppInfo.Current.VersionString} (build #{AppInfo.Current.BuildString})";

        _mqttService.OnStateReceived += OnStateReceived;
        _mqttService.OnConnectionChanged += OnConnectionChanged;
        _mqttService.OnOnlineStatusChanged += OnOnlineStatusChanged;
        _mqttService.OnEventReceived += OnEventReceived;

        BrokerConnected = _mqttService.IsConnected;
        UpdateBrokerStatus();

        if (_mqttService.LastState != null)
        {
            OnStateReceived(_mqttService.LastState);
        }

        // Load OTA fields from Preferences
        OtaRepoUrl  = Preferences.Get(PrefOtaRepoUrl,  string.Empty);
        OtaVersion  = Preferences.Get(PrefOtaVersion,  string.Empty);
        OtaFilename = Preferences.Get(PrefOtaFilename, "firmware.bin");
        OtaSha      = Preferences.Get(PrefOtaSha,      string.Empty);
        UpdateOtaFinalUrl();

        // Timer ago
        _agoTimer = Application.Current!.Dispatcher.CreateTimer();
        _agoTimer.Interval = TimeSpan.FromSeconds(15);
        _agoTimer.Tick += (s, e) => UpdateAgoText();
        _agoTimer.Start();
        UpdateAgoText();
    }

    // ── Listeners ────────────────────────────────────

    private void OnStateReceived(VentilationState state)
    {
        EspFwBuild = state.Fw;
        EspUptimeText = FormatUptime(state.UptimeSec);
        EspHeapText = FormatHeap(state.Heap);
        // Heap progress: normalize against 200KB typical max
        EspHeapProgress = Math.Clamp(state.Heap / 200_000.0, 0, 1);
        IsHeapCritical = state.Heap < 30_000;
        LeftErrors = state.Left.Errs;
        RightErrors = state.Right.Errs;
        UpdateAgoText();
    }

    private void OnConnectionChanged(bool isConnected)
    {
        BrokerConnected = isConnected;
        UpdateBrokerStatus();
    }

    private void OnOnlineStatusChanged(string status)
    {
        EspOnline = status == "online";
        EspStatusText = EspOnline ? "ESP32 ONLINE" : "ESP32 OFFLINE";
        EspStatusColor = EspOnline ? Colors.LimeGreen : Colors.Red;
    }

    private void OnEventReceived(string json)
    {
        try
        {
            using var doc = JsonDocument.Parse(json);
            var root = doc.RootElement;
            if (!root.TryGetProperty("event", out var evtEl)) return;

            var evt = evtEl.GetString();
            switch (evt)
            {
                case "ota_progress":
                    if (root.TryGetProperty("pct", out var pctEl))
                    {
                        OtaProgress = pctEl.GetInt32() / 100.0;
                        OtaStatusMessage = $"Download {pctEl.GetInt32()}%";
                        OtaStatusColor = Colors.Orange;
                    }
                    break;
                case "ota_done":
                    OtaProgress = 1.0;
                    OtaInProgress = false;
                    OtaStatusMessage = "✓ Update reușit. ESP32 se restartează...";
                    OtaStatusColor = Colors.LimeGreen;
                    break;
                case "ota_failed":
                    OtaInProgress = false;
                    var reason = root.TryGetProperty("reason", out var rEl) ? rEl.GetString() : "unknown";
                    OtaStatusMessage = $"✗ Update eșuat: {reason}";
                    OtaStatusColor = Colors.Red;
                    break;
                case "cmd_rejected":
                    var by = root.TryGetProperty("by", out var bEl) ? bEl.GetString() : "?";
                    OtaStatusMessage = $"⚠️ Comandă respinsă (lock {by})";
                    OtaStatusColor = Colors.Orange;
                    break;
            }
        }
        catch { /* ignore parse errors */ }
    }

    // ── Helpers ──────────────────────────────────────

    private void UpdateBrokerStatus()
    {
        BrokerStatusText = BrokerConnected ? "Conectat" : "Deconectat";
        BrokerStatusColor = BrokerConnected ? Colors.LimeGreen : Colors.Red;
    }

    private void UpdateAgoText()
    {
        var ts = _mqttService.LastStateReceivedAt;
        if (ts == null)
        {
            LastUpdateAgoText = "Niciodată";
            return;
        }

        var elapsed = DateTime.Now - ts.Value;
        LastUpdateAgoText = elapsed.TotalSeconds switch
        {
            < 60   => $"Actualizat acum {(int)elapsed.TotalSeconds}s",
            < 3600 => $"Actualizat acum {(int)elapsed.TotalMinutes} min",
            < 86400 => $"Actualizat acum {(int)elapsed.TotalHours}h {elapsed.Minutes}m",
            _ => $"Actualizat acum {(int)elapsed.TotalDays} zile"
        };
    }

    private static string FormatUptime(long sec)
    {
        if (sec < 60) return $"{sec}s";
        if (sec < 3600) return $"{sec / 60}m {sec % 60}s";
        if (sec < 86400) return $"{sec / 3600}h {(sec % 3600) / 60}m";
        return $"{sec / 86400}d {(sec % 86400) / 3600}h";
    }

    private static string FormatHeap(int bytes) =>
        bytes >= 1024 ? $"{bytes / 1024} KB" : $"{bytes} B";

    partial void OnOtaRepoUrlChanged(string value) { Preferences.Set(PrefOtaRepoUrl, value ?? ""); UpdateOtaFinalUrl(); }
    partial void OnOtaVersionChanged(string value) { Preferences.Set(PrefOtaVersion, value ?? ""); UpdateOtaFinalUrl(); }
    partial void OnOtaFilenameChanged(string value) { Preferences.Set(PrefOtaFilename, value ?? ""); UpdateOtaFinalUrl(); }
    partial void OnOtaShaChanged(string value) { Preferences.Set(PrefOtaSha, value ?? ""); }

    private void UpdateOtaFinalUrl()
    {
        var repo = (OtaRepoUrl ?? "").TrimEnd('/');
        var ver = (OtaVersion ?? "").Trim();
        var file = (OtaFilename ?? "").Trim();
        OtaFinalUrl = (string.IsNullOrEmpty(repo) || string.IsNullOrEmpty(ver) || string.IsNullOrEmpty(file))
            ? string.Empty
            : $"{repo}/{ver}/{file}";
    }

    // ── Commands ─────────────────────────────────────

    [RelayCommand]
    private async Task RefreshAsync()
    {
        await _mqttService.SendCommandAsync(new { cmd = "refresh" });
    }

    [RelayCommand]
    private async Task RebootAsync()
    {
        var page = Application.Current?.Windows[0]?.Page;
        if (page == null) return;
        var confirm = await page.DisplayAlertAsync(
            "Restart ESP32",
            "Confirmi restart-ul ESP32-ului? Sistemul va fi indisponibil ~5 secunde.",
            "Da, restart",
            "Anulează");
        if (!confirm) return;
        await _mqttService.SendCommandAsync(new { cmd = "reboot" });
    }

    [RelayCommand]
    private async Task SendOtaUpdateAsync()
    {
        if (string.IsNullOrEmpty(OtaFinalUrl) || OtaSha?.Length != 64)
        {
            OtaStatusMessage = "✗ URL sau SHA-256 invalid (64 hex chars)";
            OtaStatusColor = Colors.Red;
            return;
        }

        var page = Application.Current?.Windows[0]?.Page;
        if (page == null) return;
        var confirm = await page.DisplayAlertAsync(
            "Update Firmware",
            $"Trimit comandă OTA către ESP32:\n\n{OtaFinalUrl}\n\nConfirmi?",
            "Da, update",
            "Anulează");
        if (!confirm) return;

        OtaInProgress = true;
        OtaProgress = 0;
        OtaStatusMessage = "Trimis. Aștept ESP32...";
        OtaStatusColor = Colors.Orange;

        await _mqttService.SendCommandAsync(new
        {
            cmd = "update",
            url = OtaFinalUrl,
            sha256 = OtaSha
        });
    }

    public void Dispose()
    {
        _agoTimer.Stop();
        _mqttService.OnStateReceived -= OnStateReceived;
        _mqttService.OnConnectionChanged -= OnConnectionChanged;
        _mqttService.OnOnlineStatusChanged -= OnOnlineStatusChanged;
        _mqttService.OnEventReceived -= OnEventReceived;
    }
}
