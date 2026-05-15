using ProiectVentilatie.Mobile.Models;

namespace ProiectVentilatie.Mobile.Services;

public class CameraService : ICameraService
{
    private readonly INetworkProbeService _probe;
    private readonly IImouCloudService _cloud;
    private readonly ICredentialStore _creds;

    public CameraService(
        INetworkProbeService probe,
        IImouCloudService cloud,
        ICredentialStore creds)
    {
        _probe = probe;
        _cloud = cloud;
        _creds = creds;
    }

    public async Task<(Uri uri, NetworkScope usedScope)> ResolveStreamUriAsync(
        CameraConfig config,
        bool fullscreen = false,
        CancellationToken ct = default)
    {
        switch (config.PreferredScope)
        {
            case NetworkScope.Cloud:
                var hlsUri = await _cloud.GetHlsUriAsync(
                    config.ImouDeviceId, config.ImouChannelId, fullscreen);
                return (hlsUri, NetworkScope.Cloud);

            case NetworkScope.Lan:
                var rtsp = await BuildRtspUriWithCredAsync(config, fullscreen);
                return (rtsp, NetworkScope.Lan);

            default: // Auto
                var lanOk = await _probe.IsReachableAsync(
                    config.LocalIp, config.RtspPort, timeoutMs: 800);

                if (lanOk)
                {
                    var lanUri = await BuildRtspUriWithCredAsync(config, fullscreen);
                    return (lanUri, NetworkScope.Lan);
                }
                else
                {
                    var cloudUri = await _cloud.GetHlsUriAsync(
                        config.ImouDeviceId, config.ImouChannelId, fullscreen);
                    return (cloudUri, NetworkScope.Cloud);
                }
        }
    }

    public Uri BuildRtspUri(CameraConfig config, bool fullscreen = false, string? safetyCode = null)
    {
        var user = Uri.EscapeDataString(config.RtspUsername);
        var pass = safetyCode != null ? Uri.EscapeDataString(safetyCode) : "***";
        var subtype = fullscreen ? 0 : 1; // 0=main 5MP, 1=sub 640x480
        return new Uri(
            $"rtsp://{user}:{pass}@{config.LocalIp}:{config.RtspPort}" +
            $"/cam/realmonitor?channel={config.ImouChannelId}&subtype={subtype}");
    }

    private async Task<Uri> BuildRtspUriWithCredAsync(CameraConfig config, bool fullscreen)
    {
        var safetyCode = await _creds.GetCameraSafetyCodeAsync(config.Id);
        return BuildRtspUri(config, fullscreen, safetyCode ?? string.Empty);
    }
}
