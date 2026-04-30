using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Microsoft.Extensions.Options;
using ProiectVentilatie.Mobile.Models;
using ProiectVentilatie.Mobile.Services;

namespace ProiectVentilatie.Mobile.ViewModels;

public partial class DevicesViewModel : ObservableObject, IDisposable
{
    private readonly IMqttService _mqttService;
    private readonly MqttSettings _mqttSettings;

    // ── Broker ───────────────────────────────────────────────────────────────
    [ObservableProperty] private string _brokerHost = string.Empty;
    [ObservableProperty] private string _brokerPort = "8883 (TLS)";
    [ObservableProperty] private bool _brokerConnected;
    [ObservableProperty] private string _brokerStatusText = "Deconectat";
    [ObservableProperty] private Color _brokerStatusColor = Colors.OrangeRed;
    [ObservableProperty] private string _stateTopic = "ventilatie/state";
    [ObservableProperty] private string _cmdTopic = "ventilatie/cmd";

    // ── ESP32 ─────────────────────────────────────────────────────────────────
    [ObservableProperty] private bool _espOnline;
    [ObservableProperty] private string _espOnlineText = "NU";
    [ObservableProperty] private Color _espOnlineColor = Colors.OrangeRed;
    [ObservableProperty] private int _espFwBuild;
    [ObservableProperty] private string _espHeapText = "—";
    [ObservableProperty] private Color _espHeapColor = Colors.Gray;
    [ObservableProperty] private string _espUptimeText = "—";

    // ── Control Lock ──────────────────────────────────────────────────────────
    [ObservableProperty] private string _lockOwnerText = "Liber";
    [ObservableProperty] private Color _lockOwnerColor = Colors.Gray;
    [ObservableProperty] private string _lockAgeText = string.Empty;
    [ObservableProperty] private string _lockInfoText = "Niciun canal activ — MQTT și Blynk pot trimite comenzi simultan.";
    [ObservableProperty] private bool _hasLock;
    [ObservableProperty] private Color _lockAccent = Colors.Gray;

    // ── Sensors ───────────────────────────────────────────────────────────────
    [ObservableProperty] private string _leftSensorStatus = "—";
    [ObservableProperty] private Color _leftSensorColor = Colors.Gray;
    [ObservableProperty] private int _leftErrors;
    [ObservableProperty] private bool _leftHasErrors;
    [ObservableProperty] private string _rightSensorStatus = "—";
    [ObservableProperty] private Color _rightSensorColor = Colors.Gray;
    [ObservableProperty] private int _rightErrors;
    [ObservableProperty] private bool _rightHasErrors;

    public DevicesViewModel(IMqttService mqttService, IOptions<MqttSettings> mqttOptions)
    {
        _mqttService   = mqttService;
        _mqttSettings  = mqttOptions.Value;

        BrokerHost  = _mqttSettings.Host;
        StateTopic  = _mqttSettings.StateTopic;
        CmdTopic    = _mqttSettings.CommandTopic;
        BrokerConnected = _mqttService.IsConnected;
        UpdateBrokerStatus();

        _mqttService.OnStateReceived       += OnStateReceived;
        _mqttService.OnConnectionChanged   += OnConnectionChanged;
        _mqttService.OnOnlineStatusChanged += OnOnlineStatusChanged;

        if (_mqttService.LastState != null)
            OnStateReceived(_mqttService.LastState);
    }

    private void OnStateReceived(VentilationState state)
    {
        EspFwBuild    = state.Fw;
        EspHeapText   = FormatHeap(state.Heap);
        EspHeapColor  = HeapColor(state.Heap);
        EspUptimeText = FormatUptime(state.UptimeSec);

        var lk = state.Lock;
        if (lk == null)
        {
            LockOwnerText = "Liber";
            LockOwnerColor = Colors.Gray;
            LockAccent = Colors.Gray;
            LockAgeText = string.Empty;
            HasLock = false;
            LockInfoText = "Niciun canal activ — MQTT și Blynk pot trimite comenzi simultan.";
        }
        else if (lk.Owner == "blynk")
        {
            LockOwnerText = "Blynk activ";
            LockOwnerColor = Color.FromArgb("#ff7a45");
            LockAccent = Color.FromArgb("#ff7a45");
            LockAgeText = $"{lk.AgeMs / 1000}s";
            HasLock = true;
            LockInfoText = "Blynk deține controlul. Comenzile MAUI sunt respinse până la eliberare (~100ms).";
        }
        else
        {
            LockOwnerText = "MAUI activ";
            LockOwnerColor = Color.FromArgb("#00e6ff");
            LockAccent = Color.FromArgb("#00e6ff");
            LockAgeText = $"{lk.AgeMs / 1000}s";
            HasLock = true;
            LockInfoText = "MAUI deține controlul. Comenzile Blynk sunt respinse până la eliberare (~100ms).";
        }

        LeftErrors    = state.Left.Errs;
        LeftHasErrors = state.Left.Errs > 0;
        LeftSensorStatus = state.Left.Errs >= 5 ? "EROARE" : "OK";
        LeftSensorColor  = state.Left.Errs >= 5
            ? Color.FromArgb("#ff4422")
            : Color.FromArgb("#00e87a");

        RightErrors    = state.Right.Errs;
        RightHasErrors = state.Right.Errs > 0;
        RightSensorStatus = state.Right.Errs >= 5 ? "EROARE" : "OK";
        RightSensorColor  = state.Right.Errs >= 5
            ? Color.FromArgb("#ff4422")
            : Color.FromArgb("#00e87a");
    }

    private void OnConnectionChanged(bool isConnected)
    {
        BrokerConnected = isConnected;
        UpdateBrokerStatus();
    }

    private void OnOnlineStatusChanged(string status)
    {
        EspOnline      = status == "online";
        EspOnlineText  = EspOnline ? "DA"  : "NU";
        EspOnlineColor = EspOnline
            ? Color.FromArgb("#00e87a")
            : Color.FromArgb("#ff4422");
    }

    private void UpdateBrokerStatus()
    {
        BrokerStatusText  = BrokerConnected ? "Conectat"   : "Deconectat";
        BrokerStatusColor = BrokerConnected
            ? Color.FromArgb("#00e87a")
            : Color.FromArgb("#ff4422");
    }

    [RelayCommand]
    private async Task RefreshAsync() =>
        await _mqttService.SendCommandAsync(new { cmd = "refresh" });

    private static string FormatHeap(int bytes) =>
        bytes <= 0 ? "—" : bytes >= 1024 ? $"{bytes / 1024} KB" : $"{bytes} B";

    private static Color HeapColor(int bytes) => bytes switch
    {
        >= 80_000 => Color.FromArgb("#00e87a"),
        >= 30_000 => Color.FromArgb("#ffbb00"),
        _         => Color.FromArgb("#ff4422")
    };

    private static string FormatUptime(long sec)
    {
        if (sec <= 0) return "—";
        long h = sec / 3600, m = sec % 3600 / 60;
        return h > 0 ? $"{h}h {m:00}m" : $"{m}m";
    }

    public void Dispose()
    {
        _mqttService.OnStateReceived       -= OnStateReceived;
        _mqttService.OnConnectionChanged   -= OnConnectionChanged;
        _mqttService.OnOnlineStatusChanged -= OnOnlineStatusChanged;
    }
}
