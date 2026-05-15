using System.Net.Sockets;

namespace ProiectVentilatie.Mobile.Services;

public class NetworkProbeService : INetworkProbeService
{
    private readonly Dictionary<string, (bool result, DateTime checkedAt)> _cache = new();
    private static readonly TimeSpan CacheTtl = TimeSpan.FromSeconds(30);

    public async Task<bool> IsReachableAsync(string host, int port, int timeoutMs = 800)
    {
        if (string.IsNullOrEmpty(host)) return false;

        var key = $"{host}:{port}";
        if (_cache.TryGetValue(key, out var cached) && DateTime.UtcNow - cached.checkedAt < CacheTtl)
            return cached.result;

        try
        {
            using var cts = new CancellationTokenSource(timeoutMs);
            using var client = new TcpClient();
            await client.ConnectAsync(host, port, cts.Token);
            _cache[key] = (true, DateTime.UtcNow);
            return true;
        }
        catch
        {
            _cache[key] = (false, DateTime.UtcNow);
            return false;
        }
    }
}
