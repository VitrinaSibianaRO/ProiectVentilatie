using ProiectVentilatie.Mobile.Models;

namespace ProiectVentilatie.Mobile.Services;

public class CredentialStore : ICredentialStore
{
    private const string ImouAppIdKey     = "imou_app_id";
    private const string ImouAppSecretKey = "imou_app_secret";
    private const string ImouRegionKey    = "imou_region";

    public Task<string?> GetAsync(string key) =>
        SecureStorage.Default.GetAsync(key);

    public Task SetAsync(string key, string value) =>
        SecureStorage.Default.SetAsync(key, value);

    public Task RemoveAsync(string key)
    {
        SecureStorage.Default.Remove(key);
        return Task.CompletedTask;
    }

    public async Task<ImouCredentials?> GetImouCredentialsAsync()
    {
        var appId = await GetAsync(ImouAppIdKey);
        if (string.IsNullOrEmpty(appId)) return null;
        return new ImouCredentials
        {
            AppId     = appId,
            AppSecret = await GetAsync(ImouAppSecretKey) ?? string.Empty,
            Region    = await GetAsync(ImouRegionKey)    ?? "eu"
        };
    }

    public async Task SetImouCredentialsAsync(ImouCredentials creds)
    {
        await SetAsync(ImouAppIdKey,     creds.AppId);
        await SetAsync(ImouAppSecretKey, creds.AppSecret);
        await SetAsync(ImouRegionKey,    creds.Region);
    }

    public Task<string?> GetCameraSafetyCodeAsync(Guid cameraId) =>
        GetAsync($"cam_safety_{cameraId}");

    public Task SetCameraSafetyCodeAsync(Guid cameraId, string code) =>
        SetAsync($"cam_safety_{cameraId}", code);

    public Task RemoveCameraSafetyCodeAsync(Guid cameraId) =>
        RemoveAsync($"cam_safety_{cameraId}");
}
