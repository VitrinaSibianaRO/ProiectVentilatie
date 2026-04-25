using ProiectVentilatie.Mobile.Models;

namespace ProiectVentilatie.Mobile.Services;

public interface IMqttService
{
    event Action<VentilationState>? OnStateReceived;
    event Action<bool>? OnConnectionChanged;

    Task ConnectAsync();
    Task SendCommandAsync(object command);
}
