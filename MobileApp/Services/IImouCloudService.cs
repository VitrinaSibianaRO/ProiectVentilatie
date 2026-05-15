using ProiectVentilatie.Mobile.Models;

namespace ProiectVentilatie.Mobile.Services;

public interface IImouCloudService
{
    /// <summary>Validare credentiale: preia accessToken. Throws ImouApiException la eroare.</summary>
    Task<ImouTokenCache> GetTokenAsync(ImouCredentials creds);

    /// <summary>Returneaza lista camerelor din contul IMOU.</summary>
    Task<List<ImouDiscoveredDevice>> DiscoverDevicesAsync();

    /// <summary>URL HLS cloud pentru o camera. Cacheat 5 min.</summary>
    Task<Uri> GetHlsUriAsync(string deviceId, int channelId, bool mainStream = false);

    /// <summary>Re-fetch LocalIp pentru toate camerele cunoscute din IMOU Cloud.</summary>
    Task<Dictionary<string, string>> GetDeviceLocalIpsAsync();

    bool HasValidToken { get; }
    ImouTokenCache? CurrentToken { get; }
}
