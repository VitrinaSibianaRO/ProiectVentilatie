using ProiectVentilatie.Mobile.Models;

namespace ProiectVentilatie.Mobile.Services;

public interface IMqttService
{
    /// <summary>Stare completă primită de pe ventilatie/state.</summary>
    event Action<VentilationState>? OnStateReceived;

    /// <summary>Conexiune stabilită/pierdută.</summary>
    event Action<bool>? OnConnectionChanged;

    /// <summary>Status ESP32 "online"/"offline" de pe ventilatie/online.</summary>
    event Action<string>? OnOnlineStatusChanged;

    /// <summary>Event JSON de pe ventilatie/event (OTA progress, cmd_rejected etc).</summary>
    event Action<string>? OnEventReceived;

    /// <summary>Log JSON de pe ventilatie/log (răspuns la cmd:getLog).</summary>
    event Action<string>? OnLogReceived;

    /// <summary>TV state primit de pe ventilatie/tv/state.</summary>
    event Action<TvState>? OnTvStateReceived;

    /// <summary>Conectare la broker HiveMQ.</summary>
    Task ConnectAsync();

    /// <summary>Deconectare curată (lifecycle OnSleep).</summary>
    Task DisconnectAsync();

    /// <summary>Trimite comandă JSON pe ventilatie/cmd.</summary>
    Task SendCommandAsync(object command);

    /// <summary>Ultima stare ventilatie primită (cached din retained).</summary>
    VentilationState? LastState { get; }

    /// <summary>Ultima stare TV primită (cached din retained).</summary>
    TvState? LastTvState { get; }

    /// <summary>Timestamp local al ultimei state primite (pentru "ago" indicator).</summary>
    DateTime? LastStateReceivedAt { get; }

    /// <summary>Este conectat la broker.</summary>
    bool IsConnected { get; }
}
