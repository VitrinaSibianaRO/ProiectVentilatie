using System.Text.Json;
using ProiectVentilatie.Mobile.Models;

namespace ProiectVentilatie.Mobile.Services;

public class CameraConfigRepository : ICameraConfigRepository
{
    private readonly string _filePath;
    private List<CameraConfig>? _cache;
    private readonly SemaphoreSlim _lock = new(1, 1);

    private static readonly JsonSerializerOptions _json = new()
    {
        WriteIndented = true,
        Converters = { new System.Text.Json.Serialization.JsonStringEnumConverter() }
    };

    public CameraConfigRepository()
    {
        _filePath = Path.Combine(FileSystem.AppDataDirectory, "cameras.json");
    }

    public async Task<List<CameraConfig>> GetAllAsync()
    {
        await _lock.WaitAsync();
        try
        {
            if (_cache != null) return _cache;
            if (!File.Exists(_filePath)) return _cache = new();
            var json = await File.ReadAllTextAsync(_filePath);
            _cache = JsonSerializer.Deserialize<List<CameraConfig>>(json, _json) ?? new();
            return _cache;
        }
        finally { _lock.Release(); }
    }

    public async Task SaveAllAsync(List<CameraConfig> cameras)
    {
        await _lock.WaitAsync();
        try
        {
            _cache = cameras;
            var tmp = _filePath + ".tmp";
            var json = JsonSerializer.Serialize(cameras, _json);
            await File.WriteAllTextAsync(tmp, json);
            File.Move(tmp, _filePath, overwrite: true);
        }
        finally { _lock.Release(); }
    }

    public async Task<CameraConfig?> GetByIdAsync(Guid id)
    {
        var all = await GetAllAsync();
        return all.FirstOrDefault(c => c.Id == id);
    }

    public async Task UpsertAsync(CameraConfig camera)
    {
        var all = await GetAllAsync();
        var idx = all.FindIndex(c => c.Id == camera.Id);
        camera.UpdatedAt = DateTime.UtcNow;
        if (idx >= 0) all[idx] = camera;
        else all.Add(camera);
        await SaveAllAsync(all);
    }

    public async Task DeleteAsync(Guid id)
    {
        var all = await GetAllAsync();
        all.RemoveAll(c => c.Id == id);
        await SaveAllAsync(all);
    }
}
