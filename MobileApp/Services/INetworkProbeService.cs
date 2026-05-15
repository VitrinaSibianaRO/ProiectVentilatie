namespace ProiectVentilatie.Mobile.Services;

public interface INetworkProbeService
{
    Task<bool> IsReachableAsync(string host, int port, int timeoutMs = 800);
}
