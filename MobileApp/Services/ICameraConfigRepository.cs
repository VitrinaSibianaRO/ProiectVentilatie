using ProiectVentilatie.Mobile.Models;

namespace ProiectVentilatie.Mobile.Services;

public interface ICameraConfigRepository
{
    Task<List<CameraConfig>> GetAllAsync();
    Task SaveAllAsync(List<CameraConfig> cameras);
    Task<CameraConfig?> GetByIdAsync(Guid id);
    Task UpsertAsync(CameraConfig camera);
    Task DeleteAsync(Guid id);
}
