using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using ProiectVentilatie.Mobile.Models;
using ProiectVentilatie.Mobile.Services;

namespace ProiectVentilatie.Mobile.ViewModels;

public partial class CameraSettingsViewModel : ObservableObject
{
    private readonly ICredentialStore _creds;
    private readonly IImouCloudService _cloud;
    private readonly ICameraConfigRepository _repo;

    // ── Cloud credentials ──────────────────────────────────────
    [ObservableProperty] private string _appId     = string.Empty;
    [ObservableProperty] private string _appSecret = string.Empty;
    [ObservableProperty] private string _region    = "eu";
    [ObservableProperty] private string _tokenStatus = "—";

    // ── Camera list ───────────────────────────────────────────
    public ObservableCollection<CameraConfig> Cameras { get; } = new();

    // ── Discovery popup ───────────────────────────────────────
    [ObservableProperty] private bool _isDiscovering;
    [ObservableProperty] private ObservableCollection<DiscoveryItemVm> _discoveredItems = new();

    // ── State ─────────────────────────────────────────────────
    [ObservableProperty] private bool _isBusy;
    [ObservableProperty] private string? _statusMessage;
    [ObservableProperty] private bool _isError;

    public CameraSettingsViewModel(
        ICredentialStore creds,
        IImouCloudService cloud,
        ICameraConfigRepository repo)
    {
        _creds = creds;
        _cloud = cloud;
        _repo  = repo;
    }

    public async Task LoadAsync()
    {
        var savedCreds = await _creds.GetImouCredentialsAsync();
        if (savedCreds != null)
        {
            AppId     = savedCreds.AppId;
            AppSecret = savedCreds.AppSecret;
            Region    = savedCreds.Region;
        }

        if (_cloud.CurrentToken?.IsValid == true)
            TokenStatus = $"Token valid pana la {_cloud.CurrentToken.ExpiresAt:dd MMM HH:mm}";

        var cameras = await _repo.GetAllAsync();
        Cameras.Clear();
        foreach (var c in cameras.OrderBy(c => c.DisplayOrder))
            Cameras.Add(c);
    }

    [RelayCommand]
    private async Task SaveCredentialsAsync()
    {
        if (string.IsNullOrWhiteSpace(AppId) || string.IsNullOrWhiteSpace(AppSecret))
        {
            SetStatus("AppId si AppSecret sunt obligatorii", error: true);
            return;
        }

        IsBusy = true;
        SetStatus(null);
        try
        {
            var c = new ImouCredentials { AppId = AppId.Trim(), AppSecret = AppSecret.Trim(), Region = Region };
            var token = await _cloud.GetTokenAsync(c);
            await _creds.SetImouCredentialsAsync(c);
            TokenStatus = $"Token valid pana la {token.ExpiresAt:dd MMM HH:mm}";
            SetStatus("Credentiale salvate. Conexiune OK.");
        }
        catch (Exception ex)
        {
            SetStatus($"Eroare: {ex.Message}", error: true);
        }
        finally { IsBusy = false; }
    }

    [RelayCommand]
    private async Task DiscoverCamerasAsync()
    {
        IsBusy = true;
        IsDiscovering = true;
        DiscoveredItems.Clear();
        SetStatus(null);

        try
        {
            var devices = await _cloud.DiscoverDevicesAsync();
            if (devices.Count == 0)
            {
                SetStatus("Nu s-au gasit camere in contul IMOU", error: true);
                return;
            }

            var existing = (await _repo.GetAllAsync()).Select(c => c.ImouDeviceId).ToHashSet();
            foreach (var d in devices)
            {
                DiscoveredItems.Add(new DiscoveryItemVm(d)
                {
                    IsSelected     = !existing.Contains(d.DeviceId),
                    CustomName     = d.Name,
                    AlreadyImported = existing.Contains(d.DeviceId)
                });
            }
        }
        catch (Exception ex)
        {
            SetStatus($"Discovery esuat: {ex.Message}", error: true);
            IsDiscovering = false;
        }
        finally { IsBusy = false; }
    }

    [RelayCommand]
    private async Task ImportSelectedAsync()
    {
        IsBusy = true;
        SetStatus(null);
        int imported = 0;

        try
        {
            var existing = await _repo.GetAllAsync();
            int nextOrder = existing.Count;

            foreach (var item in DiscoveredItems.Where(i => i.IsSelected && !i.AlreadyImported))
            {
                var camera = new CameraConfig
                {
                    ImouDeviceId = item.Device.DeviceId,
                    Name         = string.IsNullOrWhiteSpace(item.CustomName)
                                     ? item.Device.Name : item.CustomName.Trim(),
                    LocalIp      = item.Device.LocalIp,
                    DisplayOrder = nextOrder++,
                    IsEnabled    = true
                };
                await _repo.UpsertAsync(camera);

                if (!string.IsNullOrEmpty(item.SafetyCode))
                    await _creds.SetCameraSafetyCodeAsync(camera.Id, item.SafetyCode);

                imported++;
            }

            IsDiscovering = false;
            SetStatus($"{imported} camere importate.");
            await LoadAsync();
        }
        catch (Exception ex)
        {
            SetStatus($"Import esuat: {ex.Message}", error: true);
        }
        finally { IsBusy = false; }
    }

    [RelayCommand]
    private async Task RefreshIpsAsync()
    {
        IsBusy = true;
        SetStatus("Actualizare IP-uri...");
        try
        {
            var ips  = await _cloud.GetDeviceLocalIpsAsync();
            var cams = await _repo.GetAllAsync();
            foreach (var cam in cams)
            {
                if (ips.TryGetValue(cam.ImouDeviceId, out var ip) && ip != cam.LocalIp)
                {
                    cam.LocalIp = ip;
                    await _repo.UpsertAsync(cam);
                }
            }
            SetStatus("IP-uri actualizate.");
            await LoadAsync();
        }
        catch (Exception ex)
        {
            SetStatus($"Eroare actualizare: {ex.Message}", error: true);
        }
        finally { IsBusy = false; }
    }

    [RelayCommand]
    private async Task DeleteCameraAsync(CameraConfig camera)
    {
        await _repo.DeleteAsync(camera.Id);
        await _creds.RemoveCameraSafetyCodeAsync(camera.Id);
        Cameras.Remove(camera);
    }

    [RelayCommand]
    private void CancelDiscovery()
    {
        IsDiscovering = false;
        DiscoveredItems.Clear();
    }

    private void SetStatus(string? msg, bool error = false)
    {
        StatusMessage = msg;
        IsError = error;
    }
}

public partial class DiscoveryItemVm : ObservableObject
{
    public ImouDiscoveredDevice Device { get; }

    [ObservableProperty] private bool _isSelected = true;
    [ObservableProperty] private string _customName = string.Empty;
    [ObservableProperty] private string _safetyCode = string.Empty;
    public bool AlreadyImported { get; set; }

    public DiscoveryItemVm(ImouDiscoveredDevice device) => Device = device;

    public string StatusBadge => AlreadyImported ? "IMPORTAT" : (Device.IsOnline ? "ONLINE" : "OFFLINE");
    public string IpText => string.IsNullOrEmpty(Device.LocalIp) ? "IP necunoscut" : Device.LocalIp;
}
