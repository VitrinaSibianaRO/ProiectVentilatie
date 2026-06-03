using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using ProiectVentilatie.Mobile.Models;

namespace ProiectVentilatie.Mobile.Services;

public class ImouApiException(string message, string code) : Exception(message)
{
    public string Code { get; } = code;
}

public class ImouCloudService : IImouCloudService
{
    // Endpoint-ul depinde de regiunea contului IMOU. Default = global/China.
    // Suprascris la GetTokenAsync / EnsureTokenAsync dupa ce stim Region.
    private string _baseUrl = "https://openapi.easy4ip.com/openapi/";

    private static string ResolveBaseUrl(string? region) => (region ?? string.Empty).ToLowerInvariant() switch
    {
        "eu"         => "https://openapi-fk.easy4ip.com/openapi/",  // Frankfurt
        "us"         => "https://openapi-or.easy4ip.com/openapi/",  // Oregon
        "ap" or "sg" => "https://openapi-sg.easy4ip.com/openapi/",  // Singapore
        _            => "https://openapi.easy4ip.com/openapi/",     // cn / global
    };

    private const string PrefToken       = "imou_token";
    private const string PrefTokenExpiry = "imou_token_expiry";
    private const string PrefTimeOffset  = "imou_time_offset";

    private static readonly TimeSpan HlsCacheTtl = TimeSpan.FromMinutes(5);

    private readonly HttpClient _http;
    private readonly ICredentialStore _creds;
    private readonly Dictionary<string, (Uri uri, DateTime cachedAt)> _hlsCache = new();
    private long _serverTimeOffset; // seconds
    private ImouCredentials? _cachedCredentials; // necesar pentru sign pe toate request-urile

    public ImouTokenCache? CurrentToken { get; private set; }
    public bool HasValidToken => CurrentToken?.IsValid == true;

    public ImouCloudService(ICredentialStore credentialStore)
    {
        _creds = credentialStore;
        _http = new HttpClient { Timeout = TimeSpan.FromSeconds(15) };

        // Restore cached token + offset from Preferences
        var token = Preferences.Default.Get(PrefToken, string.Empty);
        var expiryStr = Preferences.Default.Get(PrefTokenExpiry, string.Empty);
        _serverTimeOffset = Preferences.Default.Get(PrefTimeOffset, 0L);

        if (!string.IsNullOrEmpty(token) && DateTime.TryParse(expiryStr, out var expiry))
        {
            CurrentToken = new ImouTokenCache
            {
                AccessToken      = token,
                ExpiresAt        = expiry,
                IssuedAt         = DateTime.UtcNow,
                ServerTimeOffset = _serverTimeOffset
            };
        }
    }

    public async Task<ImouTokenCache> GetTokenAsync(ImouCredentials creds)
    {
        _cachedCredentials = creds; // stocam pentru sign-ul tuturor request-urilor
        _baseUrl = ResolveBaseUrl(creds.Region); // ruteaza catre endpoint regional inainte de orice HTTP call
        await SyncClockSkewAsync();

        var body = BuildRequest("accessToken", new JsonObject(), creds);
        var json = await PostAsync("accessToken", body);

        var tokenStr = json?["data"]?["tokenInfo"]?["token"]?.GetValue<string>()
            ?? throw new ImouApiException("Token missing in response", "ERR_PARSE");

        var expiredTs = json?["data"]?["tokenInfo"]?["expiredTime"]?.GetValue<long>() ?? 0;
        var expiry = expiredTs > 0
            ? DateTimeOffset.FromUnixTimeSeconds(expiredTs).UtcDateTime
            : DateTime.UtcNow.AddDays(3);

        CurrentToken = new ImouTokenCache
        {
            AccessToken      = tokenStr,
            IssuedAt         = DateTime.UtcNow,
            ExpiresAt        = expiry,
            ServerTimeOffset = _serverTimeOffset
        };

        Preferences.Default.Set(PrefToken,       tokenStr);
        Preferences.Default.Set(PrefTokenExpiry, expiry.ToString("O"));
        Preferences.Default.Set(PrefTimeOffset,  _serverTimeOffset);

        return CurrentToken;
    }

    public async Task<List<ImouDiscoveredDevice>> DiscoverDevicesAsync()
    {
        await EnsureTokenAsync();

        var devices = new List<ImouDiscoveredDevice>();
        int page = 1;

        while (true)
        {
            var p = new JsonObject
            {
                ["bindId"]      = "",
                ["needApInfo"]  = true,
                ["num"]         = 50,
                ["page"]        = page
            };
            var body = BuildRequest("deviceBaseDetailList", p);
            var json = await PostAsync("deviceBaseDetailList", body);
            var list = json?["data"]?["deviceList"]?.AsArray();
            if (list == null || list.Count == 0) break;

            foreach (var item in list)
            {
                if (item == null) continue;
                devices.Add(new ImouDiscoveredDevice
                {
                    DeviceId  = item["deviceId"]?.GetValue<string>()  ?? string.Empty,
                    Name      = item["name"]?.GetValue<string>()       ?? string.Empty,
                    IsOnline  = item["status"]?.GetValue<string>()     == "online",
                    Model     = item["deviceCatalog"]?.GetValue<string>() ?? string.Empty,
                    Firmware  = item["version"]?.GetValue<string>()    ?? string.Empty,
                    LocalIp   = item["apInfo"]?["ipAddr"]?.GetValue<string>() ?? string.Empty,
                    Channels  = Enumerable.Range(1, item["channelNum"]?.GetValue<int>() ?? 1).ToList()
                });
            }

            if (list.Count < 50) break;
            page++;
        }

        return devices;
    }

    public async Task<Uri> GetHlsUriAsync(string deviceId, int channelId, bool mainStream = false)
    {
        var key = $"{deviceId}:{channelId}:{(mainStream ? 1 : 0)}";
        if (_hlsCache.TryGetValue(key, out var cached) && DateTime.UtcNow - cached.cachedAt < HlsCacheTtl)
            return cached.uri;

        await EnsureTokenAsync();

        // bindDeviceLive — streamId: 0=HD, 1=SD
        var bindParams = new JsonObject
        {
            ["deviceId"]  = deviceId,
            ["channelId"] = (channelId - 1).ToString(), // 0-based
            ["streamId"]  = mainStream ? 0 : 1
        };
        var bindBody = BuildRequest("bindDeviceLive", bindParams);
        var bindJson = await PostAsync("bindDeviceLive", bindBody);

        // bindDeviceLive response contains HLS URL directly in streams[0].hls
        var hlsUrl = bindJson?["data"]?["streams"]?[0]?["hls"]?.GetValue<string>();
        if (string.IsNullOrEmpty(hlsUrl))
        {
            // Fallback: use getLiveStreamInfo if bindDeviceLive didn't return streams
            var liveToken = bindJson?["data"]?["params"]?["token"]?.GetValue<string>()
                ?? throw new ImouApiException("bindDeviceLive: no streams or token", "ERR_PARSE");

            var streamParams = new JsonObject { ["deviceId"] = deviceId, ["channelId"] = (channelId - 1).ToString() };
            var streamBody = BuildRequest("getLiveStreamInfo", streamParams);
            var streamJson = await PostAsync("getLiveStreamInfo", streamBody);
            hlsUrl = streamJson?["data"]?["streams"]?[0]?["hls"]?.GetValue<string>()
                ?? throw new ImouApiException("getLiveStreamInfo: hls url missing", "ERR_PARSE");
        }

        var uri = new Uri(hlsUrl);
        _hlsCache[key] = (uri, DateTime.UtcNow);
        return uri;
    }

    public async Task<Dictionary<string, string>> GetDeviceLocalIpsAsync()
    {
        var devices = await DiscoverDevicesAsync();
        return devices
            .Where(d => !string.IsNullOrEmpty(d.LocalIp))
            .ToDictionary(d => d.DeviceId, d => d.LocalIp);
    }

    // ──────────────────────────────────────────────────────────
    // Internals
    // ──────────────────────────────────────────────────────────

    private async Task EnsureTokenAsync()
    {
        // Incarca credentialele daca nu sunt deja in memorie (ex: restart cu token cached)
        if (_cachedCredentials == null)
        {
            _cachedCredentials = await _creds.GetImouCredentialsAsync();
            if (_cachedCredentials != null)
                _baseUrl = ResolveBaseUrl(_cachedCredentials.Region);
        }

        if (HasValidToken && _cachedCredentials != null) return;

        var credentials = _cachedCredentials
            ?? throw new ImouApiException("IMOU credentials not configured. Adauga AppId/AppSecret in Settings.", "ERR_NO_CREDS");
        await GetTokenAsync(credentials);
    }

    private async Task SyncClockSkewAsync()
    {
        try
        {
            // HEAD pe root-ul endpoint-ului regional curent (fara path-ul /openapi/)
            var idx = _baseUrl.IndexOf("/openapi", StringComparison.Ordinal);
            var root = idx > 0 ? _baseUrl.Substring(0, idx) + "/" : _baseUrl;
            var req = new HttpRequestMessage(HttpMethod.Head, root);
            var resp = await _http.SendAsync(req);
            if (resp.Headers.TryGetValues("Date", out var vals))
            {
                var dateStr = vals.FirstOrDefault();
                if (DateTimeOffset.TryParse(dateStr, out var serverDate))
                {
                    _serverTimeOffset = (long)(serverDate.ToUnixTimeSeconds() - DateTimeOffset.UtcNow.ToUnixTimeSeconds());
                    Preferences.Default.Set(PrefTimeOffset, _serverTimeOffset);
                }
            }
        }
        catch { /* non-critical — proceed with offset=0 */ }
    }

    private long ServerTime =>
        DateTimeOffset.UtcNow.ToUnixTimeSeconds() + _serverTimeOffset;

    private static string Nonce()
    {
        const string chars = "abcdefghijklmnopqrstuvwxyz0123456789";
        return new string(Enumerable.Range(0, 10).Select(_ => chars[Random.Shared.Next(chars.Length)]).ToArray());
    }

    // Formula corecta: MD5("time:{time},nonce:{nonce},appSecret:{secret}") — fara appId in hash
    private static string Md5Sign(string appSecret, long time, string nonce)
    {
        var input = $"time:{time},nonce:{nonce},appSecret:{appSecret}";
        var hash = MD5.HashData(Encoding.UTF8.GetBytes(input));
        return Convert.ToHexString(hash).ToLowerInvariant();
    }

    private JsonObject BuildRequest(string endpoint, JsonObject @params, ImouCredentials? credsOverride = null)
    {
        var effective = credsOverride ?? _cachedCredentials
            ?? throw new ImouApiException("Credentiale IMOU lipsesc — salveaza AppId/AppSecret in Settings", "ERR_NO_CREDS");

        // Adauga token in params pentru toate call-urile dupa accessToken
        if (CurrentToken != null && endpoint != "accessToken" && !@params.ContainsKey("token"))
            @params["token"] = CurrentToken.AccessToken;

        var time  = ServerTime;
        var nonce = Nonce();
        var system = new JsonObject
        {
            ["ver"]   = "1.0",
            ["appId"] = effective.AppId,
            ["sign"]  = Md5Sign(effective.AppSecret, time, nonce),
            ["time"]  = time,
            ["nonce"] = nonce
        };

        return new JsonObject
        {
            ["system"] = system,
            ["id"]     = Guid.NewGuid().ToString("N")[..16],
            ["params"] = @params
        };
    }

    private async Task<JsonNode?> PostAsync(string endpoint, JsonObject body)
    {
        var content = new StringContent(body.ToJsonString(), Encoding.UTF8, "application/json");
        var response = await _http.PostAsync($"{_baseUrl}{endpoint}", content);
        response.EnsureSuccessStatusCode();

        var respStr = await response.Content.ReadAsStringAsync();
        var json = JsonNode.Parse(respStr);

        var code = json?["result"]?["code"]?.GetValue<string>() ?? "0";
        if (code != "0")
        {
            var msg = json?["result"]?["msg"]?.GetValue<string>() ?? "IMOU API error";
            if (code == "TK1002") // token expired
            {
                CurrentToken = null;
                throw new ImouApiException("Token expirat — reconectare necesară", code);
            }
            throw new ImouApiException(msg, code);
        }

        return json;
    }
}
