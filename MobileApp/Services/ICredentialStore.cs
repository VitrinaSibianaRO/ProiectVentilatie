using ProiectVentilatie.Mobile.Models;

namespace ProiectVentilatie.Mobile.Services;

public interface ICredentialStore
{
    Task<string?> GetAsync(string key);
    Task SetAsync(string key, string value);
    Task RemoveAsync(string key);

    Task<ImouCredentials?> GetImouCredentialsAsync();
    Task SetImouCredentialsAsync(ImouCredentials creds);

    Task<string?> GetCameraSafetyCodeAsync(Guid cameraId);
    Task SetCameraSafetyCodeAsync(Guid cameraId, string code);
    Task RemoveCameraSafetyCodeAsync(Guid cameraId);
}
