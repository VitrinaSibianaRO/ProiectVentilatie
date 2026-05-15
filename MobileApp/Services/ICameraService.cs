using ProiectVentilatie.Mobile.Models;

namespace ProiectVentilatie.Mobile.Services;

public interface ICameraService
{
    /// <summary>
    /// Rezolva URI de stream pentru o camera: RTSP LAN (Auto+LAN pe reteaua locala)
    /// sau HLS Cloud (Cloud / Auto cand LAN nu e accesibil).
    /// </summary>
    Task<(Uri uri, NetworkScope usedScope)> ResolveStreamUriAsync(
        CameraConfig config,
        bool fullscreen = false,
        CancellationToken ct = default);

    Uri BuildRtspUri(CameraConfig config, bool fullscreen = false, string? safetyCode = null);
}
